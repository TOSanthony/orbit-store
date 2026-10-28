#include "download_policy.h"
#include <stddef.h>
#include <math.h>
#include <string.h>
typedef struct { const char *key; size_t offset; unsigned min, max; } Field;
static const Field fields[] = {
    {"downloadRetries", offsetof(DownloadPolicy, retries), 0, 10},
    {"downloadRetryDelay", offsetof(DownloadPolicy, delay), 1, 300},
    {"downloadStallTimeout", offsetof(DownloadPolicy, timeout), 15, 600},
    {"downloadMinSpeed", offsetof(DownloadPolicy, min_speed), 1, 1024},
};
static unsigned *field(DownloadPolicy *policy, const Field *f) {
    return (unsigned *)((char *)policy + f->offset);
}
static bool valid(const cJSON *value, const Field *f) {
    return cJSON_IsNumber(value) && isfinite(value->valuedouble) &&
        value->valuedouble >= f->min && value->valuedouble <= f->max &&
        value->valuedouble == value->valueint;
}
void download_policy_json(cJSON *object, const DownloadPolicy *policy) {
    for (size_t i = 0; i < sizeof fields / sizeof *fields; i++)
        cJSON_AddNumberToObject(object, fields[i].key,
            *(const unsigned *)((const char *)policy + fields[i].offset));
}
bool download_policy_patch(const cJSON *object, DownloadPolicy *policy) {
    DownloadPolicy next = *policy;
    const cJSON *item;
    cJSON_ArrayForEach(item, object) {
        bool known = item->string && (!strcmp(item->string, "concurrentDownloads") || !strcmp(item->string, "downloadConnections"));
        for (size_t i = 0; item->string && i < sizeof fields / sizeof *fields; i++)
            if (!strcmp(item->string, fields[i].key)) known = true;
        if (!known) return false;
    }
    for (size_t i = 0; i < sizeof fields / sizeof *fields; i++) {
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, fields[i].key);
        if (!value) continue;
        if (!valid(value, &fields[i])) return false;
        *field(&next, &fields[i]) = (unsigned)value->valueint;
    }
    *policy = next;
    return true;
}
void download_policy_load(const cJSON *object, DownloadPolicy *policy) {
    *policy = (DownloadPolicy)DOWNLOAD_POLICY_DEFAULT;
    for (size_t i = 0; i < sizeof fields / sizeof *fields; i++) {
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, fields[i].key);
        if (valid(value, &fields[i])) *field(policy, &fields[i]) = (unsigned)value->valueint;
    }
}
