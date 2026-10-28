#ifndef ORBIT_ARCHIVE_ACCOUNT_H
#define ORBIT_ARCHIVE_ACCOUNT_H
#include "orbit.h"
#include <curl/curl.h>
/* Archive.org serves some files to signed-in accounts only. The email and
 * password go once to Archive.org's login service and are wiped; only its two
 * session cookies are kept, on the console. */
void archive_account_load(void);
void archive_account_clear(void);
cJSON *archive_account_status(void);
/* {action:"sign-in", email, password} or {action:"sign-out"}. Signing in waits
 * for Archive.org on the calling thread; never call it under orbit.mutex. */
int archive_account_action(const cJSON *input, char *error, size_t cap);
/* Adds the session to a download handle, for archive.org hosts over HTTPS only. */
bool archive_account_apply(CURL *curl);
/* After Archive.org refused `url` (401 or 403): why, as one sentence. */
void archive_account_refusal(const char *url, char *error, size_t cap);
#endif
