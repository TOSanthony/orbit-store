/* Files: what is on Orbit's drives, and changes to it (docs/api.md#files).
 * A place is a drive ID and a path from the drive's top. Paths are walked one
 * name at a time without following links and never leave the drive's device.
 * Names starting with a dot (Orbit's own .orbit- files among them) and the
 * console's system folders are hidden and can't be reached. A new folder and
 * a rename happen at once; copying, moving, deleting, unpacking, installing
 * and measuring run one at a time on their own thread. Copies are written in
 * a private staging folder and appear only when complete, and nothing is ever
 * replaced. What a download, a transfer from a computer, the installer,
 * ShadowMount or Orbit itself is using is locked. Saving a file to a phone or
 * computer uses a short-lived link: a browser's download can't send the
 * pairing token. */
#include "files.h"
#include "extract.h"
#include "packages.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#define LIST_LIMIT 2000
#define TREE_LIMIT 200000
#define DEPTH_LIMIT 64
#define PATH_LIMIT 768
#define CHUNK (4U * 1024U * 1024U)
#define MARGIN (16LL * 1024 * 1024)
#define TICKETS 16
#define TICKET_SECONDS 300
#define COPY_STAGING ".orbit-copy-"
#define OPERATION_PREFIX "files-"

typedef struct {
    char id[24], action[12], state[16], name[256];
    Storage from, to;          /* the drives, as they were when it started */
    char path[PATH_LIMIT];     /* the entry, from its drive's top */
    char to_path[PATH_LIMIT];  /* copy and move: the destination folder */
    char origin[32];           /* install: its receipt */
    int64_t processed, total, size;
    double speed, sampled_at;
    int64_t sampled;
    char error[256];
    char password[129];
    bool password_given, final_phase; /* final_phase: a move removing its originals */
    atomic_bool pause, cancel;
} Operation;
typedef struct {
    char id[33], storage[24], path[PATH_LIMIT], name[256];
    dev_t device;
    ino_t inode;
    off_t size;
    struct timespec modified;
    time_t expires;
} Ticket;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static Operation op;
static bool present, running, joinable, stopping;
static atomic_bool stop_flag;
static pthread_t thread;
static Ticket tickets[TICKETS];
static unsigned next_ticket;

/* Reserve before inspecting paths, so a new download cannot start between
 * checking an entry and changing it. Paused jobs keep their path guards. */
static bool storage_claim(void) {
    pthread_mutex_lock(&orbit.mutex);
    bool busy = orbit.files_storage_busy || orbit.library_storage_busy || orbit.receiver_active || orbit.stop;
    for (size_t i = 0; i < orbit.job_count && !busy; i++) busy = job_active(&orbit.jobs[i]);
    if (!busy) orbit.files_storage_busy = true;
    pthread_mutex_unlock(&orbit.mutex);
    return !busy;
}
static void storage_release(void) {
    pthread_mutex_lock(&orbit.mutex);
    orbit.files_storage_busy = false;
    pthread_cond_broadcast(&orbit.changed);
    pthread_mutex_unlock(&orbit.mutex);
}

static bool ended(const char *state) {
    return !strcmp(state, "complete") || !strcmp(state, "failed") || !strcmp(state, "cancelled");
}
static double now_seconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* ---- places ---- */

/* Dot names, Orbit's own files, and the system folders a drive's top keeps. */
static bool hidden(const Storage *s, const char *parent, const char *name) {
    if (name[0] == '.' || !utf8_text(name)) return true;
    if (*parent) return false;
    if (!strcasecmp(name, "$RECYCLE.BIN") || !strcasecmp(name, "System Volume Information")) return true;
    /* Extended storage holds the console's own installed games here. */
    return !strncmp(s->id, "ext", 3) && (!strcmp(name, "user") || !strcmp(name, "ps5"));
}
/* "" is the drive's top; otherwise names joined by "/", none hidden. */
static bool path_ok(const Storage *s, const char *path) {
    size_t n = strlen(path);
    if (!n) return true;
    if (n >= PATH_LIMIT || path[0] == '/' || path[n - 1] == '/' || strstr(path, "//")) return false;
    char parent[PATH_LIMIT] = "", copy[PATH_LIMIT];
    copy_text(copy, sizeof copy, path);
    char *save = NULL;
    for (char *part = strtok_r(copy, "/", &save); part; part = strtok_r(NULL, "/", &save)) {
        if (strlen(part) >= 256 || hidden(s, parent, part)) return false;
        size_t used = strlen(parent);
        snprintf(parent + used, sizeof parent - used, "%s%s", used ? "/" : "", part);
    }
    return true;
}
/* A name Files can create: one an exFAT drive can store, not hidden. */
static bool new_name_ok(const char *name) {
    return *name && name[0] != '.' && utf8_text(name) && extract_name_ok(name);
}
static int drive_find(const char *id, Storage *out) {
    Storage all[ORBIT_MAX_STORAGE];
    size_t n = storage_list(all);
    for (size_t i = 0; i < n; i++)
        if (!strcmp(all[i].id, id)) {
            *out = all[i];
            return 0;
        }
    return -1;
}
static int open_place(const Storage *s, const char *path) {
    Job pin;
    memset(&pin, 0, sizeof pin);
    copy_text(pin.storage_id, sizeof pin.storage_id, s->id);
    copy_text(pin.root, sizeof pin.root, s->root);
    pin.device = s->device;
    pin.inode = s->inode;
    return storage_open_path(&pin, path);
}
static void absolute(const Storage *s, const char *path, char *out, size_t cap) {
    snprintf(out, cap, "%s%s%s", s->root, *path ? "/" : "", path);
}
/* "a/b/c" → "a/b" and "c". */
static void split(const char *path, char *parent, size_t cap, const char **leaf) {
    const char *slash = strrchr(path, '/');
    if (!slash) {
        parent[0] = 0;
        *leaf = path;
        return;
    }
    snprintf(parent, cap, "%.*s", (int)(slash - path), path);
    *leaf = slash + 1;
}
static const char *extension(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot && dot != name ? dot : "";
}
static const char *file_type(const char *name) {
    const char *e = extension(name);
    if (!strcasecmp(e, ".zip") || !strcasecmp(e, ".7z") || !strcasecmp(e, ".rar")) return "archive";
    if (!strcasecmp(e, ".pkg") || !strcasecmp(e, ".fpkg")) return "package";
    /* The images ShadowMount mounts, as the installer's check knows them (packages.c). */
    if (!strcasecmp(e, ".exfat") || !strcasecmp(e, ".ffpkg") || !strcasecmp(e, ".ffpfsc")) return "image";
    return "file";
}
/* A folder ShadowMount can run: it has eboot.bin or sce_sys/param.json. */
static bool game_folder(int dir, const char *name) {
    char probe[300];
    struct stat st;
    snprintf(probe, sizeof probe, "%s/eboot.bin", name);
    if (!fstatat(dir, probe, &st, AT_SYMLINK_NOFOLLOW) && S_ISREG(st.st_mode)) return true;
    snprintf(probe, sizeof probe, "%s/sce_sys/param.json", name);
    return !fstatat(dir, probe, &st, AT_SYMLINK_NOFOLLOW) && S_ISREG(st.st_mode);
}
static bool exists(int dir, const char *name) {
    struct stat st;
    return !fstatat(dir, name, &st, AT_SYMLINK_NOFOLLOW) || errno != ENOENT;
}
/* Reserve a directory name exclusively before moving a completed tree onto it.
 * In particular, renameat alone can replace an existing empty directory. */
