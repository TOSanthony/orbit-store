/* NAS source: the person's own HTTPS server publishes an index of the game files it hosts.
 *
 * Orbit lists those files as the "nas" source and downloads them through the same guarded
 * path as the other providers, restricted to the single origin (host and port) of the index
 * address the person saved. The index and every file address must stay on that origin, Orbit
 * never follows a redirect away from it, and provider_open_socket still refuses private
 * addresses. The list lives beside, never inside, the signed catalogue. */
#include "orbit.h"
#include "ca.h"
#include "patchfeed.h"
#include "provider_network.h"
#include <ctype.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define NAS_INDEX_LIMIT (12 * 1024 * 1024)
#define NAS_REFRESH_SECONDS (30 * 60)
#define NAS_RETRY_SECONDS (5 * 60)

/* The configured origin. Leaf lock: taken after orbit.mutex, never before it. */
static struct {
    pthread_mutex_t lock;
    char host[256], port[8];
    bool set;
} origin = {.lock = PTHREAD_MUTEX_INITIALIZER};

/* Protected by orbit.mutex. */
static struct {
    pthread_t thread;
    pthread_cond_t wake;
    bool started, stop, pending, busy;
    time_t next, checked;
    char error[192];
} nas = {.wake = PTHREAD_COND_INITIALIZER};

bool provider_nas_source(const char *source) {
    return source && !strcmp(source, "nas");
}

/* Scheme, host and port of an address that could name the NAS. Always https; host
 * names only; no credentials or fragment. Tests may use http on loopback. */
static bool split(const char *url, char *host, size_t host_cap, char *port, size_t port_cap) {
    if (!url || !*url || strlen(url) > 8192) return false;
    for (const unsigned char *p = (const unsigned char *)url; *p; p++)
        if (*p <= 32 || *p >= 127 || *p == '\\') return false;
    CURLU *u = curl_url();
    if (!u) return false;
    char *scheme = NULL, *h = NULL, *pt = NULL, *user = NULL, *password = NULL, *fragment = NULL;
    bool ok = !curl_url_set(u, CURLUPART_URL, url, 0) &&
              !curl_url_get(u, CURLUPART_SCHEME, &scheme, 0) &&
              !curl_url_get(u, CURLUPART_HOST, &h, 0) &&
              !curl_url_get(u, CURLUPART_PORT, &pt, CURLU_DEFAULT_PORT);
    curl_url_get(u, CURLUPART_USER, &user, 0);
    curl_url_get(u, CURLUPART_PASSWORD, &password, 0);
    curl_url_get(u, CURLUPART_FRAGMENT, &fragment, 0);
    ok = ok && scheme && h && pt && *h && !user && !password && !fragment &&
         strlen(h) < host_cap && strlen(pt) < port_cap;
    if (ok) {
#ifdef ORBIT_TEST
        ok = !strcmp(scheme, "https") || (!strcmp(scheme, "http") && !strcmp(h, "127.0.0.1"));
#else
        ok = !strcmp(scheme, "https");
#endif
    }
    for (const char *p = ok ? h : ""; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '.' && *p != '-') ok = false;
    if (ok) {
        copy_text(host, host_cap, h);
        copy_text(port, port_cap, pt);
    }
    curl_free(scheme); curl_free(h); curl_free(pt); curl_free(user);
    curl_free(password); curl_free(fragment); curl_url_cleanup(u);
    return ok;
}

bool nas_url_valid(const char *url) {
    char host[256], port[8];
    return url && strlen(url) < ORBIT_NAS_URL_MAX && split(url, host, sizeof host, port, sizeof port);
}

bool nas_url_allowed(const char *url) {
    char host[256], port[8];
    if (!split(url, host, sizeof host, port, sizeof port)) return false;
    pthread_mutex_lock(&origin.lock);
    bool same = origin.set && !strcasecmp(host, origin.host) && !strcmp(port, origin.port);
    pthread_mutex_unlock(&origin.lock);
    return same;
}

void nas_origin_set(const char *index_url) {
    char host[256], port[8];
    bool valid = nas_url_valid(index_url) &&
                 split(index_url, host, sizeof host, port, sizeof port);
    pthread_mutex_lock(&origin.lock);
    origin.set = valid;
    copy_text(origin.host, sizeof origin.host, valid ? host : "");
    copy_text(origin.port, sizeof origin.port, valid ? port : "");
    pthread_mutex_unlock(&origin.lock);
}

