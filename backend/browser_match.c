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
static bool current_akira_route(const char *url) {
    /* Browser memory also contains HTML attributes and previously visited,
     * expired signed links. Only use a current, decoded navigation address. */
    const char *query = strchr(url, '?');
    if (!query) return true;
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
bool browser_match(void *context, const unsigned char *bytes, size_t length) {
    BrowserMatch *match = context;
    const char *prefix = !strcmp(match->release->source, "vikingfile")
        ? "https://vikingfile.com/d/" : "https://";
    const size_t plen = strlen(prefix);
    for (size_t step = 1; step <= 2; step++) {
        for (size_t i = 0; (plen + 1) * step <= length && i <= length - (plen + 1) * step; i++) {
            if (bytes[i] != 'h' || (i >= step && !boundary(bytes[i - step]))) continue;
            bool valid = true;
            for (size_t j = 0; j < plen; j++) {
                if (bytes[i + j * step] != (unsigned char)prefix[j] ||
                    (step == 2 && bytes[i + j * step + 1])) { valid = false; break; }
            }
            if (!valid) continue;
            char candidate[sizeof match->url];
            memcpy(candidate, prefix, plen);
            for (size_t j = plen; j < sizeof candidate && (j + 1) * step <= length - i; j++) {
                unsigned char c = bytes[i + j * step];
                if (step == 2 && bytes[i + j * step + 1]) break;
                if (boundary(c)) {
                    candidate[j] = 0;
                    bool rejected = false;
                    for (size_t k = 0; k < match->rejected_count; k++)
                        if (!strcmp(match->rejected[k], candidate)) rejected = true;
                    if (!rejected && provider_capture_url(match->release, candidate) &&
                        (strcmp(match->release->source, "akirabox") || current_akira_route(candidate))) {
                        memcpy(match->url, candidate, j + 1);
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
