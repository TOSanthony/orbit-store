/* Paired-device release checks from the fixed feed. Installation and restart stay explicit:
 * "update-all" installs the releases the owner was shown, the TV app first, then restarts Orbit
 * through the jailbreak's ELF loader when the download service changed. */
#include "ca.h"
#include "orbit.h"
#include <arpa/inet.h>
#include <curl/curl.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define FEED "https://raw.githubusercontent.com/saawant12/orbit-store-ps5/main/payloads.json"
#define RELEASE_BASE "https://github.com/saawant12/orbit-store-ps5/releases/download/"
#define AUTO_CHECK_INTERVAL (6 * 60 * 60)
#define FIRST_CHECK_DELAY (10 * 60) /* Update notifications' first check, after Orbit starts. */
#define MAX_ELF (64 * 1024 * 1024)
#define LOADER_PORT 9021 /* The jailbreak's ELF loader, as the TV app uses it. */
static struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t thread;
    bool started, stop, busy;
    char action[16], phase[24], error[256];
    char version[64], url[512], checksum[65];
    time_t checked, retry_at, check_after, automatic_check_after, background_after;
    size_t received;
    bool saved;
    /* update-all: what the owner was shown, and how far it got. */
    bool all, all_service, tv_updated;
    char tv_version[40], tv_checksum[65];
} update = {.mutex = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER};

typedef struct {
    unsigned char *data;
    size_t size, limit;
    long retry;
    bool asset;
} Buffer;