static int publish_directory(int from, const char *name, int to, const char *target) {
    if (mkdirat(to, target, 0700)) return -1;
    struct stat reserved = {0}, current;
    int fd = openat(to, target, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    int rc = fd < 0 || fstat(fd, &reserved) ? -1 : 0;
    if (!rc && (fstatat(to, target, &current, AT_SYMLINK_NOFOLLOW) ||
                current.st_dev != reserved.st_dev || current.st_ino != reserved.st_ino)) {
        errno = EEXIST;
        rc = -1;
    }
    if (!rc) rc = renameat(from, name, to, target);
    int saved = errno;
    if (rc && fd >= 0 && !fstatat(to, target, &current, AT_SYMLINK_NOFOLLOW) &&
        current.st_dev == reserved.st_dev && current.st_ino == reserved.st_ino)
        unlinkat(to, target, AT_REMOVEDIR);
    if (fd >= 0) close(fd);
    errno = saved;
    return rc;
}

/* ---- locks: what others are using ---- */

typedef struct {
    char *path;
    const char *reason;
} Guard;
typedef struct {
    Guard *items;
    size_t count, cap;
} Guards;
static void guard(Guards *g, const char *path, const char *reason) {
    if (!*path) return;
    if (g->count == g->cap) {
        size_t cap = g->cap ? g->cap * 2 : 64;
        Guard *next = realloc(g->items, cap * sizeof *next);
        if (!next) return;
        g->items = next;
        g->cap = cap;
    }
    char *copy = strdup(path);
    if (!copy) return;
    g->items[g->count].path = copy;
    g->items[g->count++].reason = reason;
}
static void guards_free(Guards *g) {
    for (size_t i = 0; i < g->count; i++) free(g->items[i].path);
    free(g->items);
}
/* Orbit's own folder, unfinished downloads, games ShadowMount has installed or
 * mounted, and the running operation's source and destination. */
static Guards guards_collect(void) {
    Guards g = {0};
    char path[1200];
    pthread_mutex_lock(&lock);
    if (present && !ended(op.state)) {
        absolute(&op.from, op.path, path, sizeof path);
        guard(&g, path, "busy");
        if (op.to.id[0]) {
            char folder[1000];
            absolute(&op.to, op.to_path, folder, sizeof folder);
            snprintf(path, sizeof path, "%s/%s", folder, op.name);
            guard(&g, path, "busy");
        }
    }
    pthread_mutex_unlock(&lock);
    pthread_mutex_lock(&orbit.mutex);
    guard(&g, orbit.state_dir, "orbit");
    for (size_t i = 0; i < orbit.job_count; i++) {
        const Job *j = &orbit.jobs[i];
        if (!strcmp(j->status, "complete") || !strcmp(j->status, "failed") || !strcmp(j->status, "cancelled")) continue;
        snprintf(path, sizeof path, "%s/homebrew/%s", j->root, j->filename);
        guard(&g, path, "download");
        snprintf(path, sizeof path, "%s/homebrew/%s.part", j->root, j->filename);
        guard(&g, path, "download");
        if (j->output[0]) {
            snprintf(path, sizeof path, "%s/homebrew/%s", j->root, j->output);
            guard(&g, path, "download");
        }
    }
    pthread_mutex_unlock(&orbit.mutex);
    cJSON *library = library_snapshot(false), *game;
    cJSON_ArrayForEach(game, cJSON_GetObjectItemCaseSensitive(library, "games")) {
        if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(game, "installed")) &&
            !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(game, "mounted"))) continue;
        guard(&g, json_text(game, "path"), "game");
    }
    cJSON_Delete(library);
    return g;
}
/* One path inside the other, or the same. */
static bool related(const char *a, const char *b) {
    size_t na = strlen(a), nb = strlen(b);
    if (na == nb) return !strcmp(a, b);
    return na < nb ? !strncmp(a, b, na) && b[na] == '/' : !strncmp(a, b, nb) && a[nb] == '/';
}
static const char *locked(const Guards *g, const char *path) {
    static const char *const order[] = {"busy", "download", "install", "game", "orbit"};
    bool hit[5] = {0};
    for (size_t i = 0; i < g->count; i++)
        if (related(path, g->items[i].path))
            for (int k = 0; k < 5; k++)
                if (!strcmp(order[k], g->items[i].reason)) hit[k] = true;
    if (!hit[0] && !hit[1] && packages_path_busy(path)) hit[2] = true;
    for (int k = 0; k < 5; k++)
        if (hit[k]) return order[k];
    return "";
}
/* The interfaces show these sentences for a locked entry (src/FileActions.tsx). */
static const char *locked_text(const char *reason) {
    return !strcmp(reason, "orbit")      ? "Orbit keeps its own settings and downloads list here."
           : !strcmp(reason, "download") ? "A download or transfer is using this."
           : !strcmp(reason, "game")     ? "ShadowMount is using this game. Unmount or uninstall it first."
           : !strcmp(reason, "install")  ? "The PS5 is installing from this."
                                         : "Another file operation is using this.";
}

/* ---- listing ---- */

cJSON *files_list(const cJSON *input, int *code, char *error, size_t cap) {
    const char *id = json_text(input, "storageId"), *path = json_text(input, "path");
    Storage s;
    if (!*id) {
        *code = 400;
        copy_text(error, cap, "Choose a drive.");
        return NULL;
    }
    if (drive_find(id, &s)) {
        *code = 404;
        copy_text(error, cap, "This drive is not connected.");
        return NULL;
    }
    if (!path_ok(&s, path)) {
        *code = 400;
        copy_text(error, cap, "Choose a folder on this drive.");
        return NULL;
    }
    int dir = open_place(&s, path);
    DIR *d = dir >= 0 ? fdopendir(dir) : NULL;
    if (!d) {
        if (dir >= 0) close(dir);
        *code = 404;
        copy_text(error, cap, "This folder isn't there anymore. Go up a folder and try again.");
        return NULL;
    }
    Guards g = guards_collect();
    char base[1100], full[1400];
    absolute(&s, path, base, sizeof base);
    cJSON *out = cJSON_CreateObject(), *entries = cJSON_CreateArray();
    cJSON_AddStringToObject(out, "storageId", s.id);
    cJSON_AddStringToObject(out, "path", path);
    unsigned count = 0;
    bool truncated = false;
    struct dirent *e;
    while ((e = readdir(d))) {
        const char *name = e->d_name;
        struct stat st;
        if (!strcmp(name, ".") || !strcmp(name, "..") || hidden(&s, path, name) || strlen(name) >= 256 ||
            fstatat(dirfd(d), name, &st, AT_SYMLINK_NOFOLLOW) || (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) ||
            st.st_dev != s.device)
            continue;
        if (++count > LIST_LIMIT) {
            truncated = true;
            continue;
        }
        bool folder = S_ISDIR(st.st_mode);
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", name);
        cJSON_AddBoolToObject(item, "folder", folder);
        cJSON_AddStringToObject(item, "type", folder ? (game_folder(dirfd(d), name) ? "game" : "folder") : file_type(name));
        if (folder) cJSON_AddNullToObject(item, "sizeBytes");
        else cJSON_AddNumberToObject(item, "sizeBytes", (double)st.st_size);
        cJSON_AddNumberToObject(item, "modifiedAt", (double)st.st_mtime);
        snprintf(full, sizeof full, "%s/%s", base, name);
        cJSON_AddStringToObject(item, "locked", locked(&g, full));
        cJSON_AddItemToArray(entries, item);
    }
    closedir(d);
    guards_free(&g);
    cJSON_AddItemToObject(out, "entries", entries);
    cJSON_AddBoolToObject(out, "truncated", truncated);
    return out;
}

/* ---- the operation ---- */

