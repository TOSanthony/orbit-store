/* Extracts a verified ZIP, 7z or RAR download on the download worker.
 * Everything is written inside a private staging folder in homebrew; only
 * a complete result is moved into place, and existing names are never
 * replaced. Links, special files, encrypted entries and names exFAT cannot
 * store are refused. */
#ifdef ORBIT_DESKTOP
#define _POSIX_C_SOURCE 200809L
#endif
#include "extract.h"
#include "sevenzip.h"
#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define STAGING ".orbit-extract-"
#define MAX_ENTRIES 100000
#define MAX_PATH_BYTES 512
#define MAX_DEPTH 32
#define PATH_BYTES 1200
#define MARGIN (16 * 1024 * 1024)
#define REPORT_BYTES (4 * 1024 * 1024)

static bool stopped(const Extraction *x) {
    return (x->pause && atomic_load(x->pause)) || (x->cancel && atomic_load(x->cancel)) ||
           (x->stop && atomic_load(x->stop));
}
static bool utf8(const unsigned char *p) {
    while (*p) {
        if (*p < 0x80) { p++; continue; }
        int n = *p >= 0xc2 && *p <= 0xdf ? 1 : *p >= 0xe0 && *p <= 0xef ? 2 : *p >= 0xf0 && *p <= 0xf4 ? 3 : 0;
        if (!n) return false;
        /* Reject overlong forms, UTF-16 surrogates and values above U+10FFFF. */
        if ((*p == 0xe0 && p[1] < 0xa0) || (*p == 0xed && p[1] >= 0xa0) ||
            (*p == 0xf0 && p[1] < 0x90) || (*p == 0xf4 && p[1] >= 0x90)) return false;
        for (int i = 1; i <= n; i++)
            if ((p[i] & 0xc0) != 0x80) return false;
        p += n + 1;
    }
    return true;
}
/* Canonicalise in place to a relative path of plain names. '\\' is a Windows
 * separator in some archives and can't be part of an exFAT name. */
