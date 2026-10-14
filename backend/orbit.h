#ifndef ORBIT_H
#define ORBIT_H
#ifdef ORBIT_DESKTOP
#define _POSIX_C_SOURCE 200809L
#endif
#include "cJSON.h"
#include "process_identity.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <sys/types.h>
#define ORBIT_MAX_JOBS 100
#define ORBIT_MAX_DOWNLOADS 8
#define ORBIT_DEFAULT_DOWNLOADS 1
#define ORBIT_MAX_STORAGE 17
#define ORBIT_MAX_RELEASES 4096
#define ORBIT_THREAD_STACK (1024 * 1024)
#if defined(ORBIT_BENCHMARK)
#define ORBIT_MAX_RANGES 16
#else
#define ORBIT_MAX_RANGES 8
#endif
#define ORBIT_VERSION "1.2.0"
#define ORBIT_SOURCE_NOTICE_VERSION 1
#define ORBIT_SOURCE_ARCHIVE 1U
#define ORBIT_SOURCE_VIKINGFILE 2U
#define ORBIT_SOURCE_AKIRABOX 4U
#define ORBIT_SOURCE_DATANODES 8U
#define ORBIT_SOURCE_NAS 16U
#define ORBIT_SOURCE_ALL (ORBIT_SOURCE_ARCHIVE | ORBIT_SOURCE_VIKINGFILE | ORBIT_SOURCE_AKIRABOX | ORBIT_SOURCE_DATANODES | ORBIT_SOURCE_NAS)
#define ORBIT_NAS_MAX 512
#define ORBIT_NAS_URL_MAX 1024
typedef struct {
    unsigned enabled, notice_version;
    time_t acknowledged_at;
    char nas_url[ORBIT_NAS_URL_MAX]; /* HTTPS address of the person's own game index; empty when unset. */
} SourceSettings;
typedef struct {
    char id[24], title[128], title_id[16], format[12], filename[160], url[2048], sha256[65];
    char source[16], game_id[64], platform[8];
    char browser_url[128];
    char compression[8]; /* "zip", "7z" or "rar"; empty for a plain file. */
    char archive_password[129]; /* Public source password, optional. */
    int64_t size, extracted_size;
    bool archive_unverified; /* Identity checked; contents inspected after download. */
} Release;
typedef struct {
    char id[24], label[64], root[512];
    uint64_t free_bytes, total_bytes;
    dev_t device;
    ino_t inode;
    bool external;
} Storage;
typedef struct {
    Release release; /* Immutable download identity, independent of catalogue refreshes. */
    char id[24], release_id[24], storage_id[24], root[512], filename[256];
    char status[24], error[256], etag[256], modified[128], verification[24];
    char archive_password[129]; /* User override: saved privately, never API/diagnostics. */
    char delivery[16], delivery_phase[64], debrid_account[65];
    int64_t debrid_id, debrid_file;
    bool debrid_submitted;
    int64_t received, total;
    /* Equal partitions; persisted cursors advance only after the writer fsyncs. */
    int64_t range_received[ORBIT_MAX_RANGES];
    unsigned range_count;
    bool segmented;
    unsigned transfer_connections; /* Runtime network limit, distinct from durable range layout. */
    struct {
        uint64_t peak_buffered, write_calls, sync_calls;
        double buffer_wait_seconds, write_seconds, sync_seconds;
    } transfer_stats;
    double speed;
    dev_t device;
    ino_t inode;
    unsigned attempts;
    unsigned order;
    time_t retry_at;
    atomic_bool pause, cancel;
    bool remove;
    /* Compressed options: the verified archive stays `<filename>.part` until extracted. */
    char output[160]; /* Published file or folder name. */
    int64_t extracted, inspected_size;
    char inspected_format[12]; /* Local result, never changes the release identity. */
    bool archive_ready, archive_kept;
} Job;
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t workers[ORBIT_MAX_DOWNLOADS];
    unsigned worker_count, concurrent_downloads;
    Release releases[ORBIT_MAX_RELEASES + 1]; /* Includes the connection diagnostic. */
    size_t release_count;
    cJSON *catalog;
    /* The person's own NAS files. Kept apart from the signed catalogue so its revision checks are untouched. */
    Release nas_releases[ORBIT_NAS_MAX];
    size_t nas_count;
    cJSON *nas_catalog;
    unsigned catalog_revision;
    SourceSettings sources;
    Job jobs[ORBIT_MAX_JOBS];
    size_t job_count;
    char favorites[ORBIT_MAX_RELEASES][64];
    size_t favorite_count;
    char state_dir[512], desktop_storage[512], pair_code[7], tokens[16][65];
    char instance_id[33]; /* New per process; lets clients verify a service restart. */
    char preferred_storage[24];
    char appearance[24]; /* TV app background preset id (lowercase letters, digits, dashes). */
    char launcher_status[24];
    char launcher_error[192];
    char launcher_registration_method[24];
    uint32_t firmware;
    OrbitProcessIdentity process_identity;
    char system_root[512]; /* Prefix for console paths; desktop tests use a disposable tree. */
    size_t token_count;
    unsigned pair_failures;
    time_t pair_locked_until;
    time_t pair_notify_after; /* Monotonic seconds; notification requests share one cooldown. */
    int port;
    bool desktop, listen_all, state_failed, autoboot;
    bool delete_archives; /* Remove a compressed download once its files are in place. */
    bool queue_backup_created;
    atomic_bool stop;
    bool receiver_active; /* Local file receiver owns destination I/O. */
    bool library_storage_busy; /* Serializes Orbit transfers with ShadowMount storage jobs. */
    int state_fd;
    time_t host_retry_at;
    time_t debrid_next_create; /* Persisted TorBox submission pacing, including rejected requests. */
} Orbit;
extern Orbit orbit;
static inline int64_t range_boundary(int64_t size, unsigned count, unsigned index) {
    /* Avoid multiplying a potentially large file size before division. */
    return (size / count) * index + ((size % count) * index) / count;
}
void copy_text(char *to, size_t cap, const char *s);
const char *json_text(const cJSON *o, const char *key);
int64_t json_int(const cJSON *o, const char *key);
void random_hex(char *out, size_t bytes);
int state_save_locked(void);
int state_load(void);
Release *release_find(const char *id);
int release_parse(const cJSON *entry, Release *release, bool diagnostic);
cJSON *release_json(const Release *release);
int catalog_init(void);
void catalog_load_cache(void);
int catalog_start(void);
void catalog_stop(void);
cJSON *catalog_status(void);
int catalog_refresh(char *error, size_t cap);
bool provider_supported(const Release *release);
bool provider_url_supported(const Release *release);
bool provider_browser_supported(const Release *release);
bool provider_capture_url(const Release *release, const char *candidate);
bool provider_browser_source(const char *source);
bool provider_nas_source(const char *source);
/* NAS source (nas.c). nas_url_allowed accepts only the origin of the configured index. */
bool nas_url_valid(const char *url);
bool nas_url_allowed(const char *url);
void nas_origin_set(const char *index_url);
void nas_changed_locked(void);
void nas_status_locked(cJSON *out);
size_t nas_count_locked(void);
int nas_start(void);
void nas_stop(void);
int browser_start(void);
void browser_stop(void);
void browser_source_changed_locked(void);
cJSON *browser_status(void);
int browser_action(const cJSON *input, char *error, size_t cap);
void browser_job_changed_locked(const char *id);
int transfer_validate_capture(Release *release, atomic_bool *cancel, char *etag,
                              char *modified, char *error, size_t cap);