static cJSON *operation_json(void) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", op.id);
    cJSON_AddStringToObject(o, "action", op.action);
    cJSON_AddStringToObject(o, "state", op.state);
    cJSON_AddStringToObject(o, "name", op.name);
    cJSON_AddStringToObject(o, "storageId", op.from.id);
    cJSON_AddStringToObject(o, "path", op.path);
    cJSON_AddStringToObject(o, "toStorageId", op.to.id);
    cJSON_AddStringToObject(o, "toPath", op.to_path);
    cJSON_AddNumberToObject(o, "processedBytes", (double)op.processed);
    cJSON_AddNumberToObject(o, "totalBytes", (double)op.total);
    cJSON_AddNumberToObject(o, "speed", op.state[0] && !strcmp(op.state, "running") ? op.speed : 0);
    cJSON_AddStringToObject(o, "error", op.error);
    if (op.size >= 0) cJSON_AddNumberToObject(o, "sizeBytes", (double)op.size);
    else cJSON_AddNullToObject(o, "sizeBytes");
    return o;
}
cJSON *files_status(void) {
    cJSON *out = cJSON_CreateObject();
    pthread_mutex_lock(&lock);
    cJSON_AddItemToObject(out, "operation", present ? operation_json() : cJSON_CreateNull());
    pthread_mutex_unlock(&lock);
    return out;
}
static void set_total(int64_t total) {
    pthread_mutex_lock(&lock);
    op.total = total;
    pthread_mutex_unlock(&lock);
}
/* Bytes done, and the speed over the last half second or more. */
static void advance(int64_t bytes, bool absolute_value) {
    pthread_mutex_lock(&lock);
    op.processed = absolute_value ? bytes : op.processed + bytes;
    double now = now_seconds();
    if (op.sampled_at <= 0) {
        op.sampled_at = now;
        op.sampled = op.processed;
    } else if (now - op.sampled_at >= 0.5) {
        double rate = (double)(op.processed - op.sampled) / (now - op.sampled_at);
        op.speed = op.speed > 0 ? op.speed * 0.6 + rate * 0.4 : rate;
        op.sampled_at = now;
        op.sampled = op.processed;
    }
    pthread_mutex_unlock(&lock);
}
static bool stopped(void) {
    return atomic_load(&op.cancel) || atomic_load(&stop_flag) || orbit_is_stopping();
}
/* Between chunks and entries: wait while paused; true to stop. The state says
 * paused only once the work has actually stopped. */
static bool checkpoint(void) {
#ifdef ORBIT_TEST
    const char *delay = getenv("ORBIT_TEST_FILES_DELAY_MS");
    if (delay) {
        long ms = strtol(delay, NULL, 10);
        struct timespec wait = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};
        nanosleep(&wait, NULL);
    }
#endif
    pthread_mutex_lock(&lock);
    while (atomic_load(&op.pause) && !atomic_load(&op.cancel) && !stopping) {
        copy_text(op.state, sizeof op.state, "paused");
        op.speed = 0;
        pthread_cond_wait(&wake, &lock);
    }
    if (!strcmp(op.state, "paused")) {
        copy_text(op.state, sizeof op.state, "running");
        op.sampled_at = 0;
    }
    pthread_mutex_unlock(&lock);
    return stopped();
}

/* ---- trees ---- */

typedef struct {
    int64_t bytes;
    unsigned entries;
    uint64_t signature;
} Tally;
/* A tree's size without following links or crossing to another drive. Copies
 * refuse links and special files; deletes and measuring skip them. */
