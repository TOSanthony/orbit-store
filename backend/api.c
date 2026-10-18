#include "assets.h"
#include "orbit.h"
#include "debrid.h"
#include "receive.h"
#include "zero_probe.h"
#include "zero_companion.h"
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <microhttpd.h>
#include <netinet/in.h>
#include <poll.h>
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#ifdef ORBIT_TEST
#define ORBIT_PLATFORM "fixture"
#else
#define ORBIT_PLATFORM (orbit.desktop ? "desktop" : "ps5")
#endif
static struct MHD_Daemon *daemon_handle;
static pthread_t api_thread;
static atomic_bool api_stopping;
static bool api_thread_started;
static unsigned large_requests;
typedef struct {
    unsigned char *body;
    size_t length, limit;
    bool too_large, large;
} Request;
static enum MHD_Result respond(struct MHD_Connection *c, unsigned code, const char *body, size_t n,
                               const char *mime) {
    struct MHD_Response *r =
        MHD_create_response_from_buffer(n, (void *)body, MHD_RESPMEM_MUST_COPY);
    if (!r)
        return MHD_NO;
    MHD_add_response_header(r, "Content-Type", mime);
    MHD_add_response_header(r, "X-Content-Type-Options", "nosniff");
    MHD_add_response_header(r, "Referrer-Policy", "no-referrer");
    MHD_add_response_header(r, "Cache-Control", "no-store");
    MHD_add_response_header(r, "Content-Security-Policy",
                            "default-src 'self'; img-src 'self' https:; style-src 'self' "
                            "'unsafe-inline'; script-src 'self'; "
                            "connect-src 'self'; frame-ancestors 'none'; base-uri 'none'");
    enum MHD_Result rc = MHD_queue_response(c, code, r);
    MHD_destroy_response(r);
    return rc;
}
/* A large body the response takes over and frees itself, without copying it first. */
static enum MHD_Result respond_owned(struct MHD_Connection *c, unsigned code, unsigned char *body,
                                     size_t n, const char *mime, const char *version) {
    struct MHD_Response *r = MHD_create_response_from_buffer(n, body, MHD_RESPMEM_MUST_FREE);
    if (!r) {
        free(body);
        return MHD_NO;
    }
    MHD_add_response_header(r, "Content-Type", mime);
    if (version && *version)
        MHD_add_response_header(r, "X-Orbit-Version", version);
    MHD_add_response_header(r, "X-Content-Type-Options", "nosniff");
    MHD_add_response_header(r, "Referrer-Policy", "no-referrer");
    MHD_add_response_header(r, "Cache-Control", "no-store");
    enum MHD_Result rc = MHD_queue_response(c, code, r);
    MHD_destroy_response(r);
    return rc;
}
static enum MHD_Result json_response(struct MHD_Connection *c, unsigned code, cJSON *o) {
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!s)
        return MHD_NO;
    enum MHD_Result rc = respond(c, code, s, strlen(s), "application/json");
    free(s);
    return rc;
}
static enum MHD_Result error(struct MHD_Connection *c, unsigned code, const char *message) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "error", message);
    return json_response(c, code, o);
}
/* Only literals and names in local-use namespaces. Accepting every hostname
 * would let an internet origin reach the console through DNS rebinding. No DNS
 * lookup (or user-controlled outbound request) is needed to validate a Host. */
