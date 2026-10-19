/* Downloads from Sony's own servers (see sonynet.h). */
#define _POSIX_C_SOURCE 200809L
#include "sonynet.h"
#include "ca.h"
#include "orbit.h"
#include "patchfeed.h"
#include "provider_network.h"
#include <curl/curl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Sony signs its download servers with its own root, which public CA lists do not carry. */
static const char sony_root[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIID0jCCArqgAwIBAgIBADANBgkqhkiG9w0BAQUFADBUMQswCQYDVQQGEwJKUDEp\n"
    "MCcGA1UEChMgU29ueSBDb21wdXRlciBFbnRlcnRhaW5tZW50IEluYy4xGjAYBgNV\n"
    "BAMTEVNDRUkgRE5BUyBSb290IDA1MB4XDTA0MDcxMjA5MDExOVoXDTM3MTIwNjA5\n"
    "MDExOVowVDELMAkGA1UEBhMCSlAxKTAnBgNVBAoTIFNvbnkgQ29tcHV0ZXIgRW50\n"
    "ZXJ0YWlubWVudCBJbmMuMRowGAYDVQQDExFTQ0VJIEROQVMgUm9vdCAwNTCCASIw\n"
    "DQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBANmPeza8PwCqlI7esOGIkoSESnIN\n"
    "g72ZD3Ut63jy7SdothPIvGBqVZWYkIpqJYJd1I4Nh//IpXQCQL0PnJLrh9BBeowq\n"
    "Muf5NNq3Us80Ihiu9CvNEAEO18g3OFV1TYdSwQ5zUsk33OUeI7h4aBPDVcZXYeHt\n"
    "dbPLqe4K8igian5prrAD5S6h28t8aAm+qMWRo+bW25B/841XwDGBP7/IxZv8Yoio\n"
    "rCo80CVYe6lGoU08eeqQiaHI5zAF281DWZSoVfLjJUEWmEnxqr8aOhszRGePi+Ei\n"
    "7UQjHDuZX9rLhDI1zAND+BA259tn/iwOqVXe20OccJllHJcG4Ecmd98f5qMCAwEA\n"
    "AaOBrjCBqzAdBgNVHQ4EFgQUxlahM1tPzoN3YgVEhm0gV7Wv2twwfAYDVR0jBHUw\n"
    "c4AUxlahM1tPzoN3YgVEhm0gV7Wv2tyhWKRWMFQxCzAJBgNVBAYTAkpQMSkwJwYD\n"
    "VQQKEyBTb255IENvbXB1dGVyIEVudGVydGFpbm1lbnQgSW5jLjEaMBgGA1UEAxMR\n"
    "U0NFSSBETkFTIFJvb3QgMDWCAQAwDAYDVR0TBAUwAwEB/zANBgkqhkiG9w0BAQUF\n"
    "AAOCAQEACZPihjwXA27wJ03tEKcHAeFLi8aBw2ysH4GwuH1dWb3UpuznWOB0iQT1\n"
    "wQocnEFYCJx5XFEnj4aLWpSHLEq/sSO+my+aPoTEsy20ajF+YLYZm0bZxH50CJYh\n"
    "rkET4C2aC0XvhGp9k1JQ1o0W6+cFT5LTlXapsq8Btt31t+XDPX7RqGV4WGekt3hM\n"
    "T7xRc7JWXdAQijIrbYi8mtbM07KEGnPU6IT8C47+0mSurpwLOoWL1tPgo6ePpLNi\n"
    "c4quUMgh9RXVjeTyXOMmyYdeUm2gt7qErvQONli+6Epmhm0A2khpIMHSpQjTE8gV\n"
    "rZp42a6+zg1iYy2vFBOmiQ17GRUl0A==\n"
    "-----END CERTIFICATE-----\n";

typedef struct {
    SonyBody *body;
    size_t max;
    sony_stop_fn stop;
} Collect;

typedef struct {
    int fd;
    int64_t position; /* where the next byte goes, relative to file_offset */
    int64_t file_offset;
    int64_t limit;    /* bytes still wanted */
    volatile int64_t *received;
    int64_t base;
    bool disk_error;
    sony_stop_fn stop;
} Write;

static size_t collect(char *data, size_t a, size_t b, void *context) {
    Collect *c = context;
    size_t n = a * b;
    if (n > c->max - c->body->size)
        return 0;
    char *next = realloc(c->body->data, c->body->size + n + 1);
    if (!next)
        return 0;
    c->body->data = next;
    memcpy(next + c->body->size, data, n);
    c->body->size += n;
    next[c->body->size] = 0;
    return n;
}
/* A bigger receive buffer on the connection, as the main download engine does: the default is small
 * for a long path to a server far away, and caps the speed. A larger system value is never reduced,
 * and a refusal is ignored. */
#define PATCH_SOCKET_BUFFER (1024 * 1024)
#define PATCH_READ_BUFFER (256 * 1024)
static int tune_socket(void *unused, curl_socket_t fd, curlsocktype purpose) {
    (void)unused;
    if (purpose == CURLSOCKTYPE_IPCXN) {
        int current = 0, wanted = PATCH_SOCKET_BUFFER;
        socklen_t length = sizeof current;
        if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &current, &length) || current < wanted)
            setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &wanted, sizeof wanted);
    }
    return CURL_SOCKOPT_OK;
}
static size_t store(char *data, size_t a, size_t b, void *context) {
    Write *w = context;
    size_t n = a * b;
    if ((int64_t)n > w->limit)
        n = (size_t)w->limit; /* never write past the piece, whatever the server sends */
    size_t done = 0;
    while (done < n) {
        ssize_t r = pwrite(w->fd, data + done, n - done, (off_t)(w->file_offset + w->position + (int64_t)done));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            w->disk_error = true;
            return 0;
        }
        done += (size_t)r;
    }
    w->position += (int64_t)n;
    w->limit -= (int64_t)n;
    if (w->received)
        *w->received = w->base + w->position;
    return a * b;
}
typedef struct {
    sony_stop_fn stop;
} Progress;
static int progress(void *context, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)a, (void)b, (void)c, (void)d;
    Progress *p = context;
    return p->stop && p->stop();
}
/* Orbit's CA list followed by Sony's root, in one memory blob (CURL_BLOB_COPY keeps its own copy). */
static char *join_ca(struct curl_blob *blob) {
    size_t a = strlen(orbit_ca), b = strlen(sony_root);
    char *all = malloc(a + b + 2);
    if (!all)
        return NULL;
    memcpy(all, orbit_ca, a);
    all[a] = '\n';
    memcpy(all + a + 1, sony_root, b + 1);
    blob->data = all;
    blob->len = a + b + 1;
    blob->flags = CURL_BLOB_COPY;
    return all;
}
/* gst.prod.dl.playstation.net answers with Akamai's shared certificate, which names a248.e.akamai.net and
 * never gst.*: the console does not check names, Orbit does. Orbit keeps checking: it connects to gst's
 * address, asks for a248.e.akamai.net (the name the certificate really carries) and names gst in the
 * Host header so Akamai serves the right files. The chain must still end at a trusted root. */
