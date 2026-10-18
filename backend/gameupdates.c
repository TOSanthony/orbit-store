/* Which installed games have a newer patch on Sony's servers.
 *
 * Reads the installed games from the console's app.db, downloads each game's version.xml from
 * Sony, and keeps the answer for the TV app and the browser UI. It only reads: nothing is
 * downloaded or installed here, and a failure of one game never affects the others. */
#include "appdb.h"
#include "ca.h"
#include "orbit.h"
#include "patchfeed.h"
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Sony signs its download servers with its own root, which public CA lists do not carry. */
static const char sony_root[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIID0jCCArqgAwIBAgIBADANBgkqhkiG9w0BAQUFADBUMQswCQYDVQQGEwJKUDEp\n"
    "MCcGA1UEChMgU29ueSBDb21wdXRlciBFbnRlcnRhaW5tZW50IEluYy4xGjAYBgNV\n"
    "BAMTEVNDRUkgRE5BUyBSb290IDA1MB4XDTA0MDcxMjA5MDExOVoXDTM3MTIwNjA5\n"
    "MDExOVowVDELMAkGA1UEBhMCSlAxKTAnBgNVBAoTIFNvbnkgQ29tcHV0ZXIgRW50\n"
    "ZXJ0YWlubWVudCBJbmMuMRowGAYDVQQDExFTQ0VJIEROQVMgUm9vdCAwNTCCASIw\n"
    "DQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBANmPeza8PwCqlI7esOGIkoSESnIN\n"
    "g72ZD3Ut63jy7SdothPIvGBqVZWYkIpqJYJd1I4Nh//IpXQCQL0PnJLrh9BBeowq\n"
    "Muf5NNq3Us80Ihiu9CvNEAEO18g3OFV1TYdSwQ5zUsk33OUeI7h4aBPDVcZXYeHt\n"
    "dbPLqe4K8igian5prrAD5S6h28t8aAm+qMWRo+bW25B/841XwDGBP7/IxZv8Yoio\n"
    "rCo80CVYe6lGoU08eeqQiaHI5zAF281DWZSoVfLjJUEWmEnxqr8aOhszRGePi+Ei\n"
    "7UQjHDuZX9rLhDI1zAND+BA259tn/iwOqVXe20OccJllHJcG4Ecmd98f5qMCAwEA\n"
    "AaOBrjCBqzAdBgNVHQ4EFgQUxlahM1tPzoN3YgVEhm0gV7Wv2twwfAYDVR0jBHUw\n"
    "c4AUxlahM1tPzoN3YgVEhm0gV7Wv2tyhWKRWMFQxCzAJBgNVBAYTAkpQMSkwJwYD\n"
    "VQQKEyBTb255IENvbXB1dGVyIEVudGVydGFpbm1lbnQgSW5jLjEaMBgGA1UEAxMR\n"
    "U0NFSSBETkFTIFJvb3QgMDWCAQAwDAYDVR0TBAUwAwEB/zANBgkqhkiG9w0BAQUF\n"
    "AAOCAQEACZPihjwXA27wJ03tEKcHAeFLi8aBw2ysH4GwuH1dWb3UpuznWOB0iQT1\n"
    "wQocnEFYCJx5XFEnj4aLWpSHLEq/sSO+my+aPoTEsy20ajF+YLYZm0bZxH50CJYh\n"
    "rkET4C2aC0XvhGp9k1JQ1o0W6+cFT5LTlXapsq8Btt31t+XDPX7RqGV4WGekt3hM\n"
    "T7xRc7JWXdAQijIrbYi8mtbM07KEGnPU6IT8C47+0mSurpwLOoWL1tPgo6ePpLNi\n"
    "c4quUMgh9RXVjeTyXOMmyYdeUm2gt7qErvQONli+6Epmhm0A2khpIMHSpQjTE8gV\n"
    "rZp42a6+zg1iYy2vFBOmiQ17GRUl0A==\n"
    "-----END CERTIFICATE-----\n";
#define APP_DB "/system_data/priv/mms/app.db"
#define MAX_GAMES 512
#define MAX_XML (256 * 1024)
#define FIRST_CHECK_DELAY 45
#define CHECK_INTERVAL (6 * 60 * 60)

typedef struct {
    char title_id[16], name[96], installed[16], available[16];
    bool blocked; /* a newer patch exists but needs a newer firmware */
    char needs[16];
} Entry;

static struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t thread;
    bool started, stop, busy, refresh;
    time_t checked;
    char error[200];
    Entry *entries;
    size_t count;
    size_t failed;
} gu = {.mutex = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER};

typedef struct {
    char *data;
    size_t size;
} Body;

