#ifndef ORBIT_BROWSER_MATCH_H
#define ORBIT_BROWSER_MATCH_H
#include "orbit.h"
typedef struct {
    const Release *release;
    char url[sizeof(((Release *)0)->url)];
    const char (*rejected)[2048];
    size_t rejected_count;
} BrowserMatch;
/* Extract only a complete provider-bound route for the selected filename.
 * Never retain unrelated browser bytes, account credentials or cookies. */
bool browser_match(void *context, const unsigned char *bytes, size_t length);
#endif
