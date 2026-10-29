# CLAUDE.md

Orbit Store : boutique homebrew PS5/PS4. Service C (payload), app TV native C++ (GPU), interface web React. Détails : `CONTEXTE.md`.

## Lecture à la demande
- `CONTEXTE.md` : seulement si la tâche exige une vue d'ensemble (architecture, versions, état, blocages). Pas pour une correction locale.
- `AGENTS.md` (règles catalogue/commits/builds de test) : à lire avant toute tâche touchant au catalogue, aux flux `catalogue-v*.enc`, aux commits ou aux builds de test.
- `BUILDING.md` (build/tests/release), `SOURCE-BUNDLE.md` (licences/sources), `docs/*.md` (NAS, mises à jour GitHub) : seulement si pertinents.
- Ne pas explorer le dépôt en entier ; chercher avec Grep/Glob ciblés.

## Règles
- Garder les modifications existantes ; vérifier `git status` avant d'éditer. Changements minimaux, dans le style voisin.
- Compatibilité : ne pas casser les anciens clients/flux (catalogue v3–v8, queue schéma 6, formats d'état existants). Détails dans `AGENTS.md`.
- Textes d'interface : l'anglais est la clé ; après un changement, mettre à jour `i18n/*.json` (`node tools/i18n.mjs extract|check`).
- Sécurité : aucun secret/jeton/identifiant en clair ; pas de contenu de jeux ni d'artwork dans le dépôt.
- Version, état de publication ou résultat de test : le vérifier dans les sources de vérité (`package.json`, `app/sce_sys/param.json`, `release-status.json` s'il existe, tags/dépôt de release) avant de l'affirmer. Jamais présenter une version locale/bêta comme publiée sans preuve.
- Interdit sans autorisation explicite du propriétaire : publier, déployer, pousser, créer une release, modifier un flux public, toute action destructive (suppression, `reset --hard`, `push --force`). Ni commit ni push sans demande.
- Tests : toujours dire lequel a eu lieu — automatisé (gtest/node/CI), local (preview hôte, build) ou sur vraie PS5. Ne jamais déclarer « validé sur console » sans test réel.
- Pas de nouveaux fichiers de doc/skill sans demande. Réponses courtes, sans redites.
