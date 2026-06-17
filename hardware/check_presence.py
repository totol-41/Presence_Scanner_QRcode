from zeep import Client
from zeep.transports import Transport
from requests import Session
from requests.auth import HTTPBasicAuth
import re
from datetime import datetime, date, timedelta

REAL_BASE = "https://hyperplanningpstapi.lfservice.me/"
WSDL_URL = REAL_BASE+"hpsw/wsdl"
LOGIN = "admin"
PASS = "motdepasse"

# Simule le scan QR
QR_NOM    = "Bon"
QR_PRENOM = "Jean"
QR_ID_ETU = 1

session = Session()
session.auth = HTTPBasicAuth(LOGIN, PASS)
transport = Transport(session=session)
client = Client(WSDL_URL, transport=transport)

for service in client.wsdl.services.values():
    for port in service.ports.values():
        old = port.binding_options['address']
        path = re.sub(r'^https?://[^/]+', '', old)
        port.binding_options['address'] = REAL_BASE + path

cours_service = client.bind('HpSvcWDonnees', 'PortCours')
etu_service   = client.bind('HpSvcWDonnees', 'PortEtudiants')
abs_service   = client.bind('HpSvcWDonnees', 'PortAbsences')

def trouver_cours_maintenant():
    aujourd_hui = date.today()
    maintenant  = datetime.now()  # heure locale naive

    tous_cours = cours_service.TousLesCoursEntre2Dates(
        ADate1=aujourd_hui,
        ADate2=aujourd_hui
    )
    if not tous_cours:
        return None

    for cours_id in tous_cours:
        try:
            seances = cours_service.DetailDesSeancesPlaceesDuCours(cours_id)
            if not seances:
                continue
            for s in seances:
                debut = s.JourEtHeureDebut
                if not debut or debut.date() != aujourd_hui:
                    continue
                # Strip tzinfo — HyperPlanning renvoie de l'heure locale déguisée en UTC
                debut_naive = debut.replace(tzinfo=None)
                fin_naive   = debut_naive + timedelta(hours=s.Duree * 24)
                if debut_naive <= maintenant <= fin_naive:
                    return (cours_id, debut_naive, fin_naive)
        except Exception:
            continue
    return None

print(f"=== Scan QR ===")
print(f"Lu : {QR_PRENOM} {QR_NOM} (ID: {QR_ID_ETU})\n")

# Étape 0 : trouver le cours actuel
print("[0] Recherche du cours en cours...")
cours_info = trouver_cours_maintenant()
if not cours_info:
    print("    Aucun cours en ce moment")
    print("→ ROUGE : pas de cours actuellement")
    exit()

cours_id, heure_debut, heure_fin = cours_info
print(f"    Cours {cours_id} | {heure_debut.strftime('%H:%M')} → {heure_fin.strftime('%H:%M')}")

# Étape 1 : vérifier identité
print("\n[1] Vérification identité...")
try:
    nom_hp    = etu_service.NomEtudiant(QR_ID_ETU)
    prenom_hp = etu_service.PrenomEtudiant(QR_ID_ETU)
except Exception as e:
    print(f"    Étudiant introuvable : {e}")
    print("→ ROUGE : étudiant inconnu")
    exit()

if nom_hp.upper() != QR_NOM.upper() or prenom_hp.upper() != QR_PRENOM.upper():
    print(f"    Identité incorrecte (HP : {prenom_hp} {nom_hp})")
    print("→ ROUGE : identité incorrecte")
    exit()

print(f"    ✔ {prenom_hp} {nom_hp} confirmé")

# Étape 2 : récupérer et filtrer les absences par cours
print("\n[2] Vérification absence...")
try:
    absences = abs_service.AbsencesEtudiantEntre2Dates(
        AEtudiant       = QR_ID_ETU,
        ADateHeureDebut = heure_debut,
        ADateHeureFin   = heure_fin
    )
except Exception as e:
    print(f"    Erreur récupération absences : {e}")
    exit()

absences_du_cours = []
if absences:
    for abs_id in absences:
        try:
            cours_manques = abs_service.CoursManquesAbsenceEtudiant(abs_id)
            if cours_manques and cours_id in list(cours_manques):
                absences_du_cours.append(abs_id)
        except Exception as e:
            print(f"    Erreur filtrage {abs_id} : {e}")

print(f"    {len(absences_du_cours)} absence(s) pour le cours {cours_id}")

if len(absences_du_cours) == 0:
    print("    Déjà marqué présent ou pas inscrit à ce cours")
    print("→ ROUGE : déjà enregistré")
    exit()

# Étape 3 : supprimer l'absence = marquer présent
print("    Suppression de l'absence...")
try:
    abs_service.EnleverAbsenceEtudiant(
        AEtudiant       = QR_ID_ETU,
        ADateHeureDebut = heure_debut,
        ADateHeureFin   = heure_fin
    )
    print("    ✔ Présence validée")
    print(f"\n→ VERT : {QR_PRENOM} {QR_NOM} marqué présent !")
except Exception as e:
    print(f"    Erreur suppression : {e}")
    print("→ ROUGE : erreur serveur")