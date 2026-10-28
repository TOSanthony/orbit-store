#ifndef ORBIT_BROWSER_SCAN_H
#define ORBIT_BROWSER_SCAN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bounded read-only scanner. Matchers must return only the selected file. */
#define PROBE_CHUNK 65536U
/* Cumulative reads, not resident memory. Cover the entire verification window
 * at the bounded 8 MiB/s scan rate, including a late human verification. */
#define PROBE_READ_DELAY_NS 8000000L
#define PROBE_SESSION_SECONDS 120
#define PROBE_SESSION_BUDGET (1024ULL * 1024ULL * 1024ULL)
#define PROBE_MARKER_MAX 256U
#define PROBE_MATCH_MAX 2048U /* Bounded generated route, including NUL. */
typedef struct {
    void *context;
    int (*read)(void *, uint64_t, void *, size_t);
    bool (*active)(void *); /* Checks deadline, cancellation and process identity. */
    bool (*match)(void *, const unsigned char *, size_t);
    void *match_context;
    uint64_t remaining, attempted, readable;
    unsigned failures;
} ProbeReader;
typedef enum {
    PROBE_ABSENT, PROBE_FOUND, PROBE_STOPPED, PROBE_BUDGET, PROBE_INVALID
} ProbeResult;
ProbeResult probe_scan_region(ProbeReader *, uint64_t start, uint64_t end,
                              const char *marker);
#endif