static bool available(void) {
#ifdef ORBIT_TEST
    return orbit.autoboot;
#else
    return orbit.autoboot && !orbit.desktop;
#endif
}
static bool interrupted(void) {
    pthread_mutex_lock(&update.mutex);
    bool stop = update.stop;
    pthread_mutex_unlock(&update.mutex);
    return stop;
}
static size_t collect(char *data, size_t a, size_t b, void *context) {
    Buffer *buffer = context;
    size_t n = a * b;
    if (n > buffer->limit - buffer->size || interrupted())
        return 0;
    unsigned char *next = realloc(buffer->data, buffer->size + n + 1);
    if (!next)
        return 0;
    buffer->data = next;
    memcpy(next + buffer->size, data, n);
    buffer->size += n;
    next[buffer->size] = 0;
    if (buffer->asset) {
        pthread_mutex_lock(&update.mutex);
        update.received = buffer->size;
        pthread_mutex_unlock(&update.mutex);
    }
    return n;
}
static size_t headers(char *data, size_t a, size_t b, void *context) {
    Buffer *buffer = context;
    size_t n = a * b;
    if (n > 12 && !strncasecmp(data, "Retry-After:", 12)) {
        char value[100];
        size_t len = n - 12 < sizeof value - 1 ? n - 12 : sizeof value - 1;
        memcpy(value, data + 12, len);
        value[len] = 0;
        char *end;
        long seconds = strtol(value, &end, 10);
        if (end == value) {
            time_t date = curl_getdate(value, NULL);
            seconds = date > time(NULL) ? date - time(NULL) : 0;
        }
        if (seconds > buffer->retry)
            buffer->retry = seconds;
    }
    return n;
}
static int progress(void *ctx, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)ctx;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    return interrupted();
}
static bool test_url(const char *url) {
#ifdef ORBIT_TEST
    const char *base = getenv("ORBIT_TEST_UPDATE_BASE");
    return base && !strncmp(base, "http://127.0.0.1:", 17) && !strncmp(url, base, strlen(base)) &&
           url[strlen(base)] == '/';
#else
    (void)url;
    return false;
#endif
}
static bool permitted_url(const char *url, bool asset) {
    return test_url(url) ||
           (!asset ? !strcmp(url, FEED)
                   : !strncmp(url, RELEASE_BASE, strlen(RELEASE_BASE)) ||
                         !strncmp(url, "https://release-assets.githubusercontent.com/", 45));
}
static int fetch(const char *url, Buffer *buffer, char *error, size_t cap) {
    char current[4096];
    copy_text(current, sizeof current, url);
    for (unsigned redirects = 0; redirects <= 4; redirects++) {
        if (!permitted_url(current, buffer->asset)) {
            copy_text(error, cap, "The release redirected outside Orbit's allowed download hosts.");
            return -1;
        }
        CURL *curl = curl_easy_init();
        if (!curl)
            return -1;
        struct curl_blob ca = {(void *)orbit_ca, sizeof orbit_ca - 1, CURL_BLOB_COPY};
        buffer->size = 0;
        curl_easy_setopt(curl, CURLOPT_URL, current);
        curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &ca);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, test_url(current) ? "http" : "https");
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Orbit-Store/" ORBIT_VERSION);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headers);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, buffer);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        CURLcode result = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        char *location = NULL;
        curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &location);
        bool redirect = result == CURLE_OK && status >= 300 && status < 400 && location;
        if (redirect && strlen(location) < sizeof current)
            copy_text(current, sizeof current, location);
        else
            redirect = false;
        curl_easy_cleanup(curl);
        if (status == 403 || status == 429 || status == 503) {
            pthread_mutex_lock(&update.mutex);
            update.retry_at = time(NULL) + (buffer->retry > 60 ? buffer->retry : 60);
            pthread_mutex_unlock(&update.mutex);
        }
        if (redirect)
            continue;
        if (result == CURLE_OK && status == 200)
            return 0;
        snprintf(error, cap, "Release download failed (HTTP %ld, %s). Try again later.", status,
                 curl_easy_strerror(result));
        return -1;
    }
    copy_text(error, cap, "Too many release redirects.");
    return -1;
}
/* Releases use major.minor.patch or major.minor.patch-beta.number. */
static bool version_parts(const char *version, unsigned *parts) {
    if (*version == 'v')
        version++;
    parts[3] = 1;
    parts[4] = 0;
    for (int i = 0; i < 5; i++) {
        if (i == 3) {
            if (!*version)
                return true;
            if (strncmp(version, "-beta.", 6))
                return false;
            version += 6;
            parts[3] = 0;
            continue;
        }
        unsigned digits = 0, value = 0;
        while (*version >= '0' && *version <= '9') {
            if (++digits > 6)
                return false;
            value = value * 10 + (unsigned)(*version++ - '0');
        }
        if (!digits)
            return false;
        parts[i] = value;
        if (i < 2 && *version++ != '.')
            return false;
    }
    if (*version)
        return false;
    return true;
}
static int compare_version(const char *a, const char *b) {
    unsigned x[5], y[5];
    if (!version_parts(a, x) || !version_parts(b, y))
        return -2;
    for (int i = 0; i < 5; i++)
        if (x[i] != y[i])
            return x[i] > y[i] ? 1 : -1;
    return 0;
}
int orbit_version_compare(const char *a, const char *b) {
    return compare_version(a, b);
}
static int check(char *error, size_t cap) {
    const char *url = FEED;
#ifdef ORBIT_TEST
    char fixture[512];
    const char *base = getenv("ORBIT_TEST_UPDATE_BASE");
    if (base) {
        snprintf(fixture, sizeof fixture, "%s/feed", base);
        url = fixture;
    }
#endif
    Buffer buffer = {.limit = 65536};
    int rc = fetch(url, &buffer, error, cap);
    cJSON *feed = rc ? NULL : cJSON_Parse((char *)buffer.data);
    free(buffer.data);
    if (rc)
        return rc;
    cJSON *payloads = cJSON_GetObjectItemCaseSensitive(feed, "payloads"), *entry, *selected = NULL;
    cJSON_ArrayForEach(entry, payloads) {
        if (!strcmp(json_text(entry, "filename"), "orbit_store.elf")) {
            if (selected) {
                selected = NULL;
                break;
            }
            selected = entry;
        }
    }
    const char *version = json_text(selected, "version"), *asset = json_text(selected, "url"),
               *hash = json_text(selected, "checksum");
    char expected[512];
    snprintf(expected, sizeof expected, "%s%s/orbit_store.elf", RELEASE_BASE, version);
    if (!selected || !cJSON_IsArray(payloads) || compare_version(version, ORBIT_VERSION) < 0 ||
        (strcmp(asset, expected) && !test_url(asset)) || strlen(hash) != 64 ||
        strspn(hash, "0123456789abcdef") != 64) {
        cJSON_Delete(feed);
        copy_text(error, cap,
                  "No compatible Orbit release found. An older or invalid release will not replace "
                  "this build.");
        return -1;
    }
    pthread_mutex_lock(&update.mutex);
    copy_text(update.version, sizeof update.version, *version == 'v' ? version + 1 : version);
    copy_text(update.url, sizeof update.url, asset);
    copy_text(update.checksum, sizeof update.checksum, hash);
    update.checked = time(NULL);
    pthread_mutex_unlock(&update.mutex);
    cJSON_Delete(feed);
    return 0;
}
static int install(char *error, size_t cap) {
    Buffer buffer = {.limit = MAX_ELF, .asset = true};
    int rc = fetch(update.url, &buffer, error, cap);
    if (!rc) {
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned size = 0;
        char hex[65];
        if (!EVP_Digest(buffer.data, buffer.size, digest, &size, EVP_sha256(), NULL) || size != 32)
            rc = -1;
        else {
            for (unsigned i = 0; i < 32; i++)
                snprintf(hex + i * 2, 3, "%02x", digest[i]);
            /* ELF64, little endian, x86-64, executable/shared object. */
            rc = strcmp(hex, update.checksum) || buffer.size < 64 ||
                 memcmp(buffer.data, "\177ELF\2\1\1", 7) ||
                 (buffer.data[16] != 2 && buffer.data[16] != 3) || buffer.data[17] ||
                 buffer.data[18] != 62 || buffer.data[19];
        }
        if (rc)
            copy_text(
                error, cap,
                "The release failed SHA-256 or ELF validation. The saved copy was not replaced.");
    }
    if (!rc && interrupted())
        rc = -1;
    if (!rc) {
        pthread_mutex_lock(&update.mutex);
        copy_text(update.phase, sizeof update.phase, "saving");
        pthread_mutex_unlock(&update.mutex);
        rc = autoboot_install(buffer.data, buffer.size, update.version) < 0 ? -1 : 0;
        if (rc)
            copy_text(error, cap,
                      "Could not refresh every saved Orbit copy. Your running session is "
                      "unchanged; retry reinstall before restarting.");
    }
    free(buffer.data);
    return rc;
}
static int loader_port(void) {
#ifdef ORBIT_TEST
    const char *port = getenv("ORBIT_TEST_LOADER_PORT");
    return port ? atoi(port) : 0;
#elif defined(ORBIT_DESKTOP)
    return 0;
#else
    return LOADER_PORT;
#endif
}
/* A connection to the loader, made before Orbit stops so a missing loader never leaves it
 * stopped. The loader runs what it reads once this side closes. */
