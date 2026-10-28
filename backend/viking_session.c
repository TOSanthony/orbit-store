#include "viking_session.h"
#include "browser_platform.h"
#include "browser_match.h"
#include "provider_network.h"
#include "ca.h"
#include <ctype.h>
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PAGE_MAX (192 * 1024)
typedef struct {
    char *body;
    size_t length;
    char location[2048], type[128];
    long status;
    atomic_bool *cancel;
} Page;
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static struct { int user; VikingState state; time_t checked; } states[2];
static bool contains(const char *text, const char *part) {
    size_t length = strlen(part);
    for (; *text; ++text) if (!strncasecmp(text, part, length)) return true;
    return false;
}

static bool attr(const char *tag, const char *end, const char *key, char *out, size_t cap) {
    const size_t wanted = strlen(key);
    const char *p = tag;
    while (p < end) {
        while (p < end && !isspace((unsigned char)*p)) ++p;
        while (p < end && isspace((unsigned char)*p)) ++p;
        const char *name = p;
        while (p < end && (isalnum((unsigned char)*p) || *p == '-' || *p == '_')) ++p;
        size_t n = (size_t)(p - name);
        while (p < end && isspace((unsigned char)*p)) ++p;
        if (p == end || *p++ != '=') continue;
        while (p < end && isspace((unsigned char)*p)) ++p;
        if (p == end || (*p != '\'' && *p != '"')) continue;
        char quote = *p++;
        const char *value = p;
        while (p < end && *p != quote) ++p;
        if (p == end) return false;
        if (n == wanted && !strncasecmp(name, key, n)) {
            n = (size_t)(p - value);
            if (n >= cap) return false;
            memcpy(out, value, n); out[n] = 0; return true;
        }
        ++p;
    }
    return false;
}
VikingState viking_page_state(const char *html) {
    bool login = false, logout = false;
    unsigned users = 0;
    char hash[257] = {0};
    for (const char *p = html; p && (p = strchr(p, '<')); ++p) {
        const char *end = strchr(p, '>');
        if (!end) break;
        char id[64], name[64], type[32], link[256];
        if (!strncasecmp(p, "<input", 6) && isspace((unsigned char)p[6]) &&
            attr(p, end, "id", id, sizeof id) && !strcmp(id, "user") &&
            attr(p, end, "name", name, sizeof name) && !strcmp(name, "user") &&
            attr(p, end, "type", type, sizeof type) && !strcasecmp(type, "hidden")) {
            ++users;
            if (!attr(p, end, "value", hash, sizeof hash)) { OPENSSL_cleanse(hash, sizeof hash); return VIKING_UNKNOWN; }
        }
        if (!strncasecmp(p, "<a", 2) && isspace((unsigned char)p[2]) && attr(p, end, "href", link, sizeof link)) {
            if (!strcmp(link, "/login") || !strcmp(link, "https://vikingfile.com/login")) login = true;
            if (!strcmp(link, "/logout") || !strcmp(link, "https://vikingfile.com/logout")) logout = true;
        }
        p = end;
    }
    VikingState state = VIKING_UNKNOWN;
    size_t n = strlen(hash);
    if (users == 1 && !n && login) state = VIKING_SIGNED_OUT;
    if (users == 1 && n >= 8 && !login && logout &&
        strspn(hash, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == n) state = VIKING_SIGNED_IN;
    OPENSSL_cleanse(hash, sizeof hash);
    return state;
}
static void remember(bool native, int user, VikingState state) {
    pthread_mutex_lock(&state_lock);
    states[native].user = user; states[native].state = state; states[native].checked = time(NULL);
    pthread_mutex_unlock(&state_lock);
}
const char *viking_cached_state(bool native) {
    pthread_mutex_lock(&state_lock);
    VikingState state = time(NULL) - states[native].checked < 300 ? states[native].state : VIKING_UNKNOWN;
    int user = states[native].user;
    pthread_mutex_unlock(&state_lock);
#ifndef ORBIT_TEST
    if (state != VIKING_UNKNOWN && user != probe_browser_user()) state = VIKING_UNKNOWN;
#else
    (void)user;
#endif
    return state == VIKING_SIGNED_IN ? "signed-in" : state == VIKING_SIGNED_OUT ? "signed-out" : "unknown";
}
static const char *origin(void) {
#ifdef ORBIT_TEST
    const char *value = getenv("ORBIT_TEST_VIKING_ORIGIN");
    return value && !strncmp(value, "http://127.0.0.1:", 17) ? value : NULL;
#else
    return "https://vikingfile.com";
#endif
}
static CURL *session_open(bool native, int *user) {
    *user = probe_browser_user();
    char path[256];
#ifdef ORBIT_TEST
    (void)native;
    const char *fixture = getenv("ORBIT_TEST_VIKING_COOKIES");
    if (!fixture) return NULL;
    *user = 1;
    if (snprintf(path, sizeof path, "%s", fixture) >= (int)sizeof path) return NULL;
#else
    if (*user < 0 || snprintf(path, sizeof path, "/user/home/%08x/webkit/%s/NKCookie.jar.db", (unsigned)*user,
        native ? "common_dialog/webbrowser" : "shell") >= (int)sizeof path) return NULL;
#endif
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    if (!viking_read_cookies(curl, path, time(NULL))) {
        curl_easy_cleanup(curl); return NULL;
    }
    return curl;
}
static size_t header(char *data, size_t a, size_t b, void *context) {
    Page *page = context;
    if (a && b > SIZE_MAX / a) return 0;
    size_t n = a * b;
    if (n > 8192) return 0;
    char *dest = NULL; size_t cap = 0, prefix = 0;
    if (n > 9 && !strncasecmp(data, "Location:", 9)) { dest = page->location; cap = sizeof page->location; prefix = 9; }
    if (n > 13 && !strncasecmp(data, "Content-Type:", 13)) { dest = page->type; cap = sizeof page->type; prefix = 13; }
    if (dest) {
        while (prefix < n && (data[prefix] == ' ' || data[prefix] == '\t')) ++prefix;
        size_t end = n;
        while (end > prefix && (data[end-1] == '\r' || data[end-1] == '\n' || data[end-1] == ' ')) --end;
        if (end - prefix >= cap || *dest) return 0;
        memcpy(dest, data + prefix, end - prefix); dest[end - prefix] = 0;
    }
    return n;
}
static size_t body(char *data, size_t a, size_t b, void *context) {
    Page *page = context;
    if (a && b > SIZE_MAX / a) return 0;
    size_t n = a * b;
    /* Stop unexpected binary responses immediately, even if Range was ignored. */
    if (strncasecmp(page->type, "text/html", 9) || n > PAGE_MAX - page->length) return 0;
    char *next = realloc(page->body, page->length + n + 1);
    if (!next) return 0;
    page->body = next;
    memcpy(next + page->length, data, n); page->length += n; next[page->length] = 0;
    return n;
}
static int progress(void *context, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)a; (void)b; (void)c; (void)d;
    Page *page = context;
    return page->cancel && atomic_load(page->cancel);
}
static bool request(CURL *curl, const char *path, Page *page) {
    const char *base = origin(); char url[2048];
    if (!base || snprintf(url, sizeof url, "%s%s", base, path) >= (int)sizeof url) return false;
    struct curl_blob ca = {(void *)orbit_ca, sizeof orbit_ca - 1, CURL_BLOB_COPY};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &ca);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, provider_open_socket);
