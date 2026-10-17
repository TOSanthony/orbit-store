#include "network.h"
#include "orbit.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
Orbit orbit = {.mutex = PTHREAD_MUTEX_INITIALIZER,
               .changed = PTHREAD_COND_INITIALIZER,
               .port = ORBIT_HTTP_PORT,
               .state_fd = -1,
               .delete_archives = true,
               .concurrent_downloads = ORBIT_DEFAULT_DOWNLOADS};
void copy_text(char *to, size_t cap, const char *s) {
    if (cap)
        snprintf(to, cap, "%s", s ? s : "");
}
const char *json_text(const cJSON *o, const char *k) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? v->valuestring : "";
}
int64_t json_int(const cJSON *o, const char *k) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? (int64_t)v->valuedouble : 0;
}
static uint64_t json_identity(const cJSON *o, const char *key) {
    const char *s = json_text(o, key);
    return *s ? strtoumax(s, NULL, 10) : (uint64_t)json_int(o, key);
}
void random_hex(char *out, size_t n) {
    unsigned char b[32];
    if (n > 32 || RAND_bytes(b, (int)n) != 1) {
        fputs("Secure randomness unavailable\n", stderr);
        exit(1);
    }
    for (size_t i = 0; i < n; i++)
        snprintf(out + 2 * i, 3, "%02x", b[i]);
}
Release *release_find(const char *id) {
    for (size_t i = 0; i < orbit.release_count; i++)
        if (!strcmp(orbit.releases[i].id, id))
            return &orbit.releases[i];
    for (size_t i = 0; i < orbit.nas_count; i++)
        if (!strcmp(orbit.nas_releases[i].id, id))
            return &orbit.nas_releases[i];
    return NULL;
}
Job *job_find(const char *id) {
    for (size_t i = 0; i < orbit.job_count; i++)
        if (!strcmp(orbit.jobs[i].id, id))
            return &orbit.jobs[i];
    return NULL;
}
static cJSON *job_json(const Job *j, bool private_fields) {
    cJSON *o = cJSON_CreateObject();
#define S(k, f) cJSON_AddStringToObject(o, k, j->f)
    S("id", id);
    S("releaseId", release_id);
    S("storageId", storage_id);
    S("filename", filename);
    S("status", status);
    S("error", error);
    S("verification", verification);
    cJSON_AddBoolToObject(o, "canRefreshLink", job_can_refresh_link(j));
    cJSON_AddStringToObject(o, "delivery", j->delivery[0] ? j->delivery : "direct");
    S("deliveryPhase", delivery_phase);
    int64_t received = j->received;
    if (private_fields && j->segmented) {
        received = 0;
        for (unsigned i = 0; i < j->range_count; i++) received += j->range_received[i];
    }
    cJSON_AddNumberToObject(o, "received", (double)received);
    cJSON_AddNumberToObject(o, "total", (double)j->total);
    cJSON_AddNumberToObject(o, "speed", j->speed);
    cJSON_AddNumberToObject(o, "retryAt", (double)j->retry_at);
    cJSON_AddNumberToObject(o, "order", j->order);
    cJSON_AddStringToObject(o, "title", j->release.title);
    cJSON_AddStringToObject(o, "titleId", j->release.title_id);
    cJSON_AddStringToObject(o, "format", j->release.format);
    cJSON_AddStringToObject(o, "gameId", j->release.game_id);
    if (j->release.compression[0]) {
        cJSON_AddStringToObject(o, "compression", j->release.compression);
        cJSON_AddNumberToObject(o, "extracted", (double)j->extracted);
        cJSON_AddNumberToObject(o, "extractTotal", (double)(j->release.archive_unverified ? j->inspected_size : j->release.extracted_size));
        if (j->release.archive_unverified) {
            cJSON_AddStringToObject(o, "archiveContents", "unverified");
            cJSON_AddNumberToObject(o, "inspectedBytes", (double)j->inspected_size);
            S("detectedFormat", inspected_format);
        }
        S("output", output);
        cJSON_AddBoolToObject(o, "archiveKept", j->archive_kept);
        cJSON_AddBoolToObject(o, "canEnterArchivePassword", j->archive_ready && !job_active(j) &&
            (!strcmp(j->status, "error") || !strcmp(j->status, "paused")));
    }
    char path[700];
    snprintf(path, sizeof path, "%s/homebrew/%s", j->root, j->output[0] ? j->output : j->filename);
    cJSON_AddStringToObject(o, "path", path);
    if (private_fields) {
        if (!strcmp(j->delivery, "torbox")) {
            S("debridAccount", debrid_account);
            cJSON_AddNumberToObject(o, "debridId", (double)j->debrid_id);
            cJSON_AddNumberToObject(o, "debridFile", (double)j->debrid_file);
            cJSON_AddBoolToObject(o, "debridSubmitted", j->debrid_submitted);
        }
        if (j->segmented) {
            cJSON *ranges = cJSON_AddArrayToObject(o, "rangeReceived");
            for (unsigned i = 0; i < j->range_count; i++)
                cJSON_AddItemToArray(ranges, cJSON_CreateNumber((double)j->range_received[i]));
        }
        if (j->archive_ready) cJSON_AddBoolToObject(o, "archiveReady", true);
        if (j->archive_password[0]) { S("archivePassword", archive_password); }
        cJSON_AddItemToObject(o, "release", release_json(&j->release));
        S("root", root);
        S("etag", etag);
        S("modified", modified);
        char device[32], inode[32];
        snprintf(device, sizeof device, "%ju", (uintmax_t)j->device);
        snprintf(inode, sizeof inode, "%ju", (uintmax_t)j->inode);
        cJSON_AddStringToObject(o, "device", device);
        cJSON_AddStringToObject(o, "inode", inode);
        cJSON_AddNumberToObject(o, "attempts", j->attempts);
    }
#undef S
    return o;
}
cJSON *jobs_json_locked(void) {
    cJSON *a = cJSON_CreateArray();
    /* Sort references only. The transfer worker retains a pointer to its fixed slot. */
    const Job *ordered[ORBIT_MAX_JOBS];
    size_t count = 0;
    for (size_t i = 0; i < orbit.job_count; i++) {
        const Job *j = &orbit.jobs[i];
        if (!j->id[0])
            continue;
        size_t p = count++;
        while (p && ordered[p - 1]->order > j->order) {
            ordered[p] = ordered[p - 1];
            p--;
        }
        ordered[p] = j;
    }
    for (size_t i = 0; i < count; i++)
        cJSON_AddItemToArray(a, job_json(ordered[i], false));
    return a;
}
int state_save_locked(void) {
    cJSON *o = cJSON_CreateObject(), *a = cJSON_AddArrayToObject(o, "jobs"),
          *t = cJSON_AddArrayToObject(o, "tokens");
    /* Direct queues retain the 0.7.0 durable layout. TorBox queues must
     * reject older builds, which cannot retain the delivery/account identity. */
    unsigned schema = 4;
    for (size_t i = 0; i < orbit.job_count; i++)
        if (orbit.jobs[i].id[0] && !strcmp(orbit.jobs[i].delivery, "torbox") && schema < 5) schema = 5;
    /* Older builds would treat a compressed download as a finished plain file. */
    for (size_t i = 0; i < orbit.job_count; i++)
        if (orbit.jobs[i].id[0] && orbit.jobs[i].release.compression[0]) schema = 6;
    cJSON_AddNumberToObject(o, "schema", schema);
    cJSON_AddStringToObject(o, "preferredStorage", orbit.preferred_storage);
    cJSON_AddStringToObject(o, "appearance", orbit.appearance);
    cJSON_AddStringToObject(o, "language", orbit.language);
    cJSON_AddBoolToObject(o, "soundsOff", orbit.sounds_off);
    cJSON_AddBoolToObject(o, "deleteArchives", orbit.delete_archives);
    cJSON_AddNumberToObject(o, "concurrentDownloads", orbit.concurrent_downloads);
    cJSON_AddNumberToObject(o, "debridNextCreate", (double)orbit.debrid_next_create);
    cJSON_AddItemToObject(o, "favorites", favorites_json_locked());
    cJSON *sources = cJSON_AddObjectToObject(o, "sources");
    cJSON_AddNumberToObject(sources, "enabled", orbit.sources.enabled);
    cJSON_AddNumberToObject(sources, "noticeVersion", orbit.sources.notice_version);
    cJSON_AddNumberToObject(sources, "acknowledgedAt", (double)orbit.sources.acknowledged_at);
    cJSON_AddStringToObject(sources, "nasUrl", orbit.sources.nas_url);
    for (size_t i = 0; i < orbit.job_count; i++)
        if (orbit.jobs[i].id[0])
            cJSON_AddItemToArray(a, job_json(&orbit.jobs[i], true));
    for (size_t i = 0; i < orbit.token_count; i++)
        cJSON_AddItemToArray(t, cJSON_CreateString(orbit.tokens[i]));
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!s)
        return -1;
    int fd = openat(orbit.state_fd, "state.tmp", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600),
        rc = -1;
    if (fd >= 0) {
        size_t n = strlen(s), p = 0;
        while (p < n) {
            ssize_t w = write(fd, s + p, n - p);
            if (w <= 0)
                break;
            p += (size_t)w;
        }
        if (p == n && fsync(fd) == 0)
            rc = 0;
        if (close(fd) != 0)
            rc = -1;
    }
    free(s);
    if (rc == 0)
        rc = renameat(orbit.state_fd, "state.tmp", orbit.state_fd, "state.json");
    if (rc == 0)
        rc = fsync(orbit.state_fd);
    orbit.state_failed = rc != 0;
    return rc;
}
static int backup_legacy_state(const char *data, size_t length) {
    char nonce[17], name[64];
    random_hex(nonce, 8);
    snprintf(name, sizeof name, "state-before-buffered-%s.json", nonce);
    int fd = openat(orbit.state_fd, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    size_t written = 0;
    while (written < length) {
        ssize_t n = write(fd, data + written, length - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        written += (size_t)n;
    }
    int rc = written == length ? fsync(fd) : -1;
    if (close(fd)) rc = -1;
    if (!rc) rc = fsync(orbit.state_fd);
    if (rc) unlinkat(orbit.state_fd, name, 0);
    else orbit.queue_backup_created = true;
    return rc;
}
int state_load(void) {
    int fd = openat(orbit.state_fd, "state.json", O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return 0;
    struct stat st;
    if (fstat(fd, &st) || st.st_size < 1 || st.st_size > 1024 * 1024) {
        close(fd);
        return -1;
    }
    size_t n = (size_t)st.st_size;
    char *s = calloc(n + 1, 1);
    if (!s) {
        close(fd);
        return -1;
    }
    size_t p = 0;
    while (p < n) {
        ssize_t got = read(fd, s + p, n - p);
        if (got <= 0)
            break;
        p += (size_t)got;
    }
    close(fd);
    cJSON *o = p == n ? cJSON_Parse(s) : NULL;
    const cJSON *schema_value = cJSON_GetObjectItemCaseSensitive(o, "schema");
    if (!cJSON_IsNumber(schema_value) ||
        (schema_value->valuedouble != 1 && schema_value->valuedouble != 2 && schema_value->valuedouble != 3 &&
         schema_value->valuedouble != 4 && schema_value->valuedouble != 5 && schema_value->valuedouble != 6)) {
        free(s);
        cJSON_Delete(o);
        return -1;
    }
    int schema = (int)schema_value->valuedouble;
    /* Save the exact old queue before any buffered-schema write. No overwrite and no
     * migration when backup fails; filenames/credentials stay on the console. */
    if (schema < 4 && backup_legacy_state(s, n)) {
        free(s);
        cJSON_Delete(o);
        return -1;
    }
    free(s);
    const cJSON *x;
    const cJSON *next_create = cJSON_GetObjectItemCaseSensitive(o, "debridNextCreate");
    if (next_create) {
        if (!cJSON_IsNumber(next_create) || !(next_create->valuedouble >= 0 && next_create->valuedouble <= 4102444800.0) ||
            next_create->valuedouble != (double)(time_t)next_create->valuedouble) { cJSON_Delete(o); return -1; }
        orbit.debrid_next_create = (time_t)next_create->valuedouble;
    }
    copy_text(orbit.preferred_storage, sizeof orbit.preferred_storage,
              json_text(o, "preferredStorage"));
    copy_text(orbit.appearance, sizeof orbit.appearance, json_text(o, "appearance"));
    copy_text(orbit.language, sizeof orbit.language, json_text(o, "language"));
    const cJSON *sounds_off = cJSON_GetObjectItemCaseSensitive(o, "soundsOff");
    if (cJSON_IsBool(sounds_off)) orbit.sounds_off = cJSON_IsTrue(sounds_off);
    const cJSON *delete_archives = cJSON_GetObjectItemCaseSensitive(o, "deleteArchives");
    if (cJSON_IsBool(delete_archives)) orbit.delete_archives = cJSON_IsTrue(delete_archives);
    const cJSON *limit = cJSON_GetObjectItemCaseSensitive(o, "concurrentDownloads");
    if (cJSON_IsNumber(limit) && limit->valuedouble >= 1 &&
        limit->valuedouble <= ORBIT_MAX_DOWNLOADS && limit->valuedouble == limit->valueint)
        orbit.concurrent_downloads = (unsigned)limit->valueint;
    const cJSON *sources = cJSON_GetObjectItemCaseSensitive(o, "sources");
    if (json_int(sources, "noticeVersion") == ORBIT_SOURCE_NOTICE_VERSION &&
        json_int(sources, "acknowledgedAt") > 0 && json_int(sources, "enabled") >= 0 &&
        json_int(sources, "enabled") <= ORBIT_SOURCE_ALL) {
        orbit.sources.enabled = (unsigned)json_int(sources, "enabled");
        orbit.sources.notice_version = ORBIT_SOURCE_NOTICE_VERSION;
        orbit.sources.acknowledged_at = (time_t)json_int(sources, "acknowledgedAt");
        if (nas_url_valid(json_text(sources, "nasUrl")))
            copy_text(orbit.sources.nas_url, sizeof orbit.sources.nas_url, json_text(sources, "nasUrl"));
        nas_origin_set(orbit.sources.nas_url);
    }
    cJSON_ArrayForEach(x, cJSON_GetObjectItemCaseSensitive(o, "tokens")) {
        if (cJSON_IsString(x) && strlen(x->valuestring) == 64 && orbit.token_count < 16)
            copy_text(orbit.tokens[orbit.token_count++], 65, x->valuestring);
    }
    cJSON_ArrayForEach(x, cJSON_GetObjectItemCaseSensitive(o, "favorites")) {
        if (!cJSON_IsString(x) || !*x->valuestring || strlen(x->valuestring) >= 64 ||
            orbit.favorite_count >= ORBIT_MAX_RELEASES)
            continue;
        bool duplicate = false;
        for (size_t i = 0; i < orbit.favorite_count; i++)
            if (!strcmp(orbit.favorites[i], x->valuestring))
                duplicate = true;
        if (!duplicate)
            copy_text(orbit.favorites[orbit.favorite_count++], 64, x->valuestring);
    }
    cJSON_ArrayForEach(x, cJSON_GetObjectItemCaseSensitive(o, "jobs")) {
        Release snapshot, *r = release_find(json_text(x, "releaseId"));
        const cJSON *saved = cJSON_GetObjectItemCaseSensitive(x, "release");
        if (saved) {
            /* A bad saved identity must never silently switch to the current file. */
            if (release_parse(saved, &snapshot, true) ||
                strcmp(snapshot.id, json_text(x, "releaseId"))) {
                cJSON_Delete(o);
                return -1;
            }
            r = &snapshot;
        }
        if (!r || orbit.job_count >= ORBIT_MAX_JOBS)
            continue;
        Job *j = &orbit.jobs[orbit.job_count++];
        j->release = *r;
#define R(k, f) copy_text(j->f, sizeof j->f, json_text(x, k))
        R("id", id);
        R("releaseId", release_id);
        R("storageId", storage_id);
        R("root", root);
        R("status", status);
        R("etag", etag);
        R("modified", modified);
        R("error", error);
        R("verification", verification);
        R("output", output);

#undef R
        const cJSON *password = cJSON_GetObjectItemCaseSensitive(x, "archivePassword");
        if (password) {
            if (!r->compression[0] || !cJSON_IsString(password) ||
                !*password->valuestring || strlen(password->valuestring) >= sizeof j->archive_password) {
                cJSON_Delete(o); return -1;
            }
            for (const unsigned char *p = (const void *)password->valuestring; *p; p++)
                if (*p < 32 || *p == 127) { cJSON_Delete(o); return -1; }
            copy_text(j->archive_password, sizeof j->archive_password, password->valuestring);
        }
        if (strchr(j->output, '/') || !strcmp(j->output, "..")) { cJSON_Delete(o); return -1; }
        j->archive_ready = r->compression[0] && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(x, "archiveReady"));
        const cJSON *inspected = cJSON_GetObjectItemCaseSensitive(x, "inspectedBytes");
        if (inspected) {
            const char *format = json_text(x, "detectedFormat");
            if (!r->archive_unverified || !cJSON_IsNumber(inspected) || !isfinite(inspected->valuedouble) ||
                inspected->valuedouble < 0 || inspected->valuedouble > 9007199254740991.0 ||
                floor(inspected->valuedouble) != inspected->valuedouble ||
                (*format && strcmp(format, "PKG") && strcmp(format, "exFAT") && strcmp(format, "FFPFSC") &&
                 strcmp(format, "Game folder") && strcmp(format, "Unknown"))) {
                cJSON_Delete(o); return -1;
            }
            j->inspected_size = (int64_t)inspected->valuedouble;
            copy_text(j->inspected_format, sizeof j->inspected_format, format);
        }
        j->archive_kept = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(x, "archiveKept"));
        const char *delivery = json_text(x, "delivery");
        if (*delivery && strcmp(delivery, "direct") && strcmp(delivery, "torbox")) { cJSON_Delete(o); return -1; }
        if (!strcmp(delivery, "torbox")) {
            const cJSON *web = cJSON_GetObjectItemCaseSensitive(x, "debridId"),
                        *file = cJSON_GetObjectItemCaseSensitive(x, "debridFile");
            const char *account = json_text(x, "debridAccount");
            if ((schema != 3 && schema != 5 && schema != 6) || strlen(account) != 64 || strspn(account, "0123456789abcdef") != 64 ||
                !cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(x, "debridSubmitted")) ||
                !cJSON_IsNumber(web) || !cJSON_IsNumber(file) ||
                !(web->valuedouble >= -1 && web->valuedouble <= 9007199254740991.0) ||
                !(file->valuedouble >= -1 && file->valuedouble <= 9007199254740991.0) ||
                web->valuedouble != (double)(int64_t)web->valuedouble ||
                file->valuedouble != (double)(int64_t)file->valuedouble) { cJSON_Delete(o); return -1; }
            copy_text(j->delivery, sizeof j->delivery, "torbox");
            copy_text(j->debrid_account, sizeof j->debrid_account, json_text(x, "debridAccount"));
            j->debrid_id = (int64_t)web->valuedouble; j->debrid_file = (int64_t)file->valuedouble;
            j->debrid_submitted = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(x, "debridSubmitted"));
        }
        copy_text(j->filename, sizeof j->filename, r->filename);
        j->device = (dev_t)json_identity(x, "device");
        j->inode = (ino_t)json_identity(x, "inode");
        j->received = json_int(x, "received");
        const cJSON *ranges = cJSON_GetObjectItemCaseSensitive(x, "rangeReceived");
        if (ranges) {
            int count = cJSON_GetArraySize(ranges);
            if (!cJSON_IsArray(ranges) ||
                !(((schema == 2 || schema == 3) && count == 2) || ((schema >= 4) && (count == 1 || count == 2 || count == 4 || count == 8))) ||
                count > ORBIT_MAX_RANGES ||
                (count > 1 && (!j->etag[0] || !strncmp(j->etag, "W/", 2)))) {
                cJSON_Delete(o);
                return -1;
            }
            j->range_count = (unsigned)count;
            j->received = 0;
            for (unsigned i = 0; i < j->range_count; i++) {
                const cJSON *v = cJSON_GetArrayItem(ranges, i);
                int64_t length = range_boundary(r->size, j->range_count, i + 1) -
                                 range_boundary(r->size, j->range_count, i);
                if (!cJSON_IsNumber(v) || !(v->valuedouble >= 0 && v->valuedouble <= length) ||
                    v->valuedouble != (double)(int64_t)v->valuedouble) {
                    cJSON_Delete(o);
                    return -1;
                }
                j->range_received[i] = (int64_t)v->valuedouble;
                j->received += j->range_received[i];
            }
            j->segmented = true;
        }
        j->total = r->size;
        j->attempts = (unsigned)json_int(x, "attempts");
        int64_t order = json_int(x, "order");
        j->order = order > 0 && order < 2147483647 ? (unsigned)order : (unsigned)orbit.job_count;
        j->retry_at = (time_t)json_int(x, "retryAt");
        if (job_active(j)) {
            copy_text(j->status, sizeof j->status, "paused");
            copy_text(j->error, sizeof j->error, "Interrupted. Resume to continue.");
        }
        if (!source_enabled_locked(r) &&
            (!strcmp(j->status, "queued") || !strcmp(j->status, "retrying") ||
             !strcmp(j->status, "paused"))) {
            copy_text(j->status, sizeof j->status, "paused");
            copy_text(j->error, sizeof j->error,
                      "Source disabled. Enable it in Sources before resuming.");
        }
    }
    cJSON_Delete(o);
    return 0;
}
