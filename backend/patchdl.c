/* Downloads a game update from Sony and installs it. One at a time.
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

static struct {
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
} pd = {.mutex = PTHREAD_MUTEX_INITIALIZER};

/* The updates waiting for their turn, in order. Kept in game-update-queue.json so a restart
 * keeps it; each one is checked against the current offers when its turn comes. */
#define QUEUE_MAX 32
#define QUEUE_FILE "game-update-queue.json"
static struct {
    pthread_mutex_t mutex;
    char ids[QUEUE_MAX][32];
    size_t count;
} queue = {.mutex = PTHREAD_MUTEX_INITIALIZER};

/* What a paused download leaves on its drive, so cancelling it later can delete it. */
static struct {
    bool present;
    Storage drive;
    char part[96], state[96];
} kept;

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
#define PARALLEL_STREAMS 4
#define SLICE_BYTES (4LL * 1024 * 1024)
#define SLICE_RETRIES 3

static atomic_bool fan_failed;
static bool fan_stop(void) {
    return stopped() || atomic_load(&fan_failed);
}

typedef struct {
    PatchJob *job;
    const patchmanifest_piece *piece;
    int64_t base;                   /* where the first slice starts inside the piece */
    size_t slices;
    volatile int64_t *reached;      /* per slice: the position reached inside the piece */
    atomic_bool *finished;          /* per slice */
    atomic_size_t next;
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
    atomic_store(&fan_failed, true);
}

