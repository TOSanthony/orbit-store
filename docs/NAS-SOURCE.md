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

### Extraction depuis les .zip
Pour un jeu en `.zip`, le script cherche `icon0.png` (icône) et le fond dans l'archive (`pic0.png` s'il existe, sinon `pic1.png` ; il est enregistré sous `_pic1.png`), de préférence sous `sce_sys/`, et les copie dans `covers/<TITLEID>_icon0.png` et `covers/<TITLEID>_pic1.png`.

**Chargement rapide.** Les PNG extraits pèsent plusieurs Mo (le fond 1920×1080 surtout). Si Python dispose de Pillow (`pip install pillow`), le script écrit à côté une copie `.jpg` allégée (icône 512 px, fond 1920×1080, environ 10× plus légère) et l'index pointe dessus ; les PNG restent. Sans Pillow, tout fonctionne mais les images sont lentes à arriver sur la console. `--no-optimize` désactive la copie.
- Seuls les vrais PNG de 20 Mo maximum sont retenus ; une image déjà présente dans `covers/` n'est jamais écrasée (option `--refresh-covers` pour la remplacer).
- `--no-extract` désactive cette étape.
- L'utilisateur PHP (`http`) doit pouvoir écrire dans `covers/` (ou dans le dossier racine pour le créer).
- Cela ne fonctionne que si le zip contient réellement ces fichiers (zip d'un dossier de jeu) ; un zip contenant seulement un `.exfat` ou `.ffpfsc` n'en a pas, ce contenu n'étant pas lu.

## Interface web d'administration (`tools/make-nas-index.php`)
Page protégée par mot de passe : bibliothèque façon console (icônes, fonds, fiche détaillée, recherche, filtres) et bouton « Actualiser » qui relance `make-nas-index.py` et recrée `index.json`.

1. Copie `make-nas-index.php` et `make-nas-index.py` dans le dossier d'`index.json` (ou règle `$GAMES_DIR`).
2. Génère l'empreinte du mot de passe (10 caractères minimum), en SSH sur le Synology :
   `php make-nas-index.php hash`
   puis colle-la dans `$PASSWORD_HASH` (ou dans un fichier `nas-admin.config.php` à côté, qui survit aux mises à jour :
   `<?php return ['PASSWORD_HASH' => '$2y$...', 'PYTHON' => '/usr/bin/python3'];`).
3. Règle `$PYTHON`, puis ouvre l'adresse de `make-nas-index.php` dans ton navigateur.

Sécurité : sans connexion, seule une page de mot de passe vide s'affiche (aucune information sur les jeux, images comprises) ; sans empreinte configurée, la page répond « Non configuré » ; 5 mots de passe faux bloquent l'adresse 15 min ; session de 2 h d'inactivité, cookies HttpOnly/SameSite, jeton CSRF sur chaque action, images servies uniquement depuis le dossier des jeux.
Derrière un reverse proxy, le blocage s'applique à l'adresse du proxy (donc à tout le monde) : c'est volontaire, mais à savoir.
Le mot de passe protège cette page d'administration, pas l'accès d'Orbit à `index.json` et aux jeux, qui reste protégé par le chemin secret.
En ligne de commande : `php make-nas-index.php refresh [--sha256] [--refresh-covers]` (utile pour une tâche planifiée Synology).

## Informations des jeux : param.json et IA
- **param.json (par défaut)** : pour un `.zip` contenant `sce_sys/param.json` (jeu PS5), le script lit le titre (`titleName`), l'identifiant (`titleId`) et la version (`contentVersion`). Un zip dont le nom ne contient pas l'identifiant est alors quand même listé. Si l'identifiant du `param.json` diffère de celui du chemin, le `param.json` est ignoré (avertissement dans le journal). `--no-param` désactive cette lecture ; la case « Lire le param.json » existe aussi dans l'interface.
- **IA (Gemini, facultative)** : pour tous les jeux, `--ai` demande à Gemini de remplir ce qui manque encore : description et accroche en français, éditeur, genre (noms anglais, séparés par « / »), date de sortie (AAAA-MM-JJ). Il ne renomme que les jeux dont le titre vient du nom de fichier (`param.json` et `titles.json` restent prioritaires). `addedAt` reprend la date de modification du fichier. L'IA n'invente pas : si le jeu n'est pas reconnu, rien n'est écrit (nouvel essai après 7 jours). Les réponses sont gardées dans `.ai-cache.json` (un jeu n'est demandé qu'une fois), `--ai-max` limite les requêtes par exécution (40 par défaut).
- **Clés** : `GEMINI_API_KEYS` (séparées par des virgules) pour le script ; avec l'interface PHP, dans `nas-admin.config.php` :
  `<?php return ['PASSWORD_HASH' => '...', 'GEMINI_KEYS' => ['clé1', 'clé2'], 'GEMINI_MODEL' => 'gemini-3.6-flash'];`
  La case « Compléter avec l'IA » n'apparaît que si une clé est configurée. Les clés ne passent jamais par la ligne de commande ni par l'index, et `nas-admin.config.php` est bloqué par le `.htaccess`. Ne le publie pas sur GitHub.
- **Vie privée** : le Title ID et le titre déduit du nom de fichier sont envoyés à Google. Les priorités sont : `titles.json` > `param.json` > IA > nom de fichier.

### Actualisation longue (« Gateway Timeout »)
Sur l'interface web, « Actualiser » lance Python **en arrière-plan** et la page affiche l'avancement (rafraîchi toutes les 2,5 s), puis le journal à la fin : un reverse proxy qui coupe les pages au bout de 60 s ne pose donc plus de problème, et tu peux fermer l'onglet. Une seule actualisation à la fois ; délai maximal `$TIMEOUT` (3600 s par défaut). Prérequis : `proc_open` autorisé et, pour le délai, la commande `timeout` (présente sur DSM). L'index est écrit dans un fichier temporaire puis renommé, donc Orbit ne lit jamais un index à moitié écrit.

## Fiche d'un jeu (format de l'index)
Chaque jeu est généré comme une ligne du catalogue officiel :
| Champ | Valeur |
|---|---|
| `id` | `ppsa05366-nas` (`-<format>` si un même jeu a plusieurs fichiers) |
| `gameId`, `titleId` | l'identifiant du jeu, ex. `PPSA05366` |
| `sizeBytes`, `format`, `filename`, `version`, `sha256` | taille, format, nom du fichier, version (param.json ou nom de fichier), empreinte (`""` sans `--sha256`) |
| `provider`, `sourceId` | `nas` |
| `title`, `genre`, `tagline`, `description`, `publisher`, `releaseDate`, `region` | titre (param.json), puis IA : genre (noms du PlayStation Store, séparés par « / »), accroche de 2 à 4 mots, description, éditeur, date de sortie `AAAA-MM-JJ`, région `USA`/`EUR`/`JPN`/`ASIA` |
| `cover`, `hero` | `covers/<ID>_icon0.png` et `_pic1.png` (ou extraits du zip) ; à défaut les images par défaut |
| `coverFallback`, `heroFallback` | `assets/coverFallback.png` et `assets/heroFallback.png` (créés automatiquement à côté de `index.json` s'ils n'existent pas ; remplace-les par les tiens, ils ne sont jamais écrasés) |
| `artworkLayout` | `wide` |
| `addedAt` | date de modification du fichier du jeu |
| `platform` | `ps5` (PPSA) ou `ps4` (CUSA) ; un jeu rangé dans le mauvais dossier PS4/PS5 est signalé, car Orbit déduit la plateforme de l'identifiant |
| `sourceCheckedAt` | date de génération de l'index |
| `consoleVerified`, `browserAvailable`, `directAvailable` | `false`, `false`, `true` |

La langue des descriptions est celle du catalogue officiel (anglais) ; pour le français, ajoute `'AI_LANG' => 'français'` à `nas-admin.config.php` (ou `--ai-lang`). La région est une estimation de l'IA : vérifie-la ou corrige-la dans `titles.json` (`"region": "EUR"`).
