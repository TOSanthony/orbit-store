#include "orbit.h"
#include "browser_platform.h"
#include "browser_match.h"
#include "viking_session.h"
#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* One user-requested session, held only in memory. The queue receives a validated
 * selected file route, never arbitrary browser bytes or account credentials. */
static struct {
    pthread_t thread;
    pthread_cond_t changed;
    bool ready, stop, pending, busy, thread_started, return_to_browser, native_dialog, dialog_opened, account;
    bool check_account, resolving;
    atomic_bool cancel;
    char id[33], state[16], message[256], job_id[33], title[128], client_id[65];
    char native_failure[256];
    Release release;
    Storage storage;
    Job refresh_job;
    int64_t started_ms, capture_ms, link_ms, validation_ms, queued_ms, finished_ms;
    uint64_t scanned_bytes;
    unsigned candidates, browser_processes;
} session = {.changed = PTHREAD_COND_INITIALIZER};

static int64_t milliseconds(void) {
    struct timespec now;
    return clock_gettime(CLOCK_MONOTONIC, &now) ? 0 :
        (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static bool available(void) {
#ifdef ORBIT_TEST
    return true;
#else
    return !orbit.desktop;
#endif
}
#ifndef ORBIT_TEST
static time_t monotonic(void) {
    struct timespec now;
    return clock_gettime(CLOCK_MONOTONIC, &now) ? 0 : now.tv_sec;
}
#endif
static void state_locked(const char *state, const char *message) {
    copy_text(session.state, sizeof session.state, state);
    copy_text(session.message, sizeof session.message, message);
}
/* Only a small set of native-owned failures can be reported. Never accept
 * page content, signed URLs or arbitrary error text from a browser client. */
static const char *native_failure_message(const char *failure) {
    if (!strcmp(failure, "open"))
        return "The PS5 browser could not open. Close any other browser page, then try again.";
    if (!strcmp(failure, "browser"))
        return "The PS5 browser could not load this page. Check the PS5's date, time and network settings, then open the download page again.";
    if (!strcmp(failure, "timeout"))
        return "Verification timed out. Open the download page again and complete verification if shown.";
    if (!strcmp(failure, "connection"))
        return "Connection to Orbit was lost. Reconnect to Orbit, then open the download page again.";
    return NULL;
}
static bool selected_locked(void) {
    if (session.account) return true;
    if (session.refresh_job.id[0]) {
        const Job *j = job_find(session.refresh_job.id), *p = &session.refresh_job;
        return job_can_refresh_link(j) && source_enabled_locked(&j->release) &&
            !strcmp(j->status, p->status) && j->received == p->received && j->attempts == p->attempts &&
            !strcmp(j->release.url, p->release.url) && !strcmp(j->etag, p->etag) &&
            !strcmp(j->modified, p->modified) && j->device == p->device && j->inode == p->inode;
    }
    const Release *r = release_find(session.release.id), *p = &session.release;
    return r && source_enabled_locked(r) && r->size == p->size &&
        !strcmp(r->source, p->source) && !strcmp(r->game_id, p->game_id) &&
        !strcmp(r->title_id, p->title_id) && !strcmp(r->filename, p->filename) &&
        !strcmp(r->format, p->format) && !strcmp(r->sha256, p->sha256) &&
        !strcmp(r->url, p->url) && !strcmp(r->browser_url, p->browser_url);
}
static bool active(void) {
    pthread_mutex_lock(&orbit.mutex);
    bool ok = !session.stop && !orbit.stop && !atomic_load(&session.cancel) && selected_locked();
    pthread_mutex_unlock(&orbit.mutex);
    return ok;
}
static void delay(void) {
    struct timespec t = {.tv_nsec = 100000000};
    nanosleep(&t, NULL);
}
static int validate_capture(Release *r, char *etag, char *modified, char *error, size_t cap) {
    const int64_t start = milliseconds();
    int result = transfer_validate_capture(r, &session.cancel, etag, modified, error, cap);
    pthread_mutex_lock(&orbit.mutex);
    session.validation_ms += milliseconds() - start;
    pthread_mutex_unlock(&orbit.mutex);
    return result;
}
#ifndef ORBIT_TEST
static bool native_active(void *context) {
    return active() && probe_target_active(context);
}
#endif
static bool capture(Release *r, char *etag, char *modified, bool *validated, char *error, size_t cap) {
    *validated = false;
    /* Native owns the dialog. Do not scan an old browser before it confirms
     * opening this exact session. An abandoned prepare expires in 30 seconds. */
    if (session.native_dialog) {
        bool opened = false;
        for (unsigned i = 0; i < 300 && active(); ++i) {
            pthread_mutex_lock(&orbit.mutex);
            opened = session.dialog_opened;
            pthread_mutex_unlock(&orbit.mutex);
            if (opened) break;
            delay();
        }
        if (!opened || !active()) {
            copy_text(error, cap, "The native download page did not open. Return to Orbit and try again.");
            return false;
        }
    }
    pthread_mutex_lock(&orbit.mutex);
    session.capture_ms = milliseconds() - session.started_ms;
    pthread_mutex_unlock(&orbit.mutex);
#ifdef ORBIT_TEST
    (void)etag; (void)modified;
    /* Compile-time-only simulator: not exposed through the API or in ELF builds. */
    const char *candidate = getenv("ORBIT_TEST_CAPTURE_URL");
    const char *wait = getenv("ORBIT_TEST_CAPTURE_DELAY_MS");
    unsigned steps = wait ? (unsigned)strtoul(wait, NULL, 10) / 100 : 1;
    if (steps > 300) steps = 300;
    for (unsigned i = 0; i < steps && active(); i++) delay();
    if (!active()) return false;
    if (!candidate || !provider_capture_url(r, candidate)) {
        copy_text(error, cap, "No matching download route was found."); return false;
    }
    copy_text(r->url, sizeof r->url, candidate);
    pthread_mutex_lock(&orbit.mutex);
    session.link_ms = milliseconds() - session.started_ms;
    session.candidates++;
    pthread_mutex_unlock(&orbit.mutex);
    return true;
#else
    if (!active()) return false;
    if (!session.native_dialog && probe_browser_open(r->browser_url)) {
        copy_text(error, cap, "The PS5 browser could not open. Return to Orbit and try again.");
        return false;
    }
    const time_t deadline = monotonic() + PROBE_SESSION_SECONDS;
    ProbeTarget targets[PROBE_TARGET_LIMIT] = {{0}};
    int count = 0;
    time_t missing_since = monotonic();
    char rejected[6][2048] = {{0}};
    int64_t rejected_until[6] = {0};
    unsigned failures = 0;
    BrowserMatch match = {.release = r, .rejected = rejected, .rejected_until = rejected_until};
    ProbeReader reader = {.read = probe_target_read,
        .active = native_active, .match = browser_match, .match_context = &match,
        .remaining = PROBE_SESSION_BUDGET};
    while (active() && monotonic() < deadline && reader.remaining) {
        ProbeTarget found[PROBE_TARGET_LIMIT] = {{0}};
        int n = probe_targets_find(found, PROBE_TARGET_LIMIT);
        if (n < 0) {
            copy_text(error, cap, n == -2
                ? "Close other PS5 browser pages, then try opening this download again."
                : "Orbit could not read the PS5 browser status. Return to Orbit and try again.");
            return false;
        }
        if (!n) {
            if (monotonic() - missing_since >= 10) {
                copy_text(error, cap, "The PS5 browser did not become ready. Return to Orbit and try again.");
                return false;
            }
            delay();
            continue;
        }
        missing_since = monotonic();
        for (int i = 0; i < n; ++i) {
            found[i].cancel = &session.cancel;
            found[i].deadline = deadline;
            for (int j = 0; j < count; ++j)
                if (found[i].pid == targets[j].pid &&
                    found[i].started_seconds == targets[j].started_seconds &&
                    found[i].started_microseconds == targets[j].started_microseconds &&
                    !strcmp(found[i].name, targets[j].name))
                    found[i].cursor = targets[j].cursor;
        }
        memcpy(targets, found, sizeof targets);
        count = n;
        const uint64_t before = reader.attempted;
        for (int i = 0; i < count && active() && reader.remaining; ++i) {
            reader.context = &targets[i];
            match.now_ms = milliseconds();
            /* Share one read/time budget fairly. An older ShellUI browser must
             * not hide or delay the current native dialog's network process. */
            ProbeResult result = probe_target_scan_slice(&targets[i], &reader,
                !strcmp(r->source, "vikingfile") ? "https://vikingfile.com/d/" : "https://",
                PROBE_SCAN_SLICE);
            pthread_mutex_lock(&orbit.mutex);
            session.scanned_bytes = reader.attempted;
            session.browser_processes = count;
            if (result == PROBE_FOUND) {
                session.link_ms = milliseconds() - session.started_ms;
                session.candidates++;
            }
            pthread_mutex_unlock(&orbit.mutex);
            if (result == PROBE_FOUND && active()) {
                /* All candidates still require the selected filename/host and
                 * actual response identity. A stale link in a previous page
                 * cannot stop us looking for the newly generated one. */
                Release candidate = *r;
                copy_text(candidate.url, sizeof candidate.url, match.url);
                pthread_mutex_lock(&orbit.mutex);
                if (!session.stop && !atomic_load(&session.cancel))
                    state_locked("validating", "Checking the selected file before adding it to Downloads…");
                pthread_mutex_unlock(&orbit.mutex);
                int invalid = validate_capture(&candidate, etag, modified, error, cap);
                memset(&candidate, 0, sizeof candidate);
                if (invalid) {
                    pthread_mutex_lock(&orbit.mutex);
                    if (!session.stop && !atomic_load(&session.cancel))
                        state_locked("waiting-link", "Waiting for a fresh download link from the provider…");
                    pthread_mutex_unlock(&orbit.mutex);
                    /* A generated route may briefly fail before the provider
                     * makes it usable. Recheck after a cooldown, always with
                     * full identity validation and the existing session limit. */
                    size_t slot = failures % 6;
                    copy_text(rejected[slot], sizeof rejected[0], match.url);
                    rejected_until[slot] = milliseconds() + 8000;
                    if (match.rejected_count < 6) match.rejected_count++;
                    match.url[0] = 0;
                    if (++failures == 18) return false;
                    continue;
                }
                *validated = true;
                copy_text(r->url, sizeof r->url, match.url);
                memset(&match, 0, sizeof match);
                memset(rejected, 0, sizeof rejected);
                return true;
            }
            /* A process can exit/restart while the new page opens. Discovery
             * refreshes its identity next round; never keep reading a reused PID. */
        }
        if (reader.attempted == before) delay();
    }
    if (!error[0]) copy_text(error, cap,
        "No matching download was found within the verification window. Return to Orbit to try again.");
    return false;
#endif
}
static void *worker(void *unused) {
    (void)unused;
    pthread_mutex_lock(&orbit.mutex);
    while (!session.stop) {
        while (!session.pending && !session.stop)
            pthread_cond_wait(&session.changed, &orbit.mutex);
        if (session.stop) break;
        session.pending = false;
        /* Account pages use the provider's own cookie jar. Do not scan their
         * memory, read credentials, validate a file, or create a queue entry. */
        if (session.account) {
            if (session.check_account) {
                pthread_mutex_unlock(&orbit.mutex);
                (void)viking_check_session(session.native_dialog, &session.cancel);
                pthread_mutex_lock(&orbit.mutex);
                state_locked("idle", "Vikingfile");
                session.busy = false;
                continue;
            }
            if (session.native_dialog) {
                /* The native app can acknowledge the open before this worker
                 * is scheduled. Preserve that acknowledgement (or failure). */
                if (!session.dialog_opened && !atomic_load(&session.cancel))
                    state_locked("opening", "Opening Vikingfile sign-in…");
                const int64_t deadline = milliseconds() + 240000;
                while (!session.stop && !orbit.stop && !atomic_load(&session.cancel) && milliseconds() < deadline) {
                    pthread_mutex_unlock(&orbit.mutex);
                    delay();
                    pthread_mutex_lock(&orbit.mutex);
                }
                if (!session.stop && !orbit.stop && !session.native_failure[0]) {
                    pthread_mutex_unlock(&orbit.mutex);
                    (void)viking_check_session(true, NULL);
                    pthread_mutex_lock(&orbit.mutex);
                }
            } else if (!atomic_load(&session.cancel)) {
                pthread_mutex_unlock(&orbit.mutex);
#ifdef ORBIT_TEST
                const int opened = 0;
#else
                const int opened = probe_browser_open("https://vikingfile.com/login");
#endif
                pthread_mutex_lock(&orbit.mutex);
                if (opened) copy_text(session.native_failure, sizeof session.native_failure,
                    "The PS5 browser could not open. Close any other browser page, then try again.");
            }
            state_locked(session.native_failure[0] ? "failed" : "idle", session.native_failure[0]
                ? session.native_failure : "Sign in on Vikingfile, then return to Orbit.");
            session.finished_ms = milliseconds() - session.started_ms;
            session.busy = false;
            continue;
        }
        Release resolved = session.release;
        if (atomic_load(&session.cancel)) {
            /* The owner may report failure before this worker is scheduled. */
        } else if (session.resolving)
            state_locked("validating", "Checking sign-in…");
        else if (session.native_dialog && !session.dialog_opened)
            state_locked("opening", "Opening the download page in Orbit…");
        else if (!strcmp(resolved.source, "akirabox") || !strcmp(resolved.source, "vikingfile"))
            state_locked("waiting-link", "Complete verification if shown, then wait. Orbit will check the generated download link automatically.");
        else
            state_locked("waiting", "Complete the provider’s steps, select Download, then return to Orbit.");
        pthread_mutex_unlock(&orbit.mutex);
        char error[256] = {0}, etag[256] = {0}, modified[128] = {0};
        bool validated = false;
        bool ok = active() && viking_resolve_download(&resolved, session.native_dialog, &session.cancel);
        if (ok) {
            ok = !validate_capture(&resolved, etag, modified, error, sizeof error);
            validated = ok;
        }
        pthread_mutex_lock(&orbit.mutex);
        session.resolving = false;
        if (!ok) {
            resolved = session.release;
            error[0] = etag[0] = modified[0] = 0;
            if (!atomic_load(&session.cancel)) state_locked("opening", "Opening the download page on your PS5…");
        }
        /* A direct result never opened a window to return from. */
        if (ok) session.return_to_browser = false;
        pthread_mutex_unlock(&orbit.mutex);
        if (!ok && active()) ok = capture(&resolved, etag, modified, &validated, error, sizeof error);
        pthread_mutex_lock(&orbit.mutex);
        ok = ok && !session.stop && !atomic_load(&session.cancel) && selected_locked();
        if (ok) state_locked("validating", "Checking the selected file before adding it to Downloads…");
        pthread_mutex_unlock(&orbit.mutex);
        if (ok && !validated) ok = !validate_capture(&resolved, etag, modified, error, sizeof error);
        pthread_mutex_lock(&orbit.mutex);
        if (session.stop || atomic_load(&session.cancel)) {
            if (session.native_failure[0]) state_locked("failed", session.native_failure);
            else state_locked("cancelled", "Verification cancelled. No download was added.");
        } else if (!selected_locked()) {
            state_locked("failed", "The source or catalogue option changed. Start verification again.");
        } else if (ok) {
            Job *job = session.refresh_job.id[0] ? job_find(session.refresh_job.id) : NULL;
            int code = session.refresh_job.id[0]
                ? job_refresh_link_locked(job, &resolved, &session.storage, etag, modified, error, sizeof error)
                : job_create_captured_locked(&resolved, &session.storage, etag, modified, error, sizeof error, &job);
            if (code == 200 || code == 201) {
                copy_text(session.job_id, sizeof session.job_id, job->id);
                state_locked("queued", "Your verified file has been added to Downloads.");
                session.queued_ms = milliseconds() - session.started_ms;
            } else state_locked("failed", error);
        } else state_locked("failed", error[0] ? error : "Verification stopped. Return to Orbit to try again.");
#ifndef ORBIT_DESKTOP
        /* Native closes only its own dialog after observing the committed job.
         * Relaunching an already-running native title does not close ShellUI's
         * browser overlay. Older native clients return manually. */
        if (session.return_to_browser && !strcmp(session.state, "queued") &&
            !session.stop && !orbit.stop && !atomic_load(&session.cancel)) {
            char home[64];
            snprintf(home, sizeof home, "http://127.0.0.1:%d/", ORBIT_HTTP_PORT);
            pthread_mutex_unlock(&orbit.mutex);
            int returned = probe_browser_open(home);
            pthread_mutex_lock(&orbit.mutex);
            if (returned)
                state_locked("queued", "Your verified file has been added to Downloads. Return to Orbit to view it.");
        }
#endif
        /* No pending verification survives a restart. No browser bytes are saved. */
        memset(&resolved, 0, sizeof resolved);
        memset(&session.release, 0, sizeof session.release);
        memset(&session.refresh_job, 0, sizeof session.refresh_job);
        session.finished_ms = milliseconds() - session.started_ms;
        session.busy = false;
    }
    pthread_mutex_unlock(&orbit.mutex);
    return NULL;
}
int browser_start(void) {
    pthread_mutex_lock(&orbit.mutex);
    /* Readiness only. No thread or native service until an explicit request. */
    session.ready = true;
    pthread_mutex_unlock(&orbit.mutex);
    return 0;
}
static int start_worker_locked(void) {
    if (session.thread_started) return 0;
    pthread_attr_t attr;
    int rc = pthread_attr_init(&attr);
    if (rc) return rc;
    rc = pthread_attr_setstacksize(&attr, ORBIT_THREAD_STACK);
    if (!rc) rc = pthread_create(&session.thread, &attr, worker, NULL);
    pthread_attr_destroy(&attr);
    session.thread_started = !rc;
    return rc;
}
void browser_stop(void) {
    pthread_mutex_lock(&orbit.mutex);
    bool join = session.thread_started;
    session.stop = true;
    atomic_store(&session.cancel, true);
    pthread_cond_signal(&session.changed);
    pthread_mutex_unlock(&orbit.mutex);
    if (join) pthread_join(session.thread, NULL);
}
cJSON *browser_status(void) {
    cJSON *o = cJSON_CreateObject();
    /* Foreground-user lookup shares the system-service lock with browser
     * launching, whose diagnostics acquire orbit.mutex. Preserve lock order. */
    const char *browser_account = viking_cached_state(false);
    const char *native_account = viking_cached_state(true);
    pthread_mutex_lock(&orbit.mutex);
    cJSON_AddBoolToObject(o, "available", session.ready && available() && !session.stop);
    cJSON_AddBoolToObject(o, "active", session.busy);
    cJSON_AddBoolToObject(o, "vikingAccountAvailable", session.ready && available() && !session.stop);
    cJSON_AddStringToObject(o, "vikingBrowserState", browser_account);
    cJSON_AddStringToObject(o, "vikingNativeState", native_account);
    cJSON_AddBoolToObject(o, "accountPage", session.account);
    cJSON_AddBoolToObject(o, "nativeDialogAvailable", session.ready && available() && !session.stop);
    if (session.native_dialog) {
        cJSON_AddStringToObject(o, "clientId", session.client_id);
        cJSON_AddBoolToObject(o, "dialogOpened", session.dialog_opened);
        /* Only the catalogue's public page, never a captured/signed file URL. */
        if (session.busy && !session.resolving && !session.check_account && !session.dialog_opened && !atomic_load(&session.cancel))
            cJSON_AddStringToObject(o, "pageUrl", session.release.browser_url);
    }
    cJSON_AddStringToObject(o, "id", session.id);
    cJSON_AddStringToObject(o, "state", session.state[0] ? session.state : "idle");
    cJSON_AddStringToObject(o, "message", session.message);
    cJSON_AddStringToObject(o, "jobId", session.job_id);
    cJSON_AddStringToObject(o, "title", session.title);
    /* Durations/counters only. Never disclose browser bytes, file URLs, process
     * addresses, cookies or verification data through diagnostics. */
    if (session.started_ms) {
        cJSON *timing = cJSON_AddObjectToObject(o, "timing");
        cJSON_AddNumberToObject(timing, "elapsedMs", session.busy ? milliseconds() - session.started_ms : session.finished_ms);
        cJSON_AddNumberToObject(timing, "captureStartedMs", session.capture_ms);
        cJSON_AddNumberToObject(timing, "linkFoundMs", session.link_ms);
        cJSON_AddNumberToObject(timing, "validationMs", session.validation_ms);
        cJSON_AddNumberToObject(timing, "queuedMs", session.queued_ms);
        cJSON_AddNumberToObject(timing, "scannedBytes", (double)session.scanned_bytes);
        cJSON_AddNumberToObject(timing, "candidates", session.candidates);
        cJSON_AddNumberToObject(timing, "browserProcesses", session.browser_processes);
    }
    pthread_mutex_unlock(&orbit.mutex);
    return o;
}
int browser_action(const cJSON *input, char *error, size_t cap) {
    int code = 202;
    const char *action = json_text(input, "action");
    pthread_mutex_lock(&orbit.mutex);
    if (!strcmp(action, "opened")) {
        if (!session.busy || !session.native_dialog || atomic_load(&session.cancel) ||
            strcmp(json_text(input, "id"), session.id) ||
            strcmp(json_text(input, "clientId"), session.client_id)) {
            copy_text(error, cap, "This verification session is no longer active."); code = 409;
        } else {
            session.dialog_opened = true; /* idempotent acknowledgement */
            if (!strcmp(session.state, "opening")) {
                if (session.account) state_locked("waiting", "Sign in on Vikingfile, then return to Orbit.");
                else state_locked("waiting-link", "Complete verification if shown, then wait for Orbit to check the download link.");
            }
        }
    } else if (!strcmp(action, "cancel")) {
        const cJSON *failure = cJSON_GetObjectItemCaseSensitive(input, "nativeFailure");
        const cJSON *number = cJSON_GetObjectItemCaseSensitive(input, "errorCode");
        const char *message = failure && cJSON_IsString(failure)
            ? native_failure_message(failure->valuestring) : NULL;
        if ((failure && !message) || (number && (!failure || !cJSON_IsNumber(number) ||
            !(number->valuedouble >= INT32_MIN && number->valuedouble <= 0) ||
            number->valuedouble != (double)(int32_t)number->valuedouble))) {
            copy_text(error, cap, "Choose a supported browser failure."); code = 400;
            goto done;
        }
        if (!strcmp(session.state, "queued")) {
            copy_text(error, cap, "The file is already in Downloads. Manage it there."); code = 409;
        } else if (!session.busy || strcmp(json_text(input, "id"), session.id)) {
            copy_text(error, cap, "This verification session is no longer active."); code = 409;
        } else if (failure && (!session.native_dialog ||
                   strcmp(json_text(input, "clientId"), session.client_id))) {
            copy_text(error, cap, "This verification session belongs to another browser."); code = 409;
        } else {
            if (failure && !atomic_load(&session.cancel)) {
                copy_text(session.native_failure, sizeof session.native_failure, message);
                if (number && number->valuedouble < 0) {
                    size_t used = strlen(session.native_failure);
                    snprintf(session.native_failure + used, sizeof session.native_failure - used,
                             " Browser error: 0x%08X.", (unsigned)(int32_t)number->valuedouble);
                }
            }
            atomic_store(&session.cancel, true);
            if (session.native_failure[0]) state_locked("failed", session.native_failure);
            else state_locked("cancelled", "Verification cancelled. No download was added.");
        }
    } else if (strcmp(action, "start") && strcmp(action, "refresh") && strcmp(action, "viking-account") && strcmp(action, "viking-check")) {
        copy_text(error, cap, "Unknown browser action."); code = 400;
    } else if (!session.ready || session.stop || !available()) {
        copy_text(error, cap, "Provider verification is available on the PS5 only."); code = 409;
    } else if (session.busy) {
        copy_text(error, cap, "Finish or cancel the current verification first."); code = 409;
    } else {
        const cJSON *target = cJSON_GetObjectItemCaseSensitive(input, "returnTarget");
        const cJSON *legacy = cJSON_GetObjectItemCaseSensitive(input, "returnToOrbit");
        if ((target && (!cJSON_IsString(target) ||
                       (strcmp(target->valuestring, "native") && strcmp(target->valuestring, "browser")))) ||
            (legacy && !cJSON_IsBool(legacy)) || (target && legacy)) {
            copy_text(error, cap, "Choose a supported Orbit return target."); code = 400;
            goto done;
        }
        const cJSON *dialog = cJSON_GetObjectItemCaseSensitive(input, "nativeDialog");
        const char *client = json_text(input, "clientId");
        if ((dialog && !cJSON_IsBool(dialog)) || (cJSON_IsTrue(dialog) &&
            (!target || strcmp(target->valuestring, "native") || !*client || strlen(client) > 64 ||
             strspn(client, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != strlen(client)))) {
            copy_text(error, cap, "Choose a supported native browser session."); code = 400;
            goto done;
        }
        if (!strcmp(action, "viking-account") || !strcmp(action, "viking-check")) {
            /* A fixed official URL, never a caller-supplied URL or account hash. */
            if (start_worker_locked()) {
                copy_text(error, cap, "Provider verification could not start. Try again."); code = 503;
                goto done;
            }
            memset(&session.release, 0, sizeof session.release);
            memset(&session.refresh_job, 0, sizeof session.refresh_job);
            copy_text(session.release.browser_url, sizeof session.release.browser_url, "https://vikingfile.com/login");
            session.account = true;
            session.check_account = !strcmp(action, "viking-check");
            session.resolving = false;
            session.native_dialog = cJSON_IsTrue(dialog);
            session.dialog_opened = false;
            session.return_to_browser = false;
            session.native_failure[0] = session.job_id[0] = 0;
            copy_text(session.client_id, sizeof session.client_id, session.native_dialog ? client : "");
            copy_text(session.title, sizeof session.title, "Vikingfile");
            random_hex(session.id, 16);
            session.started_ms = milliseconds();
            session.capture_ms = session.link_ms = session.validation_ms = session.queued_ms = session.finished_ms = 0;
            session.scanned_bytes = session.candidates = session.browser_processes = 0;
            session.busy = session.pending = true;
            atomic_store(&session.cancel, false);
            state_locked(session.check_account ? "validating" : "opening", session.check_account ? "Checking sign-in…" : "Opening Vikingfile sign-in…");
            pthread_cond_signal(&session.changed);
            code = 202;
            goto done;
        }
        bool refreshing = !strcmp(action, "refresh");
        Job *job = refreshing ? job_find(json_text(input, "jobId")) : NULL;
        Release *r = refreshing ? (job ? &job->release : NULL) : release_find(json_text(input, "releaseId"));
        Storage drives[ORBIT_MAX_STORAGE], *s = NULL;
        size_t count = storage_list(drives);
        for (size_t i = 0; i < count; i++)
            if (!strcmp(drives[i].id, refreshing && job ? job->storage_id : json_text(input, "storageId"))) s = &drives[i];
        if (refreshing && (!job_can_refresh_link(job) || !source_enabled_locked(&job->release))) {
            copy_text(error, cap, "Pause the download and enable its source before refreshing its link."); code = 409;
        } else if (!r || !provider_browser_supported(r)) {
            copy_text(error, cap, "This option has no supported provider browser page."); code = 400;
        } else if (!s) {
            copy_text(error, cap, "Reconnect the selected drive first."); code = 409;
        } else if (refreshing && (strcmp(s->root, job->root) || s->device != job->device || s->inode != job->inode)) {
            copy_text(error, cap, "Reconnect the original drive first."); code = 409;
        } else if ((code = refreshing ? 200 : job_check_capture_locked(r, s, error, cap)) == 200) {
            if (start_worker_locked()) {
                copy_text(error, cap, "Provider verification could not start. Try again.");
                state_locked("failed", error);
                code = 503;
                goto done;
            }
            session.account = false;
            session.check_account = false;
            session.resolving = !strcmp(r->source, "vikingfile");
            session.release = *r; session.storage = *s;
            session.native_dialog = cJSON_IsTrue(dialog);
            session.dialog_opened = false;
            session.native_failure[0] = 0;
            copy_text(session.client_id, sizeof session.client_id, session.native_dialog ? client : "");
            session.return_to_browser = (target && !strcmp(target->valuestring, "browser")) || cJSON_IsTrue(legacy);
            memset(&session.refresh_job, 0, sizeof session.refresh_job);
            if (refreshing) session.refresh_job = *job;
            copy_text(session.title, sizeof session.title, r->title);
            random_hex(session.id, 16);
            session.job_id[0] = 0;
            session.started_ms = milliseconds();
            session.capture_ms = session.link_ms = session.validation_ms = 0;
            session.queued_ms = session.finished_ms = 0;
            session.scanned_bytes = 0;
            session.candidates = session.browser_processes = 0;
            session.busy = session.pending = true;
            atomic_store(&session.cancel, false);
            state_locked(session.resolving ? "validating" : "opening", session.resolving ? "Checking sign-in…" : "Opening the download page on your PS5…");
            pthread_cond_signal(&session.changed);
            code = 202;
        }
    }
done:
    pthread_mutex_unlock(&orbit.mutex);
    return code;
}

void browser_source_changed_locked(void) {
    if (session.busy && !session.account && !source_enabled_locked(&session.release)) atomic_store(&session.cancel, true);
}
void browser_job_changed_locked(const char *id) {
    if (session.busy && session.refresh_job.id[0] && !strcmp(session.refresh_job.id, id))
        atomic_store(&session.cancel, true);
}