static bool stopping(void) {
    pthread_mutex_lock(&gu.mutex);
    bool stop = gu.stop;
    pthread_mutex_unlock(&gu.mutex);
    return stop;
}
static size_t collect(char *data, size_t a, size_t b, void *context) {
    Body *body = context;
    size_t n = a * b;
    if (n > MAX_XML - body->size)
        return 0;
    char *next = realloc(body->data, body->size + n + 1);
    if (!next)
        return 0;
    body->data = next;
    memcpy(next + body->size, data, n);
    body->size += n;
    next[body->size] = 0;
    return n;
}
static int interrupted(void *context, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)context, (void)a, (void)b, (void)c, (void)d;
    return stopping();
}
static void sony_ca(struct curl_blob *blob, char **storage) {
    size_t a = strlen(orbit_ca), b = strlen(sony_root);
    *storage = malloc(a + b + 2);
    if (!*storage) {
        blob->data = NULL;
        return;
    }
    memcpy(*storage, orbit_ca, a);
    (*storage)[a] = '\n';
    memcpy(*storage + a + 1, sony_root, b + 1);
    blob->data = *storage;
    blob->len = a + b + 1;
    blob->flags = CURL_BLOB_COPY;
}
/* Downloads one version.xml. Redirects are not followed: Sony serves it directly. */
static bool fetch_xml(const char *url, Body *body) {
    const char *address = url;
    char test_url[900];
#ifdef ORBIT_TEST
    const char *base = getenv("ORBIT_TEST_GAMEUPDATES_BASE");
    if (base && !strncmp(base, "http://127.0.0.1:", 17)) {
        const char *name = strrchr(url, '/');
        snprintf(test_url, sizeof test_url, "%s%s", base, name ? name : "/version.xml");
        address = test_url;
    } else
#endif
    {
        (void)test_url;
        if (!patchfeed_url_allowed(url))
            return false;
    }
    CURL *curl = curl_easy_init();
    if (!curl)
        return false;
    struct curl_blob ca;
    char *storage = NULL;
    sony_ca(&ca, &storage);
    if (!ca.data) {
        curl_easy_cleanup(curl);
        return false;
    }
    body->data = NULL;
    body->size = 0;
    curl_easy_setopt(curl, CURLOPT_URL, address);
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &ca);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, address == url ? "http,https" : "http");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Orbit-Store/" ORBIT_VERSION);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, body);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, interrupted);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    free(storage);
    if (result != CURLE_OK || status != 200) {
        free(body->data);
        body->data = NULL;
        return false;
    }
    return true;
}
/* The version the console reports right after an install; app.db may lag behind it. */
static void installed_version(const char *title_id, const char *from_db, char *out, size_t cap) {
    copy_text(out, cap, from_db);
    const char *suffix[] = {"", "_00"};
    for (int i = 0; i < 2; i++) {
        char path[1100];
        snprintf(path, sizeof path, "%s/user/appmeta/%s%s/param.json", orbit.system_root, title_id,
                 suffix[i]);
        unsigned char *data = NULL;
        size_t length = 0;
        if (read_regular_file(path, 256 * 1024, &data, &length))
            continue;
        cJSON *json = cJSON_ParseWithLength((const char *)data, length);
        free(data);
        const char *version = json ? json_text(json, "contentVersion") : "";
        if (patchfeed_version_valid(version))
            copy_text(out, cap, version);
        cJSON_Delete(json);
        if (patchfeed_version_valid(out))
            return;
    }
}
static void set_error(const char *text) {
    pthread_mutex_lock(&gu.mutex);
    copy_text(gu.error, sizeof gu.error, text);
    pthread_mutex_unlock(&gu.mutex);
}
static void run_check(void) {
    char path[600], error[160] = "";
    snprintf(path, sizeof path, "%s%s", orbit.system_root, APP_DB);
    appdb_game *games = calloc(MAX_GAMES, sizeof *games);
    Entry *found = calloc(MAX_GAMES, sizeof *found);
    if (!games || !found) {
        free(games);
        free(found);
        set_error("Not enough memory to check for updates.");
        return;
    }
    size_t total = 0, count = 0, failed = 0;
    if (appdb_read(path, games, MAX_GAMES, &total, error, sizeof error) < 0) {
        char text[200];
        snprintf(text, sizeof text, "Could not read the installed games: %s", error);
        set_error(text);
        free(games);
        free(found);
        return;
    }
    for (size_t i = 0; i < total && !stopping(); i++) {
        const appdb_game *game = &games[i];
        /* Only PS5 titles: their version.xml is the format patchfeed reads. */
        if (strncmp(game->title_id, "PPSA", 4) || !*game->version_file_uri)
            continue;
        char installed[16];
        installed_version(game->title_id, game->content_version, installed, sizeof installed);
        Body body = {0};
        if (!fetch_xml(game->version_file_uri, &body)) {
            failed++;
            continue;
        }
        patchfeed_package list[PATCHFEED_MAX];
        int n = patchfeed_parse(body.data, body.size, list, PATCHFEED_MAX);
        free(body.data);
        if (n < 0) {
            failed++;
            continue;
        }
        patchfeed_choice choice = patchfeed_choose(list, (size_t)n,
                                                   patchfeed_version_valid(installed) ? installed : "",
                                                   orbit.firmware);
        if (choice.state == PATCHFEED_UP_TO_DATE || !choice.offer)
            continue;
        Entry *e = &found[count++];
        copy_text(e->title_id, sizeof e->title_id, game->title_id);
        copy_text(e->name, sizeof e->name, game->title_name);
        copy_text(e->installed, sizeof e->installed, installed);
        copy_text(e->available, sizeof e->available, choice.offer->version);
        e->blocked = choice.state == PATCHFEED_NEEDS_NEWER_FW;
        if (e->blocked)
            patchfeed_firmware_text(choice.offer->system_ver, e->needs, sizeof e->needs);
    }
    free(games);
    if (stopping()) {
        free(found);
        return;
    }
    pthread_mutex_lock(&gu.mutex);
    free(gu.entries);
    gu.entries = found;
    gu.count = count;
    gu.failed = failed;
    gu.checked = time(NULL);
    gu.error[0] = 0;
    if (failed && total && failed == total)
        copy_text(gu.error, sizeof gu.error, "Sony's servers could not be reached.");
    pthread_mutex_unlock(&gu.mutex);
}
static void *worker(void *unused) {
    (void)unused;
    time_t next = time(NULL) + FIRST_CHECK_DELAY;
#ifdef ORBIT_TEST
    if (getenv("ORBIT_TEST_GAMEUPDATES_BASE"))
        next = time(NULL);
#endif
    pthread_mutex_lock(&gu.mutex);
    while (!gu.stop) {
        if (!gu.refresh && time(NULL) < next) {
            struct timespec until = {.tv_sec = next, .tv_nsec = 0};
            pthread_cond_timedwait(&gu.changed, &gu.mutex, &until);
            continue;
        }
        gu.refresh = false;
        gu.busy = true;
        pthread_mutex_unlock(&gu.mutex);
        run_check();
        pthread_mutex_lock(&gu.mutex);
        gu.busy = false;
        next = time(NULL) + CHECK_INTERVAL;
    }
    pthread_mutex_unlock(&gu.mutex);
    return NULL;
}
int gameupdates_start(void) {
    if (orbit.desktop
#ifdef ORBIT_TEST
        && !getenv("ORBIT_TEST_GAMEUPDATES_BASE")
#endif
    )
        return 0; /* A desktop preview has no console games. */
    pthread_mutex_lock(&gu.mutex);
    if (gu.started) {
        pthread_mutex_unlock(&gu.mutex);
        return 0;
    }
    gu.stop = false;
    int failed = pthread_create(&gu.thread, NULL, worker, NULL);
    gu.started = !failed;
    pthread_mutex_unlock(&gu.mutex);
    return failed ? -1 : 0;
}
void gameupdates_stop(void) {
    pthread_mutex_lock(&gu.mutex);
    bool join = gu.started;
    gu.stop = true;
    pthread_cond_broadcast(&gu.changed);
    pthread_mutex_unlock(&gu.mutex);
    if (join)
        pthread_join(gu.thread, NULL);
    pthread_mutex_lock(&gu.mutex);
    gu.started = false;
    free(gu.entries);
    gu.entries = NULL;
    gu.count = 0;
    pthread_mutex_unlock(&gu.mutex);
}
int gameupdates_action(const cJSON *input, char *error, size_t cap) {
    if (strcmp(json_text(input, "action"), "refresh")) {
        copy_text(error, cap, "Expected refresh action.");
        return 400;
    }
    pthread_mutex_lock(&gu.mutex);
    bool running = gu.started;
    if (running && !gu.busy) {
        gu.refresh = true;
        pthread_cond_broadcast(&gu.changed);
    }
    pthread_mutex_unlock(&gu.mutex);
    if (!running) {
        copy_text(error, cap, "The update check is not running on this device.");
        return 409;
    }
    return 202;
}
cJSON *gameupdates_status(void) {
    cJSON *o = cJSON_CreateObject(), *games = cJSON_CreateArray();
    if (!o || !games) {
        cJSON_Delete(o);
        cJSON_Delete(games);
        return NULL;
    }
    pthread_mutex_lock(&gu.mutex);
    int available = 0;
    for (size_t i = 0; i < gu.count; i++) {
        const Entry *e = &gu.entries[i];
        cJSON *g = cJSON_CreateObject();
        if (!g)
            continue;
        cJSON_AddStringToObject(g, "titleId", e->title_id);
        cJSON_AddStringToObject(g, "name", e->name);
        cJSON_AddStringToObject(g, "installed", e->installed);
        cJSON_AddStringToObject(g, "version", e->available);
        cJSON_AddStringToObject(g, "state", e->blocked ? "needs-firmware" : "available");
        if (e->blocked)
            cJSON_AddStringToObject(g, "firmware", e->needs);
        else
            available++;
        cJSON_AddItemToArray(games, g);
    }
    cJSON_AddBoolToObject(o, "supported", gu.started);
    cJSON_AddBoolToObject(o, "checking", gu.busy);
    cJSON_AddNumberToObject(o, "checked", (double)gu.checked);
    cJSON_AddNumberToObject(o, "failed", (double)gu.failed);
    cJSON_AddStringToObject(o, "error", gu.error);
    pthread_mutex_unlock(&gu.mutex);
    cJSON_AddNumberToObject(o, "count", available);
    cJSON_AddItemToObject(o, "games", games);
    return o;
}
