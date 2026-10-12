/* Paired LAN file receiver. Disk I/O and hashing never run on the HTTP thread. */
#include "receive.h"
#include "extract.h"
#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    char id[33], owner[65], state[24], error[192], hash[65];
    char folder[512]; /* Below homebrew: empty, or a path inside an open bundle. */
    Job pin;
    int64_t offset, size;
    uint64_t file_device, file_inode;
    bool partials_retained;
} Receiver;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static pthread_t thread;
static bool started, stopping, working;
static Receiver current;
/* A folder sent as several files. They publish inside a private staging folder,
 * which becomes the visible folder only once every file has arrived. */
typedef struct {
    char id[33], owner[65], state[16], name[256], last[33];
    Job pin;
    int64_t files, bytes, total;
    uint64_t folder_device, folder_inode;
} Bundle;
static Bundle bundle;
static char command[16];
static char command_proof[65];
typedef struct {
    unsigned char *data;
    size_t size;
    char hash[65];
} Chunk;
/* Includes the active disk write: never buffer more than 32 MiB here.
 * HTTP request bodies have their own separate two-request limit. */
static Chunk pending[ORBIT_RECEIVE_WINDOW];
static size_t pending_count;
static bool writing_chunk;
static int64_t accepted_offset;
typedef struct {
    int fd, dir;
    char id[33];
    EVP_MD_CTX *hash;
    int64_t hashed;
    struct stat stamp;
} ReceiverDisk;
/* Owned exclusively by the disk worker, retained across checkpoints. */
static ReceiverDisk disk = {.fd = -1, .dir = -1};
#ifdef ORBIT_TEST
static atomic_uint checkpoint_count, prefix_hash_reads;
#endif

static void disk_close(void) {
    if (disk.fd >= 0) close(disk.fd);
    if (disk.dir >= 0) close(disk.dir);
    EVP_MD_CTX_free(disk.hash);
    disk = (ReceiverDisk){.fd = -1, .dir = -1};
}

static bool hex(const char *s, size_t n) {
    return strlen(s) == n && strspn(s, "0123456789abcdef") == n;
}
static bool name_ok(const char *s) {
    size_t n = strlen(s);
    if (!n || n >= sizeof current.pin.filename || s[0] == '.' || strchr(s, '/') || strchr(s, '\\'))
        return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] < 32 || s[i] == 127)
            return false;
    return true;
}
static bool active(const Receiver *r) {
    return r->id[0] && strcmp(r->state, "complete") && strcmp(r->state, "cancelled");
}
static bool bundle_open(void) {
    return bundle.id[0] && !strcmp(bundle.state, "open");
}
static void bundle_folder(const char *id, char *out, size_t cap) {
    snprintf(out, cap, ".orbit-bundle-%s", id);
}
static bool in_bundle(const Receiver *r) {
    char folder[64];
    bundle_folder(bundle.id, folder, sizeof folder);
    size_t n = strlen(folder);
    return bundle.id[0] && !strncmp(r->folder, folder, n) && (!r->folder[n] || r->folder[n] == '/');
}
/* ".orbit-bundle-<id>" followed by an optional relative path of plain names. */
static bool folder_ok(const char *folder) {
    if (!*folder) return true;
    if (strncmp(folder, ".orbit-bundle-", 14) || strlen(folder) >= sizeof current.folder) return false;
    char path[sizeof current.folder];
    copy_text(path, sizeof path, folder + 14);
    char *rest = strchr(path, '/');
    if (rest) *rest++ = 0;
    if (!hex(path, 32)) return false;
    for (char *part = rest, *next; part; part = next) {
        next = strchr(part, '/');
        if (next) *next++ = 0;
        if (!extract_name_ok(part)) return false;
    }
    return true;
}
/* The transfer's folder below homebrew. Each level is opened without following
 * links; `create` adds missing levels inside the bundle, never the bundle itself. */
