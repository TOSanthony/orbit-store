/* Archive.org sign-in for login-only files (see archive_account.h). */
#include "archive_account.h"
#include "ca.h"
#include "provider_network.h"
#include <errno.h>
#include <fcntl.h>
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define COOKIE_CAP 1024
#define NAME_CAP 96
#define REPLY_MAX (64 * 1024)
#define UNREACHABLE "Orbit couldn't reach Archive.org. Check your connection and try again."
#define UNEXPECTED "Archive.org sent an unexpected reply. Try again later."
/* Its own lock: downloads read the session without taking orbit.mutex. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    bool busy;
    char user[COOKIE_CAP], sig[COOKIE_CAP], name[NAME_CAP];
} account;
typedef struct {
    char *body;
    size_t length;
} Reply;

static const char *base(void) {
#ifdef ORBIT_TEST
    const char *b = getenv("ORBIT_TEST_ARCHIVE_URL");
    return b && !strncmp(b, "http://127.0.0.1:", 17) ? b : NULL;
#else
    return "https://archive.org";
#endif
}
static size_t collect(char *data, size_t a, size_t b, void *ctx) {
    Reply *r = ctx;
    if (a && b > SIZE_MAX / a) return 0;
    size_t n = a * b;
    if (n > REPLY_MAX - r->length) return 0;
    char *next = realloc(r->body, r->length + n + 1);
    if (!next) return 0;
    r->body = next;
    memcpy(next + r->length, data, n);
    r->length += n;
    next[r->length] = 0;
    return n;
}
/* One request to Archive.org itself: no redirects, no LAN, bounded reply. */
static cJSON *call(const char *path, const char *form, long *status) {
    const char *b = base();
    char url[1024];
    *status = 0;
    if (!b || snprintf(url, sizeof url, "%s%s", b, path) >= (int)sizeof url) return NULL;
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    Reply reply = {0};
    struct curl_slist *h = curl_slist_append(NULL, "Accept: application/json");
    struct curl_blob ca = {(void *)orbit_ca, sizeof orbit_ca - 1, CURL_BLOB_COPY};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &ca);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
#ifdef ORBIT_TEST
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
#endif
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, provider_open_socket);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    if (form) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "OrbitStore/" ORBIT_VERSION);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    /* Under the TV app's 8-second and the browser's 12-second request limits. */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 7L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply);
    CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status);
    curl_easy_cleanup(curl);
    curl_slist_free_all(h);
    cJSON *json = rc == CURLE_OK && reply.body ? cJSON_Parse(reply.body) : NULL;
    if (reply.body) {
        OPENSSL_cleanse(reply.body, reply.length);
        free(reply.body);
    }
    return json;
}
/* Archive.org returns "value; expires=…; path=/; domain=.archive.org". Keep
 * the value, and only if it is a plain cookie value. */
