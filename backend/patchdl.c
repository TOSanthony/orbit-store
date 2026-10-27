/* Downloads a game update from Sony and installs it. One update and one DLC at a time: they have a
 * slot each, so a DLC can download while an update does.
 *
 * The patch is a list of pieces (patchmanifest.h). Each piece is fetched with a Range request,
 * written at its place in a ".part" file next to the console's other downloads, and checked
 * against its SHA-256/SHA-1 before it counts. A small state file records what is done, so a
 * stopped or failed download resumes instead of starting over. Only when every piece is verified
 * is the file renamed to ".pkg" and handed to the console's installer. */
#define _POSIX_C_SOURCE 200809L
#include "install_service.h"
#include "orbit.h"
#include "patchfeed.h"
#include "patchmanifest.h"
#include "sonynet.h"
#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#define BLOCK (1024 * 1024)
#define MANIFEST_MAX (4 * 1024 * 1024)
#define MAX_NETWORK_FAILURES 8
#define SAVE_EVERY 5

/* What a paused download leaves on its drive, so cancelling it later can delete it. */
typedef struct {
    bool present;
    Storage drive;
    char part[96], state[96];
    bool pkg;          /* the finished package is on the drive (a failed install leaves it there) */
    char pkg_name[64];
} Kept;

typedef struct Slot {
    pthread_mutex_t mutex;
    bool active;
    atomic_bool cancel; /* stop and throw the download away */
    atomic_bool pause;  /* stop and keep it, to resume later */
    char title_id[32], name[96], version[24];
    char phase[16], error[200], detail[200];
    int64_t done, total;
    double speed;
    unsigned piece, pieces, install_percent;
    bool waiting; /* the console took the package and the update is not in yet */
    bool started;
    atomic_bool fan_failed; /* a connection of the running piece gave up */
    Kept kept;
} Slot;

/* slots[0] runs the game updates, slots[1] the DLC (their key is "<game>/<label>"). The code below
 * works on "the slot of this thread": each thread that touches a download sets `cur` first, so the
 * single-download code reads as before. */
static Slot slots[2] = {{.mutex = PTHREAD_MUTEX_INITIALIZER}, {.mutex = PTHREAD_MUTEX_INITIALIZER}};
static __thread Slot *cur = &slots[0];
#define pd (*cur)
#define kept (cur->kept)
static Slot *slot_for(const char *title_id) {
    return title_id && strchr(title_id, '/') ? &slots[1] : &slots[0];
}

/* The updates and DLC that finished, one entry each, so a long list of DLC leaves a line per DLC in the
 * downloads history rather than only the last one. Kept in memory, newest last. */
#define HISTORY_MAX 64
typedef struct {
    char title_id[32], name[96], version[24], phase[16], error[200], detail[200];
    int64_t total;
    bool pkg; /* the package is still on the drive (an install that failed) */
    Storage drive;
    char pkg_name[64];
} Finished;
static struct {
    pthread_mutex_t mutex;
    Finished items[HISTORY_MAX];
    size_t count;
} history = {.mutex = PTHREAD_MUTEX_INITIALIZER};

static bool finished_phase(const char *phase) {
    return !strcmp(phase, "installed") || !strcmp(phase, "downloaded") || !strcmp(phase, "failed");
}
/* The caller holds the slot's mutex. */
static void history_add(const Slot *s, bool pkg, const Storage *drive, const char *pkg_name) {
    if (!s->title_id[0] || !finished_phase(s->phase))
        return;
    pthread_mutex_lock(&history.mutex);
    for (size_t i = 0; i < history.count; i++)
        if (!strcmp(history.items[i].title_id, s->title_id)) {
            memmove(&history.items[i], &history.items[i + 1], (history.count - i - 1) * sizeof history.items[0]);
            history.count--;
            break;
        }
    if (history.count == HISTORY_MAX) {
        memmove(&history.items[0], &history.items[1], (HISTORY_MAX - 1) * sizeof history.items[0]);
        history.count--;
    }
    Finished *f = &history.items[history.count++];
    memset(f, 0, sizeof *f);
    copy_text(f->title_id, sizeof f->title_id, s->title_id);
    copy_text(f->name, sizeof f->name, s->name);
    copy_text(f->version, sizeof f->version, s->version);
    copy_text(f->phase, sizeof f->phase, s->phase);
    copy_text(f->error, sizeof f->error, s->error);
    copy_text(f->detail, sizeof f->detail, s->detail);
    f->total = s->total;
    f->pkg = pkg;
    f->drive = *drive;
    copy_text(f->pkg_name, sizeof f->pkg_name, pkg_name);
    pthread_mutex_unlock(&history.mutex);
}
/* Takes the entry out of the history; returns a copy when it was there. */
static bool history_take(const char *title_id, Finished *out) {
    bool found = false;
    pthread_mutex_lock(&history.mutex);
    for (size_t i = 0; i < history.count; i++)
        if (!strcmp(history.items[i].title_id, title_id)) {
            if (out)
                *out = history.items[i];
            memmove(&history.items[i], &history.items[i + 1], (history.count - i - 1) * sizeof history.items[0]);
            history.count--;
            found = true;
            break;
        }
    pthread_mutex_unlock(&history.mutex);
    return found;
}
static void remove_package(const Storage *drive, const char *name) {
    Job *place = calloc(1, sizeof *place);
    if (!place)
        return;
    copy_text(place->storage_id, sizeof place->storage_id, drive->id);
    copy_text(place->root, sizeof place->root, drive->root);
    place->device = drive->device;
    place->inode = drive->inode;
    int dir = storage_open(place);
    free(place);
    if (dir >= 0) {
        unlinkat(dir, name, 0);
        close(dir);
    }
}

/* The updates waiting for their turn, in order. Kept in game-update-queue.json so a restart
 * keeps it; each one is checked against the current offers when its turn comes. */
#define QUEUE_MAX 2048   /* "install all DLC" of a game with over a thousand DLC */
#define QUEUE_FILE "game-update-queue.json"
static struct {
    pthread_mutex_t mutex;
    char ids[QUEUE_MAX][32];
    size_t count;
} queue = {.mutex = PTHREAD_MUTEX_INITIALIZER};

static bool stopped(void) {
    return atomic_load(&pd.cancel) || atomic_load(&pd.pause) || orbit_is_stopping();
}
/* The phase a stop leads to: paused when the person paused, else cancelled. */
static const char *stop_phase(void) {
    return atomic_load(&pd.pause) && !atomic_load(&pd.cancel) && !orbit_is_stopping() ? "paused" : "cancelled";
}
static void phase(const char *name, const char *error) {
    pthread_mutex_lock(&pd.mutex);
    copy_text(pd.phase, sizeof pd.phase, name);
    copy_text(pd.error, sizeof pd.error, error ? error : "");
    pd.detail[0] = 0;
    pthread_mutex_unlock(&pd.mutex);
}
static void finish_d(const char *name, const char *error, const char *detail) {
    pthread_mutex_lock(&pd.mutex);
    pd.waiting = false;
    copy_text(pd.phase, sizeof pd.phase, name);
    copy_text(pd.error, sizeof pd.error, error ? error : "");
    copy_text(pd.detail, sizeof pd.detail, detail ? detail : "");
    pd.active = false;
    pthread_mutex_unlock(&pd.mutex);
}
static void finish(const char *name, const char *error) {
    finish_d(name, error, "");
}