static int receiver_dir(const Job *pin, const char *folder, bool create) {
    int fd = storage_open(pin);
    char path[sizeof current.folder];
    copy_text(path, sizeof path, folder);
    bool first = true;
    for (char *part = *path ? path : NULL, *next; fd >= 0 && part; part = next, first = false) {
        next = strchr(part, '/');
        if (next) *next++ = 0;
        int child = -1;
        if ((!create || first || !mkdirat(fd, part, 0755) || errno == EEXIST) && !fsync(fd))
            child = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (first && child >= 0 && bundle.id[0]) {
            char expected[64];
            struct stat st;
            bundle_folder(bundle.id, expected, sizeof expected);
            if (!strcmp(part, expected) && (fstat(child, &st) ||
                (uint64_t)st.st_dev != bundle.folder_device || (uint64_t)st.st_ino != bundle.folder_inode)) {
                close(child); child = -1; errno = ESTALE;
            }
        }
        int saved = errno;
        close(fd);
        fd = child;
        errno = saved;
    }
    return fd;
}
static void busy(bool value) {
    pthread_mutex_lock(&orbit.mutex);
    orbit.receiver_active = value;
    pthread_cond_broadcast(&orbit.changed);
    pthread_mutex_unlock(&orbit.mutex);
}
static void fail(Receiver *r, const char *message) {
    copy_text(r->state, sizeof r->state, "error");
    copy_text(r->error, sizeof r->error, message);
}
static void partial(const Receiver *r, char *s, size_t n) {
    snprintf(s, n, ".orbit-zero-%s.part", r->id);
}
static cJSON *json(const Receiver *r, bool disk) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "protocol", 1);
    cJSON_AddNumberToObject(o, "chunkBytes", ORBIT_RECEIVE_CHUNK);
    cJSON_AddStringToObject(o, "id", r->id);
    cJSON_AddStringToObject(o, "state", r->id[0] ? r->state : "idle");
    cJSON_AddStringToObject(o, "error", r->error);
    cJSON_AddStringToObject(o, "filename", r->pin.filename);
    cJSON_AddStringToObject(o, "storageId", r->pin.storage_id);
    cJSON_AddNumberToObject(o, "size", (double)r->size);
    cJSON_AddNumberToObject(o, "offset", (double)r->offset);
    cJSON_AddBoolToObject(o, "partialsRetained", r->partials_retained);
    if (disk) {
        cJSON_AddStringToObject(o, "owner", r->owner);
        cJSON_AddStringToObject(o, "folder", r->folder);
        cJSON_AddStringToObject(o, "root", r->pin.root);
        cJSON_AddStringToObject(o, "sha256", r->hash);
        char n[32];
#define ID(k, v)                                                                                   \
    snprintf(n, sizeof n, "%llu", (unsigned long long)(v));                                        \
    cJSON_AddStringToObject(o, k, n)
        ID("device", r->pin.device);
        ID("inode", r->pin.inode);
        ID("fileDevice", r->file_device);
        ID("fileInode", r->file_inode);
#undef ID
    }
    return o;
}
static int allwrite(int fd, const void *data, size_t n) {
    const unsigned char *p = data;
    while (n) {
        ssize_t k = write(fd, p, n);
        if (k < 0 && errno == EINTR)
            continue;
        if (k <= 0)
            return -1;
        p += k;
        n -= k;
    }
    return 0;
}
static int save(const Receiver *r) {
    cJSON *o = json(r, true);
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!s)
        return -1;
    int fd = openat(orbit.state_fd, "receive.tmp", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    int result = fd < 0 ? -1 : allwrite(fd, s, strlen(s));
    free(s);
    if (fd >= 0) {
        if (fsync(fd))
            result = -1;
        if (close(fd))
            result = -1;
    }
    if (!result && renameat(orbit.state_fd, "receive.tmp", orbit.state_fd, "receive.json"))
        result = -1;
    if (!result && fsync(orbit.state_fd))
        result = -1;
    return result;
}
static int bundle_save(void) {
    cJSON *o = cJSON_CreateObject();
    char n[32];
    cJSON_AddStringToObject(o, "id", bundle.id);
    cJSON_AddStringToObject(o, "owner", bundle.owner);
    cJSON_AddStringToObject(o, "state", bundle.state);
    cJSON_AddStringToObject(o, "name", bundle.name);
    cJSON_AddStringToObject(o, "last", bundle.last);
    cJSON_AddStringToObject(o, "storageId", bundle.pin.storage_id);
    cJSON_AddStringToObject(o, "root", bundle.pin.root);
#define ID(k, v) snprintf(n, sizeof n, "%llu", (unsigned long long)(v)); cJSON_AddStringToObject(o, k, n)
    ID("device", bundle.pin.device);
    ID("inode", bundle.pin.inode);
    ID("folderDevice", bundle.folder_device);
    ID("folderInode", bundle.folder_inode);
#undef ID
    cJSON_AddNumberToObject(o, "files", (double)bundle.files);
    cJSON_AddNumberToObject(o, "bytes", (double)bundle.bytes);
    cJSON_AddNumberToObject(o, "total", (double)bundle.total);
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!s) return -1;
    int fd = openat(orbit.state_fd, "receive-bundle.tmp", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    int result = fd < 0 ? -1 : allwrite(fd, s, strlen(s));
    free(s);
    if (fd >= 0) {
        if (fsync(fd)) result = -1;
        if (close(fd)) result = -1;
    }
    if (!result && renameat(orbit.state_fd, "receive-bundle.tmp", orbit.state_fd, "receive-bundle.json")) result = -1;
    if (!result && fsync(orbit.state_fd)) result = -1;
    return result;
}
static void digest_text(const unsigned char *hash, char *out) {
    for (int i = 0; i < 32; i++)
        snprintf(out + i * 2, 3, "%02x", hash[i]);
}
static bool digest_bytes(const unsigned char *data, size_t size, const char *expected) {
    unsigned char hash[32];
    unsigned n = 0;
    char text[65];
    if (!EVP_Digest(data, size, hash, &n, EVP_sha256(), NULL) || n != 32)
        return false;
    digest_text(hash, text);
    return !strcmp(text, expected);
}
static int hash_file(int fd, char *out) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned char data[65536], hash[32];
    unsigned n = 0;
    if (!ctx || !EVP_DigestInit_ex(ctx, EVP_sha256(), NULL)) {
        EVP_MD_CTX_free(ctx);
        return -1;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
        EVP_MD_CTX_free(ctx);
        return -1;
    }
    int result = 0;
    for (;;) {
        ssize_t k = read(fd, data, sizeof data);
        if (k < 0 && errno == EINTR)
            continue;
        if (k < 0) {
            result = -1;
            break;
        }
        if (!k)
            break;
        if (!EVP_DigestUpdate(ctx, data, (size_t)k)) {
            result = -1;
            break;
        }
    }
    if (!result && (!EVP_DigestFinal_ex(ctx, hash, &n) || n != 32))
        result = -1;
    EVP_MD_CTX_free(ctx);
    if (!result)
        digest_text(hash, out);
    return result;
}
static int open_partial(Receiver *r, int dir, bool create, bool removing) {
    char path[80];
    partial(r, path, sizeof path);
    int fd = openat(dir, path, O_RDWR | O_NOFOLLOW | (create ? O_CREAT | O_EXCL : 0), 0600);
    if (fd < 0)
        return -1;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_nlink != 1 ||
        (!create && ((uint64_t)st.st_dev != r->file_device ||
                     (uint64_t)st.st_ino != r->file_inode || (!removing && st.st_size < r->offset)))) {
        close(fd);
        errno = ESTALE;
        return -1;
    }
    if (create) {
        r->file_device = (uint64_t)st.st_dev;
        r->file_inode = (uint64_t)st.st_ino;
    }
    return fd;
}
/* Mount device/inode numbers can change after suspend. Only an explicit paired
 * request carrying the computer's checksum of the entire durable prefix may
 * rebind a transfer. A USB slot or a matching filename alone is not identity. */
static bool rebind_partial(Receiver *r, const char *proof) {
    if (!hex(proof, 64) || r->offset <= 0 || !r->file_inode) return false;
    Storage choices[ORBIT_MAX_STORAGE], *selected = NULL;
    size_t count = storage_list(choices);
    for (size_t i = 0; i < count; i++)
        if (!strcmp(choices[i].id, r->pin.storage_id) && !strcmp(choices[i].root, r->pin.root))
            selected = &choices[i];
    if (!selected) {
        fail(r, "Reconnect the original destination drive, then resume.");
        return false;
    }
    Job pin = r->pin;
    pin.device = selected->device; pin.inode = selected->inode;
    int dir = receiver_dir(&pin, r->folder, false), fd = -1;
    EVP_MD_CTX *ctx = NULL;
    bool ok = false;
    char path[80]; partial(r, path, sizeof path);
    struct stat before, after, named, destination;
    if (dir < 0) goto done;
    /* Never adopt, replace or remove an already-published destination. */
    if (!fstatat(dir, pin.filename, &destination, AT_SYMLINK_NOFOLLOW) || errno != ENOENT)
        goto done;
    fd = openat(dir, path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0 || fstat(fd, &before) || !S_ISREG(before.st_mode) || before.st_nlink != 1 ||
        before.st_dev != pin.device || before.st_size < r->offset || before.st_size > r->size)
        goto done;
    ctx = EVP_MD_CTX_new();
    if (!ctx || !EVP_DigestInit_ex(ctx, EVP_sha256(), NULL)) goto done;
    unsigned char bytes[256 * 1024], digest[32]; unsigned length = 0;
    int64_t left = r->offset;
    while (left) {
        pthread_mutex_lock(&lock); bool stop = stopping; pthread_mutex_unlock(&lock);
        if (stop) goto done;
        ssize_t n = read(fd, bytes, left < (int64_t)sizeof bytes ? (size_t)left : sizeof bytes);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0 || !EVP_DigestUpdate(ctx, bytes, (size_t)n)) goto done;
        left -= n;
    }
    char checksum[65];
    if (!EVP_DigestFinal_ex(ctx, digest, &length) || length != 32) goto done;
    digest_text(digest, checksum);
    if (strcmp(checksum, proof) || fstat(fd, &after) ||
        before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
        before.st_size != after.st_size || after.st_nlink != 1 ||
        before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
        before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec ||
        !storage_matches(&pin) || fstatat(dir, path, &named, AT_SYMLINK_NOFOLLOW) ||
        !S_ISREG(named.st_mode) || named.st_nlink != 1 ||
        named.st_dev != after.st_dev || named.st_ino != after.st_ino)
        goto done;
    r->pin = pin;
    r->file_device = (uint64_t)after.st_dev;
    r->file_inode = (uint64_t)after.st_ino;
    ok = true;
