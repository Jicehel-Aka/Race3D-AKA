# Ajouter et éditer des circuits — AKA Turbo 3D Grand Prix

Ce guide explique comment sont faits les circuits, comment en créer un nouveau
(avec l'éditeur ou à la main), et comment le tester. Il répond aussi à la question
« y a-t-il des niveaux de difficulté ? » (oui : voir la dernière section).

- [1. Comment un circuit est fait](#1-comment-un-circuit-est-fait)
- [2. Ajouter un circuit en 4 étapes](#2-ajouter-un-circuit-en-4-étapes)
- [3. L'éditeur](#3-léditeur)
- [4. Écrire un circuit à la main (format .trk)](#4-écrire-un-circuit-à-la-main-format-trk)
- [5. Les valeurs](#5-les-valeurs)
- [6. Recettes](#6-recettes)
- [7. Conseils pour un bon circuit](#7-conseils-pour-un-bon-circuit)
- [8. Tester sur PC](#8-tester-sur-pc)
- [9. Limites](#9-limites)
- [10. Dépannage](#10-dépannage)
- [11. Difficulté et IA](#11-difficulté-et-ia)

---

## 1. Comment un circuit est fait

Ce n'est **pas une carte** (pas de tuiles, pas d'image) : c'est une **suite de segments
de route**, comme dans les jeux d'arcade type *OutRun*. Chaque segment fait 200 unités
de long (≈ 1,1 m à l'échelle du jeu) et ne porte que deux informations :

| Information | Rôle |
|---|---|
| **courbure** | la route tourne à droite (valeur positive) ou à gauche (négative) |
| **altitude** | hauteur du début et de la fin du segment (collines, bosses) |

Un circuit de 2 500 à 3 000 segments donne un tour de 45 à 55 secondes.
Tout le reste est **calculé automatiquement** à partir de ces segments et de quelques réglages :

- le **décor** (palmiers, pins, immeubles, panneaux, chevrons dans les virages serrés,
  portique de départ) selon le *thème* et une *graine* aléatoire ;
- la **mini-carte** (le tracé est refermé automatiquement, il est donc toujours une boucle) ;
- les **silhouettes du fond** (montagnes, skyline), selon le thème et une seconde graine ;
- le **comportement des pilotes IA** (ils ralentissent avant les virages serrés).

Un circuit est donc un petit **texte** : une ligne par morceau de route. Exemple :

```
straight MEDIUM          # départ
curve MEDIUM HARD        # virage serré à droite
straight SHORT LOW       # petite montée
curve LONG -EASY         # grand virage doux à gauche
```

Les circuits d'origine (Sunset Coast, Alpine Pass, Neon City) sont écrits dans ce même
format : leur résultat est identique, segment pour segment, à celui de la version 1.0.

## 2. Ajouter un circuit en 4 étapes

1. **Dessiner** : ouvrez `tools/track_editor.html` dans un navigateur (double-clic, rien
   à installer, fonctionne hors ligne). Partez d'un exemple ou de « Nouveau ».
2. **Enregistrer** : bouton *Télécharger le .trk*, puis placez le fichier dans le dossier
   `tracks/`. Le début du nom fixe l'ordre dans le menu :
   `06_mon_circuit.trk` vient après `05_night_harbor.trk`.
3. **Générer** : à la racine du projet, lancez
   ```
   python tools/gen_tracks.py
   ```
   (sous Windows, `py tools/gen_tracks.py` fonctionne aussi). Le script vérifie les fichiers,
   réécrit `main/game/tracks_data.cpp` et met à jour `TRACK_COUNT` dans `main/game/track.h`.
4. **Compiler** comme d'habitude (`idf.py build`), ou pour le PC voir la section 8.

> `tracks_data.cpp` est généré : ne le modifiez pas à la main, vos changements seraient
> écrasés. La source de vérité, ce sont les fichiers de `tracks/`.

Pour **modifier** un circuit existant : ouvrez son `.trk` dans l'éditeur (bouton *Ouvrir un
.trk* ou glisser-déposer), retouchez, enregistrez par-dessus, relancez `gen_tracks.py`.

## 3. L'éditeur

`tools/track_editor.html` est un seul fichier autonome. Il contient le même moteur de route
que le jeu : ce que vous voyez est ce que le jeu construira.

**À gauche**
- *Circuit* : nom (14 caractères max), thème, musique, deux graines (🎲 pour en tirer une
  autre : elles ne changent que le décor et le fond, jamais la route).
- *Tracé* : la liste des morceaux de route. Chaque ligne a ses paramètres, une note
  facultative, et les boutons ▲ ▼ (déplacer), ⧉ (dupliquer), ✕ (supprimer). Les boutons
  « + Virage serré », « + Chicane », « + Colline »… ajoutent un morceau prêt à l'emploi
  après la ligne sélectionnée ; le menu *Sens des virages* choisit droite ou gauche.
- *Texte du circuit* : le contenu du `.trk`. On peut y écrire directement ou y coller un
  circuit ; il est relu quand on quitte la zone.

**À droite**
- **Aperçu 3D** : la route vue comme dans le jeu (projection, collines, virages, décor,
  fond). *Rouler* fait avancer la caméra ; le curseur permet de se déplacer sur le tour.
- **Éditer en 3D** : cliquez sur la route dans l'aperçu pour sélectionner le tronçon
  correspondant, puis réglez-le avec les boutons (ou le clavier quand l'aperçu est actif) :

  | Touche | Effet |
  |---|---|
  | ← / → | courbure vers la gauche / la droite |
  | ↑ / ↓ | pente : monte / descend |
  | + / − | longueur du tronçon |
  | Espace | rouler / pause |

  Une ligne droite à laquelle on ajoute de la courbure devient un virage de même longueur.
  Tout est annulable avec *Ctrl+Z* (et *Ctrl+Y* pour refaire).
- **Plan** : le tracé vu de dessus, coloré par difficulté (gris : droite, vert : doux,
  jaune : moyen, rouge : serré). Le tronçon sélectionné est en jaune. Cliquez pour
  déplacer la caméra.
- **Profil** : altitude (en haut) et courbure (en bas) sur tout le tour.
- **Statistiques et diagnostic** : nombre de segments, longueur du tour, **temps au tour
  estimé d'un pilote expert** (précis à environ ±1 s, mesuré sur les 6 circuits fournis),
  altitude maximale, virages serrés, et la liste des problèmes (voir section 7).

Le travail est sauvegardé automatiquement dans le navigateur ; le fichier `.trk`
téléchargé reste la vraie sauvegarde.

*Limites de l'aperçu :* les sprites (arbres, immeubles…) sont dessinés en formes simples
et les voitures ne sont pas affichées ; en revanche leur position est exactement celle du jeu.

## 4. Écrire un circuit à la main (format .trk)

Un fichier `.trk` est du texte. Les lignes `name`, `theme`, `music`, `seed`, `bgseed`
décrivent le circuit ; les autres lignes sont les morceaux de route, dans l'ordre du tour.
`#` commence un commentaire.

```
# Une phrase de description (facultatif)
name PALM BAY            # nom affiché dans le menu : 1 à 14 caractères A-Z 0-9 espace ' -
theme coast              # coast | alpine | city
music 0                  # 0 SUNSET DRIVE, 1 SUMMIT RUSH, 2 NEON NIGHTS
seed 4217                # graine du décor (changez-la pour mélanger arbres et panneaux)
bgseed 58                # graine du fond (montagnes / skyline)

straight MEDIUM
curve SHORT MED
road 10 10 10 -HARD 0
```

### Les commandes

| Commande | Effet |
|---|---|
| `straight LONG [PENTE]` | ligne droite, avec une pente facultative |
| `curve LONG COURBE [PENTE]` | virage : la courbure monte, se maintient, puis redescend |
| `road ENTRÉE MAINTIEN SORTIE COURBE [PENTE]` | la commande complète, avec trois longueurs séparées |
| `scurves COURBE` | série de 5 virages en S (750 segments) |
| `bumps` | série de 8 petites bosses (240 segments) |
| `downhill LONG` | descente douce qui ramène la route à l'altitude 0 |

`straight` et `curve` sont des raccourcis de `road` : `curve LONG c p` équivaut à
`road LONG LONG LONG c p`, donc **dure 3 × LONG segments**.

Dans `road`, la courbure monte progressivement pendant l'**entrée**, reste constante
pendant le **maintien**, puis revient à zéro pendant la **sortie**. Un virage serré court
s'écrit donc par exemple `road 10 10 10 HARD 0` (30 segments), une longue courbe
`road 40 60 40 MED 0`.

Plusieurs commandes peuvent être séparées par `;` sur une même ligne.

## 5. Les valeurs

Chaque valeur s'écrit avec un **nom** ou un **nombre**. Un `-` devant inverse le sens.

| Type | Noms | Nombre | Remarque |
|---|---|---|---|
| Longueur (en segments) | `SHORT` = 25, `MEDIUM` = 50, `LONG` = 100 | 0 à 400 | |
| Courbure | `EASY` = 2, `MED` = 4, `HARD` = 6 | −12 à 12 | positif = droite, négatif = gauche (`-HARD`) |
| Pente (hauteur gagnée sur la commande) | `LOW` = 20, `MED` = 40, `HIGH` = 60 | −200 à 200 | positif = montée, négatif = descente (`-MED`) |

Quelques repères :
- une pente de `LOW` sur 150 segments est à peine perceptible, `HIGH` sur 75 segments est
  une vraie côte ;
- `HARD` à pleine vitesse ne se passe pas sans freiner (à pleine vitesse la force qui pousse
  vers l'extérieur vaut 3,6 demi-largeurs de route par seconde, contre 2 pour le braquage maximal) ;
- une chicane de 8-10 segments par virage se passe presque à fond, en braquant franchement.

## 6. Recettes

```
# Chicane (gauche, droite, gauche) : 3 petites commandes de 24 segments
road 8 8 8 -HARD 0
road 8 8 8 HARD 0
road 8 8 8 -HARD 0

# Épingle à droite (450 segments)
curve LONG HARD

# Une pente est un gain d'altitude durable : la route reste en haut tant qu'on ne redescend pas.
# Pour une colline (bosse), il faut donc monter puis redescendre :
road SHORT SHORT SHORT 0 MED
road SHORT SHORT SHORT 0 -MED

# Montée régulière puis descente
straight MEDIUM LOW
straight MEDIUM -LOW

# Crête aveugle : la route monte vite et plonge juste derrière (plus la pente est forte
# et la longueur courte, plus la crête est marquée)
road SHORT SHORT SHORT 0 HIGH
road SHORT SHORT SHORT 0 -HIGH

# Fin de tour obligatoire : retour à l'altitude 0, puis une droite pour la grille
downhill MEDIUM
straight SHORT
```

## 7. Conseils pour un bon circuit

- **Départ et arrivée en ligne droite.** Les 8 voitures partent en grille derrière la ligne,
  sur les ~25 derniers segments du tour : ils doivent être droits et plats. Commencez et
  terminez par une `straight` d'au moins `SHORT`.
- **Altitude 0 à la fin.** Terminez par `downhill` (puis une droite) ou par une descente que vous
  écrivez vous-même. Si le circuit finit à plus de 20 unités d'altitude (0,1 hauteur de segment),
  le jeu ajoute de lui-même une descente `downhill MEDIUM` pour que la boucle reste continue ; l'éditeur
  le signale. En dessous de ce seuil, l'écart est invisible et rien n'est ajouté.
- **Durée :** 2 300 à 3 000 segments. En dessous de 1 800 le tour paraît trop court.
- **Rythme :** alternez rapide / technique. Un virage `HARD` est suivi d'une respiration
  (ligne droite ou virage doux). Évitez plus de 9 virages serrés.
- **Premiers virages faciles :** le départ se joue à huit, évitez un `HARD` dans les
  200 premiers segments.
- **Pentes :** `road 10 10 10 0 60` (30 segments pour 60 de dénivelé) est un mur. Gardez
  la pente par segment inférieure à 1.
- **Variété :** réutilisez le même thème sur deux circuits en changeant les graines et la
  disposition ; la musique et le décor suivent le thème.

Le diagnostic de l'éditeur signale automatiquement : lignes invalides, nom invalide, grille
non droite ou non plane, circuit trop court ou trop long, pente excessive, altitude très
élevée, trop de virages serrés.

## 8. Tester sur PC

La version PC (`host/`) compile les mêmes sources que la console (voir `README.md`) :

```
g++ -O2 -std=c++17 -I main host/host_main.cpp main/game/*.cpp -o host/race3d.exe -lmingw32 -lSDL2main -lSDL2
host/race3d.exe
```

Le menu propose tous les circuits (gauche/droite sur *TRACK*). Pour juger un circuit,
jouez-le au moins en *Normal* et en *Expert* : si l'IA sort de la route ou se bloque,
c'est que le tracé est trop dur.

Pour vérifier un circuit sans le jouer, la simulation IA de `race.cpp` fait tourner les 8 pilotes
et mesure leurs temps. Les valeurs de référence (meilleur tour en *Hard*) :

| Circuit | Thème | Segments | Meilleur tour IA (Hard) | Caractère |
|---|---|---|---|---|
| Sunset Coast | côte | 2 950 | 51,5 s | grands virages rapides le long de la mer |
| Alpine Pass | montagne | 2 715 | 49,0 s | grosses côtes et épingles |
| Neon City | ville | 2 625 | 46,2 s | longs boulevards, virages serrés |
| Palm Bay | côte | 2 529 | 44,9 s | technique : esses, épingle en montée, front de mer |
| Glacier Run | montagne | 2 665 | 47,4 s | montagnes russes : crêtes, bosses, longue descente |
| Night Harbor | ville | 2 806 | 49,2 s | nuit, docks : grandes droites, pont, chicane finale |

Un circuit d'environ 45 à 52 s, sans sortie de route de l'IA, est dans la norme.

## 9. Limites

| Élément | Limite |
|---|---|
| Nombre de circuits | 16 (`gen_tracks.py` refuse davantage) |
| Longueur d'un circuit | 3 600 segments (la fin est ignorée au-delà) |
| Nom | 14 caractères : A-Z, 0-9, espace, `'`, `-` |
| Thèmes | 3 : `coast`, `alpine`, `city` (couleurs, ciel, décor) |
| Musiques | 3 : choisies avec `music 0..2` |
| Pente | ±200 par commande ; courbure ±12 |

Ajouter un **nouveau thème** (nouvelle palette, nouveau ciel, nouveaux sprites) ou une **nouvelle
musique** demande de modifier le code (`g_themes` dans `track.cpp`, `render.cpp`, `sprites.cpp`,
`music.cpp`) : ce n'est pas couvert par le format `.trk`.

**Records :** ils sont enregistrés par circuit. Le fichier de records de la version 1.0
(3 circuits) est relu et converti automatiquement : les temps existants sont conservés.
Ajouter ou retirer un circuit ne casse pas les records des autres, tant que l'ordre des fichiers
de `tracks/` ne change pas : **ajoutez toujours les nouveaux circuits à la fin** (numéro supérieur).

## 10. Dépannage

| Symptôme | Cause probable |
|---|---|
| `gen_tracks.py` : « name must be 1-14 characters » | nom trop long ou avec accent / caractère interdit |
| `gen_tracks.py` : « unknown command » | faute de frappe dans une commande (voir section 4) |
| erreur de compilation `static_assert … TRACK_COUNT` | `tracks_data.cpp` modifié à la main : relancez `gen_tracks.py` |
| une ligne ne fait rien en jeu | elle est invalide (rouge dans l'éditeur) : le jeu ignore les lignes qu'il ne comprend pas |
| le circuit se termine par un saut visible | altitude finale ≠ 0 : ajoutez `downhill` en fin de circuit |
| les voitures de départ sont dans un virage | début ou fin du tour pas droit |
| l'IA sort de la route | virages `HARD` enchaînés trop près ou chicane trop agressive |
| le menu affiche les circuits dans le mauvais ordre | l'ordre vient du **nom de fichier** : `00_`, `01_`… |
| les records d'un circuit ont disparu | l'ordre des fichiers a changé après publication |

## 11. Difficulté et IA

Le jeu a **4 niveaux de difficulté** (menu *DIFFICULTY*) : *Easy*, *Normal*, *Hard*, *Expert*.
Ils règlent les 7 pilotes IA (définis dans `kDiff`, `race.cpp`) :

| Niveau | Vitesse max IA* | Habileté | Erreurs** | Rattrapage*** | Accélération |
|---|---|---|---|---|---|
| Easy | 78 – 87 % | 0,15 – 0,45 | 0,30 | ± 6 % | 0,70 |
| Normal | 86 – 95 % | 0,40 – 0,70 | 0,15 | ± 7 % | 0,85 |
| Hard | 92 – 99 % | 0,60 – 0,88 | 0,06 | ± 8 % | 0,97 |
| Expert | 97 – 104 % | 0,82 – 1,00 | 0,02 | ± 10 % | 1,06 |

\* par rapport à la voiture du joueur, sans nitro (le joueur peut aller jusqu'à +28 % avec le nitro).
L'*habileté* règle la trajectoire, l'anticipation des virages et les réflexes.
\** probabilité d'un écart de trajectoire par seconde, pour un pilote peu habile.
\*** « rubber band » : les voitures en retard sur le joueur accélèrent jusqu'à ce pourcentage, celles qui
le devancent ralentissent d'autant.

Les records de course sont enregistrés par circuit **et** par difficulté.