typedef struct {
    Slot *slot;              /* the download this job belongs to */
    int dir;                 /* the drive's homebrew folder */
    int fd;                  /* the .part file */
    char final_name[64], part[96], state[96];
    patchmanifest manifest;
    int64_t *written;        /* bytes of each piece already on disk */
    bool *done;
    char manifest_url[768];
    bool nas;                /* one file on the person's NAS rather than Sony's pieces */
    char nas_url[2048];
    time_t last_save;
} PatchJob;

/* ---- the resume state ---- */
static void save_state(PatchJob *job) {
    cJSON *o = cJSON_CreateObject(), *list = cJSON_CreateArray();
    if (!o || !list) {
        cJSON_Delete(o);
        cJSON_Delete(list);
        return;
    }
    cJSON_AddStringToObject(o, "manifest", job->manifest_url);
    cJSON_AddNumberToObject(o, "total", (double)job->manifest.total);
    for (size_t i = 0; i < job->manifest.count; i++) {
        cJSON *item = cJSON_CreateObject();
        if (!item)
            continue;
        cJSON_AddNumberToObject(item, "written", (double)job->written[i]);
        cJSON_AddBoolToObject(item, "done", job->done[i]);
        cJSON_AddItemToArray(list, item);
    }
    cJSON_AddItemToObject(o, "pieces", list);
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!text)
        return;
    char temporary[128];
    snprintf(temporary, sizeof temporary, "%s.tmp", job->state);
    fsync(job->fd);
    int fd = openat(job->dir, temporary, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd >= 0) {
        size_t length = strlen(text), done = 0;
        while (done < length) {
            ssize_t n = write(fd, text + done, length - done);
            if (n <= 0)
                break;
            done += (size_t)n;
        }
        bool ok = done == length;
        close(fd);
        if (ok)
            renameat(job->dir, temporary, job->dir, job->state);
        else
            unlinkat(job->dir, temporary, 0);
    }
    free(text);
    job->last_save = time(NULL);
}
static void load_state(PatchJob *job, int64_t part_size) {
    int fd = openat(job->dir, job->state, O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return;
    char *text = malloc(1 << 20);
    ssize_t n = text ? read(fd, text, (1 << 20) - 1) : -1;
    close(fd);
    if (n <= 0) {
        free(text);
        return;
    }
    cJSON *o = cJSON_ParseWithLength(text, (size_t)n);
    free(text);
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(o, "pieces");
    const cJSON *total = cJSON_GetObjectItemCaseSensitive(o, "total");
    if (cJSON_IsArray(list) && (size_t)cJSON_GetArraySize(list) == job->manifest.count &&
        !strcmp(json_text(o, "manifest"), job->manifest_url) && cJSON_IsNumber(total) &&
        (int64_t)total->valuedouble == job->manifest.total && part_size == job->manifest.total) {
        size_t i = 0;
        const cJSON *item;
        cJSON_ArrayForEach(item, list) {
            const cJSON *w = cJSON_GetObjectItemCaseSensitive(item, "written");
            int64_t size = job->manifest.pieces[i].size;
            int64_t value = cJSON_IsNumber(w) ? (int64_t)w->valuedouble : 0;
            job->written[i] = value < 0 ? 0 : value > size ? size : value;
            job->done[i] = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "done")) && job->written[i] == size;
            i++;
        }
    }
    cJSON_Delete(o);
}

/* ---- checking a piece ---- */
static bool digest_matches(PatchJob *job, const patchmanifest_piece *piece) {
    if (!piece->hash[0])
        return true; /* a NAS file listed without a checksum: only its size is checked */
    const EVP_MD *kind = strlen(piece->hash) == 40 ? EVP_sha1() : EVP_sha256();
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned char *buffer = malloc(BLOCK);
    if (!ctx || !buffer) {
        EVP_MD_CTX_free(ctx);
        free(buffer);
        return false;
    }
    EVP_DigestInit_ex(ctx, kind, NULL);
    int64_t done = 0;
    bool ok = true;
    while (done < piece->size) {
        if (stopped()) {
            ok = false;
            break;
        }
        size_t want = (size_t)(piece->size - done < BLOCK ? piece->size - done : BLOCK);
        ssize_t n = pread(job->fd, buffer, want, (off_t)(piece->offset + done));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            ok = false;
            break;
        }
        EVP_DigestUpdate(ctx, buffer, (size_t)n);
        done += n;
    }
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned length = 0;
    if (ok)
        EVP_DigestFinal_ex(ctx, out, &length);
    EVP_MD_CTX_free(ctx);
    free(buffer);
    if (!ok)
        return false;
    char hex[2 * EVP_MAX_MD_SIZE + 1];
    for (unsigned i = 0; i < length; i++)
        snprintf(hex + 2 * i, 3, "%02x", out[i]);
    return !strcmp(hex, piece->hash);
}

/* ---- progress shown to clients ---- */
typedef struct {
    time_t mark_time;
    int64_t mark_bytes;
    double speed;
} Speed;

static void publish(PatchJob *job, size_t current, int64_t position, double speed) {
    int64_t total = 0;
    for (size_t i = 0; i < job->manifest.count; i++)
        total += job->done[i] ? job->manifest.pieces[i].size : i == current ? position : job->written[i];
    pthread_mutex_lock(&pd.mutex);
    pd.done = total;
    pd.total = job->manifest.total;
    pd.speed = speed;
    pd.piece = (unsigned)(current + 1);
    pd.pieces = (unsigned)job->manifest.count;
    pthread_mutex_unlock(&pd.mutex);
}
static void wait_seconds(unsigned seconds) {
    for (unsigned i = 0; i < seconds && !stopped(); i++)
        sleep(1);
}

/* Downloads what is missing of piece i over several connections at once. The rest of the piece is cut
 * into slices; each connection takes the next slice, so one connection's speed limit no longer caps the
 * download. What is kept for a resume is the contiguous part from the start, as before: a slice that
 * finished beyond a gap is fetched again, which costs at most the slices that were in flight. */
#define PARALLEL_STREAMS 16
#define SLICE_BYTES (4LL * 1024 * 1024)
#define SLICE_RETRIES 3
/* How far ahead of the first unfinished slice the connections may go. Only the part before that slice
 * survives a stop, so without this limit one stalled slice could throw away hundreds of MB fetched past it. */
#define SLICE_WINDOW 24

static bool fan_stop(void) {
    return stopped() || atomic_load(&cur->fan_failed);
}