done:
    EVP_MD_CTX_free(ctx);
    if (fd >= 0) close(fd);
    if (dir >= 0) close(dir);
    if (!ok) fail(r, "The saved PS5 partial could not be verified against the computer copy. No files were changed.");
    return ok;
}
/* Reconstruct only a resumed prefix. Fresh transfers hash buffers as written. */
static int disk_hash_prefix(Receiver *r) {
    EVP_MD_CTX_free(disk.hash);
    disk.hash = EVP_MD_CTX_new();
    if (!disk.hash || !EVP_DigestInit_ex(disk.hash, EVP_sha256(), NULL) ||
        lseek(disk.fd, 0, SEEK_SET) < 0) return -1;
    unsigned char bytes[256 * 1024];
    int64_t left = r->offset;
#ifdef ORBIT_TEST
    if (left) atomic_fetch_add(&prefix_hash_reads, 1);
#endif
    while (left) {
        size_t count = left < (int64_t)sizeof bytes ? (size_t)left : sizeof bytes;
        ssize_t n = read(disk.fd, bytes, count);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0 || !EVP_DigestUpdate(disk.hash, bytes, (size_t)n)) return -1;
        left -= n;
    }
    disk.hashed = r->offset;
    return fstat(disk.fd, &disk.stamp);
}
static bool disk_unchanged(const struct stat *st) {
    return st->st_size == disk.stamp.st_size &&
           st->st_mtim.tv_sec == disk.stamp.st_mtim.tv_sec &&
           st->st_mtim.tv_nsec == disk.stamp.st_mtim.tv_nsec &&
           st->st_ctim.tv_sec == disk.stamp.st_ctim.tv_sec &&
           st->st_ctim.tv_nsec == disk.stamp.st_ctim.tv_nsec;
}
static int disk_open(Receiver *r) {
    if (disk.fd < 0 || strcmp(disk.id, r->id)) {
        disk_close();
        disk.dir = receiver_dir(&r->pin, r->folder, false);
        if (disk.dir < 0) return -1;
        disk.fd = open_partial(r, disk.dir, false, false);
        if (disk.fd < 0) { disk_close(); return -1; }
        copy_text(disk.id, sizeof disk.id, r->id);
        if (disk_hash_prefix(r)) { disk_close(); return -1; }
    }
    struct stat root, folder, opened_folder, file, path;
    char directory[sizeof r->pin.root + sizeof r->folder + 16];
    if (r->folder[0]) snprintf(directory, sizeof directory, "%s/homebrew/%s", r->pin.root, r->folder);
    else snprintf(directory, sizeof directory, "%s/homebrew", r->pin.root);
    char name[80]; partial(r, name, sizeof name);
    if (lstat(r->pin.root, &root) || !S_ISDIR(root.st_mode) ||
        root.st_dev != r->pin.device || root.st_ino != r->pin.inode ||
        lstat(directory, &folder) || !S_ISDIR(folder.st_mode) ||
        fstat(disk.dir, &opened_folder) || folder.st_dev != opened_folder.st_dev ||
        folder.st_ino != opened_folder.st_ino ||
        fstat(disk.fd, &file) || fstatat(disk.dir, name, &path, AT_SYMLINK_NOFOLLOW) ||
        !S_ISREG(path.st_mode) || path.st_nlink != 1 || file.st_nlink != 1 ||
        (uint64_t)file.st_dev != r->file_device || (uint64_t)file.st_ino != r->file_inode ||
        path.st_dev != file.st_dev || path.st_ino != file.st_ino || file.st_size < r->offset)
        return -1;
    // External edits invalidate the running checksum, including same-size edits.
    if (disk.hashed != r->offset || !disk_unchanged(&file)) return disk_hash_prefix(r);
    return 0;
}
static int disk_digest(Receiver *r, char *out) {
    if (disk_open(r)) return -1;
    EVP_MD_CTX *copy = EVP_MD_CTX_new();
    unsigned char hash[32]; unsigned n = 0;
    bool ok = copy && EVP_MD_CTX_copy_ex(copy, disk.hash) &&
              EVP_DigestFinal_ex(copy, hash, &n) && n == 32;
    EVP_MD_CTX_free(copy);
    if (!ok) return -1;
    digest_text(hash, out);
    return 0;
}
/* Recover a crash after publishing but before the completion journal was saved.
 * Only the exact inode created by this transfer can be recovered. */
