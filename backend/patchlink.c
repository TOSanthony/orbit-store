/* Picking a game update on prosperopatches.com from the console.
 *
 * The site hides its download links behind a human check, so Orbit does not ask it for anything. It opens
 * the game's page in the PS5 browser; the person passes the check, picks a version and selects its CRC
 * link. The browser then asks Sony's server for that address, and the network process holds it for a
 * moment: Orbit reads only addresses of Sony's patch servers for this game, takes the one the person
 * selected and hands its manifest to the game updates list. Nothing else of the browser is kept. */
#include "orbit.h"
#include "browser_platform.h"
#include "patchfeed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WAIT_SECONDS 600

static struct {
    pthread_t thread;
    bool started, busy, stop;
    atomic_bool cancel;
    char id[33], title_id[32], state[16], message[200], manifest[768];
} pl = {0};

static void state_locked(const char *state, const char *message) {
    copy_text(pl.state, sizeof pl.state, state);
    copy_text(pl.message, sizeof pl.message, message);
}
static bool available(void) {
#ifdef ORBIT_TEST
    return true;
#else
    return !orbit.desktop;
#endif
}
static bool active(void) {
    pthread_mutex_lock(&orbit.mutex);
    bool ok = !pl.stop && !orbit.stop && !atomic_load(&pl.cancel);
    pthread_mutex_unlock(&orbit.mutex);
    return ok;
}

#ifndef ORBIT_TEST
typedef struct {
    const char *title_id;
    char manifest[768];
} Match;

static bool url_char(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '/' || c == '.' ||
           c == '-' || c == '_' || c == ':';
}
/* A link of Sony's patch server for this game that names a manifest (.crc or .json). Memory holds the
 * text as plain bytes or as UTF-16, so both are read. */
static bool match_link(void *context, const unsigned char *bytes, size_t length) {
    Match *match = context;
    static const char prefix[] = "https://sgst.prod.dl.playstation.net/";
    const size_t plen = sizeof prefix - 1;
    char needle[48];
    snprintf(needle, sizeof needle, "/%s_00/", match->title_id);
    for (size_t step = 1; step <= 2; step++) {
        for (size_t i = 0; i + plen * step < length; i++) {
            if (bytes[i] != 'h')
                continue;
            bool same = true;
            for (size_t j = 0; j < plen && same; j++)
                same = bytes[i + j * step] == (unsigned char)prefix[j] && (step == 1 || !bytes[i + j * step + 1]);
            if (!same)
                continue;
            char candidate[760];
            size_t n = 0;
            for (; n + 1 < sizeof candidate && i + (n + 1) * step <= length; n++) {
                unsigned char c = bytes[i + n * step];
                if (!url_char(c) || (step == 2 && bytes[i + n * step + 1]))
                    break;
                candidate[n] = (char)c;
            }
            candidate[n] = 0;
            if (strstr(candidate, needle) && patchfeed_manifest_from_link(candidate, match->manifest, sizeof match->manifest))
                return true;
        }
    }
    return false;
}
static bool native_active(void *context) {
    return active() && probe_target_active(context);
}
static time_t monotonic(void) {
    struct timespec now;
    return clock_gettime(CLOCK_MONOTONIC, &now) ? 0 : now.tv_sec;
}
#endif

/* Opens the page and waits for the selected link. Returns true with the manifest address in `manifest`. */
static bool capture(const char *title_id, char *manifest, size_t manifest_cap, char *error, size_t error_cap) {
#ifdef ORBIT_TEST
    const char *candidate = getenv("ORBIT_TEST_PATCHLINK_URL");
    if (!candidate || !patchfeed_manifest_from_link(candidate, manifest, manifest_cap)) {
        copy_text(error, error_cap, "No matching link was found.");
        return false;
    }
    (void)title_id;
    return true;
#else
    char page[96];
    snprintf(page, sizeof page, "https://prosperopatches.com/%s", title_id);
    if (probe_browser_open(page)) {
        copy_text(error, error_cap, "The PS5 browser could not open. Return to Orbit and try again.");
        return false;
    }
    ProbeTarget target = {.cancel = &pl.cancel, .deadline = monotonic() + WAIT_SECONDS};
    int found = 0;
    for (unsigned i = 0; i < 100 && active(); i++) {
        found = probe_target_find(&target);
        if (found)
            break;
        struct timespec t = {.tv_nsec = 100000000};
        nanosleep(&t, NULL);
    }
    if (found != 1) {
        copy_text(error, error_cap, "The PS5 browser is unavailable or cannot be identified safely.");
        return false;
    }
    Match match = {.title_id = title_id};
    ProbeReader reader = {.context = &target, .read = probe_target_read, .active = native_active,
                          .match = match_link, .match_context = &match};
    while (native_active(&target)) {
        reader.remaining = 128U * 1024U * 1024U; /* one pass; the next one starts again */
        ProbeResult result = probe_target_scan(&target, &reader, "https://");
        if (result == PROBE_FOUND && active()) {
            copy_text(manifest, manifest_cap, match.manifest);
            memset(&match, 0, sizeof match);
            return true;
        }
        if (result == PROBE_STOPPED || result == PROBE_INVALID)
            break;
    }
    copy_text(error, error_cap, target.identity_changed
                                    ? "The PS5 browser restarted. Return to Orbit and try again."
                                    : "No link was selected in time. Return to Orbit to try again.");
    return false;
#endif
}