static bool active_locked(void) {
    return (orbit.sources.enabled & ORBIT_SOURCE_NAS) && orbit.sources.nas_url[0] &&
           orbit.sources.notice_version == ORBIT_SOURCE_NOTICE_VERSION &&
           orbit.sources.acknowledged_at > 0;
}

void nas_changed_locked(void) {
    nas.pending = true;
    nas.next = 0;
    nas.error[0] = 0;
    pthread_cond_broadcast(&nas.wake);
}

/* The catalogue refresh button also re-reads the NAS index when My NAS is on. Returns whether a
 * read was started or is already running. */
bool nas_refresh_locked(void) {
    if (!active_locked()) return false;
    if (!nas.busy && !nas.pending) nas_changed_locked();
    return true;
}

bool nas_busy_locked(void) {
    return nas.busy || nas.pending;
}

size_t nas_count_locked(void) {
    return orbit.nas_count;
}

/* Never exposes the saved path: it may be the only secret protecting the files. */
void nas_status_locked(cJSON *o) {
    char host[256] = "", port[8] = "";
    pthread_mutex_lock(&origin.lock);
    if (origin.set) {
        copy_text(host, sizeof host, origin.host);
        copy_text(port, sizeof port, origin.port);
    }
    pthread_mutex_unlock(&origin.lock);
    char label[270];
    snprintf(label, sizeof label, "%s", host);
    if (*host && strcmp(port, "443")) snprintf(label, sizeof label, "%s:%s", host, port);
    cJSON_AddBoolToObject(o, "nasConfigured", orbit.sources.nas_url[0] != 0);
    cJSON_AddStringToObject(o, "nasHost", label);
    cJSON_AddNumberToObject(o, "nasCount", (double)orbit.nas_count);
    cJSON_AddNumberToObject(o, "nasCheckedAt", (double)nas.checked);
    cJSON_AddBoolToObject(o, "nasBusy", nas.busy || nas.pending);
    cJSON_AddStringToObject(o, "nasError", nas.error);
}

/* Resolves an entry's `url` or `path`, absolute or relative to the index, against the index. */
static bool resolve(const char *index_url, const char *reference, char *out, size_t cap) {
    CURLU *u = curl_url();
    if (!u) return false;
    char *full = NULL;
    bool ok = !curl_url_set(u, CURLUPART_URL, index_url, 0) &&
              !curl_url_set(u, CURLUPART_URL, reference, CURLU_URLENCODE) &&
              !curl_url_get(u, CURLUPART_URL, &full, 0) && full && strlen(full) < cap;
    if (ok) copy_text(out, cap, full);
    curl_free(full);
    curl_url_cleanup(u);
    return ok;
}

static bool clean(const char *s, size_t max) {
    size_t n = strlen(s);
    if (!n || n > max) return false;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if (*p < 32 || *p == 127) return false;
    return true;
}

/* An index row becomes a catalogue-shaped row. release_parse applies the same identity,
 * size, format and title-ID rules as the signed catalogue and, through
 * provider_url_supported, requires the file to be on the configured origin. */
