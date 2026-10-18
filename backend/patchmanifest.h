#ifndef ORBIT_PATCHMANIFEST_H
#define ORBIT_PATCHMANIFEST_H
/* The JSON manifest Sony publishes next to a patch: the pieces of the package, in order, each with
 * its own address and checksum. Parsing only: no network, so it is tested on a computer.
 *
 * {"originalFileSize":N,"pieces":[{"url":"...","fileOffset":0,"fileSize":N,"hashValue":"<hex>"}, ...]}
 *
 * Everything is checked before a byte is downloaded: sizes are sane, every address is one of Sony's
 * download servers, and the pieces follow each other exactly from offset 0 to the total. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PATCHMANIFEST_MAX_PIECES 20000
#define PATCHMANIFEST_MAX_TOTAL (512LL * 1024 * 1024 * 1024)

typedef struct {
    char url[768];
    int64_t offset, size;
    char hash[65]; /* lowercase hex, 40 (SHA-1) or 64 (SHA-256) digits */
} patchmanifest_piece;

typedef struct {
    int64_t total;
    size_t count;
    patchmanifest_piece *pieces; /* malloc'd; free with patchmanifest_free */
} patchmanifest;

/* 0, or a negative value with a short English reason in `error`. */
int patchmanifest_parse(const char *json, size_t length, patchmanifest *out, char *error, size_t cap);
void patchmanifest_free(patchmanifest *manifest);
#endif
