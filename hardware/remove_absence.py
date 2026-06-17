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

session = Session()
session.auth = HTTPBasicAuth(LOGIN, PASS)
transport = Transport(session=session)
client = Client(WSDL_URL, transport=transport)

for service in client.wsdl.services.values():
    for port in service.ports.values():
        old  = port.binding_options['address']
        path = re.sub(r'^https?://[^/]+', '', old)
        port.binding_options['address'] = REAL_BASE + path

abs_service = client.bind('HpSvcWDonnees', 'PortAbsences')
etu_service = client.bind('HpSvcWDonnees', 'PortEtudiants')

# Plage : toute la journée d'aujourd'hui
debut = datetime.combine(date.today(), datetime.min.time())
fin   = datetime.combine(date.today(), datetime.max.time().replace(microsecond=0))

print(f"=== Suppression de toutes les absences du {date.today()} ===\n")
print(f"Plage : {debut} → {fin}\n")

try:
    absences = abs_service.AbsencesEntre2Dates(
        ADateHeureDebut = debut,
        ADateHeureFin   = fin
    )
except Exception as e:
    print(f"Erreur récupération des absences : {e}")
    exit()

if not absences:
    print("Aucune absence trouvée pour aujourd'hui.")
    exit()

print(f"{len(absences)} absence(s) trouvée(s)\n")

ok = 0
erreurs = 0
for abs_id in absences:
    try:
        # Récupérer les infos de l'absence pour affichage
        etu_id     = abs_service.EtudiantAbsenceEtudiant(abs_id)
        nom        = etu_service.NomEtudiant(etu_id)
        prenom     = etu_service.PrenomEtudiant(etu_id)
        heure_deb  = abs_service.DateHeureDebutAbsenceEtudiant(abs_id)
        heure_fin  = abs_service.DateHeureFinAbsenceEtudiant(abs_id)

        abs_service.EnleverAbsenceEtudiant(
            AEtudiant       = etu_id,
            ADateHeureDebut = heure_deb,
            ADateHeureFin   = heure_fin
        )
        print(f"  ✔ {prenom} {nom} | {heure_deb.strftime('%H:%M')} → {heure_fin.strftime('%H:%M')} supprimée")
        ok += 1
    except Exception as e:
        print(f"  ✗ absence {abs_id} → {e}")
        erreurs += 1

print(f"\n=== Terminé : {ok} supprimée(s), {erreurs} erreur(s) ===")