#ifdef ORBIT_TEST
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
#endif
    /* These are account/file HTML pages, not the generated download URL.
     * A server honouring Range: 0-0 would hide the link after its first byte.
     * The body callback rejects binary data and caps HTML at PAGE_MAX. */
    curl_easy_setopt(curl, CURLOPT_RANGE, NULL);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 OrbitStore/" ORBIT_VERSION);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, page);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, page);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, page);
    CURLcode result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &page->status);
    return result == CURLE_OK || (result == CURLE_WRITE_ERROR && page->status >= 300 && page->status < 400 && page->location[0]);
}
static void clear(Page *page) {
    if (page->body) { OPENSSL_cleanse(page->body, page->length); free(page->body); }
    atomic_bool *cancel = page->cancel;
    OPENSSL_cleanse(page, sizeof *page); page->cancel = cancel;
}
static VikingState check(CURL *curl, atomic_bool *cancel) {
    Page page = {.cancel = cancel};
    VikingState state = request(curl, "/", &page) && page.status == 200 && page.body ? viking_page_state(page.body) : VIKING_UNKNOWN;
    clear(&page); return state;
}
VikingState viking_check_session(bool native, atomic_bool *cancel) {
    int user;
    CURL *curl = session_open(native, &user);
    /* Missing/unsupported storage cannot prove the provider signed out. */
    VikingState state = curl ? check(curl, cancel) : VIKING_UNKNOWN;
    remember(native, user, state);
    curl_easy_cleanup(curl);
    return state;
}
bool viking_resolve_download(Release *release, bool native, atomic_bool *cancel) {
    if (strcmp(release->source, "vikingfile") || !provider_browser_supported(release)) return false;
    const char *path = strstr(release->browser_url, "/f/");
    if (!path || strlen(path) != 13) return false;
    int user; CURL *curl = session_open(native, &user);
    if (!curl) return false;
    VikingState state = check(curl, cancel);
    remember(native, user, state);
    bool ready = false;
    if (state == VIKING_SIGNED_IN && (!cancel || !atomic_load(cancel))) {
        Page page = {.cancel = cancel};
        if (request(curl, path, &page)) {
            char target[2048];
            if (page.location[0] == '/' && page.location[1] != '/')
                snprintf(target, sizeof target, "%s%s", origin(), page.location);
            else copy_text(target, sizeof target, page.location);
            if ((page.status == 301 || page.status == 302 || page.status == 303 || page.status == 307 || page.status == 308) &&
                provider_capture_url(release, target)) {
                copy_text(release->url, sizeof release->url, target); ready = true;
            } else if (page.status == 200 && page.body &&
                       !contains(page.body, "cf-turnstile") && !contains(page.body, "captcha-download") &&
                       !contains(page.body, "/cdn-cgi/challenge-platform/") && viking_page_state(page.body) != VIKING_SIGNED_OUT) {
                BrowserMatch match = {.release = release};
                if (browser_match(&match, (const unsigned char *)page.body, page.length + 1)) {
                    copy_text(release->url, sizeof release->url, match.url); ready = true;
                }
                OPENSSL_cleanse(&match, sizeof match);
            }
            OPENSSL_cleanse(target, sizeof target);
        }
        clear(&page);
    }
    curl_easy_cleanup(curl);
    return ready && (!cancel || !atomic_load(cancel));
}