static int tally(int parent, const char *name, dev_t device, unsigned depth, bool strict, Tally *t,
                 char *error, size_t cap) {
    struct stat st;
    if (fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW)) {
        copy_text(error, cap, "Some files couldn't be read. Check the drive and try again.");
        return -1;
    }
    if (++t->entries > TREE_LIMIT || depth > DEPTH_LIMIT) {
        copy_text(error, cap, "This folder has too many files or levels for Orbit to handle at once.");
        return -1;
    }
    /* Metadata fingerprint before/after a copy, independent of directory order.
     * Reads don't change any of these fields. A changed source is never removed. */
    uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) hash = (hash ^ *p) * 1099511628211ULL;
    uint64_t fields[] = {depth, st.st_dev, st.st_ino, st.st_mode, st.st_size,
        st.st_mtim.tv_sec, st.st_mtim.tv_nsec, st.st_ctim.tv_sec, st.st_ctim.tv_nsec};
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) hash = (hash ^ fields[i]) * 1099511628211ULL;
    t->signature ^= hash;
    if (S_ISREG(st.st_mode)) {
        if (st.st_dev != device || st.st_size < 0 || st.st_size > INT64_MAX - t->bytes) {
            copy_text(error, cap, "Some files couldn't be read. Check the drive and try again.");
            return -1;
        }
        t->bytes += st.st_size;
        return 0;
    }
    if (!S_ISDIR(st.st_mode) || st.st_dev != device) {
        if (!strict) return 0;
        copy_text(error, cap, "This contains links or special files Orbit can't copy.");
        return -1;
    }
    int fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    DIR *d = fd >= 0 ? fdopendir(fd) : NULL;
    if (!d) {
        if (fd >= 0) close(fd);
        copy_text(error, cap, "Some files couldn't be read. Check the drive and try again.");
        return -1;
    }
    int rc = 0;
    struct dirent *e;
    while (!rc && (e = readdir(d))) {
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
            rc = tally(dirfd(d), e->d_name, device, depth + 1, strict, t, error, cap);
        if (!rc && stopped()) rc = 1;
    }
    closedir(d);
    return rc;
}
static int write_all(int fd, const char *data, size_t size) {
    while (size) {
        ssize_t n = write(fd, data, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        data += n;
        size -= (size_t)n;
    }
    return 0;
}
static void copy_failed(char *error, size_t cap) {
    copy_text(error, cap, errno == ENOSPC ? "The destination drive is full." : "Some files couldn't be copied. Check the drives and try again.");
}
/* src (in src_parent) to dst (in dst_parent), which must not exist. 0, 1 stopped, -1. */
static int copy_tree(int src_parent, const char *src, int dst_parent, const char *dst, dev_t device, unsigned depth,
                     char *buffer, char *error, size_t cap) {
    if (checkpoint()) return 1;
    struct stat st;
    if (fstatat(src_parent, src, &st, AT_SYMLINK_NOFOLLOW) || depth > DEPTH_LIMIT) {
        copy_failed(error, cap);
        return -1;
    }
    struct timespec times[2] = {st.st_atim, st.st_mtim};
    if (S_ISDIR(st.st_mode)) {
        if (st.st_dev != device || mkdirat(dst_parent, dst, 0755)) {
            copy_failed(error, cap);
            return -1;
        }
        int from = openat(src_parent, src, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        int to = openat(dst_parent, dst, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        DIR *d = from >= 0 ? fdopendir(from) : NULL;
        if (!d || to < 0) {
            if (d) closedir(d);
            else if (from >= 0) close(from);
            if (to >= 0) close(to);
            copy_failed(error, cap);
            return -1;
        }
        int rc = 0;
        struct dirent *e;
        while (!rc) {
            errno = 0;
            e = readdir(d);
            if (!e) { if (errno) { copy_failed(error, cap); rc = -1; } break; }
            if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
                rc = copy_tree(dirfd(d), e->d_name, to, e->d_name, device, depth + 1, buffer, error, cap);
        }
        closedir(d);
        if (!rc && (fsync(to) || futimens(to, times))) {
            copy_failed(error, cap);
            rc = -1;
        }
        close(to);
        return rc;
    }
    if (!S_ISREG(st.st_mode)) {
        copy_text(error, cap, "This contains links or special files Orbit can't copy.");
        return -1;
    }
    int from = openat(src_parent, src, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    struct stat opened;
    if (from < 0 || fstat(from, &opened) || !S_ISREG(opened.st_mode) || opened.st_ino != st.st_ino || opened.st_dev != device) {
        if (from >= 0) close(from);
        copy_failed(error, cap);
        return -1;
    }
    int to = openat(dst_parent, dst, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
    if (to < 0) {
        close(from);
        copy_failed(error, cap);
        return -1;
    }
    int rc = 0;
    while (!rc) {
        if (checkpoint()) {
            rc = 1;
            break;
        }
        ssize_t n = read(from, buffer, CHUNK);
        if (n < 0 && errno == EINTR) continue;
        if (n == 0) break;
        if (n < 0 || write_all(to, buffer, (size_t)n)) {
            copy_failed(error, cap);
            rc = -1;
            break;
        }
        advance(n, false);
    }
    struct stat after;
    if (!rc && (fstat(from, &after) || after.st_size != st.st_size ||
                after.st_mtim.tv_sec != st.st_mtim.tv_sec || after.st_mtim.tv_nsec != st.st_mtim.tv_nsec ||
                after.st_ctim.tv_sec != st.st_ctim.tv_sec || after.st_ctim.tv_nsec != st.st_ctim.tv_nsec)) {
        copy_text(error, cap, "The source file changed while it was being copied. The original has been kept. Try again.");
        rc = -1;
    }
    if (!rc && (fsync(to) || futimens(to, times))) {
        copy_failed(error, cap);
        rc = -1;
    }
    close(from);
    if (close(to) && !rc) {
        copy_failed(error, cap);
        rc = -1;
    }
    return rc;
}
/* Deletes a tree without following links. COUNTED adds the bytes removed to the
 * operation's progress; CANCELLABLE stops between entries when asked. 0, 1 stopped, -1. */
enum { COUNTED = 1, CANCELLABLE = 2 };
static int remove_tree(int parent, const char *name, dev_t device, unsigned depth, int mode,
                       char *error, size_t cap) {
    if ((mode & CANCELLABLE) && checkpoint()) return 1;
    struct stat st;
    if (fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW)) {
        if (errno == ENOENT) return 0;
        copy_text(error, cap, "Some files couldn't be deleted. Check the drive and try again.");
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        if (unlinkat(parent, name, 0)) {
            copy_text(error, cap, "Some files couldn't be deleted. Check the drive and try again.");
            return -1;
        }
        if ((mode & COUNTED) && S_ISREG(st.st_mode)) advance(st.st_size, false);
        return (mode & CANCELLABLE) && checkpoint() ? 1 : 0;
    }
    if (st.st_dev != device || depth > DEPTH_LIMIT) {
        copy_text(error, cap, "Some files couldn't be deleted. Check the drive and try again.");
        return -1;
    }
    int fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) {
        copy_text(error, cap, "Some files couldn't be deleted. Check the drive and try again.");
        return -1;
    }
    int rc = 0;
    /* Some filesystems skip names removed during a scan; rescan until empty. */
    for (int pass = 0; pass < 4 && !rc; pass++) {
        int scan = dup(fd);
        DIR *d = scan >= 0 ? fdopendir(scan) : NULL;
        if (!d) {
            if (scan >= 0) close(scan);
            rc = -1;
            break;
        }
        rewinddir(d);
        struct dirent *e;
        while (!rc && (e = readdir(d)))
            if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
                rc = remove_tree(fd, e->d_name, device, depth + 1, mode, error, cap);
        closedir(d);
        if (rc) break;
        if (!unlinkat(parent, name, AT_REMOVEDIR)) break;
        if (errno != ENOTEMPTY && errno != EEXIST) rc = -1;
        else if (pass == 3) rc = -1;
    }
    close(fd);
    if (rc == -1 && !*error) copy_text(error, cap, "Some files couldn't be deleted. Check the drive and try again.");
    return rc;
}
/* Leftovers from an operation Orbit stopped (a crash or a lost drive): its own
 * hidden staging names in a folder about to get a new one. */
static void sweep(int dir, const char *prefix, dev_t device) {
    int scan = dup(dir);
    DIR *d = scan >= 0 ? fdopendir(scan) : NULL;
    if (!d) {
        if (scan >= 0) close(scan);
        return;
    }
    char names[8][256];
    int found = 0;
    struct dirent *e;
    while ((e = readdir(d)) && found < 8) {
        const char *suffix = e->d_name + (strlen(e->d_name) >= strlen(prefix) ? strlen(prefix) : 0);
        if (!strncmp(e->d_name, prefix, strlen(prefix)) && !strncmp(suffix, OPERATION_PREFIX, strlen(OPERATION_PREFIX)) &&
            strlen(suffix + strlen(OPERATION_PREFIX)) == 16 &&
            strspn(suffix + strlen(OPERATION_PREFIX), "0123456789abcdef") == 16)
            copy_text(names[found++], sizeof names[0], e->d_name);
    }
    closedir(d);
    char ignored[256];
    for (int i = 0; i < found; i++) remove_tree(dir, names[i], device, 0, 0, ignored, sizeof ignored);
}
/* "name", or "name (2).ext" … when it's taken. */
static int free_name(int dir, const char *name, bool folder, char *out, size_t cap) {
    if (!exists(dir, name)) {
        copy_text(out, cap, name);
        return 0;
    }
    const char *dot = folder ? "" : extension(name);
    int stem = (int)(strlen(name) - strlen(dot));
    for (int i = 2; i < 100; i++) {
        if (snprintf(out, cap, "%.*s (%d)%s", stem, name, i, dot) >= (int)cap) return -1;
        if (!exists(dir, out) && extract_name_ok(out)) return 0;
    }
    return -1;
}
/* Room for bytes on the drive, after what unfinished downloads have reserved. */
static bool space_for(int dir, const Storage *s, int64_t bytes) {
    struct statvfs fs;
    if (fstatvfs(dir, &fs)) return false;
    uint64_t free_bytes = (uint64_t)fs.f_bavail * (fs.f_frsize ? fs.f_frsize : fs.f_bsize);
#ifdef ORBIT_TEST
    const char *limited = getenv("ORBIT_TEST_FILES_FREE_BYTES");
    if (limited) free_bytes = strtoull(limited, NULL, 10);
#endif
    pthread_mutex_lock(&orbit.mutex);
    uint64_t reserved = storage_pending_locked(s, NULL);
    pthread_mutex_unlock(&orbit.mutex);
    return bytes >= 0 && reserved <= free_bytes && (uint64_t)bytes + MARGIN <= free_bytes - reserved;
}

/* ---- each action, on the operation's thread ---- */

static int run_copy(bool move, char *error, size_t cap) {
    char parent[PATH_LIMIT];
    const char *leaf;
    split(op.path, parent, sizeof parent, &leaf);
    int from = open_place(&op.from, parent), to = open_place(&op.to, op.to_path);
    int rc = -1;
    char *buffer = NULL;
    if (from < 0 || to < 0) {
        copy_text(error, cap, "A drive or folder isn't there anymore. Check the drives and try again.");
        goto done;
    }
    struct stat st;
    if (fstatat(from, leaf, &st, AT_SYMLINK_NOFOLLOW)) {
        copy_text(error, cap, "This isn't there anymore. Open the folder again and retry.");
        goto done;
    }
    bool folder = S_ISDIR(st.st_mode);
    if (checkpoint()) { rc = 1; goto done; }
    if (move && !strcmp(op.from.id, op.to.id)) {
        /* The same drive: one rename, never over an existing name. */
        if (exists(to, leaf)) {
            copy_text(error, cap, "A file or folder with that name already exists here.");
            goto done;
        }
        Tally t = {0};
        char ignored[256];
        if (!tally(from, leaf, op.from.device, 0, false, &t, ignored, sizeof ignored)) set_total(t.bytes);
        rc = folder ? publish_directory(from, leaf, to, leaf) : extract_publish_file(from, leaf, to, leaf);
        if (rc || fsync(to)) {
            copy_text(error, cap, "Couldn't move it. Check the drive and try again.");
            rc = -1;
            goto done;
        }
        advance(t.bytes, true);
        goto done;
    }
    Tally t = {0};
    rc = tally(from, leaf, op.from.device, 0, true, &t, error, cap);
    if (rc) goto done;
    set_total(t.bytes);
    rc = -1;
    if (!space_for(to, &op.to, t.bytes)) {
        copy_text(error, cap, "There isn't enough free space on the destination drive.");
        goto done;
    }
    char name[256];
    if (move ? exists(to, leaf) : free_name(to, leaf, folder, name, sizeof name)) {
        copy_text(error, cap, "A file or folder with that name already exists here.");
        goto done;
    }
    if (move) copy_text(name, sizeof name, leaf);
    pthread_mutex_lock(&lock);
    copy_text(op.name, sizeof op.name, name);
    pthread_mutex_unlock(&lock);
    sweep(to, COPY_STAGING, op.to.device);
    char staging[64];
    snprintf(staging, sizeof staging, COPY_STAGING "%s", op.id);
    int stage = mkdirat(to, staging, 0700) ? -1 : openat(to, staging, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    buffer = malloc(CHUNK);
    if (stage < 0 || !buffer) {
        if (stage >= 0) close(stage);
        copy_failed(error, cap);
        goto done;
    }
    rc = copy_tree(from, leaf, stage, name, op.from.device, 0, buffer, error, cap);
    if (!rc && checkpoint()) rc = 1;
    if (!rc) {
        Tally after = {0};
        if (tally(from, leaf, op.from.device, 0, true, &after, error, cap) ||
            t.signature != after.signature || t.entries != after.entries || t.bytes != after.bytes) {
            copy_text(error, cap, "The source changed while it was being copied. The originals have been kept. Try again.");
            rc = -1;
        }
    }
    /* Only a complete copy appears; nothing is replaced. */
    if (!rc && exists(to, name)) {
        copy_text(error, cap, "A file or folder with that name already exists here.");
        rc = -1;
    }
    if (!rc && (folder ? publish_directory(stage, name, to, name) : extract_publish_file(stage, name, to, name))) {
        copy_failed(error, cap);
        rc = -1;
    }
    close(stage);
    char ignored[256];
    remove_tree(to, staging, op.to.device, 0, 0, ignored, sizeof ignored);
    if (!rc && fsync(to)) {
        copy_failed(error, cap);
        rc = -1;
    }
    if (!rc && move) {
        /* The copy is in place; removing the originals can't be cancelled. */
        pthread_mutex_lock(&lock);
        op.final_phase = true;
        atomic_store(&op.pause, false);
        pthread_mutex_unlock(&lock);
        int64_t copied = t.bytes;
        rc = remove_tree(from, leaf, op.from.device, 0, 0, error, cap);
        advance(copied, true);
        if (rc) copy_text(error, cap, "The copy is in place, but some originals couldn't be deleted. Delete them in Files.");
    }
done:
    free(buffer);
    if (from >= 0) close(from);
    if (to >= 0) close(to);
    return rc;
}
static int run_delete(char *error, size_t cap) {
    char parent[PATH_LIMIT];
    const char *leaf;
    split(op.path, parent, sizeof parent, &leaf);
    int dir = open_place(&op.from, parent);
    if (dir < 0) {
        copy_text(error, cap, "This drive or folder isn't there anymore.");
        return -1;
    }
    Tally t = {0};
    char ignored[256];
    if (!tally(dir, leaf, op.from.device, 0, false, &t, ignored, sizeof ignored)) set_total(t.bytes);
    int rc = remove_tree(dir, leaf, op.from.device, 0, COUNTED | CANCELLABLE, error, cap);
    if (!rc) fsync(dir);
    close(dir);
    return rc;
}
static int run_measure(char *error, size_t cap) {
    char parent[PATH_LIMIT];
    const char *leaf;
    split(op.path, parent, sizeof parent, &leaf);
    int dir = open_place(&op.from, parent);
    if (dir < 0) {
        copy_text(error, cap, "This drive or folder isn't there anymore.");
        return -1;
    }
    Tally t = {0};
    int rc = tally(dir, leaf, op.from.device, 0, false, &t, error, cap);
    close(dir);
    if (!rc) {
        pthread_mutex_lock(&lock);
        op.size = op.total = op.processed = t.bytes;
        pthread_mutex_unlock(&lock);
    }
    return rc;
}
static void unpacked(void *context, int64_t written) {
    (void)context;
    advance(written, true);
}
/* Waits for a password; false when stopped instead. */
static bool password_wait(const char *message) {
    pthread_mutex_lock(&lock);
    copy_text(op.state, sizeof op.state, "password");
    copy_text(op.error, sizeof op.error, message);
    op.password_given = false;
    op.speed = 0;
    while (!op.password_given && !atomic_load(&op.cancel) && !stopping) pthread_cond_wait(&wake, &lock);
    bool given = op.password_given;
    if (given) {
        copy_text(op.state, sizeof op.state, "running");
        op.error[0] = 0;
    }
    pthread_mutex_unlock(&lock);
    return given;
}
static int run_unpack(char *error, size_t cap) {
    char parent[PATH_LIMIT], path[1100], folder[256];
    const char *leaf;
    split(op.path, parent, sizeof parent, &leaf);
    absolute(&op.from, parent, path, sizeof path);
    const char *e = extension(leaf);
    const char *compression = !strcasecmp(e, ".zip") ? "zip" : !strcasecmp(e, ".7z") ? "7z" : "rar";
    /* Several top-level entries go into one folder named after the archive. */
    snprintf(folder, sizeof folder, "%.*s", (int)(strlen(leaf) - strlen(e)), leaf);
    int dir = open_place(&op.from, parent);
    if (dir < 0) {
        copy_text(error, cap, "This drive or folder isn't there anymore.");
        return -1;
    }
    sweep(dir, ".orbit-extract-" OPERATION_PREFIX, op.from.device);
    int rc;
    for (;;) {
        pthread_mutex_lock(&lock);
        char password[129];
        copy_text(password, sizeof password, op.password_given ? op.password : "");
        pthread_mutex_unlock(&lock);
        Extraction x = {.dir = dir, .path = path, .archive = leaf, .job_id = op.id, .compression = compression,
                        .password = *password ? password : NULL, .folder = folder, .expected = 0,
                        .cancel = &op.cancel, .stop = &stop_flag, .progress = unpacked};
        /* One extraction at a time on the console: wait for a download's. */
        while (pthread_mutex_trylock(&extraction_mutex)) {
            if (stopped()) {
                close(dir);
                return 1;
            }
            struct timespec wait = {.tv_sec = 0, .tv_nsec = 100000000};
            nanosleep(&wait, NULL);
        }
        int64_t bytes = 0;
        char format[12];
        error[0] = 0;
        rc = inspect_archive(&x, &bytes, format, error, cap);
        if (!rc) {
            set_total(bytes);
            if (!space_for(dir, &op.from, bytes)) {
                copy_text(error, cap, "There isn't enough free space on this drive for the archive's contents.");
                rc = -1;
            } else {
                x.expected = bytes;
                rc = extract_archive(&x, error, cap);
            }
        }
        pthread_mutex_unlock(&extraction_mutex);
        if (rc != -1 || !strstr(error, "assword")) break;
        /* None of the passwords Orbit knows (or the one given) opened it. */
        if (!password_wait(*password ? "That password didn't work. Try again." : "")) {
            rc = 1;
            break;
        }
    }
    /* The extractor speaks of downloads; here the archive is simply kept. */
    if (rc == -1 && (strstr(error, "download") || strstr(error, "Download")))
        copy_text(error, cap, "The archive couldn't be unpacked. It may be damaged or use an unsupported format.");
    close(dir);
    return rc;
}
/* Hands the entry to the installer's receipts and follows them: packages
 * install, an image or game folder goes to ShadowMount. It can't be stopped
 * once the PS5 has it, so it isn't cancelled; Orbit stopping leaves it running. */
static int run_install(char *error, size_t cap) {
    char parent[PATH_LIMIT];
    const char *leaf;
    split(op.path, parent, sizeof parent, &leaf);
    Job pin;
    memset(&pin, 0, sizeof pin);
    copy_text(pin.storage_id, sizeof pin.storage_id, op.from.id);
    copy_text(pin.root, sizeof pin.root, op.from.root);
    pin.device = op.from.device;
    pin.inode = op.from.inode;
    int code = packages_install_path(&pin, *parent ? parent : ".", leaf, op.origin, error, cap);
    if (code != 202) return -1;
    while (!atomic_load(&stop_flag) && !orbit_is_stopping()) {
        struct timespec wait = {.tv_sec = 0, .tv_nsec = 250000000};
        nanosleep(&wait, NULL);
        cJSON *r = packages_for_download(op.origin), *p;
        const char *state = json_text(r, "state"), *scan = json_text(r, "shadowMount");
        if (!cJSON_IsObject(r) || !strcmp(state, "error")) {
            copy_text(error, cap, *json_text(r, "message") ? json_text(r, "message") : "The installer couldn't check these files.");
            cJSON_Delete(r);
            return -1;
        }
        if (!strcmp(state, "checking")) {
            cJSON_Delete(r);
            continue;
        }
        int64_t received = 0, total = 0;
        int packages = 0;
        bool busy = !strcmp(scan, "pending") || !strcmp(scan, "requested");
        const char *failed = NULL;
        cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(r, "packages")) {
            const char *s = json_text(p, "state");
            packages++;
            received += json_int(p, "received");
            total += json_int(p, "total");
            if (!strcmp(s, "queued") || !strcmp(s, "installing")) busy = true;
            else if (!strcmp(s, "error") || !strcmp(s, "blocked") || !strcmp(s, "ready")) failed = json_text(p, "message");
        }
        if (total > 0) {
            set_total(total);
            advance(received, true);
        }
        if (busy) {
            cJSON_Delete(r);
            continue;
        }
        int rc = 0;
        if (failed) {
            copy_text(error, cap, *failed ? failed : "The package couldn't be installed.");
            rc = -1;
        } else if (!strcmp(scan, "unavailable") || !strcmp(scan, "unconfirmed")) {
            copy_text(error, cap, *json_text(r, "shadowMountMessage") ? json_text(r, "shadowMountMessage")
                                                                      : "ShadowMount couldn't add this game.");
            rc = -1;
        } else if (!packages && strcmp(scan, "registered")) {
            copy_text(error, cap, "No package, game image or game folder was found.");
            rc = -1;
        }
        cJSON_Delete(r);
        return rc;
    }
    copy_text(error, cap, "Orbit stopped while following the installation. Check the PS5 home screen.");
    return -1;
}
/* A drive-relative folder ShadowMount scans: homebrew, or inside it. */
static bool in_homebrew(const char *folder) {
    return !strncmp(folder, "homebrew", 8) && (!folder[8] || folder[8] == '/');
}
/* After a copy, move or unpack: did a game land where a scan finds it? */
static bool game_added(void) {
    char parent[PATH_LIMIT];
    const char *leaf;
    split(op.path, parent, sizeof parent, &leaf);
    if (!strcmp(op.action, "unpack")) return in_homebrew(parent);
    if (!in_homebrew(op.to_path)) return false;
    if (!strcmp(file_type(op.name), "image")) return true;
    int dir = open_place(&op.to, op.to_path);
    bool game = dir >= 0 && game_folder(dir, op.name);
    if (dir >= 0) close(dir);
    return game;
}
static void *run(void *unused) {
    (void)unused;
    char error[256] = "";
    int rc = -1;
    if (!strcmp(op.action, "copy") || !strcmp(op.action, "move")) rc = run_copy(!strcmp(op.action, "move"), error, sizeof error);
    else if (!strcmp(op.action, "delete")) rc = run_delete(error, sizeof error);
    else if (!strcmp(op.action, "measure")) rc = run_measure(error, sizeof error);
    else if (!strcmp(op.action, "unpack")) rc = run_unpack(error, sizeof error);
    else if (!strcmp(op.action, "install")) rc = run_install(error, sizeof error);
    if (!rc && strcmp(op.action, "delete") && strcmp(op.action, "measure") && strcmp(op.action, "install") && game_added())
        library_auto_scan();
    pthread_mutex_lock(&lock);
    copy_text(op.state, sizeof op.state, !rc ? "complete" : rc == 1 ? "cancelled" : "failed");
    copy_text(op.error, sizeof op.error, rc == -1 ? error : "");
    if (!rc && op.total > op.processed) op.processed = op.total;
    op.speed = 0;
    memset(op.password, 0, sizeof op.password);
    if (strcmp(op.action, "measure")) storage_release();
    running = false;
    pthread_mutex_unlock(&lock);
    return NULL;
}

/* ---- requests ---- */

static bool receiving(void) {
    pthread_mutex_lock(&orbit.mutex);
    bool active = orbit.receiver_active;
    pthread_mutex_unlock(&orbit.mutex);
    return active;
}
/* The entry the request names: its drive, its folder (open) and its kind. */
typedef struct {
    Storage drive;
    char path[PATH_LIMIT], parent[PATH_LIMIT], full[1200];
    const char *leaf;
    int dir;
    struct stat st;
} Entry;
static int entry_open(const cJSON *input, Entry *e, char *error, size_t cap) {
    const char *id = json_text(input, "storageId"), *path = json_text(input, "path");
    e->dir = -1;
    if (!*id || drive_find(id, &e->drive)) {
        copy_text(error, cap, "This drive is not connected.");
        return 404;
    }
    if (!*path || !path_ok(&e->drive, path)) {
        copy_text(error, cap, "Choose a file or folder on this drive.");
        return 400;
    }
    copy_text(e->path, sizeof e->path, path);
    split(e->path, e->parent, sizeof e->parent, &e->leaf);
    absolute(&e->drive, e->path, e->full, sizeof e->full);
    e->dir = open_place(&e->drive, e->parent);
    if (e->dir < 0 || fstatat(e->dir, e->leaf, &e->st, AT_SYMLINK_NOFOLLOW) ||
        (!S_ISDIR(e->st.st_mode) && !S_ISREG(e->st.st_mode)) || e->st.st_dev != e->drive.device) {
        if (e->dir >= 0) close(e->dir);
        e->dir = -1;
        copy_text(error, cap, "This isn't there anymore. Open the folder again and retry.");
        return 404;
    }
    return 0;
}
static bool operation_running(void) {
    return present && !ended(op.state);
}
static int start(const cJSON *input, const char *action, char *error, size_t cap) {
    Entry e;
    int code = entry_open(input, &e, error, cap);
    if (code) return code;
    bool folder = S_ISDIR(e.st.st_mode);
    bool game = folder && game_folder(e.dir, e.leaf);
    close(e.dir);
    if (strcmp(action, "measure") && receiving()) {
        copy_text(error, cap, "A transfer from your computer is writing to this PS5. Wait for it to finish.");
        return 409;
    }
    Storage to = {0};
    char to_path[PATH_LIMIT] = "";
    if (!strcmp(action, "copy") || !strcmp(action, "move")) {
        const char *to_id = json_text(input, "toStorageId");
        if (strlen(json_text(input, "toPath")) >= sizeof to_path) {
            copy_text(error, cap, "Choose a folder on the destination drive.");
            return 400;
        }
        copy_text(to_path, sizeof to_path, json_text(input, "toPath"));
        if (!*to_id || drive_find(to_id, &to)) {
            copy_text(error, cap, "The destination drive is not connected.");
            return 404;
        }
        if (!path_ok(&to, to_path)) {
            copy_text(error, cap, "Choose a folder on the destination drive.");
            return 400;
        }
        char destination[1200];
        absolute(&to, to_path, destination, sizeof destination);
        size_t n = strlen(e.full);
        if (!strcmp(destination, e.full) || (!strncmp(destination, e.full, n) && destination[n] == '/')) {
            copy_text(error, cap, "A folder can't be copied or moved into itself.");
            return 409;
        }
        if (!strcmp(action, "move") && !strcmp(to.id, e.drive.id) && !strcmp(to_path, e.parent)) {
            copy_text(error, cap, "It's already in this folder.");
            return 409;
        }
        int dir = open_place(&to, to_path);
        if (dir < 0) {
            copy_text(error, cap, "The destination folder isn't there anymore.");
            return 404;
        }
        close(dir);
        char target[1500];
        snprintf(target, sizeof target, "%s/%s", destination, e.leaf);
        Guards g = guards_collect();
        const char *reason = locked(&g, target);
        guards_free(&g);
        if (*reason) {
            copy_text(error, cap, locked_text(reason));
            return 409;
        }
    }
    if (!strcmp(action, "unpack") && (folder || strcmp(file_type(e.leaf), "archive"))) {
        copy_text(error, cap, "Choose a ZIP, 7z or RAR archive.");
        return 400;
    }
    if (!strcmp(action, "install") && (folder ? !game : strcmp(file_type(e.leaf), "package") && strcmp(file_type(e.leaf), "image"))) {
        copy_text(error, cap, "Choose a PKG file, game image or game folder.");
        return 400;
    }
    if (!strcmp(action, "measure") && !folder) {
        copy_text(error, cap, "Choose a folder to measure.");
        return 400;
    }
    /* Reading is allowed while others use it; changing it is not. */
    Guards g = guards_collect();
    const char *reason = locked(&g, e.full);
    guards_free(&g);
    bool changes = !strcmp(action, "move") || !strcmp(action, "delete");
    bool reads = !strcmp(action, "copy") || !strcmp(action, "unpack") || !strcmp(action, "install");
    if ((changes && *reason) || (reads && (!strcmp(reason, "busy") || !strcmp(reason, "download") || !strcmp(reason, "orbit"))) ||
        (!strcmp(action, "unpack") && *reason) ||
        (!strcmp(action, "install") && !strcmp(reason, "install"))) {
        copy_text(error, cap, locked_text(reason));
        return 409;
    }
    pthread_mutex_lock(&lock);
    /* The last operation's thread has finished; reclaim it once (the lock is
     * released while joining, so check again after). */
    while (joinable && !operation_running()) {
        pthread_t previous = thread;
        joinable = false;
        pthread_mutex_unlock(&lock);
        pthread_join(previous, NULL);
        pthread_mutex_lock(&lock);
    }
    if (operation_running() || stopping) {
        pthread_mutex_unlock(&lock);
        copy_text(error, cap, "Another file operation is running. Wait for it to finish, or cancel it.");
        return 409;
    }
    memset(&op, 0, sizeof op);
    char hex[17];
    random_hex(hex, 8);
    snprintf(op.id, sizeof op.id, OPERATION_PREFIX "%s", hex);
    copy_text(op.action, sizeof op.action, action);
    copy_text(op.state, sizeof op.state, !strcmp(action, "measure") ? "measuring" : "running");
    copy_text(op.name, sizeof op.name, e.leaf);
    op.from = e.drive;
    op.to = to;
    copy_text(op.path, sizeof op.path, e.path);
    copy_text(op.to_path, sizeof op.to_path, to_path);
    op.size = -1;
    if (!strcmp(action, "install")) {
        /* One receipt per file as it is now: the same file reuses it. */
        char identity[1400];
        snprintf(identity, sizeof identity, "%s\n%ju\n%ju\n%jd\n%jd", e.full, (uintmax_t)e.st.st_dev,
                 (uintmax_t)e.st.st_ino, (intmax_t)e.st.st_size, (intmax_t)e.st.st_mtime);
        unsigned long long h = 1469598103934665603ULL;
        for (const unsigned char *p = (const unsigned char *)identity; *p; p++) h = (h ^ *p) * 1099511628211ULL;
        snprintf(op.origin, sizeof op.origin, OPERATION_PREFIX "%016llx", h);
    }
    atomic_store(&op.pause, false);
    atomic_store(&op.cancel, false);
    present = running = true;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, ORBIT_THREAD_STACK);
    int failed = pthread_create(&thread, &attr, run, NULL);
    pthread_attr_destroy(&attr);
    if (failed) {
        copy_text(op.state, sizeof op.state, "failed");
        copy_text(op.error, sizeof op.error, "Orbit couldn't start this. Try again.");
        running = false;
        if (strcmp(action, "measure")) storage_release();
    } else
        joinable = true;
    pthread_mutex_unlock(&lock);
    return 202;
}
/* Pause, resume, cancel, and a password for an unpack that needs one. */
static int control(const cJSON *input, const char *action, char *error, size_t cap) {
    pthread_mutex_lock(&lock);
    int code = 200;
    if (!present || strcmp(json_text(input, "id"), op.id) || ended(op.state)) {
        code = 404;
        copy_text(error, cap, "This operation has already finished.");
    } else if (!strcmp(action, "cancel")) {
        if (!strcmp(op.action, "install")) {
            code = 409;
            copy_text(error, cap, "An installation can't be stopped from Orbit.");
        } else if (op.final_phase) {
            code = 409;
            copy_text(error, cap, "The copy is already in place. Orbit is removing the originals.");
        } else {
            atomic_store(&op.cancel, true);
            atomic_store(&op.pause, false);
            pthread_cond_broadcast(&wake);
        }
    } else if (!strcmp(action, "password")) {
        const char *password = json_text(input, "password");
        if (strcmp(op.state, "password")) {
            code = 409;
            copy_text(error, cap, "This archive isn't waiting for a password.");
        } else if (!*password || strlen(password) >= sizeof op.password) {
            code = 400;
            copy_text(error, cap, "Enter the archive's password.");
        } else {
            copy_text(op.password, sizeof op.password, password);
            op.password_given = true;
            pthread_cond_broadcast(&wake);
        }
    } else if (strcmp(op.action, "copy") && strcmp(op.action, "move")) {
        code = 409;
        copy_text(error, cap, "Only copies and moves can be paused.");
    } else if (op.final_phase) {
        code = 409;
        copy_text(error, cap, "The copy is already in place. Orbit is removing the originals.");
    } else {
        atomic_store(&op.pause, !strcmp(action, "pause"));
        pthread_cond_broadcast(&wake);
    }
    pthread_mutex_unlock(&lock);
    return code;
}
static int make_folder(const cJSON *input, char *error, size_t cap) {
    const char *id = json_text(input, "storageId"), *path = json_text(input, "path"), *name = json_text(input, "name");
    Storage s;
    if (!*id || drive_find(id, &s)) {
        copy_text(error, cap, "This drive is not connected.");
        return 404;
    }
    if (!path_ok(&s, path)) {
        copy_text(error, cap, "Choose a folder on this drive.");
        return 400;
    }
    if (!new_name_ok(name) || hidden(&s, path, name)) {
        copy_text(error, cap, "That name can't be used on this drive. Choose another name.");
        return 400;
    }
    char full[1500], parent[1200];
    absolute(&s, path, parent, sizeof parent);
    snprintf(full, sizeof full, "%s/%s", parent, name);
    Guards g = guards_collect();
    const char *reason = locked(&g, full);
    guards_free(&g);
    if (*reason) {
        copy_text(error, cap, locked_text(reason));
        return 409;
    }
    int dir = open_place(&s, path);
    if (dir < 0) {
        copy_text(error, cap, "This folder isn't there anymore. Go up a folder and try again.");
        return 404;
    }
    int code = 200;
    if (mkdirat(dir, name, 0755)) {
        code = errno == EEXIST ? 409 : 500;
        copy_text(error, cap, errno == EEXIST ? "A file or folder with that name already exists here." : "Couldn't create the folder. Check the drive and try again.");
    } else
        fsync(dir);
    close(dir);
    return code;
}
static int rename_entry(const cJSON *input, char *error, size_t cap) {
    const char *name = json_text(input, "name");
    if (!new_name_ok(name)) {
        copy_text(error, cap, "That name can't be used on this drive. Choose another name.");
        return 400;
    }
    Entry e;
    int code = entry_open(input, &e, error, cap);
    if (code) return code;
    if (hidden(&e.drive, e.parent, name)) {
        close(e.dir);
        copy_text(error, cap, "That name can't be used on this drive. Choose another name.");
        return 400;
    }
    Guards g = guards_collect();
    const char *reason = locked(&g, e.full);
    guards_free(&g);
    struct stat other;
    if (*reason || receiving()) {
        copy_text(error, cap, *reason ? locked_text(reason) : "A transfer from your computer is writing to this PS5. Wait for it to finish.");
        code = 409;
    } else if (!strcmp(name, e.leaf)) {
        code = 200;
    } else if (!fstatat(e.dir, name, &other, AT_SYMLINK_NOFOLLOW) &&
               (other.st_dev != e.st.st_dev || other.st_ino != e.st.st_ino)) {
        /* The same entry under another case is a case change on exFAT. */
        copy_text(error, cap, "A file or folder with that name already exists here.");
        code = 409;
    } else if ((!fstatat(e.dir, name, &other, AT_SYMLINK_NOFOLLOW) ? renameat(e.dir, e.leaf, e.dir, name) :
                S_ISDIR(e.st.st_mode) ? publish_directory(e.dir, e.leaf, e.dir, name) :
                extract_publish_file(e.dir, e.leaf, e.dir, name)) || fsync(e.dir)) {
        copy_text(error, cap, "Couldn't rename it. Check the drive and try again.");
        code = 500;
    } else
        code = 200;
    close(e.dir);
    return code;
}
int files_action(const cJSON *input, char *error, size_t cap) {
    const char *action = json_text(input, "action");
    if (!strcmp(action, "pause") || !strcmp(action, "resume") || !strcmp(action, "cancel") || !strcmp(action, "password"))
        return control(input, action, error, cap);
    if (!strcmp(action, "measure")) return start(input, action, error, cap);
    bool mutation = !strcmp(action, "folder") || !strcmp(action, "rename") || !strcmp(action, "copy") ||
        !strcmp(action, "move") || !strcmp(action, "delete") || !strcmp(action, "unpack") || !strcmp(action, "install");
    if (mutation) {
        if (!storage_claim()) {
            copy_text(error, cap, "Wait for the current storage operation to finish, or pause active downloads first.");
            return 409;
        }
        int code = !strcmp(action, "folder") ? make_folder(input, error, cap) :
                   !strcmp(action, "rename") ? rename_entry(input, error, cap) : start(input, action, error, cap);
        if (code != 202) storage_release();
        return code;
    }
    copy_text(error, cap, "Unknown file action.");
    return 400;
}

/* ---- saving a file to a device ---- */

int files_link(const cJSON *input, cJSON **result, char *error, size_t cap) {
    Entry e;
    int code = entry_open(input, &e, error, cap);
    if (code) return code;
    close(e.dir);
    if (!S_ISREG(e.st.st_mode)) {
        copy_text(error, cap, "Choose a file to save.");
        return 400;
    }
    pthread_mutex_lock(&lock);
    Ticket *t = &tickets[next_ticket++ % TICKETS];
    memset(t, 0, sizeof *t);
    random_hex(t->id, 16);
    copy_text(t->storage, sizeof t->storage, e.drive.id);
    copy_text(t->path, sizeof t->path, e.path);
    copy_text(t->name, sizeof t->name, e.leaf);
    t->device = e.st.st_dev;
    t->inode = e.st.st_ino;
    t->size = e.st.st_size;
    t->modified = e.st.st_mtim;
    t->expires = time(NULL) + TICKET_SECONDS;
    char url[1100];
    int n = snprintf(url, sizeof url, "/api/v1/files/download/%s/", t->id);
    for (const unsigned char *p = (const unsigned char *)e.leaf; *p && n < (int)sizeof url - 4; p++)
        n += strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~", *p) && *p
                 ? snprintf(url + n, sizeof url - (size_t)n, "%c", *p)
                 : snprintf(url + n, sizeof url - (size_t)n, "%%%02X", *p);
    pthread_mutex_unlock(&lock);
    *result = cJSON_CreateObject();
    cJSON_AddStringToObject(*result, "url", url);
    cJSON_AddStringToObject(*result, "name", e.leaf);
    cJSON_AddNumberToObject(*result, "sizeBytes", (double)e.st.st_size);
    return 200;
}
int files_download_open(const char *ticket, int *fd, int64_t *size, char *name, size_t name_cap,
                        char *error, size_t cap) {
    Ticket t = {0};
    bool found = false;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < TICKETS; i++)
        if (tickets[i].id[0] && strlen(ticket) == 32 && !strcmp(tickets[i].id, ticket) && tickets[i].expires >= time(NULL)) {
            t = tickets[i];
            found = true;
        }
    pthread_mutex_unlock(&lock);
    if (!found) {
        copy_text(error, cap, "This link has expired. Save the file again from Files.");
        return 404;
    }
    Storage s;
    char parent[PATH_LIMIT];
    const char *leaf;
    split(t.path, parent, sizeof parent, &leaf);
    int dir = drive_find(t.storage, &s) ? -1 : open_place(&s, parent);
    int file = dir >= 0 ? openat(dir, leaf, O_RDONLY | O_NOFOLLOW) : -1;
    if (dir >= 0) close(dir);
    struct stat st;
    if (file < 0 || fstat(file, &st) || !S_ISREG(st.st_mode) || st.st_dev != t.device || st.st_ino != t.inode ||
        st.st_size != t.size || st.st_mtim.tv_sec != t.modified.tv_sec || st.st_mtim.tv_nsec != t.modified.tv_nsec) {
        if (file >= 0) close(file);
        copy_text(error, cap, "The file changed or isn't there anymore. Save it again from Files.");
        return 409;
    }
    *fd = file;
    *size = st.st_size;
    copy_text(name, name_cap, t.name);
    return 200;
}
void files_stop(void) {
    pthread_mutex_lock(&lock);
    stopping = true;
    atomic_store(&stop_flag, true);
    pthread_cond_broadcast(&wake);
    bool join = joinable;
    joinable = false;
    pthread_mutex_unlock(&lock);
    if (join) pthread_join(thread, NULL);
}