static int loader_connect(void) {
    int port = loader_port();
    if (port <= 0 || port > 65535)
        return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port)};
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    struct timeval wait = {.tv_sec = 10};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof wait);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof wait);
    if (connect(fd, (struct sockaddr *)&address, sizeof address)) {
        close(fd);
        return -1;
    }
    return fd;
}
static int send_all(int fd, const unsigned char *data, size_t size) {
    for (size_t sent = 0; sent < size;) {
#ifdef MSG_NOSIGNAL
        ssize_t n = send(fd, data + sent, size - sent, MSG_NOSIGNAL);
#else
        ssize_t n = send(fd, data + sent, size - sent, 0);
#endif
        if (n <= 0)
            return -1;
        sent += (size_t)n;
    }
    return 0;
}
int updates_restart_send(int loader, const unsigned char *fallback, size_t fallback_size) {
    unsigned char *saved = NULL;
    size_t size = 0;
    const unsigned char *image = fallback;
    if (!autoboot_saved_image(&saved, &size))
        image = saved;
    else
        size = fallback ? fallback_size : 0;
    int rc = size ? send_all(loader, image, size) : -1;
    if (rc && size) {
        /* The loader may have dropped a connection held through the shutdown. */
        close(loader);
        loader = loader_connect();
        rc = loader < 0 ? -1 : send_all(loader, image, size);
    }
    if (!rc) {
        shutdown(loader, SHUT_WR);
        char ignored[256];
        while (recv(loader, ignored, sizeof ignored, 0) > 0) {
        }
    }
    if (loader >= 0)
        close(loader);
    free(saved);
    if (rc)
        notify_restart_failed();
    return rc;
}
/* Pauses the queue and stops Orbit. With resume, the paused downloads continue after the
 * restart. Call with update.mutex held. */
