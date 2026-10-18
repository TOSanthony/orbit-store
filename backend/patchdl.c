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
    atomic_bool cancel;
    char title_id[16], name[96], version[16];
    char phase[16], error[200], detail[200];
    int64_t done, total;
    double speed;
    unsigned piece, pieces, install_percent;
    bool started;
} pd = {.mutex = PTHREAD_MUTEX_INITIALIZER};

static bool stopped(void) {
    return atomic_load(&pd.cancel) || orbit_is_stopping();
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

/* Downloads what is missing of piece i in slices, so progress and the stop flag move steadily. */
static sony_range_result download_piece(PatchJob *job, size_t i, Speed *speed, char *why, size_t why_cap) {
    const patchmanifest_piece *piece = &job->manifest.pieces[i];
    sony_range_result r = SONY_RANGE_OK;
    while (job->written[i] < piece->size) {
        int64_t start = job->written[i];
        int64_t end = start + 8LL * 1024 * 1024 < piece->size ? start + 8LL * 1024 * 1024 : piece->size;
        volatile int64_t position = start;
        r = job->nas ? nas_range(job->nas_url, start, end, job->fd, piece->offset, &position, stopped, why, why_cap)
                     : sony_range(piece->url, start, end, job->fd, piece->offset, &position, stopped, why, why_cap);
        job->written[i] = r == SONY_RANGE_IGNORED ? 0 : position;
        if (r != SONY_RANGE_OK)
            return r;
        time_t now = time(NULL);
        if (now - speed->mark_time >= 3) {
            speed->speed = (double)(job->written[i] - speed->mark_bytes) / (double)(now - speed->mark_time);
            speed->mark_time = now;
            speed->mark_bytes = job->written[i];
        }
        publish(job, i, job->written[i], speed->speed);
        if (now - job->last_save >= SAVE_EVERY)
            save_state(job);
    }
    return SONY_RANGE_OK;
}

/* One piece with retries. Returns "" on success, "cancelled" when stopped, or a reason. */
static const char *fetch_piece(PatchJob *job, size_t i, char *detail, size_t detail_cap) {
    const patchmanifest_piece *piece = &job->manifest.pieces[i];
    unsigned network_failures = 0, hash_failures = 0;
    Speed speed = {.mark_time = time(NULL), .mark_bytes = job->written[i]};
    while (!job->done[i]) {
        if (stopped())
            return "cancelled";
        if (job->written[i] < piece->size) {
            char why[120] = "";
            sony_range_result r = download_piece(job, i, &speed, why, sizeof why);
            if (r != SONY_RANGE_OK) {
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
                continue;
            }
        }
        phase("verifying", "");
        bool ok = digest_matches(job, piece);
        if (stopped())
            return "cancelled";
        phase("downloading", "");
        if (ok) {
            job->done[i] = true;
            save_state(job);
            publish(job, i, piece->size, speed.speed);
            return "";
        }
        if (++hash_failures >= 2) {
            snprintf(detail, detail_cap, "piece %zu", i + 1);
            return "A downloaded piece failed its checksum twice.";
        }
        job->written[i] = 0;
        save_state(job);
    }
    return "";
}

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
                return finish("cancelled", "");
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
        for (size_t i = 0; !*outcome && i < job.manifest.count; i++)
            outcome = fetch_piece(&job, i, detail, sizeof detail);
        if (!*outcome && fsync(job.fd) != 0)
            outcome = "The drive could not save the file.";
        if (!strcmp(outcome, "cancelled"))
            save_state(&job);
        close(job.fd);
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
            return finish("cancelled", "");
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
    char name[96], error[200] = "";
    pthread_mutex_lock(&pd.mutex);
    copy_text(name, sizeof name, pd.name);
    pthread_mutex_unlock(&pd.mutex);
    int code = install_package_blocking(path, name, installed_progress, NULL, stopped, error, sizeof error);
    if (code != 0)
        return finish(stopped() ? "cancelled" : "failed", error);
    pthread_mutex_lock(&orbit.mutex);
    bool remove_it = orbit.delete_pkgs;
    pthread_mutex_unlock(&orbit.mutex);
    if (remove_it)
        unlink(path);
    finish("installed", "");
    gameupdates_request_refresh();
}

static void *worker(void *arg) {
    char *url = arg;
    run(url);
    free(url);
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
    char name[96], version[16], manifest[768];
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
    char *url = strdup(manifest);
    if (!url) {
        pthread_mutex_unlock(&pd.mutex);
        copy_text(error, cap, "Not enough memory.");
        return 503;
    }
    atomic_store(&pd.cancel, false);
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

void patchdl_cancel(void) {
    atomic_store(&pd.cancel, true);
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
        cJSON_AddStringToObject(o, "error", pd.error);
        cJSON_AddStringToObject(o, "detail", pd.detail);
    }
    pthread_mutex_unlock(&pd.mutex);
    return o;
}