typedef struct {
    PatchJob *job;
    const patchmanifest_piece *piece;
    int64_t base;                   /* where the first slice starts inside the piece */
    size_t slices;
    _Atomic int64_t *reached;      /* per slice: the position reached inside the piece */
    atomic_bool *finished;          /* per slice */
    atomic_size_t next;
    atomic_size_t low;              /* every slice before this one is finished */
    atomic_int alive;
    pthread_mutex_t mutex;          /* guards the first error */
    sony_range_result result;
    char why[120];
} Fan;

static int64_t slice_start(const Fan *fan, size_t k) {
    return fan->base + (int64_t)k * SLICE_BYTES;
}
static int64_t slice_end(const Fan *fan, size_t k) {
    int64_t end = slice_start(fan, k) + SLICE_BYTES;
    return end < fan->piece->size ? end : fan->piece->size;
}

static void fan_fail(Fan *fan, sony_range_result r, const char *why) {
    pthread_mutex_lock(&fan->mutex);
    /* A real error beats the "aborted" the other connections report once one has failed. */
    if (fan->result == SONY_RANGE_OK || (fan->result == SONY_RANGE_ABORTED && r != SONY_RANGE_ABORTED)) {
        fan->result = r;
        snprintf(fan->why, sizeof fan->why, "%s", why);
    }
    pthread_mutex_unlock(&fan->mutex);
    atomic_store(&cur->fan_failed, true);
}

static size_t fan_low(Fan *fan) {
    size_t k = atomic_load(&fan->low);
    while (k < fan->slices && atomic_load(&fan->finished[k]))
        k++;
    size_t seen = atomic_load(&fan->low);
    while (k > seen && !atomic_compare_exchange_weak(&fan->low, &seen, k))
        ;
    return k;
}

static void *fan_worker(void *context) {
    Fan *fan = context;
    PatchJob *job = fan->job;
    cur = job->slot;
    char why[120] = "";
    for (;;) {
        size_t k = atomic_fetch_add(&fan->next, 1);
        if (k >= fan->slices || fan_stop())
            break;
        /* Too far ahead of a slice that is still running: wait for it rather than pile up data that would be lost. */
        while (k >= fan_low(fan) + SLICE_WINDOW && !fan_stop())
            nanosleep(&(struct timespec){.tv_nsec = 100 * 1000000L}, NULL);
        if (fan_stop())
            break;
        unsigned tries = 0;
        for (;;) {
            _Atomic int64_t *position = &fan->reached[k];
            sony_range_result r =
                job->nas ? nas_range(job->nas_url, atomic_load_explicit(position, memory_order_relaxed), slice_end(fan, k), job->fd, fan->piece->offset,
                                     position, fan_stop, why, sizeof why)
                         : sony_range(fan->piece->url, atomic_load_explicit(position, memory_order_relaxed), slice_end(fan, k), job->fd, fan->piece->offset,
                                      position, fan_stop, why, sizeof why);
            if (r == SONY_RANGE_OK) {
                atomic_store(&fan->finished[k], true);
                break;
            }
            if (r == SONY_RANGE_FAILED && ++tries <= SLICE_RETRIES && !fan_stop()) {
                sleep(1); /* a hiccup on one connection: try that slice again from where it stopped */
                continue;
            }
            fan_fail(fan, r, why);
            goto out;
        }
    }
out:
    sony_range_close();
    atomic_fetch_sub(&fan->alive, 1);
    return NULL;
}

/* The payload's standard output is kept nowhere a person can fetch it, so the speed lines go to
 * <state dir>/patch.log (over FTP: /data/orbit-store/patch.log). Capped; the old file becomes patch.prev.log. */
static void patch_log_file(const char *line) {
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    char path[560], old[560];
    snprintf(path, sizeof path, "%s/patch.log", orbit.state_dir);
    snprintf(old, sizeof old, "%s/patch.prev.log", orbit.state_dir);
    pthread_mutex_lock(&lock);
    FILE *f = fopen(path, "a");
    if (f) {
        fputs(line, f);
        long size = ftell(f);
        fclose(f);
        if (size > 128 * 1024)
            rename(path, old);
    }
    pthread_mutex_unlock(&lock);
}

static double wall_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
/* How long each step before the first byte took, for the "slow to start" question: patch.log. */
static void start_log(const char *what, double since) {
    char line[200];
    snprintf(line, sizeof line, "[patch] %s: %s took %.1f s\n", cur->title_id, what, wall_now() - since);
    fputs(line, stderr);
    patch_log_file(line);
}

