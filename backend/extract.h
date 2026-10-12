#ifndef ORBIT_EXTRACT_H
#define ORBIT_EXTRACT_H
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    int dir;                 /* The job's verified homebrew directory. */
    const char *path;        /* Its path, used only to list Orbit's own staging folder. */
    const char *archive;     /* Archive file name inside dir. */
    const char *job_id;      /* Names the private staging folder. */
    const char *compression; /* "zip", "7z" or "rar": the detected format must match. */
    const char *password;    /* Optional user override or catalogue password; never printed in errors. */
    const char *folder;      /* Published name when the archive has several top-level entries. */
    int64_t expected;        /* Catalogue-declared total of regular file sizes. */
    atomic_bool *pause, *cancel, *stop; /* Any set: stop and discard the staging folder. */
    void (*progress)(void *context, int64_t written);
    void *context;
    char output[160]; /* Published name on success. */
} Extraction;
/* 0 on success, 1 when paused or cancelled, -1 with error set. The staging
 * folder is always removed unless extraction succeeded and was published. */
/* Inspect headers only, with no output. Validate safe paths and exact listed
 * bytes; report a recognised layout or Unknown without restricting contents. */
int inspect_archive(Extraction *x, int64_t *bytes, char format[12], char *error, size_t cap);
int extract_archive(Extraction *x, char *error, size_t cap);
/* Remove a leftover staging folder from an interrupted extraction. */
int extract_discard(int dir, const char *path, const char *job_id);
/* Rename a regular file without ever replacing an existing name. */
int extract_publish_file(int from_dir, const char *from, int to_dir, const char *to);
/* Remove one of Orbit's own staging folders; never follows links. */
int extract_remove_tree(int parent, const char *name, const char *path);
/* A single name exFAT can store that is not one of Orbit's own (".orbit-"). */
bool extract_name_ok(const char *name);
#endif
