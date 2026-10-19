/* Which installed games have a newer patch on Sony's servers.
 *
 * Reads the installed games from the console's app.db, downloads each game's version.xml from
 * Sony, and keeps the answer for the TV app and the browser UI. It only reads: nothing is
 * downloaded or installed here, and a failure of one game never affects the others. */
#include "appdb.h"
#include "orbit.h"
#include "patchfeed.h"
#include "patchmanifest.h"
#include "sonynet.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define APP_DB "/system_data/priv/mms/app.db"
#define MAX_GAMES 512
#define MAX_XML (256 * 1024)
#define FIRST_CHECK_DELAY 45
#define CHECK_INTERVAL (6 * 60 * 60)

typedef struct {
    char title_id[16], name[96], installed[16], available[16];
    bool blocked; /* a newer patch exists but needs a newer firmware */
    char needs[16];
    char manifest[768]; /* the JSON listing the patch's pieces */
    int64_t size;       /* the patch's size in bytes, 0 when Sony's list could not be read */
} Entry;

static struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t thread;
    bool started, stop, busy, refresh;
    time_t checked;
    char error[200];
    char detail[200]; /* why the first game of the last check failed */
    Entry *entries;
    size_t count;
    size_t failed;
    size_t ps4_seen, ps4_offered;
    char ps4_skipped[200]; /* PS4 games whose installed version could not be read, with what app.db holds */
} gu = {.mutex = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER};

