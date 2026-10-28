#include "viking_session.h"
#include "sqlite3.h"
#include <openssl/crypto.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static bool plain(const unsigned char *s, int bytes, bool name) {
    if (!s || bytes < 1 || bytes > (name ? 128 : 4096) || (int)strlen((const char *)s) != bytes) return false;
    for (int i = 0; i < bytes; ++i)
        if (s[i] < 0x21 || s[i] > 0x7e || strchr(";\"\\,", s[i]) || (name && strchr("()<>@:/[]?={}", s[i]))) return false;
    return true;
}
static int budget(void *p) { return --*(int *)p <= 0; }
bool viking_read_cookies(CURL *curl, const char *path, time_t now) {
    struct stat st;
    if (!curl || lstat(path, &st) || !S_ISREG(st.st_mode) || st.st_size < 512 || st.st_size > 8 * 1024 * 1024) return false;
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    bool ok = false;
    unsigned count = 0;
    size_t bytes = 0;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOFOLLOW | SQLITE_OPEN_PRIVATECACHE, NULL) != SQLITE_OK) goto done;
    sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, NULL);
    sqlite3_db_config(db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, NULL);
    sqlite3_limit(db, SQLITE_LIMIT_LENGTH, 32768);
    sqlite3_busy_timeout(db, 150);
    int steps = 2000;
    sqlite3_progress_handler(db, 100, budget, &steps);
    /* Exact domains only. Never select other sites' cookie values or enumerate
     * other users. NKCookie's schema was inspected on the owner's PS5. WAL is
     * read through SQLite; immutable=1 would silently miss recent sign-ins. */
    const char *sql = "SELECT name,value,domain,path,expires,session,secure FROM Cookie "
        "WHERE domain IN ('vikingfile.com','.vikingfile.com','www.vikingfile.com','.www.vikingfile.com') "
        "AND (session=1 OR expires>?) LIMIT 33";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) goto done;
    sqlite3_bind_int64(stmt, 1, now);
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 0), *value = sqlite3_column_text(stmt, 1);
        const char *domain = (const char *)sqlite3_column_text(stmt, 2), *cookie_path = (const char *)sqlite3_column_text(stmt, 3);
        sqlite3_int64 expires = sqlite3_column_int64(stmt, 4);
        const int session = sqlite3_column_int(stmt, 5), secure = sqlite3_column_int(stmt, 6);
        if (!plain(name, sqlite3_column_bytes(stmt, 0), true) || !plain(value, sqlite3_column_bytes(stmt, 1), false) ||
            !cookie_path || cookie_path[0] != '/' || !plain((const unsigned char *)cookie_path, sqlite3_column_bytes(stmt, 3), false) ||
            (session != 0 && session != 1) || (secure != 0 && secure != 1)) continue;
        if (!session && (expires <= now || expires > (sqlite3_int64)now + 20LL * 366 * 86400)) continue;
        if (++count > 32) goto done;
        char line[8704];
        int n = snprintf(line, sizeof line, "%s\t%s\t%s\t%s\t%lld\t%s\t%s", domain,
            domain[0] == '.' ? "TRUE" : "FALSE", cookie_path, secure ? "TRUE" : "FALSE",
            (long long)(session ? 0 : expires), name, value);
        bool added = n > 0 && n < (int)sizeof line && (bytes += (size_t)n) <= 32768 &&
            curl_easy_setopt(curl, CURLOPT_COOKIELIST, line) == CURLE_OK;
        OPENSSL_cleanse(line, sizeof line);
        if (!added) goto done;
    }
    ok = rc == SQLITE_DONE && count > 0;
done:
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    if (!ok && curl) curl_easy_setopt(curl, CURLOPT_COOKIELIST, "ALL");
    return ok;
}
