/* Messages between the Orbit service and its package-install helper.
 * Both sides run on the same console and talk over a loopback socket, so the
 * structures are plain fixed-size records. The helper alone holds the pointer-bearing
 * system structures; they never cross this connection. */
#ifndef ORBIT_INSTALL_PROTO_H
#define ORBIT_INSTALL_PROTO_H

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define ORBIT_INST_MAGIC 0x4f524254u /* "ORBT" */
#define ORBIT_INST_VERSION 1u
#define ORBIT_INST_PORT 18851
#define ORBIT_INST_TOKEN_FILE "/data/orbit-store/install-helper.token"
#define ORBIT_INST_LOG_FILE "/data/orbit-store/install-helper.log"
#define ORBIT_INST_TOKEN_LEN 32

enum { ORBIT_INST_HELLO = 1, ORBIT_INST_INSTALL = 2, ORBIT_INST_STATUS = 3, ORBIT_INST_CLOSE = 4 };

/* Local failures, distinct from the system's own negative result codes. */
#define ORBIT_INST_E_UNAVAILABLE (-2001)
#define ORBIT_INST_E_DISCONNECTED (-2002)
#define ORBIT_INST_E_CANCELED (-2003)
#define ORBIT_INST_E_TIMEOUT (-2004)

typedef struct {
    uint32_t magic, version, op;
    char path[1024]; /* absolute path of the .pkg on the console */
    char name[256];  /* title shown while installing */
    char content_id[48]; /* STATUS: the content to ask about */
} orbit_inst_request;

typedef struct {
    uint32_t magic, version, op;
    int32_t result;      /* the system call's result; 0 is success */
    int32_t native_errno;
    int32_t pid;         /* HELLO: the helper's process id */
    char token[ORBIT_INST_TOKEN_LEN + 1]; /* HELLO: proves the helper was started by the service */
    char content_id[48]; /* INSTALL: what the system is installing */
    char state[16];      /* STATUS: the system's status word, unmodified */
    uint64_t downloaded, total;
    uint32_t progress, remain_seconds;
    int32_t error_code;
    char description[256];
} orbit_inst_response;

static inline int64_t orbit_inst_now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* Moves exactly `size` bytes. Returns 0, or one of the local failures above.
 * `canceled`, when given, is asked every 100 ms and may end the wait. */
static inline int orbit_inst_xfer(int fd, void *buffer, size_t size, int sending, int timeout_ms,
                                  int (*canceled)(void)) {
    char *bytes = buffer;
    size_t done = 0;
    int64_t deadline = orbit_inst_now_ms() + timeout_ms;
    while (done < size) {
        if (canceled && canceled()) return ORBIT_INST_E_CANCELED;
        int64_t left = deadline - orbit_inst_now_ms();
        if (left <= 0) return ORBIT_INST_E_TIMEOUT;
        struct pollfd p = {.fd = fd, .events = sending ? POLLOUT : POLLIN};
        int r = poll(&p, 1, left > 100 ? 100 : (int)left);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) return ORBIT_INST_E_DISCONNECTED;
        if (r == 0) continue;
        ssize_t n = sending ? send(fd, bytes + done, size - done, MSG_NOSIGNAL)
                            : recv(fd, bytes + done, size - done, 0);
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (n <= 0) return ORBIT_INST_E_DISCONNECTED;
        done += (size_t)n;
    }
    return 0;
}

#endif