static sony_range_result download_piece(PatchJob *job, size_t i, Speed *speed, char *why, size_t why_cap) {
    const patchmanifest_piece *piece = &job->manifest.pieces[i];
    if (job->written[i] >= piece->size)
        return SONY_RANGE_OK;
    const time_t started_at = time(NULL);
    Fan fan = {.job = job, .piece = piece, .base = job->written[i], .result = SONY_RANGE_OK};
    fan.slices = (size_t)((piece->size - fan.base + SLICE_BYTES - 1) / SLICE_BYTES);
    fan.reached = calloc(fan.slices, sizeof *fan.reached);
    fan.finished = calloc(fan.slices, sizeof *fan.finished);
    if (!fan.reached || !fan.finished) {
        free(fan.reached);
        free(fan.finished);
        snprintf(why, why_cap, "not enough memory");
        return SONY_RANGE_FAILED;
    }
    for (size_t k = 0; k < fan.slices; k++)
        atomic_store_explicit(&fan.reached[k], slice_start(&fan, k), memory_order_relaxed);
    atomic_store(&fan.next, 0);
    atomic_store(&fan.low, 0);
    atomic_store(&cur->fan_failed, false);
    pthread_mutex_init(&fan.mutex, NULL);

    double tick_time = wall_now(), tick_received = 0, tick_write = 0;
    long tick_requests = 0;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 512 * 1024);
    pthread_t threads[PARALLEL_STREAMS];
    size_t started = 0, wanted = fan.slices < PARALLEL_STREAMS ? fan.slices : PARALLEL_STREAMS;
    atomic_store(&fan.alive, (int)wanted);
    for (; started < wanted; started++)
        if (pthread_create(&threads[started], &attr, fan_worker, &fan))
            break;
    pthread_attr_destroy(&attr);
    if (started < wanted) {
        atomic_fetch_sub(&fan.alive, (int)(wanted - started));
        if (started == 0) {
            /* No thread at all: nothing could run. */
            fan_fail(&fan, SONY_RANGE_FAILED, "could not start the download");
        }
    }

    while (atomic_load(&fan.alive) > 0) {
        nanosleep(&(struct timespec){.tv_nsec = 200 * 1000000L}, NULL);
        int64_t prefix = fan.base, received = 0;
        bool gap = false;
        for (size_t k = 0; k < fan.slices; k++) {
            int64_t reached = atomic_load(&fan.finished[k]) ? slice_end(&fan, k) : atomic_load_explicit(&fan.reached[k], memory_order_relaxed);
            received += reached - slice_start(&fan, k);
            if (!gap) {
                prefix = reached;
                gap = reached < slice_end(&fan, k);
            }
        }
        job->written[i] = prefix;
        time_t now = time(NULL);
        if (now - speed->mark_time >= 3) {
            speed->speed = (double)(fan.base + received - speed->mark_bytes) / (double)(now - speed->mark_time);
            speed->mark_time = now;
            speed->mark_bytes = fan.base + received;
        }
        publish(job, i, fan.base + received, speed->speed);
        if (wall_now() - tick_time >= 15.0) {
            /* A line every 15 s: what the last stretch really did, so a slow or stuck download shows up at once. */
            SonyStats so_far;
            sony_stats_peek(&so_far);
            const double span = wall_now() - tick_time;
            char line[300];
            snprintf(line, sizeof line,
                     "[patch] %s piece %zu now: %.1f MB/s over the last %.0f s; write %.1f s, %ld requests finished, %.0f MB of the piece\n",
                     cur->title_id, i + 1, ((double)received - tick_received) / 1e6 / span, span,
                     so_far.write_seconds - tick_write, so_far.requests - tick_requests, (double)received / 1e6);
            fputs(line, stderr);
            patch_log_file(line);
            tick_time = wall_now();
            tick_received = (double)received;
            tick_write = so_far.write_seconds;
            tick_requests = so_far.requests;
        }
        if (now - job->last_save >= SAVE_EVERY)
            save_state(job);
    }
    for (size_t t = 0; t < started; t++)
        pthread_join(threads[t], NULL);

    int64_t prefix = fan.base;
    bool gap = false;
    for (size_t k = 0; k < fan.slices && !gap; k++) {
        int64_t reached = atomic_load(&fan.finished[k]) ? slice_end(&fan, k) : atomic_load_explicit(&fan.reached[k], memory_order_relaxed);
        prefix = reached;
        gap = reached < slice_end(&fan, k);
    }
    sony_range_result r = fan.result;
    if (r == SONY_RANGE_OK && prefix < piece->size)
        r = stopped() ? SONY_RANGE_ABORTED : SONY_RANGE_FAILED;
    job->written[i] = r == SONY_RANGE_IGNORED ? 0 : prefix;
    if (r != SONY_RANGE_OK)
        snprintf(why, why_cap, "%s", fan.why[0] ? fan.why : "the download was interrupted");
    pthread_mutex_destroy(&fan.mutex);
    free(fan.reached);
    free(fan.finished);
    atomic_store(&cur->fan_failed, false);
    if (r == SONY_RANGE_OK) {
        publish(job, i, piece->size, speed->speed);
        save_state(job);
        /* What this piece really got, to tune the connection count from facts. */
        const long seconds = (long)(time(NULL) - started_at);
        const double got = (double)(piece->size - fan.base);
        SonyStats net;
        sony_stats_take(&net);
        /* "write" is the time spent in pwrite summed over all connections: close to seconds x connections
         * means the drive is the limit; small means the network or the console is. */
        char line[400];
        snprintf(line, sizeof line,
                 "[patch] %s piece %zu/%zu: %.1f MB in %ld s = %.1f MB/s over up to %d connections; "
                 "write %.1f s, %ld requests, connect %.2f s, first byte %.2f s (sums), server %s\n",
                 cur->title_id, i + 1, job->manifest.count, got / 1e6, seconds,
                 seconds > 0 ? got / 1e6 / (double)seconds : got / 1e6, PARALLEL_STREAMS, net.write_seconds,
                 net.requests, net.connect_seconds, net.first_byte_seconds, net.ip);
        fputs(line, stderr);
        patch_log_file(line);
    }
    return r;
}

/* One piece downloaded, with retries. Returns "" once all of it is on the drive, "cancelled" when
 * stopped, or a reason. Its checksum is checked separately (see settle), while the next piece downloads. */
static const char *fetch_piece(PatchJob *job, size_t i, char *detail, size_t detail_cap) {
    const patchmanifest_piece *piece = &job->manifest.pieces[i];
    unsigned network_failures = 0;
    Speed speed = {.mark_time = time(NULL), .mark_bytes = job->written[i]};
    while (job->written[i] < piece->size) {
        if (stopped())
            return "cancelled";
        char why[120] = "";
        sony_range_result r = download_piece(job, i, &speed, why, sizeof why);
        if (r == SONY_RANGE_OK)
            break;
        save_state(job);
        if (stopped())
            return "cancelled";
        if (r == SONY_RANGE_DISK) {
            snprintf(detail, detail_cap, "%s", why);
            return "The drive is full or was disconnected.";
        }
        if (r == SONY_RANGE_REFUSED) {
            snprintf(detail, detail_cap, "piece %zu: %s", i + 1, why);
            return "Sony refused the download. The link may have expired.";
        }
        if (++network_failures > MAX_NETWORK_FAILURES) {
            snprintf(detail, detail_cap, "piece %zu: %s", i + 1, why);
            return "The connection to Sony kept failing.";
        }
        unsigned wait = 1u << (network_failures < 5 ? network_failures : 5);
        wait_seconds(wait);
    }
    return "";
}

/* A piece being checked on its own thread while the next one downloads. */
typedef struct {
    PatchJob *job;
    size_t piece;
    bool ok;
    bool running;
    pthread_t thread;
} Check;

static void *check_worker(void *context) {
    Check *check = context;
    cur = check->job->slot;
    check->ok = digest_matches(check->job, &check->job->manifest.pieces[check->piece]);
    return NULL;
}

static bool check_start(Check *check, PatchJob *job, size_t piece) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 512 * 1024);
    check->job = job;
    check->piece = piece;
    check->ok = false;
    check->running = pthread_create(&check->thread, &attr, check_worker, check) == 0;
    pthread_attr_destroy(&attr);
    if (!check->running) { /* no thread to spare: check it here, as one piece at a time */
        check->ok = digest_matches(job, &job->manifest.pieces[piece]);
        check->running = true;
        check->thread = 0;
        return false;
    }
    return true;
}

/* Waits for the running check. A piece that fails its checksum is downloaded again once; a second
 * failure ends the download. *redo receives that piece's number so the caller goes back to it. */
static const char *settle(PatchJob *job, Check *check, unsigned *failures, size_t *redo, char *detail,
                          size_t detail_cap) {
    if (check->thread)
        pthread_join(check->thread, NULL);
    check->running = false;
    check->thread = 0;
    if (stopped())
        return "cancelled";
    if (check->ok) {
        job->done[check->piece] = true;
        save_state(job);
        return "";
    }
    if (++failures[check->piece] >= 2) {
        snprintf(detail, detail_cap, "piece %zu", check->piece + 1);
        return "A downloaded piece failed its checksum twice.";
    }
    job->written[check->piece] = 0;
    save_state(job);
    *redo = check->piece;
    return "";
}

#define INSTALL_VERIFY_SECONDS 1800
#define INSTALL_VERIFY_STEP 5

