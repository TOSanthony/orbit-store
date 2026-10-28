/* Source-specific, bounded preflight for the user's Prefer PS5 setting. */
#include "zero_probe.h"
#include "archive_account.h"
#include "ca.h"
#include "provider_network.h"
#include <curl/curl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static pthread_t thread;
static bool started, stopping, pending, checking, reachable;
static Release release;
typedef struct {
    size_t bytes;
    bool range;
    int64_t total;
} Probe;
static size_t body(char *data, size_t size, size_t count, void *context) {
    (void)data;
    Probe *p = context;
    size_t n = size * count;
    if (n > 1 - p->bytes)
        return 0;
    p->bytes += n;
    return n;
}
static size_t header(char *data, size_t size, size_t count, void *context) {
    Probe *p = context;
    size_t n = size * count;
    if (n >= 5 && !strncasecmp(data, "HTTP/", 5)) {
        p->range = false;
        p->bytes = 0;
    }
    if (n > 14 && !strncasecmp(data, "Content-Range:", 14)) {
        char line[160];
        size_t take = n < sizeof line - 1 ? n : sizeof line - 1;
        memcpy(line, data, take);
        line[take] = 0;
        int64_t a = -1, b = -1, total = -1;
        if (sscanf(line + 14, " bytes %" SCNd64 "-%" SCNd64 "/%" SCNd64, &a, &b, &total) == 3 &&
            a == 0 && b == 0 && total == p->total)
            p->range = true;
    }
    return n;
}
static bool test_source(const Release *r) {
    if (!provider_supported(r))
        return false;
    CURL *c = curl_easy_init();
    if (!c)
        return false;
    struct curl_blob ca = {(void *)orbit_ca, sizeof orbit_ca - 1, CURL_BLOB_COPY};
    Probe probe = {.total = r->size};
    curl_easy_setopt(c, CURLOPT_URL, r->url);
    curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &ca);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
#ifdef ORBIT_TEST
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http");
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "http");
#else
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#endif
    curl_easy_setopt(c, CURLOPT_OPENSOCKETFUNCTION, provider_open_socket);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_RANGE, "0-0");
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "identity");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 4L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 8L);
    /* A login-only Archive.org file is reachable when the console is signed in. */
    if (!strcmp(r->source, "archive"))
        archive_account_apply(c);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, body);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &probe);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, header);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &probe);
    CURLcode result = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    char *type = NULL;
    curl_easy_getinfo(c, CURLINFO_CONTENT_TYPE, &type);
    bool ok = result == CURLE_OK && status == 206 && probe.range && probe.bytes == 1 &&
              (!type || (!strstr(type, "text/html") && !strstr(type, "application/json")));
    curl_easy_cleanup(c);
    return ok;
}
static void *worker(void *unused) {
    (void)unused;
    pthread_mutex_lock(&lock);
    while (!stopping) {
        while (!pending && !stopping)
            pthread_cond_wait(&wake, &lock);
        if (stopping)
            break;
        Release r = release;
        pending = false;
        pthread_mutex_unlock(&lock);
        bool ok = test_source(&r);
        pthread_mutex_lock(&lock);
        reachable = ok;
        checking = false;
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}
int zero_probe_start(void) {
    pthread_attr_t attr;
    if (pthread_attr_init(&attr))
        return -1;
    int rc = pthread_attr_setstacksize(&attr, ORBIT_THREAD_STACK);
    if (!rc)
        rc = pthread_create(&thread, &attr, worker, NULL);
    pthread_attr_destroy(&attr);
    started = !rc;
    return rc;
}
void zero_probe_stop(void) {
    if (!started)
        return;
    pthread_mutex_lock(&lock);
    stopping = true;
    pthread_cond_signal(&wake);
    pthread_mutex_unlock(&lock);
    pthread_join(thread, NULL);
    started = false;
}
int zero_probe_request(const char *id, char *error, size_t cap) {
    Release r;
    pthread_mutex_lock(&orbit.mutex);
    const Release *found = release_find(id);
    bool enabled = found && source_enabled_locked(found);
    if (found)
        r = *found;
    pthread_mutex_unlock(&orbit.mutex);
    if (!enabled) {
        copy_text(error, cap,
                  "Enable this source in Orbit before choosing a direct console download.");
        return 409;
    }
    pthread_mutex_lock(&lock);
    int code = 202;
    if (!started) {
        code = 503;
        copy_text(error, cap, "Source preflight is unavailable.");
    } else if (checking) {
        code = 409;
        copy_text(error, cap, "A source check is already running. Try again shortly.");
    } else {
        release = r;
        pending = true;
        checking = true;
        reachable = false;
        pthread_cond_signal(&wake);
    }
    pthread_mutex_unlock(&lock);
    return code;
}
cJSON *zero_probe_status(void) {
    pthread_mutex_lock(&lock);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "releaseId", release.id);
    cJSON_AddStringToObject(o, "state",
                            checking        ? "checking"
                            : release.id[0] ? "complete"
                                            : "idle");
    cJSON_AddBoolToObject(o, "reachable", reachable);
    pthread_mutex_unlock(&lock);
    return o;
}
