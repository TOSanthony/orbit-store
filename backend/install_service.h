#ifndef ORBIT_INSTALL_SERVICE_H
#define ORBIT_INSTALL_SERVICE_H
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include "../helper/install_proto.h"

/* Installing a package takes system calls the service itself may not make, so each install
 * runs through a small helper program sent to the console's ELF loader. One session is one
 * helper; it serves one install and ends with the session. */
typedef struct {
    int fd;
    pid_t pid;
    char content_id[48];
} install_session;

#define INSTALL_SESSION_INIT {.fd = -1, .pid = -1, .content_id = {0}}

typedef struct {
    int result;            /* the system's result; 0 is success */
    char state[16];        /* the system's status word, unmodified */
    uint64_t downloaded, total;
    uint32_t progress, remain_seconds;
    int error_code;
    char description[256];
} install_status;

/* Installs a finished package that is not a queue job (a game update) and waits until the
 * console reports it installed. 0 on success; otherwise a non-zero code with a reason in `error`.
 * Shares the one-install-at-a-time rule with the download queue. */
typedef void (*install_progress_fn)(unsigned percent, void *context);
int install_package_blocking(const char *path, const char *name, install_progress_fn progress, void *context,
                             bool (*stop)(void), char *error, size_t cap);
/* True when the console's own drive has room to install a package of this size. */
bool install_has_space(int64_t package_bytes);

/* False off the console, or when this build carries no helper. */
bool install_service_available(void);
/* Starts a helper and waits for it to be ready. 0, or a negative ORBIT_INST_E_* / system code. */
int install_session_open(install_session *session);
/* Asks the system to install the package at `path` (absolute). */
int install_session_install(install_session *session, const char *path, const char *name);
int install_session_status(install_session *session, install_status *status);
/* Always safe to call, including after a failed open. */
void install_session_close(install_session *session);
#endif
