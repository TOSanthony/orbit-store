<?php
/**
 * config.php — tes réglages Orbit (à placer à côté de index.php).
 *
 * Seules les lignes que tu dé-commentes (en retirant « // » au début) changent quelque chose ; les autres gardent
 * la valeur par défaut de index.php. Ce fichier n'est jamais écrasé quand tu remplaces index.php et make-nas-index.py.
 * Mot de passe : php index.php hash   puis colle le résultat dans PASSWORD_HASH.
 */
return [
    // 'SITE_NAME' => 'Mon NAS',
    // 'PASSWORD_HASH' => '',   // voir « php index.php hash »
    // 'GAMES_DIR' => __DIR__,   // dossier d'index.json, de covers/ et des jeux
    // 'SCRIPT' => __DIR__ . '/make-nas-index.py',
    // 'PYTHON' => '/usr/local/bin/python3',
    // 'TIMEOUT' => 3600,   // secondes max pour une actualisation
    // 'SESSION_MIN' => 120,   // déconnexion après N minutes d'inactivité
    // 'MAX_FAILS' => 5,   // mots de passe faux avant blocage
    // 'LOCK_MIN' => 15,   // durée du blocage (minutes)
    // 'GEMINI_KEYS' => [],   // clés API Gemini (option « IA » : traduction et description manquante)
    // 'GEMINI_MODEL' => 'gemini-3.6-flash',
    // 'AI_PROVIDER' => 'gemini',   // 'gemini', 'ollama' (API native /api/generate) ou 'openai' (API /chat/completions : LM Studio, OpenAI, OpenRouter...)
    // 'AI_BASE' => '',   // ollama : 'http://192.168.1.20:11434' (ou .../api/generate) ; openai : 'http://192.168.1.20:1234/v1'
    // 'AI_API_KEYS' => [],   // avec 'openai' : clés de l'API (facultatif pour un serveur local)
    // 'AI_MODEL' => '',   // avec 'openai' : nom du modèle, ex. 'llama3.1:8b' (sinon $GEMINI_MODEL)
    // 'DESC_URLS' => ['https://pegasus-catalog.fly.dev/catalogs/pfs.json', 'https://pegasus-catalog.fly.dev/catalogs/dlps.json'],   // catalogues (titleId + description) : le texte des jeux et applications, sans IA ni limite par passage ([] pour désactiver)
    // 'IGDB_CLIENT_ID' => '',   // IGDB (compte Twitch) : date de sortie et, si le catalogue n'en a pas, description (summary)
    // 'IGDB_CLIENT_SECRET' => '',   // secret du client Twitch (le jeton d'accès est alors demandé tout seul)
    // 'IGDB_ACCESS_TOKEN' => '',   // ou un jeton d'accès déjà obtenu (expire au bout de ~60 jours)
    // 'AI_MAX' => 40,   // traductions IA max par actualisation
    // 'AI_LANG' => 'English',   // langue des descriptions ; si ce n'est pas 'English', les descriptions du catalogue ($DESC_URLS) sont traduites par l'IA ('français'...)
    // 'UPDATE_REPO' => 'TOSanthony/orbit-store',   // dépôt GitHub dont on affiche les versions (Réglages)
    // 'UPDATE_BRANCH' => 'updates',   // branche qui porte payloads.json et tv-app.json
    // 'ALLOW_LINKS' => true,   // Réglages : choisir un dossier et créer un lien symbolique vers lui
    // 'LINK_ROOTS' => ['/volume*'],   // où le sélecteur de dossier a le droit de naviguer (volumes du Synology)
    // 'FETCH_SCRIPT' => __DIR__ . '/fetch-patch.py',   // téléchargeur de patchs PS5 (serveurs Sony)
    // 'PATCH_TIMEOUT' => 21600,   // secondes max pour un téléchargement de patch (6 h)
    // 'CONSOLE_FW' => '',   // firmware de ta PS5 (ex. '10.20') : sert à repérer les patchs compatibles
];
