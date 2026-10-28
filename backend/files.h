#ifndef ORBIT_FILES_H
#define ORBIT_FILES_H
#include "orbit.h"
/* Files: browse and change what is on Orbit's drives (docs/api.md#files). */
void files_stop(void);
/* POST /api/v1/files/list: one folder of a drive. NULL with *code and error set. */
cJSON *files_list(const cJSON *input, int *code, char *error, size_t cap);
/* GET /api/v1/files/status: {"operation": the running or last one, or null}. */
cJSON *files_status(void);
/* POST /api/v1/files/action: 200 done at once, 202 started, else an error code. */
int files_action(const cJSON *input, char *error, size_t cap);
/* POST /api/v1/files/link: a short-lived download URL for one file. */
int files_link(const cJSON *input, cJSON **result, char *error, size_t cap);
/* GET /api/v1/files/download/<ticket>/<name>: the file, opened and checked; the
 * caller sends and closes it. 200, or an error code with error set. */
int files_download_open(const char *ticket, int *fd, int64_t *size, char *name, size_t name_cap,
                        char *error, size_t cap);
#endif
