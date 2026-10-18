#define _DEFAULT_SOURCE 1
/* Runs the game-update check against a disposable console tree and a local fake of Sony's server.
 * usage: gameupdates_driver <system root> <console firmware, hex> ; the fake server's address is
 * in ORBIT_TEST_GAMEUPDATES_BASE. Prints the status JSON once the first check has finished. */
#include "orbit.h"
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

int main(int argc, char **argv) {
    if (argc != 3)
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
