<?php
/**
 * Interface web d'administration de « Mon NAS » pour Orbit (Synology Web Station + PHP 7.4+).
 *
 *  - protégée par mot de passe : sans connexion, rien n'est affiché sur tes jeux ;
 *  - affiche ta bibliothèque comme sur la console (icônes, fonds, détails) ;
 *  - bouton « Actualiser » : relance make-nas-index.py et recrée index.json.
 *
 * INSTALLATION
 *   1. Copie ce fichier et make-nas-index.py dans le dossier servi à Orbit (celui d'index.json),
 *      ou dans un autre dossier et règle $GAMES_DIR.
 *   2. Crée l'empreinte de ton mot de passe (en SSH, ou terminal du Synology) :
 *          php make-nas-index.php hash
 *      puis colle le résultat dans $PASSWORD_HASH ci-dessous (ou dans nas-admin.config.php).
 *   3. Règle $PYTHON (commande `which python3`), ouvre https://ton.domaine/dossier/make-nas-index.php
 *
 * Les réglages peuvent aussi être placés dans nas-admin.config.php (à côté de ce fichier) :
 *     <?php return ['PASSWORD_HASH' => '...', 'PYTHON' => '/usr/bin/python3'];
 * pour que la mise à jour de ce fichier ne les écrase pas.
 *
 * Ligne de commande : `php make-nas-index.php hash` | `php make-nas-index.php refresh [--ai]`.
 */
declare(strict_types=1);

// ---- Réglages -------------------------------------------------------------
$SITE_NAME     = 'Mon NAS';
$PASSWORD_HASH = '';                                   // voir « php make-nas-index.php hash »
$GAMES_DIR     = __DIR__;                              // dossier d'index.json, de covers/ et des jeux
$SCRIPT        = __DIR__ . '/make-nas-index.py';
$PYTHON        = '/usr/local/bin/python3';
$TIMEOUT       = 3600;                                 // secondes max pour une actualisation
$SESSION_MIN   = 120;                                  // déconnexion après N minutes d'inactivité
$MAX_FAILS     = 5;                                    // mots de passe faux avant blocage
$LOCK_MIN      = 15;                                   // durée du blocage (minutes)
$GEMINI_KEYS   = [];                                   // clés API Gemini (option « Compléter avec l'IA »)
$GEMINI_MODEL  = 'gemini-3.6-flash';
$AI_MAX        = 40;                                   // requêtes IA max par actualisation
$AI_LANG       = 'English';                            // langue des descriptions ('français', 'English'...)
$UPDATE_REPO   = 'TOSanthony/orbit-store';              // dépôt GitHub dont on affiche les versions (Réglages)
$UPDATE_BRANCH = 'updates';                             // branche qui porte payloads.json et tv-app.json
$ALLOW_LINKS   = true;                                  // Réglages : choisir un dossier et créer un lien symbolique vers lui
$LINK_ROOTS    = ['/volume*'];                          // où le sélecteur de dossier a le droit de naviguer (volumes du Synology)
$FETCH_SCRIPT  = __DIR__ . '/fetch-patch.py';           // téléchargeur de patchs PS5 (serveurs Sony)
$PATCH_TIMEOUT = 21600;                                 // secondes max pour un téléchargement de patch (6 h)
$CONSOLE_FW    = '';                                    // firmware de ta PS5 (ex. '10.20') : sert à repérer les patchs compatibles
// ---------------------------------------------------------------------------

$extra = __DIR__ . '/nas-admin.config.php';
if (is_file($extra)) {
    $cfg = require $extra;
    if (is_array($cfg)) {
        foreach ($cfg as $k => $v) {
            if (in_array($k, ['SITE_NAME','PASSWORD_HASH','GAMES_DIR','SCRIPT','PYTHON','TIMEOUT','SESSION_MIN','MAX_FAILS','LOCK_MIN','GEMINI_KEYS','GEMINI_MODEL','AI_MAX','AI_LANG','UPDATE_REPO','UPDATE_BRANCH','ALLOW_LINKS','LINK_ROOTS','FETCH_SCRIPT','PATCH_TIMEOUT','CONSOLE_FW'], true)) {
                $$k = $v;
            }
        }
    }
}

const IMAGE_TYPES = ['png' => 'image/png', 'jpg' => 'image/jpeg', 'jpeg' => 'image/jpeg', 'webp' => 'image/webp'];

// ---- Actualisation (partagée par le web et la ligne de commande) ------------
/**
 * Prépare la commande Python.
 * @return array{0:?array,1:array,2:string} [commande, environnement, erreur]
 */
function refresh_command(bool $sha256, bool $refreshCovers, bool $param, bool $ai, bool $prospero = false): array
{
    global $GAMES_DIR, $SCRIPT, $PYTHON, $GEMINI_KEYS, $GEMINI_MODEL, $AI_MAX, $AI_LANG;
    if (!is_file($SCRIPT)) return [null, [], "make-nas-index.py introuvable : $SCRIPT"];
    if (!is_file($PYTHON)) return [null, [], "Python 3 introuvable : $PYTHON (règle \$PYTHON)"];
    if (!is_dir($GAMES_DIR)) return [null, [], "Dossier des jeux introuvable : $GAMES_DIR"];
    if (!is_writable($GAMES_DIR)) return [null, [], "Le dossier $GAMES_DIR n'est pas inscriptible par l'utilisateur PHP (« http »). Donne-lui les droits d'écriture."];
    if (!function_exists('proc_open')) return [null, [], 'proc_open est désactivé dans le profil PHP de Web Station.'];

    $cmd = [$PYTHON, '-I', $SCRIPT, $GAMES_DIR];
    if ($sha256) $cmd[] = '--sha256';
    if ($refreshCovers) $cmd[] = '--refresh-covers';
    if (!$param) $cmd[] = '--no-param';
    if ($prospero) $cmd[] = '--prospero';
    $env = ['LANG' => 'C.UTF-8', 'PYTHONIOENCODING' => 'utf-8', 'PATH' => '/usr/local/bin:/usr/bin:/bin'];
    $keys = array_values(array_filter(array_map('strval', (array)$GEMINI_KEYS), static fn($k) => trim($k) !== ''));
    if ($ai && $keys) {
        // Les clés passent par l'environnement, jamais par la ligne de commande.
        array_push($cmd, '--ai', '--ai-max', (string)max(1, (int)$AI_MAX), '--ai-model', (string)$GEMINI_MODEL);
        $env['GEMINI_API_KEYS'] = implode(',', array_map('trim', $keys));
        $env['GEMINI_MODEL'] = (string)$GEMINI_MODEL;
        $env['GEMINI_LANG'] = (string)$AI_LANG;
    }
    return [$cmd, $env, ''];
}

/** @return array{0:int,1:string} code de sortie et sortie texte (ligne de commande : on attend la fin) */
function run_refresh(bool $sha256, bool $refreshCovers, bool $param = true, bool $ai = false, bool $prospero = false): array
{
    global $TIMEOUT;
    [$cmd, $env, $err] = refresh_command($sha256, $refreshCovers, $param, $ai, $prospero);
    if ($cmd === null) return [1, $err];
    @set_time_limit(0);
    $proc = proc_open($cmd, [1 => ['pipe', 'w'], 2 => ['pipe', 'w']], $pipes, null, $env);
    if (!is_resource($proc)) return [1, 'Impossible de lancer Python.'];
    stream_set_blocking($pipes[1], false);
    stream_set_blocking($pipes[2], false);
    $out = '';
    $start = time();
    $exit = -1;
    while (true) {
        $out .= (string)stream_get_contents($pipes[1]) . (string)stream_get_contents($pipes[2]);
        $st = proc_get_status($proc);
        if (!$st['running']) { $exit = (int)$st['exitcode']; break; }
        if (time() - $start > $TIMEOUT) { proc_terminate($proc, 9); $out .= "\nDélai dépassé ($TIMEOUT s)."; $exit = 124; break; }
        usleep(200000);
    }
    $out .= (string)stream_get_contents($pipes[1]) . (string)stream_get_contents($pipes[2]);
    fclose($pipes[1]);
    fclose($pipes[2]);
    proc_close($proc);
    if (strlen($out) > 20000) $out = substr($out, 0, 20000) . "\n…";
    return [$exit, trim($out)];
}

// ---- Actualisation en arrière-plan (web) --------------------------------------
// La page répond tout de suite ; Python tourne détaché et la page suit son avancement.
// Cela évite le « Gateway Timeout » du reverse proxy sur les longues actualisations.
function job_base(): string
{
    global $GAMES_DIR;
    return sys_get_temp_dir() . '/orbit-nas-job-' . sha1((string)$GAMES_DIR);
}

/** @return array{state:string,elapsed:int,ok:bool,log:string,at:int} idle | running | done */
function job_state(bool $consume = false): array
{
    global $TIMEOUT;
    $b = job_base();
    $out = ['state' => 'idle', 'elapsed' => 0, 'ok' => false, 'log' => '', 'at' => 0];
    if (!is_file("$b.start")) return $out;
    $started = (int)@filemtime("$b.start");
    $out['elapsed'] = max(0, time() - $started);
    $out['at'] = $started;
    if (is_file("$b.code")) {
        $out['state'] = 'done';
        $out['ok'] = trim((string)@file_get_contents("$b.code")) === '0';
    } elseif ($out['elapsed'] > $TIMEOUT + 120) {
        $out['state'] = 'done';
        $out['log'] = "L'actualisation a été interrompue (délai dépassé).\n";
    } else {
        $out['state'] = 'running';
    }
    $log = is_file("$b.log") ? (string)@file_get_contents("$b.log", false, null, 0, 20000) : '';
    $out['log'] .= trim($log);
    if ($out['state'] === 'done' && $consume) {
        foreach (['start', 'code', 'log'] as $ext) @unlink("$b.$ext");
    }
    return $out;
}

/** @return string message d'erreur, vide si lancé */
function start_background_refresh(bool $sha256, bool $refreshCovers, bool $param, bool $ai, bool $prospero = false): string
{
    global $TIMEOUT;
    if (job_state()['state'] === 'running') return 'Une actualisation est déjà en cours.';
    [$cmd, $env, $err] = refresh_command($sha256, $refreshCovers, $param, $ai, $prospero);
    if ($cmd === null) return $err;
    $b = job_base();
    foreach (['code', 'log'] as $ext) @unlink("$b.$ext");
    @unlink("$b.start");
    $mark = @fopen("$b.start", 'x');                       // création atomique : un seul lancement
    if (!$mark) return 'Une actualisation est déjà en cours.';
    fclose($mark);
    $line = implode(' ', array_map('escapeshellarg', $cmd));
    $inner = 'T=""; command -v timeout >/dev/null 2>&1 && T="timeout -k 10 ' . (int)$TIMEOUT . '"; '
        . '$T ' . $line . ' > ' . escapeshellarg("$b.log") . ' 2>&1; echo $? > ' . escapeshellarg("$b.code");
    $launch = 'nohup sh -c ' . escapeshellarg($inner) . ' >/dev/null 2>&1 &';
    $proc = proc_open($launch, [0 => ['file', '/dev/null', 'r'], 1 => ['file', '/dev/null', 'w'], 2 => ['file', '/dev/null', 'w']], $pipes, null, $env);
    if (!is_resource($proc)) { @unlink("$b.start"); return 'Impossible de lancer Python.'; }
    proc_close($proc);
    return '';
}