static bool local_host(const char *host) {
    if (!host || !*host || strlen(host) > 260)
        return false;
    char h[261];
    copy_text(h, sizeof h, host);
    char *p = strchr(h, ':');
    if (p) {
        *p++ = 0;
        unsigned port = 0;
        if (!*p) return false;
        for (; *p; p++) {
            if (*p < '0' || *p > '9' || port > 6553) return false;
            port = port * 10 + (unsigned)(*p - '0');
        }
        if (port != (unsigned)orbit.port) return false;
    }
    size_t length = strlen(h);
    if (!length || length > 253) return false;
    if (h[length - 1] == '.') h[--length] = 0;
    if (!length) return false;
    size_t label = 0;
    for (size_t i = 0; i < length; i++) {
        char ch = h[i];
        if (ch >= 'A' && ch <= 'Z') h[i] = ch = (char)(ch - 'A' + 'a');
        if (ch == '.') {
            if (!label || h[i - 1] == '-') return false;
            label = 0;
        } else {
            if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-') ||
                (!label && ch == '-') || ++label > 63) return false;
        }
    }
    if (!label || h[length - 1] == '-') return false;
    if (!strcmp(h, "localhost"))
        return true;
    struct in_addr a;
    if (inet_pton(AF_INET, h, &a) == 1) return true;
    static const char *suffixes[] = {".home.arpa", ".local", ".localhost"};
    for (size_t i = 0; i < sizeof suffixes / sizeof suffixes[0]; i++) {
        size_t n = strlen(suffixes[i]);
        if (length > n && !strcmp(h + length - n, suffixes[i])) return true;
    }
    return false;
}
static bool local_peer(struct MHD_Connection *c) {
    const union MHD_ConnectionInfo *i =
        MHD_get_connection_info(c, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
    if (!i || !i->client_addr || i->client_addr->sa_family != AF_INET)
        return false;
    const struct sockaddr_in *a = (const struct sockaddr_in *)i->client_addr;
    return (ntohl(a->sin_addr.s_addr) >> 24) == 127;
}
static bool authorized(struct MHD_Connection *c) {
    const char *h = MHD_lookup_connection_value(c, MHD_HEADER_KIND, "Authorization");
    if (!h || strncmp(h, "Bearer ", 7) || strlen(h + 7) != 64)
        return false;
    bool yes = false;
    pthread_mutex_lock(&orbit.mutex);
    for (size_t i = 0; i < orbit.token_count; i++)
        if (CRYPTO_memcmp(h + 7, orbit.tokens[i], 64) == 0)
            yes = true;
    pthread_mutex_unlock(&orbit.mutex);
    return yes;
}
/* The console's LAN address, which the TV app shows so a phone knows where to open Orbit: the
 * source address the default route would use. Connecting a UDP socket sends nothing, and
 * 192.0.2.1 (TEST-NET-1) is never contacted. Empty without a route. */
static void lan_address(char *out, size_t cap) {
    out[0] = 0;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return;
    struct sockaddr_in peer = {0}, self = {0};
    peer.sin_family = AF_INET;
    peer.sin_port = htons(53);
    inet_pton(AF_INET, "192.0.2.1", &peer.sin_addr);
    socklen_t length = sizeof self;
    if (!connect(fd, (const struct sockaddr *)&peer, sizeof peer) &&
        !getsockname(fd, (struct sockaddr *)&self, &length) && self.sin_family == AF_INET &&
        self.sin_addr.s_addr != 0 && (ntohl(self.sin_addr.s_addr) >> 24) != 127 &&
        !inet_ntop(AF_INET, &self.sin_addr, out, (socklen_t)cap))
        out[0] = 0;
    close(fd);
}
static cJSON *storage_json(void) {
    Storage s[ORBIT_MAX_STORAGE];
    size_t n = storage_list(s);
    cJSON *a = cJSON_CreateArray();
    pthread_mutex_lock(&orbit.mutex);
    for (size_t i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", s[i].id);
        cJSON_AddStringToObject(o, "label", s[i].label);
        char path[540];
        snprintf(path, sizeof path, "%s/homebrew", s[i].root);
        cJSON_AddStringToObject(o, "path", path);
        cJSON_AddNumberToObject(o, "freeBytes", (double)s[i].free_bytes);
        cJSON_AddNumberToObject(o, "totalBytes", (double)s[i].total_bytes);
        uint64_t pending = storage_pending_locked(&s[i], NULL);
        cJSON_AddNumberToObject(o, "pendingBytes", (double)pending);
        cJSON_AddNumberToObject(o, "projectedFreeBytes", (double)s[i].free_bytes - (double)pending);
        cJSON_AddBoolToObject(o, "external", s[i].external);
        cJSON_AddItemToArray(a, o);
    }
    pthread_mutex_unlock(&orbit.mutex);
    return a;
}
typedef struct { unsigned host, origin, site; } AuthorityHeaders;
static enum MHD_Result authority_header(void *cls, enum MHD_ValueKind kind,
                                      const char *key, const char *value) {
    (void)kind; (void)value;
    AuthorityHeaders *headers = cls;
    if (!strcasecmp(key, "Host")) headers->host++;
    if (!strcasecmp(key, "Origin")) headers->origin++;
    if (!strcasecmp(key, "Sec-Fetch-Site")) headers->site++;
    return MHD_YES;
}
static enum MHD_Result route(struct MHD_Connection *c, const char *url, const char *method,
                             Request *req) {
    AuthorityHeaders authority = {0};
    MHD_get_connection_values(c, MHD_HEADER_KIND, authority_header, &authority);
    if (authority.host != 1 || authority.origin > 1 || authority.site > 1)
        return error(c, 403, "Origin does not match this console.");
    const char *host = MHD_lookup_connection_value(c, MHD_HEADER_KIND, "Host");
    if (!local_host(host))
        return error(c, 403, "Use the console IP address or a .local or .home.arpa hostname to access Orbit.");
    bool get = !strcmp(method, "GET"), post = !strcmp(method, "POST");
    if (!get && !post)
        return error(c, 405, "Method not supported.");
    const char *origin = MHD_lookup_connection_value(c, MHD_HEADER_KIND, "Origin");
    char expected[270];
    snprintf(expected, sizeof expected, "http://%s", host);
    if (origin && strcasecmp(origin, expected))
        return error(c, 403, "Origin does not match this console.");
    const char *site = MHD_lookup_connection_value(c, MHD_HEADER_KIND, "Sec-Fetch-Site");
    if (!strncmp(url, "/api/", 5) && site && strcmp(site, "same-origin") && strcmp(site, "none"))
        return error(c, 403, "Open Orbit directly on this console or your local network.");
    if (post) {
        const char *ct = MHD_lookup_connection_value(c, MHD_HEADER_KIND, "Content-Type");
        if (!ct || (strcmp(url, "/api/v1/receive/chunk") ? strncasecmp(ct, "application/json", 16) : strcmp(ct, "application/octet-stream")))
            return error(c, 415, "Use application/json.");
    }
    /* Workers may release their snapshots during shutdown. Keep only the
     * retained service/update status readable while those workers drain. */
    if (orbit_is_stopping() && (post ||
        (!strncmp(url, "/api/", 5) && strcmp(url, "/api/v1/system") &&
         strcmp(url, "/api/v1/updates"))))
        return error(c, 503, "Orbit is stopping. Wait for shutdown before starting the saved payload.");
    if (!strcmp(url, "/api/v1/zero/companion")) {
        if (!authorized(c)) return error(c,401,"Pair this device first.");
        if (get) return json_response(c,200,zero_companion_status(local_peer(c)));
        const union MHD_ConnectionInfo *info=MHD_get_connection_info(c,MHD_CONNECTION_INFO_CLIENT_ADDRESS);
        char peer[INET_ADDRSTRLEN]="",message[192]="";
        if(info && info->client_addr && info->client_addr->sa_family==AF_INET)
            inet_ntop(AF_INET,&((const struct sockaddr_in *)info->client_addr)->sin_addr,peer,sizeof peer);
        cJSON *input=cJSON_Parse((const char *)req->body);
        int code=zero_companion_update(input,peer,local_peer(c),message,sizeof message);cJSON_Delete(input);
        return code>=400?error(c,code,message):json_response(c,200,zero_companion_status(false));
    }
    if (!strcmp(url, "/api/v1/zero/probe")) {
        if (!authorized(c)) return error(c,401,"Pair this device first.");
        if (get) return json_response(c,200,zero_probe_status());
        cJSON *input = cJSON_Parse((const char *)req->body); char message[256] = {0};
        int code = zero_probe_request(json_text(input,"releaseId"),message,sizeof message); cJSON_Delete(input);
        return code >= 400 ? error(c,code,message) : json_response(c,code,zero_probe_status());
    }
    if (!strncmp(url, "/api/v1/receive", 15) && (!url[15] || url[15] == '/')) {
        if (!authorized(c)) return error(c, 401, "Pair this device first.");
        const char *auth = MHD_lookup_connection_value(c, MHD_HEADER_KIND, "Authorization");
        unsigned char hash[32]; unsigned length = 0; char owner[65];
        if (!EVP_Digest(auth + 7, 64, hash, &length, EVP_sha256(), NULL) || length != 32)
            return error(c, 503, "Could not identify the paired client.");
        for (int i = 0; i < 32; i++) snprintf(owner + i * 2, 3, "%02x", hash[i]);
        if (get && !strcmp(url, "/api/v1/receive")) return json_response(c, 200, receive_status(owner));
        if (!post) return error(c, 405, "Method not supported.");
        char message[256] = {0}; int code;
        if (!strcmp(url, "/api/v1/receive/bundle")) {
            cJSON *input = cJSON_Parse((const char *)req->body);
            if (!cJSON_IsObject(input)) { cJSON_Delete(input); return error(c, 400, "Invalid receiver request."); }
            code = receive_bundle(json_text(input, "action"), input, owner, message, sizeof message);
            cJSON_Delete(input);
        } else if (!strcmp(url, "/api/v1/receive/chunk")) {
            code = receive_chunk(MHD_lookup_connection_value(c,MHD_HEADER_KIND,"X-Orbit-Transfer"),
                MHD_lookup_connection_value(c,MHD_HEADER_KIND,"X-Orbit-Offset"),
                MHD_lookup_connection_value(c,MHD_HEADER_KIND,"X-Orbit-SHA256"),
                &req->body, req->length, owner, message, sizeof message);
        } else {
            const char *action = !strcmp(url,"/api/v1/receive") ? "begin" :
                !strcmp(url,"/api/v1/receive/resume") ? "resume" :
                !strcmp(url,"/api/v1/receive/finish") ? "finish" :
                !strcmp(url,"/api/v1/receive/cancel") ? "cancel" :
                !strcmp(url,"/api/v1/receive/abandon") ? "abandon" : "";
            cJSON *input = cJSON_Parse((const char *)req->body);
            if (!*action || !cJSON_IsObject(input)) { cJSON_Delete(input); return error(c,400,"Invalid receiver request."); }
            code = receive_command(action,input,owner,message,sizeof message); cJSON_Delete(input);
        }
        return code >= 400 ? error(c,code,message) : json_response(c,code,receive_status(owner));
    }
    if (get && !strcmp(url, "/api/v1/system/payload")) {
        /* The saved copy, for the TV app to hand to the ELF loader when it restarts Orbit: the
         * app cannot open /data/orbit-store itself. Served to this console only. */
        if (!authorized(c)) return error(c, 401, "Pair this device first.");
        if (!local_peer(c)) return error(c, 403, "The saved copy is only sent to this console.");
        unsigned char *image = NULL;
        size_t length = 0;
        if (!orbit.autoboot || autoboot_saved_image(&image, &length))
            return error(c, 404, "Orbit has no saved copy to send.");
        char saved[64] = {0};
        autoboot_saved_version(saved, sizeof saved);
        return respond_owned(c, 200, image, length, "application/octet-stream", saved);
    }
    if (get && !strcmp(url, "/api/v1/system")) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", "Orbit Store");
        cJSON_AddStringToObject(o, "version", ORBIT_VERSION);
        cJSON_AddStringToObject(o, "instanceId", orbit.instance_id);
        cJSON_AddBoolToObject(o, "stopping", orbit_is_stopping());
        cJSON_AddNumberToObject(o, "receiveProtocol", 1);
        /* Folder transfers are an addition; protocol 1 clients are unaffected. */
        cJSON_AddNumberToObject(o, "receiveFolders", 1);
        cJSON_AddBoolToObject(o, "receiveAvailable", receive_available());
        cJSON_AddStringToObject(o, "platform", ORBIT_PLATFORM);
        cJSON_AddBoolToObject(o, "paired", authorized(c));
        cJSON_AddBoolToObject(o, "localSessionAvailable", local_peer(c));
#if defined(ORBIT_DESKTOP) && !defined(ORBIT_TEST)
        cJSON_AddBoolToObject(o, "pairNotificationAvailable", false);
#else
        cJSON_AddBoolToObject(o, "pairNotificationAvailable", true);
#endif
        cJSON_AddBoolToObject(o, "consoleValidated", false);
        cJSON_AddStringToObject(o, "targetFirmware", "12.60 / Relapse");
        pthread_mutex_lock(&orbit.mutex);
        cJSON_AddBoolToObject(o, "stateHealthy", !orbit.state_failed);
        cJSON_AddNumberToObject(o, "catalogueRevision", orbit.catalog_revision);
        cJSON_AddStringToObject(o, "preferredStorage", orbit.preferred_storage);
        cJSON_AddBoolToObject(o, "deleteArchives", orbit.delete_archives);
        cJSON_AddStringToObject(o, "appearance", orbit.appearance);
        cJSON_AddStringToObject(o, "language", orbit.language);
        cJSON_AddStringToObject(o, "keyboard", orbit.keyboard);
        cJSON_AddBoolToObject(o, "soundsOff", orbit.sounds_off);
        cJSON_AddBoolToObject(o, "ps4Hidden", orbit.ps4_hidden);
        cJSON_AddBoolToObject(o, "installPkgs", orbit.install_pkgs);
        cJSON_AddBoolToObject(o, "deletePkgs", orbit.delete_pkgs);
        cJSON_AddNumberToObject(o, "concurrentDownloads", orbit.concurrent_downloads);
        cJSON_AddNumberToObject(o, "httpPort", orbit.port);
        cJSON_AddStringToObject(o, "launcherStatus", orbit.launcher_status);
        cJSON_AddStringToObject(o, "launcherError", orbit.launcher_error);
        cJSON_AddStringToObject(o, "launcherRegistrationMethod",
                                orbit.launcher_registration_method);
        pthread_mutex_unlock(&orbit.mutex);
        return json_response(c, 200, o);
    }
    if (get && !strcmp(url, "/api/v1/catalog/updates"))
        return json_response(c, 200, catalog_status());
    if (get && !strcmp(url, "/api/v1/catalog")) {
        cJSON *a = cJSON_CreateArray(), *x;
        pthread_mutex_lock(&orbit.mutex);
        /* The signed catalogue first, then the person's own NAS files. */
        const cJSON *lists[2] = {orbit.catalog, orbit.nas_catalog};
        for (size_t list = 0; list < 2; list++)
        cJSON_ArrayForEach(x, lists[list]) {
            if (!source_enabled_locked(release_find(json_text(x, "id"))))
                continue;
            cJSON *entry = cJSON_Duplicate(x, true);
            cJSON_DeleteItemFromObjectCaseSensitive(entry, "url");
            cJSON_DeleteItemFromObjectCaseSensitive(entry, "browserUrl");
            cJSON_AddBoolToObject(entry, "browserAvailable",
                provider_browser_supported(release_find(json_text(x, "id"))));
            cJSON_AddBoolToObject(entry, "directAvailable",
                provider_url_supported(release_find(json_text(x, "id"))));
            cJSON_AddItemToArray(a, entry);
        }
        pthread_mutex_unlock(&orbit.mutex);
        return json_response(c, 200, a);
    }
    if (get && !strcmp(url, "/api/v1/sources")) {
        pthread_mutex_lock(&orbit.mutex);
        cJSON *o = sources_json_locked();
        pthread_mutex_unlock(&orbit.mutex);
        return json_response(c, 200, o);
    }
    if (get && !strcmp(url, "/api/v1/session")) {
        if (!local_peer(c))
            return error(c, 403, "Pair this device using the code shown on your PS5.");
        char address[INET_ADDRSTRLEN];
        lan_address(address, sizeof address);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "address", address);
        pthread_mutex_lock(&orbit.mutex);
        if (!orbit.token_count) {
            random_hex(orbit.tokens[0], 32);
            orbit.token_count = 1;
        }
        int rc = state_save_locked();
        cJSON_AddStringToObject(o, "token", orbit.tokens[0]);
        cJSON_AddStringToObject(o, "pairCode", orbit.pair_code);
        pthread_mutex_unlock(&orbit.mutex);
        if (rc) {
            cJSON_Delete(o);
            return error(c, 503, "Could not save pairing.");
        }
        return json_response(c, 200, o);
    }
    cJSON *input = NULL;
    if (post) {
        input = cJSON_Parse((const char *)req->body);
        if (!cJSON_IsObject(input)) {
            cJSON_Delete(input);
            return error(c, 400, "Expected a JSON object.");
        }
    }
    if (post && !strcmp(url, "/api/v1/pair/show")) {
        cJSON_Delete(input);
        struct timespec clock;
        if (clock_gettime(CLOCK_MONOTONIC, &clock))
            return error(c, 503, "Could not show the pairing code. Try again.");
        pthread_mutex_lock(&orbit.mutex);
        time_t remaining = orbit.pair_notify_after - clock.tv_sec;
        int rc = 0;
        if (remaining <= 0) {
            rc = pairing_notify();
            /* Failed notifications are throttled too, so repeated requests cannot flood the API. */
            orbit.pair_notify_after = clock.tv_sec + 30;
        }
        pthread_mutex_unlock(&orbit.mutex);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "retryAfter", remaining > 0 ? (double)remaining : 30);
        if (remaining > 0)
            cJSON_AddStringToObject(o, "error", "Wait a moment before showing the code again.");
        else if (rc)
            cJSON_AddStringToObject(
                o, "error",
                "Could not show a notification. Open Pair devices in Orbit on your PS5.");
        else
            cJSON_AddBoolToObject(o, "shown", true);
        /* Only the console displays the code. This response never grants a session or token. */
        return json_response(c, remaining > 0 ? 429 : rc ? 503 : 200, o);
    }
    if (post && !strcmp(url, "/api/v1/pair")) {
        pthread_mutex_lock(&orbit.mutex);
        time_t now = time(NULL);
        int code = 200;
        char token[65] = {0};
        if (now < orbit.pair_locked_until)
            code = 429;
        else if (strlen(json_text(input, "code")) != 6 ||
                 CRYPTO_memcmp(json_text(input, "code"), orbit.pair_code, 6) != 0) {
            code = 403;
            if (++orbit.pair_failures >= 5) {
                orbit.pair_locked_until = now + 60;
                orbit.pair_failures = 0;
            }
        } else if (orbit.token_count == 16)
            code = 409;
        else {
            random_hex(token, 32);
            copy_text(orbit.tokens[orbit.token_count++], 65, token);
            orbit.pair_failures = 0;
            if (state_save_locked()) {
                orbit.token_count--;
                code = 503;
            }
        }
        pthread_mutex_unlock(&orbit.mutex);
        cJSON_Delete(input);
        if (code != 200)
            return error(c, (unsigned)code,
                         code == 429   ? "Too many attempts. Wait one minute."
                         : code == 409 ? "Pairing slots are full."
                                       : "Pairing failed. Check the console code.");
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "token", token);
        return json_response(c, 200, o);
    }
    if (!strncmp(url, "/api/", 5)) {
        if (!authorized(c)) {
            cJSON_Delete(input);
            return error(c, 401, "Pair this device with your PS5 to continue.");
        }
        if (get && !strcmp(url, "/api/v1/storage"))
            return json_response(c, 200, storage_json());
        if ((get || post) && !strcmp(url, "/api/v1/settings/downloads")) {
            const cJSON *limit = cJSON_GetObjectItemCaseSensitive(input, "concurrentDownloads");
            if (post && (!cJSON_IsNumber(limit) || limit->valuedouble < 1 ||
                limit->valuedouble > ORBIT_MAX_DOWNLOADS || limit->valuedouble != limit->valueint)) {
                cJSON_Delete(input);
                return error(c, 400, "Choose a whole number from 1 to 8.");
            }
            pthread_mutex_lock(&orbit.mutex);
            unsigned previous = orbit.concurrent_downloads;
            if (post) orbit.concurrent_downloads = (unsigned)limit->valueint;
            int rc = post ? state_save_locked() : 0;
            if (rc) orbit.concurrent_downloads = previous;
            else if (post) pthread_cond_broadcast(&orbit.changed);
            cJSON *o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "concurrentDownloads", orbit.concurrent_downloads);
            pthread_mutex_unlock(&orbit.mutex);
            cJSON_Delete(input);
            if (rc) {
                cJSON_Delete(o);
                return error(c, 503, "Could not save the setting.");
            }
            return json_response(c, 200, o);
        }
        if ((get || post) && !strcmp(url, "/api/v1/settings/sounds")) {
            const cJSON *value = cJSON_GetObjectItemCaseSensitive(input, "soundsOff");
            if (post && !cJSON_IsBool(value)) {
                cJSON_Delete(input);
                return error(c, 400, "Expected soundsOff.");
            }
            pthread_mutex_lock(&orbit.mutex);
            bool previous = orbit.sounds_off;
            if (post) orbit.sounds_off = cJSON_IsTrue(value);
            int rc = post ? state_save_locked() : 0;
            if (rc) orbit.sounds_off = previous;
            cJSON *o = cJSON_CreateObject();
            cJSON_AddBoolToObject(o, "soundsOff", orbit.sounds_off);
            pthread_mutex_unlock(&orbit.mutex);
            cJSON_Delete(input);
            if (rc) {
                cJSON_Delete(o);
                return error(c, 503, "Could not save the setting.");
            }
            return json_response(c, 200, o);
        }
        if ((get || post) && !strcmp(url, "/api/v1/settings/delete-pkgs")) {
            const cJSON *value = cJSON_GetObjectItemCaseSensitive(input, "deletePkgs");
            if (post && !cJSON_IsBool(value)) {
                cJSON_Delete(input);
                return error(c, 400, "Expected deletePkgs.");
            }
            pthread_mutex_lock(&orbit.mutex);
            bool previous = orbit.delete_pkgs;
            if (post) orbit.delete_pkgs = cJSON_IsTrue(value);
            int rc = post ? state_save_locked() : 0;
            if (rc) orbit.delete_pkgs = previous;
            cJSON *o = cJSON_CreateObject();
            cJSON_AddBoolToObject(o, "deletePkgs", orbit.delete_pkgs);
            pthread_mutex_unlock(&orbit.mutex);
            cJSON_Delete(input);
            if (rc) {
                cJSON_Delete(o);
                return error(c, 503, "Could not save the setting.");
            }
            return json_response(c, 200, o);
        }
        if ((get || post) && !strcmp(url, "/api/v1/settings/install-pkgs")) {
            const cJSON *value = cJSON_GetObjectItemCaseSensitive(input, "installPkgs");
            if (post && !cJSON_IsBool(value)) {
                cJSON_Delete(input);
                return error(c, 400, "Expected installPkgs.");
            }
            pthread_mutex_lock(&orbit.mutex);
            bool previous = orbit.install_pkgs;
            if (post) orbit.install_pkgs = cJSON_IsTrue(value);
            int rc = post ? state_save_locked() : 0;
            if (rc) orbit.install_pkgs = previous;
            cJSON *o = cJSON_CreateObject();
            cJSON_AddBoolToObject(o, "installPkgs", orbit.install_pkgs);
            pthread_mutex_unlock(&orbit.mutex);
            cJSON_Delete(input);
            if (rc) {
                cJSON_Delete(o);
                return error(c, 503, "Could not save the setting.");
            }
            return json_response(c, 200, o);
        }
        if ((get || post) && !strcmp(url, "/api/v1/settings/ps4")) {
            const cJSON *value = cJSON_GetObjectItemCaseSensitive(input, "ps4Hidden");
            if (post && !cJSON_IsBool(value)) {
                cJSON_Delete(input);
                return error(c, 400, "Expected ps4Hidden.");
            }
            pthread_mutex_lock(&orbit.mutex);
            bool previous = orbit.ps4_hidden;
            if (post) orbit.ps4_hidden = cJSON_IsTrue(value);
            int rc = post ? state_save_locked() : 0;
            if (rc) orbit.ps4_hidden = previous;
            cJSON *o = cJSON_CreateObject();
            cJSON_AddBoolToObject(o, "ps4Hidden", orbit.ps4_hidden);
            pthread_mutex_unlock(&orbit.mutex);
            cJSON_Delete(input);
            if (rc) {
                cJSON_Delete(o);
                return error(c, 503, "Could not save the setting.");
            }
            return json_response(c, 200, o);
        }
        if ((get || post) && !strcmp(url, "/api/v1/settings/keyboard")) {
            const cJSON *value = cJSON_GetObjectItemCaseSensitive(input, "keyboard");
            bool valid = cJSON_IsString(value) && value->valuestring &&
                         (!strcmp(value->valuestring, "auto") || !strcmp(value->valuestring, "qwerty") ||
                          !strcmp(value->valuestring, "azerty"));
            if (post && !valid) {
                cJSON_Delete(input);
                return error(c, 400, "Expected keyboard.");
            }
            pthread_mutex_lock(&orbit.mutex);
            char previous[sizeof orbit.keyboard];
            memcpy(previous, orbit.keyboard, sizeof previous);
            if (post) snprintf(orbit.keyboard, sizeof orbit.keyboard, "%s", value->valuestring);
            int rc = post ? state_save_locked() : 0;
            if (rc) memcpy(orbit.keyboard, previous, sizeof previous);
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "keyboard", orbit.keyboard);
            pthread_mutex_unlock(&orbit.mutex);
            cJSON_Delete(input);
            if (rc) {
                cJSON_Delete(o);
                return error(c, 503, "Could not save the setting.");
            }
            return json_response(c, 200, o);
        }
        if ((get || post) && !strcmp(url, "/api/v1/settings/language")) {
            const cJSON *value = cJSON_GetObjectItemCaseSensitive(input, "language");
            bool valid = cJSON_IsString(value) && value->valuestring && value->valuestring[0] &&
                         strlen(value->valuestring) < sizeof orbit.language;
            for (const char *p = valid ? value->valuestring : ""; *p; p++)
                if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '-')) valid = false;
            if (post && !valid) {
                cJSON_Delete(input);
                return error(c, 400, "Expected language.");
            }
            pthread_mutex_lock(&orbit.mutex);
            char previous[sizeof orbit.language];
            memcpy(previous, orbit.language, sizeof previous);
            if (post) snprintf(orbit.language, sizeof orbit.language, "%s", value->valuestring);
            int rc = post ? state_save_locked() : 0;
            if (rc) memcpy(orbit.language, previous, sizeof previous);
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "language", orbit.language);
            pthread_mutex_unlock(&orbit.mutex);
            cJSON_Delete(input);
            if (rc) {
                cJSON_Delete(o);
                return error(c, 503, "Could not save the setting.");
            }
            return json_response(c, 200, o);
        }
        if ((get || post) && !strcmp(url, "/api/v1/settings/appearance")) {
            const cJSON *value = cJSON_GetObjectItemCaseSensitive(input, "appearance");
            bool valid = cJSON_IsString(value) && value->valuestring && value->valuestring[0] &&
                         strlen(value->valuestring) < sizeof orbit.appearance;
            for (const char *p = valid ? value->valuestring : ""; *p; p++)
                if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-')) valid = false;
            if (post && !valid) {
                cJSON_Delete(input);
                return error(c, 400, "Expected appearance.");
            }
            pthread_mutex_lock(&orbit.mutex);
            char previous[sizeof orbit.appearance];
            memcpy(previous, orbit.appearance, sizeof previous);
            if (post) snprintf(orbit.appearance, sizeof orbit.appearance, "%s", value->valuestring);
            int rc = post ? state_save_locked() : 0;
            if (rc) memcpy(orbit.appearance, previous, sizeof previous);
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "appearance", orbit.appearance);
            pthread_mutex_unlock(&orbit.mutex);
            cJSON_Delete(input);
            if (rc) {
                cJSON_Delete(o);
                return error(c, 503, "Could not save the setting.");
            }
            return json_response(c, 200, o);
        }
        if ((get || post) && !strcmp(url, "/api/v1/settings/extraction")) {
            const cJSON *value = cJSON_GetObjectItemCaseSensitive(input, "deleteArchives");
            if (post && !cJSON_IsBool(value)) {
                cJSON_Delete(input);
                return error(c, 400, "Expected deleteArchives.");
            }
            pthread_mutex_lock(&orbit.mutex);
            bool previous = orbit.delete_archives;
            if (post) orbit.delete_archives = cJSON_IsTrue(value);
            int rc = post ? state_save_locked() : 0;
            if (rc) orbit.delete_archives = previous;
            cJSON *o = cJSON_CreateObject();
            cJSON_AddBoolToObject(o, "deleteArchives", orbit.delete_archives);
            pthread_mutex_unlock(&orbit.mutex);
            cJSON_Delete(input);
            if (rc) {
                cJSON_Delete(o);
                return error(c, 503, "Could not save the setting.");
            }
            return json_response(c, 200, o);
        }
        if (post && !strcmp(url, "/api/v1/storage/preferred")) {
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(input, "storageId");
            char e[256] = {0}, chosen[24] = {0};
            int code = 400;
            if (cJSON_IsString(id) && strlen(id->valuestring) < sizeof chosen) {
                copy_text(chosen, sizeof chosen, id->valuestring);
                code = storage_prefer(chosen, e, sizeof e);
            }
            cJSON_Delete(input);
            if (code != 200)
                return error(c, (unsigned)code, code == 400 ? "Expected storageId." : e);
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "preferredStorage", chosen);
            return json_response(c, 200, o);
        }
        if (get && !strncmp(url, "/api/v1/art/", 12)) {
            /* Catalogue artwork for the native TV app (backend/art.c). */
            char game[64] = {0}, kind[8] = {0}, trailing;
            if (sscanf(url + 12, "%63[^/]/%7[^/]%c", game, kind, &trailing) != 2)
                return error(c, 404, "Unknown artwork.");
            unsigned char *data = NULL;
            size_t size = 0;
            char mime[16] = {0}, why[96] = {0};
            int code = art_get(game, kind, &data, &size, mime, sizeof mime, why, sizeof why);
            if (code == 200) {
                enum MHD_Result rc = respond(c, 200, (const char *)data, size, mime);
                free(data);
                return rc;
            }
            if (code == 202) {
                cJSON *o = cJSON_CreateObject();
                cJSON_AddBoolToObject(o, "pending", true);
                return json_response(c, 202, o);
            }
            char message[160];
            snprintf(message, sizeof message, "%s%s%s%s",
                     code == 404   ? "Unknown artwork."
                     : code == 503 ? "Artwork is busy. Try again shortly."
                                   : "This artwork could not be loaded.",
                     *why ? " (" : "", why, *why ? ")" : "");
            return error(c, (unsigned)code, message);
        }
        if (get && !strcmp(url, "/api/v1/debrid"))
            return json_response(c, 200, debrid_status());
        if (post && !strcmp(url, "/api/v1/debrid")) {
            char e[256] = {0};
            int code = debrid_action(input, e, sizeof e);
            cJSON *key = cJSON_GetObjectItemCaseSensitive(input, "apiKey");
            if (cJSON_IsString(key)) OPENSSL_cleanse(key->valuestring, strlen(key->valuestring));
            cJSON_Delete(input);
            return code == 202 ? json_response(c, 202, debrid_status()) : error(c, (unsigned)code, e);
        }
        if (get && !strcmp(url, "/api/v1/browser"))
            return json_response(c, 200, browser_status());
        if (post && !strcmp(url, "/api/v1/browser")) {
            char e[256] = {0};
            int code = browser_action(input, e, sizeof e);
            cJSON_Delete(input);
            return code == 202 ? json_response(c, 202, browser_status()) : error(c, (unsigned)code, e);
        }
        if (get && !strcmp(url, "/api/v1/diagnostics"))
            return json_response(c, 200, diagnostics_snapshot());
        if ((get || post) && !strcmp(url, "/api/v1/library")) {
            cJSON_Delete(input);
            return json_response(c, 200, library_snapshot(post));
        }
        if ((get || post) && !strcmp(url, "/api/v1/library/storage")) {
            cJSON_Delete(input);
            return json_response(c, 200, library_storage_snapshot(post));
        }
        if (post && !strcmp(url, "/api/v1/library/actions")) {
            char e[256] = {0};
            int code = library_action(input, e, sizeof e);
            cJSON_Delete(input);
            return code == 202 ? json_response(c, 202, library_snapshot(false))
                               : error(c, (unsigned)code, e);
        }
        if ((get || post) && !strcmp(url, "/api/v1/favorites")) {
            char e[256] = {0};
            pthread_mutex_lock(&orbit.mutex);
            int code = post ? favorite_set_locked(input, e, sizeof e) : 200;
            cJSON *o = code == 200 ? favorites_json_locked() : NULL;
            pthread_mutex_unlock(&orbit.mutex);
            cJSON_Delete(input);
            return o ? json_response(c, 200, o) : error(c, (unsigned)code, e);
        }
        if (post && !strcmp(url, "/api/v1/downloads/clear-history")) {
            char e[256] = {0};
            pthread_mutex_lock(&orbit.mutex);
            int code = history_clear_locked(input, e, sizeof e);
            cJSON *o = code == 200 ? jobs_json_locked() : NULL;
            pthread_mutex_unlock(&orbit.mutex);
            cJSON_Delete(input);
            return o ? json_response(c, 200, o) : error(c, (unsigned)code, e);
        }
        if (post && !strcmp(url, "/api/v1/sources")) {
            char e[256] = {0};
            pthread_mutex_lock(&orbit.mutex);
            int code = sources_set_locked(input, e, sizeof e);
            cJSON *o = code == 200 ? sources_json_locked() : NULL;
            pthread_mutex_unlock(&orbit.mutex);
            cJSON_Delete(input);
            return o ? json_response(c, 200, o) : error(c, (unsigned)code, e);
        }
        if (get && !strcmp(url, "/api/v1/downloads")) {
            pthread_mutex_lock(&orbit.mutex);
            cJSON *o = jobs_json_locked();
            pthread_mutex_unlock(&orbit.mutex);
            return json_response(c, 200, o);
        }
        if (post && !strcmp(url, "/api/v1/downloads")) {
            const cJSON *delivery = cJSON_GetObjectItemCaseSensitive(input, "delivery");
            if (delivery && !cJSON_IsString(delivery)) {
                cJSON_Delete(input);
                return error(c, 400, "Choose direct or torbox delivery.");
            }
            char e[256] = {0};
            Job *j = NULL;
            pthread_mutex_lock(&orbit.mutex);
            int code = job_create_delivery_locked(json_text(input, "releaseId"),
                                         json_text(input, "storageId"), json_text(input, "delivery"), e, sizeof e, &j);
            cJSON *o = NULL;
            if (code == 201) {
                o = cJSON_CreateObject();
                cJSON_AddStringToObject(o, "id", j->id);
            }
            pthread_mutex_unlock(&orbit.mutex);
            cJSON_Delete(input);
            return o ? json_response(c, 201, o) : error(c, (unsigned)code, e);
        }
        if (post && !strcmp(url, "/api/v1/catalog/updates")) {
            char e[256] = {0};
            int code =
                !strcmp(json_text(input, "action"), "refresh") ? catalog_refresh(e, sizeof e) : 400;
            cJSON_Delete(input);
            return code == 202 ? json_response(c, 202, catalog_status())
                               : error(c, (unsigned)code, *e ? e : "Expected refresh action.");
        }
        if (get && !strcmp(url, "/api/v1/game-updates"))
            return json_response(c, 200, gameupdates_status());
        if (post && !strcmp(url, "/api/v1/game-updates")) {
            char e[256] = {0};
            int code = gameupdates_action(input, e, sizeof e);
            cJSON_Delete(input);
            return code == 202 ? json_response(c, 202, gameupdates_status())
                               : error(c, (unsigned)code, e);
        }
        if (get && !strcmp(url, "/api/v1/updates"))
            return json_response(c, 200, updates_status());
        if (post && !strcmp(url, "/api/v1/updates")) {
            char e[256] = {0};
            int code = updates_action(input, e, sizeof e);
            cJSON_Delete(input);
            return code == 202 ? json_response(c, 202, updates_status())
                               : error(c, (unsigned)code, e);
        }
        if (get && !strcmp(url, "/api/v1/tv-app"))
            return json_response(c, 200, tvapp_status());
        if (post && !strcmp(url, "/api/v1/tv-app")) {
            char e[256] = {0};
            int code = tvapp_action(input, e, sizeof e);
            cJSON_Delete(input);
            return code == 202 ? json_response(c, 202, tvapp_status())
                               : error(c, (unsigned)code, e);
        }
        if (get && !strcmp(url, "/api/v1/autoboot"))
            return json_response(c, 200, autoboot_status());
        if (post && (!strcmp(url, "/api/v1/autoboot") || !strcmp(url, "/api/v1/integrations"))) {
            char e[256] = {0};
            const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(input, "enabled");
            int code = !cJSON_IsBool(enabled) ? 400 :
                !strcmp(url, "/api/v1/integrations")
                    ? integration_set(json_text(input, "manager"), cJSON_IsTrue(enabled), e, sizeof e)
                    : autoboot_set(json_text(input, "manager"), cJSON_IsTrue(enabled), e, sizeof e);
            cJSON_Delete(input);
            if (code == 200)
                return json_response(c, 200, autoboot_status());
            return error(c, (unsigned)code, code == 400 ? "Expected manager and enabled." : e);
        }
        if (post && !strncmp(url, "/api/v1/downloads/", 18)) {
            char id[24] = {0}, action[24] = {0}, trailing;
            int parsed = sscanf(url + 18, "%23[^/]/%23[^/]%c", id, action, &trailing);
            if (parsed != 2) {
                cJSON_Delete(input);
                return error(c, 404, "Unknown download action.");
            }
            char e[256] = {0};
            if (!strcmp(action, "refresh-link")) {
                cJSON_Delete(input);
                input = cJSON_CreateObject();
                cJSON_AddStringToObject(input, "action", "refresh");
                cJSON_AddStringToObject(input, "jobId", id);
                int code = browser_action(input, e, sizeof e);
                cJSON_Delete(input);
                if (code != 202) return error(c, (unsigned)code, e);
                pthread_mutex_lock(&orbit.mutex);
                cJSON *jobs = jobs_json_locked();
                pthread_mutex_unlock(&orbit.mutex);
                return json_response(c, 200, jobs);
            }
            pthread_mutex_lock(&orbit.mutex);
            Job *j = job_find(id);
            int code =
                j ? !strcmp(action, "archive-password") ? job_password_locked(j, json_text(input, "password"), e, sizeof e) : job_action_locked(
                        j, action,
                        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(input, "deletePartial")), e,
                        sizeof e)
                  : 404;
            cJSON *o = code == 200 ? jobs_json_locked() : NULL;
            pthread_mutex_unlock(&orbit.mutex);
            cJSON_Delete(input);
            return o ? json_response(c, 200, o)
                     : error(c, (unsigned)code, j ? e : "Unknown download.");
        }
        cJSON_Delete(input);
        return error(c, 404, "Unknown endpoint.");
    }
    cJSON_Delete(input);
    if (!get)
        return error(c, 405, "Method not supported.");
    const char *path = !strcmp(url, "/") ? "/index.html" : url;
    for (size_t i = 0; i < sizeof assets / sizeof assets[0]; i++)
        if (!strcmp(path, assets[i].path))
            return respond(c, 200, (const char *)assets[i].data, assets[i].size, assets[i].mime);
    return error(c, 404, "Not found.");
}
static enum MHD_Result handle(void *cls, struct MHD_Connection *c, const char *url,
                              const char *method, const char *version, const char *data,
                              size_t *size, void **con_cls) {
    (void)cls;
    (void)version;
    if (!*con_cls) {
        Request *r = calloc(1, sizeof *r);
        if (!r) return MHD_NO;
        r->limit = 4096;
        if (!strcmp(url,"/api/v1/receive/chunk") && authorized(c) && large_requests < 2) {
            r->large = true; large_requests++; r->limit = ORBIT_RECEIVE_CHUNK;
        }
        r->body = malloc(r->limit + 1);
        if (!r->body) { if(r->large)large_requests--; free(r); return MHD_NO; }
        r->body[0] = 0; *con_cls = r; return MHD_YES;
    }
    Request *r = *con_cls;
    if (*size) {
        if (*size > r->limit - r->length)
            r->too_large = true;
        else {
            memcpy(r->body + r->length, data, *size);
            r->length += *size;
            r->body[r->length] = 0;
        }
        *size = 0;
        return MHD_YES;
    }
    if (r->too_large)
        return error(c, 413, "Request body is too large.");
    return route(c, url, method, r);
}
static void completed(void *cls, struct MHD_Connection *c, void **con_cls,
                      enum MHD_RequestTerminationCode reason) {
    (void)cls;
    (void)c;
    (void)reason;
    Request *r = *con_cls;
    if (r) { if(r->body){OPENSSL_cleanse(r->body,r->length);free(r->body);} if(r->large)large_requests--; }
    free(r);
    *con_cls = NULL;
}
/* One owner drives and rebuilds the HTTP daemon. An internal MHD thread can
 * survive a lost listener without reopening it; stopping that thread also
 * depends on its old wake socket. External polling gives us a bounded wait
 * and lets the same owner tear down stale connections before rebinding. */
