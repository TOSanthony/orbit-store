/* ntfy push notifications (see ntfy.h). One sender thread, started by the first
 * notification, posts them in order so a slow server never holds up a download. */
#include "ntfy.h"
#include "ca.h"
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define SERVER_CAP 257
#define TOPIC_CAP 65
#define SECRET_CAP 129
#define QUEUE_CAP 8
typedef struct {
    bool enabled;
    char server[SERVER_CAP], topic[TOPIC_CAP], username[SECRET_CAP], password[SECRET_CAP];
} Settings;
typedef struct {
    char text[512];
    NtfyKind kind;
} Message;
/* Its own lock, never held while taking orbit.mutex or waiting on the network. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static Settings settings;
static Message queue[QUEUE_CAP];
static size_t queued;
static pthread_t sender;
static bool started, stopping;

static bool printable(const char *s, size_t max) {
    size_t n = strlen(s);
    if (n > max) return false;
    for (const unsigned char *p = (const void *)s; *p; p++)
        if (*p < 0x20 || *p == 0x7f) return false;
    return true;
}
/* An http(s) address with a host, no spaces, query or fragment; a trailing slash is dropped. */
static bool server_ok(const char *s, char out[SERVER_CAP]) {
    size_t scheme = !strncmp(s, "https://", 8) ? 8 : !strncmp(s, "http://", 7) ? 7 : 0;
    size_t n = strlen(s);
    while (n > scheme && s[n - 1] == '/') n--;
    if (!scheme || n <= scheme || n >= SERVER_CAP || !printable(s, SERVER_CAP - 1)) return false;
    for (size_t i = scheme; i < n; i++)
        if (s[i] == ' ' || s[i] == '?' || s[i] == '#' || s[i] == '@' || s[i] == '\\') return false;
    memcpy(out, s, n);
    out[n] = 0;
    return true;
}
/* ntfy topics: letters, digits, hyphens and underscores. */
static bool topic_ok(const char *s) {
    size_t n = strlen(s);
    return n > 0 && n < TOPIC_CAP &&
        strspn(s, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") == n;
}
static void forget(Settings *s) { OPENSSL_cleanse(s, sizeof *s); }

static int persist(const Settings *s) {
    if (!s->server[0] && !s->topic[0] && !s->enabled && !s->username[0] && !s->password[0]) {
        if (unlinkat(orbit.state_fd, "ntfy.json", 0) && errno != ENOENT) return -1;
        return fsync(orbit.state_fd);
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "schema", 1);
    cJSON_AddBoolToObject(o, "enabled", s->enabled);
    cJSON_AddStringToObject(o, "server", s->server);
    cJSON_AddStringToObject(o, "topic", s->topic);
    cJSON_AddStringToObject(o, "username", s->username);
    cJSON_AddStringToObject(o, "password", s->password);
    char *json = cJSON_PrintUnformatted(o);
    cJSON *secret = cJSON_GetObjectItemCaseSensitive(o, "password");
    if (secret && secret->valuestring) OPENSSL_cleanse(secret->valuestring, strlen(secret->valuestring));
    cJSON_Delete(o);
    if (!json) return -1;
    char name[64], random[17];
    random_hex(random, 8);
    snprintf(name, sizeof name, "ntfy-%s.tmp", random);
    int fd = openat(orbit.state_fd, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600), rc = -1;
    if (fd >= 0) {
        size_t size = strlen(json), done = 0;
        while (done < size) {
            ssize_t n = write(fd, json + done, size - done);
            if (n <= 0) break;
            done += (size_t)n;
        }
        if (done == size && !fsync(fd)) rc = 0;
        if (close(fd)) rc = -1;
        if (!rc) rc = renameat(orbit.state_fd, name, orbit.state_fd, "ntfy.json");
        if (!rc) rc = fsync(orbit.state_fd);
        unlinkat(orbit.state_fd, name, 0);
    }
    OPENSSL_cleanse(json, strlen(json));
    free(json);
    return rc;
}
void ntfy_load(void) {
    int fd = openat(orbit.state_fd, "ntfy.json", O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return;
    struct stat st;
    char buf[2048] = {0};
    if (!fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_nlink == 1 && st.st_size > 0 &&
        st.st_size < (off_t)sizeof buf && read(fd, buf, (size_t)st.st_size) == st.st_size) {
        cJSON *o = cJSON_Parse(buf);
        Settings s = {0};
        const char *username = json_text(o, "username"), *password = json_text(o, "password");
        const char *server = json_text(o, "server"), *topic = json_text(o, "topic");
        const bool enabled = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "enabled"));
        /* As ntfy_configure saves them: switched off, a server or topic may be missing. */
        if (json_int(o, "schema") == 1 && (*server ? server_ok(server, s.server) : !enabled) &&
            (*topic ? topic_ok(topic) : !enabled) &&
            printable(username, SECRET_CAP - 1) && printable(password, SECRET_CAP - 1)) {
            s.enabled = enabled;
            copy_text(s.topic, sizeof s.topic, json_text(o, "topic"));
            copy_text(s.username, sizeof s.username, username);
            copy_text(s.password, sizeof s.password, password);
            pthread_mutex_lock(&lock);
            settings = s;
            pthread_mutex_unlock(&lock);
        }
        forget(&s);
        cJSON *secret = cJSON_GetObjectItemCaseSensitive(o, "password");
        if (secret && secret->valuestring) OPENSSL_cleanse(secret->valuestring, strlen(secret->valuestring));
        cJSON_Delete(o);
    }
    OPENSSL_cleanse(buf, sizeof buf);
    close(fd);
}
bool ntfy_enabled(void) {
    pthread_mutex_lock(&lock);
    bool on = settings.enabled;
    pthread_mutex_unlock(&lock);
    return on;
}
cJSON *ntfy_status(void) {
    cJSON *o = cJSON_CreateObject();
    pthread_mutex_lock(&lock);
    cJSON_AddBoolToObject(o, "enabled", settings.enabled);
    cJSON_AddStringToObject(o, "server", settings.server);
    cJSON_AddStringToObject(o, "topic", settings.topic);
    cJSON_AddStringToObject(o, "username", settings.username);
    cJSON_AddBoolToObject(o, "hasPassword", settings.password[0] != 0);
    pthread_mutex_unlock(&lock);
    return o;
}
int ntfy_configure(const cJSON *input, char *error, size_t cap) {
    const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(input, "enabled");
    const cJSON *auth = cJSON_GetObjectItemCaseSensitive(input, "auth");
    const char *server = json_text(input, "server"), *topic = json_text(input, "topic");
    const char *username = json_text(input, "username"), *password = json_text(input, "password");
    Settings next = {0};
    int code = 200;
    if (!cJSON_IsBool(enabled) || (auth && !cJSON_IsBool(auth))) {
        copy_text(error, cap, "Expected enabled and auth.");
        code = 400;
    } else if ((*server || cJSON_IsTrue(enabled)) && !server_ok(server, next.server)) {
        copy_text(error, cap, "Enter your ntfy server’s address, starting with https:// or http://.");
        code = 400;
    } else if ((*topic || cJSON_IsTrue(enabled)) && !topic_ok(topic)) {
        copy_text(error, cap, "Enter a topic of up to 64 letters, numbers, hyphens or underscores.");
        code = 400;
    } else if (cJSON_IsTrue(auth) && (!printable(username, SECRET_CAP - 1) || !printable(password, SECRET_CAP - 1) || strchr(username, ':'))) {
        copy_text(error, cap, "Use a username and password of up to 128 characters each. The username can’t contain a colon.");
        code = 400;
    }
    if (code != 200) { forget(&next); return code; }
    next.enabled = cJSON_IsTrue(enabled);
    copy_text(next.topic, sizeof next.topic, topic);
    pthread_mutex_lock(&lock);
    if (cJSON_IsTrue(auth)) {
        copy_text(next.username, sizeof next.username, username);
        /* The saved password stays unless a new one is typed, for the same username. */
        if (*password) copy_text(next.password, sizeof next.password, password);
        else if (!strcmp(username, settings.username)) copy_text(next.password, sizeof next.password, settings.password);
        if (!next.password[0]) {
            pthread_mutex_unlock(&lock);
            forget(&next);
            copy_text(error, cap, "Enter the password for this ntfy username, or turn off authentication.");
            return 400;
        }
    }
    if (persist(&next)) {
        copy_text(error, cap, "Could not save the ntfy settings on this console.");
        code = 503;
    } else settings = next;
    pthread_mutex_unlock(&lock);
    forget(&next);
    return code;
}

