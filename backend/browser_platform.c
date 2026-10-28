#include "browser_platform.h"
#ifndef ORBIT_DESKTOP
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/sysctl.h>
#include <ps5/kernel.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* No private structure offsets, ptrace, writes, code injection or process kills. */
#define METADATA_LIMIT (4U * 1024U * 1024U)
static void *query(int kind, int pid, size_t *size) {
    int mib[] = {CTL_KERN, KERN_PROC, kind, pid};
    *size = 0;
    if (sysctl(mib, 4, NULL, size, NULL, 0) || !*size || *size > METADATA_LIMIT)
        return NULL;
    void *bytes = calloc(1, *size);
    if (!bytes) return NULL;
    size_t capacity = *size;
    if (sysctl(mib, 4, bytes, size, NULL, 0) || *size > capacity) {
        free(bytes);
        return NULL;
    }
    return bytes;
}

static bool network_name(const char *name, size_t size) {
    return memchr(name, 0, size) &&
           (!strcmp(name, "SceNKNetworkProcess") || !strcmp(name, "SceNKNetworkProc"));
}

int probe_targets_find(ProbeTarget *targets, size_t capacity) {
    if (!targets || !capacity || capacity > PROBE_TARGET_LIMIT) return -1;
    size_t size = 0;
    unsigned char *bytes = query(KERN_PROC_PROC, 0, &size);
    if (!bytes) return -1;
    int count = 0;
    for (size_t offset = 0; offset < size;) {
        /* SDK's PS sample documents variable-size kinfo_proc records. */
        struct kinfo_proc entry = {0};
        int length = 0;
        if (size - offset < sizeof length) { count = -1; break; }
        memcpy(&length, bytes + offset, sizeof length);
        if (length < (int)(offsetof(struct kinfo_proc, ki_comm) + sizeof entry.ki_comm) ||
            (size_t)length > size - offset) { count = -1; break; }
        memcpy(&entry, bytes + offset, (size_t)length < sizeof entry ? (size_t)length : sizeof entry);
        if (entry.ki_pid > 0 && entry.ki_pid != getpid() &&
            network_name(entry.ki_comm, sizeof entry.ki_comm)) {
            if ((size_t)count == capacity) { count = -2; break; }
            ProbeTarget *target = &targets[count++];
            target->pid = entry.ki_pid;
            target->started_seconds = entry.ki_start.tv_sec;
            target->started_microseconds = entry.ki_start.tv_usec;
            memcpy(target->name, entry.ki_comm, sizeof target->name);
        }
        offset += (size_t)length;
    }
    free(bytes);
    return count;
}

int probe_target_find(ProbeTarget *target) {
    return probe_targets_find(target, 1);
}

bool probe_target_active(void *context) {
    ProbeTarget *target = context;
    struct timespec now;
    if (atomic_load(target->cancel) || (target->interrupted && *target->interrupted) ||
        clock_gettime(CLOCK_MONOTONIC, &now) ||
        now.tv_sec >= target->deadline) return false;
    size_t size = 0;
    struct kinfo_proc *entry = query(KERN_PROC_PID, target->pid, &size);
    bool same = entry && size >= offsetof(struct kinfo_proc, ki_comm) + sizeof entry->ki_comm &&
                entry->ki_structsize >= (int)(offsetof(struct kinfo_proc, ki_comm) + sizeof entry->ki_comm) &&
                (size_t)entry->ki_structsize <= size &&
                entry->ki_pid == target->pid &&
                entry->ki_start.tv_sec == target->started_seconds &&
                entry->ki_start.tv_usec == target->started_microseconds &&
                network_name(entry->ki_comm, sizeof entry->ki_comm) &&
                !strcmp(entry->ki_comm, target->name);
    free(entry);
    if (!same) target->identity_changed = true;
    return same;
}

int probe_target_read(void *context, uint64_t address, void *bytes, size_t length) {
    ProbeTarget *target = context;
    /* One small read at a time, capped below 8 MiB/s while the user-requested
     * page is open. The session budget also covers late verification. */
    if (!length || length > PROBE_CHUNK) return -1;
    struct timespec delay = {.tv_nsec = PROBE_READ_DELAY_NS};
    while (nanosleep(&delay, &delay)) {
        if (errno != EINTR || !probe_target_active(target)) return -1;
    }
    if (!probe_target_active(target)) return -1;
    return kernel_proc_copyout(target->pid, (intptr_t)address, bytes, length);
}

