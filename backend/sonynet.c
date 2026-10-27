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
#include <pthread.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <time.h>
#include <fcntl.h>
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
    _Atomic int64_t *received;
    int64_t base;
    bool disk_error;
    sony_stop_fn stop;
    int64_t durable;  /* how far the drive really has it: position minus what still waits in the buffer */
    size_t buffered;  /* bytes received but not yet written, in this thread's buffer */
} Write;

static _Atomic long long stat_write_ns, stat_connect_ns, stat_first_byte_ns, stat_requests;
static pthread_mutex_t stat_mutex = PTHREAD_MUTEX_INITIALIZER;
static char stat_ip[48];
static long long now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
}
void sony_stats_peek(SonyStats *out) {
    memset(out, 0, sizeof *out);
    out->write_seconds = (double)atomic_load(&stat_write_ns) / 1e9;
    out->connect_seconds = (double)atomic_load(&stat_connect_ns) / 1e9;
    out->first_byte_seconds = (double)atomic_load(&stat_first_byte_ns) / 1e9;
    out->requests = (long)atomic_load(&stat_requests);
}
void sony_stats_take(SonyStats *out) {
    memset(out, 0, sizeof *out);
    out->write_seconds = (double)atomic_exchange(&stat_write_ns, 0) / 1e9;
    out->connect_seconds = (double)atomic_exchange(&stat_connect_ns, 0) / 1e9;
    out->first_byte_seconds = (double)atomic_exchange(&stat_first_byte_ns, 0) / 1e9;
    out->requests = (long)atomic_exchange(&stat_requests, 0);
    pthread_mutex_lock(&stat_mutex);
    snprintf(out->ip, sizeof out->ip, "%s", stat_ip);
    pthread_mutex_unlock(&stat_mutex);
}

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
/* Data is collected into a 4 MiB buffer (one slice) and written in one go, one writer at a time. Sixteen
 * connections each writing 256 KiB pieces at different places of a huge file made the drive's file system
 * the bottleneck (the log showed 93 % of the time inside pwrite, 6 MB/s); the large ordered writes are what
 * a game download does. Only what has been written counts as received, so a stop never claims more than is
 * on the drive. */
#define WRITE_BUFFER (4 * 1024 * 1024)
static pthread_mutex_t write_lock = PTHREAD_MUTEX_INITIALIZER;
static __thread char *write_buffer;

static bool flush_buffer(Write *w) {
    if (!w->buffered)
        return true;
    pthread_mutex_lock(&write_lock);
    const long long began = now_ns();
    size_t done = 0;
    bool ok = true;
    while (done < w->buffered) {
        ssize_t r = pwrite(w->fd, write_buffer + done, w->buffered - done,
                           (off_t)(w->file_offset + w->durable + (int64_t)done));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            ok = false;
            break;
        }
        done += (size_t)r;
    }
    atomic_fetch_add(&stat_write_ns, now_ns() - began);
    pthread_mutex_unlock(&write_lock);
    if (!ok) {
        w->disk_error = true;
        return false;
    }
    w->durable += (int64_t)w->buffered;
    w->buffered = 0;
    if (w->received)
        atomic_store_explicit(w->received, w->base + w->durable, memory_order_relaxed);
    return true;
}
static size_t store(char *data, size_t a, size_t b, void *context) {
    Write *w = context;
    size_t n = a * b;
    if ((int64_t)n > w->limit)
        n = (size_t)w->limit; /* never write past the piece, whatever the server sends */
    size_t taken = 0;
    while (taken < n) {
        size_t room = WRITE_BUFFER - w->buffered, part = n - taken < room ? n - taken : room;
        memcpy(write_buffer + w->buffered, data + taken, part);
        w->buffered += part;
        taken += part;
        if (w->buffered == WRITE_BUFFER && !flush_buffer(w))
            return 0;
    }
    w->position += (int64_t)n;
    w->limit -= (int64_t)n;
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
    struct curl_slist *resolve;
} Route;
static void route_apply(CURL *curl, char *address, size_t cap, Route *route) {
    static const char gst[] = "https://gst.prod.dl.playstation.net/";
    route->headers = route->connect_to = route->resolve = NULL;
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
/* A DNS filter on the network (AdGuard, Pi-hole...) often blocks Sony's download hosts so the console cannot
 * update itself, and Orbit's patch lookups and downloads are blocked with them. Orbit therefore resolves
 * Sony's hosts itself, through DNS-over-HTTPS to Cloudflare's address (no DNS lookup needed to reach it),
 * so the filter can stay as strict as you like for the console. TLS to the resolver is verified against
 * Orbit's CA list; if the resolver cannot be reached the network's own DNS is used. */
static void own_dns(CURL *curl, bool local, bool nas) {
    if (!local && !nas)
        curl_easy_setopt(curl, CURLOPT_DOH_URL, ORBIT_DNS_URL);
}
/* Sony's older servers (sgst.*) chain to a root from 2003 whose signatures use SHA-1, which OpenSSL
 * refuses at its default security level ("CA signature digest algorithm too weak"). Only after that
 * exact refusal is the request repeated with that one rule relaxed: the chain must still end at Sony's
 * root, and the name and certificate are still checked. */
static CURLcode perform(CURL *curl, const char *detail, bool doh) {
    CURLcode result = curl_easy_perform(curl);
    if (doh && (result == CURLE_COULDNT_RESOLVE_HOST || result == CURLE_COULDNT_RESOLVE_PROXY)) {
        curl_easy_setopt(curl, CURLOPT_DOH_URL, NULL);   /* DNS-over-HTTPS unreachable: ask the network's own DNS after all */
        result = curl_easy_perform(curl);
    }
    if (result == CURLE_PEER_FAILED_VERIFICATION && strstr(detail, "too weak")) {
        curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, "DEFAULT:@SECLEVEL=0");
        result = curl_easy_perform(curl);
    }
    return result;
}
/* The address Sony's host resolved to, remembered for a few minutes. Every slice is a new request, and the
 * log showed all 16 connections "Resolving timed out after 20 s" together at the start of each piece: the
 * lookup (DNS-over-HTTPS, 16 at once) was what stalled, not Sony. Once one request has reached a host, the
 * address it really connected to is reused for the others, so no lookup is needed until it expires or fails. */