typedef struct {
    struct curl_slist *headers;
    struct curl_slist *connect_to;
} Route;
static void route_apply(CURL *curl, char *address, size_t cap, Route *route) {
    static const char gst[] = "https://gst.prod.dl.playstation.net/";
    route->headers = route->connect_to = NULL;
    if (strncmp(address, gst, sizeof gst - 1))
        return;
    char rest[900];
    snprintf(rest, sizeof rest, "%s", address + sizeof gst - 1);
    snprintf(address, cap, "https://a248.e.akamai.net/%s", rest);
    route->headers = curl_slist_append(NULL, "Host: gst.prod.dl.playstation.net");
    route->connect_to = curl_slist_append(NULL, "a248.e.akamai.net:443:gst.prod.dl.playstation.net:443");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, route->headers);
    curl_easy_setopt(curl, CURLOPT_CONNECT_TO, route->connect_to);
}
/* Sony's older servers (sgst.*) chain to a root from 2003 whose signatures use SHA-1, which OpenSSL
 * refuses at its default security level ("CA signature digest algorithm too weak"). Only after that
 * exact refusal is the request repeated with that one rule relaxed: the chain must still end at Sony's
 * root, and the name and certificate are still checked. */
static CURLcode perform(CURL *curl, const char *detail) {
    CURLcode result = curl_easy_perform(curl);
    if (result == CURLE_PEER_FAILED_VERIFICATION && strstr(detail, "too weak")) {
        curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, "DEFAULT:@SECLEVEL=0");
        result = curl_easy_perform(curl);
    }
    return result;
}
static void route_free(Route *route) {
    curl_slist_free_all(route->headers);
    curl_slist_free_all(route->connect_to);
}
/* The address to contact: Sony's own, or (tests only) a local fake that serves the same file names. */
static bool resolve(const char *url, char *out, size_t cap, bool *local, bool nas, char *why, size_t why_cap) {
    *local = false;
#ifdef ORBIT_TEST
    const char *base = getenv("ORBIT_TEST_GAMEUPDATES_BASE");
    if (base && !strncmp(base, "http://127.0.0.1:", 17)) {
        const char *name = strrchr(url, '/');
        snprintf(out, cap, "%s%s", base, name ? name : "/x");
        *local = true;
        return true;
    }
#endif
    if (nas) {
        if (strncmp(url, "https://", 8) || !nas_url_allowed(url) || strlen(url) >= cap) {
            snprintf(why, why_cap, "address not on your NAS");
            return false;
        }
        snprintf(out, cap, "%s", url);
        return true;
    }
    if (!patchfeed_url_allowed(url)) {
        snprintf(why, why_cap, "address not on Sony's download servers");
        return false;
    }
    snprintf(out, cap, "%s", url);
    return true;
}
/* "host: libcurl's own words" so a failed check says which server and why (for instance which certificate). */
static void explain(char *why, size_t cap, const char *address, CURLcode result, const char *detail) {
    char host[96] = "";
    const char *start = strstr(address, "://");
    start = start ? start + 3 : address;
    size_t n = strcspn(start, "/:");
    if (n >= sizeof host)
        n = sizeof host - 1;
    memcpy(host, start, n);
    host[n] = 0;
    snprintf(why, cap, "%s: %s", host, *detail ? detail : curl_easy_strerror(result));
}
static void common(CURL *curl, struct curl_blob *ca, bool local, Progress *p) {
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, ca);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, local ? "http" : "http,https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Orbit-Store/" ORBIT_VERSION);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, p);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
}