static int progress(void *unused, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)unused; (void)a; (void)b; (void)c; (void)d;
    pthread_mutex_lock(&lock);
    bool stop = stopping;
    pthread_mutex_unlock(&lock);
    return stop;
}
static size_t discard(char *data, size_t a, size_t b, void *unused) { (void)data; (void)unused; return a * b; }
/* One POST to <server>/<topic>; the HTTP status, or 0 when the server couldn't be reached. */
static long post(const Settings *s, const char *message, NtfyKind kind, bool abortable) {
    char url[SERVER_CAP + TOPIC_CAP + 1];
    snprintf(url, sizeof url, "%s/%s", s->server, s->topic);
    CURL *curl = curl_easy_init();
    if (!curl) return 0;
    struct curl_slist *h = curl_slist_append(NULL, "Title: Orbit Store");
    h = curl_slist_append(h, kind == NTFY_FAILED ? "Tags: warning" : kind == NTFY_UPDATE ? "Tags: arrow_up" : "Tags: white_check_mark");
    if (kind == NTFY_FAILED) h = curl_slist_append(h, "Priority: high");
    struct curl_blob ca = {(void *)orbit_ca, sizeof orbit_ca - 1, CURL_BLOB_COPY};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &ca);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    /* A self-hosted server is often on the local network, without TLS. */
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, message);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "OrbitStore/" ORBIT_VERSION);
    if (s->password[0]) {
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERNAME, s->username);
        curl_easy_setopt(curl, CURLOPT_PASSWORD, s->password);
    }
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    /* Under the TV app's 8-second and the browser's 12-second request limits. */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 7L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard);
    if (abortable) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
    }
    long status = 0;
    if (curl_easy_perform(curl) == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    curl_slist_free_all(h);
    return status;
}
int ntfy_test(const char *message, char *error, size_t cap) {
    pthread_mutex_lock(&lock);
    Settings s = settings;
    pthread_mutex_unlock(&lock);
    int code = 200;
    if (!s.server[0] || !s.topic[0]) {
        copy_text(error, cap, "Save an ntfy server and topic first.");
        code = 409;
    } else {
        long status = post(&s, message, NTFY_DONE, false);
        if (status >= 200 && status < 300) code = 200;
        else {
            code = 502;
            copy_text(error, cap,
                      !status ? "Orbit couldn’t reach the ntfy server. Check its address and your network."
                      : status == 401 || status == 403 ? "The ntfy server didn’t accept this username and password, or this topic needs them."
                      : status == 429 ? "The ntfy server is limiting notifications. Try again in a minute."
                                      : "The ntfy server refused the notification. Check its address and topic.");
        }
    }
    forget(&s);
    return code;
}
static void *run(void *unused) {
    (void)unused;
    pthread_mutex_lock(&lock);
    while (!stopping) {
        if (!queued) { pthread_cond_wait(&wake, &lock); continue; }
        Message m = queue[0];
        memmove(queue, queue + 1, --queued * sizeof *queue);
        Settings s = settings;
        pthread_mutex_unlock(&lock);
        /* Failures are not retried: a later download's notification would arrive first. */
        if (s.enabled && s.server[0] && s.topic[0]) post(&s, m.text, m.kind, true);
        forget(&s);
        pthread_mutex_lock(&lock);
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}
void ntfy_send(const char *message, NtfyKind kind) {
    pthread_mutex_lock(&lock);
    if (settings.enabled && !stopping) {
        if (!started) {
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setstacksize(&attr, ORBIT_THREAD_STACK);
            started = !pthread_create(&sender, &attr, run, NULL);
            pthread_attr_destroy(&attr);
        }
        /* A full queue drops its oldest notification rather than block a download. */
        if (queued == QUEUE_CAP) memmove(queue, queue + 1, --queued * sizeof *queue);
        copy_text(queue[queued].text, sizeof queue[queued].text, message);
        queue[queued++].kind = kind;
        pthread_cond_signal(&wake);
    }
    pthread_mutex_unlock(&lock);
}
void ntfy_stop(void) {
    pthread_mutex_lock(&lock);
    stopping = true;
    pthread_cond_signal(&wake);
    bool join = started;
    pthread_mutex_unlock(&lock);
    if (join) pthread_join(sender, NULL);
    pthread_mutex_lock(&lock);
    forget(&settings);
    started = false;
    pthread_mutex_unlock(&lock);
}