static bool recover_published(Receiver *r, int dir) {
    if (r->offset != r->size || !hex(r->hash, 64) || !r->file_inode)
        return false;
    int fd = openat(dir, r->pin.filename, O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return false;
    struct stat st;
    bool owned = !fstat(fd, &st) && S_ISREG(st.st_mode) &&
                 (uint64_t)st.st_dev == r->file_device && (uint64_t)st.st_ino == r->file_inode;
    if (!owned) {
        close(fd);
        return false;
    }
    char hash[65], path[80];
    partial(r, path, sizeof path);
    if (st.st_size != r->size || hash_file(fd, hash) || strcmp(hash, r->hash))
        fail(r, "The published file changed. It was not modified.");
    else {
        struct stat stage;
        int found = fstatat(dir, path, &stage, AT_SYMLINK_NOFOLLOW);
        if (!found && (!S_ISREG(stage.st_mode) || (uint64_t)stage.st_dev != r->file_device ||
                       (uint64_t)stage.st_ino != r->file_inode))
            fail(r, "The partial file changed. It was not modified.");
        else if ((found && errno != ENOENT) || (!found && unlinkat(dir, path, 0)) || fsync(dir))
            fail(r, "Could not confirm the completed file on disk. Retry after checking storage.");
        else
            copy_text(r->state, sizeof r->state, "complete");
    }
    close(fd);
    return true;
}
static void execute(Receiver *r, const char *action, unsigned char *data, size_t n,
                    const char *sha) {
    if (!strcmp(action, "chunk")) {
        if (disk_open(r)) fail(r, "Cannot open the original partial file safely.");
        else if (!digest_bytes(data, n, sha)) fail(r, "Chunk checksum did not match. Retry this chunk.");
        else if (lseek(disk.fd, r->offset, SEEK_SET) < 0 || allwrite(disk.fd, data, n) ||
                 !EVP_DigestUpdate(disk.hash, data, n) || fstat(disk.fd, &disk.stamp))
            fail(r, "Could not write and flush the chunk. Check storage space.");
        else {
            r->offset += (int64_t)n;
            disk.hashed = r->offset;
            copy_text(r->state, sizeof r->state, "ready");
        }
        return;
    }
    if (strcmp(action, "finish")) disk_close();
    if (!strcmp(action, "abandon")) {
        /* Explicit owner confirmation releases only the reservation. No drive
         * access: unavailable or unverified files must remain untouched. */
        r->partials_retained = true;
        copy_text(r->state, sizeof r->state, "cancelled");
        return;
    }
    if (!strcmp(action, "cancel") && !r->file_inode) {
        /* A failed begin never created a file; unavailable storage must not
         * prevent cancelling that empty reservation. */
        r->offset = 0;
        copy_text(r->state, sizeof r->state, "cancelled");
        return;
    }
    if (sha && *sha && (!strcmp(action, "resume") || !strcmp(action, "cancel")) &&
        !rebind_partial(r, sha)) return;
    int dir = receiver_dir(&r->pin, r->folder, !strcmp(action, "begin"));
    if (dir < 0) {
        fail(r, "Reconnect the original destination drive.");
        return;
    }
    char path[80];
    partial(r, path, sizeof path);
    if ((!strcmp(action, "resume") || !strcmp(action, "finish") || !strcmp(action, "cancel")) &&
        recover_published(r, dir)) {
        close(dir);
        return;
    }
    if (!strcmp(action, "cancel")) {
        int fd = open_partial(r, dir, false, true);
        if (fd < 0 && errno != ENOENT)
            fail(r, "The partial file changed; it was not deleted.");
        else {
            if (fd >= 0)
                close(fd);
            if ((unlinkat(dir, path, 0) && errno != ENOENT) || fsync(dir))
                fail(r, "Could not remove the partial file.");
            else {
                r->offset = 0;
                copy_text(r->state, sizeof r->state, "cancelled");
            }
        }
        close(dir);
        return;
    }
    bool create = !strcmp(action, "begin");
    if (create) {
        struct stat st;
        if (!fstatat(dir, r->pin.filename, &st, AT_SYMLINK_NOFOLLOW) || errno != ENOENT) {
            close(dir);
            fail(r, "A destination file already exists.");
            return;
        }
    }
    int fd = open_partial(r, dir, create, false);
    if (fd < 0) {
        close(dir);
        fail(r, "Cannot open the original partial file safely.");
        return;
    }
    if (create) {
        if (fsync(fd) || fsync(dir))
            fail(r, "Cannot save the new partial file.");
        else
            copy_text(r->state, sizeof r->state, "ready");
    } else if (!strcmp(action, "resume")) {
        copy_text(r->state, sizeof r->state, "ready");
    } else if (!strcmp(action, "finish")) {
        char hash[65];
        struct stat st;
        if (fstat(fd, &st) || st.st_size != r->size || disk_digest(r, hash) || strcmp(hash, r->hash))
            fail(r, "Complete-file checksum did not match. The partial file was retained.");
        else if (linkat(dir, path, dir, r->pin.filename, 0)) {
            /* Same exclusive reservation strategy used by Orbit's download writer on exFAT. */
            if (errno != EPERM && errno != EOPNOTSUPP && errno != ENOSYS)
                fail(r, "Cannot publish: the destination name is already in use or inaccessible.");
            else {
                int reserve =
                    openat(dir, r->pin.filename, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
                if (reserve < 0)
                    fail(r, "Destination filename is already in use.");
                else {
                    close(reserve);
                    if (renameat(dir, path, dir, r->pin.filename)) {
                        unlinkat(dir, r->pin.filename, 0);
                        fail(r, "Could not publish the completed file.");
                    } else
                        copy_text(r->state, sizeof r->state, "complete");
                }
            }
        } else {
            unlinkat(dir, path, 0);
            copy_text(r->state, sizeof r->state, "complete");
        }
        if (!strcmp(r->state, "complete") && fsync(dir))
            fail(r, "File published, but directory durability could not be confirmed.");
    }
    close(fd);
    close(dir);
}
static struct timespec checkpoint_deadline(void) {
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 250000000;
    if (deadline.tv_nsec >= 1000000000) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000; }
    return deadline;
}
static bool deadline_passed(struct timespec deadline) {
    struct timespec now; clock_gettime(CLOCK_REALTIME, &now);
    return now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec);
}
static void *worker(void *unused) {
    (void)unused;
    pthread_mutex_lock(&lock);
    for (;;) {
        while (!working && !stopping) pthread_cond_wait(&wake, &lock);
        if (!working && stopping) break;
        Receiver next = current;
        int64_t previous_offset = current.offset;
        char action[16]; copy_text(action, sizeof action, command);
        char proof[65]; copy_text(proof, sizeof proof, command_proof);
        if (!strcmp(action, "chunk")) {
            struct timespec deadline = checkpoint_deadline();
            for (;;) {
                while (!pending_count && !stopping && !deadline_passed(deadline))
                    pthread_cond_timedwait(&wake, &lock, &deadline);
                if (!pending_count) break;
                Chunk block = pending[0];
                pending_count--;
                memmove(pending, pending + 1, pending_count * sizeof *pending);
                writing_chunk = true;
                pthread_mutex_unlock(&lock);
#ifdef ORBIT_TEST
                const char *delay = getenv("ORBIT_TEST_RECEIVE_DELAY_MS");
                if (delay) {
                    long ms = strtol(delay, NULL, 10);
                    if (ms > 0 && ms <= 2000) {
                        struct timespec pause = {ms / 1000, (ms % 1000) * 1000000};
                        nanosleep(&pause, NULL);
                    }
                }
#endif
                next.error[0] = 0;
                execute(&next, action, block.data, block.size, block.hash);
                free(block.data);
                pthread_mutex_lock(&lock);
                writing_chunk = false;
                if (!strcmp(next.state, "error") || next.offset == next.size || stopping ||
                    next.offset - previous_offset >= ORBIT_RECEIVE_CHUNK * ORBIT_RECEIVE_WINDOW ||
                    deadline_passed(deadline)) break;
            }
            pthread_mutex_unlock(&lock);
            if (next.offset > previous_offset && (disk.fd < 0 || fsync(disk.fd))) {
                next.offset = previous_offset;
                fail(&next, "Could not persist transfer progress. Stop and check console storage.");
            }
#ifdef ORBIT_TEST
            atomic_fetch_add(&checkpoint_count, 1);
#endif
        } else {
            pthread_mutex_unlock(&lock);
            next.error[0] = 0;
            if (!strcmp(action, "finish") && save(&next))
                fail(&next, "Could not save verification progress. The partial file was retained.");
            else execute(&next, action, NULL, 0, proof);
        }
        if (save(&next)) {
            next.offset = previous_offset;
            fail(&next, "Could not persist transfer progress. Stop and check console storage.");
        }
        if (!strcmp(next.state, "error") || !strcmp(next.state, "complete") || !strcmp(next.state, "cancelled"))
            disk_close();
        pthread_mutex_lock(&lock);
        current = next;
        /* Count each published file once, even when a lost reply repeats finish. */
        if (!strcmp(current.state, "complete") && bundle_open() && in_bundle(&current) &&
            strcmp(bundle.last, current.id)) {
            bundle.files++;
            bundle.bytes += current.size;
            copy_text(bundle.last, sizeof bundle.last, current.id);
            if (bundle_save()) {
                fail(&current, "Could not save folder progress. Stop and check console storage.");
            }
        }
        if (!strcmp(current.state, "error")) {
            for (size_t i = 0; i < pending_count; i++) free(pending[i].data);
            pending_count = 0;
        }
        working = pending_count > 0;
        if (working) copy_text(current.state, sizeof current.state, "writing");
        else accepted_offset = current.offset;
        busy(active(&current) || bundle_open());
    }
    pthread_mutex_unlock(&lock);
    disk_close();
    return NULL;
}
static bool integer(const cJSON *o, const char *key, int64_t min, int64_t max, int64_t *out) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v) || !(v->valuedouble >= (double)min && v->valuedouble <= (double)max) ||
        v->valuedouble != (double)(int64_t)v->valuedouble)
        return false;
    *out = (int64_t)v->valuedouble;
    return true;
}
int receive_start(void) {
    int fd = openat(orbit.state_fd, "receive.json", O_RDONLY | O_NOFOLLOW);
    if (fd >= 0) {
        char data[4097];
        ssize_t n = read(fd, data, 4096);
        close(fd);
        if (n <= 0 || n >= 4096)
            return -1;
        data[n] = 0;
        cJSON *o = cJSON_Parse(data);
        Receiver r = {0};
        const char *folder = json_text(o, "folder");
        bool valid = hex(json_text(o, "id"), 32) && hex(json_text(o, "owner"), 64) && folder_ok(folder) &&
                     (*folder ? extract_name_ok(json_text(o, "filename")) : name_ok(json_text(o, "filename"))) &&
                     integer(o, "size", *folder ? 0 : 1, INT64_C(8) * 1024 * 1024 * 1024 * 1024, &r.size) &&
                     integer(o, "offset", 0, r.size, &r.offset);
        if (valid) {
            copy_text(r.id, sizeof r.id, json_text(o, "id"));
            copy_text(r.owner, sizeof r.owner, json_text(o, "owner"));
            copy_text(r.folder, sizeof r.folder, folder);
            copy_text(r.pin.filename, sizeof r.pin.filename, json_text(o, "filename"));
            copy_text(r.pin.storage_id, sizeof r.pin.storage_id, json_text(o, "storageId"));
            copy_text(r.pin.root, sizeof r.pin.root, json_text(o, "root"));
            copy_text(r.hash, sizeof r.hash, json_text(o, "sha256"));
            r.pin.device = (dev_t)strtoull(json_text(o, "device"), NULL, 10);
            r.pin.inode = (ino_t)strtoull(json_text(o, "inode"), NULL, 10);
            r.file_device = strtoull(json_text(o, "fileDevice"), NULL, 10);
            r.file_inode = strtoull(json_text(o, "fileInode"), NULL, 10);
            r.partials_retained = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "partialsRetained"));
            const char *s = json_text(o, "state");
            copy_text(r.state, sizeof r.state,
                      !strcmp(s, "complete") || !strcmp(s, "cancelled") ? s : "ready");
            current = r;
        }
        cJSON_Delete(o);
        if (!valid)
            return -1;
    } else if (errno != ENOENT)
        return -1;
    fd = openat(orbit.state_fd, "receive-bundle.json", O_RDONLY | O_NOFOLLOW);
    if (fd >= 0) {
        char data[4097];
        ssize_t n = read(fd, data, 4096);
        close(fd);
        if (n <= 0 || n >= 4096) return -1;
        data[n] = 0;
        cJSON *o = cJSON_Parse(data);
        const char *state = json_text(o, "state"), *last = json_text(o, "last");
        Bundle b = {0};
        bool valid = hex(json_text(o, "id"), 32) && hex(json_text(o, "owner"), 64) &&
                     extract_name_ok(json_text(o, "name")) && strlen(json_text(o, "name")) < sizeof b.name &&
                     (!*last || hex(last, 32)) &&
                     (!strcmp(state, "open") || !strcmp(state, "complete") || !strcmp(state, "cancelled") ||
                      !strcmp(state, "abandoned")) &&
                     integer(o, "total", 0, INT64_C(8) * 1024 * 1024 * 1024 * 1024, &b.total) &&
                     integer(o, "bytes", 0, b.total, &b.bytes) && integer(o, "files", 0, 10000000, &b.files);
        if (valid) {
            copy_text(b.id, sizeof b.id, json_text(o, "id"));
            copy_text(b.owner, sizeof b.owner, json_text(o, "owner"));
            copy_text(b.state, sizeof b.state, state);
            copy_text(b.name, sizeof b.name, json_text(o, "name"));
            copy_text(b.last, sizeof b.last, last);
            copy_text(b.pin.storage_id, sizeof b.pin.storage_id, json_text(o, "storageId"));
            copy_text(b.pin.root, sizeof b.pin.root, json_text(o, "root"));
            b.pin.device = (dev_t)strtoull(json_text(o, "device"), NULL, 10);
            b.pin.inode = (ino_t)strtoull(json_text(o, "inode"), NULL, 10);
            b.folder_device = strtoull(json_text(o, "folderDevice"), NULL, 10);
            b.folder_inode = strtoull(json_text(o, "folderInode"), NULL, 10);
            bundle = b;
        }
        cJSON_Delete(o);
        if (!valid) return -1;
    } else if (errno != ENOENT)
        return -1;
    /* A crash may occur after the file journal is durable but before the
     * bundle count is saved. Reconcile that one last file before accepting
     * another transfer; its completion was already verified and published. */
    if (!strcmp(current.state, "complete") && bundle_open() && in_bundle(&current) &&
        !strcmp(current.owner, bundle.owner) &&
        strcmp(bundle.last, current.id)) {
        if (current.size > bundle.total - bundle.bytes) return -1;
        bundle.files++;
        bundle.bytes += current.size;
        copy_text(bundle.last, sizeof bundle.last, current.id);
        if (bundle_save()) return -1;
    }
    accepted_offset = current.offset;
    busy(active(&current) || bundle_open());
    pthread_attr_t attr;
    if (pthread_attr_init(&attr))
        return -1;
    int rc = pthread_attr_setstacksize(&attr, ORBIT_THREAD_STACK);
    if (!rc)
        rc = pthread_create(&thread, &attr, worker, NULL);
    pthread_attr_destroy(&attr);
    started = !rc;
    return rc;
}
void receive_stop(void) {
    if (!started)
        return;
    pthread_mutex_lock(&lock);
    stopping = true;
    pthread_cond_signal(&wake);
    pthread_mutex_unlock(&lock);
    pthread_join(thread, NULL);
    pthread_mutex_lock(&lock);
    started = false;
    pthread_mutex_unlock(&lock);
}
bool receive_available(void) {
    pthread_mutex_lock(&lock);
    bool available = started && !stopping;
    pthread_mutex_unlock(&lock);
    return available;
}
cJSON *receive_status(const char *owner) {
    pthread_mutex_lock(&lock);
    cJSON *o = json(&current, false);
    if (current.id[0] && strcmp(owner, current.owner)) {
        cJSON_Delete(o);
        o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "state", "other-client");
    } else {
        cJSON_AddNumberToObject(o, "pipelineChunks", ORBIT_RECEIVE_WINDOW);
        cJSON_AddNumberToObject(o, "acceptedOffset", (double)accepted_offset);
        cJSON_AddNumberToObject(o, "bufferedChunks", pending_count + (writing_chunk ? 1 : 0));
        cJSON_AddNumberToObject(o, "partialVerification", 1);
        cJSON_AddNumberToObject(o, "stopTransfer", 1);
        cJSON_AddNumberToObject(o, "folders", 1);
    }
    if (bundle.id[0] && !strcmp(owner, bundle.owner)) {
        cJSON *b = cJSON_AddObjectToObject(o, "bundle");
        cJSON_AddStringToObject(b, "id", bundle.id);
        cJSON_AddStringToObject(b, "state", bundle.state);
        cJSON_AddStringToObject(b, "name", bundle.name);
        cJSON_AddNumberToObject(b, "files", (double)bundle.files);
        cJSON_AddNumberToObject(b, "bytes", (double)bundle.bytes);
    } else if (bundle_open())
        cJSON_AddStringToObject(o, "bundleState", "other-client");
