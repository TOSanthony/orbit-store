#include "orbit.h"
#include "debrid.h"
#include "extract.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
/* Case-insensitive comparisons also protect exFAT destinations. */
bool job_paths_overlap(const Job *a, const Job *b) {
    if (a->device != b->device || a->inode != b->inode || strcmp(a->root, b->root)) return false;
    char ap[300], bp[300];
    snprintf(ap, sizeof ap, "%s.part", a->filename);
    snprintf(bp, sizeof bp, "%s.part", b->filename);
    return !strcasecmp(a->filename, b->filename) || !strcasecmp(ap, b->filename) ||
           !strcasecmp(a->filename, bp);
}
bool job_waiting(const Job *j) {
    return !strcmp(j->status, "queued") || !strcmp(j->status, "paused") ||
           !strcmp(j->status, "retrying");
}
bool job_active(const Job *j) {
    return !strcmp(j->status, "downloading") || !strcmp(j->status, "verifying") ||
           !strcmp(j->status, "extracting") || !strcmp(j->status, "inspecting");
}
/* A compressed option needs room for the archive and its files together. */
uint64_t job_space_needed(const Release *r) {
    uint64_t size = r->size > 0 ? (uint64_t)r->size : 0;
    uint64_t extra = r->compression[0] && r->extracted_size > 0 ? (uint64_t)r->extracted_size : 0;
    return UINT64_MAX - size < extra ? UINT64_MAX : size + extra;
}
static uint64_t download_remaining(const Job *j) {
    if (j->total <= 0 || j->received >= j->total) return 0;
    uint64_t present = j->received > 0 ? (uint64_t)j->received : 0;
    if (j->segmented) {
        /* exFAT may allocate the gap before a tail write. Those blocks already
         * reduce free space, but they must NEVER count as downloaded progress. */
        int dir = storage_open(j);
        if (dir >= 0) {
            char partial[192];
            snprintf(partial, sizeof partial, "%s.part", j->filename);
            struct stat st;
            if (!fstatat(dir, partial, &st, AT_SYMLINK_NOFOLLOW) &&
                S_ISREG(st.st_mode) && st.st_nlink == 1 && st.st_blocks > 0) {
                uint64_t allocated = (uint64_t)st.st_blocks * 512;
                if (allocated > present) present = allocated;
            }
            close(dir);
        }
    }
    return present >= (uint64_t)j->total ? 0 : (uint64_t)j->total - present;
}
static uint64_t remaining(const Job *j) {
    uint64_t left = download_remaining(j);
    if (!j->release.compression[0]) return left;
    int64_t expected = j->release.archive_unverified ? j->inspected_size : j->release.extracted_size;
    uint64_t total = expected > 0 ? (uint64_t)expected : 0;
    uint64_t staged = j->extracted > 0 ? (uint64_t)j->extracted : 0;
    uint64_t files = staged >= total ? 0 : total - staged;
    return UINT64_MAX - left < files ? UINT64_MAX : left + files;
}
uint64_t storage_pending_locked(const Storage *s, const Job *exclude) {
    uint64_t pending = 0;
    for (size_t i = 0; i < orbit.job_count; i++) {
        const Job *j = &orbit.jobs[i];
        if (j == exclude || !j->id[0] || !strcmp(j->status, "complete") ||
            !strcmp(j->status, "cancelled") || strcmp(j->storage_id, s->id) ||
            strcmp(j->root, s->root) || j->device != s->device || j->inode != s->inode)
            continue;
        uint64_t left = remaining(j);
        if (UINT64_MAX - pending < left)
            return UINT64_MAX;
        pending += left;
    }
    return pending;
}
static bool fits(const Storage *s, uint64_t size, const Job *exclude) {
    uint64_t pending = storage_pending_locked(s, exclude), margin = 16 * 1024 * 1024;
    return pending <= s->free_bytes && size <= s->free_bytes - pending &&
           margin <= s->free_bytes - pending - size;
}
/* Never orphan a kept partial file. Removing a history record deletes no disk files. */
static bool removable(const Job *j) {
    if (!strcmp(j->status, "complete"))
        return true;
    if (strcmp(j->status, "cancelled"))
        return false;
    int dir = storage_open(j);
    if (dir < 0)
        return false;
    char partial[192];
    snprintf(partial, sizeof partial, "%s.part", j->filename);
    struct stat st;
    int rc = fstatat(dir, partial, &st, AT_SYMLINK_NOFOLLOW), saved = errno;
    close(dir);
    return rc && saved == ENOENT;
}
/* Call only after the writer stops. An absent file is already cleaned up;
 * other errors must not discard the saved resume map, even at zero progress. */