ProbeResult probe_target_scan_slice(ProbeTarget *target, ProbeReader *reader, const char *marker, size_t slice) {
    if (slice && slice <= PROBE_MATCH_MAX * 4) return PROBE_INVALID;
    if (!probe_target_active(target)) return PROBE_STOPPED;
    size_t size = 0;
    unsigned char *bytes = query(KERN_PROC_VMMAP, target->pid, &size);
    if (!bytes) return PROBE_INVALID;
    ProbeResult result = PROBE_ABSENT;
    const uint64_t before = reader->attempted;
    bool end_of_map = true;
    for (size_t offset = 0; offset < size;) {
        struct kinfo_vmentry entry = {0};
        int length = 0;
        if (size - offset < sizeof length) { result = PROBE_INVALID; break; }
        memcpy(&length, bytes + offset, sizeof length);
        if (length < (int)offsetof(struct kinfo_vmentry, kve_path) + 1 ||
            (size_t)length > size - offset) { result = PROBE_INVALID; break; }
        memcpy(&entry, bytes + offset, (size_t)length < sizeof entry ? (size_t)length : sizeof entry);
        /* Only anonymous, readable/writable, non-executable user memory. */
        bool eligible = (entry.kve_type == KVME_TYPE_DEFAULT || entry.kve_type == KVME_TYPE_SWAP) &&
                        (entry.kve_protection & (KVME_PROT_READ | KVME_PROT_WRITE)) ==
                            (KVME_PROT_READ | KVME_PROT_WRITE) &&
                        !(entry.kve_protection & KVME_PROT_EXEC) &&
                        !(entry.kve_flags & KVME_FLAG_NOCOREDUMP) && !entry.kve_path[0] &&
                        entry.kve_start >= 0x4000 && entry.kve_start < entry.kve_end &&
                        entry.kve_end <= INT64_MAX;
        if (eligible && entry.kve_end > target->cursor) {
            uint64_t start = target->cursor > entry.kve_start ? target->cursor : entry.kve_start;
            uint64_t end = entry.kve_end;
            uint64_t left = slice && reader->attempted - before < slice
                ? slice - (reader->attempted - before) : 0;
            if (slice && end - start > left) end = start + left;
            if (start >= end) { end_of_map = false; break; }
            result = probe_scan_region(reader, start, end, marker);
            if (result != PROBE_ABSENT) break;
            /* Re-read a bounded overlap so URLs crossing a slice edge retain
             * the same ASCII/UTF-16/JSON handling as a continuous scan. */
            if (end < entry.kve_end) {
                const uint64_t overlap = PROBE_MATCH_MAX * 4 - 1;
                target->cursor = end - start > overlap ? end - overlap : start;
                end_of_map = false;
                break;
            }
            target->cursor = end;
            if (slice && reader->attempted - before >= slice) {
                end_of_map = false;
                break;
            }
        }
        offset += (size_t)length;
    }
    if (end_of_map && result == PROBE_ABSENT) target->cursor = 0;
    free(bytes);
    return result;
}

ProbeResult probe_target_scan(ProbeTarget *target, ProbeReader *reader, const char *marker) {
    target->cursor = 0;
    return probe_target_scan_slice(target, reader, marker, 0);
}

#else
int probe_target_find(ProbeTarget *target) { (void)target; return -1; }
int probe_targets_find(ProbeTarget *targets, size_t capacity) { (void)targets; (void)capacity; return -1; }
bool probe_target_active(void *context) { (void)context; return false; }
int probe_target_read(void *context, uint64_t address, void *bytes, size_t length) {
    (void)context; (void)address; (void)bytes; (void)length; return -1;
}
ProbeResult probe_target_scan(ProbeTarget *target, ProbeReader *reader, const char *marker) {
    (void)target; (void)reader; (void)marker; return PROBE_INVALID;
}
ProbeResult probe_target_scan_slice(ProbeTarget *target, ProbeReader *reader, const char *marker, size_t slice) {
    (void)slice; return probe_target_scan(target, reader, marker);
}
#endif