static bool entry(const char *index_url, const cJSON *x, Release *r, cJSON **view) {
    if (!cJSON_IsObject(x)) return false;
    cJSON *e = cJSON_Duplicate(x, true);
    if (!e) return false;
    char url[2048], artwork[2048];
    const char *reference = json_text(e, "url");
    if (!*reference) reference = json_text(e, "path");
    bool ok = *reference && resolve(index_url, reference, url, sizeof url);
    if (ok) {
        cJSON_DeleteItemFromObjectCaseSensitive(e, "url");
        cJSON_DeleteItemFromObjectCaseSensitive(e, "sourceId");
        cJSON_AddStringToObject(e, "url", url);
        cJSON_AddStringToObject(e, "sourceId", "nas");
        if (!*json_text(e, "id")) {
            /* FNV-1a of the address: stable for the same file across refreshes. */
            uint64_t h = 1469598103934665603ULL;
            for (const char *p = url; *p; p++) h = (h ^ (unsigned char)*p) * 1099511628211ULL;
            char id[32];
            snprintf(id, sizeof id, "nas-%016llx", (unsigned long long)h);
            cJSON_AddStringToObject(e, "id", id);
        }
        const char *title_id = json_text(e, "titleId");
        if (!*json_text(e, "gameId") && strlen(title_id) == 9) {
            char game[16];
            for (size_t i = 0; i < 9; i++) game[i] = (char)toupper((unsigned char)title_id[i]);
            game[9] = 0;
            cJSON_AddStringToObject(e, "gameId", game);
        }
        /* A DLC row of the index also shows in Add-ons: it gets an add-on identity (its own id, the
         * game's title as the base) when that validates, and stays a plain row when it does not. */
        const char *tagline = json_text(e, "tagline");
        if (!strcmp(tagline, "DLC") && !*json_text(e, "addonId") && strlen(title_id) == 9) {
            cJSON *add = cJSON_Duplicate(e, true);
            if (add) {
                char game[48], base_name[256];
                snprintf(game, sizeof game, "%s-%s", json_text(e, "gameId"), json_text(e, "id"));
                copy_text(base_name, sizeof base_name, json_text(e, "baseGameTitle"));
                if (!*base_name) copy_text(base_name, sizeof base_name, json_text(e, "gameName"));
                if (!*base_name) copy_text(base_name, sizeof base_name, json_text(e, "title"));
                cJSON_DeleteItemFromObjectCaseSensitive(add, "gameId");
                cJSON_AddStringToObject(add, "gameId", game);
                cJSON_AddStringToObject(add, "addonId", game);
                cJSON_AddStringToObject(add, "baseGameId", json_text(e, "gameId"));
                cJSON_AddStringToObject(add, "baseGameTitle", base_name);
                cJSON_AddStringToObject(add, "addonType", "content-pack");
                cJSON *requirements = cJSON_AddObjectToObject(add, "requirements");
                cJSON *ids = requirements ? cJSON_AddArrayToObject(requirements, "titleIds") : NULL;
                if (ids) {
                    cJSON_AddItemToArray(ids, cJSON_CreateString(json_text(e, "titleId")));
                    cJSON_AddStringToObject(requirements, "edition", "");
                    cJSON_AddStringToObject(requirements, "minimumVersion", "");
                    cJSON_AddStringToObject(requirements, "notes", "");
                    if (!release_parse(add, r, false)) {
                        cJSON_Delete(e);
                        e = add;
                        add = NULL;
                        ok = true;
                    }
                }
                cJSON_Delete(add);
            }
        }
        if (!ok || !r->addon_id[0]) {
            /* The index writes a contentId on every .pkg row. Only an add-on may carry one, so a plain
             * game, update or app row keeps its place by leaving it out. */
            if (!*json_text(e, "addonId")) cJSON_DeleteItemFromObjectCaseSensitive(e, "contentId");
            ok = !release_parse(e, r, false);
        }
    }
    if (ok) {
        cJSON *v = release_json(r);
        ok = v != NULL;
        if (ok) {
            cJSON_AddStringToObject(v, "provider", "nas");
            static const char *const optional[] = {"genre", "tagline", "description", "publisher",
                                                   "releaseDate", "version", "addedAt", "region"};
            for (size_t k = 0; k < sizeof optional / sizeof optional[0]; k++) {
                const char *s = json_text(e, optional[k]);
                if (clean(s, 4096)) cJSON_AddStringToObject(v, optional[k], s);
                else cJSON_AddNullToObject(v, optional[k]);
            }
            const char *notes = json_text(e, "notes");
            if (clean(notes, 4096)) cJSON_AddStringToObject(v, "notes", notes);
            /* Artwork may live on the NAS too; anything else falls back to Orbit's own mark. */
            static const char *const keys[] = {"cover", "hero"};
            static const char *const spares[] = {"coverFallback", "heroFallback"};
            bool has_hero = false;
            for (size_t k = 0; k < 2; k++) {
                const char *ref = json_text(e, keys[k]);
                bool usable = *ref && resolve(index_url, ref, artwork, sizeof artwork) &&
                              nas_url_allowed(artwork);
                cJSON_AddStringToObject(v, keys[k], usable ? artwork : "/orbit.svg");
                if (k == 1) has_hero = usable;
                /* The default image (assets/ next to the index), tried when the first one fails. */
                ref = json_text(e, spares[k]);
                if (*ref && resolve(index_url, ref, artwork, sizeof artwork) && nas_url_allowed(artwork))
                    cJSON_AddStringToObject(v, spares[k], artwork);
                else
                    cJSON_AddNullToObject(v, spares[k]);
            }
            /* "wide" makes Orbit use the hero as the page background. */
            cJSON_AddStringToObject(v, "artworkLayout", has_hero ? "wide" : "ambient");
            /* Date the row was put in the index (the generator's), else now. */
            const char *checked = json_text(e, "sourceCheckedAt");
            char stamp[40];
            if (!clean(checked, 40)) {
                time_t now = time(NULL);
                struct tm utc;
                checked = gmtime_r(&now, &utc) &&
                          strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S+00:00", &utc) ? stamp : "";
            }
            if (*checked) cJSON_AddStringToObject(v, "sourceCheckedAt", checked);
            cJSON_AddBoolToObject(v, "consoleVerified", false);
            *view = v;
        }
    }
    cJSON_Delete(e);
    return ok;
}