static bool cookie_value(const char *raw, char *out) {
    size_t n = strcspn(raw, ";");
    while (n && raw[n - 1] == ' ') n--;
    if (!n || n >= COOKIE_CAP) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)raw[i];
        if (c < 0x21 || c > 0x7e || c == '"' || c == ',' || c == '\\') return false;
    }
    memcpy(out, raw, n);
    out[n] = 0;
    return true;
}
static bool text_ok(const char *s, size_t min, size_t max, bool spaces) {
    size_t n = strlen(s);
    if (n < min || n > max) return false;
    for (const unsigned char *p = (const void *)s; *p; p++)
        if (*p < 0x20 || *p == 0x7f || (!spaces && *p == ' ')) return false;
    return true;
}
static int persist(void) {
    if (!account.user[0]) {
        if (unlinkat(orbit.state_fd, "archive-account.json", 0) && errno != ENOENT) return -1;
        return fsync(orbit.state_fd);
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "schema", 1);
    cJSON_AddStringToObject(o, "user", account.user);
    cJSON_AddStringToObject(o, "sig", account.sig);
    cJSON_AddStringToObject(o, "name", account.name);
    char *json = cJSON_PrintUnformatted(o);
    const char *secrets[] = {"user", "sig"};
    for (size_t i = 0; i < 2; i++) {
        cJSON *v = cJSON_GetObjectItemCaseSensitive(o, secrets[i]);
        if (v && v->valuestring) OPENSSL_cleanse(v->valuestring, strlen(v->valuestring));
    }
    cJSON_Delete(o);
    if (!json) return -1;
    char name[64], random[17];
    random_hex(random, 8);
    snprintf(name, sizeof name, "archive-account-%s.tmp", random);
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
        if (!rc) rc = renameat(orbit.state_fd, name, orbit.state_fd, "archive-account.json");
        if (!rc) rc = fsync(orbit.state_fd);
        unlinkat(orbit.state_fd, name, 0);
    }
    OPENSSL_cleanse(json, strlen(json));
    free(json);
    return rc;
}
void archive_account_load(void) {
    int fd = openat(orbit.state_fd, "archive-account.json", O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return;
    struct stat st;
    char buf[4096] = {0};
    if (!fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_nlink == 1 && st.st_size > 0 &&
        st.st_size < (off_t)sizeof buf && read(fd, buf, (size_t)st.st_size) == st.st_size) {
        cJSON *o = cJSON_Parse(buf);
        pthread_mutex_lock(&lock);
        if (json_int(o, "schema") == 1 && cookie_value(json_text(o, "user"), account.user) &&
            cookie_value(json_text(o, "sig"), account.sig)) {
            const char *name = json_text(o, "name");
            copy_text(account.name, sizeof account.name, text_ok(name, 0, NAME_CAP - 1, true) ? name : "");
        } else {
            OPENSSL_cleanse(account.user, sizeof account.user);
            OPENSSL_cleanse(account.sig, sizeof account.sig);
        }
        pthread_mutex_unlock(&lock);
        const char *secrets[] = {"user", "sig"};
        for (size_t i = 0; i < 2; i++) {
            cJSON *v = cJSON_GetObjectItemCaseSensitive(o, secrets[i]);
            if (v && v->valuestring) OPENSSL_cleanse(v->valuestring, strlen(v->valuestring));
        }
        cJSON_Delete(o);
    }
    OPENSSL_cleanse(buf, sizeof buf);
    close(fd);
}
void archive_account_clear(void) {
    pthread_mutex_lock(&lock);
    OPENSSL_cleanse(&account, sizeof account);
    pthread_mutex_unlock(&lock);
}
cJSON *archive_account_status(void) {
    cJSON *o = cJSON_CreateObject();
    pthread_mutex_lock(&lock);
    cJSON_AddBoolToObject(o, "signedIn", account.user[0] != 0);
    cJSON_AddStringToObject(o, "name", account.name);
    cJSON_AddBoolToObject(o, "busy", account.busy);
    pthread_mutex_unlock(&lock);
    return o;
}
/* Sends the email and password once. Fills the session or returns why not. */
static int sign_in(const char *email, const char *password, char *user, char *sig, char *name,
                   char *error, size_t cap) {
    char *e = curl_easy_escape(NULL, email, 0), *p = curl_easy_escape(NULL, password, 0);
    char form[2048];
    int fits = e && p ? snprintf(form, sizeof form, "email=%s&password=%s", e, p) : -1;
    if (e) { OPENSSL_cleanse(e, strlen(e)); curl_free(e); }
    if (p) { OPENSSL_cleanse(p, strlen(p)); curl_free(p); }
    if (fits < 0 || fits >= (int)sizeof form) {
        OPENSSL_cleanse(form, sizeof form);
        copy_text(error, cap, "Use an email of up to 254 characters and a password of up to 256.");
        return 400;
    }
    long status = 0;
    cJSON *reply = call("/services/xauthn/?op=login", form, &status);
    OPENSSL_cleanse(form, sizeof form);
    if (!status) {
        cJSON_Delete(reply);
        copy_text(error, cap, UNREACHABLE);
        return 502;
    }
    const cJSON *values = cJSON_GetObjectItemCaseSensitive(reply, "values");
    const cJSON *cookies = cJSON_GetObjectItemCaseSensitive(values, "cookies");
    int code = 200;
    /* Never relay Archive.org's own text: it can contain the account's details. */
    if (status != 200 || !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reply, "success"))) {
        const char *why = json_text(values, "reason");
        bool wrong = status == 401 || !strcmp(why, "account_not_found") || !strcmp(why, "account_bad_password");
        copy_text(error, cap, !reply ? UNEXPECTED
                              : wrong ? "Archive.org didn't accept this email or password."
                                      : "Archive.org couldn't sign in this account. Check it on archive.org.");
        code = reply ? 409 : 502;
    } else if (!cookie_value(json_text(cookies, "logged-in-user"), user) ||
               !cookie_value(json_text(cookies, "logged-in-sig"), sig)) {
        copy_text(error, cap, UNEXPECTED);
        code = 502;
    } else {
        const char *screen = json_text(values, "screenname");
        copy_text(name, NAME_CAP, text_ok(screen, 0, NAME_CAP - 1, true) ? screen : "");
    }
    const char *secrets[] = {"logged-in-user", "logged-in-sig"};
    for (size_t i = 0; i < 2; i++) {
        cJSON *v = cJSON_GetObjectItemCaseSensitive((cJSON *)cookies, secrets[i]);
        if (v && v->valuestring) OPENSSL_cleanse(v->valuestring, strlen(v->valuestring));
    }
    cJSON_Delete(reply);
    return code;
}
int archive_account_action(const cJSON *input, char *error, size_t cap) {
    const char *action = json_text(input, "action");
    if (!strcmp(action, "sign-out")) {
        pthread_mutex_lock(&lock);
        int code = 200;
        if (account.busy) {
            copy_text(error, cap, "Wait for the Archive.org sign-in to finish.");
            code = 409;
        } else {
            char user[COOKIE_CAP], sig[COOKIE_CAP], name[NAME_CAP];
            memcpy(user, account.user, sizeof user);
            memcpy(sig, account.sig, sizeof sig);
            memcpy(name, account.name, sizeof name);
            OPENSSL_cleanse(account.user, sizeof account.user);
            OPENSSL_cleanse(account.sig, sizeof account.sig);
            account.name[0] = 0;
            if (persist()) {
                memcpy(account.user, user, sizeof user);
                memcpy(account.sig, sig, sizeof sig);
                memcpy(account.name, name, sizeof name);
                copy_text(error, cap, "Could not remove the saved Archive.org sign-in.");
                code = 503;
            }
            OPENSSL_cleanse(user, sizeof user);
            OPENSSL_cleanse(sig, sizeof sig);
        }
        pthread_mutex_unlock(&lock);
        return code;
    }
    if (strcmp(action, "sign-in")) {
        copy_text(error, cap, "Choose sign-in or sign-out.");
        return 400;
    }
    const char *email = json_text(input, "email"), *password = json_text(input, "password");
    if (!text_ok(email, 3, 254, false) || !strchr(email, '@')) {
        copy_text(error, cap, "Enter the email address of your Archive.org account.");
        return 400;
    }
    if (!text_ok(password, 1, 256, true)) {
        copy_text(error, cap, "Enter your Archive.org password.");
        return 400;
    }
    pthread_mutex_lock(&lock);
    bool busy = account.busy;
    account.busy = true;
    pthread_mutex_unlock(&lock);
    if (busy) {
        copy_text(error, cap, "An Archive.org sign-in is already running.");
        return 409;
    }
    char user[COOKIE_CAP] = "", sig[COOKIE_CAP] = "", name[NAME_CAP] = "";
    int code = sign_in(email, password, user, sig, name, error, cap);
    pthread_mutex_lock(&lock);
    if (code == 200) {
        char old_user[COOKIE_CAP], old_sig[COOKIE_CAP], old_name[NAME_CAP];
        memcpy(old_user, account.user, sizeof old_user);
        memcpy(old_sig, account.sig, sizeof old_sig);
        memcpy(old_name, account.name, sizeof old_name);
        copy_text(account.user, sizeof account.user, user);
        copy_text(account.sig, sizeof account.sig, sig);
        copy_text(account.name, sizeof account.name, name);
        if (persist()) {
            memcpy(account.user, old_user, sizeof old_user);
            memcpy(account.sig, old_sig, sizeof old_sig);
            memcpy(account.name, old_name, sizeof old_name);
            copy_text(error, cap, "Could not save the Archive.org sign-in on this console.");
            code = 503;
        }
        OPENSSL_cleanse(old_user, sizeof old_user);
        OPENSSL_cleanse(old_sig, sizeof old_sig);
    }
    account.busy = false;
    pthread_mutex_unlock(&lock);
    OPENSSL_cleanse(user, sizeof user);
    OPENSSL_cleanse(sig, sizeof sig);
    return code;
}
bool archive_account_apply(CURL *curl) {
#ifdef ORBIT_TEST
    const char *domain = "127.0.0.1\tFALSE\t/\tFALSE";
#else
    /* .archive.org and its storage servers (ia800000.us.archive.org), HTTPS only. */
    const char *domain = ".archive.org\tTRUE\t/\tTRUE";
#endif
    char line[COOKIE_CAP + 96];
    pthread_mutex_lock(&lock);
    bool on = account.user[0] && account.sig[0];
    if (on) {
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
        snprintf(line, sizeof line, "%s\t0\tlogged-in-user\t%s", domain, account.user);
        curl_easy_setopt(curl, CURLOPT_COOKIELIST, line);
        snprintf(line, sizeof line, "%s\t0\tlogged-in-sig\t%s", domain, account.sig);
        curl_easy_setopt(curl, CURLOPT_COOKIELIST, line);
        OPENSSL_cleanse(line, sizeof line);
    }
    pthread_mutex_unlock(&lock);
    return on;
}
/* The item in https://archive.org/download/<item>/<file>, already URL-encoded. */
static bool item_of(const char *url, char *out, size_t cap) {
    const char *at = strstr(url, "/download/");
    if (!at) return false;
    at += 10;
    size_t n = strcspn(at, "/?#");
    if (!n || n >= cap || !at[n]) return false;
    for (size_t i = 0; i < n; i++) {
        char c = at[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_' || c == '.' || c == '%'))
            return false;
    }
    memcpy(out, at, n);
    out[n] = 0;
    return true;
}
void archive_account_refusal(const char *url, char *error, size_t cap) {
    char item[256], path[320];
    bool dark = false;
    if (item_of(url, item, sizeof item)) {
        long status = 0;
        snprintf(path, sizeof path, "/metadata/%s/is_dark", item);
        cJSON *reply = call(path, NULL, &status);
        dark = status == 200 && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reply, "result"));
        cJSON_Delete(reply);
    }
    pthread_mutex_lock(&lock);
    bool signed_in = account.user[0] != 0;
    pthread_mutex_unlock(&lock);
    copy_text(error, cap,
              dark ? "Archive.org has taken this game down. Choose another source."
              : signed_in ? "Archive.org didn't allow this file for your account. Sign in to Archive.org again in App settings, or choose another source."
                          : "This file needs an Archive.org account. Sign in to Archive.org in App settings, then retry.");
}