#ifdef ORBIT_TEST
    cJSON_AddNumberToObject(o, "checkpointCount", atomic_load(&checkpoint_count));
    cJSON_AddNumberToObject(o, "prefixHashReads", atomic_load(&prefix_hash_reads));
#endif
    cJSON_AddBoolToObject(o, "available", started);
    pthread_mutex_unlock(&lock);
    return o;
}
int receive_command(const char *action, const cJSON *input, const char *owner, char *error,
                    size_t cap) {
    int code = 202;
    const char *message = NULL, *id = json_text(input, "id");
    pthread_mutex_lock(&lock);
    if (!started || stopping) {
        code = 503;
        message = "The local receiver could not start. Check console storage.";
    } else if (!hex(id, 32)) {
        code = 400;
        message = "Invalid transfer ID.";
    } else if (working) {
        code = 409;
        message = "Wait for the current disk operation.";
    } else if (!strcmp(action, "begin")) {
        int64_t size = 0;
        const char *name = json_text(input, "filename"), *sid = json_text(input, "storageId");
        /* A file of a folder transfer goes to its place inside the bundle's staging folder. */
        const char *group = json_text(input, "bundle"), *directory = json_text(input, "directory");
        char folder[sizeof current.folder] = "";
        bool fits = !*group || snprintf(folder, sizeof folder, *directory ? ".orbit-bundle-%s/%s" : ".orbit-bundle-%s",
                                        group, directory) < (int)sizeof folder;
        if ((*group ? !extract_name_ok(name) || !hex(group, 32) || !fits || !folder_ok(folder) : !name_ok(name) || *directory) ||
            !integer(input, "size", *group ? 0 : 1, INT64_C(8) * 1024 * 1024 * 1024 * 1024, &size)) {
            code = 400;
            message = "Invalid filename or size.";
        } else if (*group && (!bundle_open() || strcmp(bundle.id, group) || strcmp(bundle.owner, owner) ||
                              strcmp(bundle.pin.storage_id, sid))) {
            code = 409;
            message = "Open this folder transfer before sending its files.";
        } else if (!*group && bundle_open()) {
            code = 409;
            message = "Finish or cancel the folder transfer in progress first.";
        } else if (!strcmp(current.id, id)) {
            code = 200;
            if (strcmp(current.owner, owner) || strcmp(current.pin.filename, name) ||
                strcmp(current.pin.storage_id, sid) || current.size != size || strcmp(current.folder, folder)) {
                code = 409;
                message = "Transfer identity does not match.";
            }
        } else if (active(&current)) {
            code = 409;
            message = "Finish or cancel the existing local transfer first.";
        } else {
            Storage choices[ORBIT_MAX_STORAGE], *selected = NULL;
            size_t count = storage_list(choices);
            for (size_t i = 0; i < count; i++)
                if (!strcmp(choices[i].id, sid))
                    selected = &choices[i];
            pthread_mutex_lock(&orbit.mutex);
            bool occupied = orbit.library_storage_busy || orbit.stop;
            for (size_t i = 0; i < orbit.job_count; i++)
                if (job_active(&orbit.jobs[i]))
                    occupied = true;
            if (occupied) {
                code = 409;
                message = "Wait for the active Orbit storage operation or pause the download.";
            } else if (!selected || (*group && (strcmp(selected->root, bundle.pin.root) ||
                                                selected->device != bundle.pin.device ||
                                                selected->inode != bundle.pin.inode))) {
                code = 409;
                message = "Selected storage is unavailable.";
            } else {
                uint64_t pending = storage_pending_locked(selected, NULL);
                if (pending > selected->free_bytes ||
                    (uint64_t)size + 16 * 1024 * 1024 > selected->free_bytes - pending) {
                    code = 409;
                    message = "Not enough free space after queued downloads.";
                }
            }
            if (!message) {
                Receiver r = {0};
                copy_text(r.id, sizeof r.id, id);
                copy_text(r.owner, sizeof r.owner, owner);
                copy_text(r.pin.storage_id, sizeof r.pin.storage_id, sid);
                copy_text(r.pin.root, sizeof r.pin.root, selected->root);
                copy_text(r.pin.filename, sizeof r.pin.filename, name);
                copy_text(r.folder, sizeof r.folder, folder);
                r.pin.device = selected->device;
                r.pin.inode = selected->inode;
                r.size = size;
                copy_text(r.state, sizeof r.state, "preparing");
                current = r;
                accepted_offset = 0;
                orbit.receiver_active = true;
            }
            pthread_mutex_unlock(&orbit.mutex);
        }
    } else if (strcmp(id, current.id) || strcmp(owner, current.owner)) {
        code = 409;
        message = "This is not your active transfer.";
    } else if ((!strcmp(action, "resume") || !strcmp(action, "cancel")) &&
               (cJSON_HasObjectItem(input, "prefixSha256") || cJSON_HasObjectItem(input, "prefixBytes")) &&
               (!hex(json_text(input, "prefixSha256"), 64) || current.offset <= 0 ||
                !cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(input, "prefixBytes")) ||
                cJSON_GetObjectItemCaseSensitive(input, "prefixBytes")->valuedouble != (double)current.offset)) {
        code = 409;
        message = "Read the current durable offset before verifying the partial file.";
    } else if (!strcmp(action, "abandon")) {
        if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(input, "confirmed"))) {
            code = 400;
            message = "Confirm stopping this transfer and keeping its remaining files.";
        } else if (!active(&current))
            code = 200;
        else
            copy_text(current.state, sizeof current.state, "cancelling");
    } else if (!strcmp(action, "resume")) {
        if (!strcmp(current.state, "complete"))
            code = 200;
        else if (!active(&current)) {
            code = 409;
            message = "This transfer has been cancelled.";
        } else
            copy_text(current.state, sizeof current.state, "preparing");
    } else if (!strcmp(action, "finish")) {
        if (!strcmp(current.state, "complete"))
            code = 200;
        else if (current.offset != current.size || !hex(json_text(input, "sha256"), 64)) {
            code = 409;
            message = "The file is incomplete or its checksum is missing.";
        } else {
            copy_text(current.hash, sizeof current.hash, json_text(input, "sha256"));
            copy_text(current.state, sizeof current.state, "verifying");
        }
    } else if (!strcmp(action, "cancel")) {
        if (!active(&current))
            code = 200;
        else
            copy_text(current.state, sizeof current.state, "cancelling");
    } else {
        code = 400;
        message = "Unknown receiver operation.";
    }
    if (code == 202 && !message) {
        copy_text(command, sizeof command, action);
        copy_text(command_proof, sizeof command_proof, json_text(input, "prefixSha256"));
        working = true;
        pthread_cond_signal(&wake);
    }
    if (message)
        copy_text(error, cap, message);
    pthread_mutex_unlock(&lock);
    return code;
}
int receive_chunk(const char *id, const char *offset, const char *sha, unsigned char **data,
                  size_t n, const char *owner, char *error, size_t cap) {
    int code = 202;
    const char *message = NULL;
    char *end = NULL;
    errno = 0;
    long long at = offset ? strtoll(offset, &end, 10) : -1;
    pthread_mutex_lock(&lock);
    if (!started || stopping) {
        code = 503;
        message = "Receiver unavailable.";
    } else if (!id || !offset || !sha || !hex(id, 32) || !hex(sha, 64) || !*offset || *end ||
               errno || at < 0 || !n || n > ORBIT_RECEIVE_CHUNK) {
        code = 400;
        message = "Invalid chunk metadata.";
    } else if (working && strcmp(command, "chunk")) {
        code = 409;
        message = "Wait for the current disk operation.";
    } else if (strcmp(current.id, id) || strcmp(current.owner, owner) || !active(&current)) {
        code = 409;
        message = "This is not your active transfer.";
    } else if (at != accepted_offset || (int64_t)n > current.size - accepted_offset) {
        code = 409;
        message = "Read the accepted and durable offsets before sending another chunk.";
    } else if (pending_count + (writing_chunk ? 1U : 0U) >= ORBIT_RECEIVE_WINDOW) {
        code = 429;
        message = "The receiver buffer is full. Wait for durable progress.";
    } else {
        Chunk *block = &pending[pending_count++];
        block->data = *data;
        *data = NULL;
        block->size = n;
        copy_text(block->hash, sizeof block->hash, sha);
        accepted_offset += (int64_t)n;
        copy_text(command, sizeof command, "chunk");
        copy_text(current.state, sizeof current.state, "writing");
        working = true;
        pthread_cond_signal(&wake);
    }
    if (message)
        copy_text(error, cap, message);
    pthread_mutex_unlock(&lock);
    return code;
}