static void clear_locked(void) {
    cJSON_Delete(orbit.nas_catalog);
    orbit.nas_catalog = NULL;
    free(orbit.nas_releases);
    orbit.nas_releases = NULL;
    orbit.nas_count = 0;
}

/* Replaces the NAS list as a whole under orbit.mutex. Jobs keep their own release copies. */
static void merge_locked(const char *index_url, const cJSON *doc) {
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(doc, "schemaVersion");
    const cJSON *entries = cJSON_GetObjectItemCaseSensitive(doc, "releases");
    if ((schema && !(cJSON_IsNumber(schema) && schema->valuedouble == 1)) || !cJSON_IsArray(entries)) {
        copy_text(nas.error, sizeof nas.error, "The NAS index is not in the expected format.");
        return;
    }
    if (cJSON_GetArraySize(entries) > ORBIT_NAS_MAX) {
        snprintf(nas.error, sizeof nas.error, "The NAS index lists too many files (maximum %d).", ORBIT_NAS_MAX);
        return;
    }
    int listed = cJSON_GetArraySize(entries);
    Release *parsed = calloc(listed > 0 ? (size_t)listed : 1, sizeof *parsed);   /* only what the index lists, not the maximum */
    cJSON *catalog = cJSON_CreateArray();
    if (!parsed || !catalog) {
        free(parsed);
        cJSON_Delete(catalog);
        copy_text(nas.error, sizeof nas.error, "Not enough memory to read the NAS index.");
        return;
    }
    size_t count = 0, rejected = 0;
    const cJSON *x;
    cJSON_ArrayForEach(x, entries) {
        Release r = {0};
        cJSON *view = NULL;
        bool ok = entry(index_url, x, &r, &view);
        /* Identities must stay unique, and one game cannot span platforms. */
        for (size_t i = 0; ok && i < orbit.release_count; i++)
            if (!strcmp(orbit.releases[i].id, r.id) ||
                (!strcmp(orbit.releases[i].game_id, r.game_id) &&
                 strcmp(orbit.releases[i].platform, r.platform)))
                ok = false;
        for (size_t i = 0; ok && i < count; i++)
            if (!strcmp(parsed[i].id, r.id) ||
                (!strcmp(parsed[i].game_id, r.game_id) && strcmp(parsed[i].platform, r.platform)))
                ok = false;
        if (!ok) {
            cJSON_Delete(view);
            rejected++;
            continue;
        }
        parsed[count++] = r;
        cJSON_AddItemToArray(catalog, view);
    }
    clear_locked();
    orbit.nas_releases = parsed;
    orbit.nas_count = count;
    orbit.nas_catalog = catalog;
    if (rejected)
        snprintf(nas.error, sizeof nas.error,
                 "%zu entr%s skipped: invalid, duplicated or not on your NAS address.", rejected,
                 rejected == 1 ? "y was" : "ies were");
    else if (!count)
        copy_text(nas.error, sizeof nas.error, "The NAS index does not list any game.");
}

typedef struct { char *data; size_t size; } Body;
static size_t collect(char *data, size_t a, size_t b, void *context) {
    Body *body = context;
    if (a && b > SIZE_MAX / a) return 0;
    size_t n = a * b;
    if (n > NAS_INDEX_LIMIT - body->size) return 0;
    char *next = realloc(body->data, body->size + n + 1);
    if (!next) return 0;
    body->data = next;
    memcpy(next + body->size, data, n);
    body->size += n;
    next[body->size] = 0;
    return n;
}
static int progress(void *ctx, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)ctx; (void)a; (void)b; (void)c; (void)d;
    pthread_mutex_lock(&orbit.mutex);
    bool stop = nas.stop || orbit.stop;
    pthread_mutex_unlock(&orbit.mutex);
    return stop;
}