static bool stopping(void) {
    pthread_mutex_lock(&gu.mutex);
    bool stop = gu.stop;
    pthread_mutex_unlock(&gu.mutex);
    return stop;
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
    size_t total = 0, count = 0, failed = 0, ps4_seen = 0, ps4_offered = 0;
    char ps4_skipped[200] = "";
    char first_failure[200] = "";
    if (appdb_read(path, games, MAX_GAMES, &total, error, sizeof error) < 0) {
        char text[200];
        snprintf(text, sizeof text, "Could not read the installed games: %s", error);
        set_error(text);
        free(games);
        free(found);
        return;
    }
    cJSON *saved = NULL;
    {
        char *saved_text = NULL;
        size_t saved_size = 0;
        if (nas_game_patches(&saved_text, &saved_size) == 1)
            saved = cJSON_ParseWithLength(saved_text, saved_size);
        free(saved_text);
        if (saved && !cJSON_IsObject(saved)) {
            cJSON_Delete(saved);
            saved = NULL;
        }
    }
    for (size_t i = 0; i < total && !stopping(); i++) {
        const appdb_game *game = &games[i];
        /* PS5 titles: Sony's version.xml is the format patchfeed reads. PS4 titles have no such list
         * here; only an update .pkg on the person's NAS can offer them an update. */
        bool ps4 = !strncmp(game->title_id, "CUSA", 4);
        if (ps4) {
            ps4_seen++;
            if (!patchfeed_app_version_valid(game->content_version)) {
                size_t used = strlen(ps4_skipped);
                snprintf(ps4_skipped + used, sizeof ps4_skipped - used, "%s%s=%s", used ? " " : "", game->title_id,
                         *game->content_version ? game->content_version : "?");
                continue;
            }
        } else if (strncmp(game->title_id, "PPSA", 4) || !*game->version_file_uri)
            continue;
        char installed[16];
        installed_version(game->title_id, game->content_version, installed, sizeof installed);
        Entry *e = NULL;
        if (!ps4) {
            SonyBody body = {0};
            char why[160] = "";
            if (!sony_get(game->version_file_uri, MAX_XML, &body, stopping, why, sizeof why)) {
                if (!failed)
                    snprintf(first_failure, sizeof first_failure, "%s: %s", game->title_id, why);
                failed++;
                continue;
            }
            patchfeed_package list[PATCHFEED_MAX];
            int n = patchfeed_parse(body.data, body.size, list, PATCHFEED_MAX);
            free(body.data);
            if (n < 0) {
                if (!failed)
                    snprintf(first_failure, sizeof first_failure, "%s: not a Sony update list", game->title_id);
                failed++;
                continue;
            }
            patchfeed_choice choice = patchfeed_choose(list, (size_t)n,
                                                       patchfeed_version_valid(installed) ? installed : "",
                                                       orbit.firmware);
            if (choice.state != PATCHFEED_UP_TO_DATE && choice.offer) {
                e = &found[count++];
                copy_text(e->title_id, sizeof e->title_id, game->title_id);
                copy_text(e->name, sizeof e->name, game->title_name);
                copy_text(e->installed, sizeof e->installed, installed);
                copy_text(e->available, sizeof e->available, choice.offer->version);
                copy_text(e->manifest, sizeof e->manifest, choice.offer->manifest_url);
                e->blocked = choice.state == PATCHFEED_NEEDS_NEWER_FW;
                if (e->blocked)
                    patchfeed_firmware_text(choice.offer->system_ver, e->needs, sizeof e->needs);
            }
            /* A version and Sony link saved on the person's NAS page wins when it is newer than what
             * is installed and than anything Sony offers this console. */
            const cJSON *saved_game = saved ? cJSON_GetObjectItemCaseSensitive(saved, game->title_id) : NULL;
            const char *saved_version = saved_game ? json_text(saved_game, "version") : "";
            const char *saved_link = saved_game ? json_text(saved_game, "link") : "";
            char saved_manifest[768];
            if (patchfeed_version_valid(saved_version) && patchfeed_manifest_from_link(saved_link, saved_manifest, sizeof saved_manifest) &&
                (!patchfeed_version_valid(installed) || strcmp(saved_version, installed) > 0) &&
                (!e || e->blocked || strcmp(saved_version, e->available) > 0)) {
                if (!e) {
                    e = &found[count++];
                    copy_text(e->title_id, sizeof e->title_id, game->title_id);
                    copy_text(e->name, sizeof e->name, game->title_name);
                    copy_text(e->installed, sizeof e->installed, installed);
                }
                copy_text(e->available, sizeof e->available, saved_version);
                copy_text(e->manifest, sizeof e->manifest, saved_manifest);
                e->blocked = false;
                e->needs[0] = 0;
            }
        }
        /* A plain .pkg listed as an update in the NAS index (packages/ps5/updates/) counts as an update
         * too, when it is newer than the installed version and than what Sony or the saved link offer. */
        char nas_id[24], nas_version[16];
        int64_t nas_size = 0;
        const char *floor = e && !e->blocked ? e->available : (patchfeed_version_valid(installed) || patchfeed_app_version_valid(installed) ? installed : "");
        if (nas_best_update(game->title_id, floor, e && !e->blocked ? e->available : "", nas_id, sizeof nas_id, nas_version, sizeof nas_version, &nas_size)) {
            if (!e) {
                e = &found[count++];
                copy_text(e->title_id, sizeof e->title_id, game->title_id);
                copy_text(e->name, sizeof e->name, game->title_name);
                copy_text(e->installed, sizeof e->installed, installed);
            }
            copy_text(e->available, sizeof e->available, nas_version);
            snprintf(e->manifest, sizeof e->manifest, "nas:%s", nas_id);
            e->blocked = false;
            e->needs[0] = 0;
            e->size = nas_size;
            if (ps4)
                ps4_offered++;
        }
        if (e && !e->blocked && strncmp(e->manifest, "nas:", 4)) {
            /* The size is only in the manifest; a failure here just leaves the size out. */
            SonyBody list_body = {0};
            char ignore[160] = "", reason[100] = "";
            if (sony_get(e->manifest, 4u * 1024 * 1024, &list_body, stopping, ignore, sizeof ignore)) {
                patchmanifest m;
                if (!patchmanifest_parse(list_body.data, list_body.size, &m, reason, sizeof reason)) {
                    e->size = m.total;
                    patchmanifest_free(&m);
                }
                free(list_body.data);
            }
        }
    }
    cJSON_Delete(saved);
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
    gu.ps4_seen = ps4_seen;
    gu.ps4_offered = ps4_offered;
    copy_text(gu.ps4_skipped, sizeof gu.ps4_skipped, ps4_skipped);
    copy_text(gu.detail, sizeof gu.detail, first_failure);
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
    const char *action = json_text(input, "action");
    if (!strcmp(action, "download"))
        return patchdl_start(json_text(input, "titleId"), error, cap);
    if (!strcmp(action, "cancel")) {
        patchdl_cancel();
        return 202;
    }
    if (strcmp(action, "refresh")) {
        copy_text(error, cap, "Expected refresh action.");
        return 400;
    }
    pthread_mutex_lock(&gu.mutex);
    bool running = gu.started;
    if (running && !gu.busy) {
        gu.refresh = true;
        gu.busy = true; /* shown at once; the worker confirms it */
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
    /* Patches this console can install first, then those that need a newer firmware. */
    for (size_t n = 0; n < gu.count * 2; n++) {
        const Entry *e = &gu.entries[n % gu.count];
        if (e->blocked != (n >= gu.count))
            continue;
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
        else {
            cJSON_AddNumberToObject(g, "size", (double)e->size);
            available++;
        }
        cJSON_AddItemToArray(games, g);
    }
    cJSON_AddBoolToObject(o, "supported", gu.started);
    cJSON_AddBoolToObject(o, "checking", gu.busy);
    cJSON_AddNumberToObject(o, "checked", (double)gu.checked);
    cJSON_AddNumberToObject(o, "failed", (double)gu.failed);
    cJSON_AddStringToObject(o, "error", gu.error);
    cJSON_AddStringToObject(o, "detail", gu.detail);
    cJSON *ps4 = cJSON_AddObjectToObject(o, "ps4");
    cJSON_AddNumberToObject(ps4, "installed", (double)gu.ps4_seen);
    cJSON_AddNumberToObject(ps4, "offered", (double)gu.ps4_offered);
    cJSON_AddStringToObject(ps4, "unreadableVersion", gu.ps4_skipped);
    pthread_mutex_unlock(&gu.mutex);
    cJSON_AddNumberToObject(o, "count", available);
    cJSON_AddItemToObject(o, "games", games);
    cJSON *job = patchdl_status();
    if (job)
        cJSON_AddItemToObject(o, "job", job);
    return o;
}

bool gameupdates_offer(const char *title_id, char *name, size_t name_cap, char *version, size_t version_cap,
                       char *manifest, size_t manifest_cap) {
    bool found = false;
    pthread_mutex_lock(&gu.mutex);
    for (size_t i = 0; i < gu.count; i++) {
        const Entry *e = &gu.entries[i];
        if (e->blocked || strcmp(e->title_id, title_id))
            continue;
        copy_text(name, name_cap, e->name);
        copy_text(version, version_cap, e->available);
        copy_text(manifest, manifest_cap, e->manifest);
        found = true;
        break;
    }
    pthread_mutex_unlock(&gu.mutex);
    return found;
}
void gameupdates_request_refresh(void) {
    pthread_mutex_lock(&gu.mutex);
    if (gu.started && !gu.busy) {
        gu.refresh = true;
        gu.busy = true;
        pthread_cond_broadcast(&gu.changed);
    }
    pthread_mutex_unlock(&gu.mutex);
}