static void installed_progress(unsigned percent, void *context) {
    cur = context;
    pthread_mutex_lock(&pd.mutex);
    pd.install_percent = percent;
    pthread_mutex_unlock(&pd.mutex);
}

/* Updates and DLC download to a USB drive, never to the console's own drive: the first one (usb0,
 * then usb1...) with room for the file. Returns 1 when one was chosen, 0 when the drives are full,
 * -1 when no USB drive is connected. */
static int choose_drive(int64_t total, Storage *chosen) {
    Storage drives[ORBIT_MAX_STORAGE];
    size_t count = storage_list(drives);
    bool any = false;
    for (size_t i = 0; i < count; i++) {
        if (strncmp(drives[i].id, "usb", 3) && strcmp(drives[i].id, "desktop"))
            continue;
        any = true;
        if (drives[i].free_bytes < (uint64_t)total + (256ULL << 20))
            continue;
        *chosen = drives[i];
        return 1;
    }
    return any ? 0 : -1;
}

static void run(const char *manifest_url) {
    char why[160] = "", detail[200] = "";
    phase("preparing", "");
    const double run_began = wall_now();
    PatchJob job;
    memset(&job, 0, sizeof job);
    job.slot = cur;
    job.dir = job.fd = -1;
    copy_text(job.manifest_url, sizeof job.manifest_url, manifest_url);
    if (!strncmp(manifest_url, "nas:", 4)) {
        /* An update file listed in the NAS index: one piece, checked by its SHA-256 when the index has it. */
        Release release;
        if (!nas_release_copy(manifest_url + 4, &release) || release.size <= 0) {
            return finish("failed", "This game has no update this console can install.");
        }
        job.nas = true;
        copy_text(job.nas_url, sizeof job.nas_url, release.url);
        job.manifest.pieces = calloc(1, sizeof *job.manifest.pieces);
        if (!job.manifest.pieces)
            return finish("failed", "Not enough memory.");
        job.manifest.count = 1;
        job.manifest.total = release.size;
        job.manifest.pieces[0].offset = 0;
        job.manifest.pieces[0].size = release.size;
        copy_text(job.manifest.pieces[0].url, sizeof job.manifest.pieces[0].url, "nas");
        if (strlen(release.sha256) == 64)
            copy_text(job.manifest.pieces[0].hash, sizeof job.manifest.pieces[0].hash, release.sha256);
    } else {
        SonyBody body = {0};
        const double manifest_began = wall_now();
        const bool got_manifest = sony_get(manifest_url, MANIFEST_MAX, &body, stopped, why, sizeof why);
        start_log(got_manifest ? "reading Sony's update list" : "reading Sony's update list (failed)", manifest_began);
        if (!got_manifest) {
            if (stopped())
                return finish(stop_phase(), "");
            return finish_d("failed", "Could not read Sony's update list.", why);
        }
        int parsed = patchmanifest_parse(body.data, body.size, &job.manifest, why, sizeof why);
        free(body.data);
        if (parsed < 0) {
            return finish_d("failed", "Sony's update list could not be used.", why);
        }
    }
    Storage drive;
    int found = choose_drive(job.manifest.total, &drive);
    if (found != 1) {
        patchmanifest_free(&job.manifest);
        return finish("failed", found < 0 ? "Connect a USB drive to download updates and add-ons."
                                           : "Not enough free space on the USB drives. Free some space and try again.");
    }
    Job *place = calloc(1, sizeof *place);
    if (place) {
        copy_text(place->storage_id, sizeof place->storage_id, drive.id);
        copy_text(place->root, sizeof place->root, drive.root);
        place->device = drive.device;
        place->inode = drive.inode;
        job.dir = storage_open(place);
        free(place);
    }
    if (job.dir < 0) {
        patchmanifest_free(&job.manifest);
        return finish("failed", "The drive could not be opened for writing.");
    }
    pthread_mutex_lock(&pd.mutex);
    char dlc_game[16], dlc_label[24];
    if (gameupdates_split_dlc(pd.title_id, dlc_game, sizeof dlc_game, dlc_label, sizeof dlc_label))
        snprintf(job.final_name, sizeof job.final_name, "%s-dlc-%s.pkg", dlc_game, dlc_label);
    else
        snprintf(job.final_name, sizeof job.final_name, "%s-v%s.pkg", pd.title_id, pd.version);
    pthread_mutex_unlock(&pd.mutex);
    snprintf(job.part, sizeof job.part, ".%s.part", job.final_name);
    snprintf(job.state, sizeof job.state, ".%s.state.json", job.final_name);
    const char *outcome = "";
    struct stat existing;
    bool have_final = !fstatat(job.dir, job.final_name, &existing, AT_SYMLINK_NOFOLLOW) &&
                      S_ISREG(existing.st_mode) && existing.st_size == job.manifest.total;
    if (!have_final) {
        job.fd = openat(job.dir, job.part, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
        struct stat part;
        if (job.fd < 0 || fstat(job.fd, &part)) {
            patchmanifest_free(&job.manifest);
            close(job.dir);
            return finish("failed", "The download file could not be created.");
        }
        job.written = calloc(job.manifest.count, sizeof *job.written);
        job.done = calloc(job.manifest.count, sizeof *job.done);
        if (!job.written || !job.done) {
            outcome = "Not enough memory.";
        } else {
            const double truncate_began = wall_now();
            if (part.st_size != job.manifest.total && ftruncate(job.fd, (off_t)job.manifest.total) != 0)
                outcome = "The drive is full or was disconnected.";
            else
                load_state(&job, part.st_size == job.manifest.total ? job.manifest.total : -1);
            start_log("reserving the file on the drive", truncate_began);
        }
        start_log("everything before the first byte", run_began);
        phase("downloading", "");
        unsigned *failures = calloc(job.manifest.count, sizeof *failures);
        Check check = {0};
        size_t i = 0;
        if (!failures)
            outcome = "Not enough memory.";
        while (!*outcome) {
            if (i < job.manifest.count && job.done[i]) {
                i++;
                continue;
            }
            if (i < job.manifest.count) {
                outcome = fetch_piece(&job, i, detail, sizeof detail);
                if (*outcome)
                    break;
            } else if (check.running) {
                phase("verifying", ""); /* only the last piece is left to check */
            }
            size_t redo = (size_t)-1;
            if (check.running) {
                outcome = settle(&job, &check, failures, &redo, detail, sizeof detail);
                if (*outcome)
                    break;
            }
            if (redo != (size_t)-1) {
                i = redo;
                continue;
            }
            if (i >= job.manifest.count)
                break;
            check_start(&check, &job, i); /* checked while the next piece downloads */
            i++;
        }
        if (check.running) {
            if (check.thread)
                pthread_join(check.thread, NULL);
            check.running = false;
        }
        free(failures);
        if (!*outcome)
            phase("downloading", "");
        sony_range_close();
        if (!*outcome && fsync(job.fd) != 0)
            outcome = "The drive could not save the file.";
        if (!strcmp(outcome, "cancelled"))
            save_state(&job);
        close(job.fd);
        if (!strcmp(outcome, "cancelled")) {
            if (!strcmp(stop_phase(), "paused")) {
                /* Kept as it is: the part file and its state wait for the person to resume. */
                pthread_mutex_lock(&pd.mutex);
                kept.present = true;
                kept.drive = drive;
                copy_text(kept.part, sizeof kept.part, job.part);
                copy_text(kept.state, sizeof kept.state, job.state);
                pthread_mutex_unlock(&pd.mutex);
            } else if (atomic_load(&pd.cancel) && !orbit_is_stopping()) {
                /* Cancelled on purpose: what was downloaded goes too. */
                unlinkat(job.dir, job.part, 0);
                unlinkat(job.dir, job.state, 0);
            }
        }
        if (!*outcome && renameat(job.dir, job.part, job.dir, job.final_name) != 0)
            outcome = "The finished file could not be saved.";
        if (!*outcome)
            unlinkat(job.dir, job.state, 0);
    }
    free(job.written);
    free(job.done);
    patchmanifest_free(&job.manifest);
    close(job.dir);
    if (*outcome) {
        if (!strcmp(outcome, "cancelled"))
            return finish(stop_phase(), "");
        return finish_d("failed", outcome, detail);
    }
    pthread_mutex_lock(&pd.mutex);
    pd.done = pd.total;
    pthread_mutex_unlock(&pd.mutex);

    char path[900];
    snprintf(path, sizeof path, "%s/homebrew/%s", drive.root, job.final_name);
    pthread_mutex_lock(&pd.mutex);
    kept.pkg = true;
    kept.drive = drive;
    copy_text(kept.pkg_name, sizeof kept.pkg_name, job.final_name);
    pthread_mutex_unlock(&pd.mutex);
    if (!install_service_available()) {
        return finish_d("downloaded", "Downloaded. Install the file from the console's Game Library.", job.final_name);
    }
    if (!install_has_space(job.manifest.total)) {
        return finish("failed", "Downloaded, but the console has too little free space to install it.");
    }
    phase("installing", "");
    atomic_store(&pd.pause, false); /* a pause asked as the download ended does not stop the install */
    char name[96], error[200] = "";
    pthread_mutex_lock(&pd.mutex);
    copy_text(name, sizeof name, pd.name);
    pthread_mutex_unlock(&pd.mutex);
    int code = install_package_blocking(path, name, installed_progress, cur, stopped, error, sizeof error);
    if (code != 0)
        return finish(stopped() ? "cancelled" : "failed", error);
    /* The console takes over from here and may report "playable" before the update is in. It is
     * installed only once the game reports the new version; until then the job stays installing. */
    char title_id[32], target[24];
    pthread_mutex_lock(&pd.mutex);
    copy_text(title_id, sizeof title_id, pd.title_id);
    copy_text(target, sizeof target, pd.version);
    pthread_mutex_unlock(&pd.mutex);
    pthread_mutex_lock(&pd.mutex);
    pd.waiting = true; /* no real progress is known from here: the console is finishing */
    pthread_mutex_unlock(&pd.mutex);
    bool arrived = false;
    for (int waited = 0; waited < INSTALL_VERIFY_SECONDS && !arrived; waited += INSTALL_VERIFY_STEP) {
        char now[16], game[16], label[24];
        if (gameupdates_split_dlc(title_id, game, sizeof game, label, sizeof label)) {
            /* A DLC is in once the console has made its folder. */
            if (gameupdates_dlc_installed(game, label)) {
                arrived = true;
                break;
            }
        } else if (gameupdates_current_version(title_id, now, sizeof now) && strcmp(now, target) >= 0) {
            arrived = true;
            break;
        }
        if (stopped() || orbit_is_stopping())
            return finish("cancelled", "");
        sleep(INSTALL_VERIFY_STEP);
    }
    if (!arrived)
        return finish("failed", "The console has not finished installing the update yet. Check the Notifications on the console, then check for updates again.");
    pthread_mutex_lock(&orbit.mutex);
    bool remove_it = orbit.delete_pkgs;
    pthread_mutex_unlock(&orbit.mutex);
    if (remove_it)
        unlink(path);
    finish("installed", "");
    gameupdates_request_refresh();
}

void patchdl_queue_pump(void);
typedef struct {
    Slot *slot;
    char *url;
} Start;
static void *worker(void *arg) {
    Start *start = arg;
    cur = start->slot;
    run(start->url);
    free(start->url);
    free(start);
    patchdl_queue_pump(); /* a finished, failed or cancelled update gives way to the next one */
    return NULL;
}

int patchdl_start(const char *title_id, char *error, size_t cap) {
    cur = slot_for(title_id);
    if (orbit.desktop
#ifdef ORBIT_TEST
        && !getenv("ORBIT_TEST_GAMEUPDATES_BASE")
#endif
    ) {
        copy_text(error, cap, "Game updates can only be downloaded on the console.");
        return 409;
    }
    char name[96], version[24], manifest[768];
    if (!gameupdates_offer(title_id, name, sizeof name, version, sizeof version, manifest, sizeof manifest) ||
        (strncmp(manifest, "nas:", 4) && !patchfeed_url_allowed(manifest))) {
        copy_text(error, cap, "This game has no update this console can install.");
        return 404;
    }
    pthread_mutex_lock(&pd.mutex);
    if (pd.active) {
        pthread_mutex_unlock(&pd.mutex);
        copy_text(error, cap, "Another game update is already in progress.");
        return 409;
    }
    if (!strcmp(pd.phase, "paused") && kept.present && strcmp(pd.title_id, title_id)) {
        pthread_mutex_unlock(&pd.mutex);
        copy_text(error, cap, "Another game update is paused. Resume it or cancel it first.");
        return 409;
    }
    history_add(cur, kept.pkg, &kept.drive, kept.pkg_name); /* what this slot finished before moves to the history */
    history_take(title_id, NULL); /* starting this one again replaces its old line */
    char *url = strdup(manifest);
    Start *start = url ? malloc(sizeof *start) : NULL;
    if (start)
        *start = (Start){.slot = cur, .url = url};
    if (!start) {
        free(url);
        pthread_mutex_unlock(&pd.mutex);
        copy_text(error, cap, "Not enough memory.");
        return 503;
    }
    atomic_store(&pd.cancel, false);
    atomic_store(&pd.pause, false);
    kept.present = false;
    kept.pkg = false;
    pd.active = true;
    copy_text(pd.title_id, sizeof pd.title_id, title_id);
    copy_text(pd.name, sizeof pd.name, name);
    copy_text(pd.version, sizeof pd.version, version);
    copy_text(pd.phase, sizeof pd.phase, "preparing");
    pd.error[0] = 0;
    pd.detail[0] = 0;
    pd.done = pd.total = 0;
    pd.speed = 0;
    pd.piece = pd.pieces = pd.install_percent = 0;
    pd.waiting = false;
    pd.started = true;
    pthread_attr_t attr;
    pthread_t thread;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 512 * 1024);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int failed = pthread_create(&thread, &attr, worker, start);
    pthread_attr_destroy(&attr);
    if (failed) {
        pd.active = false;
        pd.started = false;
        free(url);
        free(start);
        pthread_mutex_unlock(&pd.mutex);
        copy_text(error, cap, "Orbit could not start the download.");
        return 503;
    }
    pthread_mutex_unlock(&pd.mutex);
    return 202;
}

