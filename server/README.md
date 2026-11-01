# Serveur de course en ligne

Serveur WebSocket (Node) pour jouer a plusieurs : chaque joueur envoie sa position, le serveur
complete avec des pilotes IA, calcule les classements et rediffuse l'etat a 20 Hz.

- `/ws/race` : WebSocket. `?mode=binary&room=CODE&platform=AKA&name=NOM` pour la console (trames binaires
  de 20 octets, voir `shared/raceProtocol.ts`), sans `mode=binary` pour les clients JSON (page web).
- `/api/health`, `/api/rooms` : etat du serveur et salons en cours (lus par la page de controle).
- A l'arrivee d'un client binaire, le serveur envoie une trame d'accueil de 6 octets
  `0xA6, place, tours, difficulte, circuit, drapeaux` (bit0 = hote), puis les snapshots `0xA5` (164 octets, octet 1 : phase bits 0-1, circuit bits 2-5, index du nombre de tours [1,2,3,5] bits 6-7).
- Un client JSON peut rejoindre en `spectator: true` : il commande le salon (circuit, tours, depart) sans prendre de voiture.

## Lancer en local

    cd server && npm install && npm start      # http://localhost:3000

## Heberger gratuitement (Render, depuis ce depot)

1. https://render.com > New > Blueprint > choisir ce depot (le fichier `render.yaml` decrit le service).
2. Attendre le premier deploiement, noter l'adresse `https://aka-turbo-race-server.onrender.com`.
3. Ouvrir la page de controle (GitHub Pages du depot) et y coller cette adresse.

Le plan gratuit s'endort apres 15 min sans activite (premier acces : ~1 min). La console doit etre
configuree avec le meme nom d'hote et un code de salon.
