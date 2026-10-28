#include "browser_match.h"
#include "browser_scan.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
_Static_assert(sizeof(((BrowserMatch *)0)->url) <= PROBE_MATCH_MAX, "Scanner overlap must cover encoded filenames");
static bool boundary(unsigned char c) {
    return c == 0 || c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '"' || c == '\'' || c == '<' || c == '>';
}
static bool current_akira_route(char *url) {
    /* A ready link may only exist in the page's HTML until it is clicked.
     * Decode query separators once, then apply the same filename, host and
     * response checks as a navigation URL. Never decode the path or % escapes. */
    char *query = strchr(url, '?');
    if (!query) return true;
    char *out = query + 1;
    for (const char *in = out; *in; in++) {
        if (!strncmp(in, "&amp;", 5)) { *out++ = '&'; in += 4; }
        else *out++ = *in;
    }
    *out = 0;
    /* Do not recursively decode doubly escaped or ambiguous addresses. */
    if (strstr(query, "&amp;")) return false;
    for (const char *p = query + 1; p && *p;) {
        if (!strncmp(p, "expiration=", 11)) {
            char *end = NULL;
            errno = 0;
            long long expiration = strtoll(p + 11, &end, 10);
            if (errno || end == p + 11 || (*end && *end != '&') || expiration <= (long long)time(NULL))
                return false;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    return true;
}
static bool next_url_char(const unsigned char *bytes, size_t length, size_t step,
                          size_t *position, bool json_slashes, unsigned char *out) {
    if (*position > length || length - *position < step) return false;
    unsigned char c = bytes[*position];
    if (step == 2 && bytes[*position + 1]) return false;
    *position += step;
    if (json_slashes && c == '\\') {
        /* A provider's verified link can still be JSON in the network process,
         * before a click creates a navigation URL. Decode only JSON's optional
         * slash escape, once. Other escapes are not download addresses. */
        if (length - *position < step || bytes[*position] != '/' ||
            (step == 2 && bytes[*position + 1])) return false;
        *position += step;
        c = '/';
    }
    *out = c;
    return true;
}
bool browser_match(void *context, const unsigned char *bytes, size_t length) {
    BrowserMatch *match = context;
    bool viking = !strcmp(match->release->source, "vikingfile");
    const char *prefix = viking
        ? "https://vikingfile.com/d/" : "https://";
    const size_t plen = strlen(prefix);
    for (size_t step = 1; step <= 2; step++) {
        for (size_t i = 0; (plen + 1) * step <= length && i <= length - (plen + 1) * step; i++) {
            if (bytes[i] != 'h' || (i >= step && !boundary(bytes[i - step]))) continue;
            bool valid = true;
            size_t position = i;
            for (size_t j = 0; j < plen; j++) {
                unsigned char c;
                if (!next_url_char(bytes, length, step, &position, true, &c) ||
                    c != (unsigned char)prefix[j]) { valid = false; break; }
            }
            if (!valid) continue;
            char candidate[sizeof match->url];
            memcpy(candidate, prefix, plen);
            for (size_t j = plen; j < sizeof candidate; j++) {
                unsigned char c;
                if (!next_url_char(bytes, length, step, &position, true, &c)) break;
                if (boundary(c)) {
                    candidate[j] = 0;
                    char resolved[sizeof candidate];
                    memcpy(resolved, candidate, j + 1);
                    bool current = strcmp(match->release->source, "akirabox") || current_akira_route(resolved);
                    bool rejected = false;
                    for (size_t k = 0; k < match->rejected_count; k++)
                        if (!strcmp(match->rejected[k], resolved) &&
                            (!match->rejected_until || match->now_ms < match->rejected_until[k])) rejected = true;
                    if (current && !rejected && provider_capture_url(match->release, resolved)) {
                        memcpy(match->url, resolved, strlen(resolved) + 1);
                        return true;
                    }
                    if (c != '\'') break; /* Apostrophe may be inside an unescaped path. */
                }
                if (c < 33 || c > 126) break;
                candidate[j] = (char)c;
            }
        }
    }
    return false;
}
