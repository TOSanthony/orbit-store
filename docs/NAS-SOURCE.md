# Source « Mon NAS » (Synology)

Orbit lit un fichier `index.json` hébergé sur ton NAS et propose ces jeux comme une source de plus.
Les téléchargements passent par les mêmes contrôles que les autres sources (HTTPS, Range, taille, ETag/Last-Modified).

## 1. Préparer le dossier
1. Crée un dossier avec un nom long et aléatoire dans le dossier web de Web Station, par ex. `/volume1/web/orbit-3f9a7c1e/`. Ce nom sert de secret : ne le partage pas.
2. Copie-y tes jeux (`.exfat`, `.ffpfsc`, `.pkg`, `.zip`). Le ID du jeu (`PPSA01234` ou `CUSA01234`) doit apparaître dans le chemin du fichier.
3. Génère l'index (Python 3 requis sur le NAS ou sur un PC qui voit le partage) :
   `python3 tools/make-nas-index.py /volume1/web/orbit-3f9a7c1e`
   Option `--sha256` pour enregistrer les empreintes. Relance la commande à chaque ajout de jeu.
   Un `titles.json` optionnel permet de corriger titre, version, titleId, jaquette.

## 2. Servir en HTTPS
Web Station (ou le reverse proxy) doit servir le dossier avec :
- `Content-Length` exact, requêtes `Range` (206), et `Last-Modified` ou `ETag` fort ;
- pas de redirection ;
- un domaine qui résout vers une IP publique vue depuis la PS5 (Orbit refuse les IP privées/loopback).
Désactive la compression sur ces fichiers et ne mets aucun mot de passe : l'accès repose sur le chemin secret.

## 3. Dans Orbit
Sources → coche « My NAS » → saisis `https://ton.domaine/orbit-3f9a7c1e/index.json` → Enregistrer.
Les liens de l'index doivent être sur le même domaine/port que l'adresse saisie. L'index est relu toutes les 30 min.

## Limites
- 512 jeux max par index, index de 4 Mio max.
- `.7z`/`.rar` : contenu non vérifié (format « Unknown »).
- Orbit ne dévoile jamais le chemin enregistré dans l'interface (seul le nom d'hôte est affiché).

## Images
Mets les images dans un sous-dossier `covers/` à côté d'`index.json`, nommées d'après l'identifiant du jeu :
- `covers/PPSA01234_icon0.png` : l'icône (cover) ;
- `covers/PPSA01234_pic1.png` : le fond (hero). Sans fond, Orbit affiche un fond généré.
(.png, .jpg, .jpeg ou .webp ; majuscules/minuscules indifférentes.) Le script les détecte tout seul à chaque génération ;
`--covers autre-dossier` change le dossier. Un `cover`/`hero` écrit dans `titles.json` reste prioritaire.
Les images doivent être sur le même domaine que l'index.