int job_delete_partial_locked(Job *j, char *err, size_t cap) {
    int dir = storage_open(j);
    if (dir < 0) {
        copy_text(err, cap, "Reconnect the original drive before deleting the partial file.");
        return -1;
    }
    char partial[192], path[540];
    snprintf(partial, sizeof partial, "%s.part", j->filename);
    snprintf(path, sizeof path, "%s/homebrew", j->root);
    int rc = unlinkat(dir, partial, 0), saved = errno;
    /* An interrupted extraction leaves only Orbit's own staging folder. */
    int staged = j->release.compression[0] ? extract_discard(dir, path, j->id) : 0;
    close(dir);
    if ((rc && saved != ENOENT) || staged) {
        copy_text(err, cap, "Could not delete the partial file.");
        return -1;
    }
    j->archive_ready = false;
    j->inspected_size = 0;
    j->inspected_format[0] = 0;
    j->extracted = 0;
    j->received = 0;
    j->segmented = false;
    j->range_count = 0;
    memset(j->range_received, 0, sizeof j->range_received);
    j->etag[0] = 0;
    j->modified[0] = 0;
    j->error[0] = 0;
    return 0;
}
int history_clear_locked(const cJSON *input, char *err, size_t cap) {
    const cJSON *filter = cJSON_GetObjectItemCaseSensitive(input, "status");
    const char *status = json_text(input, "status");
    if (filter && (!cJSON_IsString(filter) ||
                   (strcmp(status, "complete") && strcmp(status, "cancelled")))) {
        copy_text(err, cap, "History status must be complete or cancelled.");
        return 400;
    }
    if (orbit.stop) {
        copy_text(err, cap, "Orbit is stopping.");
        return 503;
    }
    Job *backup = malloc(sizeof orbit.jobs);
    if (!backup) {
        copy_text(err, cap, "Could not clear history.");
        return 503;
    }
    memcpy(backup, orbit.jobs, sizeof orbit.jobs);
    for (size_t i = 0; i < orbit.job_count; i++)
        if (orbit.jobs[i].id[0] && (!filter || !strcmp(orbit.jobs[i].status, status)) &&
            removable(&orbit.jobs[i]))
            memset(&orbit.jobs[i], 0, sizeof(Job));
    int rc = state_save_locked();
    if (rc) {
        /* Only cleared, inactive slots changed. Never overwrite an active job's
         * atomics while its network callbacks are reading them. */
        for (size_t i = 0; i < orbit.job_count; i++)
            if (!orbit.jobs[i].id[0] && backup[i].id[0]) orbit.jobs[i] = backup[i];
        copy_text(err, cap, "Could not save history changes.");
    }
    free(backup);
    return rc ? 503 : 200;
}
static int create_locked(const Release *r, const char *sid, const Storage *pin,
                         const char *etag, const char *modified, bool dry_run, const char *delivery,
                         char *err, size_t cap, Job **result) {
    const char *rid = r ? r->id : "";
    if (orbit.library_storage_busy || orbit.receiver_active) {
        copy_text(err, cap, "A library storage operation is in progress. Wait for it to finish before downloading.");
        return 409;
    }
    if (orbit.stop) {
        copy_text(err, cap, "Orbit is stopping. Reconnect after restarting it.");
        return 503;
    }
    if (!r) {
        copy_text(err, cap, "Unknown curated release.");
        return 404;
    }
    if (!source_enabled_locked(r)) {
        copy_text(err, cap,
                  "Enable this source and acknowledge the download notice in Sources first.");
        return 409;
    }
    Job delivery_job = {0};
    bool torbox = delivery && !strcmp(delivery, "torbox");
    if (delivery && *delivery && strcmp(delivery, "direct") && !torbox) {
        copy_text(err, cap, "Choose direct or torbox delivery."); return 400;
    }
    if (torbox) {
        int code = debrid_prepare_locked(r, &delivery_job, err, cap);
        if (code != 200) return code;
    }
    if (!torbox && !provider_supported(r) && !(dry_run && provider_browser_supported(r))) {
        copy_text(err, cap, provider_browser_supported(r)
            ? (!strcmp(r->source, "akirabox")
                ? "Complete verification on AkiraBox, then wait for Orbit to check the download link."
                : "Open this option's download page on PS5 first, select Download, then return to Orbit.")
            : "This provider does not support direct console downloads yet.");
        return 409;
    }
#ifndef ORBIT_TEST
    if (orbit.desktop) {
        copy_text(err, cap, "Desktop preview cannot download games. Connect to Orbit on your PS5.");
        return 409;
    }
#endif
    if (orbit.state_failed) {
        copy_text(err, cap, "Queue state cannot be saved. Check console storage.");
        return 503;
    }
    Storage a[ORBIT_MAX_STORAGE], *s = NULL;
    size_t n = storage_list(a);
    for (size_t i = 0; i < n; i++)
        if (!strcmp(a[i].id, sid)) {
            s = &a[i];
            break;
        }
    if (!s) {
        copy_text(err, cap, "Selected storage is not connected or writable.");
        return 409;
    }
    if (pin && (strcmp(s->root, pin->root) || s->device != pin->device || s->inode != pin->inode)) {
        copy_text(err, cap, "The selected drive changed. Start verification again.");
        return 409;
    }
    Job destination = {.device = s->device, .inode = s->inode};
    copy_text(destination.root, sizeof destination.root, s->root);
    copy_text(destination.filename, sizeof destination.filename, r->filename);
    long finished = -1, unused = -1;
    unsigned last_order = 0;
    for (size_t i = 0; i < orbit.job_count; i++) {
        Job *j = &orbit.jobs[i];
        if (!j->id[0]) {
            if (unused < 0)
                unused = (long)i;
            continue;
        }
        if (j->order > last_order)
            last_order = j->order;
        if (strcmp(j->release_id, rid) && strcmp(j->status, "complete") &&
            strcmp(j->status, "cancelled") && job_paths_overlap(j, &destination)) {
            copy_text(err, cap, "Another download uses this filename on that drive. Open Downloads to manage it.");
            return 409;
        }
        if (!strcmp(j->release_id, rid) && !strcmp(j->storage_id, sid)) {
            /* Reconnecting a drive can change its identity. A cancelled job on
             * an unverified old drive must not block a NEW download to the
             * selected drive. Retain that record (and any original partial),
             * never rebind its resume map or delete files on the new drive.
             * The destination checks below still require both names absent. */
            if (!strcmp(j->status, "cancelled") &&
                (strcmp(j->root, s->root) || j->device != s->device || j->inode != s->inode))
                continue;
            /* Completed jobs and cancellations without a kept partial are history.
               Verify the original drive before reusing a cancelled slot, even when
               received is zero. The file check below still refuses to overwrite. */
            if (removable(j)) {
                finished = (long)i;
                continue;
            }
            copy_text(err, cap,
                      !strcmp(j->status, "cancelled")
                          ? "Open Downloads to resume this cancelled download, or reconnect its original drive and delete the partial file before downloading again."
                          : "This release already has a job on that drive. Open Downloads to resume or retry it.");
            return 409;
        }
    }
    if (finished < 0)
        finished = unused;
    if (finished < 0 && orbit.job_count >= ORBIT_MAX_JOBS) {
        copy_text(err, cap, "Queue is full. Clear finished or cancelled history to make room.");
        return 409;
    }
    if (!fits(s, job_space_needed(r), NULL)) {
        copy_text(
            err, cap,
            "Not enough space after unfinished downloads. Free space or cancel a queued download.");
        return 409;
    }
    Job j = delivery_job;
    j.release = *r;
    copy_text(j.etag, sizeof j.etag, etag ? etag : "");
    copy_text(j.modified, sizeof j.modified, modified ? modified : "");
    j.order = last_order + 1;
    random_hex(j.id, 8);
    copy_text(j.release_id, sizeof j.release_id, rid);
    copy_text(j.storage_id, sizeof j.storage_id, sid);
    copy_text(j.root, sizeof j.root, s->root);
    copy_text(j.filename, sizeof j.filename, r->filename);
    copy_text(j.status, sizeof j.status, "queued");
    j.device = s->device;
    j.inode = s->inode;
    j.total = r->size;
    int dir = storage_open(&j);
    if (dir < 0) {
        copy_text(err, cap, "Cannot open the selected homebrew folder.");
        return 409;
    }
    char partial[192];
    snprintf(partial, sizeof partial, "%s.part", j.filename);
    struct stat st;
    bool absent = fstatat(dir, j.filename, &st, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT &&
                  fstatat(dir, partial, &st, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT;
    if (!absent) {
        /* A copy left from an earlier download is replaced, as the person asked: only a plain
         * file with this exact name, and only when no queued job is using the name. Anything
         * else (a folder, a link) still stops the download. */
        bool replace = true, busy = false;
        const char *names[2] = {j.filename, partial};
        for (int k = 0; k < 2 && replace; k++) {
            if (fstatat(dir, names[k], &st, AT_SYMLINK_NOFOLLOW) == 0)
                replace = S_ISREG(st.st_mode);
            else
                replace = errno == ENOENT;
        }
        for (size_t i = 0; i < orbit.job_count && replace; i++) {
            const Job *other = &orbit.jobs[i];
            if (other->id[0] && !removable(other) && !strcmp(other->root, s->root) &&
                !strcmp(other->filename, j.filename))
                busy = true;
        }
        if (replace && !busy) {
            if (!dry_run) {
                for (int k = 0; k < 2; k++)
                    if (unlinkat(dir, names[k], 0) && errno != ENOENT) replace = false;
            }
            absent = replace;
        }
    }
    if (!absent) {
        close(dir);
        copy_text(err, cap,
                  "A destination or partial file already exists. Orbit will not overwrite it.");
        return 409;
    }
    if (!dry_run && storage_check_writable(dir)) {
        close(dir);
        copy_text(err, cap, "Destination is missing or not writable.");
        return 409;
    }
    close(dir);
    if (dry_run) return 200;
    size_t slot = finished >= 0 ? (size_t)finished : orbit.job_count;
    Job previous = orbit.jobs[slot];
    orbit.jobs[slot] = j;
    if (finished < 0)
        orbit.job_count++;
    *result = &orbit.jobs[slot];
    copy_text(orbit.preferred_storage, sizeof orbit.preferred_storage, sid);
    if (state_save_locked()) {
        if (finished >= 0)
            orbit.jobs[slot] = previous;
        else
            orbit.job_count--;
        copy_text(err, cap, "Could not persist the queue.");
        return 503;
    }
    pthread_cond_broadcast(&orbit.changed);
    diagnostics_event_locked("queued", *result, 0, 0);
    return 201;
}
int job_create_locked(const char *rid, const char *sid, char *err, size_t cap, Job **result) {
    return create_locked(release_find(rid), sid, NULL, NULL, NULL, false, "direct", err, cap, result);
}
int job_create_delivery_locked(const char *rid, const char *sid, const char *delivery,
                                char *err, size_t cap, Job **result) {
    return create_locked(release_find(rid), sid, NULL, NULL, NULL, false, delivery, err, cap, result);
}
int job_check_capture_locked(const Release *r, const Storage *s, char *err, size_t cap) {
    return create_locked(r, s->id, s, NULL, NULL, true, "direct", err, cap, NULL);
}
int job_create_captured_locked(const Release *r, const Storage *s, const char *etag,
                               const char *modified, char *err, size_t cap, Job **result) {
    if (!provider_capture_url(r, r->url) || (!etag[0] && !modified[0])) {
        copy_text(err, cap, "The provider file has not been validated.");
        return 409;
    }
    return create_locked(r, s->id, s, etag, modified, false, "direct", err, cap, result);
}
bool job_can_refresh_link(const Job *j) {
    return j && (!strcmp(j->status, "paused") || !strcmp(j->status, "error")) &&
        !j->archive_ready && !(j->release.compression[0] && j->received >= j->total) &&
        strcmp(j->delivery, "torbox") &&
        (!strcmp(j->release.source, "akirabox") || !strcmp(j->release.source, "datanodes")) &&
        provider_browser_supported(&j->release);
}
int job_refresh_link_locked(Job *j, const Release *r, const Storage *pin,
                            const char *etag, const char *modified, char *err, size_t cap) {
    if (orbit.stop || orbit.state_failed) {
        copy_text(err, cap, "Queue state cannot be saved. Try again after restarting Orbit."); return 503;
    }
    if (!job_can_refresh_link(j) || !source_enabled_locked(&j->release) ||
        strcmp(j->release_id, r->id) || strcmp(j->release.source, r->source) ||
        strcmp(j->filename, r->filename) || strcmp(j->release.browser_url, r->browser_url) ||
        j->total != r->size || strcmp(j->release.compression, r->compression) ||
        j->release.extracted_size != r->extracted_size || j->release.archive_unverified != r->archive_unverified || !provider_capture_url(r, r->url)) {
        copy_text(err, cap, "This download changed. Open its download page again."); return 409;
    }
    if ((!etag[0] && !modified[0]) || (j->etag[0] ? strcmp(j->etag, etag) :
        j->modified[0] ? strcmp(j->modified, modified) : j->received > 0)) {
        copy_text(err, cap, "The provider file changed. Your partial file was kept."); return 409;
    }
    Storage drives[ORBIT_MAX_STORAGE]; size_t count = storage_list(drives);
    bool same = false;
    for (size_t i = 0; i < count; i++) {
        Storage *s = &drives[i];
        if (!strcmp(s->id, j->storage_id) && !strcmp(s->root, j->root) &&
            s->device == j->device && s->inode == j->inode &&
            !strcmp(s->root, pin->root) && s->device == pin->device && s->inode == pin->inode)
            same = fits(s, remaining(j), j);
    }
    if (!same) { copy_text(err, cap, "Reconnect the original drive and check its free space."); return 409; }
    int dir = storage_open(j);
    if (dir < 0) { copy_text(err, cap, "Cannot open the original homebrew folder."); return 409; }
    close(dir);
    Job previous = *j;
    copy_text(j->release.url, sizeof j->release.url, r->url);
    copy_text(j->etag, sizeof j->etag, etag);
    copy_text(j->modified, sizeof j->modified, modified);
    copy_text(j->status, sizeof j->status, "queued");
    j->error[0] = 0; j->retry_at = 0; j->attempts = 0;
    atomic_store(&j->pause, false); atomic_store(&j->cancel, false);
    if (state_save_locked()) { *j = previous; copy_text(err, cap, "Could not persist the queue."); return 503; }
    pthread_cond_broadcast(&orbit.changed);
    return 200;
}
int job_password_locked(Job *j, const char *password, char *err, size_t cap) {
    if (!j->release.compression[0] || !j->archive_ready ||
        (strcmp(j->status, "error") && strcmp(j->status, "paused"))) {
        copy_text(err, cap, "Pause extraction or wait for it to finish before entering a password."); return 409;
    }
    if (!password || !*password || strlen(password) >= sizeof j->archive_password) {
        copy_text(err, cap, "Enter an archive password of up to 128 bytes."); return 400;
    }
    for (const unsigned char *p = (const void *)password; *p; p++)
        if (*p < 32 || *p == 127) {
            copy_text(err, cap, "The archive password cannot contain control characters."); return 400;
        }
    Job previous = *j;
    copy_text(j->archive_password, sizeof j->archive_password, password);
    int code = job_action_locked(j, "retry", false, err, cap);
    if (code != 200) *j = previous;
    return code;
}
int job_action_locked(Job *j, const char *action, bool remove, char *err, size_t cap) {
    browser_job_changed_locked(j->id);
    if (orbit.stop) {
        copy_text(err, cap, "Orbit is stopping. Reconnect after restarting it.");
        return 503;
    }
    bool active = job_active(j);
    if (!strcmp(action, "delete") || !strcmp(action, "forget")) {
        if (strcmp(j->status, "cancelled")) {
            copy_text(err, cap, "Only cancelled downloads can be deleted.");
            return 409;
        }
        /* "forget" is an explicit history-only confirmation. It must never
         * open a drive or delete a file, even when the original is unavailable. */
        if (!strcmp(action, "delete") && job_delete_partial_locked(j, err, cap))
            return 409;
        Job previous = *j;
        memset(j, 0, sizeof *j);
        if (state_save_locked()) {
            *j = previous;
            copy_text(err, cap, "Could not save history changes.");
            return 503;
        }
        return 200;
    }
    if (!strcmp(action, "remove")) {
        if (!removable(j)) {
            copy_text(err, cap,
                      "Only completed or cancelled history can be removed. Reconnect the original "
                      "drive and delete any kept partial file first.");
            return 409;
        }
        Job previous = *j;
        memset(j, 0, sizeof *j);
        if (state_save_locked()) {
            *j = previous;
            copy_text(err, cap, "Could not save history changes.");
            return 503;
        }
        return 200;
    }
    if (!strcmp(action, "move-up") || !strcmp(action, "move-down")) {
        if (!job_waiting(j)) {
            copy_text(err, cap, "Only waiting downloads can be reordered.");
            return 409;
        }
        bool up = !strcmp(action, "move-up");
        Job *neighbor = NULL;
        for (size_t i = 0; i < orbit.job_count; i++) {
            Job *other = &orbit.jobs[i];
            if (other == j || !job_waiting(other))
                continue;
            if ((up ? other->order < j->order : other->order > j->order) &&
                (!neighbor ||
                 (up ? other->order > neighbor->order : other->order < neighbor->order)))
                neighbor = other;
        }
        if (!neighbor)
            return 200;
        unsigned previous = j->order;
        j->order = neighbor->order;
        neighbor->order = previous;
        if (state_save_locked()) {
            neighbor->order = j->order;
            j->order = previous;
            copy_text(err, cap, "Could not save queue order.");
            return 503;
        }
        return 200;
    }
    if (!strcmp(action, "pause")) {
        if (active)
            j->pause = true;
        else if (!strcmp(j->status, "queued") || !strcmp(j->status, "retrying"))
            copy_text(j->status, sizeof j->status, "paused");
        else {
            copy_text(err, cap, "This job is not running.");
            return 409;
        }
    } else if (!strcmp(action, "resume") || !strcmp(action, "retry")) {
        if (orbit.library_storage_busy) {
            copy_text(err, cap, "Wait for the library storage operation to finish before resuming downloads.");
            return 409;
        }
        Release *r = &j->release;
        bool torbox = !strcmp(j->delivery, "torbox");
        if (!source_enabled_locked(r) || (!torbox && !provider_supported(r))) {
            copy_text(err, cap,
                      "Enable a supported source in Sources before resuming this download.");
            return 409;
        }
        if (torbox) {
            Job checked = {0};
            int code = debrid_prepare_locked(r, &checked, err, cap);
            if (code != 200) return code;
            if (strcmp(checked.debrid_account, j->debrid_account)) {
                copy_text(err, cap, "Reconnect the original TorBox account before resuming.");
                return 409;
            }
        }
        if (active || !strcmp(j->status, "complete") || !strcmp(j->status, "queued")) {
            copy_text(err, cap, "This job cannot be resumed in its current state.");
            return 409;
        }
        if (!storage_matches(j)) {
            copy_text(err, cap, "Reconnect the original destination drive.");
            return 409;
        }
        Storage drives[ORBIT_MAX_STORAGE];
        size_t count = storage_list(drives);
        bool enough = false;
        for (size_t i = 0; i < count; i++)
            if (!strcmp(drives[i].id, j->storage_id))
                enough = fits(&drives[i], remaining(j), j);
        if (!enough) {
            copy_text(err, cap,
                      "Not enough space after unfinished downloads. Free space or cancel a queued "
                      "download.");
            return 409;
        }
        j->pause = false;
        j->cancel = false;
        j->remove = false;
        j->attempts = 0;
        j->retry_at = 0;
        j->error[0] = 0;
        copy_text(j->status, sizeof j->status, "queued");
    } else if (!strcmp(action, "cancel")) {
        if (!strcmp(j->status, "complete")) {
            copy_text(err, cap, "Completed files cannot be deleted through cancellation.");
            return 409;
        }
        if (!active) {
            if (remove && job_delete_partial_locked(j, err, cap))
                return 409;
            copy_text(j->status, sizeof j->status, "cancelled");
        }
        j->cancel = true;
        j->remove = remove;
    } else {
        copy_text(err, cap, "Unknown queue action.");
        return 404;
    }
    if (state_save_locked()) {
        copy_text(err, cap, "Could not persist the queue change.");
        return 503;
    }
    pthread_cond_broadcast(&orbit.changed);
    diagnostics_event_locked(action, j, 0, 0);
    return 200;
}