/* Pause: stops the transfer and keeps everything, so "download" resumes where it stopped. */
int patchdl_pause(const char *title_id, char *error, size_t cap) {
    cur = slot_for(title_id);
    pthread_mutex_lock(&pd.mutex);
    bool can = pd.active && (!strcmp(pd.phase, "preparing") || !strcmp(pd.phase, "downloading") ||
                             !strcmp(pd.phase, "verifying"));
    if (can)
        atomic_store(&pd.pause, true);
    pthread_mutex_unlock(&pd.mutex);
    if (!can) {
        copy_text(error, cap, "There is no download to pause.");
        return 409;
    }
    return 202;
}

/* Cancel: stops a running update, or discards a paused one together with what it had downloaded. */
void patchdl_dismiss(const char *title_id) {
    Slot *slot = slot_for(title_id);
    pthread_mutex_lock(&slot->mutex);
    const bool mine = strcmp(slot->title_id, title_id) == 0;
    if (mine && !slot->active && finished_phase(slot->phase)) {
        slot->phase[0] = 0;
        slot->error[0] = 0;
        slot->detail[0] = 0;
        slot->done = 0;
        slot->waiting = false;
    }
    pthread_mutex_unlock(&slot->mutex);
    if (!mine)
        history_take(title_id, NULL); /* an older finished line */
}

