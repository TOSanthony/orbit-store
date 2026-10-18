#define _DEFAULT_SOURCE 1
/* Runs the game-update check against a disposable console tree and a local fake of Sony's server.
 * usage: gameupdates_driver <system root> <console firmware, hex> ; the fake server's address is
 * in ORBIT_TEST_GAMEUPDATES_BASE. Prints the status JSON once the first check has finished. */
#include "install_service.h"
#include "orbit.h"
#include <curl/curl.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

Orbit orbit;
const char orbit_ca[] = "";

void copy_text(char *to, size_t cap, const char *s) {
    snprintf(to, cap, "%s", s ? s : "");
}
const char *json_text(const cJSON *o, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}
int read_regular_file(const char *path, size_t max, unsigned char **data, size_t *length) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    unsigned char *buffer = malloc(max + 1);
    size_t n = buffer ? fread(buffer, 1, max, f) : 0;
    fclose(f);
    if (!buffer)
        return -1;
    buffer[n] = 0;
    *data = buffer;
    *length = n;
    return 0;
}

/* Stand-ins for the console pieces the downloader uses. The "drive" is a folder from the environment. */
size_t storage_list(Storage *out) {
    const char *root = getenv("ORBIT_TEST_STORAGE");
    struct stat st;
    if (!root || stat(root, &st))
        return 0;
    memset(out, 0, sizeof *out);
    snprintf(out->id, sizeof out->id, "desktop");
    snprintf(out->root, sizeof out->root, "%s", root);
    out->free_bytes = 1ULL << 40;
    out->device = st.st_dev;
    out->inode = st.st_ino;
    return 1;
}
int storage_open(const Job *j) {
    char path[600];
    snprintf(path, sizeof path, "%s/homebrew", j->root);
    mkdir(path, 0755);
    return open(path, O_RDONLY | O_DIRECTORY);
}
/* The patches saved on the person's NAS page: a file named by the test, or none. */
int nas_game_patches(char **data, size_t *size) {
    *data = NULL;
    *size = 0;
    const char *path = getenv("ORBIT_TEST_GAME_PATCHES");
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return 0;
    char *buffer = calloc(1, 65536);
    *size = fread(buffer, 1, 65535, f);
    fclose(f);
    *data = buffer;
    return 1;
}

/* An update file listed in the NAS index, described by the test: "PPSA00004:03.000.000:2048:<sha256>". */
bool nas_best_update(const char *title_id, const char *newer_than, const char *same_ok, char *id, size_t id_cap,
                     char *version, size_t version_cap, int64_t *size) {
    const char *spec = getenv("ORBIT_TEST_NAS_UPDATE");
    char game[16] = "", ver[16] = "", hash[80] = "";
    long long bytes = 0;
    if (!spec || sscanf(spec, "%15[^:]:%15[^:]:%lld:%79s", game, ver, &bytes, hash) < 3 || strcmp(game, title_id))
        return false;
    bool same = *same_ok && !strcmp(ver, same_ok) && strlen(hash) == 64;
    if (*newer_than && strcmp(ver, newer_than) <= 0 && !same)
        return false;
    snprintf(id, id_cap, "nas-test");
    snprintf(version, version_cap, "%s", ver);
    *size = bytes;
    return true;
}
bool nas_release_copy(const char *id, Release *out) {
    const char *spec = getenv("ORBIT_TEST_NAS_UPDATE");
    char game[16] = "", ver[16] = "", hash[80] = "";
    long long bytes = 0;
    if (strcmp(id, "nas-test") || !spec ||
        sscanf(spec, "%15[^:]:%15[^:]:%lld:%79s", game, ver, &bytes, hash) < 3)
        return false;
    memset(out, 0, sizeof *out);
    snprintf(out->id, sizeof out->id, "nas-test");
    snprintf(out->url, sizeof out->url, "https://nas.example/updates/nas-update.pkg");
    snprintf(out->sha256, sizeof out->sha256, "%s", hash);
    out->size = bytes;
    return true;
}
bool nas_url_allowed(const char *url) {
    (void)url;
    return true;
}
curl_socket_t provider_open_socket(void *context, curlsocktype purpose, struct curl_sockaddr *address) {
    (void)context; (void)purpose; (void)address;
    return CURL_SOCKET_BAD;
}

bool orbit_is_stopping(void) {
    return false;
}
bool install_service_available(void) {
    return false;
}
bool install_has_space(int64_t bytes) {
    (void)bytes;
    return true;
}
int install_package_blocking(const char *path, const char *name, install_progress_fn progress, void *context,
                             bool (*stop)(void), char *error, size_t cap) {
    (void)path, (void)name, (void)progress, (void)context, (void)stop, (void)error, (void)cap;
    return -1;
}

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4)
        return 2;
    copy_text(orbit.system_root, sizeof orbit.system_root, argv[1]);
    orbit.firmware = (uint32_t)strtoul(argv[2], NULL, 16);
    orbit.desktop = true;
    if (gameupdates_start())
        return 3;
    for (int i = 0; i < 200; i++) {
        cJSON *status = gameupdates_status();
        cJSON *checked = cJSON_GetObjectItemCaseSensitive(status, "checked");
        const cJSON *error = cJSON_GetObjectItemCaseSensitive(status, "error");
        bool done = (cJSON_IsNumber(checked) && checked->valuedouble > 0) ||
                    (cJSON_IsString(error) && *error->valuestring);
        if (done && argc == 4) {
            char message[256] = "";
            int code = patchdl_start(argv[3], message, sizeof message);
            if (code != 202) {
                printf("{\"startRefused\":%d,\"message\":\"%s\"}\n", code, message);
                cJSON_Delete(status);
                gameupdates_stop();
                return 0;
            }
            for (int wait = 0; wait < 600; wait++) {
                cJSON *now = gameupdates_status();
                const cJSON *job = cJSON_GetObjectItemCaseSensitive(now, "job");
                const cJSON *active = cJSON_GetObjectItemCaseSensitive(job, "active");
                if (cJSON_IsFalse(active)) {
                    char *text = cJSON_Print(job);
                    puts(text);
                    free(text);
                    cJSON_Delete(now);
                    break;
                }
                cJSON_Delete(now);
                usleep(100000);
            }
            cJSON_Delete(status);
            patchdl_stop();
            gameupdates_stop();
            return 0;
        }
        if (done) {
            char *text = cJSON_Print(status);
            puts(text);
            free(text);
            cJSON_Delete(status);
            gameupdates_stop();
            return 0;
        }
        cJSON_Delete(status);
        usleep(50000);
    }
    gameupdates_stop();
    return 4;
}