static struct MHD_Daemon *listen_start(void) {
    struct sockaddr_in bind_address = {0};
    bind_address.sin_family = AF_INET;
    bind_address.sin_port = htons((uint16_t)orbit.port);
    bind_address.sin_addr.s_addr =
        htonl(orbit.desktop && !orbit.listen_all ? INADDR_LOOPBACK : INADDR_ANY);
    return MHD_start_daemon(
        MHD_NO_FLAG, (uint16_t)orbit.port, NULL, NULL, handle, NULL,
        MHD_OPTION_SOCK_ADDR, &bind_address, MHD_OPTION_CONNECTION_LIMIT, 32U,
        MHD_OPTION_CONNECTION_TIMEOUT, 15U, MHD_OPTION_CONNECTION_MEMORY_LIMIT, (size_t)32768,
        MHD_OPTION_NOTIFY_COMPLETED, completed, NULL, MHD_OPTION_END);
}
static bool listener_address(int fd) {
    struct sockaddr_in address = {0};
    socklen_t size = sizeof address;
    return getsockname(fd, (struct sockaddr *)&address, &size) == 0 &&
           address.sin_family == AF_INET && address.sin_port == htons((uint16_t)orbit.port) &&
           address.sin_addr.s_addr ==
               htonl(orbit.desktop && !orbit.listen_all ? INADDR_LOOPBACK : INADDR_ANY);
}
static bool listener_valid(void) {
    const union MHD_DaemonInfo *info =
        MHD_get_daemon_info(daemon_handle, MHD_DAEMON_INFO_LISTEN_FD);
    if (!info || info->listen_fd == MHD_INVALID_SOCKET)
        return false;
    int listening = 0;
    socklen_t size = sizeof listening;
    return getsockopt(info->listen_fd, SOL_SOCKET, SO_ACCEPTCONN, &listening, &size) == 0 &&
           listening && listener_address(info->listen_fd);
}
/* A stale socket may still report SO_ACCEPTCONN after the network stack resets.
 * Test the kernel's loopback handshake, not an HTTP response: a busy route or
 * slow disk must not be mistaken for listener loss. Resource exhaustion and a
 * full backlog are inconclusive; only a refused connection counts as dead. */
