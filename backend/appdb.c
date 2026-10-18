#define _POSIX_C_SOURCE 200809L
#include "appdb.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define MAX_ROW_PAYLOAD (256u * 1024u)   /* a row larger than this is not an app entry: skipped */
#define MAX_PAGES_VISITED 4000000u
#define MAX_DEPTH 24
#define MAX_COLUMNS 64
#define SCHEMA_SQL_MAX 4096

typedef struct {
    int fd;
    uint32_t page_size, usable, page_count, visited;
    char error[96];
} db;

typedef enum { COL_NULL, COL_INT, COL_TEXT, COL_OTHER } column_kind;

typedef struct {
    column_kind kind;
    int64_t integer;
    const unsigned char *text;
    size_t length;
} column;

static int fail(db *d, const char *message) {
    if (!d->error[0]) snprintf(d->error, sizeof d->error, "%s", message);
    return -1;
}

static uint32_t be16(const unsigned char *p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* SQLite varint, up to 9 bytes. Returns the bytes used, or 0 when it would run past `end`. */
static size_t varint(const unsigned char *p, const unsigned char *end, uint64_t *value) {
    uint64_t v = 0;
    for (size_t i = 0; i < 9; i++) {
        if (p + i >= end) return 0;
        if (i == 8) { *value = (v << 8) | p[i]; return 9; }
        v = (v << 7) | (p[i] & 0x7f);
        if (!(p[i] & 0x80)) { *value = v; return i + 1; }
    }
    return 0;
}

static int read_page(db *d, uint32_t number, unsigned char *buffer) {
    if (number == 0 || number > d->page_count) return fail(d, "page number outside the file");
    if (++d->visited > MAX_PAGES_VISITED) return fail(d, "too many pages");
    off_t offset = (off_t)(number - 1) * (off_t)d->page_size;
    size_t done = 0;
    while (done < d->page_size) {
        ssize_t n = pread(d->fd, buffer + done, d->page_size - done, offset + (off_t)done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return fail(d, "short read");
        done += (size_t)n;
    }
    return 0;
}

/* The full payload of a table leaf cell (overflow pages followed), NUL-terminated, or NULL when the
 * row is skipped (too large) or on error (d->error set). */
static unsigned char *cell_payload(db *d, const unsigned char *cell, const unsigned char *limit, uint64_t *size_out) {
    uint64_t size, key;
    size_t a = varint(cell, limit, &size);
    if (!a) { fail(d, "cell out of page"); return NULL; }
    size_t b = varint(cell + a, limit, &key);
    if (!b) { fail(d, "cell out of page"); return NULL; }
    const unsigned char *body = cell + a + b;
    if (size > MAX_ROW_PAYLOAD) return NULL;
    uint32_t u = d->usable, x = u - 35, local = (uint32_t)size;
    bool overflow = false;
    if (size > x) {
        uint32_t m = ((u - 12) * 32 / 255) - 23;
        uint32_t k = m + (uint32_t)((size - m) % (u - 4));
        local = k <= x ? k : m;
        overflow = true;
    }
    if (body + local + (overflow ? 4 : 0) > limit) { fail(d, "cell out of page"); return NULL; }
    unsigned char *out = malloc((size_t)size + 1);
    if (!out) { fail(d, "out of memory"); return NULL; }
    memcpy(out, body, local);
    if (overflow) {
        uint32_t next = be32(body + local);
        uint64_t have = local;
        unsigned char *page = malloc(d->page_size);
        if (!page) { free(out); fail(d, "out of memory"); return NULL; }
        uint32_t hops = 0, limit_hops = (uint32_t)(MAX_ROW_PAYLOAD / (u - 4)) + 2;
        while (have < size) {
            if (!next || ++hops > limit_hops || read_page(d, next, page)) {
                free(out); free(page); fail(d, "bad overflow chain"); return NULL;
            }
            uint64_t take = size - have < u - 4 ? size - have : u - 4;
            memcpy(out + have, page + 4, (size_t)take);
            have += take;
            next = be32(page);
        }
        free(page);
    }
    out[size] = 0;
    *size_out = size;
    return out;
}

/* Splits a record into columns. Returns 0 or -1 when the record is malformed. */
static int decode(const unsigned char *payload, uint64_t size, column *cols, size_t *count) {
    static const unsigned char widths[10] = {0, 1, 2, 3, 4, 6, 8, 8, 0, 0};
    const unsigned char *end = payload + size;
    uint64_t header;
    size_t n = varint(payload, end, &header);
    if (!n || header > size || header < n) return -1;
    const unsigned char *types = payload + n, *types_end = payload + header, *data = payload + header;
    size_t c = 0;
    while (types < types_end) {
        uint64_t t;
        size_t used = varint(types, types_end, &t);
        if (!used || c >= MAX_COLUMNS || t == 10 || t == 11) return -1;
        types += used;
        column *col = &cols[c++];
        col->kind = COL_OTHER;
        col->integer = 0;
        col->text = NULL;
        col->length = 0;
        uint64_t length;
        if (t >= 12) {
            length = (t - 12) / 2;
            if (length > (uint64_t)(end - data)) return -1;
            if (t & 1) { col->kind = COL_TEXT; col->text = data; col->length = (size_t)length; }
        } else {
            length = widths[t];
            if (length > (uint64_t)(end - data)) return -1;
            if (t == 0) col->kind = COL_NULL;
            else if (t == 8 || t == 9) { col->kind = COL_INT; col->integer = t - 8; }
            else if (t >= 1 && t <= 6) {
                int64_t v = (int8_t)data[0];
                for (uint64_t i = 1; i < length; i++) v = (int64_t)(((uint64_t)v << 8) | data[i]);
                col->kind = COL_INT;
                col->integer = v;
            }
        }
        data += length;
    }
    *count = c;
    return 0;
}

typedef int (*row_fn)(db *d, const column *cols, size_t count, void *context);

/* In-order walk of a table b-tree. Returns 0, 1 when the visitor asked to stop, or -1 on error. */
static int walk(db *d, uint32_t root, int depth, row_fn visit, void *context) {
    if (depth > MAX_DEPTH) return fail(d, "b-tree too deep");
    unsigned char *page = malloc(d->page_size);
    if (!page) return fail(d, "out of memory");
    int result = 0;
    if (read_page(d, root, page)) { free(page); return -1; }
    size_t at0 = root == 1 ? 100 : 0;
    unsigned type = page[at0];
    uint32_t cells = be16(page + at0 + 3);
    size_t header_size = type == 0x05 ? 12 : 8;
    if (type != 0x0d && type != 0x05) { free(page); return fail(d, "not a table page"); }
    if (at0 + header_size + (size_t)cells * 2 > d->usable) { free(page); return fail(d, "bad cell count"); }
    for (uint32_t i = 0; i < cells && result == 0; i++) {
        uint32_t at = be16(page + at0 + header_size + (size_t)i * 2);
        if (at < at0 + header_size + (size_t)cells * 2 || at >= d->usable) { result = fail(d, "bad cell pointer"); break; }
        const unsigned char *cell = page + at, *limit = page + d->usable;
        if (type == 0x05) {
            if (cell + 4 > limit) { result = fail(d, "cell out of page"); break; }
            result = walk(d, be32(cell), depth + 1, visit, context);
        } else {
            uint64_t size = 0;
            unsigned char *payload = cell_payload(d, cell, limit, &size);
            if (!payload) { if (d->error[0]) result = -1; continue; }
            column cols[MAX_COLUMNS];
            size_t count = 0;
            if (decode(payload, size, cols, &count) == 0) result = visit(d, cols, count, context);
            free(payload);
        }
    }
    if (result == 0 && type == 0x05) result = walk(d, be32(page + at0 + 8), depth + 1, visit, context);
    free(page);
    return result;
}

/* ---- sqlite_master: the table's root page and column names ---------------------------------- */
typedef struct {
    const char *table;
    uint32_t root;
    char sql[SCHEMA_SQL_MAX];
    bool found;
} master_lookup;

static int master_row(db *d, const column *cols, size_t count, void *context) {
    (void)d;
    master_lookup *m = context;
    if (count < 5 || cols[0].kind != COL_TEXT || cols[1].kind != COL_TEXT || cols[3].kind != COL_INT || cols[4].kind != COL_TEXT) return 0;
    if (cols[0].length != 5 || memcmp(cols[0].text, "table", 5)) return 0;
    if (cols[1].length != strlen(m->table) || memcmp(cols[1].text, m->table, cols[1].length)) return 0;
    if (cols[3].integer <= 0 || cols[3].integer > 0x7fffffff || cols[4].length >= sizeof m->sql) return 0;
    m->root = (uint32_t)cols[3].integer;
    memcpy(m->sql, cols[4].text, cols[4].length);
    m->sql[cols[4].length] = 0;
    m->found = true;
    return 1;
}

/* Index of a column in a CREATE TABLE statement, or -1. Skips constraint clauses. */
static int column_index(const char *sql, const char *name) {
    const char *p = strchr(sql, '(');
    if (!p) return -1;
    p++;
    size_t want = strlen(name);
    int index = 0;
    while (*p) {
        while (isspace((unsigned char)*p)) p++;
        const char *start = p;
        char quote = (*p == '"' || *p == '`' || *p == '\'') ? *p : *p == '[' ? ']' : 0;
        if (quote) start = ++p;
        const char *stop = start;
        if (quote) { while (*stop && *stop != quote) stop++; }
        else { while (*stop && !isspace((unsigned char)*stop) && *stop != ',' && *stop != ')') stop++; }
        size_t n = (size_t)(stop - start);
        static const char *const keywords[] = {"PRIMARY", "UNIQUE", "CHECK", "FOREIGN", "CONSTRAINT"};
        bool constraint = false;
        for (size_t k = 0; k < sizeof keywords / sizeof *keywords; k++)
            if (!quote && n == strlen(keywords[k]) && !strncasecmp(start, keywords[k], n)) constraint = true;
        if (!constraint) {
            if (n == want && !strncasecmp(start, name, n)) return index;
            index++;
        }
        int depth = 0;                                   /* skip to the next top-level comma */
        for (p = stop; *p; p++) {
            if (*p == '(') depth++;
            else if (*p == ')') { if (depth == 0) return -1; depth--; }
            else if (*p == ',' && depth == 0) { p++; break; }
            else if (*p == '\'' || *p == '"') { char q = *p; for (p++; *p && *p != q; p++) {} if (!*p) return -1; }
        }
    }
    return -1;
}

/* ---- JSON strings ----------------------------------------------------------------------------- */
bool appdb_json_string(const char *json, size_t length, const char *key, char *out, size_t cap) {
    size_t k = strlen(key);
    if (!cap) return false;
    for (size_t i = 0; i + k + 3 <= length; i++) {
        if (json[i] != '"' || strncmp(json + i + 1, key, k) || json[i + 1 + k] != '"') continue;
        if (i > 0 && json[i - 1] == '\\') continue;
        size_t p = i + k + 2;
        while (p < length && isspace((unsigned char)json[p])) p++;
        if (p >= length || json[p] != ':') continue;
        p++;
        while (p < length && isspace((unsigned char)json[p])) p++;
        if (p >= length || json[p] != '"') return false;
        p++;
        size_t w = 0;
        for (; p < length && json[p] != '"'; p++) {
            char c = json[p];
            if (c == '\\') {
                if (++p >= length) return false;
                switch (json[p]) {
                case '/': c = '/'; break;
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'u': {
                    if (p + 4 >= length) return false;
                    unsigned v = 0;
                    for (int h = 1; h <= 4; h++) {
                        int d = json[p + h];
                        v = v << 4 | (unsigned)(isdigit(d) ? d - '0' : isxdigit(d) ? (tolower(d) - 'a' + 10) : 16);
                        if (!isxdigit(d)) return false;
                    }
                    if (v >= 0x80) return false;       /* the fields we read are plain ASCII */
                    c = (char)v;
                    p += 4;
                    break;
                }
                default: return false;
                }
            }
            if (w + 1 >= cap) return false;
            out[w++] = c;
        }
        if (p >= length) return false;
        while (w && (out[w - 1] == ' ' || out[w - 1] == '\t' || out[w - 1] == '\r' || out[w - 1] == '\n')) w--;
        out[w] = 0;
        return true;
    }
    return false;
}

/* ---- tbl_contentinfo -------------------------------------------------------------------------- */
typedef struct {
    int id_col, name_col, json_col;
    appdb_game *out;
    size_t cap, count;
} collect;

static bool plausible_title(const char *id) {
    if (strlen(id) != 9) return false;
    for (int i = 0; i < 4; i++) if (id[i] < 'A' || id[i] > 'Z') return false;
    for (int i = 4; i < 9; i++) if (!isdigit((unsigned char)id[i])) return false;
    return true;
}

static int game_row(db *d, const column *cols, size_t count, void *context) {
    (void)d;
    collect *c = context;
    if (c->count >= c->cap) return 1;
    if ((size_t)c->id_col >= count || (size_t)c->json_col >= count) return 0;
    const column *id = &cols[c->id_col], *json = &cols[c->json_col];
    if (id->kind != COL_TEXT || json->kind != COL_TEXT || id->length != 9) return 0;
    appdb_game g;
    memset(&g, 0, sizeof g);
    memcpy(g.title_id, id->text, 9);
    if (!plausible_title(g.title_id)) return 0;
    if (c->name_col >= 0 && (size_t)c->name_col < count && cols[c->name_col].kind == COL_TEXT) {
        size_t n = cols[c->name_col].length < sizeof g.title_name - 1 ? cols[c->name_col].length : sizeof g.title_name - 1;
        memcpy(g.title_name, cols[c->name_col].text, n);
    }
    const char *text = (const char *)json->text;
    if (!appdb_json_string(text, json->length, "CONTENT_VERSION", g.content_version, sizeof g.content_version) &&
        !appdb_json_string(text, json->length, "APP_VER", g.content_version, sizeof g.content_version))
        g.content_version[0] = 0;
    if (!appdb_json_string(text, json->length, "VERSION_FILE_URI", g.version_file_uri, sizeof g.version_file_uri))
        g.version_file_uri[0] = 0;
    c->out[c->count++] = g;
    return 0;
}

int appdb_read(const char *path, appdb_game *out, size_t cap, size_t *count, char *error, size_t error_cap) {
    db d;
    memset(&d, 0, sizeof d);
    *count = 0;
    int rc = -1;
    unsigned char header[100];
    d.fd = open(path, O_RDONLY | O_CLOEXEC);
    if (d.fd < 0) { snprintf(d.error, sizeof d.error, "cannot open the file (errno %d)", errno); goto done; }
    ssize_t n = pread(d.fd, header, sizeof header, 0);
    if (n != (ssize_t)sizeof header || memcmp(header, "SQLite format 3", 16)) { fail(&d, "not an SQLite file"); goto done; }
    d.page_size = be16(header + 16);
    if (d.page_size == 1) d.page_size = 65536;
    if (d.page_size < 512 || (d.page_size & (d.page_size - 1))) { fail(&d, "bad page size"); goto done; }
    d.usable = d.page_size - header[20];
    if (d.usable < 480) { fail(&d, "bad reserved space"); goto done; }
    if (be32(header + 56) != 1) { fail(&d, "unsupported text encoding"); goto done; }
    off_t size = lseek(d.fd, 0, SEEK_END);
    if (size < (off_t)d.page_size) { fail(&d, "file too small"); goto done; }
    d.page_count = (uint32_t)(size / d.page_size);       /* the header's own count may be stale */
    master_lookup m = {"tbl_contentinfo", 0, "", false};
    if (walk(&d, 1, 0, master_row, &m) < 0) goto done;
    if (!m.found) { fail(&d, "tbl_contentinfo not found"); goto done; }
    collect c = {column_index(m.sql, "titleId"), column_index(m.sql, "titleName"), column_index(m.sql, "AppInfoJson"), out, cap, 0};
    if (c.id_col < 0 || c.json_col < 0) { fail(&d, "unexpected table layout"); goto done; }
    if (walk(&d, m.root, 0, game_row, &c) < 0) goto done;
    *count = c.count;
    rc = 0;
done:
    if (rc && error && error_cap) snprintf(error, error_cap, "%s", d.error[0] ? d.error : "read failed");
    if (d.fd >= 0) close(d.fd);
    return rc;
}
