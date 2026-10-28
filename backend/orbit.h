#ifndef ORBIT_H
#define ORBIT_H
#ifdef ORBIT_DESKTOP
#define _POSIX_C_SOURCE 200809L
#endif
#include "cJSON.h"
#include "download_policy.h"
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
#define ORBIT_MAX_RELEASES 8192
#define ORBIT_THREAD_STACK (1024 * 1024)
/* Connections per download: the setting's range (1–64), and its default. */
#define ORBIT_MAX_RANGES 64
#define ORBIT_DEFAULT_CONNECTIONS 8
#define ORBIT_VERSION "1.7.0"
#define ORBIT_SOURCE_NOTICE_VERSION 1
#define ORBIT_SOURCE_ARCHIVE 1U
#define ORBIT_SOURCE_VIKINGFILE 2U
#define ORBIT_SOURCE_AKIRABOX 4U
#define ORBIT_SOURCE_DATANODES 8U
#define ORBIT_SOURCE_ONEFICHIER 16U
#define ORBIT_SOURCE_ALL (ORBIT_SOURCE_ARCHIVE | ORBIT_SOURCE_VIKINGFILE | ORBIT_SOURCE_AKIRABOX | ORBIT_SOURCE_DATANODES | ORBIT_SOURCE_ONEFICHIER)
typedef struct {
    unsigned enabled, notice_version;
    time_t acknowledged_at;
} SourceSettings;
typedef struct {
    char id[24], title[128], title_id[16], format[12], filename[160], url[2048], sha256[65];
    char source[16], game_id[64], platform[8];
    char addon_id[64], base_game_id[64], base_title[128], addon_type[24], content_id[64];
    char requirements[2048]; /* JSON snapshot of the selected DLC edition's requirements. */
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
    bool resume_after_restart; /* Paused by a restart Orbit started itself; queued again after. */
} Job;
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t workers[ORBIT_MAX_DOWNLOADS];
    unsigned worker_count, concurrent_downloads;
    DownloadPolicy download_policy;
    Release *releases; /* Fixed backing array, including the connection diagnostic. */
    size_t release_count;
    cJSON *catalog, *addons; /* Base-game and add-on APIs remain separate. */
    unsigned catalog_revision;
    SourceSettings sources;
    Job jobs[ORBIT_MAX_JOBS];
    size_t job_count;
    char favorites[ORBIT_MAX_RELEASES][64];
    size_t favorite_count;
    cJSON *saved_filters; /* Browse's named filters (saved_filters.c); NULL for none */
    char state_dir[512], desktop_storage[512], pair_code[7], tokens[16][65];
    char instance_id[33]; /* New per process; lets clients verify a service restart. */
    char preferred_storage[24];
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
    bool download_notifications; /* Show notifications on this PS5 ("downloadNotifications" in state.json). */
    unsigned notify_events; /* NOTIFY_* events that reach the PS5 and ntfy. */
    char notified_update[64]; /* The Orbit release last announced as available. */
    bool delete_packages; /* Remove a downloaded PKG once the PS5 reports it installed. */
    unsigned download_connections; /* Ranges a new download is split into, 1–ORBIT_MAX_RANGES. */
    bool queue_backup_created;
    atomic_bool stop;
    bool receiver_active; /* Local file receiver owns destination I/O. */
    bool library_storage_busy; /* Serializes Orbit transfers with ShadowMount storage jobs. */
    bool files_storage_busy; /* Files mutations reserve drives against new downloads/transfers. */
    int state_fd;
    time_t source_retry_at[6]; /* Provider throttling must not block unrelated sources. */
    time_t debrid_next_create; /* Persisted TorBox submission pacing, including rejected requests. */
} Orbit;
extern Orbit orbit;
static inline int64_t range_boundary(int64_t size, unsigned count, unsigned index) {
    /* Avoid multiplying a potentially large file size before division. */
    return (size / count) * index + ((size % count) * index) / count;
}
void copy_text(char *to, size_t cap, const char *s);
/* Valid UTF-8 without control characters. */
bool utf8_text(const char *s);
const char *json_text(const cJSON *o, const char *key);
int64_t json_int(const cJSON *o, const char *key);
void random_hex(char *out, size_t bytes);
int state_save_locked(void);
int state_load(void);
Release *release_find(const char *id);
int release_parse(const cJSON *entry, Release *release, bool diagnostic);
int release_context_parse(const cJSON *x, Release *r);
cJSON *release_context_json(const Release *r);
cJSON *release_json(const Release *release);
void release_download_title(const Release *release, char *out, size_t cap);
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
int storage_open_path(const Job *job, const char *path);
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
/* Browse's saved filters, shared by every app (docs/api.md#saved-filters). */
cJSON *saved_filters_json_locked(void);
void saved_filters_load_locked(const cJSON *saved);
int saved_filters_change_locked(const cJSON *input, char *error, size_t cap);
int read_regular_file(const char *path, size_t max, unsigned char **data, size_t *length);
int autoboot_install(const unsigned char *image, size_t length, const char *version);
void autoboot_saved_version(char *version, size_t cap);
/* The copy Orbit starts from, read whole; 0 when it's a readable ELF. */
int autoboot_saved_image(unsigned char **data, size_t *length);
/* -1, 0 or 1 for Orbit release versions; -2 when either is not one. */
int orbit_version_compare(const char *a, const char *b);
int updates_start(void);
void updates_stop(void);
/* Re-reads whether update notifications want background checks. Call without orbit.mutex. */
void updates_wake(void);
cJSON *updates_status(void);
int updates_action(const cJSON *input, char *error, size_t cap);
void orbit_request_stop(void);
/* After stopping, send the saved copy to the ELF loader already connected on this socket. */
void orbit_restart_after_stop(int loader);
int updates_restart_send(int loader, const unsigned char *fallback, size_t fallback_size);
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
/* Ask ShadowMount to scan soon: Files put a game image or folder in homebrew. */
void library_auto_scan(void);
int pairing_notify(void);
int notify_console(const char *text);
/* Events the owner can be notified about, on this PS5 and through ntfy. */
enum {
    NOTIFY_DOWNLOAD_FINISHED = 1u << 0,
    NOTIFY_DOWNLOAD_FAILED = 1u << 1,
    NOTIFY_INSTALL_FINISHED = 1u << 2,
    NOTIFY_INSTALL_FAILED = 1u << 3,
    NOTIFY_UPDATE_AVAILABLE = 1u << 4,
    NOTIFY_EVENTS_ALL = (1u << 5) - 1,
};
/* Each event's settings name ("downloadFinished"…), or NULL past the last; index i is 1u << i. */
const char *notify_event_name(unsigned index);
/* Whether an event reaches anyone: chosen, with the PS5 or ntfy on. The _locked form needs orbit.mutex. */
bool notify_wanted(unsigned event);
bool notify_wanted_locked(unsigned event);
/* Sends "Download finished/failed: <title>" in the console's language. Call without orbit.mutex. */
void notify_download(const char *title, bool finished);
/* "Installed: <title>" (with what happened to the package) or "Installation failed: <title>".
 * cleanup: 0 the package stays by choice, 1 it was deleted, 2 deleting it was skipped or failed. */
void notify_install(const char *title, bool installed, int cleanup);
/* "Orbit <version> is available", once per version. Call without orbit.mutex. */
void notify_update(const char *version);
const char *notify_ntfy_test(void); /* the test notification's text, in the console's language */
void notify_restart_failed(void);
int tvapp_start(void);
void tvapp_stop(void);
cJSON *tvapp_status(void);
int tvapp_action(const cJSON *input, char *error, size_t cap);
int tvapp_wait(char *error, size_t cap);
int art_start(void);
void art_stop(void);
int art_get(const char *game_id, const char *kind, unsigned char **data, size_t *size, char *mime,
            size_t mime_cap);
/* Bounded, session-only diagnostics. Call event while holding orbit.mutex. */
void diagnostics_event_locked(const char *phase, const Job *job, long http_status, int curl_code);
void diagnostics_begin(void);
void diagnostics_stage(const char *phase, const char *status, int result);
cJSON *diagnostics_snapshot(void);
/* Saves the report on a drive; 0 with {path, storageId, label} in *result, or an HTTP status. */
int diagnostics_export(const cJSON *input, cJSON **result, char *error, size_t cap);
#endif