#define DNS_KEEP_SECONDS 300
typedef struct {
    char key[160];
    char ip[64];
    time_t when;
} DnsEntry;
static DnsEntry dns_cache[8];
static pthread_mutex_t dns_mutex = PTHREAD_MUTEX_INITIALIZER;

/* "host:port" of the address a request will connect to (the original one, before the Akamai rewrite). */
static bool dns_key(const char *address, char *key, size_t cap) {
    int port = 443;
    const char *host = address;
    if (!strncmp(address, "https://", 8))
        host += 8;
    else if (!strncmp(address, "http://", 7)) {
        host += 7;
        port = 80;
    } else
        return false;
    size_t n = strcspn(host, "/:?");
    if (!n || n >= 120)
        return false;
    snprintf(key, cap, "%.*s:%d", (int)n, host, port);
    return true;
}
static void dns_apply(CURL *curl, const char *key, Route *route) {
    char ip[64] = "";
    pthread_mutex_lock(&dns_mutex);
    for (size_t i = 0; i < sizeof dns_cache / sizeof *dns_cache; i++)
        if (!strcmp(dns_cache[i].key, key) && time(NULL) - dns_cache[i].when < DNS_KEEP_SECONDS)
            snprintf(ip, sizeof ip, "%s", dns_cache[i].ip);
    pthread_mutex_unlock(&dns_mutex);
    if (!ip[0])
        return;
    char entry[260];
    if (strchr(ip, ':'))
        snprintf(entry, sizeof entry, "%s:[%s]", key, ip);
    else
        snprintf(entry, sizeof entry, "%s:%s", key, ip);
    route->resolve = curl_slist_append(NULL, entry);
    curl_easy_setopt(curl, CURLOPT_RESOLVE, route->resolve);
}
static void dns_remember(const char *key, const char *ip) {
    pthread_mutex_lock(&dns_mutex);
    size_t slot = 0;
    for (size_t i = 0; i < sizeof dns_cache / sizeof *dns_cache; i++) {
        if (!strcmp(dns_cache[i].key, key)) {
            slot = i;
            break;
        }
        if (dns_cache[i].when < dns_cache[slot].when)
            slot = i;
    }
    snprintf(dns_cache[slot].key, sizeof dns_cache[slot].key, "%s", key);
    snprintf(dns_cache[slot].ip, sizeof dns_cache[slot].ip, "%s", ip);
    dns_cache[slot].when = time(NULL);
    pthread_mutex_unlock(&dns_mutex);
}
static void dns_forget(const char *key) {
    pthread_mutex_lock(&dns_mutex);
    for (size_t i = 0; i < sizeof dns_cache / sizeof *dns_cache; i++)
        if (!strcmp(dns_cache[i].key, key))
            dns_cache[i].when = 0;
    pthread_mutex_unlock(&dns_mutex);
}
static void route_free(Route *route) {
    curl_slist_free_all(route->headers);
    curl_slist_free_all(route->connect_to);
    curl_slist_free_all(route->resolve);
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
    own_dns(curl, local, false);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &collected);
    char detail[CURL_ERROR_SIZE] = "";
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, detail);
    CURLcode result = perform(curl, detail, !local);
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
    free(write_buffer);
    write_buffer = NULL;
    if (held) {
        curl_easy_cleanup(held);
        held = NULL;
    }
}
static sony_range_result range(const char *url, int64_t start, int64_t end, int fd, int64_t file_offset,
                               _Atomic int64_t *received, sony_stop_fn stop, bool nas, char *why, size_t why_cap) {
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
    if (!write_buffer)
        write_buffer = malloc(WRITE_BUFFER);
    if (!write_buffer) {
        free(storage);
        snprintf(why, why_cap, "not enough memory");
        return SONY_RANGE_FAILED;
    }
    Write w = {.fd = fd, .position = start, .file_offset = file_offset, .limit = end - start,
               .received = received, .base = 0, .stop = stop, .durable = start};
    Progress watch = {stop};
    char range_text[64];
    snprintf(range_text, sizeof range_text, "%lld-%lld", (long long)start, (long long)(end - 1));
    char dns_name[160] = "";
    const bool use_cache = !local && !nas && dns_key(address, dns_name, sizeof dns_name);
    Route route;
    route_apply(curl, address, sizeof address, &route);
    if (use_cache)
        dns_apply(curl, dns_name, &route);
    curl_easy_setopt(curl, CURLOPT_URL, address);
    common(curl, &ca, local, &watch);
    own_dns(curl, local, nas);
    if (nas && !local) {
        curl_easy_setopt(curl, CURLOPT_PROXY, "");
        curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, provider_open_socket);
    }
    curl_easy_setopt(curl, CURLOPT_RANGE, range_text);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, (long)PATCH_READ_BUFFER);
    curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, tune_socket);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, store);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &w);
    char detail[CURL_ERROR_SIZE] = "";
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, detail);
    CURLcode result = perform(curl, detail, !local && !nas);
    /* Sony's old sgst.* servers sometimes fail the TLS handshake or its certificate check in ways that
     * differ from the SHA-1 case above. A piece of a patch is only kept once its SHA-256 from the
     * manifest matches (the manifest itself is always fetched with full verification), so for the
     * pieces alone the transfer is repeated once without certificate verification. */
    if (!local && !nas && w.position == start &&
        (result == CURLE_PEER_FAILED_VERIFICATION || result == CURLE_SSL_CONNECT_ERROR ||
         result == CURLE_SSL_CERTPROBLEM || result == CURLE_SSL_CACERT_BADFILE || result == CURLE_SSL_CIPHER ||
         result == CURLE_SSL_ISSUER_ERROR)) {
        sony_range_close();
        curl_easy_reset(curl);
        held = curl;
        route_free(&route);
        route_apply(curl, address, sizeof address, &route);
        if (use_cache)
            dns_apply(curl, dns_name, &route);
        curl_easy_setopt(curl, CURLOPT_URL, address);
        common(curl, &ca, local, &watch);
        own_dns(curl, local, nas);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, "DEFAULT:@SECLEVEL=0");
        curl_easy_setopt(curl, CURLOPT_RANGE, range_text);
        curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, (long)PATCH_READ_BUFFER);
        curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, tune_socket);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, store);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &w);
        detail[0] = 0;
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, detail);
        result = curl_easy_perform(curl);
    }
    flush_buffer(&w); /* whatever arrived, also when the transfer failed or was stopped, so a retry resumes after it */
    w.position = w.durable;
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (result == CURLE_OK) {
        curl_off_t connect = 0, first = 0;
        char *ip = NULL;
        curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME_T, &connect);
        curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME_T, &first);
        atomic_fetch_add(&stat_connect_ns, (long long)connect * 1000);
        atomic_fetch_add(&stat_first_byte_ns, (long long)first * 1000);
        atomic_fetch_add(&stat_requests, 1);
        if (!curl_easy_getinfo(curl, CURLINFO_PRIMARY_IP, &ip) && ip) {
            if (use_cache && ip[0])
                dns_remember(dns_name, ip);
            pthread_mutex_lock(&stat_mutex);
            snprintf(stat_ip, sizeof stat_ip, "%s", ip);
            pthread_mutex_unlock(&stat_mutex);
        }
    }
    if (result != CURLE_OK) {
        if (use_cache && (result == CURLE_COULDNT_CONNECT || result == CURLE_COULDNT_RESOLVE_HOST))
            dns_forget(dns_name); /* the remembered address no longer answers: look it up again */
        sony_range_close(); /* never reuse a connection that just failed */
    }
    route_free(&route);
    free(storage);
    if (received)
        atomic_store_explicit(received, w.position, memory_order_relaxed);
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
                             _Atomic int64_t *received, sony_stop_fn stop, char *why, size_t why_cap) {
    return range(url, start, end, fd, file_offset, received, stop, false, why, why_cap);
}
sony_range_result nas_range(const char *url, int64_t start, int64_t end, int fd, int64_t file_offset,
                            _Atomic int64_t *received, sony_stop_fn stop, char *why, size_t why_cap) {
    return range(url, start, end, fd, file_offset, received, stop, true, why, why_cap);
}

/* One byte from the piece's address before the connections start, so a single lookup (and a single TLS
 * handshake) happens first and the sixteen that follow reuse its address instead of all resolving at once. */
void sony_warm(const char *url, sony_stop_fn stop) {
    const int sink = open("/dev/null", O_WRONLY);
    if (sink < 0)
        return;
    char why[120];
    for (int attempt = 0; attempt < 3; attempt++) {
        if (stop && stop())
            break;
        if (range(url, 0, 1, sink, 0, NULL, stop, false, why, sizeof why) == SONY_RANGE_OK)
            break;
    }
    sony_range_close();
    close(sink);
}