// ---- Patchs PS5 (liste prosperopatches.com, téléchargement depuis les serveurs Sony) ----
function fw_major_minor(string $v): ?array
{
    return preg_match('/^(\d{1,2})\.(\d{1,2})(?:\.\d{1,2}){0,2}$/', trim($v), $m) ? [(int)$m[1], (int)$m[2]] : null;
}
/** true si le firmware demandé par le patch est <= à celui de la console (compare majeur.mineur) ; null si inconnu */
function fw_compatible(string $required, string $console): ?bool
{
    $r = fw_major_minor($required);
    $c = fw_major_minor($console);
    if ($r === null || $c === null) return null;
    return $r <= $c;
}
/** firmware de la console : valeur saisie dans la fiche si valide, sinon réglage CONSOLE_FW */
function console_fw(): string
{
    global $CONSOLE_FW;
    $v = trim((string)($_POST['fw'] ?? $_GET['fw'] ?? ''));
    return fw_major_minor($v) !== null ? $v : (string)$CONSOLE_FW;
}
function http_request(string $url, ?string $post = null, int $max = 2097152): ?string
{
    if (!function_exists('curl_init') || strncmp($url, 'https://prosperopatches.com/', 28) !== 0) return null;
    $c = curl_init($url);
    $opt = [CURLOPT_RETURNTRANSFER => true, CURLOPT_TIMEOUT => 15, CURLOPT_CONNECTTIMEOUT => 6, CURLOPT_FOLLOWLOCATION => false,
        CURLOPT_PROTOCOLS => CURLPROTO_HTTPS, CURLOPT_MAXFILESIZE => $max, CURLOPT_USERAGENT => 'Mozilla/5.0 (compatible; orbit-nas-admin)'];
    if ($post !== null) {
        $opt[CURLOPT_POST] = true;
        $opt[CURLOPT_POSTFIELDS] = $post;
        $opt[CURLOPT_HTTPHEADER] = ['Content-Type: application/x-www-form-urlencoded; charset=UTF-8'];
    }
    curl_setopt_array($c, $opt);
    $body = curl_exec($c);
    $code = (int)curl_getinfo($c, CURLINFO_RESPONSE_CODE);
    curl_close($c);
    return is_string($body) && $code === 200 ? $body : null;
}
/** @return array{error:string,patches:array} */
function patch_list(string $id, string $consoleFw): array
{
    $out = ['error' => '', 'patches' => []];
    if (!preg_match('/^PPSA\d{5}$/', $id)) { $out['error'] = 'Identifiant invalide.'; return $out; }
    $cache = sys_get_temp_dir() . '/orbit-nas-patches-' . $id . '.json';
    $raw = null;
    if (is_file($cache) && time() - (int)@filemtime($cache) < 600) $raw = (string)@file_get_contents($cache);
    if ($raw === null || $raw === '') {
        $page = http_request("https://prosperopatches.com/$id");
        if ($page === null || !preg_match('/data-key="([0-9a-f]{16,128})"/', $page, $m)) {
            $out['error'] = 'La liste des patchs est inaccessible depuis le NAS (site indisponible ou bloqué). Saisis la version à la main.';
            return $out;
        }
        $raw = http_request('https://prosperopatches.com/api/internal/loadpatches', json_encode(['titleid' => $id, 'key' => $m[1]]));
        if ($raw === null) { $out['error'] = 'Le site n\'a pas répondu. Saisis la version à la main.'; return $out; }
        @file_put_contents($cache, $raw, LOCK_EX);
    }
    $j = json_decode($raw, true);
    if (!is_array($j) || empty($j['success']) || !is_array($j['patches'] ?? null)) {
        $out['error'] = 'Réponse du site inattendue. Saisis la version à la main.';
        return $out;
    }
    foreach (array_slice($j['patches'], 0, 60) as $p) {
        $ver = (string)($p['content_ver'] ?? '');
        if (!preg_match('/^\d{2}\.\d{3}\.\d{3}$/', $ver)) continue;
        $fw = (string)($p['required_firmware'] ?? '');
        $out['patches'][] = ['version' => $ver, 'size' => substr((string)($p['filesize'] ?? ''), 0, 20), 'fw' => substr($fw, 0, 16),
            'date' => substr((string)($p['import_date'] ?? ''), 0, 10), 'latest' => !empty($p['is_latest']), 'ok' => fw_compatible($fw, $consoleFw)];
    }
    return $out;
}

function patch_base(): string { return job_base() . '-patch'; }
/** @return array{state:string,elapsed:int,ok:bool,id:string,version:string,done:int,total:int,speed:int,piece:int,pieces:int,msg:string} */
function patch_state(bool $consume = false): array
{
    global $PATCH_TIMEOUT;
    $b = patch_base();
    $o = ['state' => 'idle', 'elapsed' => 0, 'ok' => false, 'id' => '', 'version' => '', 'done' => 0, 'total' => 0, 'speed' => 0, 'piece' => 0, 'pieces' => 0, 'msg' => ''];
    if (!is_file("$b.start")) return $o;
    $meta = json_decode((string)@file_get_contents("$b.start"), true);
    if (is_array($meta)) { $o['id'] = (string)($meta['id'] ?? ''); $o['version'] = (string)($meta['version'] ?? ''); }
    $o['elapsed'] = max(0, time() - (int)@filemtime("$b.start"));
    if (is_file("$b.code")) {
        $o['state'] = 'done';
        $o['ok'] = trim((string)@file_get_contents("$b.code")) === '0';
    } elseif ($o['elapsed'] > (int)$PATCH_TIMEOUT + 120) {
        $o['state'] = 'done';
        $o['msg'] = 'Téléchargement interrompu (délai dépassé).';
    } else {
        $o['state'] = 'running';
    }
    $log = '';
    if (is_file("$b.log")) {
        $size = (int)@filesize("$b.log");
        $log = (string)@file_get_contents("$b.log", false, null, max(0, $size - 4000));
    }
    $last = '';
    foreach (preg_split('/\r?\n/', trim($log)) as $line) {
        if (preg_match('/^progress: (\d+) (\d+) (\d+) (\d+) (\d+)/', $line, $m)) {
            [$o['done'], $o['total'], $o['piece'], $o['pieces'], $o['speed']] = [(int)$m[1], (int)$m[2], (int)$m[3], (int)$m[4], (int)$m[5]];
        } elseif ($line !== '') {
            $last = $line;
        }
    }
    if ($o['msg'] === '') $o['msg'] = substr($last, 0, 300);
    if ($o['state'] === 'done' && $consume) {
        foreach (['start', 'code', 'log', 'cancel'] as $ext) @unlink("$b.$ext");
    }
    return $o;
}
/** Version et lien CRC mémorisés par jeu (fichier caché du dossier des jeux, bloqué par nas.htaccess). */
function patch_saved_file(): string
{
    global $GAMES_DIR;
    return rtrim((string)$GAMES_DIR, '/') . '/.nas-admin-patches.json';
}
function patch_saved_load(): array
{
    $j = json_decode((string)@file_get_contents(patch_saved_file()), true);
    $out = [];
    if (is_array($j)) {
        foreach ($j as $id => $v) {
            if (is_string($id) && preg_match('/^PPSA\d{5}$/', $id) && is_array($v)) {
                $out[$id] = ['version' => (string)($v['version'] ?? ''), 'link' => (string)($v['link'] ?? '')];
            }
        }
    }
    return $out;
}
function patch_link_ok(string $link): bool
{
    return (bool)preg_match('~^https?://[A-Za-z0-9.-]+/[A-Za-z0-9/._-]{1,400}$~', $link) && strpos($link, '..') === false;
}
/**
 * Publie, à côté d'index.json, game-patches.json : pour chaque jeu dont tu as enregistré une version
 * ET un lien CRC, cette version et ce lien. Orbit le lit pour proposer la mise à jour dans l'onglet
 * « Updates » (même chemin secret que l'index ; rien d'autre n'y figure).
 */
function patch_public_sync(): void
{
    global $GAMES_DIR;
    $out = [];
    foreach (patch_saved_load() as $id => $v) {
        if ($v['version'] !== '' && $v['link'] !== '' && preg_match('/^\d{2}\.\d{3}\.\d{3}$/', $v['version']) && patch_link_ok($v['link'])) {
            $out[$id] = ['version' => $v['version'], 'link' => $v['link']];
        }
    }
    $file = rtrim((string)$GAMES_DIR, '/') . '/game-patches.json';
    $json = json_encode((object)$out, JSON_UNESCAPED_SLASHES);
    if ($json === false || (is_file($file) && @file_get_contents($file) === $json)) return;
    $tmp = $file . '.tmp';
    if (@file_put_contents($tmp, $json, LOCK_EX) !== false) @rename($tmp, $file);
}
/** @return string message d'erreur, vide si enregistré */
function patch_saved_set(string $id, string $version, string $link): string
{
    if (!preg_match('/^PPSA\d{5}$/', $id)) return 'Identifiant du jeu invalide.';
    $version = trim($version);
    $link = trim($link);
    if ($version !== '' && !preg_match('/^\d{2}\.\d{3}\.\d{3}$/', $version)) return 'Version invalide.';
    if ($link !== '' && !patch_link_ok($link)) return 'Lien invalide.';
    $fh = @fopen(patch_saved_file(), 'c+');
    if (!$fh) return 'Enregistrement impossible (dossier non modifiable).';
    flock($fh, LOCK_EX);
    $all = json_decode((string)stream_get_contents($fh), true);
    $all = is_array($all) ? $all : [];
    if ($version === '' && $link === '') unset($all[$id]);
    else $all[$id] = ['version' => $version, 'link' => $link];
    if (count($all) > 500) $all = array_slice($all, -500, null, true);
    ftruncate($fh, 0);
    rewind($fh);
    fwrite($fh, json_encode($all, JSON_UNESCAPED_SLASHES));
    fflush($fh);
    flock($fh, LOCK_UN);
    fclose($fh);
    patch_public_sync();
    return '';
}
/** @return string message d'erreur, vide si lancé */
function patch_start(string $id, string $version, string $link, bool $force): string
{
    global $PYTHON, $FETCH_SCRIPT, $GAMES_DIR, $PATCH_TIMEOUT;
    $CONSOLE_FW = console_fw();
    if (!preg_match('/^PPSA\d{5}$/', $id)) return 'Identifiant du jeu invalide.';
    if (!preg_match('/^\d{2}\.\d{3}\.\d{3}$/', $version)) return 'Version invalide (attendu : 06.005.000).';
    $link = trim($link);
    if (!preg_match('~^https?://[A-Za-z0-9.-]+/[A-Za-z0-9/._-]{1,400}$~', $link) || strpos($link, '..') !== false) return 'Lien CRC invalide.';
    if (!is_file($FETCH_SCRIPT)) return 'fetch-patch.py est introuvable à côté de cette page.';
    if (patch_state()['state'] === 'running') return 'Un téléchargement est déjà en cours.';
    $py = (string)$PYTHON;
    if ($py === '' || !is_file($py)) return 'Python introuvable (réglage PYTHON).';
    $required = '';
    $list = patch_list($id, $CONSOLE_FW);
    foreach ($list['patches'] as $p) if ($p['version'] === $version) $required = $p['fw'];
    if ($required !== '' && fw_compatible($required, $CONSOLE_FW) === false && !$force) {
        return "Ce patch demande le firmware $required, supérieur à celui de ta console ($CONSOLE_FW). Coche « Forcer » pour le télécharger quand même.";
    }
    $b = patch_base();
    foreach (['code', 'log', 'cancel', 'start'] as $ext) @unlink("$b.$ext");
    $mark = @fopen("$b.start", 'x');
    if (!$mark) return 'Un téléchargement est déjà en cours.';
    patch_saved_set($id, $version, $link);
    fwrite($mark, json_encode(['id' => $id, 'version' => $version]));
    fclose($mark);
    $cmd = [$py, '-u', (string)$FETCH_SCRIPT, (string)$GAMES_DIR, $id, $version, $link, '--cancel-file', "$b.cancel"];
    if ($required !== '') { $cmd[] = '--required-fw'; $cmd[] = $required; }
    if ($CONSOLE_FW !== '') { $cmd[] = '--console-fw'; $cmd[] = $CONSOLE_FW; }
    if ($force) $cmd[] = '--force';
    $line = implode(' ', array_map('escapeshellarg', $cmd));
    $inner = 'T=""; command -v timeout >/dev/null 2>&1 && T="timeout -k 10 ' . (int)$PATCH_TIMEOUT . '"; '
        . '$T ' . $line . ' > ' . escapeshellarg("$b.log") . ' 2>&1; echo $? > ' . escapeshellarg("$b.code");
    $launch = 'nohup sh -c ' . escapeshellarg($inner) . ' >/dev/null 2>&1 &';
    $proc = proc_open($launch, [0 => ['file', '/dev/null', 'r'], 1 => ['file', '/dev/null', 'w'], 2 => ['file', '/dev/null', 'w']], $pipes);
    if (!is_resource($proc)) { @unlink("$b.start"); return 'Impossible de lancer Python.'; }
    proc_close($proc);
    return '';
}

// ---- Ligne de commande -------------------------------------------------------
if (PHP_SAPI === 'cli') {
    $cmd = $argv[1] ?? '';
    if ($cmd === 'hash') {
        $pw = $argv[2] ?? null;
        if ($pw === null) { fwrite(STDERR, 'Mot de passe : '); $pw = rtrim((string)fgets(STDIN), "\r\n"); }
        if (strlen($pw) < 10) { fwrite(STDERR, "Choisis un mot de passe d'au moins 10 caractères.\n"); exit(1); }
        echo password_hash($pw, PASSWORD_DEFAULT) . "\n";
        exit(0);
    }
    if ($cmd === 'refresh') {
        [$code, $text] = run_refresh(in_array('--sha256', $argv, true), in_array('--refresh-covers', $argv, true), !in_array('--no-param', $argv, true), in_array('--ai', $argv, true), in_array('--prospero', $argv, true));
        echo ($code === 0 ? "OK\n" : "ERREUR ($code)\n") . $text . "\n";
        exit($code === 0 ? 0 : 1);
    }
    fwrite(STDERR, "Usage : php make-nas-index.php hash [motdepasse] | refresh [--sha256] [--refresh-covers] [--no-param] [--ai]\n");
    exit(2);
}

// ---- En-têtes de sécurité ----------------------------------------------------
$nonce = base64_encode(random_bytes(16));
header('Cache-Control: no-store');
header('X-Robots-Tag: noindex, nofollow, noarchive');
header('X-Content-Type-Options: nosniff');
header('X-Frame-Options: DENY');
header('Referrer-Policy: no-referrer');
header("Content-Security-Policy: default-src 'none'; img-src 'self' https: data:; connect-src 'self'; style-src 'nonce-$nonce'; script-src 'nonce-$nonce'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'");