static int bundle_select(const char *sid, Storage *out, int64_t total, const char **message) {
    Storage choices[ORBIT_MAX_STORAGE];
    size_t count = storage_list(choices);
    bool found = false;
    for (size_t i = 0; i < count; i++)
        if (!strcmp(choices[i].id, sid)) { *out = choices[i]; found = true; }
    pthread_mutex_lock(&orbit.mutex);
    bool occupied = orbit.library_storage_busy || orbit.stop;
    for (size_t i = 0; i < orbit.job_count; i++)
        if (job_active(&orbit.jobs[i])) occupied = true;
    uint64_t pending = found ? storage_pending_locked(out, NULL) : 0;
    pthread_mutex_unlock(&orbit.mutex);
    if (occupied) *message = "Wait for the active Orbit storage operation or pause the download.";
    else if (!found) *message = "Selected storage is unavailable.";
    else if (pending > out->free_bytes || (uint64_t)total + 16 * 1024 * 1024 > out->free_bytes - pending)
        *message = "Not enough free space after queued downloads.";
    return *message ? 409 : 200;
}
static int bundle_start(const char *id, const cJSON *input, const char *owner, const char **message) {
    const char *name = json_text(input, "name"), *sid = json_text(input, "storageId");
    int64_t total = 0;
    if (!extract_name_ok(name) || strlen(name) >= sizeof bundle.name ||
        !integer(input, "totalBytes", 0, INT64_C(8) * 1024 * 1024 * 1024 * 1024, &total)) {
        *message = "Invalid folder name or size.";
        return 400;
    }
    if (!strcmp(bundle.id, id)) { /* Repeating the same identity resumes it. */
        if (strcmp(bundle.owner, owner) || strcmp(bundle.name, name) ||
            strcmp(bundle.pin.storage_id, sid) || bundle.total != total) {
            *message = "Folder transfer identity does not match.";
            return 409;
        }
        if (bundle_open() || !strcmp(bundle.state, "complete")) return 200;
        *message = "This folder transfer has been cancelled.";
        return 409;
    }
    if (bundle_open()) {
        *message = "Finish or cancel the existing folder transfer first.";
        return 409;
    }
    if (active(&current)) {
        *message = "Finish or cancel the existing local transfer first.";
        return 409;
    }
    Storage selected;
    int code = bundle_select(sid, &selected, total, message);
    if (code != 200) return code;
    Bundle next = {0};
    copy_text(next.id, sizeof next.id, id);
    copy_text(next.owner, sizeof next.owner, owner);
    copy_text(next.state, sizeof next.state, "open");
    copy_text(next.name, sizeof next.name, name);
    copy_text(next.pin.storage_id, sizeof next.pin.storage_id, sid);
    copy_text(next.pin.root, sizeof next.pin.root, selected.root);
    next.pin.device = selected.device;
    next.pin.inode = selected.inode;
    next.total = total;
    char folder[64];
    bundle_folder(id, folder, sizeof folder);
    int dir = storage_open(&next.pin);
    struct stat st;
    if (dir < 0) {
        *message = "Selected storage is unavailable.";
        return 409;
    }
    if (!fstatat(dir, name, &st, AT_SYMLINK_NOFOLLOW) || errno != ENOENT)
        *message = "A file or folder with this name already exists on the drive. Orbit will not overwrite it.";
    else if (mkdirat(dir, folder, 0755) || fstatat(dir, folder, &st, AT_SYMLINK_NOFOLLOW) || fsync(dir))
        *message = "Could not create the folder on the drive.";
    if (*message) {
        close(dir);
        return 409;
    }
    next.folder_device = (uint64_t)st.st_dev;
    next.folder_inode = (uint64_t)st.st_ino;
    Bundle previous = bundle;
    bundle = next;
    if (bundle_save()) {
        bundle = previous;
        unlinkat(dir, folder, AT_REMOVEDIR);
        close(dir);
        *message = "Could not save the folder transfer. Check console storage.";
        return 503;
    }
    close(dir);
    return 200;
}
static int bundle_commit(const cJSON *input, const char **message) {
    int64_t files = 0, bytes = 0;
    if (!strcmp(bundle.state, "complete")) return 200;
    if (!bundle_open()) {
        *message = "This folder transfer has been cancelled.";
        return 409;
    }
    if (active(&current)) {
        *message = "Finish sending the current file first.";
        return 409;
    }
    if (!integer(input, "files", 0, 10000000, &files) ||
        !integer(input, "totalBytes", 0, INT64_C(8) * 1024 * 1024 * 1024 * 1024, &bytes)) {
        *message = "Expected the folder's file count and size.";
        return 400;
    }
    if (files != bundle.files || bytes != bundle.bytes || bytes != bundle.total) {
        *message = "The PS5 has not received every file in this folder. Resume to send the rest.";
        return 409;
    }
    int dir = storage_open(&bundle.pin);
    if (dir < 0) {
        *message = "Reconnect the original destination drive.";
        return 409;
    }
    char folder[64];
    bundle_folder(bundle.id, folder, sizeof folder);
    struct stat staged, published;
    bool moved = fstatat(dir, folder, &staged, AT_SYMLINK_NOFOLLOW) && errno == ENOENT &&
                 !fstatat(dir, bundle.name, &published, AT_SYMLINK_NOFOLLOW) && S_ISDIR(published.st_mode) &&
                 (uint64_t)published.st_dev == bundle.folder_device &&
                 (uint64_t)published.st_ino == bundle.folder_inode;
    /* A folder can only replace an empty folder, so check, then rename. */
    if (!moved && (fstatat(dir, folder, &staged, AT_SYMLINK_NOFOLLOW) || !S_ISDIR(staged.st_mode) ||
                  (uint64_t)staged.st_dev != bundle.folder_device || (uint64_t)staged.st_ino != bundle.folder_inode))
        *message = "The partial folder changed. It was kept for you to check.";
    else if (!moved && (!fstatat(dir, bundle.name, &published, AT_SYMLINK_NOFOLLOW) || errno != ENOENT))
        *message = "A file or folder with this name already exists on the drive. Orbit will not overwrite it.";
    else if (!moved && (renameat(dir, folder, dir, bundle.name) || fsync(dir)))
        *message = "Could not move the folder into place.";
    close(dir);
    if (*message) return 409;
    copy_text(bundle.state, sizeof bundle.state, "complete");
    if (bundle_save()) {
        *message = "The folder is in place, but its status could not be saved. Commit again.";
        copy_text(bundle.state, sizeof bundle.state, "open");
        return 503;
    }
    return 200;
}
static int bundle_cancel(const char **message) {
    if (!strcmp(bundle.state, "cancelled") || !strcmp(bundle.state, "abandoned")) return 200;
    if (!strcmp(bundle.state, "complete")) {
        *message = "This folder is already on the PS5.";
        return 409;
    }
    if (active(&current) && in_bundle(&current)) {
        *message = "Cancel the file being sent first.";
        return 409;
    }
    int dir = storage_open(&bundle.pin);
    if (dir < 0) {
        *message = "Reconnect the original destination drive before removing the partial folder.";
        return 409;
    }
    char folder[64], path[sizeof bundle.pin.root + 96];
    bundle_folder(bundle.id, folder, sizeof folder);
    snprintf(path, sizeof path, "%s/homebrew/%s", bundle.pin.root, folder);
    struct stat staged;
    int status = fstatat(dir, folder, &staged, AT_SYMLINK_NOFOLLOW);
    if ((!status && (!S_ISDIR(staged.st_mode) || (uint64_t)staged.st_dev != bundle.folder_device ||
                     (uint64_t)staged.st_ino != bundle.folder_inode)) || (status && errno != ENOENT)) {
        close(dir);
        *message = "The partial folder changed. It was kept for you to check.";
        return 409;
    }
    int removed = extract_remove_tree(dir, folder, path);
    if (!removed) fsync(dir);
    close(dir);
    if (removed) {
        *message = "Could not remove the partial folder.";
        return 409;
    }
    copy_text(bundle.state, sizeof bundle.state, "cancelled");
    if (bundle_save()) {
        *message = "Could not save the folder transfer. Check console storage.";
        return 503;
    }
    return 200;
}
static int bundle_mkdir(const cJSON *input, const char **message) {
    if (!strcmp(bundle.state, "complete")) return 200;
    const char *directory = json_text(input, "directory");
    char folder[sizeof current.folder];
    if (!bundle_open() || !*directory ||
        snprintf(folder, sizeof folder, ".orbit-bundle-%s/%s", bundle.id, directory) >= (int)sizeof folder ||
        !folder_ok(folder)) {
        *message = "Invalid folder name or size.";
        return 400;
    }
    int fd = receiver_dir(&bundle.pin, folder, true);
    if (fd < 0) {
        *message = "Could not create the folder on the drive.";
        return 409;
    }
    int rc = fsync(fd);
    close(fd);
    if (rc) { *message = "Could not save the folder transfer. Check console storage."; return 503; }
    return 200;
}
int receive_bundle(const char *action, const cJSON *input, const char *owner, char *error, size_t cap) {
    int code = 400;
    const char *message = NULL, *id = json_text(input, "bundle");
    pthread_mutex_lock(&lock);
    if (!started || stopping) {
        code = 503;
        message = "The local receiver could not start. Check console storage.";
    } else if (!hex(id, 32))
        message = "Invalid folder transfer ID.";
    else if (working) {
        code = 409;
        message = "Wait for the current disk operation.";
    } else if (!strcmp(action, "open"))
        code = bundle_start(id, input, owner, &message);
    else if (strcmp(bundle.id, id) || strcmp(bundle.owner, owner)) {
        code = 409;
        message = "This is not your folder transfer.";
    } else if (!strcmp(action, "commit"))
        code = bundle_commit(input, &message);
    else if (!strcmp(action, "mkdir"))
        code = bundle_mkdir(input, &message);
    else if (!strcmp(action, "cancel"))
        code = bundle_cancel(&message);
    else if (!strcmp(action, "abandon")) {
        /* Releases the reservation only; nothing on the drive is touched. */
        if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(input, "confirmed")))
            message = "Confirm stopping this transfer and keeping its remaining files.";
        else if (bundle_open()) {
            copy_text(bundle.state, sizeof bundle.state, "abandoned");
            code = bundle_save() ? 503 : 200;
            if (code != 200) {
                copy_text(bundle.state, sizeof bundle.state, "open");
                message = "Could not save the folder transfer. Check console storage.";
            }
        } else
            code = 200;
    } else
        message = "Unknown folder operation.";
    if (message) copy_text(error, cap, message);
    busy(active(&current) || bundle_open());
    pthread_mutex_unlock(&lock);
    return message ? code : 200;
}