bool sony_get(const char *url, size_t max, SonyBody *body, sony_stop_fn stop, char *why, size_t why_cap) {
    char address[900];
    bool local;
    body->data = NULL;
    body->size = 0;
    if (!resolve(url, address, sizeof address, &local, false, why, why_cap))
        return false;
    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(why, why_cap, "not enough memory");
        return false;
    }
    struct curl_blob ca;
    char *storage = join_ca(&ca);
    if (!storage) {
        curl_easy_cleanup(curl);
        snprintf(why, why_cap, "not enough memory");
        return false;
    }
    Collect collected = {body, max, stop};
    Progress watch = {stop};
    Route route;
    route_apply(curl, address, sizeof address, &route);
    curl_easy_setopt(curl, CURLOPT_URL, address);
    common(curl, &ca, local, &watch);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &collected);
    char detail[CURL_ERROR_SIZE] = "";
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, detail);
    CURLcode result = perform(curl, detail);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    route_free(&route);
    free(storage);
    if (result != CURLE_OK || status != 200) {
        if (result != CURLE_OK)
            explain(why, why_cap, address, result, detail);
        else
            snprintf(why, why_cap, "HTTP %ld", status);
        free(body->data);
        body->data = NULL;
        body->size = 0;
        return false;
    }
    return true;
}