static int stop_locked(bool resume, char *error, size_t cap) {
    pthread_mutex_lock(&orbit.mutex);
    char previous[ORBIT_MAX_JOBS][24];
    bool paused[ORBIT_MAX_JOBS];
    int code = 202;
    for (size_t i = 0; i < orbit.job_count; i++) {
        Job *job = &orbit.jobs[i];
        copy_text(previous[i], sizeof previous[i], job->status);
        paused[i] = job->pause;
        job->resume_after_restart = resume && job->id[0] && !job->pause &&
            (job_active(job) || !strcmp(job->status, "queued") || !strcmp(job->status, "retrying"));
        if (!strcmp(job->status, "queued") || !strcmp(job->status, "retrying"))
            copy_text(job->status, sizeof job->status, "paused");
    }
    if (state_save_locked()) {
        for (size_t i = 0; i < orbit.job_count; i++) {
            copy_text(orbit.jobs[i].status, sizeof orbit.jobs[i].status, previous[i]);
            orbit.jobs[i].pause = paused[i];
            orbit.jobs[i].resume_after_restart = false;
        }
        code = 503;
        copy_text(error, cap, "Could not save the queue. Orbit has not been stopped.");
    } else {
        /* Commit before publishing a stop to lock-free transfer callbacks. */
        for (size_t i = 0; i < orbit.job_count; i++)
            if (job_active(&orbit.jobs[i])) orbit.jobs[i].pause = true;
        orbit.stop = true;
        pthread_cond_signal(&orbit.changed);
        update.stop = true;
        copy_text(update.phase, sizeof update.phase, resume ? "restarting" : "stopping");
        pthread_cond_signal(&update.changed);
        orbit_request_stop();
    }
    pthread_mutex_unlock(&orbit.mutex);
    return code;
}
/* The TV app, then the download service, then a restart when the service changed. */
static int update_all(char *error, size_t cap) {
    pthread_mutex_lock(&update.mutex);
    bool tv = *update.tv_version, service = update.all_service;
    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "action", "install");
    cJSON_AddStringToObject(request, "version", update.tv_version);
    cJSON_AddStringToObject(request, "checksum", update.tv_checksum);
    if (tv)
        copy_text(update.phase, sizeof update.phase, "tv-app");
    pthread_mutex_unlock(&update.mutex);
    int rc = 0;
    if (tv) {
        /* An automatic check may be running: let it finish, then install. */
        char ignored[8];
        tvapp_wait(ignored, sizeof ignored);
        int code = tvapp_action(request, error, cap);
        rc = code != 202 ? -1 : tvapp_wait(error, cap);
    }
    cJSON_Delete(request);
    if (rc || interrupted())
        return -1;
    pthread_mutex_lock(&update.mutex);
    update.tv_updated = tv;
    if (service)
        copy_text(update.phase, sizeof update.phase, "downloading");
    pthread_mutex_unlock(&update.mutex);
    if (!service)
        return 0;
    if (install(error, cap))
        return -1;
    int loader = loader_connect();
    pthread_mutex_lock(&update.mutex);
    update.saved = true;
    int code = loader < 0 || update.stop || orbit_is_stopping() ? 409 : stop_locked(true, error, cap);
    pthread_mutex_unlock(&update.mutex);
    if (code == 202)
        orbit_restart_after_stop(loader);
    else if (loader >= 0)
        close(loader);
    /* Without a loader the update stays saved; the owner restarts Orbit themselves. */
    return code == 202 || code == 409 ? 0 : -1;
}
static time_t later(time_t a, time_t b) { return a > b ? a : b; }
/* Update notifications need checks of their own, as no client may be open: spaced like the
 * clients' automatic checks, the first a few minutes after Orbit starts. Returns whether one began. */
