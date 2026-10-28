#ifndef ORBIT_BROWSER_NATIVE_H
#define ORBIT_BROWSER_NATIVE_H
#include "browser_scan.h"
#include <signal.h>
#include <stdatomic.h>
#include <time.h>
typedef struct {
    int pid;
    int64_t started_seconds, started_microseconds;
    char name[20];
    atomic_bool *cancel;
    volatile sig_atomic_t *interrupted;
    time_t deadline;
    bool identity_changed;
    uint64_t cursor;
} ProbeTarget;
#define PROBE_TARGET_LIMIT 4U
#define PROBE_SCAN_SLICE (1024U * 1024U)
/* Enumerate a bounded set instead of guessing which of ShellUI's and the
 * native dialog's network processes owns the requested page. */
int probe_targets_find(ProbeTarget *targets, size_t capacity);
/* 1: a unique network process, 0: absent, -1: unsupported metadata,
 * -2: multiple eligible processes. Never guess a process. */
int probe_target_find(ProbeTarget *target);
bool probe_target_active(void *context);
int probe_target_read(void *context, uint64_t address, void *bytes, size_t length);
ProbeResult probe_target_scan(ProbeTarget *target, ProbeReader *reader, const char *marker);
ProbeResult probe_target_scan_slice(ProbeTarget *, ProbeReader *, const char *, size_t);
int probe_browser_open(const char *url);
int probe_browser_user(void);
#endif
