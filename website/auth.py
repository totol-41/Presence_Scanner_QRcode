from zeep import Client
from zeep.transports import Transport
from requests import Session
from requests.auth import HTTPBasicAuth
from requests.exceptions import HTTPError
import re
import os

REAL_BASE = os.environ.get("REAL_BASE", "https://urlSiteHyperplanningAPI.com") #Pour le dev local veuillez remplacer par la bonne valeur
WSDL_URL = REAL_BASE+"hpsw/wsdl"

# Identifiants admin (pour interroger la base)
ADMIN_LOGIN = os.environ.get("ADMIN_LOGIN", "admin") #Pour le dev local veuillez remplacer par la bonne valeur
ADMIN_PASS = os.environ.get("ADMIN_PASS", "motdepasse") #Pour le dev local veuillez remplacer par la bonne valeur

def patch_addresses(client):
    for service in client.wsdl.services.values():
        for port in service.ports.values():
            old_address = port.binding_options['address']
            path = re.sub(r'^https?://[^/]+', '', old_address)
            port.binding_options['address'] = REAL_BASE + path

def recuperer_nom_prenom(login_etudiant):
    """Récupère nom/prénom via un compte admin."""
    session = Session()
    session.auth = HTTPBasicAuth(ADMIN_LOGIN, ADMIN_PASS)
    transport = Transport(session=session)

    client = Client(WSDL_URL, transport=transport)
    patch_addresses(client)
    etudiants_service = client.bind('HpSvcWDonnees', 'PortEtudiants')

    tous_etudiants = etudiants_service.TousLesEtudiants()
    for etu_id in tous_etudiants:
        login = etudiants_service.IdentifiantConnexionEtudiant(etu_id)
        if login == login_etudiant:
            nom = etudiants_service.NomEtudiant(etu_id)
            prenom = etudiants_service.PrenomEtudiant(etu_id)
            return prenom, nom, etu_id
    return None