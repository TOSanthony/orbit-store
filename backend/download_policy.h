#ifndef ORBIT_DOWNLOAD_POLICY_H
#define ORBIT_DOWNLOAD_POLICY_H
#include "cJSON.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct { unsigned retries, delay, timeout, min_speed; } DownloadPolicy;
#define DOWNLOAD_POLICY_DEFAULT {3, 10, 60, 1}
/* Values are retries, seconds, seconds and KiB/s for the entire game. */
void download_policy_json(cJSON *object, const DownloadPolicy *policy);
bool download_policy_patch(const cJSON *object, DownloadPolicy *policy);
void download_policy_load(const cJSON *object, DownloadPolicy *policy);
typedef struct { double since; int64_t bytes; } DownloadHealth;
static inline bool download_stalled(DownloadHealth *health, double now, int64_t bytes,
                                   const DownloadPolicy *policy) {
    double elapsed = now - health->since;
    if (elapsed < policy->timeout) return false;
    bool stalled = (double)(bytes - health->bytes) / elapsed < policy->min_speed * 1024.0;
    *health = (DownloadHealth){now, bytes};
    return stalled;
}
static inline unsigned download_connections(unsigned count, unsigned attempts) {
    /* Keep the on-disk partition layout; reduce only simultaneous requests. */
    while (attempts-- && count > 1) count /= 2;
    return count ? count : 1;
}
#endif
