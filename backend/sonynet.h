#ifndef ORBIT_SONYNET_H
#define ORBIT_SONYNET_H
/* Downloads from Sony's own servers, for game updates.
 *
 * Only the hosts patchfeed_url_allowed accepts are contacted, redirects are never followed, and
 * the certificate is always verified: Orbit's CA list plus Sony's own root (Sony's download
 * servers are signed by it, and public CA lists do not carry it). `stop` is polled while a
 * transfer runs; returning true aborts it. `why` receives a short English reason on failure. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *data; /* malloc'd, NUL terminated; the caller frees it */
    size_t size;
} SonyBody;

typedef bool (*sony_stop_fn)(void);

/* A small document (version.xml, a manifest). Fails when it is larger than `max`. */
bool sony_get(const char *url, size_t max, SonyBody *body, sony_stop_fn stop, char *why, size_t why_cap);

typedef enum {
    SONY_RANGE_OK = 0,
    SONY_RANGE_FAILED = -1,   /* network or HTTP error; `received` says how far it got */
    SONY_RANGE_ABORTED = -2,  /* stop() returned true */
    SONY_RANGE_IGNORED = -3,  /* the server ignored the Range header; begin the piece again */
    SONY_RANGE_DISK = -4,     /* a write failed (full disk, unplugged drive) */
    SONY_RANGE_REFUSED = -5   /* HTTP 400/401/403/404/410: the link is gone */
} sony_range_result;

/* Downloads bytes [start, end) of `url` and writes them at `file_offset + position` of `fd`.
 * *received is the position reached inside the piece (it starts at `start`), updated as data arrives. */
sony_range_result sony_range(const char *url, int64_t start, int64_t end, int fd, int64_t file_offset,
                             volatile int64_t *received, sony_stop_fn stop, char *why, size_t why_cap);
/* The same, from a file on the person's own NAS (its address must be on the NAS origin saved in Sources,
 * over HTTPS; private addresses are refused). Used for updates listed in the NAS index. */
sony_range_result nas_range(const char *url, int64_t start, int64_t end, int fd, int64_t file_offset,
                            volatile int64_t *received, sony_stop_fn stop, char *why, size_t why_cap);
#endif
