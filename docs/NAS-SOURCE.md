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
- **Tes propres images d'abord** : si `covers/` contient déjà `<ID>_icon0.jpg`, `.jpeg` ou `.webp` (et `<ID>_pic1.…` pour le fond) que tu as mises toi-même, le script les utilise telles quelles : il n'extrait rien du zip pour ce jeu et ne les réécrit jamais, même avec `--refresh-covers`. Les `.jpg` allégés qu'il crée lui-même portent une marque interne, ce qui permet de les distinguer des tiens.
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
  Pour chaque jeu, les clés sont essayées dans l'ordre : si l'une est refusée ou a épuisé son quota (HTTP 429), la suivante prend le relais, et la clé épuisée n'est plus demandée pendant le reste du lancement.
  La case « Compléter avec l'IA » n'apparaît que si une clé est configurée. Les clés ne passent jamais par la ligne de commande ni par l'index, et `nas-admin.config.php` est bloqué par le `.htaccess`. Ne le publie pas sur GitHub.
- **Vie privée** : le Title ID et le titre déduit du nom de fichier sont envoyés à Google. Les priorités sont : `titles.json` > `param.json` > IA > nom de fichier.

### Actualisation longue (« Gateway Timeout »)
L'interface PHP reprend celle de l'app TV : menu du haut (Découvrir, Jeux, Applications, DLC, Mises à jour), Découvrir avec bannière, rangées « Nouveautés » et « Dernières sorties » puis tous les éléments, fiche avec la liste des fichiers, recherche sur tous les types. Pas de Bibliothèque ni de Téléchargements : elle sert à administrer l'index, pas à télécharger.

En haut à droite, la roue dentée ouvre les **Réglages** : dernières versions de l'app TV et de l'ELF publiées sur ton GitHub (lues dans les flux `payloads.json` et `tv-app.json` de la branche `updates`, mises en cache 10 min, bouton « Vérifier maintenant »), options et bouton « Actualiser » de l'index, et Déconnexion. Le dépôt se règle avec `UPDATE_REPO` (et `UPDATE_BRANCH`) dans `nas-admin.config.php` ; par défaut `TOSanthony/orbit-store`, branche `updates`. Le Synology doit pouvoir joindre `raw.githubusercontent.com`.

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

## Applications, DLC et mises à jour

