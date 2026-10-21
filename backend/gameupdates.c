/* Which installed games have a newer patch on Sony's servers.
 *
 * Reads the installed games from the console's app.db, downloads each game's version.xml from
 * Sony, and keeps the answer for the TV app and the browser UI. It only reads: nothing is
 * downloaded or installed here, and a failure of one game never affects the others. */
#define _POSIX_C_SOURCE 200809L
#include "appdb.h"
#include "orbit.h"
#include "patchfeed.h"
#include "patchmanifest.h"
#include "sonynet.h"
#include <ctype.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define APP_DB "/system_data/priv/mms/app.db"
#define MAX_GAMES 512
#define MAX_XML (256 * 1024)
#define FIRST_CHECK_DELAY 45
#define CHECK_INTERVAL (6 * 60 * 60)

typedef struct {
    char title_id[32], name[96], installed[16], available[16]; /* a DLC's title_id is its key, see orbit.h */
    bool dlc;
    char base[16], label[24], game_name[96]; /* for a DLC: the game it belongs to and its folder name */
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
    char seen[1600];       /* every PS5 game checked: installed version and the newest one Sony lists */
    char no_uri[200];      /* PS5 games with no update list address in app.db or param.json */
    char dlc_skipped[200]; /* DLC files on the NAS whose name on the console could not be worked out */
    size_t dlc_count;
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
/* The version the console reports for one installed game right now ("" when it cannot tell). */
bool gameupdates_current_version(const char *title_id, char *out, size_t cap) {
    out[0] = 0;
    char path[600], error[160];
    snprintf(path, sizeof path, "%s%s", orbit.system_root, APP_DB);
    appdb_game *games = calloc(MAX_GAMES, sizeof *games);
    if (!games)
        return false;
    size_t total = 0;
    bool found = false;
    if (appdb_read(path, games, MAX_GAMES, &total, error, sizeof error) >= 0) {
        for (size_t i = 0; i < total; i++) {
            if (strcmp(games[i].title_id, title_id))
                continue;
            installed_version(title_id, games[i].content_version, out, cap);
            found = out[0] != 0;
            break;
        }
    }
    free(games);
    return found;
}
bool gameupdates_check_ok(void) {
    pthread_mutex_lock(&gu.mutex);
    bool ok = !gu.error[0] && gu.failed == 0;
    pthread_mutex_unlock(&gu.mutex);
    return ok;
}
/* What the console keeps for an installed game in /user/appmeta/<id>/param.json: it is written
 * at once after an install, while app.db can lag behind. Fills what app.db did not give. */
static bool read_param(const char *title_id, appdb_game *g) {
    char path[1100];
    snprintf(path, sizeof path, "%s/user/appmeta/%s/param.json", orbit.system_root, title_id);
    unsigned char *data = NULL;
    size_t length = 0;
    if (read_regular_file(path, 256 * 1024, &data, &length))
        return false;
    cJSON *json = cJSON_ParseWithLength((const char *)data, length);
    free(data);
    if (!json)
        return false;
    if (!g->title_id[0])
        copy_text(g->title_id, sizeof g->title_id, title_id);
    if (!g->version_file_uri[0]) {
        const char *uri = json_text(json, "versionFileUri");
        if (uri[0] && strlen(uri) < sizeof g->version_file_uri) {
            copy_text(g->version_file_uri, sizeof g->version_file_uri, uri);
            /* The console pads this value with spaces. */
            size_t end = strlen(g->version_file_uri);
            while (end && g->version_file_uri[end - 1] == ' ')
                g->version_file_uri[--end] = 0;
        }
    }
    if (!g->content_version[0]) {
        const char *version = json_text(json, "contentVersion");
        if (version[0] && strlen(version) < sizeof g->content_version)
            copy_text(g->content_version, sizeof g->content_version, version);
    }
    if (!g->title_name[0]) {
        const cJSON *all = cJSON_GetObjectItemCaseSensitive(json, "localizedParameters");
        const char *language = json_text(all, "defaultLanguage");
        const cJSON *one = language[0] ? cJSON_GetObjectItemCaseSensitive(all, language) : NULL;
        const char *name = one ? json_text(one, "titleName") : "";
        if (name[0])
            copy_text(g->title_name, sizeof g->title_name, name);
    }
    cJSON_Delete(json);
    return true;
}
/* Games installed since app.db was last written are missing from it: add them from appmeta. */
static size_t add_unlisted_games(appdb_game *games, size_t total, size_t cap) {
    char path[600];
    snprintf(path, sizeof path, "%s/user/appmeta", orbit.system_root);
    DIR *dir = opendir(path);
    if (!dir)
        return total;
    struct dirent *entry;
    while ((entry = readdir(dir)) && total < cap) {
        const char *name = entry->d_name;
        if (strlen(name) != 9 || (strncmp(name, "PPSA", 4) && strncmp(name, "CUSA", 4)))
            continue;
        bool known = false;
        for (size_t i = 0; i < total && !known; i++)
            known = !strcmp(games[i].title_id, name);
        if (known)
            continue;
        appdb_game g;
        memset(&g, 0, sizeof g);
        if (read_param(name, &g) && g.content_version[0])
            games[total++] = g;
    }
    closedir(dir);
    return total;
}
/* ---- DLC ---- */
bool gameupdates_split_dlc(const char *key, char *base, size_t base_cap, char *label, size_t label_cap) {
    const char *slash = strchr(key, '/');
    if (!slash || slash == key || !slash[1])
        return false;
    size_t n = (size_t)(slash - key);
    if (n >= base_cap || strlen(slash + 1) >= label_cap)
        return false;
    memcpy(base, key, n);
    base[n] = 0;
    copy_text(label, label_cap, slash + 1);
    return true;
}
/* The console keeps each installed DLC in its own folder: /user/addcont/<game id>/<label>/. */
bool gameupdates_dlc_installed(const char *title_id, const char *label) {
    char path[1200];
    snprintf(path, sizeof path, "%s/user/addcont/%s/%s", orbit.system_root, title_id, label);
    struct stat st;
    return !stat(path, &st) && S_ISDIR(st.st_mode);
}
/* The label of a DLC package is the end of its content id (EP0001-PPSA20397_00-LIMITEDPACK00000), which the
 * package carries in its header: straight at 0x40 for a PS4 package, inside the embedded header of a PS5 one. */
static uint32_t be32(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint64_t le64(const unsigned char *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
        v = v << 8 | p[i];
    return v;
}
static bool label_from_content_id(const char *content_id, const char *title_id, char *label, size_t cap) {
    char id[40];
    memcpy(id, content_id, 36);
    id[36] = 0;
    const char *dash = strrchr(id, '-');
    if (!dash || !strstr(id, title_id) || strlen(dash + 1) < 1 || strlen(dash + 1) >= cap)
        return false;
    for (const char *c = dash + 1; *c; c++)
        if (!isalnum((unsigned char)*c) && *c != '_')
            return false;
    copy_text(label, cap, dash + 1);
    return true;
}
/* What the package says about a DLC, read with a few small range requests: its label, its name and its icon. */
typedef struct {
    char label[24], name[96];
    unsigned char *icon; /* malloc'd PNG, or NULL */
    size_t icon_size;
} PkgMeta;
#define PKG_ICON_MAX (1024 * 1024)
#define ICON_BUDGET (16 * 1024 * 1024)

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}
/* TITLE from a PS4 param.sfo. */
static bool sfo_title(const unsigned char *d, size_t n, char *out, size_t cap) {
    if (n < 20 || memcmp(d, "\0PSF", 4))
        return false;
    uint32_t keys = le32(d + 8), data = le32(d + 12), count = le32(d + 16);
    for (uint32_t i = 0; i < count && 20 + (size_t)(i + 1) * 16 <= n; i++) {
        const unsigned char *e = d + 20 + (size_t)i * 16;
        uint32_t key = keys + (uint32_t)(e[0] | e[1] << 8), at = data + le32(e + 12), len = le32(e + 4);
        if (key >= n || at >= n || n - key < 6 || memcmp(d + key, "TITLE", 6))
            continue;
        if (len > n - at)
            len = (uint32_t)(n - at);
        char text[128];
        snprintf(text, sizeof text, "%.*s", (int)(len < sizeof text - 1 ? len : sizeof text - 1), (const char *)d + at);
        copy_text(out, cap, text);
        return out[0] != 0;
    }
    return false;
}
/* The title in a PS5 param.json. */
static bool json_title(const unsigned char *d, size_t n, char *out, size_t cap) {
    cJSON *json = cJSON_ParseWithLength((const char *)d, n);
    const cJSON *all = json ? cJSON_GetObjectItemCaseSensitive(json, "localizedParameters") : NULL;
    const char *language = json_text(all, "defaultLanguage");
    const cJSON *one = language[0] ? cJSON_GetObjectItemCaseSensitive(all, language) : NULL;
    const char *name = one ? json_text(one, "titleName") : "";
    if (!name[0] && all) {
        const cJSON *each;
        cJSON_ArrayForEach(each, all) {
            if (cJSON_IsObject(each) && json_text(each, "titleName")[0]) {
                name = json_text(each, "titleName");
                break;
            }
        }
    }
    copy_text(out, cap, name);
    cJSON_Delete(json);
    return out[0] != 0;
}
/* Bytes [start, start+len) of the NAS file, into buf. */
static bool pkg_read(const Release *r, int fd, int64_t start, size_t len, unsigned char *buf, char *why,
                     size_t why_cap) {
    if (start < 0 || !len || start + (int64_t)len > r->size) {
        snprintf(why, why_cap, "outside the file");
        return false;
    }
    _Atomic int64_t at = start;
    if (nas_range(r->url, start, start + (int64_t)len, fd, -start, &at, stopping, why, why_cap) != SONY_RANGE_OK)
        return false;
    return pread(fd, buf, len, 0) == (ssize_t)len;
}
static bool pkg_meta(const Release *r, const char *title_id, PkgMeta *meta, char *why, size_t why_cap) {
    memset(meta, 0, sizeof *meta);
    FILE *tmp = tmpfile();
    if (!tmp) {
        snprintf(why, why_cap, "no temporary file");
        return false;
    }
    int fd = fileno(tmp);
    bool ok = false;
    unsigned char head[0x100], cnt[0x100];
    int64_t base = 0;
    if (!pkg_read(r, fd, 0, sizeof head, head, why, why_cap))
        goto done;
    if (be32(head) == 0x7F434E54) { /* PS4: the header is the file's own */
        memcpy(cnt, head, sizeof cnt);
    } else if (be32(head) == 0x7F464948) { /* PS5: the header sits where the first one says */
        uint64_t inner = le64(head + 0x58);
        if (!inner || inner > (uint64_t)r->size || !pkg_read(r, fd, (int64_t)inner, sizeof cnt, cnt, why, why_cap) ||
            be32(cnt) != 0x7F434E54) {
            snprintf(why, why_cap, "no embedded header");
            goto done;
        }
        base = (int64_t)inner;
    } else {
        snprintf(why, why_cap, "not a package");
        goto done;
    }
    if (!label_from_content_id((const char *)cnt + 0x40, title_id, meta->label, sizeof meta->label)) {
        snprintf(why, why_cap, "no usable content id");
        goto done;
    }
    ok = true; /* the label is what matters; the name and the icon are a bonus */
    uint32_t count = be32(cnt + 0x10), table = be32(cnt + 0x18);
    if (count == 0 || count > 64 || table == 0)
        goto done;
    unsigned char entries[64 * 32];
    if (!pkg_read(r, fd, base + table, (size_t)count * 32, entries, why, why_cap))
        goto done;
    why[0] = 0;
    for (uint32_t i = 0; i < count; i++) {
        const unsigned char *e = entries + (size_t)i * 32;
        uint32_t ident = be32(e), flags = be32(e + 8), off = be32(e + 16), size = be32(e + 20);
        if ((flags & 0x80000000u) || !size)
            continue; /* encrypted */
        if ((ident == 0x2000 || ident == 0x1000) && size <= 64 * 1024 && !meta->name[0]) {
            unsigned char *text = malloc(size);
            if (text && pkg_read(r, fd, base + off, size, text, why, why_cap)) {
                if (ident == 0x2000 && text[0] == '{')
                    json_title(text, size, meta->name, sizeof meta->name);
                else if (ident == 0x1000)
                    sfo_title(text, size, meta->name, sizeof meta->name);
            }
            free(text);
        } else if (ident == 0x1200 && size <= PKG_ICON_MAX && !meta->icon) {
            unsigned char *png = malloc(size);
            if (png && pkg_read(r, fd, base + off, size, png, why, why_cap) && size > 8 &&
                !memcmp(png, "\x89PNG\r\n\x1a\n", 8)) {
                meta->icon = png;
                meta->icon_size = size;
            } else {
                free(png);
            }
        }
    }
    why[0] = 0;
done:
    fclose(tmp);
    if (!ok && !why[0])
        snprintf(why, why_cap, "no content id in the header");
    return ok;
}
/* Same label from a name like EP0001-PPSA20397_00-LIMITEDPACK00000.pkg, when the header could not be read. */
static bool label_from_name(const char *filename, char *label, size_t cap) {
    const char *at = strstr(filename, "_00-");
    if (!at || cap < 17)
        return false;
    at += 4;
    size_t n = 0;
    while (n < 16 && (isalnum((unsigned char)at[n]) || at[n] == '_'))
        n++;
    if (n != 16)
        return false;
    memcpy(label, at, 16);
    label[16] = 0;
    return true;
}
static struct {
    char id[24], base[16], label[24], name[96];
    unsigned char *icon;
    size_t icon_size;
} dlc_meta[256];
static size_t dlc_meta_count, dlc_icon_bytes;
static pthread_mutex_t dlc_meta_mutex = PTHREAD_MUTEX_INITIALIZER;

/* The icon the package of this DLC carries, as a copy the caller frees; false when there is none. */
bool gameupdates_dlc_icon(const char *title_id, const char *label, unsigned char **data, size_t *size) {
    bool found = false;
    pthread_mutex_lock(&dlc_meta_mutex);
    for (size_t i = 0; i < dlc_meta_count && !found; i++) {
        if (!dlc_meta[i].icon || strcmp(dlc_meta[i].base, title_id) || strcmp(dlc_meta[i].label, label))
            continue;
        *data = malloc(dlc_meta[i].icon_size);
        if (*data) {
            memcpy(*data, dlc_meta[i].icon, dlc_meta[i].icon_size);
            *size = dlc_meta[i].icon_size;
            found = true;
        }
    }
    pthread_mutex_unlock(&dlc_meta_mutex);
    return found;
}
static bool dlc_has_icon(const char *base, const char *label) {
    bool has = false;
    pthread_mutex_lock(&dlc_meta_mutex);
    for (size_t i = 0; i < dlc_meta_count && !has; i++)
        has = dlc_meta[i].icon && !strcmp(dlc_meta[i].base, base) && !strcmp(dlc_meta[i].label, label);
    pthread_mutex_unlock(&dlc_meta_mutex);
    return has;
}
static void dlc_meta_free(void) {
    pthread_mutex_lock(&dlc_meta_mutex);
    for (size_t i = 0; i < dlc_meta_count; i++)
        free(dlc_meta[i].icon);
    memset(dlc_meta, 0, sizeof dlc_meta);
    dlc_meta_count = dlc_icon_bytes = 0;
    pthread_mutex_unlock(&dlc_meta_mutex);
}

/* Adds an entry for each DLC the NAS lists for an installed game and the console does not have yet. */
static void add_dlc(const appdb_game *games, size_t total, Entry *found, size_t *count, char *skipped,
                    size_t skipped_cap) {
    Release *list = calloc(16, sizeof *list);
    if (!list)
        return;
    for (size_t i = 0; i < total && !stopping(); i++) {
        const char *id = games[i].title_id;
        if (strncmp(id, "PPSA", 4) && strncmp(id, "CUSA", 4))
            continue;
        size_t n = nas_dlc_list(id, list, 16);
        for (size_t k = 0; k < n && *count < MAX_GAMES; k++) {
            const Release *r = &list[k];
            char label[24] = "", name[96] = "", why[120] = "";
            bool cached = false;
            pthread_mutex_lock(&dlc_meta_mutex);
            for (size_t c = 0; c < dlc_meta_count && !cached; c++)
                if (!strcmp(dlc_meta[c].id, r->id) && !strcmp(dlc_meta[c].base, id)) {
                    copy_text(label, sizeof label, dlc_meta[c].label);
                    copy_text(name, sizeof name, dlc_meta[c].name);
                    cached = true;
                }
            pthread_mutex_unlock(&dlc_meta_mutex);
            if (!cached) {
                PkgMeta meta;
                if (pkg_meta(r, id, &meta, why, sizeof why)) {
                    copy_text(label, sizeof label, meta.label);
                    copy_text(name, sizeof name, meta.name);
                } else if (!label_from_name(r->filename, label, sizeof label)) {
                    size_t used = strlen(skipped);
                    snprintf(skipped + used, skipped_cap - used, "%s%s (%s)", used ? "; " : "", r->filename, why);
                    continue;
                }
                pthread_mutex_lock(&dlc_meta_mutex);
                if (dlc_meta_count < sizeof dlc_meta / sizeof dlc_meta[0]) {
                    size_t at = dlc_meta_count++;
                    copy_text(dlc_meta[at].id, sizeof dlc_meta[at].id, r->id);
                    copy_text(dlc_meta[at].base, sizeof dlc_meta[at].base, id);
                    copy_text(dlc_meta[at].label, sizeof dlc_meta[at].label, label);
                    copy_text(dlc_meta[at].name, sizeof dlc_meta[at].name, name);
                    if (meta.icon && dlc_icon_bytes + meta.icon_size <= ICON_BUDGET) {
                        dlc_meta[at].icon = meta.icon;
                        dlc_meta[at].icon_size = meta.icon_size;
                        dlc_icon_bytes += meta.icon_size;
                        meta.icon = NULL;
                    }
                }
                pthread_mutex_unlock(&dlc_meta_mutex);
                free(meta.icon);
            }
            if (gameupdates_dlc_installed(id, label))
                continue;
            bool twice = false;
            for (size_t e = 0; e < *count && !twice; e++)
                twice = found[e].dlc && !strcmp(found[e].base, id) && !strcmp(found[e].label, label);
            if (twice)
                continue;
            Entry *e = &found[(*count)++];
            snprintf(e->title_id, sizeof e->title_id, "%s/%s", id, label);
            e->dlc = true;
            copy_text(e->base, sizeof e->base, id);
            copy_text(e->label, sizeof e->label, label);
            copy_text(e->game_name, sizeof e->game_name, games[i].title_name);
            /* The DLC's own name from its package; else the index's title (the game's, unless titles.json says better), else the label. */
            copy_text(e->name, sizeof e->name, name[0] ? name : strcmp(r->title, id) ? r->title : label);
            snprintf(e->manifest, sizeof e->manifest, "nas:%s", r->id);
            e->size = r->size;
        }
    }
    free(list);
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
    char no_uri[200] = "";
    char seen[1600] = "";
    char first_failure[200] = "";
    if (appdb_read(path, games, MAX_GAMES, &total, error, sizeof error) < 0) {
        char text[200];
        snprintf(text, sizeof text, "Could not read the installed games: %s", error);
        set_error(text);
        free(games);
        free(found);
        return;
    }
    total = add_unlisted_games(games, total, MAX_GAMES);
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
        appdb_game *game = &games[i];
        /* PS5 titles: Sony's version.xml is the format patchfeed reads. PS4 titles have no such list
         * here; only an update .pkg on the person's NAS can offer them an update. */
        bool ps4 = !strncmp(game->title_id, "CUSA", 4);
        bool no_list = false;
        if (ps4) {
            ps4_seen++;
            if (!patchfeed_app_version_valid(game->content_version)) {
                size_t used = strlen(ps4_skipped);
                snprintf(ps4_skipped + used, sizeof ps4_skipped - used, "%s%s=%s", used ? " " : "", game->title_id,
                         *game->content_version ? game->content_version : "?");
                continue;
            }
        } else if (strncmp(game->title_id, "PPSA", 4))
            continue;
        else if (!*game->version_file_uri) {
            /* app.db has no update list address for it (a disc install, say): param.json may. */
            appdb_game fill = *game;
            read_param(game->title_id, &fill);
            if (!*fill.version_file_uri) {
                /* No Sony list for it (a disc game, say). A version and link saved on the NAS page, or an
                 * update .pkg on the NAS, can still offer it an update: only the Sony list is skipped. */
                no_list = true;
            } else
                copy_text(game->version_file_uri, sizeof game->version_file_uri, fill.version_file_uri);
        }
        char installed[16];
        installed_version(game->title_id, game->content_version, installed, sizeof installed);
        Entry *e = NULL;
        if (!ps4) {
            if (!no_list) {
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
                {
                    char newest[16] = "-";
                    for (int k = 0; k < n; k++)
                        if (strcmp(list[k].version, newest) > 0 || newest[0] == '-')
                            copy_text(newest, sizeof newest, list[k].version);
                    size_t used = strlen(seen);
                    snprintf(seen + used, sizeof seen - used, "%s%s %s>%s(%d)", used ? "; " : "", game->title_id,
                             installed[0] ? installed : "?", newest, n);
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
        if (no_list && !e) {
            size_t used = strlen(no_uri);
            snprintf(no_uri + used, sizeof no_uri - used, "%s%s", used ? " " : "", game->title_id);
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
    size_t updates = count;
    char dlc_skipped[200] = "";
    add_dlc(games, total, found, &count, dlc_skipped, sizeof dlc_skipped);
    free(games);
    if (stopping()) {
        free(found);
        return;
    }
    pthread_mutex_lock(&gu.mutex);
    free(gu.entries);
    gu.entries = found;
    gu.count = count;
    gu.dlc_count = count - updates;
    copy_text(gu.dlc_skipped, sizeof gu.dlc_skipped, dlc_skipped);
    gu.failed = failed;
    gu.ps4_seen = ps4_seen;
    gu.ps4_offered = ps4_offered;
    copy_text(gu.ps4_skipped, sizeof gu.ps4_skipped, ps4_skipped);
    copy_text(gu.no_uri, sizeof gu.no_uri, no_uri);
    copy_text(gu.seen, sizeof gu.seen, seen);
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
        /* A restart keeps the queue; it only runs once a check has shown what is on offer. */
        if (gameupdates_check_ok())
            patchdl_queue_pump();
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
    patchdl_queue_load();
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
    dlc_meta_free();
}
int gameupdates_action(const cJSON *input, char *error, size_t cap) {
    const char *action = json_text(input, "action");
    if (!strcmp(action, "download"))
        return patchdl_start(json_text(input, "titleId"), error, cap);
    if (!strcmp(action, "pause"))
        return patchdl_pause(json_text(input, "titleId"), error, cap);
    if (!strcmp(action, "queue"))
        return patchdl_enqueue(json_text(input, "titleId"), error, cap);
    if (!strcmp(action, "dequeue")) {
        patchdl_dequeue(json_text(input, "titleId"));
        return 202;
    }
    if (!strcmp(action, "clearQueue")) {
        patchdl_queue_clear();
        return 202;
    }
    if (!strcmp(action, "queueAll") || !strcmp(action, "queueAllDlc")) {
        char ids[128][32];
        size_t n = 0;
        const bool want_dlc = !strcmp(action, "queueAllDlc");
        pthread_mutex_lock(&gu.mutex);
        for (size_t i = 0; i < gu.count && n < 128; i++)
            if (!gu.entries[i].blocked && gu.entries[i].dlc == want_dlc)
                copy_text(ids[n++], sizeof ids[0], gu.entries[i].title_id);
        pthread_mutex_unlock(&gu.mutex);
        if (!n) {
            copy_text(error, cap, want_dlc ? "There is no DLC to install." : "There is no update this console can install.");
            return 409;
        }
        int accepted = 0;
        for (size_t i = 0; i < n; i++) {
            char ignored[120];
            int code = patchdl_enqueue(ids[i], ignored, sizeof ignored);
            if (code == 202 || code == 200)
                accepted++;
        }
        if (!accepted) {
            copy_text(error, cap, want_dlc ? "Every DLC is already running or waiting." : "Every update is already running or waiting.");
            return 409;
        }
        return 202;
    }
    if (!strcmp(action, "cancel")) {
        patchdl_cancel(json_text(input, "titleId"));
        return 202;
    }
    if (!strcmp(action, "dismiss")) {
        patchdl_dismiss(json_text(input, "titleId"));
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
        if (e->dlc || e->blocked != (n >= gu.count))
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
    cJSON *dlc = cJSON_CreateArray();
    for (size_t n = 0; dlc && n < gu.count; n++) {
        const Entry *e = &gu.entries[n];
        cJSON *g = e->dlc ? cJSON_CreateObject() : NULL;
        if (!g)
            continue;
        cJSON_AddStringToObject(g, "titleId", e->title_id);
        cJSON_AddStringToObject(g, "baseId", e->base);
        cJSON_AddStringToObject(g, "name", e->name);
        cJSON_AddStringToObject(g, "gameName", e->game_name);
        cJSON_AddStringToObject(g, "label", e->label);
        cJSON_AddBoolToObject(g, "hasIcon", dlc_has_icon(e->base, e->label));
        cJSON_AddStringToObject(g, "state", "available");
        cJSON_AddNumberToObject(g, "size", (double)e->size);
        cJSON_AddItemToArray(dlc, g);
    }
    cJSON_AddNumberToObject(o, "dlcCount", (double)gu.dlc_count);
    cJSON_AddStringToObject(o, "dlcSkipped", gu.dlc_skipped);
    if (dlc)
        cJSON_AddItemToObject(o, "dlc", dlc);
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
    cJSON_AddStringToObject(o, "noUpdateAddress", gu.no_uri);
    cJSON_AddStringToObject(o, "seen", gu.seen);
    pthread_mutex_unlock(&gu.mutex);
    cJSON_AddNumberToObject(o, "count", available);
    cJSON_AddItemToObject(o, "games", games);
    cJSON_AddItemToObject(o, "queue", patchdl_queue_json());
    cJSON *job = patchdl_status(false);
    if (job)
        cJSON_AddItemToObject(o, "job", job);
    cJSON *dlc_job = patchdl_status(true);
    if (dlc_job)
        cJSON_AddItemToObject(o, "dlcJob", dlc_job);
    cJSON *finished = patchdl_history_json();
    if (finished)
        cJSON_AddItemToObject(o, "history", finished);
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