void patchdl_cancel(const char *title_id) {
    cur = slot_for(title_id);
    pthread_mutex_lock(&pd.mutex);
    if (strcmp(pd.title_id, title_id) != 0) {
        /* Not what this slot holds now: an older finished line. Its package goes with it. */
        pthread_mutex_unlock(&pd.mutex);
        Finished f;
        if (history_take(title_id, &f) && f.pkg)
            remove_package(&f.drive, f.pkg_name);
        return;
    }
    if (pd.active) {
        atomic_store(&pd.cancel, true);
        pthread_mutex_unlock(&pd.mutex);
        return;
    }
    bool paused = !strcmp(pd.phase, "paused");
    if (!paused && kept.pkg && (!strcmp(pd.phase, "failed") || !strcmp(pd.phase, "downloaded"))) {
        /* The install failed (or never ran): cancelling removes the package and the row. */
        Job *place = calloc(1, sizeof *place);
        char name[64];
        copy_text(name, sizeof name, kept.pkg_name);
        if (place) {
            copy_text(place->storage_id, sizeof place->storage_id, kept.drive.id);
            copy_text(place->root, sizeof place->root, kept.drive.root);
            place->device = kept.drive.device;
            place->inode = kept.drive.inode;
            int dir = storage_open(place);
            free(place);
            if (dir >= 0) {
                unlinkat(dir, name, 0);
                close(dir);
            }
        }
        kept.pkg = false;
        copy_text(pd.phase, sizeof pd.phase, "cancelled");
        pd.done = 0;
        pthread_mutex_unlock(&pd.mutex);
        return;
    }
    bool had_files = paused && kept.present;
    Storage drive = kept.drive;
    char part[96], state[96];
    copy_text(part, sizeof part, kept.part);
    copy_text(state, sizeof state, kept.state);
    if (paused)
        kept.present = false;
    pthread_mutex_unlock(&pd.mutex);
    if (!paused)
        return;
    if (had_files) {
        Job *place = calloc(1, sizeof *place);
        if (place) {
            copy_text(place->storage_id, sizeof place->storage_id, drive.id);
            copy_text(place->root, sizeof place->root, drive.root);
            place->device = drive.device;
            place->inode = drive.inode;
            int dir = storage_open(place);
            free(place);
            if (dir >= 0) {
                unlinkat(dir, part, 0);
                unlinkat(dir, state, 0);
                close(dir);
            }
        }
    }
    pthread_mutex_lock(&pd.mutex);
    if (!pd.active && !strcmp(pd.phase, "paused")) {
        copy_text(pd.phase, sizeof pd.phase, "cancelled");
        pd.done = 0;
    }
    pthread_mutex_unlock(&pd.mutex);
    patchdl_queue_pump(); /* the queue was waiting behind the paused update */
}

/* ---- the queue ---- */
/* Caller holds queue.mutex. */
static void queue_save_locked(void) {
    if (orbit.state_fd < 0)
        return;
    cJSON *o = cJSON_CreateObject(), *a = cJSON_AddArrayToObject(o, "queue");
    for (size_t i = 0; i < queue.count; i++)
        cJSON_AddItemToArray(a, cJSON_CreateString(queue.ids[i]));
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!s)
        return;
    int fd = openat(orbit.state_fd, QUEUE_FILE ".tmp", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    bool ok = false;
    if (fd >= 0) {
        size_t n = strlen(s), p = 0;
        while (p < n) {
            ssize_t w = write(fd, s + p, n - p);
            if (w <= 0)
                break;
            p += (size_t)w;
        }
        ok = p == n && fsync(fd) == 0;
        if (close(fd) != 0)
            ok = false;
    }
    free(s);
    if (ok)
        renameat(orbit.state_fd, QUEUE_FILE ".tmp", orbit.state_fd, QUEUE_FILE);
}

void patchdl_queue_load(void) {
    if (orbit.state_fd < 0)
        return;
    int fd = openat(orbit.state_fd, QUEUE_FILE, O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return;
    char buffer[8192];
    ssize_t n = read(fd, buffer, sizeof buffer - 1);
    close(fd);
    if (n <= 0)
        return;
    cJSON *o = cJSON_ParseWithLength(buffer, (size_t)n);
    const cJSON *a = o ? cJSON_GetObjectItemCaseSensitive(o, "queue") : NULL;
    pthread_mutex_lock(&queue.mutex);
    queue.count = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, a) {
        if (queue.count < QUEUE_MAX && cJSON_IsString(item) && item->valuestring[0] &&
            strlen(item->valuestring) < sizeof queue.ids[0])
            copy_text(queue.ids[queue.count++], sizeof queue.ids[0], item->valuestring);
    }
    pthread_mutex_unlock(&queue.mutex);
    cJSON_Delete(o);
}

