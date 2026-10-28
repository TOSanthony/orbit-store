#ifndef ORBIT_BROWSER_MATCH_H
#define ORBIT_BROWSER_MATCH_H
#include "orbit.h"
typedef struct {
    const Release *release;
    char url[sizeof(((Release *)0)->url)];
    const char (*rejected)[2048];
    size_t rejected_count;
    const int64_t *rejected_until; /* Monotonic milliseconds; NULL rejects permanently. */
    int64_t now_ms;
} BrowserMatch;
/* Extract only a complete provider-bound route for the selected filename.
 * Never retain unrelated browser bytes, account credentials or cookies. */
bool browser_match(void *context, const unsigned char *bytes, size_t length);
#endif