static void *fan_worker(void *context) {
    Fan *fan = context;
    PatchJob *job = fan->job;
    char why[120] = "";
    for (;;) {
        size_t k = atomic_fetch_add(&fan->next, 1);
        if (k >= fan->slices || fan_stop())
            break;
        unsigned tries = 0;
        for (;;) {
            volatile int64_t *position = &fan->reached[k];
            sony_range_result r =
                job->nas ? nas_range(job->nas_url, *position, slice_end(fan, k), job->fd, fan->piece->offset,
                                     position, fan_stop, why, sizeof why)
                         : sony_range(fan->piece->url, *position, slice_end(fan, k), job->fd, fan->piece->offset,
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

static sony_range_result download_piece(PatchJob *job, size_t i, Speed *speed, char *why, size_t why_cap) {
    const patchmanifest_piece *piece = &job->manifest.pieces[i];
    if (job->written[i] >= piece->size)
        return SONY_RANGE_OK;
    Fan fan = {.job = job, .piece = piece, .base = job->written[i], .result = SONY_RANGE_OK};
    fan.slices = (size_t)((piece->size - fan.base + SLICE_BYTES - 1) / SLICE_BYTES);
    fan.reached = calloc(fan.slices, sizeof *fan.reached);
    fan.finished = calloc(fan.slices, sizeof *fan.finished);
    if (!fan.reached || !fan.finished) {
        free((void *)fan.reached);
        free(fan.finished);
        snprintf(why, why_cap, "not enough memory");
        return SONY_RANGE_FAILED;
    }
    for (size_t k = 0; k < fan.slices; k++)
        fan.reached[k] = slice_start(&fan, k);
    atomic_store(&fan.next, 0);
    atomic_store(&fan_failed, false);
    pthread_mutex_init(&fan.mutex, NULL);

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
            int64_t reached = atomic_load(&fan.finished[k]) ? slice_end(&fan, k) : fan.reached[k];
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
        if (now - job->last_save >= SAVE_EVERY)
            save_state(job);
    }
    for (size_t t = 0; t < started; t++)
        pthread_join(threads[t], NULL);

    int64_t prefix = fan.base;
    bool gap = false;
    for (size_t k = 0; k < fan.slices && !gap; k++) {
        int64_t reached = atomic_load(&fan.finished[k]) ? slice_end(&fan, k) : fan.reached[k];
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
    free((void *)fan.reached);
    free(fan.finished);
    atomic_store(&fan_failed, false);
    if (r == SONY_RANGE_OK) {
        publish(job, i, piece->size, speed->speed);
        save_state(job);
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
    (void)context;
    pthread_mutex_lock(&pd.mutex);
    pd.install_percent = percent;
    pthread_mutex_unlock(&pd.mutex);
}

/* The drive that receives the file: the person's default one if it has room, else any with room.
 * The console's own drive also receives the installed copy, so it needs room for both. */
static bool choose_drive(int64_t total, Storage *chosen) {
    Storage drives[ORBIT_MAX_STORAGE];
    size_t count = storage_list(drives);
    char preferred[24];
    pthread_mutex_lock(&orbit.mutex);
    copy_text(preferred, sizeof preferred, orbit.preferred_storage);
    pthread_mutex_unlock(&orbit.mutex);
    for (int pass = 0; pass < 2; pass++)
        for (size_t i = 0; i < count; i++) {
            uint64_t need = (uint64_t)total + (256ULL << 20);
            if (!strcmp(drives[i].id, "internal") || !strcmp(drives[i].id, "desktop"))
                need = 2 * (uint64_t)total + (1ULL << 30);
            if (drives[i].free_bytes < need || (pass == 0 && strcmp(drives[i].id, preferred)))
                continue;
            *chosen = drives[i];
            return true;
        }
    return false;
}

static void run(const char *manifest_url) {
    char why[160] = "", detail[200] = "";
    phase("preparing", "");
    PatchJob job;
    memset(&job, 0, sizeof job);
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
        if (!sony_get(manifest_url, MANIFEST_MAX, &body, stopped, why, sizeof why)) {
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
    if (!choose_drive(job.manifest.total, &drive)) {
        patchmanifest_free(&job.manifest);
        return finish("failed", "Not enough free space on any drive for this update.");
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
            if (part.st_size != job.manifest.total && ftruncate(job.fd, (off_t)job.manifest.total) != 0)
                outcome = "The drive is full or was disconnected.";
            else
                load_state(&job, part.st_size == job.manifest.total ? job.manifest.total : -1);
        }
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
    int code = install_package_blocking(path, name, installed_progress, NULL, stopped, error, sizeof error);
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
static void *worker(void *arg) {
    char *url = arg;
    run(url);
    free(url);
    patchdl_queue_pump(); /* a finished, failed or cancelled update gives way to the next one */
    return NULL;
}

int patchdl_start(const char *title_id, char *error, size_t cap) {
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
    char *url = strdup(manifest);
    if (!url) {
        pthread_mutex_unlock(&pd.mutex);
        copy_text(error, cap, "Not enough memory.");
        return 503;
    }
    atomic_store(&pd.cancel, false);
    atomic_store(&pd.pause, false);
    kept.present = false;
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
    int failed = pthread_create(&thread, &attr, worker, url);
    pthread_attr_destroy(&attr);
    if (failed) {
        pd.active = false;
        pd.started = false;
        free(url);
        pthread_mutex_unlock(&pd.mutex);
        copy_text(error, cap, "Orbit could not start the download.");
        return 503;
    }
    pthread_mutex_unlock(&pd.mutex);
    return 202;
}

/* Pause: stops the transfer and keeps everything, so "download" resumes where it stopped. */
int patchdl_pause(char *error, size_t cap) {
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
void patchdl_cancel(void) {
    pthread_mutex_lock(&pd.mutex);
    if (pd.active) {
        atomic_store(&pd.cancel, true);
        pthread_mutex_unlock(&pd.mutex);
        return;
    }
    bool paused = !strcmp(pd.phase, "paused");
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
    char buffer[2048];
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

/* Starts the next waiting update when nothing is running and nothing is paused. */
void patchdl_queue_pump(void) {
    for (;;) {
        if (orbit_is_stopping())
            return;
        pthread_mutex_lock(&pd.mutex);
        bool busy = pd.active || !strcmp(pd.phase, "paused");
        pthread_mutex_unlock(&pd.mutex);
        if (busy)
            return;
        char id[32];
        pthread_mutex_lock(&queue.mutex);
        if (!queue.count) {
            pthread_mutex_unlock(&queue.mutex);
            return;
        }
        copy_text(id, sizeof id, queue.ids[0]);
        memmove(queue.ids[0], queue.ids[1], (queue.count - 1) * sizeof queue.ids[0]);
        queue.count--;
        queue_save_locked();
        pthread_mutex_unlock(&queue.mutex);
        char error[200];
        int code = patchdl_start(id, error, sizeof error);
        if (code == 202)
            return;
        if (code == 409) { /* something else got in first: keep its place at the front */
            pthread_mutex_lock(&queue.mutex);
            if (queue.count < QUEUE_MAX) {
                memmove(queue.ids[1], queue.ids[0], queue.count * sizeof queue.ids[0]);
                copy_text(queue.ids[0], sizeof queue.ids[0], id);
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
    if (!busy && !queue.count) {
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
    atomic_store(&pd.cancel, true);
    for (int i = 0; i < 50; i++) {
        pthread_mutex_lock(&pd.mutex);
        bool active = pd.active;
        pthread_mutex_unlock(&pd.mutex);
        if (!active)
            return;
        nanosleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 100000000}, NULL);
    }
}

cJSON *patchdl_status(void) {
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