static bool listener_refused(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        return false;
    }
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)orbit.port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int result = connect(fd, (const struct sockaddr *)&address, sizeof address);
    int error = result == 0 ? 0 : errno;
    if (error == EINPROGRESS || error == EWOULDBLOCK) {
        struct pollfd event = {.fd = fd, .events = POLLOUT};
        socklen_t size = sizeof error;
        error = 0;
        if (poll(&event, 1, 50) > 0 &&
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) != 0)
            error = 0;
    }
    close(fd);
    return error == ECONNREFUSED;
}
static void listen_stop(void) {
    if (!daemon_handle) return;
    /* Detach the listener first: MHD aborts if it closes an already-invalid
     * listen FD. Never close a descriptor that now refers to another file. */
    int fd = MHD_quiesce_daemon(daemon_handle);
    MHD_stop_daemon(daemon_handle);
    daemon_handle = NULL;
    if (fd != MHD_INVALID_SOCKET && listener_address(fd)) close(fd);
}
static void retry_wait(unsigned milliseconds) {
    while (milliseconds && !atomic_load(&api_stopping)) {
        unsigned step = milliseconds < 100 ? milliseconds : 100;
        struct timespec delay = {.tv_nsec = (long)step * 1000000};
        nanosleep(&delay, NULL);
        milliseconds -= step;
    }
}
static void *api_worker(void *unused) {
    (void)unused;
    time_t next_check = 0;
    unsigned refused = 0, retry_ms = 500;
    bool recovering = false;
    while (!atomic_load(&api_stopping)) {
        if (!daemon_handle) {
            retry_wait(retry_ms);
            if (atomic_load(&api_stopping)) break;
            daemon_handle = listen_start();
            if (!daemon_handle) {
                retry_ms = retry_ms < 8000 ? retry_ms * 2 : 8000;
                continue;
            }
            retry_ms = 500;
            refused = 0;
            next_check = 0;
        }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        bool healthy = true;
        if (now.tv_sec >= next_check) {
            next_check = now.tv_sec + 2;
            healthy = listener_valid();
            if (healthy) {
                refused = listener_refused() ? refused + 1 : 0;
                healthy = refused < 3;
            }
        }
        if (healthy && MHD_run_wait(daemon_handle, 250) == MHD_YES) {
            if (recovering) {
                diagnostics_stage("listener-recovery", "ok", 0);
                recovering = false;
            }
            continue;
        }
        if (!recovering) diagnostics_stage("listener-recovery", "started", 0);
        recovering = true;
        listen_stop();
    }
    listen_stop();
    return NULL;
}
int api_start(void) {
    if (api_thread_started) return -1;
    daemon_handle = listen_start();
    if (!daemon_handle) return -1;
    atomic_store(&api_stopping, false);
    pthread_attr_t attr;
    if (pthread_attr_init(&attr)) {
        listen_stop();
        return -1;
    }
    int result = pthread_attr_setstacksize(&attr, ORBIT_THREAD_STACK);
    if (!result) result = pthread_create(&api_thread, &attr, api_worker, NULL);
    pthread_attr_destroy(&attr);
    if (result) {
        listen_stop();
        return -1;
    }
    api_thread_started = true;
    return 0;
}
void api_stop(void) {
    if (!api_thread_started) return;
    atomic_store(&api_stopping, true);
    pthread_join(api_thread, NULL);
    api_thread_started = false;
}
