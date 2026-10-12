# Mises à jour depuis ton GitHub

Chaque push sur la branche `nas-source` (ou « Run workflow » à la main) :

1. numérote la version : service `1.2.<n°>`, app TV `1.5.<n°>` (`<n°>` = numéro d'exécution du workflow) ;
2. compile l'ELF et l'app TV (`PPSA99177.ffpkg`), l'ELF sachant qu'il doit chercher ses mises à jour dans **ton** dépôt ;
3. publie une *release* GitHub `v1.2.<n°>` avec les deux fichiers et leurs SHA-256 ;
4. met à jour `payloads.json` et `tv-app.json` sur la branche `updates` (créée au premier passage).

Dans l'app, **Paramètres → mise à jour du service / de l'app TV** compare avec ces fichiers et propose la nouvelle version ; l'ELF et l'app sont téléchargés depuis ta release, vérifiés (SHA-256, format) puis installés par Orbit.

## À faire une fois

- Le dépôt doit être **public** : la PS5 lit `raw.githubusercontent.com` et `github.com/.../releases` sans identifiant.
- Réglages du dépôt → Actions → General → *Workflow permissions* : « Read and write permissions » (le workflow le demande déjà, mais certains comptes/organisations le bloquent).
- Installe **à la main** (FTP) la première version issue de ce workflow : les anciens builds cherchent leurs mises à jour chez l'auteur d'origine. Ensuite, plus de FTP.

## Notes

- Les mises à jour ne s'installent jamais seules : c'est toi qui valides dans les paramètres, puis tu relances Orbit.
- L'app TV 1.5.<n°> ne compte que 3 chiffres pour `<n°>` : au-delà de 999 exécutions, le workflow s'arrête (à adapter).
- Publier des binaires GPL oblige à fournir les sources : l'archive source du tag de chaque release le fait.
- Le catalogue chiffré, lui, vient toujours du projet d'origine ; seules les mises à jour de l'application viennent de ton dépôt.