static void *worker(void *unused) {
    (void)unused;
    char title_id[32], error[200] = "", manifest[768] = "";
    pthread_mutex_lock(&orbit.mutex);
    copy_text(title_id, sizeof title_id, pl.title_id);
    pthread_mutex_unlock(&orbit.mutex);
    bool ok = capture(title_id, manifest, sizeof manifest, error, sizeof error);
    pthread_mutex_lock(&orbit.mutex);
    if (pl.stop || atomic_load(&pl.cancel)) {
        state_locked("cancelled", "Selection cancelled.");
    } else if (ok) {
        pthread_mutex_unlock(&orbit.mutex);
#ifndef ORBIT_TEST
        /* The page that replaces the one the person selected opens first, with nothing in front of it: the
         * browser cannot show a manifest, and the local page is fixed text, so what follows can happen while
         * the browser switches over. No address from the browser is carried back. */
        char home[64];
        snprintf(home, sizeof home, "http://127.0.0.1:%d/picked", (int)orbit.port);
        (void)probe_browser_open(home);
#endif
        gameupdates_choose(title_id, manifest);
        /* The update starts at once, or waits its turn when another one is running. */
        char started_error[120] = "";
        (void)patchdl_enqueue(title_id, started_error, sizeof started_error);
        pthread_mutex_lock(&orbit.mutex);
        copy_text(pl.manifest, sizeof pl.manifest, manifest);
        state_locked("captured", "Update selected and added to the downloads.");
    } else {
        state_locked("failed", error[0] ? error : "No link was selected.");
    }
    pl.busy = false;
    pthread_mutex_unlock(&orbit.mutex);
    return NULL;
}

int patchlink_action(const cJSON *input, char *error, size_t cap) {
    const char *action = json_text(input, "action");
    int code = 202;
    bool join = false;
    pthread_t old;
    if (strcmp(action, "pickCancel")) {
        /* A new selection replaces one still waiting (the person closed that page and opened another). */
        bool replace = false;
        pthread_mutex_lock(&orbit.mutex);
        if (pl.busy && pl.started) {
            atomic_store(&pl.cancel, true);
            old = pl.thread;
            pl.started = false;
            replace = true;
        }
        pthread_mutex_unlock(&orbit.mutex);
        if (replace)
            pthread_join(old, NULL);
    }
    pthread_mutex_lock(&orbit.mutex);
    if (!strcmp(action, "pickCancel")) {
        if (pl.busy)
            atomic_store(&pl.cancel, true);
    } else if (!available()) {
        copy_text(error, cap, "Picking an update is available on the PS5 only.");
        code = 409;
    } else if (pl.busy) {
        copy_text(error, cap, "Finish or cancel the current selection first.");
        code = 409;
    } else {
        const char *title_id = json_text(input, "titleId");
        bool valid = strlen(title_id) == 9 && !strncmp(title_id, "PPSA", 4);
        for (int i = 4; valid && i < 9; i++)
            valid = title_id[i] >= '0' && title_id[i] <= '9';
        if (!valid) {
            copy_text(error, cap, "Expected a PS5 title id such as PPSA01234.");
            code = 400;
        } else {
            if (pl.started) {
                old = pl.thread;
                join = true;
                pl.started = false;
            }
            copy_text(pl.title_id, sizeof pl.title_id, title_id);
            pl.manifest[0] = 0;
            random_hex(pl.id, 16);
            atomic_store(&pl.cancel, false);
            pl.busy = true;
            state_locked("waiting", "Pick the version in the browser, then select its CRC link.");
        }
    }
    pthread_mutex_unlock(&orbit.mutex);
    if (code == 202 && strcmp(action, "pickCancel")) {
        if (join)
            pthread_join(old, NULL);
        pthread_attr_t attr;
        int rc = pthread_attr_init(&attr);
        if (!rc)
            rc = pthread_attr_setstacksize(&attr, ORBIT_THREAD_STACK);
        if (!rc)
            rc = pthread_create(&pl.thread, &attr, worker, NULL);
        pthread_attr_destroy(&attr);
        pthread_mutex_lock(&orbit.mutex);
        pl.started = !rc;
        if (rc) {
            pl.busy = false;
            state_locked("failed", "The selection could not start. Try again.");
            copy_text(error, cap, pl.message);
            code = 503;
        }
        pthread_mutex_unlock(&orbit.mutex);
    }
    return code;
}

cJSON *patchlink_status(void) {
    cJSON *o = cJSON_CreateObject();
    pthread_mutex_lock(&orbit.mutex);
    cJSON_AddBoolToObject(o, "active", pl.busy);
    cJSON_AddStringToObject(o, "id", pl.id);
    cJSON_AddStringToObject(o, "titleId", pl.title_id);
    cJSON_AddStringToObject(o, "state", pl.state[0] ? pl.state : "idle");
    cJSON_AddStringToObject(o, "message", pl.message);
    pthread_mutex_unlock(&orbit.mutex);
    return o;
}

void patchlink_stop(void) {
    pthread_mutex_lock(&orbit.mutex);
    bool join = pl.started;
    pl.stop = true;
    atomic_store(&pl.cancel, true);
    pthread_mutex_unlock(&orbit.mutex);
    if (join)
        pthread_join(pl.thread, NULL);
}