function h(string $s): string { return htmlspecialchars($s, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8'); }
function is_https(): bool
{
    return (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off')
        || strtolower((string)($_SERVER['HTTP_X_FORWARDED_PROTO'] ?? '')) === 'https';
}
function plain(int $code, string $text): void
{
    http_response_code($code);
    header('Content-Type: text/plain; charset=utf-8');
    echo $text;
    exit;
}

if (strncmp((string)$PASSWORD_HASH, '$', 1) !== 0) plain(503, 'Non configuré.');

// ---- Session -----------------------------------------------------------------
session_name('orbit_nas');
session_set_cookie_params(['lifetime' => 0, 'path' => '', 'secure' => is_https(), 'httponly' => true, 'samesite' => 'Strict']);
ini_set('session.use_strict_mode', '1');
session_start();
$now = time();
if (!empty($_SESSION['ok']) && $now - (int)($_SESSION['last'] ?? 0) > $SESSION_MIN * 60) {
    $_SESSION = [];
    session_regenerate_id(true);
}
if (empty($_SESSION['csrf'])) $_SESSION['csrf'] = bin2hex(random_bytes(16));
$csrf = (string)$_SESSION['csrf'];
$logged = !empty($_SESSION['ok']);
if ($logged) $_SESSION['last'] = $now;

function csrf_ok(string $csrf): bool
{
    return isset($_POST['csrf']) && is_string($_POST['csrf']) && hash_equals($csrf, $_POST['csrf']);
}

// ---- Connexion (avec frein anti force brute) ---------------------------------
$loginError = '';
$throttle = sys_get_temp_dir() . '/orbit-nas-login-' . sha1((string)($_SERVER['REMOTE_ADDR'] ?? '?')) . '.json';
if (!$logged && ($_SERVER['REQUEST_METHOD'] ?? '') === 'POST' && ($_POST['action'] ?? '') === 'login') {
    $state = is_file($throttle) ? (json_decode((string)file_get_contents($throttle), true) ?: []) : [];
    if (($state['until'] ?? 0) > $now) {
        $loginError = 'Trop de tentatives. Réessaie plus tard.';
    } elseif (!csrf_ok($csrf)) {
        $loginError = 'Session expirée, réessaie.';
    } elseif (is_string($_POST['password'] ?? null) && password_verify($_POST['password'], (string)$PASSWORD_HASH)) {
        @unlink($throttle);
        session_regenerate_id(true);
        $_SESSION['ok'] = true;
        $_SESSION['last'] = $now;
        $_SESSION['csrf'] = bin2hex(random_bytes(16));
        header('Location: ' . strtok((string)($_SERVER['REQUEST_URI'] ?? ''), '?'));
        exit;
    } else {
        usleep(700000);
        $fails = (int)($state['fails'] ?? 0) + 1;
        $until = $fails >= $MAX_FAILS ? $now + $LOCK_MIN * 60 : 0;
        @file_put_contents($throttle, json_encode(['fails' => $until ? 0 : $fails, 'until' => $until]), LOCK_EX);
        $loginError = $until ? 'Trop de tentatives. Réessaie plus tard.' : 'Mot de passe incorrect.';
    }
}

$baseCss = <<<'CSS'
:root{color-scheme:dark;--bg:#081329;--panel:#0f1d3a;--panel2:#142548;--line:#243764;--text:#eaf0ff;--dim:#9db0d8;--accent:#4fb3ff;--ok:#4ade80;--bad:#ff6b6b}
*{box-sizing:border-box}html,body{margin:0;min-height:100%}
body{background:var(--bg);color:var(--text);font:16px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
button,input,select{font:inherit;color:inherit}
CSS;

if (!$logged) {
    header('Content-Type: text/html; charset=utf-8');
    ?><!doctype html>
<html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="robots" content="noindex,nofollow"><title>&nbsp;</title>
<style nonce="<?= h($nonce) ?>"><?= $baseCss ?>
body{display:grid;place-items:center;min-height:100vh;padding:1rem;background:radial-gradient(70rem 40rem at 20% 0,#14306b55,transparent),var(--bg)}
form{width:min(22rem,100%);display:grid;gap:.9rem}
.mark{width:3rem;height:3rem;margin:0 auto .4rem;opacity:.85}
input[type=password]{width:100%;padding:.85rem 1rem;border-radius:.8rem;border:1px solid var(--line);background:var(--panel);outline:none}
input[type=password]:focus{border-color:var(--accent);box-shadow:0 0 0 3px #4fb3ff33}
button{padding:.85rem;border:0;border-radius:.8rem;background:var(--accent);color:#04101f;font-weight:600;cursor:pointer}
button:focus-visible{outline:3px solid #fff;outline-offset:2px}
.err{color:var(--bad);text-align:center;margin:0;font-size:.9rem}
</style></head><body>
<form method="post" autocomplete="off">
<svg class="mark" viewBox="0 0 64 64" aria-hidden="true"><g fill="none" stroke="#9db0d8" stroke-width="3.5"><circle cx="32" cy="32" r="18"/><ellipse cx="32" cy="32" rx="30" ry="10" transform="rotate(-34 32 32)"/></g></svg>
<input type="hidden" name="action" value="login"><input type="hidden" name="csrf" value="<?= h($csrf) ?>">
<input type="password" name="password" aria-label="Mot de passe" placeholder="Mot de passe" autofocus required autocomplete="current-password">
<?php if ($loginError): ?><p class="err" role="alert"><?= h($loginError) ?></p><?php endif; ?>
<button>Entrer</button>
</form></body></html>
<?php
    exit;
}

// ---- Image (réservé aux connectés) --------------------------------------------
if (isset($_GET['img'])) {
    session_write_close();                      // ne bloque pas les autres images en parallèle
    $rel = str_replace('\\', '/', (string)$_GET['img']);
    $ext = strtolower(pathinfo($rel, PATHINFO_EXTENSION));
    $bad = $rel === '' || strpos($rel, "\0") !== false || $rel[0] === '/' || !isset(IMAGE_TYPES[$ext]);
    foreach (explode('/', $rel) as $part) if ($part === '..' || $part === '' || $part[0] === '.') $bad = true;
    $base = realpath($GAMES_DIR);
    $full = $bad || !$base ? false : realpath($base . '/' . $rel);
    if (!$full || strncmp($full, $base . DIRECTORY_SEPARATOR, strlen($base) + 1) !== 0 || !is_file($full)) plain(404, 'Introuvable.');
    header('Content-Type: ' . IMAGE_TYPES[$ext]);
    header('Content-Length: ' . filesize($full));
    header('Cache-Control: private, max-age=3600');
    readfile($full);
    exit;
}

// ---- Avancement de l'actualisation (JSON, réservé aux connectés) ---------------
if (isset($_GET['job'])) {
    session_write_close();
    $j = job_state();
    header('Content-Type: application/json; charset=utf-8');
    echo json_encode(['state' => $j['state'], 'elapsed' => $j['elapsed']]);
    exit;
}

// ---- Patchs PS5 : liste et avancement (JSON, réservé aux connectés) ----------------
if (isset($_GET['patches'])) {
    session_write_close();
    header('Content-Type: application/json; charset=utf-8');
    $l = patch_list((string)$_GET['patches'], console_fw());
    echo json_encode($l + ['consoleFw' => console_fw()], JSON_UNESCAPED_UNICODE);
    exit;
}
if (isset($_GET['patchjob'])) {
    session_write_close();
    header('Content-Type: application/json; charset=utf-8');
    echo json_encode(patch_state(), JSON_UNESCAPED_UNICODE);
    exit;
}

// ---- Versions publiées sur GitHub (flux lus par l'app : payloads.json et tv-app.json) ----
function fetch_text(string $url): ?string
{
    if (function_exists('curl_init')) {
        $c = curl_init($url);
        curl_setopt_array($c, [CURLOPT_RETURNTRANSFER => true, CURLOPT_TIMEOUT => 8, CURLOPT_CONNECTTIMEOUT => 5,
            CURLOPT_FOLLOWLOCATION => false, CURLOPT_PROTOCOLS => CURLPROTO_HTTPS, CURLOPT_MAXFILESIZE => 1 << 20,
            CURLOPT_USERAGENT => 'orbit-nas-admin']);
        $body = curl_exec($c);
        $code = (int)curl_getinfo($c, CURLINFO_RESPONSE_CODE);
        curl_close($c);
        return is_string($body) && $code === 200 ? $body : null;
    }
    $ctx = stream_context_create(['http' => ['timeout' => 8, 'follow_location' => 0, 'header' => "User-Agent: orbit-nas-admin\r\n"]]);
    $body = @file_get_contents($url, false, $ctx, 0, 1 << 20);
    return is_string($body) ? $body : null;
}
/** @return array{repo:string,elf:string,tv:string,tvSize:int,checked:int,error:string} */
function github_versions(string $repo, string $branch, bool $force): array
{
    $out = ['repo' => $repo, 'elf' => '', 'tv' => '', 'tvSize' => 0, 'checked' => time(), 'error' => ''];
    if (!preg_match('#^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$#', $repo) || !preg_match('#^[A-Za-z0-9_./-]+$#', $branch) || strpos($branch, '..') !== false) {
        $out['error'] = 'Dépôt ou branche invalide (UPDATE_REPO / UPDATE_BRANCH).';
        return $out;
    }
    $cache = sys_get_temp_dir() . '/orbit-nas-versions-' . md5($repo . '|' . $branch) . '.json';
    if (!$force && is_file($cache) && time() - (int)filemtime($cache) < 600) {
        $saved = json_decode((string)@file_get_contents($cache), true);
        if (is_array($saved) && ($saved['repo'] ?? '') === $repo) return $saved + $out;
    }
    $base = "https://raw.githubusercontent.com/$repo/$branch/";
    $version = static fn($v): string => is_string($v) && preg_match('/^v?\d{1,4}\.\d{1,5}\.\d{1,6}$/', $v) ? $v : '';
    $elf = fetch_text($base . 'payloads.json');
    $tv = fetch_text($base . 'tv-app.json');
    $a = $elf === null ? null : json_decode($elf, true);
    $b = $tv === null ? null : json_decode($tv, true);
    $out['elf'] = is_array($a) ? $version($a['payloads'][0]['version'] ?? '') : '';
    $out['tv'] = is_array($b) ? $version($b['version'] ?? '') : '';
    $out['tvSize'] = is_array($b) && isset($b['size']) && is_int($b['size']) && $b['size'] > 0 ? $b['size'] : 0;
    if ($out['elf'] === '' && $out['tv'] === '') {
        $out['error'] = "Impossible de lire les versions sur GitHub (dépôt privé, branche absente, ou le Synology n'a pas accès à Internet).";
        return $out;                                  // pas de mise en cache d'un échec
    }
    @file_put_contents($cache, json_encode($out), LOCK_EX);
    return $out;
}

// ---- Versions (JSON, réservé aux connectés) ---------------------------------------
if (isset($_GET['versions'])) {
    session_write_close();
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store');
    echo json_encode(github_versions((string)$UPDATE_REPO, (string)$UPDATE_BRANCH, isset($_GET['force'])), JSON_UNESCAPED_UNICODE);
    exit;
}

// ---- Dossiers de jeux : navigation et liens symboliques ----------------------------------
const LINK_RESERVED = ['index.json', 'covers', 'assets', 'make-nas-index.php', 'make-nas-index.py', 'nas-admin.config.php', 'titles.json'];
/** @return string[] racines réelles (ex. /volume2, /volume3) où la navigation est permise */
function link_roots(): array
{
    global $LINK_ROOTS;
    $out = [];
    foreach ((array)$LINK_ROOTS as $pattern) {
        foreach ((array)(glob((string)$pattern, GLOB_ONLYDIR) ?: []) as $d) {
            $r = realpath($d);
            if ($r !== false) $out[] = rtrim($r, '/');
        }
    }
    sort($out);
    return array_values(array_unique($out));
}
/** Le dossier est une racine ou dedans ; renvoie son chemin réel, ou null. */
function link_resolve(string $path, bool $allowRoot): ?string
{
    if ($path === '' || strpos($path, "\0") !== false || $path[0] !== '/') return null;
    $real = realpath($path);
    if ($real === false || !is_dir($real)) return null;
    $real = rtrim($real, '/');
    foreach (link_roots() as $root) {
        if ($real === $root) return $allowRoot ? $real : null;
        if (strncmp($real, $root . '/', strlen($root) + 1) === 0) {
            foreach (explode('/', substr($real, strlen($root) + 1)) as $part) {
                if ($part === '' || $part[0] === '@' || $part[0] === '#' || $part[0] === '.') return null;   // dossiers système de DSM
            }
            return $real;
        }
    }
    return null;
}
/** @return array{path:string,parent:?string,dirs:array,pick:bool,error:string} */
function link_browse(string $path): array
{
    $out = ['path' => '', 'parent' => null, 'dirs' => [], 'pick' => false, 'error' => ''];
    if ($path === '') {
        foreach (link_roots() as $r) $out['dirs'][] = ['name' => basename($r), 'path' => $r];
        if (!$out['dirs']) $out['error'] = "Aucun volume trouvé (réglage LINK_ROOTS) ou accès refusé par open_basedir.";
        return $out;
    }
    $real = link_resolve($path, true);
    if ($real === null) { $out['error'] = 'Dossier inaccessible ou non autorisé.'; return $out; }
    $out['path'] = $real;
    $isRoot = in_array($real, link_roots(), true);
    $out['pick'] = !$isRoot;
    $out['parent'] = $isRoot ? '' : dirname($real);
    $names = @scandir($real);
    if ($names === false) { $out['error'] = "Lecture impossible (droits de l'utilisateur « http » sur ce dossier ?)."; return $out; }
    natcasesort($names);
    foreach ($names as $n) {
        if ($n === '.' || $n === '..' || $n[0] === '.' || $n[0] === '@' || $n[0] === '#' || $n[0] === '$') continue;
        if (is_dir($real . '/' . $n)) $out['dirs'][] = ['name' => $n, 'path' => $real . '/' . $n];
        if (count($out['dirs']) >= 400) break;
    }
    return $out;
}
/** @return array<int,array{name:string,target:string,ok:bool}> les liens symboliques du dossier web */
function link_list(string $dir): array
{
    $out = [];
    foreach ((array)(@scandir($dir) ?: []) as $n) {
        if ($n === '.' || $n === '..' || !is_link($dir . '/' . $n)) continue;
        $t = (string)@readlink($dir . '/' . $n);
        $out[] = ['name' => $n, 'target' => $t, 'ok' => is_dir($dir . '/' . $n)];
    }
    return $out;
}
/** @return string message d'erreur, ou '' si le lien est créé */
function link_create(string $webDir, string $target, string $name, bool $replace): string
{
    $real = link_resolve($target, false);
    if ($real === null) return "Dossier refusé : choisis un dossier à l'intérieur d'un volume (pas le volume lui-même, pas de dossier système).";
    if (!preg_match('/^[A-Za-z0-9][A-Za-z0-9 _.-]{0,63}$/', $name) || in_array(strtolower($name), LINK_RESERVED, true)) {
        return "Nom de lien invalide ou réservé (lettres, chiffres, espace, point, tiret ; 64 caractères max).";
    }
    $web = realpath($webDir);
    if ($web === false) return 'Dossier web introuvable.';
    if ($real === $web || strncmp($web, $real . '/', strlen($real) + 1) === 0) return "Ce dossier contient le dossier web : tout son contenu, dont le mot de passe, serait exposé.";
    if (strncmp($real, $web . '/', strlen($web) + 1) === 0) return 'Ce dossier est déjà dans le dossier web, pas besoin de lien.';
    if (!is_writable($web)) return "Le dossier web n'est pas modifiable par l'utilisateur « http ».";
    $link = $web . '/' . $name;
    if (is_link($link)) {
        if (!$replace) return "Un lien « $name » existe déjà (coche « Remplacer »).";
        if (!@unlink($link)) return "Impossible de remplacer l'ancien lien « $name ».";
    } elseif (file_exists($link)) {
        return "« $name » existe déjà dans le dossier web et n'est pas un lien : choisis un autre nom.";
    }
    if (!function_exists('symlink') || !@symlink($real, $link)) {
        return "Création impossible (symlink() désactivé ou refusé). Commande à lancer en SSH : sudo ln -s " . escapeshellarg($real) . ' ' . escapeshellarg($link);
    }
    return '';
}

// ---- Navigation dans les dossiers (JSON, réservé aux connectés) --------------------------
if (isset($_GET['browse'])) {
    session_write_close();
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store');
    echo json_encode($ALLOW_LINKS ? link_browse((string)($_GET['path'] ?? '')) : ['path' => '', 'parent' => null, 'dirs' => [], 'pick' => false, 'error' => 'Désactivé (ALLOW_LINKS).'], JSON_UNESCAPED_UNICODE | JSON_INVALID_UTF8_SUBSTITUTE);
    exit;
}

// ---- Actions (POST) ------------------------------------------------------------
if (($_SERVER['REQUEST_METHOD'] ?? '') === 'POST') {
    if (!csrf_ok($csrf)) plain(403, 'Session expirée. Recharge la page.');
    $action = (string)($_POST['action'] ?? '');
    if ($action === 'logout') {
        $_SESSION = [];
        session_destroy();
        header('Location: ' . strtok((string)($_SERVER['REQUEST_URI'] ?? ''), '?'));
        exit;
    }
    if ($action === 'refresh') {
        $err = start_background_refresh(!empty($_POST['sha256']), !empty($_POST['refresh_covers']), !empty($_POST['param']), !empty($_POST['ai']), !empty($_POST['prospero']));
        if ($err !== '') $_SESSION['flash'] = ['ok' => false, 'log' => $err, 'at' => time()];
        header('Location: ' . strtok((string)($_SERVER['REQUEST_URI'] ?? ''), '?'));
        exit;
    }
    if ($action === 'patch_dl' || $action === 'patch_cancel' || $action === 'patch_ack' || $action === 'patch_save') {
        header('Content-Type: application/json; charset=utf-8');
        if ($action === 'patch_dl') {
            $err = patch_start((string)($_POST['id'] ?? ''), (string)($_POST['version'] ?? ''), (string)($_POST['link'] ?? ''), !empty($_POST['force']));
            echo json_encode(['ok' => $err === '', 'error' => $err], JSON_UNESCAPED_UNICODE);
        } elseif ($action === 'patch_save') {
            $err = patch_saved_set((string)($_POST['id'] ?? ''), (string)($_POST['version'] ?? ''), (string)($_POST['link'] ?? ''));
            echo json_encode(['ok' => $err === '', 'error' => $err], JSON_UNESCAPED_UNICODE);
        } elseif ($action === 'patch_cancel') {
            $st = patch_state();
            if ($st['state'] === 'running') @file_put_contents(patch_base() . '.cancel', '1');
            echo json_encode(['ok' => true, 'error' => '']);
        } else {
            patch_state(true);                         // efface un résultat déjà affiché
            echo json_encode(['ok' => true, 'error' => '']);
        }
        exit;
    }
    if ($action === 'link_create' || $action === 'link_delete') {
        $name = (string)($_POST['name'] ?? '');
        if (!$ALLOW_LINKS) {
            $msg = 'Désactivé (ALLOW_LINKS).';
        } elseif ($action === 'link_create') {
            $msg = link_create((string)$GAMES_DIR, (string)($_POST['path'] ?? ''), $name, !empty($_POST['replace']));
        } else {
            $full = rtrim((string)$GAMES_DIR, '/') . '/' . $name;
            $msg = preg_match('/^[A-Za-z0-9][A-Za-z0-9 _.-]{0,63}$/', $name) && is_link($full) ? (@unlink($full) ? '' : 'Suppression impossible.') : 'Ce n\'est pas un lien.';
        }
        $made = $action === 'link_create' ? 'Lien « ' . $name . ' » créé. Clique sur « Actualiser » pour indexer les jeux.' : 'Lien « ' . $name . ' » retiré (les fichiers du volume ne sont pas touchés).';
        $_SESSION['flash'] = ['ok' => $msg === '', 'log' => $msg === '' ? $made : $msg, 'at' => time(), 'title' => $msg === '' ? 'Dossier des jeux' : 'Dossier des jeux : échec'];
        header('Location: ' . strtok((string)($_SERVER['REQUEST_URI'] ?? ''), '?'));
        exit;
    }
    plain(400, 'Action inconnue.');
}

// ---- Page principale -------------------------------------------------------------
function art_url(string $ref): string
{
    if ($ref === '') return '';
    if (preg_match('#^https://#i', $ref)) return $ref;
    if (preg_match('#^[a-z][a-z0-9+.-]*:|^//#i', $ref)) return '';
    return '?img=' . rawurlencode(rawurldecode($ref));
}

$indexFile = rtrim($GAMES_DIR, '/') . '/index.json';
$games = [];
$indexError = '';
$indexTime = 0;
if (is_file($indexFile)) {
    $indexTime = (int)filemtime($indexFile);
    $json = json_decode((string)file_get_contents($indexFile), true);
    if (!is_array($json) || !isset($json['releases']) || !is_array($json['releases'])) {
        $indexError = "index.json n'a pas le format attendu.";
    } else {
        foreach ($json['releases'] as $r) {
            if (!is_array($r)) continue;
            $s = static fn(string $k): string => isset($r[$k]) && is_scalar($r[$k]) ? (string)$r[$k] : '';
            $fallback = static fn(string $ref): bool => (bool)preg_match('#fallback\.(png|jpe?g|webp)$#i', $ref);
            $games[] = [
                'gameId' => $s('gameId') ?: $s('titleId'),
                'title' => $s('title') ?: $s('filename'),
                'titleId' => $s('titleId'),
                'filename' => $s('filename'),
                'path' => rawurldecode($s('path') ?: $s('url')),
                'size' => (int)($r['sizeBytes'] ?? 0),
                'format' => $s('format') ?: '—',
                'compression' => $s('compression'),
                'version' => $s('version'),
                'description' => $s('description'),
                'publisher' => $s('publisher'),
                'genre' => $s('genre'),
                'region' => $s('region'),
                'platform' => $s('platform'),
                'tagline' => $s('tagline'),
                'releaseDate' => $s('releaseDate'),
                'addedAt' => $s('addedAt'),
                'sha256' => $s('sha256'),
                'cover' => art_url($s('cover')),
                'hero' => art_url($s('hero')),
                'coverReal' => $s('cover') !== '' && !$fallback($s('cover')),
                'heroReal' => $s('hero') !== '' && !$fallback($s('hero')),
            ];
        }
    }
} else {
    $indexError = "Pas encore d'index : clique sur « Actualiser ».";
}
$flash = $_SESSION['flash'] ?? null;
unset($_SESSION['flash']);
$job = job_state(true);                          // une actualisation terminée est affichée une seule fois
if ($job['state'] === 'done') $flash = ['ok' => $job['ok'], 'log' => $job['log'] !== '' ? $job['log'] : '(aucune sortie)', 'at' => $job['at']];
$running = $job['state'] === 'running';
$links = $ALLOW_LINKS ? link_list((string)$GAMES_DIR) : [];
patch_public_sync();
$data = json_encode(['games' => $games, 'indexTime' => $indexTime, 'today' => date('Y-m-d'), 'csrf' => $csrf, 'consoleFw' => (string)$CONSOLE_FW, 'patches' => is_file((string)$FETCH_SCRIPT), 'saved' => (object)patch_saved_load()], JSON_HEX_TAG | JSON_HEX_AMP | JSON_HEX_APOS | JSON_HEX_QUOT | JSON_UNESCAPED_UNICODE | JSON_INVALID_UTF8_SUBSTITUTE);

header('Content-Type: text/html; charset=utf-8');
?><!doctype html>
<html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="robots" content="noindex,nofollow"><meta name="theme-color" content="#081329">
<title><?= h($SITE_NAME) ?></title>
<style nonce="<?= h($nonce) ?>"><?= $baseCss ?>
body{background:radial-gradient(80rem 40rem at 85% -10%,#14306b44,transparent),var(--bg);background-attachment:fixed}
:root{--pad:clamp(1rem,3.2vw,3rem)}

/* Barre du haut : logo, onglets, recherche, actions */
header.bar{position:sticky;top:0;z-index:10;display:flex;flex-wrap:nowrap;gap:.6rem 1.2rem;align-items:center;padding:.7rem var(--pad);background:linear-gradient(180deg,#081329f2,#081329b0);backdrop-filter:blur(12px)}
.brand{display:flex;align-items:center;gap:.6rem;font-weight:600;font-size:1.05rem}
.brand svg{width:1.7rem;height:1.7rem;flex:none}
nav.tabs{display:flex;gap:.2rem;overflow-x:auto;scrollbar-width:none;flex:1 1 0;min-width:0}
nav.tabs::-webkit-scrollbar{display:none}
.tab{all:unset;box-sizing:border-box;cursor:pointer;white-space:nowrap;padding:.5rem .95rem;border-radius:999px;color:var(--dim);font-weight:500;border:1px solid transparent;transition:background .15s,color .15s}
.tab:hover{color:var(--text);background:#ffffff10}
.tab[aria-current=page]{color:#04101f;background:var(--text);font-weight:600}
.tab:focus-visible{outline:3px solid #fff;outline-offset:2px}
.btn.gear{width:2.5rem;height:2.5rem;padding:0;display:inline-flex;align-items:center;justify-content:center;border-radius:50%;flex:none;position:relative}
.gear svg{display:block;flex:none}
.gear.busy::after{content:"";position:absolute;inset:-4px;border-radius:50%;border:2px solid #4fb3ff33;border-top-color:var(--accent);animation:s 1s linear infinite;pointer-events:none}
.stitle{margin:0;padding:1.3rem 3.5rem .2rem 1.4rem;font-size:1.3rem}
.srow{display:flex;gap:.5rem;flex-wrap:wrap;margin:.8rem 1.4rem 0}
.sform .srow{margin:0}
.srow a.btn{text-decoration:none;color:inherit}
.sform{display:grid;gap:.6rem;margin:0 1.4rem}
.sform label{display:flex;gap:.55rem;align-items:flex-start;font-size:.9rem;color:var(--dim)}
.sform input{margin-top:.2rem;accent-color:var(--accent)}
.sform .btn{justify-self:start;margin-top:.3rem}
#vers dd{font-weight:600}
.btn.small{padding:.3rem .7rem;font-size:.85rem}
form.inl{margin:0}
ul.files li:has(form.inl){grid-template-columns:auto 1fr auto}
.fb{border:1px solid var(--line);border-radius:.8rem;background:#ffffff08;overflow:hidden}
.fb-path{padding:.5rem .8rem;font:.82rem ui-monospace,Menlo,Consolas,monospace;color:var(--dim);border-bottom:1px solid var(--line);word-break:break-all}
.fb-list{max-height:11rem;overflow:auto;display:grid}
.fb-item{all:unset;box-sizing:border-box;cursor:pointer;padding:.5rem .8rem;font-size:.9rem;border-bottom:1px solid #ffffff0d}
.fb-item:hover,.fb-item:focus-visible{background:#ffffff14}
.fb-foot{display:flex;gap:.6rem;align-items:center;justify-content:space-between;padding:.5rem .8rem;border-top:1px solid var(--line)}
.fb-foot .note{min-width:0}
.field{padding:.55rem .9rem;border-radius:.7rem;border:1px solid var(--line);background:var(--panel);outline:none;min-width:0}
.field:focus{border-color:var(--accent);box-shadow:0 0 0 3px #4fb3ff33}
#q{width:min(15rem,30vw);flex:none}
#plat{width:auto;flex:none}
.btn{padding:.55rem 1rem;border-radius:.7rem;border:1px solid var(--line);background:var(--panel2);cursor:pointer;white-space:nowrap}
.btn:hover{border-color:var(--accent)}
.btn.primary{background:var(--accent);border-color:var(--accent);color:#04101f;font-weight:600}
.btn:focus-visible,.tile:focus-visible{outline:3px solid #fff;outline-offset:3px}
.btn[disabled]{opacity:.6;cursor:progress}

/* Découvrir : bannière */
.hero{position:relative;min-height:min(31rem,64vh);margin-top:-3.6rem;padding:7rem var(--pad) 2.5rem;display:flex;flex-direction:column;justify-content:flex-end;overflow:hidden}
.hero-bg{position:absolute;inset:0;background:center 20%/cover no-repeat;opacity:0;transition:opacity .5s}
.hero-bg.on{opacity:1}
.hero:after{content:"";position:absolute;inset:0;background:linear-gradient(90deg,var(--bg) 0,#081329e6 28%,#08132955 70%,transparent),linear-gradient(0deg,var(--bg) 0,transparent 55%)}
.hero>*:not(.hero-bg){position:relative;z-index:1;max-width:38rem}
.eyebrow{margin:0 0 .5rem;color:var(--accent);font-weight:600;letter-spacing:.12em;text-transform:uppercase;font-size:.8rem}
.hero h1{margin:0;font-size:clamp(2rem,5vw,3.9rem);line-height:1.08;font-weight:650;overflow:hidden;display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical}
.hero p.tag{margin:.8rem 0 0;color:#cfdaf5;font-size:1.05rem;min-height:1.5em;overflow:hidden;display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical}
.hero .btn{margin-top:1.3rem;align-self:flex-start;padding:.75rem 1.5rem;font-size:1rem}

/* Rangées et grilles */
main{padding:0 0 3rem}
.wrap{padding:0 var(--pad)}
.rail{margin-top:1.6rem}
.rail-head{display:flex;align-items:baseline;gap:.8rem;padding:0 var(--pad);margin-bottom:.4rem}
.rail-head h2,.page-head h1{margin:0;font-size:1.25rem}
.note{color:var(--dim);font-size:.88rem}
.rail-head .nudge{margin-left:auto;display:flex;gap:.3rem}
.nudge button{all:unset;cursor:pointer;width:2rem;height:2rem;border-radius:50%;display:grid;place-items:center;background:#ffffff12}
.nudge button:hover{background:#ffffff22}.nudge button:focus-visible{outline:3px solid #fff}
.rail-row{display:flex;gap:1rem;overflow-x:auto;padding:.7rem var(--pad) 1rem;scrollbar-width:none;scroll-padding:0 var(--pad)}
.rail-row::-webkit-scrollbar{display:none}
.rail-row .tile{flex:none;width:10.5rem}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(10.5rem,1fr));gap:1.1rem}
.page-head{display:flex;flex-wrap:wrap;align-items:baseline;gap:.6rem 1rem;padding:6rem var(--pad) 1rem;margin-top:-3.6rem}
.page-head h1{font-size:2rem}
.filters{display:flex;gap:.5rem;flex-wrap:wrap;margin-left:auto}
.pager{display:flex;gap:.8rem;align-items:center;justify-content:center;margin-top:1.6rem;color:var(--dim)}
.tile{all:unset;box-sizing:border-box;display:block;cursor:pointer;border-radius:1rem;padding:.55rem;background:#ffffff08;border:1px solid transparent;transition:transform .15s,background .15s}
.tile:hover,.tile:focus-visible{transform:translateY(-3px) scale(1.02);background:#ffffff16;border-color:#ffffff2a}
.art{aspect-ratio:1;border-radius:.75rem;overflow:hidden;background:linear-gradient(135deg,var(--panel2),var(--panel));display:grid;place-items:center;position:relative}
.art img{width:100%;height:100%;object-fit:cover;display:block}
.art svg{width:38%;opacity:.35}
.plat{position:absolute;left:.4rem;top:.4rem;font-size:.7rem;font-weight:700;padding:.12rem .5rem;border-radius:999px;background:#1f5fe0;color:#fff;letter-spacing:.04em;box-shadow:0 1px 4px #0006}
.badge{position:absolute;left:.4rem;bottom:.4rem;font-size:.7rem;padding:.12rem .45rem;border-radius:999px;background:#000a;color:#fff;letter-spacing:.02em}
.name{margin-top:.55rem;font-weight:600;font-size:.95rem;overflow:hidden;display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical}
.sub{color:var(--dim);font-size:.8rem;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.empty{padding:3rem 1rem;text-align:center;color:var(--dim);max-width:34rem;margin:0 auto}
.stats{color:var(--dim);font-size:.85rem;margin:2rem var(--pad) 0}

/* Fiche d'un jeu */
dialog{border:1px solid var(--line);border-radius:1.2rem;background:var(--panel);color:var(--text);padding:0;width:min(42rem,calc(100% - 2rem));max-height:calc(100% - 2rem);overflow:auto}
dialog::backdrop{background:#030814cc;backdrop-filter:blur(4px)}
.dhero{height:11rem;background:linear-gradient(135deg,#1b3a7a,var(--panel2)) center/cover;position:relative}
.dhero:after{content:"";position:absolute;inset:0;background:linear-gradient(180deg,transparent 40%,var(--panel))}
.det{display:flex;gap:1.1rem;padding:0 1.4rem 1.4rem;margin-top:-4rem;position:relative;z-index:1}
.det .art{width:7.5rem;flex:none;box-shadow:0 8px 24px #0008;border:1px solid #ffffff22}
.det h2{margin:.2rem 0 .1rem;font-size:1.35rem}
.desc{margin:0 1.4rem;color:var(--dim);font-size:.92rem}.desc:empty{display:none}
h3{margin:1.2rem 1.4rem .5rem;font-size:.85rem;color:var(--dim);text-transform:uppercase;letter-spacing:.08em;font-weight:600}
ul.files{list-style:none;margin:0 1.4rem;padding:0;display:grid;gap:.4rem}
ul.files li{display:grid;grid-template-columns:auto 1fr auto;gap:.2rem .8rem;align-items:baseline;padding:.55rem .8rem;border-radius:.7rem;background:#ffffff0a;font-size:.88rem}
ul.files .fmt{font-weight:600;color:var(--accent);white-space:nowrap}
.pbox{margin:0 1.4rem;display:grid;gap:.6rem}
.pbox label{display:grid;gap:.25rem;font-size:.85rem;color:var(--dim)}
.pbox .row{display:flex;gap:.6rem;flex-wrap:wrap;align-items:flex-end}
.pbox a{color:var(--accent)}
.prow{display:flex;gap:.6rem;align-items:center;flex-wrap:wrap;width:100%;text-align:left;padding:.45rem .7rem;border-radius:.6rem;border:1px solid var(--line);background:var(--panel2);cursor:pointer;color:inherit;font:inherit}
.prow[aria-pressed=true]{border-color:var(--accent);box-shadow:0 0 0 2px #4fb3ff33}
.prow b{font-variant-numeric:tabular-nums}.prow .m{color:var(--dim);font-size:.82rem}
.prow .t{margin-left:auto;font-size:.75rem;font-weight:700;padding:.1rem .5rem;border-radius:999px}
.t.ok{background:#14532d;color:#bbf7d0}.t.no{background:#7f1d1d;color:#fecaca}.t.unk{background:#334155;color:#cbd5e1}
#pRows{display:grid;gap:.35rem;max-height:13rem;overflow:auto}
.pbar{height:.7rem;border-radius:999px;background:var(--panel2);border:1px solid var(--line);overflow:hidden}
.pbar i{display:block;height:100%;width:0;background:var(--accent);transition:width .6s linear}
.pbox .chk{display:flex;gap:.5rem;align-items:center}
ul.files .fn{word-break:break-all}
ul.files .sz{color:var(--dim);white-space:nowrap}
dl{display:grid;grid-template-columns:max-content 1fr;gap:.35rem 1rem;margin:0 1.4rem;font-size:.9rem}
dt{color:var(--dim)}dd{margin:0;word-break:break-all}
.close{position:absolute;top:.7rem;right:.7rem;z-index:2}
.actions{padding:1.2rem 1.4rem 1.4rem;display:flex;justify-content:flex-end}

/* Actualisation */
.panel{margin:1rem var(--pad) 0;border:1px solid var(--line);border-radius:.9rem;background:var(--panel);padding:.8rem 1rem}
.panel pre{margin:.6rem 0 0;white-space:pre-wrap;font:.82rem/1.5 ui-monospace,Menlo,Consolas,monospace;color:var(--dim);max-height:16rem;overflow:auto}
.panel.ok summary{color:var(--ok)}.panel.bad summary{color:var(--bad)}
.panel.first{margin-top:5.5rem}
summary{cursor:pointer;font-weight:600}
.spin{display:none;width:1rem;height:1rem;border:2px solid #04101f55;border-top-color:#04101f;border-radius:50%;animation:s 1s linear infinite;vertical-align:-.2rem;margin-right:.4rem}
.busy .spin,.spin.on{display:inline-block}.panel .spin{border-color:#9db0d855;border-top-color:var(--accent);vertical-align:-.15rem}@keyframes s{to{transform:rotate(360deg)}}
@media (max-width:40rem){.brand span{display:none}#q{width:8rem}.det{flex-direction:column;margin-top:-3rem}.grid{grid-template-columns:repeat(2,1fr)}.rail-row .tile{width:8.8rem}.hero{padding-top:2rem}.page-head{padding-top:1.5rem}.hero,.page-head{margin-top:0}header.bar{position:relative}.panel.first{margin-top:1rem}.filters{margin-left:0}}
@media (prefers-reduced-motion:reduce){*{transition:none!important;animation-duration:2s!important}}
</style></head><body>
<header class="bar">
  <div class="brand"><svg viewBox="0 0 64 64" aria-hidden="true"><g fill="none" stroke="#eaf0ff" stroke-width="4"><circle cx="32" cy="32" r="18"/><ellipse cx="32" cy="32" rx="30" ry="10" transform="rotate(-34 32 32)"/></g></svg><span><?= h($SITE_NAME) ?></span></div>
  <nav class="tabs" id="tabs" aria-label="Catégories"></nav>
  <input id="q" class="field" type="search" placeholder="Rechercher…" aria-label="Rechercher">
  <select id="plat" class="field" aria-label="Plateformes affichées" title="Plateformes affichées"><option value="">PS5 + PS4</option><option value="ps5">PS5 seulement</option><option value="ps4">PS4 seulement</option></select>
  <button class="btn gear" id="gear" type="button" aria-label="Réglages" title="Réglages"><svg viewBox="0 0 24 24" width="22" height="22" aria-hidden="true"><path d="M9.78 4.73 L10.40 1.93 L13.60 1.93 L14.22 4.73 L15.57 5.29 L18.00 3.75 L20.25 6.00 L18.71 8.43 L19.27 9.78 L22.07 10.40 L22.07 13.60 L19.27 14.22 L18.71 15.57 L20.25 18.00 L18.00 20.25 L15.57 18.71 L14.22 19.27 L13.60 22.07 L10.40 22.07 L9.78 19.27 L8.43 18.71 L6.00 20.25 L3.75 18.00 L5.29 15.57 L4.73 14.22 L1.93 13.60 L1.93 10.40 L4.73 9.78 L5.29 8.43 L3.75 6.00 L6.00 3.75 L8.43 5.29Z" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linejoin="round"/><circle cx="12" cy="12" r="3.1" fill="none" stroke="currentColor" stroke-width="1.7"/></svg></button>
</header>
<?php if ($running): ?>
  <div class="panel first" id="running" role="status"><span class="spin on"></span> Actualisation en cours… <span id="elapsed"><?= (int)$job['elapsed'] ?></span> s — tu peux fermer cette page, elle continue en arrière-plan.</div>
<?php endif; ?>
<?php if ($flash): ?>
  <details class="panel first <?= $flash['ok'] ? 'ok' : 'bad' ?>" open><summary><?= h((string)($flash['title'] ?? ($flash['ok'] ? 'Index actualisé' : "Échec de l'actualisation"))) ?> — <?= h(date('d/m/Y H:i:s', (int)$flash['at'])) ?></summary><pre><?= h((string)$flash['log']) ?></pre></details>
<?php endif; ?>
<?php if ($indexError): ?><div class="panel bad first"><?= h($indexError) ?></div><?php endif; ?>
<main id="main"></main>
<dialog id="settings" aria-labelledby="sTitle">
  <button class="btn close" id="sClose" aria-label="Fermer">✕</button>
  <h2 id="sTitle" class="stitle">Réglages</h2>
  <h3>Dernières versions sur GitHub</h3>
  <dl id="vers"><dt>App TV</dt><dd id="vTv">…</dd><dt>ELF (Orbit Store)</dt><dd id="vElf">…</dd></dl>
  <p class="desc" id="vNote"></p>
  <div class="srow"><button class="btn" id="vCheck" type="button">Vérifier maintenant</button><a class="btn" id="vLink" target="_blank" rel="noopener noreferrer">Voir les releases</a></div>
  <?php if ($ALLOW_LINKS): ?>
  <h3>Dossiers de jeux sur un autre volume</h3>
  <p class="desc">Choisis le dossier qui contient tes jeux (par exemple sur le volume3) : un lien est créé dans le dossier web pour que le serveur le serve. Son contenu devient accessible à quiconque connaît l'adresse secrète.</p>
<?php if ($links): ?>
  <ul class="files" id="linkList">
<?php foreach ($links as $l): ?>
    <li><span class="fmt"><?= h($l['name']) ?></span><span class="fn">→ <?= h($l['target']) ?><?= $l['ok'] ? '' : ' (introuvable)' ?></span>
      <form method="post" class="inl"><input type="hidden" name="action" value="link_delete"><input type="hidden" name="csrf" value="<?= h($csrf) ?>"><input type="hidden" name="name" value="<?= h($l['name']) ?>"><button class="btn small">Retirer</button></form></li>
<?php endforeach; ?>
  </ul>
<?php endif; ?>
  <form class="sform" method="post" id="linkForm">
    <input type="hidden" name="action" value="link_create"><input type="hidden" name="csrf" value="<?= h($csrf) ?>">
    <div class="fb" id="fb" hidden>
      <div class="fb-path" id="fbPath"></div>
      <div class="fb-list" id="fbList"></div>
      <div class="fb-foot"><span class="note" id="fbMsg"></span><button class="btn small" type="button" id="fbPick">Choisir ce dossier</button></div>
    </div>
    <input class="field" id="lnkPath" name="path" placeholder="Dossier choisi (ex. /volume3/Jeux)" readonly required>
    <input class="field" id="lnkName" name="name" placeholder="Nom du lien (ex. packages)" maxlength="64" pattern="[A-Za-z0-9][A-Za-z0-9 _.\-]{0,63}" required>
    <label><input type="checkbox" name="replace" value="1"> Remplacer un lien du même nom</label>
    <div class="srow"><button class="btn" type="button" id="fbOpen">Parcourir les volumes…</button><button class="btn primary">Créer le lien</button></div>
  </form>
  <?php endif; ?>
  <h3>Index des jeux</h3>
  <form class="sform" method="post" id="refreshForm">
    <input type="hidden" name="action" value="refresh"><input type="hidden" name="csrf" value="<?= h($csrf) ?>">
    <label><input type="checkbox" name="param" value="1" checked> Lire le param.json des .zip PS5 et l'intérieur des .pkg (titre, identifiant, version, icône, DLC)</label>
    <label><input type="checkbox" name="prospero" value="1" checked> Récupérer titre, éditeur, région, icône et fond sur prosperopatches.com (tes propres images de covers/ restent prioritaires)</label>
<?php if (!empty(array_filter((array)$GEMINI_KEYS))): ?>
    <label><input type="checkbox" name="ai" value="1"> Compléter avec l'IA (Gemini) les infos manquantes (description, éditeur, genre, accroche, date de sortie)</label>
<?php endif; ?>
    <label><input type="checkbox" name="refresh_covers" value="1"> Remplacer les images déjà extraites des .zip</label>
    <label><input type="checkbox" name="sha256" value="1"> Calculer les SHA-256 (long sur de gros fichiers)</label>
    <button class="btn primary" id="refreshGo"<?= $running ? ' disabled' : '' ?>><span class="spin"></span><span id="refreshLabel">Actualiser</span></button>
  </form>
  <div class="actions"><form method="post"><input type="hidden" name="action" value="logout"><input type="hidden" name="csrf" value="<?= h($csrf) ?>"><button class="btn">Déconnexion</button></form></div>
</dialog>
<dialog id="dlg" aria-labelledby="dTitle">
  <button class="btn close" id="dClose" aria-label="Fermer">✕</button>
  <div class="dhero" id="dHero"></div>
  <div class="det"><div class="art" id="dArt"></div><div><h2 id="dTitle"></h2><div class="sub" id="dSub"></div></div></div>
  <p class="desc" id="dDesc"></p>
  <h3 id="dFilesTitle">Fichiers</h3>
  <ul class="files" id="dFiles"></ul>
  <h3>Informations</h3>
  <dl id="dList"></dl>
  <div id="dPatch" hidden>
    <h3>Patchs (PS5)</h3>
    <div class="pbox">
      <div class="row">
        <label>Firmware de ta console<input class="field" id="pFw" size="7" maxlength="11" placeholder="10.20" autocomplete="off"></label>
        <button class="btn small" id="pList" type="button">Chercher les patchs</button>
        <a id="pSite" href="#" target="_blank" rel="noopener noreferrer">Page du jeu ↗</a>
      </div>
      <div id="pRows"></div>
      <label>Version<input class="field" id="pVer" maxlength="10" placeholder="06.005.000" autocomplete="off"></label>
      <label>Lien CRC du patch<input class="field" id="pLink" maxlength="500" placeholder="https://sgst.prod.dl.playstation.net/…/….crc" autocomplete="off"></label>
      <p class="note">Sur la page du jeu, ouvre le patch, fais un clic droit sur « CRC » → copier l'adresse du lien, puis colle-la ici. Le fichier est téléchargé depuis les serveurs Sony dans <code>packages/ps5/updates/</code>.</p>
      <label class="chk"><input type="checkbox" id="pForce"> Forcer même si le firmware de la console est trop ancien</label>
      <div class="row"><button class="btn primary" id="pGo" type="button">Télécharger le patch</button><button class="btn" id="pStop" type="button" hidden>Annuler</button></div>
      <div class="pbar" id="pBar" hidden><i id="pFill"></i></div>
      <p class="note" id="pMsg" role="status"></p>
    </div>
  </div>
  <div class="actions"><button class="btn" id="dOk">Fermer</button></div>
</dialog>
<script type="application/json" id="data"><?= $data ?></script>
<script nonce="<?= h($nonce) ?>">
(() => {
  const { games: releases, indexTime, today, csrf: CSRF, consoleFw: CFG_FW, patches: PATCHES_ON, saved: SAVED } = JSON.parse(document.getElementById('data').textContent);
  const $ = (id) => document.getElementById(id);
  const main = $('main'), tabsEl = $('tabs'), q = $('q');
  const SVG = 'http://www.w3.org/2000/svg';
  const PAGE = 96;

  // ---- Catégories : la « tagline » de l'index sert de marqueur (DLC, UPDATES, APPS) ----
  const TABS = [
    ['discover', 'Découvrir'], ['games', 'Jeux'], ['apps', 'Applications'], ['dlc', 'DLC'], ['updates', 'Mises à jour'],
  ];
  const LABEL = { game: 'Jeu', apps: 'Application', dlc: 'DLC', updates: 'Mise à jour' };
  const EMPTY = {
    games: 'Aucun jeu pour l\'instant. Place tes jeux dans packages/ps5/games/… puis clique sur « Actualiser ».',
    apps: 'Aucune application. Place-les dans packages/ps5/apps/<extension>/<application> puis clique sur « Actualiser ».',
    dlc: 'Aucun DLC. Place les .pkg dans packages/ps5/dlc/<ID du jeu>/ puis clique sur « Actualiser ».',
    updates: 'Aucune mise à jour. Place les .pkg dans packages/ps5/updates/<ID du jeu>/ puis clique sur « Actualiser ».',
  };
  const catOf = (tagline) => {
    const t = (tagline || '').trim().toUpperCase();
    return t === 'DLC' ? 'dlc' : t === 'UPDATES' ? 'updates' : t === 'APPS' ? 'apps' : 'game';
  };

  // ---- Un jeu = tous ses fichiers (formats, DLC, mises à jour) regroupés par gameId ----
  const byKey = new Map();
  for (const r of releases) {
    const key = r.gameId || r.titleId || r.filename;
    let g = byKey.get(key);
    if (!g) {
      g = { key, cat: catOf(r.tagline), title: r.title, titleId: r.titleId, tagline: '', description: '', publisher: '', genre: '',
        region: '', releaseDate: '', platform: r.platform, cover: '', hero: '', coverReal: false, heroReal: false, added: 0, files: [] };
      byKey.set(key, g);
    }
    g.files.push(r);
    if (g.cat === 'game' && !g.tagline) g.tagline = r.tagline;
    for (const k of ['description', 'publisher', 'genre', 'region', 'releaseDate', 'platform']) if (!g[k] && r[k]) g[k] = r[k];
    if (r.cover && (!g.cover || (r.coverReal && !g.coverReal))) { g.cover = r.cover; g.coverReal = r.coverReal; }
    if (r.hero && (!g.hero || (r.heroReal && !g.heroReal))) { g.hero = r.hero; g.heroReal = r.heroReal; }
    g.added = Math.max(g.added, Date.parse(r.addedAt) || 0);
  }
  const all = [...byKey.values()];
  all.forEach((g) => {
    g.size = g.files.reduce((a, f) => a + f.size, 0);
    g.formats = [...new Set(g.files.map((f) => f.format))];
    g.search = (g.title + ' ' + g.titleId + ' ' + g.files.map((f) => f.filename).join(' ')).toLowerCase();
  });
  const sortTitle = (a, b) => a.title.localeCompare(b.title, 'fr', { sensitivity: 'base' });
  // Plateformes affichées : les deux, PS5 seulement ou PS4 seulement (retenu dans ce navigateur).
  let platform = '';
  try { platform = localStorage.getItem('orbit.nas.platform') || ''; } catch (e) { /* stockage indisponible */ }
  if (platform !== 'ps5' && platform !== 'ps4') platform = '';
  const onPlatform = (g) => !platform || (platform === 'ps4' ? g.platform === 'ps4' : g.platform !== 'ps4');
  const pool = (tab) => (tab === 'discover' ? all.filter((g) => g.cat === 'game' || g.cat === 'apps')
    : all.filter((g) => g.cat === ({ games: 'game', apps: 'apps', dlc: 'dlc', updates: 'updates' })[tab])).filter(onPlatform);

  // ---- Petits utilitaires ----
  const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
  const mark = () => {
    const s = document.createElementNS(SVG, 'svg'); s.setAttribute('viewBox', '0 0 64 64');
    s.innerHTML = '<g fill="none" stroke="#eaf0ff" stroke-width="4"><circle cx="32" cy="32" r="18"/><ellipse cx="32" cy="32" rx="30" ry="10" transform="rotate(-34 32 32)"/></g>';
    return s;
  };
  const bytes = (n) => {
    if (!n) return '—';
    const u = ['o', 'Ko', 'Mo', 'Go', 'To']; let i = 0;
    while (n >= 1024 && i < u.length - 1) { n /= 1024; i++; }
    return n.toFixed(i > 2 ? 2 : i ? 1 : 0).replace('.', ',') + ' ' + u[i];
  };
  const plural = (n, one, many) => n + ' ' + (n > 1 ? many : one);
  const cssUrl = (u) => 'url("' + u.replace(/"/g, '%22') + '")';
  const picture = (g, badge) => {
    const box = el('div', 'art');
    if (g.cover) { const i = new Image(); i.alt = ''; i.loading = 'lazy'; i.decoding = 'async'; i.src = g.cover; i.onerror = () => { i.remove(); box.prepend(mark()); }; box.append(i); }
    else box.append(mark());
    if (g.platform === 'ps4') { const p = el('span', 'plat', 'PS4'); p.title = 'Jeu PS4'; box.append(p); }
    if (badge) box.append(el('span', 'badge', g.files.length > 1 ? plural(g.files.length, 'fichier', 'fichiers') : g.formats.join(' · ')));
    return box;
  };
  const firstGenre = (g) => (g.genre || '').split('/')[0].trim();

  // ---- Navigation par onglets (#discover, #games, #apps, #dlc, #updates) ----
  let tab = 'discover', page = 0, focused = null;
  const filters = { fmt: '', genre: '', sort: 'title', art: '' };
  const fromHash = () => { const h = location.hash.slice(1); return TABS.some(([id]) => id === h) ? h : 'discover'; };

  function drawTabs() {
    tabsEl.replaceChildren(...TABS.map(([id, label]) => {
      const b = el('button', 'tab', label); b.type = 'button';
      if (id === tab && !q.value.trim()) b.setAttribute('aria-current', 'page');
      b.addEventListener('click', () => { q.value = ''; go(id); });
      return b;
    }));
  }
  function go(id) {
    if (location.hash.slice(1) !== id) { history.replaceState(null, '', '#' + id); }
    tab = id; page = 0; filters.fmt = filters.genre = filters.art = ''; filters.sort = 'title';
    render(); window.scrollTo(0, 0);
  }

  // ---- Tuile, rangée, grille ----
  function tile(g, onFocus, withCategory) {
    const b = el('button', 'tile'); b.type = 'button'; b.setAttribute('role', 'listitem');
    const sub = [withCategory && LABEL[g.cat], firstGenre(g), g.titleId].filter(Boolean).join(' · ');
    b.append(picture(g, true), el('div', 'name', g.title), el('div', 'sub', sub || '—'));
    if (onFocus) { b.addEventListener('mouseenter', () => onFocus(g)); b.addEventListener('focus', () => onFocus(g)); }
    b.addEventListener('click', () => openGame(g));
    return b;
  }
  function rail(title, note, games, onFocus) {
    const sec = el('section', 'rail');
    const head = el('div', 'rail-head'); head.append(el('h2', null, title), el('span', 'note', note));
    const row = el('div', 'rail-row'); row.setAttribute('role', 'list');
    row.append(...games.map((g) => tile(g, onFocus)));
    const nudge = el('div', 'nudge');
    [['‹', -1, 'Précédent'], ['›', 1, 'Suivant']].forEach(([glyph, dir, label]) => {
      const b = el('button', null, glyph); b.type = 'button'; b.setAttribute('aria-label', label); b.tabIndex = -1;
      b.addEventListener('click', () => row.scrollBy({ left: dir * row.clientWidth * 0.8, behavior: 'smooth' }));
      nudge.append(b);
    });
    head.append(nudge);
    sec.append(head, row);
    return sec;
  }
  function pagedGrid(games, withCategory, onFocus) {
    const pages = Math.max(1, Math.ceil(games.length / PAGE));
    page = Math.min(page, pages - 1);
    const box = el('div', 'wrap');
    const grid = el('div', 'grid'); grid.setAttribute('role', 'list');
    grid.append(...games.slice(page * PAGE, (page + 1) * PAGE).map((g) => tile(g, onFocus, withCategory)));
    box.append(grid);
    if (pages > 1) {
      const pager = el('div', 'pager');
      const prev = el('button', 'btn', '‹ Précédent'), next = el('button', 'btn', 'Suivant ›');
      prev.type = next.type = 'button'; prev.disabled = page === 0; next.disabled = page >= pages - 1;
      prev.onclick = () => { page--; render(); window.scrollTo(0, 0); };
      next.onclick = () => { page++; render(); window.scrollTo(0, 0); };
      pager.append(prev, el('span', null, 'Page ' + (page + 1) + ' sur ' + pages), next);
      box.append(pager);
    }
    return box;
  }

  // ---- Découvrir : bannière + rangées + tous les jeux ----
  function discover() {
    const games = pool('discover');
    if (!games.length) return [empty('games')];
    const recent = [...games].filter((g) => g.added).sort((a, b) => b.added - a.added || sortTitle(a, b)).slice(0, 20);
    const latest = games.filter((g) => g.releaseDate && g.releaseDate <= today)
      .sort((a, b) => b.releaseDate.localeCompare(a.releaseDate) || sortTitle(a, b)).slice(0, 20);
    if (!focused || !games.includes(focused)) focused = recent[0] || latest[0] || games[0];

    const hero = el('section', 'hero'); hero.setAttribute('aria-live', 'polite');
    const bgs = [el('div', 'hero-bg'), el('div', 'hero-bg')]; let bgIndex = 0;
    const eyebrow = el('p', 'eyebrow', 'Découvrir'), title = el('h1'), tag = el('p', 'tag');
    const see = el('button', 'btn primary'); see.type = 'button';
    see.addEventListener('click', () => openGame(focused));
    hero.append(...bgs, eyebrow, title, tag, see);
    const show = (g) => {
      focused = g;
      title.textContent = g.title;
      tag.textContent = (g.cat === 'game' ? g.tagline : '') || g.description || [firstGenre(g), g.publisher].filter(Boolean).join(' · ');
      see.textContent = g.cat === 'apps' ? "Voir l'application" : 'Voir le jeu';
      const url = g.heroReal ? g.hero : g.coverReal ? g.cover : '';
      bgIndex ^= 1; const on = bgs[bgIndex], off = bgs[bgIndex ^ 1];
      if (url) { on.style.backgroundImage = cssUrl(url); on.classList.add('on'); } else on.classList.remove('on');
      off.classList.remove('on');
    };
    show(focused);

    const parts = [hero];
    if (recent.length) parts.push(rail('Nouveautés', 'Ajoutés en dernier', recent, show));
    if (latest.length) parts.push(rail('Dernières sorties', 'Plus récentes d\'abord', latest, show));
    const sorted = [...games].sort((a, b) => (b.size - a.size) || sortTitle(a, b));
    const head = el('div', 'rail-head'); head.style.marginTop = '1.8rem';
    head.append(el('h2', null, 'Tous les jeux'), el('span', 'note', plural(games.length, 'élément', 'éléments')));
    const grid = pagedGrid(sorted, true, show);
    parts.push(head, grid);
    parts.push(el('p', 'stats', plural(games.length, 'élément', 'éléments') + ' · ' + bytes(games.reduce((a, g) => a + g.size, 0)) + (indexTime ? ' · index du ' + new Date(indexTime * 1000).toLocaleString('fr-FR') : '')));
    return parts;
  }

  function empty(kind) { const e = el('div', 'empty', EMPTY[kind]); e.style.paddingTop = '8rem'; return e; }

  // ---- Jeux / Applications / DLC / Mises à jour : grille filtrable ----
  const select = (label, value, options, set) => {
    const s = el('select', 'field'); s.setAttribute('aria-label', label);
    options.forEach(([v, t]) => s.append(new Option(t, v)));
    s.value = value; s.addEventListener('input', () => { set(s.value); page = 0; render(); });
    return s;
  };
  function browse(kind) {
    const items = pool(kind);
    const head = el('div', 'page-head');
    head.append(el('h1', null, TABS.find(([id]) => id === kind)[1]), el('span', 'note', plural(items.length, 'élément', 'éléments')));
    if (!items.length) return [head, empty(kind)];
    const formats = [...new Set(items.flatMap((g) => g.formats))].sort();
    const genres = [...new Set(items.map(firstGenre).filter(Boolean))].sort((a, b) => a.localeCompare(b, 'fr'));
    const bar = el('div', 'filters');
    if (formats.length > 1) bar.append(select('Format', filters.fmt, [['', 'Tous les formats'], ...formats.map((f) => [f, f])], (v) => (filters.fmt = v)));
    if (genres.length > 1) bar.append(select('Genre', filters.genre, [['', 'Tous les genres'], ...genres.map((x) => [x, x])], (v) => (filters.genre = v)));
    bar.append(select('Trier par', filters.sort, [['title', 'Titre A → Z'], ['added', 'Ajoutés récemment'], ['release', 'Date de sortie'], ['size', 'Taille décroissante']], (v) => (filters.sort = v)));
    bar.append(select('Images', filters.art, [['', 'Toutes les images'], ['nocover', 'Sans icône'], ['nohero', 'Sans fond']], (v) => (filters.art = v)));
    head.append(bar);
    const shown = items.filter((g) => (!filters.fmt || g.formats.includes(filters.fmt)) && (!filters.genre || firstGenre(g) === filters.genre)
      && (filters.art !== 'nocover' || !g.coverReal) && (filters.art !== 'nohero' || !g.heroReal));
    const order = { title: sortTitle, added: (a, b) => b.added - a.added || sortTitle(a, b), release: (a, b) => (b.releaseDate || '').localeCompare(a.releaseDate || '') || sortTitle(a, b), size: (a, b) => b.size - a.size || sortTitle(a, b) };
    shown.sort(order[filters.sort]);
    return [head, shown.length ? pagedGrid(shown, false) : el('div', 'empty', 'Aucun résultat avec ces filtres.')];
  }

  // ---- Recherche : tous les types à la fois ----
  function results(term) {
    const found = all.filter((g) => onPlatform(g) && g.search.includes(term)).sort(sortTitle);
    const head = el('div', 'page-head');
    head.append(el('h1', null, 'Résultats'), el('span', 'note', plural(found.length, 'élément', 'éléments') + ' pour « ' + q.value.trim() + ' »'));
    return [head, found.length ? pagedGrid(found, true) : el('div', 'empty', 'Aucun résultat.')];
  }

  function render() {
    const term = q.value.trim().toLowerCase();
    drawTabs();
    const content = term ? results(term) : tab === 'discover' ? discover() : browse(tab);
    main.replaceChildren(...content);
  }

  // ---- Fiche ----
  const dlg = $('dlg');
  function openGame(g) {
    $('dHero').style.backgroundImage = g.heroReal ? cssUrl(g.hero) : g.hero ? cssUrl(g.hero) : '';
    $('dArt').replaceWith(Object.assign(picture(g, false), { id: 'dArt' }));
    $('dTitle').textContent = g.title;
    $('dSub').textContent = [LABEL[g.cat], g.titleId, g.publisher].filter(Boolean).join(' · ');
    $('dDesc').textContent = [g.cat === 'game' && g.tagline, g.description].filter(Boolean).join(' — ');
    const files = [...g.files].sort((a, b) => a.filename.localeCompare(b.filename, 'fr', { numeric: true }));
    $('dFilesTitle').textContent = files.length > 1 ? 'Fichiers (' + files.length + ')' : 'Fichier';
    $('dFiles').replaceChildren(...files.map((f) => {
      const li = el('li'); li.title = f.path;
      li.append(el('span', 'fmt', f.format + (f.compression ? ' · ' + f.compression : '')), el('span', 'fn', f.filename),
        el('span', 'sz', [f.version && 'v' + f.version, bytes(f.size)].filter(Boolean).join(' · ')));
      return li;
    }));
    const rows = [files.length > 1 ? ['Taille totale', bytes(g.size)] : ['Chemin', files[0].path], g.platform && ['Plateforme', g.platform.toUpperCase()], g.region && ['Région', g.region],
      g.genre && ['Genre', g.genre], g.releaseDate && ['Sortie', g.releaseDate], files.length === 1 && files[0].sha256 && ['SHA-256', files[0].sha256],
      ['Icône', g.coverReal ? 'oui' : 'manquante'], ['Fond', g.heroReal ? 'oui' : 'manquant']].filter(Boolean);
    $('dList').replaceChildren(...rows.flatMap(([k, v]) => [el('dt', null, k), el('dd', null, v)]));
    dlg.showModal();
    setupPatch(g);
  }
  // ---- Patchs PS5 ----
  const pbox = $('dPatch');
  let patchGame = '', patchTimer = 0;
  const lsGet = (k) => { try { return localStorage.getItem(k) || ''; } catch (e) { return ''; } };
  const lsSet = (k, v) => { try { localStorage.setItem(k, v); } catch (e) { /* stockage indisponible */ } };
  const fwOk = () => /^\d{1,2}\.\d{1,2}(\.\d{1,2}){0,2}$/.test($('pFw').value.trim());
  const pPost = async (data) => {
    const r = await fetch(location.pathname, { method: 'POST', credentials: 'same-origin', body: new URLSearchParams({ csrf: CSRF, ...data }) });
    return r.json();
  };
  const hms = (s) => { const m = Math.floor(s / 60); return m + ' min ' + String(s % 60).padStart(2, '0') + ' s'; };
  function showPatchState(st) {
    const run = st.state === 'running';
    $('pStop').hidden = !run;
    $('pGo').disabled = run;
    $('pBar').hidden = !(run || (st.state === 'done' && st.ok));
    const pct = st.total ? Math.min(100, Math.floor(st.done * 100 / st.total)) : 0;
    $('pFill').style.width = (st.state === 'done' && st.ok ? 100 : pct) + '%';
    if (run) {
      $('pMsg').textContent = st.id + ' v' + st.version + ' : ' + (st.total ? pct + ' % · ' + bytes(st.done) + ' / ' + bytes(st.total) + ' · morceau ' + Math.min(st.piece + 1, st.pieces) + '/' + st.pieces + (st.speed ? ' · ' + bytes(st.speed) + '/s' : '') : 'préparation…') + ' · ' + hms(st.elapsed);
    } else if (st.state === 'done') {
      $('pMsg').textContent = st.ok ? 'Terminé : ' + st.id + ' v' + st.version + '. Ferme cette fiche et clique sur « Actualiser » pour l\'ajouter à la liste.' : 'Échec : ' + (st.msg || 'erreur inconnue') + (st.msg && /cancel|annul/i.test(st.msg) ? ' (la reprise est possible : relance le téléchargement)' : '');
    }
  }
  async function pollPatch() {
    clearTimeout(patchTimer);
    if (!dlg.open) return;
    try {
      const st = await (await fetch('?patchjob=1', { cache: 'no-store', credentials: 'same-origin' })).json();
      if (st.state !== 'idle') showPatchState(st);
      if (st.state === 'running') { patchTimer = setTimeout(pollPatch, 2000); return; }
      if (st.state === 'done') pPost({ action: 'patch_ack' }).catch(() => {});
      else { $('pGo').disabled = false; $('pStop').hidden = true; }
    } catch (e) { patchTimer = setTimeout(pollPatch, 4000); }
  }
  function setupPatch(g) {
    const on = PATCHES_ON && g.platform === 'ps5' && g.cat === 'game' && /^PPSA\d{5}$/.test(g.titleId || '');
    pbox.hidden = !on;
    clearTimeout(patchTimer);
    if (!on) return;
    patchGame = g.titleId;
    $('pFw').value = lsGet('orbitFw') || CFG_FW || '';
    $('pSite').href = 'https://prosperopatches.com/' + g.titleId;
    $('pRows').replaceChildren(); $('pVer').value = (SAVED[g.titleId] || {}).version || ''; $('pLink').value = (SAVED[g.titleId] || {}).link || ''; $('pForce').checked = false;
    $('pMsg').textContent = ''; $('pBar').hidden = true; $('pStop').hidden = true; $('pGo').disabled = false;
    pollPatch();
  }
  let saveTimer = 0;
  function saveFields() {
    const id = patchGame, version = $('pVer').value.trim(), link = $('pLink').value.trim();
    if (!id) return;
    if (!/^(\d{2}\.\d{3}\.\d{3})?$/.test(version) || !(link === '' || /^https?:\/\/\S+$/.test(link))) return;
    SAVED[id] = { version, link };
    clearTimeout(saveTimer);
    saveTimer = setTimeout(() => pPost({ action: 'patch_save', id, version, link }).catch(() => {}), 400);
  }
  $('pVer').addEventListener('input', saveFields);
  $('pLink').addEventListener('input', saveFields);
  $('pFw').addEventListener('change', () => { if (fwOk()) lsSet('orbitFw', $('pFw').value.trim()); });
  $('pList').addEventListener('click', async () => {
    const id = patchGame; if (!id) return;
    $('pMsg').textContent = 'Recherche des patchs…'; $('pRows').replaceChildren();
    try {
      const r = await fetch('?patches=' + encodeURIComponent(id) + (fwOk() ? '&fw=' + encodeURIComponent($('pFw').value.trim()) : ''), { cache: 'no-store', credentials: 'same-origin' });
      const j = await r.json();
      if (id !== patchGame) return;
      $('pMsg').textContent = j.error || (j.patches.length ? '' : 'Aucun patch listé.');
      const rows = j.patches.map((p) => {
        const b = el('button', 'prow'); b.type = 'button'; b.setAttribute('aria-pressed', 'false');
        const tag = p.ok === true ? ['ok', 'compatible'] : p.ok === false ? ['no', 'firmware trop récent'] : ['unk', 'firmware ?'];
        b.append(el('b', null, 'v' + p.version), el('span', 'm', [p.latest && 'dernier', p.size, p.date, p.fw && 'FW ' + p.fw].filter(Boolean).join(' · ')), el('span', 't ' + tag[0], tag[1]));
        b.onclick = () => { $('pVer').value = p.version; saveFields(); for (const x of $('pRows').children) x.setAttribute('aria-pressed', String(x === b)); };
        return b;
      });
      $('pRows').replaceChildren(...rows);
      const keep = j.patches.findIndex((p) => p.version === $('pVer').value.trim());
      if (rows.length) rows[keep >= 0 ? keep : 0].click();
    } catch (e) { $('pMsg').textContent = 'Impossible de contacter le serveur.'; }
  });
  $('pGo').addEventListener('click', async () => {
    const version = $('pVer').value.trim(), link = $('pLink').value.trim();
    if (!/^\d{2}\.\d{3}\.\d{3}$/.test(version)) { $('pMsg').textContent = 'Version attendue au format 06.005.000.'; return; }
    if (!/^https?:\/\/\S+\.(crc|json)$/i.test(link)) { $('pMsg').textContent = 'Colle le lien CRC (qui finit par .crc).'; return; }
    $('pGo').disabled = true; $('pMsg').textContent = 'Lancement…';
    try {
      const j = await pPost({ action: 'patch_dl', id: patchGame, version, link, force: $('pForce').checked ? '1' : '', fw: fwOk() ? $('pFw').value.trim() : '' });
      if (!j.ok) { $('pMsg').textContent = j.error || 'Échec du lancement.'; $('pGo').disabled = false; return; }
      showPatchState({ state: 'running', id: patchGame, version, elapsed: 0, done: 0, total: 0, piece: 0, pieces: 0, speed: 0 });
      patchTimer = setTimeout(pollPatch, 1500);
    } catch (e) { $('pMsg').textContent = 'Impossible de contacter le serveur.'; $('pGo').disabled = false; }
  });
  $('pStop').addEventListener('click', async () => {
    $('pStop').disabled = true;
    try { await pPost({ action: 'patch_cancel' }); $('pMsg').textContent = 'Annulation…'; } catch (e) { /* on réessaiera */ }
    setTimeout(() => { $('pStop').disabled = false; }, 3000);
  });
  dlg.addEventListener('close', () => clearTimeout(patchTimer));

  $('dClose').onclick = $('dOk').onclick = () => dlg.close();
  dlg.addEventListener('click', (e) => { if (e.target === dlg) dlg.close(); });

  // ---- Flèches du clavier, comme à la manette ----
  main.addEventListener('keydown', (e) => {
    const cur = document.activeElement;
    if (!cur || !cur.classList.contains('tile') || !e.key.startsWith('Arrow')) return;
    const tiles = [...main.querySelectorAll('.tile')];
    let next = null;
    if (e.key === 'ArrowRight' || e.key === 'ArrowLeft') {
      const sib = e.key === 'ArrowRight' ? cur.nextElementSibling : cur.previousElementSibling;
      next = sib && sib.classList.contains('tile') ? sib : null;
    } else {
      const t = cur.getBoundingClientRect(), dir = e.key === 'ArrowDown' ? 1 : -1;
      const rows = tiles.filter((x) => { const r = x.getBoundingClientRect(); return dir > 0 ? r.top > t.top + 8 : r.bottom < t.bottom - 8; });
      const pool2 = dir > 0 ? rows : rows.reverse();
      const top = pool2.length && pool2[0].getBoundingClientRect().top;
      const same = pool2.filter((x) => Math.abs(x.getBoundingClientRect().top - top) < 8);
      if (same.length) next = same.reduce((p, x) => Math.abs(x.getBoundingClientRect().left - t.left) < Math.abs(p.getBoundingClientRect().left - t.left) ? x : p);
    }
    if (next) { e.preventDefault(); next.focus(); next.scrollIntoView({ block: 'nearest', inline: 'nearest' }); }
  });

  q.addEventListener('input', () => { page = 0; render(); });
  window.addEventListener('hashchange', () => { if (fromHash() !== tab) { q.value = ''; go(fromHash()); } });
  // ---- Réglages : choisir un dossier et créer un lien ----
  if ($('fb')) {
    const fb = $('fb');
    let current = { path: '', pick: false };
    async function browse(path) {
      $('fbMsg').textContent = '';
      try {
        const r = await fetch('?browse=1&path=' + encodeURIComponent(path), { cache: 'no-store', credentials: 'same-origin' });
        const j = await r.json();
        current = j;
        $('fbPath').textContent = j.path || 'Volumes';
        const items = [];
        if (j.parent !== null && j.parent !== undefined) { const up = el('button', 'fb-item', '↑ Dossier parent'); up.type = 'button'; up.onclick = () => browse(j.parent); items.push(up); }
        for (const d of j.dirs) { const b = el('button', 'fb-item', '📁 ' + d.name); b.type = 'button'; b.onclick = () => browse(d.path); items.push(b); }
        if (!j.dirs.length && !j.error) items.push(el('div', 'fb-item', 'Aucun sous-dossier.'));
        $('fbList').replaceChildren(...items);
        $('fbMsg').textContent = j.error || '';
        $('fbPick').disabled = !j.pick;
      } catch (e) { $('fbMsg').textContent = 'Impossible de contacter le serveur.'; }
    }
    $('fbOpen').addEventListener('click', () => { fb.hidden = false; browse($('lnkPath').value ? $('lnkPath').value.replace(/\/[^/]*$/, '') : ''); });
    $('fbPick').addEventListener('click', () => {
      if (!current.pick) return;
      $('lnkPath').value = current.path;
      if (!$('lnkName').value) $('lnkName').value = current.path.split('/').pop().replace(/[^A-Za-z0-9 _.-]/g, '').replace(/^[^A-Za-z0-9]+/, '');
      fb.hidden = true;
    });
    $('linkForm').addEventListener('submit', (e) => {
      if (!confirm('Créer le lien « ' + $('lnkName').value + ' » vers ' + $('lnkPath').value + ' ?\nTout le contenu de ce dossier sera accessible depuis Internet à quiconque connaît l\'adresse secrète.')) e.preventDefault();
    });
  }
  // ---- Réglages : versions GitHub + actualisation de l'index ----
  const settings = $('settings');
  const when = (t) => new Date(t * 1000).toLocaleTimeString('fr-FR', { hour: '2-digit', minute: '2-digit' });
  async function loadVersions(force) {
    $('vTv').textContent = $('vElf').textContent = '…'; $('vNote').textContent = '';
    try {
      const r = await fetch('?versions=1' + (force ? '&force=1' : ''), { cache: 'no-store', credentials: 'same-origin' });
      const v = await r.json();
      $('vLink').href = 'https://github.com/' + v.repo + '/releases';
      $('vTv').textContent = v.tv ? v.tv + (v.tvSize ? ' · ' + bytes(v.tvSize) : '') : '—';
      $('vElf').textContent = v.elf || '—';
      $('vNote').textContent = v.error || 'Vérifié à ' + when(v.checked) + '. Compare avec « Mises à jour » dans les paramètres de l\'app sur la console.';
    } catch (e) { $('vTv').textContent = $('vElf').textContent = '—'; $('vNote').textContent = 'Impossible de contacter le serveur.'; }
  }
  $('gear').addEventListener('click', () => { settings.showModal(); loadVersions(false); });
  $('vCheck').addEventListener('click', () => loadVersions(true));
  $('sClose').onclick = () => settings.close();
  settings.addEventListener('click', (e) => { if (e.target === settings) settings.close(); });
  $('refreshForm').addEventListener('submit', () => {
    $('refreshLabel').textContent = 'Actualisation…'; $('refreshGo').classList.add('busy'); $('refreshGo').disabled = true; $('gear').classList.add('busy');
  });
  if ($('running')) $('gear').classList.add('busy');
  tab = fromHash();
  const platEl = $('plat');
  platEl.value = platform;
  platEl.addEventListener('input', () => {
    platform = platEl.value;
    try { localStorage.setItem('orbit.nas.platform', platform); } catch (e) { /* stockage indisponible */ }
    page = 0; render();
  });
  render();
  if ($('running')) {
    const tick = async () => {
      try {
        const r = await fetch('?job=1', { cache: 'no-store', credentials: 'same-origin' });
        const j = await r.json();
        $('elapsed').textContent = j.elapsed;
        if (j.state !== 'running') { location.reload(); return; }
      } catch (e) { /* réseau ou proxy occupé : on réessaie */ }
      setTimeout(tick, 2500);
    };
    setTimeout(tick, 2500);
  }
})();
</script>
</body></html>
