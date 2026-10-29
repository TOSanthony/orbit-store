# CONTEXTE — Orbit Store

Sources : `package.json`, `BUILDING.md`, `SOURCE-BUNDLE.md`, `AGENTS.md`, `THIRD-PARTY-NOTICES.md`, `app/README.md`, `app/sce_sys/param.json`, `tools/release-check.mjs`, `git log`. Tout ce qui n'est pas confirmé est marqué « à vérifier ».

## Objectif et architecture
Boutique homebrew PS5 (et jeux PS4) : catalogue, téléchargements, file d'attente, stockage (disque, NAS), mises à jour.
- **Service/payload** (C, `backend/*.c`) : tourne sur la console, serveur HTTP local (`127.0.0.1:34177`), catalogue, téléchargements, archives, file d'attente. Continue quand l'app TV est fermée.
- **App TV native** (C++, `app/src/{orbit,ui,gfx}`, rendu OpenGL GPU) : client du service ; embarque le payload et le relance via le loader ELF (`127.0.0.1:9021`) si rien ne répond.
- **Interface web** (`src/*.tsx`, React 19 + Vite 6 + TS ~5.8, paquet `orbit-store-ps5-source`, GPL-3.0-or-later) : même service, même catalogue.
- Catalogue chiffré : `catalog/{games,releases,addons,catalog}.enc`, outillage `tools/catalog.py` (+ `.venv`).
- i18n partagé web/TV : `i18n/*.json` (clé = texte anglais ; de en es fr hu it nl pl pt-BR ru tr…).
- Le bureau (desktop) est maintenu et empaqueté à part (hors de ce dépôt).

## Dossiers utiles
`backend/` service C · `app/src/orbit` logique/écrans TV · `app/src/ui`,`gfx` kit UI/rendu · `app/host/` aperçu hôte (`preview_main.cpp` scénarios, `fixture.cpp` faux backend) · `app/tests/unit` gtest · `src/` web · `tests/` tests backend/Python/C · `tools/` build, catalogue, release, i18n · `docs/` (NAS-SOURCE, UPDATES-GITHUB) · `config/network.json` · `launcher/`, `diagnostics/`, `experiments/` annexes.