/* Called without orbit.mutex. Returns 0 and a NUL-terminated body, or -1 and a message. */
static int fetch(const char *url, Body *body, char *error, size_t cap) {
    CURL *c = curl_easy_init();
    if (!c) {
        copy_text(error, cap, "Could not initialize HTTPS.");
        return -1;
    }
    struct curl_blob ca = {(void *)orbit_ca, sizeof orbit_ca - 1, CURL_BLOB_COPY};
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &ca);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
#ifdef ORBIT_TEST
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
#endif
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c, CURLOPT_PROXY, "");
    curl_easy_setopt(c, CURLOPT_OPENSOCKETFUNCTION, provider_open_socket);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "OrbitStore/" ORBIT_VERSION);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, body);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress);
    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK)
        snprintf(error, cap, "Could not reach your NAS index (%s).", curl_easy_strerror(rc));
    else if (status != 200)
        snprintf(error, cap, "Your NAS answered the index request with HTTP %ld.", status);
    else if (!body->data)
        copy_text(error, cap, "Your NAS returned an empty index.");
    else
        return 0;
    return -1;
}

/* game-patches.json, the patch versions and Sony links saved on the person's NAS page, read from
 * the folder of index.json. Returns 1 with a NUL-terminated body (free it), 0 when My NAS is off or
 * has no such file, -1 when it could not be read. Called without orbit.mutex. */
/* The newest plain .pkg listed in the NAS index as an update (packages/ps5/updates/) for this game,
 * when its version is strictly newer than `newer_than` (a NN.NNN.NNN version, or empty), or equal to
 * `same_ok` (the version already on offer) and listed with a SHA-256. Versions
 * the index gives in another shape are not compared. Returns false when there is none. */
bool nas_best_update(const char *title_id, const char *newer_than, const char *same_ok, char *id, size_t id_cap,
                     char *version, size_t version_cap, int64_t *size) {
    bool found = false;
    char best[16] = "";
    pthread_mutex_lock(&orbit.mutex);
    if (active_locked() && orbit.nas_catalog) {
        for (size_t i = 0; i < orbit.nas_count; i++) {
            const Release *r = &orbit.nas_releases[i];
            const cJSON *view = cJSON_GetArrayItem(orbit.nas_catalog, (int)i);
            if (!view || strcasecmp(r->title_id, title_id) || strcmp(json_text(view, "tagline"), "UPDATES"))
                continue;
            const char *v = json_text(view, "version");
            size_t name = strlen(r->filename);
            bool ps4 = !strncmp(r->title_id, "CUSA", 4);
            if (!(ps4 ? patchfeed_app_version_valid(v) : patchfeed_version_valid(v)) || r->compression[0] || r->size <= 0 || name < 5 ||
                strcasecmp(r->filename + name - 4, ".pkg"))
                continue;
            /* The same version Sony offers is taken from the NAS too when the index gives its checksum,
             * so the file is verified like a Sony download. */
            bool same = *same_ok && !strcmp(v, same_ok) && strlen(r->sha256) == 64;
            if (*newer_than && strcmp(v, newer_than) <= 0 && !same)
                continue;
            if (found && strcmp(v, best) <= 0)
                continue;
            found = true;
            copy_text(best, sizeof best, v);
            copy_text(id, id_cap, r->id);
            *size = r->size;
        }
    }
    pthread_mutex_unlock(&orbit.mutex);
    if (found)
        copy_text(version, version_cap, best);
    return found;
}

/* The plain .pkg files the NAS index lists as DLC (packages/ps5/dlc/) for this game. */
size_t nas_dlc_list(const char *title_id, Release *out, size_t max) {
    size_t found = 0;
    pthread_mutex_lock(&orbit.mutex);
    if (active_locked() && orbit.nas_catalog) {
        for (size_t i = 0; i < orbit.nas_count && found < max; i++) {
            const Release *r = &orbit.nas_releases[i];
            const cJSON *view = cJSON_GetArrayItem(orbit.nas_catalog, (int)i);
            size_t name = strlen(r->filename);
            if (!view || strcasecmp(r->title_id, title_id) || strcmp(json_text(view, "tagline"), "DLC") ||
                r->compression[0] || r->size <= 0 || name < 5 || strcasecmp(r->filename + name - 4, ".pkg"))
                continue;
            out[found++] = *r;
        }
    }
    pthread_mutex_unlock(&orbit.mutex);
    return found;
}

