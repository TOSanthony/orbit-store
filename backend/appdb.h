#ifndef ORBIT_APPDB_H
#define ORBIT_APPDB_H
/* Reads the console's installed-game list (the system's app.db, an SQLite file) without SQLite.
 *
 * Only what Orbit needs: the rows of table tbl_contentinfo, and in each row's AppInfoJson the
 * installed version (CONTENT_VERSION) and the address of Sony's update list (VERSION_FILE_URI).
 * The file is opened read-only and never modified. Every length and page number read from the
 * file is checked, the walk is bounded, and a damaged or unexpected file gives an error instead
 * of a crash. Journals are ignored, so the very latest writes may be missing: callers should also
 * read /user/appmeta/<id>/param.json, which the console rewrites at once after an install. */
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char title_id[16];           /* "PPSA01552" or "CUSA16194" */
    char title_name[96];
    char content_version[16];    /* PS5: "06.005.000" (CONTENT_VERSION); PS4: APP_VER, e.g. "01.05" */
    char version_file_uri[768];  /* https://sgst.prod.dl.playstation.net/.../<uuid>-version.xml, trimmed */
} appdb_game;

/* Stores up to `cap` games and sets *count. Returns 0, or a negative error with a short English
 * reason in `error`. Rows without a plausible title id are skipped. */
int appdb_read(const char *path, appdb_game *out, size_t cap, size_t *count, char *error, size_t error_cap);

/* The value of a string key in a flat JSON object (as found in AppInfoJson): copied, unescaped
 * (\/ \" \\ \uXXXX in the basic range), trailing spaces removed. False when absent or too long. */
bool appdb_json_string(const char *json, size_t length, const char *key, char *out, size_t cap);

/* The console's installed add-on content (DLC): the system's addcont.db, read the same way. The layout of that
 * file is not documented, so no column name is assumed: every row is scanned for a title id (a column holding
 * one, or a content id such as EP0001-CUSA00093_00-LIMITEDPACK00000) and for the short names that go with it
 * (the folder / label of the content). `schema` receives a one-line summary of the tables and their columns, to
 * show what the file really holds. Returns 0, or a negative error with a short English reason. */
typedef struct {
    char title_id[16];
    char label[32];
} appdb_content;
int appdb_addcont_read(const char *path, appdb_content *out, size_t cap, size_t *count, char *schema,
                       size_t schema_cap, char *error, size_t error_cap);
#endif