static bool safe_path(char *path) {
    for (char *p = path; *p; p++)
        if (*p == '\\') *p = '/';
    while (!strncmp(path, "./", 2)) memmove(path, path + 2, strlen(path + 2) + 1);
    size_t n = strlen(path);
    while (n && path[n - 1] == '/') path[--n] = 0;
    if (!n || n > MAX_PATH_BYTES || path[0] == '/' || !utf8((const unsigned char *)path))
        return false;
    unsigned depth = 0;
    for (const char *part = path;;) {
        const char *end = strchr(part, '/');
        size_t length = end ? (size_t)(end - part) : strlen(part);
        if (!length || length > 255 || (length == 1 && part[0] == '.') ||
            (length == 2 && part[0] == '.' && part[1] == '.') || ++depth > MAX_DEPTH)
            return false;
        for (size_t i = 0; i < length; i++) {
            unsigned char c = (unsigned char)part[i];
            if (c < 32 || c == 127 || strchr("\"*:<>?|", c)) return false;
        }
        if (length >= 7 && !strncmp(part, ".orbit-", 7)) return false; /* Orbit's own names. */
        if (!end) return true;
        part = end + 1;
    }
}
/* Operating-system clutter in archives made on a Mac: never extracted or counted. */
static bool clutter(const char *path) {
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    if (!strcmp(name, ".DS_Store") || !strncmp(name, "._", 2)) return true;
    for (const char *p = path; (p = strstr(p, "__MACOSX")); p += 8)
        if ((p == path || p[-1] == '/') && (p[8] == '/' || !p[8])) return true;
    return false;
}
static void staging_name(char *out, size_t cap, const char *job_id) {
    snprintf(out, cap, STAGING "%s", job_id);
}
/* Orbit's own folder only: never follows links, only descends into real folders. */
static int remove_tree(int parent, const char *name, const char *path, unsigned depth) {
    struct stat st;
    if (fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW)) return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode)) return unlinkat(parent, name, 0);
    if (depth > MAX_DEPTH + 1) return -1;
    int fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) return -1;
    int rc = -1;
    /* Some filesystems skip names removed during a scan; rescan until empty. */
    for (int pass = 0; pass < 4; pass++) {
        DIR *d = opendir(path);
        if (!d) break;
        struct dirent *e;
        rc = 0;
        while (!rc && (e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char child[PATH_BYTES];
            if (snprintf(child, sizeof child, "%s/%s", path, e->d_name) >= (int)sizeof child) rc = -1;
            else rc = remove_tree(fd, e->d_name, child, depth + 1);
        }
        closedir(d);
        if (rc) break;
        if (!(rc = unlinkat(parent, name, AT_REMOVEDIR)) || errno != ENOTEMPTY) break;
    }
    close(fd);
    return rc;
}
int extract_remove_tree(int parent, const char *name, const char *path) {
    return remove_tree(parent, name, path, 0);
}
bool extract_name_ok(const char *name) {
    char copy[256];
    if (strlen(name) >= sizeof copy || strchr(name, '/') || strchr(name, '\\')) return false;
    snprintf(copy, sizeof copy, "%s", name);
    return safe_path(copy) && !strcmp(copy, name);
}
int extract_discard(int dir, const char *path, const char *job_id) {
    char staging[64], full[PATH_BYTES];
    staging_name(staging, sizeof staging, job_id);
    if (snprintf(full, sizeof full, "%s/%s", path, staging) >= (int)sizeof full) return -1;
    return remove_tree(dir, staging, full, 0);
}
int extract_publish_file(int from_dir, const char *from, int to_dir, const char *to) {
    if (!linkat(from_dir, from, to_dir, to, 0)) {
        unlinkat(from_dir, from, 0);
        return 0;
    }
    /* exFAT has no hard links: reserve the name exclusively, then replace only that. */
    if (errno != EPERM && errno != EOPNOTSUPP && errno != ENOSYS) return -1;
    int reserve = openat(to_dir, to, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (reserve < 0) return -1;
    close(reserve);
    if (renameat(from_dir, from, to_dir, to)) {
        int saved = errno;
        unlinkat(to_dir, to, 0);
        errno = saved;
        return -1;
    }
    return 0;
}
/* Fixed sentences, so the interfaces can translate them. */
static void failure(struct archive *a, const char *what, char *error, size_t cap) {
    const char *detail = archive_error_string(a);
    if (archive_errno(a) == ENOMEM)
        snprintf(error, cap, "Not enough memory to extract this archive on the PS5. Use Orbit Zero to extract it on a computer.");
    else if (detail && (strstr(detail, "ncrypt") || strstr(detail, "assword")))
        snprintf(error, cap, "Password-protected archives aren't supported.");
    else
        snprintf(error, cap, "%s", what);
}
static bool format_matches(struct archive *a, const char *compression) {
    int f = archive_format(a) & ARCHIVE_FORMAT_BASE_MASK;
    return (!strcmp(compression, "zip") && f == ARCHIVE_FORMAT_ZIP) ||
           (!strcmp(compression, "7z") && f == ARCHIVE_FORMAT_7ZIP) ||
           (!strcmp(compression, "rar") && (f == ARCHIVE_FORMAT_RAR || f == ARCHIVE_FORMAT_RAR_V5));
}
/* Walk `path`'s parent folders below `stage`, creating them as needed and never
 * following links. Returns a descriptor for the parent that the caller closes. */
static int open_parent(int stage, char *path, char **leaf) {
    int fd = openat(stage, ".", O_RDONLY | O_DIRECTORY);
    char *part = path;
    for (char *slash; fd >= 0 && (slash = strchr(part, '/')); part = slash + 1) {
        *slash = 0;
        int next = -1;
        if ((!mkdirat(fd, part, 0755) || errno == EEXIST) && !fsync(fd))
            next = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        *slash = '/';
        close(fd);
        fd = next;
    }
    *leaf = part;
    return fd;
}
static int write_all(int fd, const unsigned char *data, size_t size, int64_t offset) {
    while (size) {
        ssize_t n = pwrite(fd, data, size, (off_t)offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        data += n;
        size -= (size_t)n;
        offset += n;
    }
    return 0;
}
static int publish(Extraction *x, int stage, const char *staging, const char *top, bool several,
                   char *error, size_t cap) {
    const char *name = several ? x->folder : top;
    struct stat st;
    if (!name[0] || !strncmp(name, ".orbit-", 7)) {
        snprintf(error, cap, "The archive has no usable top-level name.");
        return -1;
    }
    if (!fstatat(x->dir, name, &st, AT_SYMLINK_NOFOLLOW) || errno != ENOENT) {
        snprintf(error, cap, "%s already exists on this drive. Orbit will not overwrite it.", name);
        return -1;
    }
    int rc;
    if (several)
        rc = renameat(x->dir, staging, x->dir, name);
    else if (fstatat(stage, top, &st, AT_SYMLINK_NOFOLLOW))
        rc = -1;
    else {
        /* A folder can only replace an empty folder, so a check-then-rename is
         * enough; files use an exclusive reservation instead. */
        rc = S_ISDIR(st.st_mode) ? renameat(stage, top, x->dir, name)
                                 : extract_publish_file(stage, top, x->dir, name);
        if (!rc) unlinkat(x->dir, staging, AT_REMOVEDIR);
    }
    if (rc) {
        snprintf(error, cap, "Could not move the extracted files into place.");
        return -1;
    }
    if (fsync(x->dir)) {
        snprintf(error, cap, "Extracted files are in place, but their directory could not be saved. The archive was kept.");
        return -1;
    }
    snprintf(x->output, sizeof x->output, "%s", name);
    return 0;
}
static int extract_file(Extraction *x, struct archive *a, int stage, char *path, int64_t size,
                        int64_t *done, int64_t *reported, char *error, size_t cap) {
    struct statvfs fs;
    if (fstatvfs(stage, &fs) ||
        (uint64_t)fs.f_bavail * (fs.f_frsize ? fs.f_frsize : fs.f_bsize) < (uint64_t)(size > 0 ? size : 0) + MARGIN) {
        snprintf(error, cap, "Not enough free space to extract this archive.");
        return -1;
    }
    char *leaf;
    int parent = open_parent(stage, path, &leaf);
    int fd = parent < 0 ? -1 : openat(parent, leaf, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
    if (fd < 0) {
        if (errno == EEXIST) snprintf(error, cap, "The archive lists %s more than once.", path);
        else snprintf(error, cap, "Could not create %s.", path);
        if (parent >= 0) close(parent);
        return -1;
    }
    int rc = 0;
    int64_t end = 0, data = 0;
    for (;;) {
        if (stopped(x)) { rc = 1; break; }
        const void *block;
        size_t n;
        la_int64_t offset;
        int r = archive_read_data_block(a, &block, &n, &offset);
        if (r == ARCHIVE_EOF) break;
        if (r != ARCHIVE_OK) { failure(a, "The archive is damaged or uses an unsupported compression method.", error, cap); rc = -1; break; }
        if (offset < 0 || (size >= 0 && (offset > size || (int64_t)n > size - offset)) ||
            (x->expected > 0 && (int64_t)n > x->expected - *done - data)) {
            snprintf(error, cap, "The archive holds more data than the catalogue lists. Nothing was published.");
            rc = -1;
            break;
        }
        if (n && write_all(fd, block, n, offset)) {
            snprintf(error, cap, errno == ENOSPC ? "Not enough free space to extract this archive." : "Could not write %s.", path);
            rc = -1;
            break;
        }
        data += (int64_t)n;
        if (offset + (int64_t)n > end) end = offset + (int64_t)n;
        if (x->progress && *done + data - *reported >= REPORT_BYTES) {
            *reported = *done + data;
            x->progress(x->context, *reported);
        }
    }
    /* A trailing sparse region still belongs to the file. */
    if (!rc && size > end && ftruncate(fd, (off_t)size)) rc = -1;
    if (!rc && fsync(fd)) {
        snprintf(error, cap, "Could not write %s.", path);
        rc = -1;
    }
    if (close(fd) && !rc) {
        snprintf(error, cap, "Could not write %s.", path);
        rc = -1;
    }
    if (!rc && fsync(parent)) {
        snprintf(error, cap, "Could not save the extracted file's directory.");
        rc = -1;
    }
    close(parent);
    if (!rc) *done += size > end ? size : end;
    return rc;
}
static int extract_unencrypted(Extraction *x, char *error, size_t cap) {
    char staging[64], stage_path[PATH_BYTES], path[MAX_PATH_BYTES + 2], top[256] = "";
    staging_name(staging, sizeof staging, x->job_id);
    x->output[0] = 0;
    if (snprintf(stage_path, sizeof stage_path, "%s/%s", x->path, staging) >= (int)sizeof stage_path ||
        extract_discard(x->dir, x->path, x->job_id)) {
        snprintf(error, cap, "Could not remove an earlier extraction folder.");
        return -1;
    }
    if (mkdirat(x->dir, staging, 0755)) {
        snprintf(error, cap, "Could not create the extraction folder.");
        return -1;
    }
    int stage = openat(x->dir, staging, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    int input = openat(x->dir, x->archive, O_RDONLY | O_NOFOLLOW);
    struct archive *a = archive_read_new();
    int rc = -1;
    struct stat st;
    if (stage < 0 || input < 0 || fstat(input, &st) || !S_ISREG(st.st_mode) || !a) {
        snprintf(error, cap, "Could not open the downloaded archive.");
        goto done;
    }
    if (!strcmp(x->compression, "zip"))
        archive_read_support_format_zip_seekable(a); /* Central directory: ZIP64 and descriptors. */
    else if (!strcmp(x->compression, "7z"))
        archive_read_support_format_7zip(a);
    else if (!strcmp(x->compression, "rar")) {
        archive_read_support_format_rar(a);
        archive_read_support_format_rar5(a);
    } else {
        snprintf(error, cap, "Unsupported archive type.");
        goto done;
    }
    if (archive_read_open_fd(a, input, 1024 * 1024) != ARCHIVE_OK) {
        /* The format's errno differs between platforms; the message does not. */
        const char *detail = archive_error_string(a);
        if (detail && strstr(detail, "Unrecognized archive format"))
            snprintf(error, cap, "This download is not a %s archive.", x->compression);
        else
            failure(a, "Could not read the archive. It may be damaged.", error, cap);
        goto done;
    }
    int64_t done_bytes = 0, reported = 0;
    unsigned count = 0;
    bool several = false;
    for (rc = 0; !rc;) {
        if (stopped(x)) { rc = 1; break; }
        struct archive_entry *e;
        int r = archive_read_next_header(a, &e);
        if (r == ARCHIVE_EOF) break;
        /* A warning here is usually a name the C locale can't show; the UTF-8 name is used. */
        if (r != ARCHIVE_OK && r != ARCHIVE_WARN) { failure(a, "Could not read the archive. It may be damaged.", error, cap); rc = -1; break; }
        if (!format_matches(a, x->compression)) {
            snprintf(error, cap, "This download is not a %s archive.", x->compression);
            rc = -1;
            break;
        }
        if (++count > MAX_ENTRIES) {
            snprintf(error, cap, "The archive has too many entries.");
            rc = -1;
            break;
        }
        const char *name = archive_entry_pathname_utf8(e);
        if (!name) name = archive_entry_pathname(e);
        if (!name || strlen(name) >= sizeof path) {
            snprintf(error, cap, "The archive contains a file name Orbit can't store.");
            rc = -1;
            break;
        }
        snprintf(path, sizeof path, "%s", name);
        bool safe = safe_path(path);
        if (safe && clutter(path)) continue;
        if (archive_entry_is_encrypted(e)) {
            snprintf(error, cap, "Password-protected archives aren't supported.");
            rc = -1;
            break;
        }
        mode_t type = archive_entry_filetype(e);
        if (archive_entry_hardlink(e) || archive_entry_symlink(e) || (type != AE_IFREG && type != AE_IFDIR)) {
            snprintf(error, cap, "The archive contains links or special files. Orbit extracts only files and folders.");
            rc = -1;
            break;
        }
        if (!safe) {
            snprintf(error, cap, "The archive contains a file name Orbit can't store safely: %.80s", name);
            rc = -1;
            break;
        }
        size_t first = strcspn(path, "/");
        if (!top[0]) snprintf(top, sizeof top, "%.*s", (int)first, path);
        else if (strlen(top) != first || strncmp(top, path, first)) several = true;
        if (type == AE_IFDIR) {
            char *leaf;
            int parent = open_parent(stage, path, &leaf);
            if (parent < 0 || (mkdirat(parent, leaf, 0755) && errno != EEXIST) ||
                fstatat(parent, leaf, &st, AT_SYMLINK_NOFOLLOW) || !S_ISDIR(st.st_mode) || fsync(parent)) {
                snprintf(error, cap, "Could not create %s.", path);
                rc = -1;
            }
            if (parent >= 0) close(parent);
            continue;
        }
        int64_t size = archive_entry_size_is_set(e) ? archive_entry_size(e) : -1;
        if (x->expected > 0 && size > x->expected - done_bytes) {
            snprintf(error, cap, "The archive holds more data than the catalogue lists. Nothing was published.");
            rc = -1;
            break;
        }
        rc = extract_file(x, a, stage, path, size, &done_bytes, &reported, error, cap);
    }
    if (!rc && !top[0]) {
        snprintf(error, cap, "The archive is empty.");
        rc = -1;
    }
    if (!rc && x->expected > 0 && done_bytes != x->expected) {
        snprintf(error, cap, "The extracted files total %" PRId64 " bytes, but the catalogue lists %" PRId64 ". Nothing was published.",
                 done_bytes, x->expected);
        rc = -1;
    }
    if (!rc && x->progress) x->progress(x->context, done_bytes);
    if (!rc && fsync(stage)) {
        snprintf(error, cap, "Could not save the extraction directory.");
        rc = -1;
    }
    if (!rc) rc = publish(x, stage, staging, top, several, error, cap);
done:
    if (a) archive_read_free(a);
    if (input >= 0) close(input);
    if (stage >= 0) close(stage);
    if (rc) extract_discard(x->dir, x->path, x->job_id);
    return rc;
}

/* Encrypted archives use 7-Zip's decoder, but never its filesystem writer.
 * Validation, staging, durability, cancellation and publication remain ours. */
typedef struct {
    Extraction *x;
    int stage, fd, parent;
    int64_t declared, done, size, written, reported;
    char top[256], path[MAX_PATH_BYTES + 2];
    bool several;
    char *error;
    size_t cap;
} Encrypted;
static int encrypted_stopped(void *p) { return stopped(((Encrypted *)p)->x); }
static int encrypted_check(void *p, const char *name, int64_t size, int directory, int unsafe) {
    Encrypted *s = p;
    if (strlen(name) >= sizeof s->path) {
        snprintf(s->error, s->cap, "The archive contains a file name Orbit can't store."); return -1;
    }
    snprintf(s->path, sizeof s->path, "%s", name);
    bool safe = safe_path(s->path);
    if (safe && clutter(s->path)) return 2;
    if (unsafe) {
        snprintf(s->error, s->cap, "The archive contains links or special files. Orbit extracts only files and folders."); return -1;
    }
    if (!safe) {
        snprintf(s->error, s->cap, "The archive contains a file name Orbit can't store safely: %.80s", name); return -1;
    }
    size_t first = strcspn(s->path, "/");
    if (!s->top[0]) snprintf(s->top, sizeof s->top, "%.*s", (int)first, s->path);
    else if (strlen(s->top) != first || strncmp(s->path, s->top, first)) s->several = true;
    if (!directory) {
        if (size < 0 || size > INT64_MAX - s->declared ||
            (s->x->expected > 0 && size > s->x->expected - s->declared)) {
            snprintf(s->error, s->cap, "The archive holds more data than the catalogue lists. Nothing was published."); return -1;
        }
        s->declared += size;
    }
    return 0;
}
static int encrypted_begin(void *p, const char *name, int64_t size, int directory) {
    Encrypted *s = p;
    if (s->fd >= 0 || s->parent >= 0) return -1;
    snprintf(s->path, sizeof s->path, "%s", name);
    if (!safe_path(s->path)) return -1; // Same checked name, canonicalised again.
    struct statvfs fs;
    if (fstatvfs(s->stage, &fs) || (uint64_t)fs.f_bavail * (fs.f_frsize ? fs.f_frsize : fs.f_bsize) < (uint64_t)size + MARGIN) {
        snprintf(s->error, s->cap, "Not enough free space to extract this archive."); return -1;
    }
    char *leaf;
    s->parent = open_parent(s->stage, s->path, &leaf);
    if (s->parent < 0) return -1;
    s->written = 0; s->size = size;
    if (directory) {
        struct stat st;
        if ((mkdirat(s->parent, leaf, 0755) && errno != EEXIST) ||
            fstatat(s->parent, leaf, &st, AT_SYMLINK_NOFOLLOW) || !S_ISDIR(st.st_mode)) return -1;
    } else {
        s->fd = openat(s->parent, leaf, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
        if (s->fd < 0) {
            snprintf(s->error, s->cap, errno == EEXIST ? "The archive lists %s more than once." : "Could not create %s.", s->path);
            return -1;
        }
    }
    return 0;
}
static int encrypted_write(void *p, const void *data, size_t size) {
    Encrypted *s = p;
    if (stopped(s->x)) return 1;
    if (s->fd < 0 || size > (uint64_t)(s->size - s->written)) {
        snprintf(s->error, s->cap, "The archive holds more data than the catalogue lists. Nothing was published."); return -1;
    }
    if (write_all(s->fd, data, size, s->written)) {
        snprintf(s->error, s->cap, errno == ENOSPC ? "Not enough free space to extract this archive." : "Could not write %s.", s->path);
        return -1;
    }
    s->written += (int64_t)size;
    if (s->x->progress && s->done + s->written - s->reported >= REPORT_BYTES) {
        s->reported = s->done + s->written;
        s->x->progress(s->x->context, s->reported);
    }
    return 0;
}
static int encrypted_end(void *p) {
    Encrypted *s = p;
    if (s->written != s->size) return -1;
    if (s->fd >= 0) {
        int r = fsync(s->fd); if (close(s->fd)) r = -1;
        s->fd = -1;
        if (r) return -1;
    }
    if (s->parent >= 0) {
        int r = fsync(s->parent); if (close(s->parent)) r = -1;
        s->parent = -1;
        if (r) return -1;
    }
    s->done += s->written;
    return 0;
}
static int extract_password(Extraction *x, const char *password, char *error, size_t cap) {
    char staging[64];
    staging_name(staging, sizeof staging, x->job_id);
    if (extract_discard(x->dir, x->path, x->job_id) || mkdirat(x->dir, staging, 0755)) return -1;
    Encrypted s = {.x = x, .stage = -1, .fd = -1, .parent = -1, .error = error, .cap = cap};
    s.stage = openat(x->dir, staging, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    int input = openat(x->dir, x->archive, O_RDONLY | O_NOFOLLOW), rc = -1;
    struct stat st;
    error[0] = 0;
    if (s.stage < 0 || input < 0 || fstat(input, &st) || !S_ISREG(st.st_mode)) goto done;
    SevenCallbacks cb = {encrypted_check, encrypted_begin, encrypted_write, encrypted_end, encrypted_stopped};
    rc = seven_extract(input, x->compression, password, &cb, &s, error, cap);
    if (!rc && !s.top[0]) { snprintf(error, cap, "The archive is empty."); rc = -1; }
    if (!rc && x->expected > 0 && s.done != x->expected) {
        snprintf(error, cap, "The extracted files total %" PRId64 " bytes, but the catalogue lists %" PRId64 ". Nothing was published.", s.done, x->expected);
        rc = -1;
    }
    if (!rc && stopped(x)) rc = 1;
    if (!rc && fsync(s.stage)) rc = -1;
    if (!rc) rc = publish(x, s.stage, staging, s.top, s.several, error, cap);
    if (!rc && x->progress) x->progress(x->context, s.done);
done:
    if (s.fd >= 0) close(s.fd);
    if (s.parent >= 0) close(s.parent);
    if (input >= 0) close(input);
    if (s.stage >= 0) close(s.stage);
    if (rc && extract_discard(x->dir, x->path, x->job_id)) {
        snprintf(error, cap, "Could not remove an earlier extraction folder."); rc = -1;
    }
    if (rc < 0 && !error[0]) snprintf(error, cap, "Could not extract the archive. The downloaded archive was kept.");
    return rc;
}
int extract_archive(Extraction *x, char *error, size_t cap) {
    int rc = extract_unencrypted(x, error, cap);
    if (rc != -1 || !strstr(error, "Password-protected")) return rc;
    const char *passwords[] = {x->password, "DLPSGAME.COM", "[DLPSGAME.COM]", "SuperPSX",
                               "www.DLPSGAME.COM", "downloadgameps3.com", "hako"};
    for (size_t i = 0; i < sizeof passwords / sizeof passwords[0]; i++) {
        if (!passwords[i] || !*passwords[i]) continue;
        bool duplicate = false;
        for (size_t j = 0; j < i; j++) if (passwords[j] && !strcmp(passwords[i], passwords[j])) duplicate = true;
        if (duplicate) continue;
        if (stopped(x)) return 1;
        if (x->progress) x->progress(x->context, 0);
        rc = extract_password(x, passwords[i], error, cap);
        if (rc != -2) return rc;
    }
    return -1;
}

/* Header-only inspection: no output files, no executable loading, no publication.
 * The decoder is still untrusted input; apply the same path limits before using
 * its listing for space reservations or deciding whether we support its contents. */
typedef struct { char *name; int64_t size; bool directory; } InspectEntry;
typedef struct {
    Extraction *x;
    InspectEntry *entries;
    size_t count, capacity;
    int64_t bytes;
    char *error;
    size_t cap;
} Inspection;
static int inspect_stopped(void *p) { return stopped(((Inspection *)p)->x); }
static int inspect_check(void *p, const char *name, int64_t size, int directory, int unsafe) {
    Inspection *s = p;
    char path[MAX_PATH_BYTES + 2];
    if (strlen(name) >= sizeof path) goto invalid;
    snprintf(path, sizeof path, "%s", name);
    if (!safe_path(path)) goto invalid;
    if (clutter(path)) return 2;
    if (unsafe) goto invalid;
    if (size < 0 || size > 9007199254740991LL - s->bytes || s->count >= MAX_ENTRIES) goto invalid;
    if (s->count == s->capacity) {
        size_t capacity = s->capacity ? s->capacity * 2 : 64;
        InspectEntry *entries = realloc(s->entries, capacity * sizeof *entries);
        if (!entries) goto memory;
        s->entries = entries; s->capacity = capacity;
    }
    char *saved = strdup(path);
    if (!saved) goto memory;
    s->entries[s->count++] = (InspectEntry){saved, size, directory != 0};
    if (!directory) s->bytes += size;
    return 0;
invalid:
    snprintf(s->error, s->cap, "The archive contains unsafe names, links, special files or invalid sizes. Nothing was extracted.");
    return -1;
memory:
    snprintf(s->error, s->cap, "Not enough memory to inspect this archive. Use Orbit Zero on a computer.");
    return -1;
}
static int inspect_compare(const void *a, const void *b) {
    return strcasecmp(((const InspectEntry *)a)->name, ((const InspectEntry *)b)->name);
}
static InspectEntry *inspect_find(Inspection *s, const char *name) {
    InspectEntry key = {.name = (char *)name};
    return bsearch(&key, s->entries, s->count, sizeof *s->entries, inspect_compare);
}
static bool ends_with(const char *text, const char *suffix) {
    size_t a = strlen(text), b = strlen(suffix);
    return a >= b && !strcasecmp(text + a - b, suffix);
}
static int inspect_finish(Inspection *s, char format[12]) {
    qsort(s->entries, s->count, sizeof *s->entries, inspect_compare);
    size_t files = 0;
    const char *single = NULL;
    for (size_t i = 0; i < s->count; i++) {
        InspectEntry *e = &s->entries[i];
        if (i && !strcasecmp(e->name, s->entries[i - 1].name)) goto invalid;
        char parent[MAX_PATH_BYTES + 2];
        snprintf(parent, sizeof parent, "%s", e->name);
        for (char *end = strchr(parent, '/'); end; end = strchr(end + 1, '/')) {
            *end = 0;
            InspectEntry *p = inspect_find(s, parent);
            *end = '/';
            if (p && !p->directory) goto invalid;
        }
        if (!e->directory) { files++; single = e->name; }
    }
    if (!files || s->bytes <= 0) {
        snprintf(s->error, s->cap, "The archive is empty.");
        return -1;
    }
    const char *kind = files == 1 ? ends_with(single, ".pkg") ? "PKG"
        : ends_with(single, ".exfat") ? "exFAT" : ends_with(single, ".ffpfsc") ? "FFPFSC" : NULL : NULL;
    if (!kind) {
        /* Layout recognition supplies a label only. Safe contents do not need
         * to match a known game layout in order to be extracted. */
        for (size_t i = 0; i < s->count && !kind; i++) {
            const char *name = s->entries[i].name;
            const char *suffix = ends_with(name, "sce_sys/param.json") ? "sce_sys/param.json"
                : ends_with(name, "sce_sys/param.sfo") ? "sce_sys/param.sfo" : NULL;
            if (!suffix || s->entries[i].directory) continue;
            size_t root = strlen(name) - strlen(suffix);
            if (root && name[root - 1] != '/') continue;
            char executable[MAX_PATH_BYTES + 2];
            snprintf(executable, sizeof executable, "%.*seboot.bin", (int)root, name);
            InspectEntry *boot = inspect_find(s, executable);
            if (!boot || boot->directory || boot->size <= 0) continue;
            bool contained = true;
            for (size_t j = 0; j < s->count; j++) {
                if (!s->entries[j].directory && strncasecmp(s->entries[j].name, name, root)) contained = false;
            }
            if (contained) kind = "Game folder";
        }
    }
    snprintf(format, 12, "%s", kind ? kind : "Unknown");
    return 0;
invalid:
    snprintf(s->error, s->cap, "The archive contains conflicting file names. Nothing was extracted."); return -1;
}
int inspect_archive(Extraction *x, int64_t *bytes, char format[12], char *error, size_t cap) {
    const char *passwords[] = {x->password ? x->password : "", "DLPSGAME.COM", "[DLPSGAME.COM]", "SuperPSX",
                               "www.DLPSGAME.COM", "downloadgameps3.com", "hako"};
    *bytes = 0; format[0] = 0;
    for (size_t i = 0; i < sizeof passwords / sizeof passwords[0]; i++) {
        if (stopped(x)) return 1;
        Inspection s = {.x = x, .error = error, .cap = cap};
        int fd = openat(x->dir, x->archive, O_RDONLY | O_NOFOLLOW), rc = -1;
        struct stat st;
        error[0] = 0;
        SevenCallbacks cb = {.check = inspect_check, .stopped = inspect_stopped};
        if (fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_nlink == 1)
            rc = seven_inspect(fd, x->compression, passwords[i], &cb, &s, error, cap);
        if (fd >= 0) close(fd);
        if (!rc && !stopped(x)) rc = inspect_finish(&s, format);
        if (!rc && stopped(x)) rc = 1;
        if (!rc) *bytes = s.bytes;
        for (size_t j = 0; j < s.count; j++) free(s.entries[j].name);
        free(s.entries);
        if (rc != -2) return rc;
    }
    return -1;
}