/* Starts the waiting updates and DLC whose slot is free: nothing running and nothing paused there. */
void patchdl_queue_pump(void) {
    for (;;) {
        if (orbit_is_stopping())
            return;
        char id[32];
        size_t at = (size_t)-1;
        pthread_mutex_lock(&queue.mutex);
        for (size_t i = 0; i < queue.count && at == (size_t)-1; i++) {
            Slot *slot = slot_for(queue.ids[i]);
            pthread_mutex_lock(&slot->mutex);
            bool busy = slot->active || !strcmp(slot->phase, "paused");
            pthread_mutex_unlock(&slot->mutex);
            if (!busy)
                at = i;
        }
        if (at == (size_t)-1) {
            pthread_mutex_unlock(&queue.mutex);
            return;
        }
        copy_text(id, sizeof id, queue.ids[at]);
        memmove(queue.ids[at], queue.ids[at + 1], (queue.count - at - 1) * sizeof queue.ids[0]);
        queue.count--;
        queue_save_locked();
        pthread_mutex_unlock(&queue.mutex);
        char error[200];
        int code = patchdl_start(id, error, sizeof error);
        if (code == 202)
            continue; /* its slot is taken now; the other one may be free */
        if (code == 409) { /* something else got in first: keep its place */
            pthread_mutex_lock(&queue.mutex);
            if (queue.count < QUEUE_MAX) {
                if (at > queue.count)
                    at = queue.count;
                memmove(queue.ids[at + 1], queue.ids[at], (queue.count - at) * sizeof queue.ids[0]);
                copy_text(queue.ids[at], sizeof queue.ids[0], id);
                queue.count++;
                queue_save_locked();
            }
            pthread_mutex_unlock(&queue.mutex);
            return;
        }
        /* No update to install any more (already done, or no longer offered): next one. */
    }
}

/* Starts the update when the line is free, otherwise puts it at the end of the queue. */
int patchdl_enqueue(const char *title_id, char *error, size_t cap) {
    char name[96], version[24], manifest[768];
    if (!gameupdates_offer(title_id, name, sizeof name, version, sizeof version, manifest, sizeof manifest)) {
        copy_text(error, cap, "This game has no update this console can install.");
        return 404;
    }
    cur = slot_for(title_id);
    pthread_mutex_lock(&pd.mutex);
    bool busy = pd.active || !strcmp(pd.phase, "paused");
    bool same = busy && !strcmp(pd.title_id, title_id);
    pthread_mutex_unlock(&pd.mutex);
    if (same) {
        copy_text(error, cap, "This update is already in progress.");
        return 409;
    }
    pthread_mutex_lock(&queue.mutex);
    for (size_t i = 0; i < queue.count; i++) {
        if (!strcmp(queue.ids[i], title_id)) {
            pthread_mutex_unlock(&queue.mutex);
            return 200; /* already waiting */
        }
    }
    size_t ahead = 0; /* waiting for this same slot */
    for (size_t i = 0; i < queue.count; i++)
        ahead += slot_for(queue.ids[i]) == cur;
    if (!busy && !ahead) {
        pthread_mutex_unlock(&queue.mutex);
        return patchdl_start(title_id, error, cap);
    }
    if (queue.count >= QUEUE_MAX) {
        pthread_mutex_unlock(&queue.mutex);
        copy_text(error, cap, "The update queue is full.");
        return 409;
    }
    copy_text(queue.ids[queue.count++], sizeof queue.ids[0], title_id);
    queue_save_locked();
    pthread_mutex_unlock(&queue.mutex);
    patchdl_queue_pump();
    return 202;
}

void patchdl_dequeue(const char *title_id) {
    pthread_mutex_lock(&queue.mutex);
    for (size_t i = 0; i < queue.count; i++) {
        if (strcmp(queue.ids[i], title_id))
            continue;
        memmove(queue.ids[i], queue.ids[i + 1], (queue.count - i - 1) * sizeof queue.ids[0]);
        queue.count--;
        queue_save_locked();
        break;
    }
    pthread_mutex_unlock(&queue.mutex);
}

void patchdl_queue_clear(void) {
    pthread_mutex_lock(&queue.mutex);
    queue.count = 0;
    queue_save_locked();
    pthread_mutex_unlock(&queue.mutex);
}

cJSON *patchdl_queue_json(void) {
    cJSON *a = cJSON_CreateArray();
    pthread_mutex_lock(&queue.mutex);
    for (size_t i = 0; i < queue.count; i++)
        cJSON_AddItemToArray(a, cJSON_CreateString(queue.ids[i]));
    pthread_mutex_unlock(&queue.mutex);
    return a;
}

void patchdl_stop(void) {
    for (int k = 0; k < 2; k++)
        atomic_store(&slots[k].cancel, true);
    for (int i = 0; i < 50; i++) {
        bool active = false;
        for (int k = 0; k < 2; k++) {
            pthread_mutex_lock(&slots[k].mutex);
            active = active || slots[k].active;
            pthread_mutex_unlock(&slots[k].mutex);
        }
        if (!active)
            return;
        nanosleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 100000000}, NULL);
    }
}

/* The update download (dlc = false) or the DLC download (dlc = true), NULL before the first one. */
cJSON *patchdl_history_json(void) {
    cJSON *list = cJSON_CreateArray();
    if (!list)
        return NULL;
    pthread_mutex_lock(&history.mutex);
    for (size_t i = history.count; i > 0; i--) { /* newest first */
        const Finished *f = &history.items[i - 1];
        cJSON *o = cJSON_CreateObject();
        if (!o)
            break;
        cJSON_AddStringToObject(o, "titleId", f->title_id);
        cJSON_AddStringToObject(o, "name", f->name);
        cJSON_AddStringToObject(o, "version", f->version);
        cJSON_AddStringToObject(o, "phase", f->phase);
        cJSON_AddNumberToObject(o, "total", (double)f->total);
        cJSON_AddStringToObject(o, "error", f->error);
        cJSON_AddStringToObject(o, "detail", f->detail);
        cJSON_AddItemToArray(list, o);
    }
    pthread_mutex_unlock(&history.mutex);
    return list;
}

cJSON *patchdl_status(bool dlc) {
    cur = &slots[dlc ? 1 : 0];
    pthread_mutex_lock(&pd.mutex);
    if (!pd.started) {
        pthread_mutex_unlock(&pd.mutex);
        return NULL;
    }
    cJSON *o = cJSON_CreateObject();
    if (o) {
        cJSON_AddStringToObject(o, "titleId", pd.title_id);
        cJSON_AddStringToObject(o, "name", pd.name);
        cJSON_AddStringToObject(o, "version", pd.version);
        cJSON_AddStringToObject(o, "phase", pd.phase);
        cJSON_AddBoolToObject(o, "active", pd.active);
        cJSON_AddNumberToObject(o, "done", (double)pd.done);
        cJSON_AddNumberToObject(o, "total", (double)pd.total);
        cJSON_AddNumberToObject(o, "speed", pd.speed);
        cJSON_AddNumberToObject(o, "piece", pd.piece);
        cJSON_AddNumberToObject(o, "pieces", pd.pieces);
        cJSON_AddNumberToObject(o, "installPercent", pd.install_percent);
        cJSON_AddBoolToObject(o, "waiting", pd.waiting);
        cJSON_AddStringToObject(o, "error", pd.error);
        cJSON_AddStringToObject(o, "detail", pd.detail);
    }
    pthread_mutex_unlock(&pd.mutex);
    return o;
}