static bool background_check_locked(void) {
    time_t due = later(later(update.background_after, update.automatic_check_after),
                       later(update.check_after, update.retry_at));
    pthread_mutex_unlock(&update.mutex);
    bool wanted = available() && notify_wanted(NOTIFY_UPDATE_AVAILABLE);
    pthread_mutex_lock(&update.mutex);
    if (update.action[0] || update.stop)
        return false;
    if (!wanted) {
        pthread_cond_wait(&update.changed, &update.mutex);
        return false;
    }
    if (time(NULL) < due || update.busy) {
        struct timespec deadline = {.tv_sec = due};
        pthread_cond_timedwait(&update.changed, &update.mutex, &deadline);
        return false;
    }
    update.busy = true;
    update.error[0] = 0;
    update.received = 0;
    update.checked = 0;
    update.version[0] = 0;
    update.checksum[0] = 0;
    update.all = false;
    update.check_after = time(NULL) + 60;
    update.automatic_check_after = time(NULL) + AUTO_CHECK_INTERVAL;
    copy_text(update.action, sizeof update.action, "check");
    copy_text(update.phase, sizeof update.phase, "checking");
    return true;
}
static void *worker(void *unused) {
    (void)unused;
    pthread_mutex_lock(&update.mutex);
    while (!update.stop) {
        while (!update.action[0] && !update.stop)
            if (background_check_locked())
                break;
        if (update.stop)
            break;
        bool checking = !strcmp(update.action, "check"), all = !strcmp(update.action, "update-all");
        update.action[0] = 0;
        pthread_mutex_unlock(&update.mutex);
        char error[256] = {0};
        int rc = checking ? check(error, sizeof error) : all ? update_all(error, sizeof error) : install(error, sizeof error);
        pthread_mutex_lock(&update.mutex);
        copy_text(update.error, sizeof update.error,
                  rc ? (*error ? error : "Update interrupted. Try again.") : "");
        if (rc || !all || strcmp(update.phase, "restarting"))
            copy_text(update.phase, sizeof update.phase,
                      rc ? "error" : checking ? "checked" : all && !update.all_service ? "updated" : "saved");
        if (!checking && !all && !rc)
            update.saved = true;
        update.busy = false;
        /* Any check that finds a newer release, the owner's or a background one, announces it once. */
        char announce[sizeof update.version] = "";
        if (checking && !rc && compare_version(update.version, ORBIT_VERSION) > 0)
            copy_text(announce, sizeof announce, update.version);
        if (*announce) {
            pthread_mutex_unlock(&update.mutex);
            notify_update(announce);
            pthread_mutex_lock(&update.mutex);
        }
    }
    pthread_mutex_unlock(&update.mutex);
    return NULL;
}
void updates_wake(void) {
    pthread_mutex_lock(&update.mutex);
    pthread_cond_signal(&update.changed);
    pthread_mutex_unlock(&update.mutex);
}
int updates_start(void) {
    time_t delay = FIRST_CHECK_DELAY;
#ifdef ORBIT_TEST
    const char *seconds = getenv("ORBIT_TEST_UPDATE_DELAY");
    if (seconds) delay = atoi(seconds);
#endif
    pthread_mutex_lock(&update.mutex);
    update.background_after = time(NULL) + delay;
    pthread_mutex_unlock(&update.mutex);
    pthread_attr_t attr;
    if (pthread_attr_init(&attr))
        return -1;
    int rc = pthread_attr_setstacksize(&attr, ORBIT_THREAD_STACK);
    if (!rc)
        rc = pthread_create(&update.thread, &attr, worker, NULL);
    pthread_attr_destroy(&attr);
    pthread_mutex_lock(&update.mutex);
    update.started = !rc;
    if (rc)
        copy_text(update.error, sizeof update.error,
                  "Could not start the update worker. Restart Orbit to retry.");
    pthread_mutex_unlock(&update.mutex);
    return rc ? -1 : 0;
}
void updates_stop(void) {
    pthread_mutex_lock(&update.mutex);
    update.stop = true;
    pthread_cond_signal(&update.changed);
    bool started = update.started;
    pthread_mutex_unlock(&update.mutex);
    if (started)
        pthread_join(update.thread, NULL);
}
cJSON *updates_status(void) {
    cJSON *o = cJSON_CreateObject();
    char saved[64] = {0};
    if (orbit.autoboot)
        autoboot_saved_version(saved, sizeof saved);
    pthread_mutex_lock(&update.mutex);
    cJSON_AddBoolToObject(o, "available", available() && update.started);
    cJSON_AddStringToObject(o, "runningVersion", ORBIT_VERSION);
    cJSON_AddStringToObject(o, "instanceId", orbit.instance_id);
    cJSON_AddStringToObject(o, "savedVersion", saved);
    cJSON_AddStringToObject(o, "latestVersion", update.version);
    cJSON_AddStringToObject(o, "checksum", update.checksum);
    cJSON_AddBoolToObject(o, "updateAvailable", update.checked && compare_version(update.version, ORBIT_VERSION) > 0);
    cJSON_AddNumberToObject(o, "automaticCheckAfter", (double)update.automatic_check_after);
    cJSON_AddStringToObject(o, "phase", *update.phase ? update.phase : "idle");
    cJSON_AddStringToObject(o, "error", update.error);
    cJSON_AddBoolToObject(o, "busy", update.busy);
    cJSON_AddBoolToObject(o, "restartRequired",
                          update.saved || (*saved && strcmp(saved, ORBIT_VERSION)));
    cJSON_AddNumberToObject(o, "received", (double)update.received);
    cJSON_AddNumberToObject(o, "retryAt", (double)update.retry_at);
    cJSON_AddNumberToObject(o, "checkedAt", (double)update.checked);
    cJSON_AddNumberToObject(o, "checkAfter", (double)update.check_after);
    /* Orbit restarts itself through the ELF loader ("restart"), and installs the TV app and
     * itself in one step ("update-all"). */
    cJSON_AddBoolToObject(o, "selfRestart", loader_port() > 0);
    cJSON_AddBoolToObject(o, "updateAll", update.all);
    cJSON_AddStringToObject(o, "tvAppUpdated", update.tv_updated ? update.tv_version : "");
    pthread_mutex_unlock(&update.mutex);
    return o;
}
int updates_action(const cJSON *input, char *error, size_t cap) {
    const char *action = json_text(input, "action");
    bool checking = !strcmp(action, "check"), installing = !strcmp(action, "install"),
         stopping = !strcmp(action, "stop"), restarting = !strcmp(action, "restart"),
         all = !strcmp(action, "update-all");
    const char *tv_version = json_text(input, "tvAppVersion"), *tv_checksum = json_text(input, "tvAppChecksum"),
               *service_version = json_text(input, "serviceVersion");
    const cJSON *automatic_input = cJSON_GetObjectItemCaseSensitive(input, "automatic");
    bool automatic = checking && cJSON_IsTrue(automatic_input);
    int code = 202;
    pthread_mutex_lock(&update.mutex);
    if (automatic_input && (!checking || !cJSON_IsBool(automatic_input))) {
        code = 400;
        copy_text(error, cap, "Automatic mode is only valid for release checks.");
    } else if (!checking && !installing && !stopping && !restarting && !all) {
        code = 400;
        copy_text(error, cap, "Unknown update action.");
    } else if (all && !*tv_version && !*service_version) {
        code = 400;
        copy_text(error, cap, "Choose the releases to install.");
    } else if (!available() || !update.started) {
        code = 409;
        copy_text(error, cap, "Updates are available from Orbit running on your PS5.");
    } else if (automatic && !update.stop &&
               (update.busy || update.automatic_check_after > time(NULL) ||
                update.retry_at > time(NULL) || update.check_after > time(NULL))) {
        /* Opening more tabs or paired devices shares the console's cooldown. */
        code = 202;
    } else if (update.busy || update.stop) {
        code = 409;
        copy_text(error, cap, "An Orbit update is in progress. Wait for it to finish.");
    } else if ((!stopping && !restarting && update.retry_at > time(NULL)) ||
               (checking && update.check_after > time(NULL))) {
        code = 429;
        copy_text(error, cap, "Please wait before checking or downloading again.");
    } else if ((stopping || restarting) &&
               !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(input, "confirmed"))) {
        code = 400;
        copy_text(error, cap, "Confirm stopping Orbit and pausing downloads first.");
    } else if ((installing || (all && *service_version)) &&
               (!update.checked || time(NULL) - update.checked > 900 ||
                strcmp(installing ? json_text(input, "version") : service_version, update.version) ||
                strcmp(json_text(input, installing ? "checksum" : "serviceChecksum"), update.checksum))) {
        code = 409;
        copy_text(error, cap, "Check for updates again before installing this release.");
    } else if (all && *tv_version && (strlen(tv_version) >= sizeof update.tv_version ||
                                      strlen(tv_checksum) != 64)) {
        code = 400;
        copy_text(error, cap, "Check for the TV app again before installing it.");
    } else if (stopping) {
        /* Persist the paused queue before acknowledging shutdown. */
        code = stop_locked(false, error, cap);
    } else if (restarting) {
        int loader = loader_connect();
        if (loader < 0) {
            code = 409;
            copy_text(error, cap,
                      "Orbit can't restart itself because no ELF loader answers on port 9021. Stop "
                      "Orbit, then start it from your payload manager.");
        } else {
            code = stop_locked(true, error, cap);
            if (code == 202)
                orbit_restart_after_stop(loader);
            else
                close(loader);
        }
    } else {
        update.busy = true;
        update.error[0] = 0;
        update.received = 0;
        if (checking) {
            update.checked = 0;
            update.version[0] = 0;
            update.checksum[0] = 0;
        }
        if (checking) {
            update.check_after = time(NULL) + 60;
            update.automatic_check_after = time(NULL) + AUTO_CHECK_INTERVAL;
        } else
            update.retry_at = time(NULL) + 60;
        update.all = all;
        if (all) {
            update.all_service = *service_version;
            update.tv_updated = false;
            copy_text(update.tv_version, sizeof update.tv_version, tv_version);
            copy_text(update.tv_checksum, sizeof update.tv_checksum, tv_checksum);
        }
        copy_text(update.action, sizeof update.action, action);
        copy_text(update.phase, sizeof update.phase,
                  checking ? "checking" : all && *tv_version ? "tv-app" : "downloading");
        pthread_cond_signal(&update.changed);
    }
    pthread_mutex_unlock(&update.mutex);
    return code;
}