/* A copy of the NAS release with this id (its address and checksum), for downloading it. */
bool nas_release_copy(const char *id, Release *out) {
    bool found = false;
    pthread_mutex_lock(&orbit.mutex);
    for (size_t i = 0; i < orbit.nas_count && !found; i++) {
        if (!strcmp(orbit.nas_releases[i].id, id)) {
            *out = orbit.nas_releases[i];
            found = true;
        }
    }
    pthread_mutex_unlock(&orbit.mutex);
    return found;
}

int nas_game_patches(char **data, size_t *size) {
    *data = NULL;
    *size = 0;
    char url[ORBIT_NAS_URL_MAX + 32];
    pthread_mutex_lock(&orbit.mutex);
    bool on = active_locked();
    copy_text(url, sizeof url, orbit.sources.nas_url);
    pthread_mutex_unlock(&orbit.mutex);
    if (!on) return 0;
    char *slash = strrchr(url, '/');
    if (!slash || strlen(url) + sizeof "game-patches.json" >= sizeof url) return -1;
    strcpy(slash + 1, "game-patches.json");
    if (!nas_url_allowed(url)) return -1;
    Body body = {0};
    char error[192] = "";
    if (fetch(url, &body, error, sizeof error)) {
        free(body.data);
        return strstr(error, "HTTP 404") ? 0 : -1;
    }
    *data = body.data;
    *size = body.size;
    return 1;
}

static void *run(void *unused) {
    (void)unused;
    pthread_mutex_lock(&orbit.mutex);
    while (!nas.stop && !orbit.stop) {
        if (!active_locked()) {
            clear_locked();
            nas.pending = false;
            nas.error[0] = 0;
            pthread_cond_wait(&nas.wake, &orbit.mutex);
            continue;
        }
        time_t now = time(NULL);
        if (!nas.pending && now < nas.next) {
            struct timespec until = {.tv_sec = nas.next, .tv_nsec = 0};
            pthread_cond_timedwait(&nas.wake, &orbit.mutex, &until);
            continue;
        }
        char url[ORBIT_NAS_URL_MAX];
        copy_text(url, sizeof url, orbit.sources.nas_url);
        nas.pending = false;
        nas.busy = true;
        pthread_mutex_unlock(&orbit.mutex);
        Body body = {0};
        char error[192] = "";
        int rc = fetch(url, &body, error, sizeof error);
        cJSON *doc = rc ? NULL : cJSON_ParseWithLength(body.data, body.size);
        free(body.data);
        if (!rc && !cJSON_IsObject(doc)) {
            copy_text(error, sizeof error, "The NAS index is not valid JSON.");
            rc = -1;
        }
        pthread_mutex_lock(&orbit.mutex);
        nas.busy = false;
        /* The address or sources may have changed while the request ran. */
        if (!nas.stop && !orbit.stop && active_locked() && !strcmp(url, orbit.sources.nas_url)) {
            nas.checked = time(NULL);
            nas.error[0] = 0;
            if (rc) copy_text(nas.error, sizeof nas.error, error);
            else merge_locked(url, doc);
            nas.next = nas.checked + (rc ? NAS_RETRY_SECONDS : NAS_REFRESH_SECONDS);
        }
        cJSON_Delete(doc);
    }
    pthread_mutex_unlock(&orbit.mutex);
    return NULL;
}

int nas_start(void) {
    pthread_attr_t attr;
    if (pthread_attr_init(&attr)) return -1;
    int rc = pthread_attr_setstacksize(&attr, ORBIT_THREAD_STACK);
    pthread_mutex_lock(&orbit.mutex);
    if (!rc && !nas.started) {
        nas.pending = true;
        rc = pthread_create(&nas.thread, &attr, run, NULL);
        nas.started = !rc;
    }
    pthread_mutex_unlock(&orbit.mutex);
    pthread_attr_destroy(&attr);
    return rc;
}

void nas_stop(void) {
    pthread_mutex_lock(&orbit.mutex);
    bool started = nas.started;
    nas.stop = true;
    pthread_cond_broadcast(&nas.wake);
    pthread_mutex_unlock(&orbit.mutex);
    if (started) pthread_join(nas.thread, NULL);
}
