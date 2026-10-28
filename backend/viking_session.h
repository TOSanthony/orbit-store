#ifndef ORBIT_VIKING_SESSION_H
#define ORBIT_VIKING_SESSION_H
#include "orbit.h"
#include <curl/curl.h>
/* No cookie values or account hashes are returned through the API or saved in
 * Orbit. Only a foreground user's explicitly selected browser store is read. */
typedef enum { VIKING_UNKNOWN, VIKING_SIGNED_OUT, VIKING_SIGNED_IN } VikingState;
VikingState viking_check_session(bool native, atomic_bool *cancel);
bool viking_resolve_download(Release *release, bool native, atomic_bool *cancel);
/* Kept separate for tests with synthetic databases, never exposed by HTTP. */
bool viking_read_cookies(CURL *curl, const char *path, time_t now);
VikingState viking_page_state(const char *html);
const char *viking_cached_state(bool native);
#endif