## Commandes
- Web : `npm run typecheck` · `npm test` (`node --test tests/*.test.mjs`; échoue tant que i18n incomplet) · `npm run build` · `npm run catalog:check` · `npm run test:catalog` · `npm run release:check`.
- i18n : `node tools/i18n.mjs extract` (réécrit `en.json`) · `check` (nécessite `catalog/catalog.json`, absent ici → échoue).
- Tests TV (hôte) : `cd app && HOST_TEST_SANITIZERS="-fno-omit-frame-pointer" USE_CCACHE=0 bash tools/build-tests.sh && build/tests/unit_tests --gtest_brief=1` (dernier run connu : 232 réussis).
- Aperçus : `cd app && USE_CCACHE=0 [ORBIT_INTERFACE=crossbar|cinema|panorama] bash tools/preview.sh build/preview/<dir> <scénario>`.
- Syntaxe backend : `clang -std=c11 -fsyntax-only -Wall -I<.deps/cjson> -Ibackend -Ibuild/generated backend/<f>.c`.
- Build complet (Docker, voir `BUILDING.md`) : payload `orbit-build:0.1 payload` → `build/orbit_store.elf` ; app `make ffpkg` dans `orbit-app-build:0.1` → `app/dist/PPSA99177.ffpkg` ; `app/tools/verify-package.sh`. Release : `npm run release:package` (jamais d'upload) ; `-- --review` pour préparation locale.

## Composants et contraintes
- Looks TV : store / crossbar / cinema / panorama (`look::is(...)`, namespaces `xb::`, `cn::`, `pano::`) ; chaque look a ses chemins de dessin — toute modif d'écran doit être répercutée sur les 4. Crossbar est le défaut quand un chemin de look est défini.
- Tailles : `listed_size()` (taille après extraction pour archives) ; espace job via `extracted_size`.
- Archives : 7-Zip 26.03 (lecteur seul) + libarchive ; écritures/validation par Orbit.
- Réseau sortant du bac à sable de dev limité (registres de paquets, GitHub) ; pas de console accessible.
- Builds de test pour rapporteurs d'issues : dépôt `saawant12/orbit-issues-test-builds` seulement (voir `AGENTS.md`).

## Règles à ne pas casser (voir `AGENTS.md`)
- Pas de nouveau fichier/génération de flux catalogue (garder v3–v8 ; jamais `catalogue-v9.enc`/`-v10.enc` ni équivalent renommé) ; ne pas changer les URL/constantes de génération côté client ; options non livrables : différer en conservant métadonnées/recherche.
- Compat : v5 (PS4+PS5, sans archives), v6 (ZIP/7z/RAR, bloqué sous la limite 4 096 options), v7 (jeux seuls), v8 (jeux + add-ons, 1.5.0), v3/v4 PS5 seul. Queue schéma 6 (illisible par anciennes versions). 1fichier toujours différé.
- Commits : AGENTS.md demande sujet court en anglais, sans trailers d'attribution, et pas de commit/push avant demande du propriétaire. Note : cette session a parfois ajouté des trailers `Co-Authored-By`/`Claude-Session` ; conflit à arbitrer par le propriétaire (à vérifier).

## Versions (à ne pas confondre)
| Élément | Valeur | Source |
|---|---|---|
| Service/payload + web (développement, checkout local) | 1.7.0 | `package.json`, `BUILDING.md` |
| App TV (développement) | 1.11.0, `PPSA99177`, contentVersion `01.011.000` | `BUILDING.md`, `app/sce_sys/param.json` |
| Version réellement publiée | **à vérifier** (aucun `release-status.json`, tags ou release consultés ; tags git locaux jusqu'à `v1.7.282`, sans lien prouvé avec 1.7.0) | — |
BUILDING.md qualifie 1.7.0 de « local release preparation » : ne pas la présenter comme publiée. Docs périmées : `app/README.md` (TV 1.9.1 / service 1.5.0), `SOURCE-BUNDLE.md` (hotfix 1.9.1, ELF 1.5.0 publié) — publié historique, pas l'état courant.
Branche de travail : `game-updates-17` (fork `TOSanthony/orbit-store`).

## État actuel
- Fait (récent, poussé jusqu'à `7e87b4f`) : popup clavier du look pour l'adresse NAS ; barre de progression de mise à jour du service (`total`, Crossbar) ; À propos Crossbar (seule la description défile, faits visibles) ; tailles « après extraction » dans listes et pastille ; icônes (stockage, version…) sur les faits dans les 4 looks ; libellés « Compressed »/« Installed ».
- Ouvert : ligne `installedDlcSource` de `GET /api/v1/game-updates` (DRIVECLUB DLC, `backend/gameupdates.c`) en attente du retour utilisateur.
- Problèmes connus : à vérifier au cas par cas (pas de suivi d'issues consulté).

## Tests et validation PS5
- Automatisés/locaux faits : gtest hôte (232 réussis au dernier run), aperçus hôte, vérifs de syntaxe ; résultats web/catalogue de cet état : à vérifier (non relancés pour ce document).
- **Aucun test sur vraie PS5 prouvé par le dépôt.** `BUILDING.md` cite `docs/1.7.0-validation.md` : fichier absent → validation console de 1.7.0 à vérifier.
- À confirmer sur console : rebind disque/retry TLS, installation DLC NAS, chemin d'installeur de paquets, `.pkg` PS4/app, noms/images de groupes, barre de progression du service (nécessite le nouveau service), auto-défilement Crossbar.

## Blocages de publication
- `release-status.json` est lu par `tools/release-check.mjs` (`../release-status.json`) mais **absent du dépôt** : `npm run release:check` échoue ici ; état des portes de release à vérifier. Portes attendues : bêta → `licenseCleared`, `localRegressionPassed`, `catalogLinksVerified`, `betaLimitationsDocumented` + `betaConsoleSmokePassed` (ou report autorisé par le propriétaire) ; stable → `licenseCleared`, `consoleTestPassed`, `catalogSourcesVerifiedOnConsole`, `homeScreenLifecycleVerified`.
- Licence : `BUILDING.md` indique « Distribution clearance is pending for the RAR component » (restriction unRAR de 7-Zip, `THIRD-PARTY-NOTICES.md`) — blocage de distribution confirmé par le texte, statut actuel de `licenseCleared` à vérifier.

## Prochaines étapes / à vérifier
- Obtenir/restaurer `release-status.json` et `docs/1.7.0-validation.md` (ou confirmer leur absence voulue).
- Vérifier la version publiée (releases GitHub) avant toute affirmation.
- Valider sur PS5 les points listés ci-dessus ; trancher la ligne DRIVECLUB DLC.
- Rafraîchir `app/README.md`/`SOURCE-BUNDLE.md` si le propriétaire le souhaite.