/* The connection kept between the slices of one download: opening a new one for each slice costs a
 * TCP and TLS handshake and restarts the transfer's slow start. Each download thread has its own. */
static __thread CURL *held; /* one per thread: parallel slices each keep their own */
void sony_range_close(void) {
    if (held) {
        curl_easy_cleanup(held);
        held = NULL;
    }
}
static sony_range_result range(const char *url, int64_t start, int64_t end, int fd, int64_t file_offset,
                               volatile int64_t *received, sony_stop_fn stop, bool nas, char *why, size_t why_cap) {
    char address[2100];
    bool local;
    if (start < 0 || end <= start) {
        snprintf(why, why_cap, "empty range");
        return SONY_RANGE_FAILED;
    }
    if (!resolve(url, address, sizeof address, &local, nas, why, why_cap))
        return SONY_RANGE_REFUSED;
    CURL *curl = held;
    if (curl)
        curl_easy_reset(curl); /* keeps the open connection, forgets the options */
    else
        curl = curl_easy_init();
    if (!curl) {
        snprintf(why, why_cap, "not enough memory");
        return SONY_RANGE_FAILED;
    }
    held = curl;
    struct curl_blob ca;
    char *storage = join_ca(&ca);
    if (!storage) {
        sony_range_close();
        snprintf(why, why_cap, "not enough memory");
        return SONY_RANGE_FAILED;
    }
    Write w = {.fd = fd, .position = start, .file_offset = file_offset, .limit = end - start,
               .received = received, .base = 0, .stop = stop};
    Progress watch = {stop};
    char range_text[64];
    snprintf(range_text, sizeof range_text, "%lld-%lld", (long long)start, (long long)(end - 1));
    Route route;
    route_apply(curl, address, sizeof address, &route);
    curl_easy_setopt(curl, CURLOPT_URL, address);
    common(curl, &ca, local, &watch);
    if (nas && !local) {
        curl_easy_setopt(curl, CURLOPT_PROXY, "");
        curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, provider_open_socket);
    }
    curl_easy_setopt(curl, CURLOPT_RANGE, range_text);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, (long)PATCH_READ_BUFFER);
    curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, tune_socket);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 45L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, store);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &w);
    char detail[CURL_ERROR_SIZE] = "";
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, detail);
    CURLcode result = perform(curl, detail);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (result != CURLE_OK)
        sony_range_close(); /* never reuse a connection that just failed */
    route_free(&route);
    free(storage);
    if (received)
        *received = w.position;
    if (w.disk_error) {
        snprintf(why, why_cap, "could not write to the drive");
        return SONY_RANGE_DISK;
    }
    if (result == CURLE_ABORTED_BY_CALLBACK)
        return SONY_RANGE_ABORTED;
    if (status == 400 || status == 401 || status == 403 || status == 404 || status == 410) {
        snprintf(why, why_cap, "HTTP %ld", status);
        return SONY_RANGE_REFUSED;
    }
    if (result == CURLE_OK && start > 0 && status == 200) {
        snprintf(why, why_cap, "the server ignored the range");
        return SONY_RANGE_IGNORED;
    }
    if (result != CURLE_OK || (status != 200 && status != 206)) {
        if (result != CURLE_OK)
            explain(why, why_cap, address, result, detail);
        else
            snprintf(why, why_cap, "HTTP %ld", status);
        return SONY_RANGE_FAILED;
    }
    return w.position == end ? SONY_RANGE_OK : SONY_RANGE_FAILED;
}

sony_range_result sony_range(const char *url, int64_t start, int64_t end, int fd, int64_t file_offset,
                             volatile int64_t *received, sony_stop_fn stop, char *why, size_t why_cap) {
    return range(url, start, end, fd, file_offset, received, stop, false, why, why_cap);
}
sony_range_result nas_range(const char *url, int64_t start, int64_t end, int fd, int64_t file_offset,
                            volatile int64_t *received, sony_stop_fn stop, char *why, size_t why_cap) {
    return range(url, start, end, fd, file_offset, received, stop, true, why, why_cap);
}
