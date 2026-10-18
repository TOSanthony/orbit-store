/* Host test for backend/appdb.c. Databases come from tests/appdb_make.py:
 *   python3 tests/appdb_make.py DIR && cc -std=c11 -Ibackend backend/appdb.c tests/appdb_test.c -o t && ./t DIR */
#define _POSIX_C_SOURCE 200809L
#include "appdb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static appdb_game *find(appdb_game *list, size_t n, const char *id) {
    for (size_t i = 0; i < n; i++) if (!strcmp(list[i].title_id, id)) return &list[i];
    return NULL;
}

static void check_game_set(const char *dir, const char *name, int rows) {
    char path[512], error[128] = "";
    snprintf(path, sizeof path, "%s/%s", dir, name);
    size_t cap = (size_t)rows + 8, count = 0;
    appdb_game *list = calloc(cap, sizeof *list);
    int rc = appdb_read(path, list, cap, &count, error, sizeof error);
    if (rc) printf("%s: error: %s\n", name, error);
    CHECK(rc == 0);
    CHECK(count == (size_t)rows + 1);                       /* every game plus the PS4 row; 'junk' skipped */
    int bad = 0;
    for (int i = 1; i <= rows; i++) {
        char id[16], version[16], uri[200];
        snprintf(id, sizeof id, "PPSA%05d", i);
        snprintf(version, sizeof version, "06.%03d.000", i % 1000);
        snprintf(uri, sizeof uri, "https://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA%05d_00/%08d-aaaa-bbbb-cccc-0123456789ab-version.xml", i, i);
        appdb_game *g = find(list, count, id);
        if (!g || strcmp(g->content_version, version) || strcmp(g->version_file_uri, uri) || strncmp(g->title_name, "Game ", 5)) bad++;
    }
    CHECK(bad == 0);
    appdb_game *ps4 = find(list, count, "CUSA16194");
    CHECK(ps4 && !strcmp(ps4->content_version, "01.05") && ps4->version_file_uri[0] == 0);
    CHECK(find(list, count, "junk") == NULL);
    free(list);
}

static void check_error(const char *dir, const char *name) {
    char path[512], error[128] = "";
    snprintf(path, sizeof path, "%s/%s", dir, name);
    appdb_game list[4];
    size_t count = 99;
    CHECK(appdb_read(path, list, 4, &count, error, sizeof error) != 0);
    CHECK(count == 0 && error[0] != 0);
    printf("  %s -> \"%s\"\n", name, error);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "/tmp/appdb-test";
    check_game_set(dir, "small.db", 5);
    check_game_set(dir, "many.db", 3000);
    check_game_set(dir, "overflow.db", 40);
    check_game_set(dir, "tiny_pages.db", 300);
    check_game_set(dir, "big_pages.db", 400);
    check_error(dir, "utf16.db");
    check_error(dir, "garbage.db");
    check_error(dir, "empty.db");
    check_error(dir, "other.db");
    check_error(dir, "missing.db");

    /* capacity limit */
    char path[512], error[64];
    snprintf(path, sizeof path, "%s/many.db", dir);
    appdb_game few[10];
    size_t count = 0;
    CHECK(appdb_read(path, few, 10, &count, error, sizeof error) == 0 && count == 10);

    /* json strings */
    char out[64];
    const char *j = "{\"A\":\"x\\/y\\\\z\\u0041  \",\"B\" : \"2\", \"LONG\":\"0123456789\"}";
    CHECK(appdb_json_string(j, strlen(j), "A", out, sizeof out) && !strcmp(out, "x/y\\zA"));
    CHECK(appdb_json_string(j, strlen(j), "B", out, sizeof out) && !strcmp(out, "2"));
    CHECK(!appdb_json_string(j, strlen(j), "C", out, sizeof out));
    CHECK(!appdb_json_string(j, strlen(j), "LONG", out, 5));              /* too long for the buffer */
    CHECK(!appdb_json_string("{\"A\":\"unterminated", 18, "A", out, sizeof out));
    CHECK(!appdb_json_string("{\"A\":5}", 7, "A", out, sizeof out));
    CHECK(!appdb_json_string("{\"A\":\"\\u00e9\"}", 14, "A", out, sizeof out));
    printf(failures ? "%d FAILED\n" : "all appdb tests passed\n", failures);
    return failures != 0;
}
