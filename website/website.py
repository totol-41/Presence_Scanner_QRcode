#Importation des bibliothèque
from flask import Flask, render_template, request, jsonify #Serveur web
from werkzeug.utils import send_from_directory
import os
from dotenv import load_dotenv

from auth import recuperer_nom_prenom

# Charger les variables d'environnement depuis le fichier .env
load_dotenv()

app = Flask(__name__)

@app.route("/static/<path:path>") #Route pour les appels des pages statiques
def static_dir(path):
    return send_from_directory("static", path) #il renvoie vers le dossier corespondant

@app.route('/') #Route par défaut lorsque on arrive sur le site sans paramètre
def index():
    return render_template('index.html') #Renvoie l'index.html

@app.route('/api/etudiant')
def api_etudiant():
    login = request.args.get('login')
    if not login:
        return jsonify({"error": "Paramètre 'login' manquant"}), 400

    resultat = recuperer_nom_prenom(login)
    if resultat is None:
        return jsonify({"error": "Étudiant non trouvé"}), 404

    prenom, nom, etu_id = resultat
    return jsonify({"prenom": prenom, "nom": nom, "id": etu_id})

# lance le serveur web
print("Start Flask")
if __name__ == '__main__':
    app.run(debug=True, host='0.0.0.0', port=5400)