Le menu du haut est : Découvrir, **Jeux** (l'ancien « Parcourir », uniquement les jeux), **Applications**, **DLC**, **Mises à jour**, Bibliothèque, Téléchargements. Ça vaut pour l'interface web comme pour l'app plein écran.

- **Applications :** tout fichier dont le chemin contient un dossier `apps` (ex. `packages/ps5/apps/zip/MonApp PPSA01650.zip`, `packages/ps5/apps/pkg/…`) reçoit `"tagline": "APPS"` ; il apparaît dans **Applications** *et* dans **Découvrir**, mais pas dans Jeux. Comme pour un jeu, l'identifiant PPSA/CUSA doit figurer dans le chemin ou dans le `param.json` du zip.
- **DLC et mises à jour :** uniquement dans leur propre onglet, jamais dans Découvrir ni dans Jeux.

Rangement sur le NAS (le nom du dossier `dlc` ou `updates` peut être n'importe où dans le chemin) :

```
packages/ps5/dlc/PPSA01342/EP0000-PPSA01342_00-DLC1.pkg
packages/ps5/dlc/PPSA01342/EP0000-PPSA01342_00-DLC2.pkg
packages/ps5/updates/PPSA01342/Mon jeu PPSA01342 v1.05.pkg
```

- Chaque `.pkg` devient une fiche d'index avec `"tagline": "DLC"` (ou `"UPDATES"`), `gameId` `PPSA01342-DLC` (ou `-UPDATES`) et un `id` du type `ppsa01342-dlc-ab12cd` (ou `-upd-`). Le titre, le genre, l'éditeur… sont repris du jeu lui-même s'il est dans le même index ; l'icône et le fond sont ceux du jeu (`covers/PPSA01342_icon0…`).
- Ces fiches n'apparaissent **pas** dans Découvrir ni Parcourir : seulement dans leur onglet, une carte par jeu.
- Un clic sur la carte : si le jeu a plusieurs fichiers, le bouton principal devient **Choisir un DLC** (ou **Choisir une mise à jour**). La liste montre le nom de chaque fichier ; **Tout installer (N)** les ajoute tous à la file de téléchargements (ceux déjà en file sont ignorés).
- Le nom de fichier d'un `.pkg` ne doit contenir aucun autre identifiant PPSA/CUSA que celui du dossier, et rester sous 160 caractères.

## Jeux sur un autre volume (dossier web sur le volume2, jeux sur le volume3)

Le dossier web et les jeux n'ont pas besoin d'être sur le même volume : il suffit que les jeux soient **accessibles depuis le dossier web** grâce à un lien symbolique. Le script suit les liens vers des dossiers (sans boucler).

1. Crée le lien (SSH, ou Planificateur de tâches > Créer > Script défini par l'utilisateur, exécuté en `root`, puis « Exécuter ») :
   ```
   ln -s /volume3/Jeux/packages /volume2/web/orbit-3f9a7c1e/packages
   ```
   Le lien porte le nom du dossier que tu veux voir dans l'URL (`packages` ici) et pointe vers le vrai dossier du volume3. Tu peux aussi lier un seul sous-dossier (`.../packages/ps5`).
2. Donne la lecture à l'utilisateur `http` sur le dossier partagé du volume3 (Panneau de configuration > Dossier partagé > Modifier > Permissions > utilisateurs/groupes système : `http` en lecture seule).
3. Clique sur « Actualiser » : l'index contient `packages/ps5/…` et le serveur web suit le lien pour envoyer les fichiers.

Notes : un lien ne se crée pas depuis File Station ; `index.json`, `covers/` et les scripts restent dans le dossier web (volume2). Si Web Station refuse de suivre le lien, vérifie que le profil PHP/le serveur autorise les liens symboliques et, pour PHP, ajoute `/volume3/Jeux` à `open_basedir` (Web Station > Paramètres du langage de script).

**Sans ligne de commande** : dans l'interface PHP, roue dentée > Réglages > « Dossiers de jeux sur un autre volume » > « Parcourir les volumes… ». Choisis le dossier du volume3, donne un nom au lien (par exemple `packages`) et clique sur « Créer le lien », puis sur « Actualiser ». Les liens existants sont listés avec un bouton « Retirer » (seul le lien est supprimé, jamais les fichiers).
- Le sélecteur ne navigue que dans les volumes (`LINK_ROOTS`, par défaut `/volume*`) et masque les dossiers système (`@eaDir`, `#recycle`…). Il refuse un volume entier, un dossier qui contient le dossier web, et les noms réservés (`covers`, `index.json`…).
- L'utilisateur `http` doit pouvoir lire le dossier à parcourir et écrire dans le dossier web. Si `symlink()` est refusé, le message affiche la commande `ln -s` à lancer en SSH.
- `$ALLOW_LINKS = false` (ou dans `nas-admin.config.php`) retire cette fonction.

## Base de jeux (`games-db.json`)

Option `--db games-db.json` (par défaut, `<dossier>/games-db.json` s'il existe). Cette base, produite par `tools/make-game-db.py`, contient une entrée par identifiant (`titleId`, `region`, `title`, `genre`, `tagline`, `description`, `cover`, `hero`, `releaseDate`), sans aucun lien de téléchargement. La version fournie a l'accroche et la description **en français**.

- Le script complète **genre, accroche, description, date de sortie et région** à partir de l'identifiant, avant l'étape IA (l'IA ne demande donc plus que ce qui manque).
- Ordre de priorité : `titles.json` > `param.json` du zip > prosperopatches.com / orbispatches.com > base de jeux > IA. Le titre de la base ne remplace que celui tiré du nom de fichier.
- Les URL `cover` / `hero` de la base (images distantes) ne sont pas utilisées : les images viennent de `covers/`.

## Infos et images depuis prosperopatches.com et orbispatches.com

Option `--prospero` (case cochée par défaut dans Réglages > Index des jeux) : pour chaque identifiant, le script lit la page `https://prosperopatches.com/<ID>` (jeux PS5, `PPSA…`) ou `https://orbispatches.com/<ID>` (jeux PS4, `CUSA…`) et récupère le **titre** (pour un jeu dont le titre est un logo, le texte alternatif du logo), l'**éditeur**, la **région** (EU → EUR, US → USA, JP → JPN…), l'**icône** (`icon0.webp`) et le **fond** (`pic0.webp`), enregistrés dans `covers/<ID>_icon0.webp` et `covers/<ID>_pic1.webp`.

- Ordre de priorité : `titles.json` > `param.json` du zip > prosperopatches.com > IA (Gemini, qui ne complète plus que description, genre, accroche, date de sortie). Les DLC et mises à jour gardent le titre du jeu de base.
- **Tes images passent d'abord** : si `covers/` contient déjà `<ID>_icon0` ou `<ID>_pic1` en `.jpg`/`.webp` de ta main, rien n'est téléchargé ni extrait du zip ; sinon les images du site sont préférées à l'extraction du zip (qui reste le recours si le site n'a pas le jeu).
- Réponses gardées dans `.prospero-cache.json` : une page par jeu, une seule fois (un jeu absent du site est redemandé après 7 jours, une page illisible après 1 jour). Une page par seconde, 400 pages par lancement au plus (relancer le refresh pour continuer) (`--prospero-max`). Après 3 échecs d'affilée (site injoignable ou protection anti-robot de Cloudflare), le script s'arrête pour ce lancement et le dit dans le journal.
- Le site est tiers et non officiel : le script lit ses pages comme un navigateur le ferait (son `robots.txt` l'autorise) ; s'il change sa mise en page, l'option cesse simplement de trouver des infos, sans rien casser. `--refresh-covers` retélécharge les images venant du site.
