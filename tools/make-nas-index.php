<?php
/**
 * Lance tools/make-nas-index.py depuis le navigateur (Synology Web Station + PHP).
 *
 * Installation :
 *   1. Copie ce fichier ET make-nas-index.py dans un dossier web SÉPARÉ de tes jeux,
 *      par ex. /volume1/web/orbit-admin/ (pas dans le dossier des jeux).
 *   2. Modifie les réglages ci-dessous (surtout $TOKEN et $GAMES_DIR).
 *   3. Ouvre https://ton.domaine/orbit-admin/make-nas-index.php?key=TON_TOKEN
 *
 * Prérequis : paquet « Python 3 » installé sur le Synology, PHP avec exec/proc_open
 * autorisé (Web Station > Profil PHP > désactiver proc_open dans disable_functions),
 * et l'utilisateur « http » doit pouvoir lire les jeux et écrire index.json
 * dans $GAMES_DIR (Permissions du dossier partagé).
 */

// ---- Réglages -------------------------------------------------------------
$TOKEN     = 'CHANGE-MOI-par-une-longue-valeur-aleatoire';
$GAMES_DIR = '/volume1/web/orbit-3f9a7c1e';          // dossier servi à Orbit
$SCRIPT    = __DIR__ . '/make-nas-index.py';
$PYTHON    = '/usr/local/bin/python3';                // `which python3` en SSH
$SHA256    = false;                                   // true = empreintes (lent)
$TIMEOUT   = 900;                                     // secondes
// ---------------------------------------------------------------------------

header('Content-Type: text/plain; charset=utf-8');
header('Cache-Control: no-store');
header('X-Robots-Tag: noindex');

function fail(int $code, string $msg): void {
    if (PHP_SAPI !== 'cli') http_response_code($code);
    echo $msg . "\n";
    exit($code === 200 ? 0 : 1);
}

if (PHP_SAPI !== 'cli') {
    $given = $_POST['key'] ?? $_GET['key'] ?? '';
    if ($TOKEN === '' || str_starts_with($TOKEN, 'CHANGE-MOI') ||
        !is_string($given) || !hash_equals($TOKEN, $given)) {
        usleep(500000);
        fail(403, 'Accès refusé (token absent, faux, ou non configuré).');
    }
}

foreach ([$SCRIPT => 'make-nas-index.py', $PYTHON => 'python3'] as $path => $name) {
    if (!is_file($path)) fail(500, "Introuvable : $name ($path)");
}
if (!is_dir($GAMES_DIR)) fail(500, "Dossier des jeux introuvable : $GAMES_DIR");
if (!is_writable($GAMES_DIR)) fail(500, "Le dossier $GAMES_DIR n'est pas inscriptible par l'utilisateur PHP (http).");
if (!function_exists('proc_open')) fail(500, 'proc_open est désactivé dans ce profil PHP.');

// Un seul lancement à la fois.
$lock = fopen(sys_get_temp_dir() . '/orbit-make-index.lock', 'c');
if (!$lock || !flock($lock, LOCK_EX | LOCK_NB)) fail(409, 'Une génération est déjà en cours.');

set_time_limit($TIMEOUT + 30);
$cmd = [$PYTHON, '-I', $SCRIPT, $GAMES_DIR];
if ($SHA256) $cmd[] = '--sha256';

$proc = proc_open($cmd, [1 => ['pipe', 'w'], 2 => ['pipe', 'w']], $pipes, null, ['LANG' => 'C.UTF-8', 'PYTHONIOENCODING' => 'utf-8']);
if (!is_resource($proc)) fail(500, 'Impossible de lancer Python.');
stream_set_blocking($pipes[1], false);
stream_set_blocking($pipes[2], false);

$out = $err = '';
$start = time();
while (true) {
    $out .= stream_get_contents($pipes[1]);
    $err .= stream_get_contents($pipes[2]);
    $status = proc_get_status($proc);
    if (!$status['running']) break;
    if (time() - $start > $TIMEOUT) {
        proc_terminate($proc, 9);
        fail(504, "Délai dépassé ($TIMEOUT s).\n$out");
    }
    usleep(200000);
}
$out .= stream_get_contents($pipes[1]);
$err .= stream_get_contents($pipes[2]);
fclose($pipes[1]); fclose($pipes[2]);
$code = $status['exitcode'] >= 0 ? $status['exitcode'] : proc_close($proc);
if ($status['exitcode'] >= 0) proc_close($proc);

echo ($code === 0 ? "OK\n" : "ERREUR (code $code)\n") . trim($out . "\n" . $err) . "\n";
if ($code !== 0 && PHP_SAPI !== 'cli') http_response_code(500);
exit($code === 0 ? 0 : 1);