unsigned source_flag(const char *id);
bool source_enabled_locked(const Release *release);
cJSON *sources_json_locked(void);
int sources_set_locked(const cJSON *input, char *error, size_t cap);
Job *job_find(const char *id);
size_t storage_list(Storage *out);
cJSON *storage_diagnostics(void);
int storage_prefer(const char *id, char *error, size_t cap);
bool storage_matches(const Job *job);
int storage_open(const Job *job);
int storage_check_writable(int directory);
void *download_worker(void *unused);
cJSON *jobs_json_locked(void);
int job_create_locked(const char *release_id, const char *storage_id, char *error, size_t cap,
                      Job **result);
int job_create_delivery_locked(const char *release_id, const char *storage_id,
                                const char *delivery, char *error, size_t cap, Job **result);
int job_check_capture_locked(const Release *release, const Storage *destination, char *error, size_t cap);
bool job_can_refresh_link(const Job *job);
int job_refresh_link_locked(Job *job, const Release *release, const Storage *destination,
                            const char *etag, const char *modified, char *error, size_t cap);
int job_create_captured_locked(const Release *release, const Storage *destination,
                               const char *etag, const char *modified, char *error,
                               size_t cap, Job **result);
int job_action_locked(Job *job, const char *action, bool remove, char *error, size_t cap);
int job_password_locked(Job *job, const char *password, char *error, size_t cap);
int job_delete_partial_locked(Job *job, char *error, size_t cap);
bool job_waiting(const Job *job);
bool job_paths_overlap(const Job *a, const Job *b);
bool job_active(const Job *job);
uint64_t job_space_needed(const Release *release);
uint64_t storage_pending_locked(const Storage *storage, const Job *exclude);
int history_clear_locked(const cJSON *input, char *error, size_t cap);
cJSON *favorites_json_locked(void);
int favorite_set_locked(const cJSON *input, char *error, size_t cap);
int read_regular_file(const char *path, size_t max, unsigned char **data, size_t *length);
int autoboot_install(const unsigned char *image, size_t length, const char *version);
void autoboot_saved_version(char *version, size_t cap);
/* -1, 0 or 1 for Orbit release versions; -2 when either is not one. */
int orbit_version_compare(const char *a, const char *b);
int updates_start(void);
void updates_stop(void);
cJSON *updates_status(void);
int updates_action(const cJSON *input, char *error, size_t cap);
void orbit_request_stop(void);
bool orbit_is_stopping(void);
int autoboot_sync(const unsigned char *image, size_t length);
cJSON *autoboot_status(void);
int autoboot_set(const char *manager, bool enabled, char *error, size_t cap);
int integration_set(const char *manager, bool enabled, char *error, size_t cap);
int api_start(void);
void api_stop(void);
int library_start(void);
void library_stop(void);
cJSON *library_snapshot(bool refresh);
cJSON *library_storage_snapshot(bool refresh);
int library_action(const cJSON *input, char *error, size_t cap);
int pairing_notify(void);
int tvapp_start(void);
void tvapp_stop(void);
cJSON *tvapp_status(void);
int tvapp_action(const cJSON *input, char *error, size_t cap);
int art_start(void);
void art_stop(void);
int art_get(const char *game_id, const char *kind, unsigned char **data, size_t *size, char *mime,
            size_t mime_cap, char *why, size_t why_cap);
/* Bounded, session-only diagnostics. Call event while holding orbit.mutex. */
void diagnostics_event_locked(const char *phase, const Job *job, long http_status, int curl_code);
void diagnostics_begin(void);
void diagnostics_stage(const char *phase, const char *status, int result);
cJSON *diagnostics_snapshot(void);
#endif
