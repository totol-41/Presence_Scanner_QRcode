from zeep import Client
from zeep.transports import Transport
from requests import Session
from requests.auth import HTTPBasicAuth
import re
from datetime import datetime, date, timezone, timedelta

REAL_BASE = "https://urlSiteHyperplanningAPI.com"
WSDL_URL = REAL_BASE+"hpsw/wsdl"
LOGIN = "admin"
PASS = "motdepasse"

session = Session()
session.auth = HTTPBasicAuth(LOGIN, PASS)
transport = Transport(session=session)
client = Client(WSDL_URL, transport=transport)

for service in client.wsdl.services.values():
    for port in service.ports.values():
        old  = port.binding_options['address']
        path = re.sub(r'^https?://[^/]+', '', old)
        port.binding_options['address'] = REAL_BASE + path

cours_service = client.bind('HpSvcWDonnees', 'PortCours')
etu_service   = client.bind('HpSvcWDonnees', 'PortEtudiants')
abs_service   = client.bind('HpSvcWDonnees', 'PortAbsences')

def trouver_cours_maintenant():
    aujourd_hui = date.today()
    maintenant  = datetime.now()  # heure locale naive, pas UTC

    tous_cours = cours_service.TousLesCoursEntre2Dates(ADate1=aujourd_hui, ADate2=aujourd_hui)
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
                duree_heures = s.Duree * 24
                fin_naive = debut_naive + timedelta(hours=duree_heures)
                if debut_naive <= maintenant <= fin_naive:
                    return (cours_id, debut_naive, fin_naive)
        except Exception:
            continue
    return None

print("=== Initialisation du cours en cours ===\n")

cours_info = trouver_cours_maintenant()
if not cours_info:
    print("Aucun cours en ce moment → rien à initialiser.")
    exit()

cours_id, heure_debut, heure_fin = cours_info
print(f"Cours trouvé : ID {cours_id}")
print(f"Plage : {heure_debut.strftime('%H:%M')} → {heure_fin.strftime('%H:%M')}\n")

etudiants = cours_service.EtudiantsDuCours(cours_id)
print(f"{len(etudiants)} étudiant(s) à marquer absents\n")

ok = 0
for etu_id in etudiants:
    try:
        nom    = etu_service.NomEtudiant(etu_id)
        prenom = etu_service.PrenomEtudiant(etu_id)
        abs_service.AjouterAbsenceEtudiant(
            AEtudiant       = etu_id,
            ADateHeureDebut = heure_debut,
            ADateHeureFin   = heure_fin,
            AJustifie       = False
        )
        print(f"  ✔ {prenom} {nom} → absent")
        ok += 1
    except Exception as e:
        print(f"  ✗ ID {etu_id} → {e}")

print(f"\n{ok}/{len(etudiants)} étudiants marqués absents.")