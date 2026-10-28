// Orbit Store TV app - Typed calls to the Orbit backend's local API v1.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every call blocks on the transport; run them on a worker thread. The app
// runs on the console itself, so it takes the backend's loopback-only local
// session instead of a pairing code (GET /api/v1/session).

#pragma once

#include "orbit/http.hpp"
#include "orbit/model.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace orbit::api
{

template <typename T> struct Result
{
    T value{};
    int status = 0;    // HTTP status, 0 when the backend was not reached
    std::string error; // empty on success: the backend's message, or a transport reason
    int retry_after = -1;

    bool ok() const
    {
        return error.empty();
    }
    // Orbit itself is not answering (stopped, not started, port taken).
    bool unreachable() const
    {
        return status == 0 && !error.empty();
    }
};

enum class ArtKind : std::uint8_t
{
    cover,
    hero,
    thumbnail, // the hero endpoint, decoded and cached at tile size
};

struct Art
{
    bool pending = false; // the backend is still fetching it; ask again shortly
    std::string bytes;    // the encoded image (WebP, PNG or JPEG)
    std::string content_type;
};

class Client
{
  public:
    explicit Client(http::Transport &transport, http::Transport *companion = nullptr) : transport_(transport), companion_(companion)
    {
    }

    void set_token(std::string token)
    {
        token_ = std::move(token);
    }
    const std::string &token() const
    {
        return token_;
    }

    Result<System> system();
    Result<bool> set_zero(bool enabled);
    Result<Sources> sources();
    Result<Debrid> debrid();
    Result<BrowserSession> browser_session();
    Result<BrowserSession> start_browser(std::string_view release_id, std::string_view storage_id,
                                         std::string_view client_id = {}, bool account = false, bool check_account = false);
    Result<BrowserSession> browser_opened(std::string_view session_id, std::string_view client_id);
    Result<BrowserSession> cancel_browser(std::string_view session_id, std::string_view client_id = {},
                                        std::string_view failure = {}, int error_code = 0);
    Result<std::vector<Release>> catalog();
    Result<std::vector<Release>> addons();
    // The console's own session: its token, the pairing code and the console's
    // address (loopback callers only). Orbit saves its state on each call, so
    // ask only when these are needed.
    Result<Session> session();
    Result<std::vector<Drive>> storage();
    Result<std::vector<Job>> downloads();
    // Queues one option on one drive; the value is the new job ID.
    Result<std::string> create_download(std::string_view release_id, std::string_view storage_id,
                                        bool torbox = false);
    // pause, resume, retry, cancel or remove; the value is the updated queue.
    Result<std::vector<Job>> job_action(std::string_view job_id, std::string_view action,
                                        bool delete_partial = false, std::string_view password = {});
    // Removes finished ("complete") or cancelled entries from history at once; the reply is the queue.
    Result<std::vector<Job>> clear_history(std::string_view status);
    // Catalogue artwork through the backend (GET /api/v1/art/<gameId>/<kind>).
    Result<Art> art(std::string_view game_id, ArtKind kind);
    // Favourites: the saved game IDs, shared with every paired client.
    Result<std::vector<std::string>> favourites();
    Result<std::vector<std::string>> set_favourite(std::string_view game_id, bool favourite);
    // Browse's saved filters; a change answers with the whole list.
    Result<std::vector<SavedFilter>> saved_filters();
    Result<std::vector<SavedFilter>> change_saved_filters(std::string body);
    // Library: refresh asks for a read sooner (POST {}), subject to the shared cooldown.
    Result<LibrarySnapshot> library(bool refresh);
    Result<LibraryStorage> library_storage(bool refresh);
    // One explicit Library action (docs/library.md). request_id makes it
    // idempotent: the same request is never carried out twice.
    struct LibraryRequest
    {
        std::string action; // scan, mount, unmount, measure, copy, move, cancel
        std::string title_id;
        std::string source_key;
        std::string destination_id;
        int job_id = 0;
        bool confirmed = false;
        std::string request_id;
    };
    Result<LibrarySnapshot> library_action(const LibraryRequest &request);
    // Files: a folder of a drive, the running operation, and one action on an
    // entry (docs/api.md#files). Empty fields are left out.
    Result<FileListing> files_list(const std::string &storage_id, const std::string &path);
    struct FileRequest
    {
        std::string action; // folder, rename, delete, copy, move, unpack, install, measure, pause, resume, cancel, password
        std::string storage_id, path, name;
        std::string to_storage_id, to_path;
        std::string id;       // the operation, for pause, resume, cancel and password
        std::string password; // never logged
    };
    // The operation after the request, or none.
    Result<FileOperation> files_status();
    Result<FileOperation> files_action(const FileRequest &request);

    // ---- settings ----
    // Saves the source choice with the download notice accepted.
    Result<Sources> save_sources(const std::vector<std::string> &enabled, int notice_version);
    // The drive new downloads start with; empty lets the app choose. The value
    // is the saved choice. A 404 means this Orbit is too old to save one.
    Result<std::string> set_preferred_storage(std::string_view storage_id);
    Result<bool> set_delete_archives(bool enabled);
    // Saves the service's diagnostic report on a drive (an external one first); the path it wrote.
    Result<std::string> save_diagnostics(const std::string &version, const std::string &language,
                                         const std::string &interface);
    Result<ArchiveAccount> archive_account();
    // The email and password go to the console once; it keeps only
    // Archive.org's session.
    Result<ArchiveAccount> archive_sign_in(const std::string &email, const std::string &password);
    Result<ArchiveAccount> archive_sign_out();
    Result<int> set_download_limit(int limit);
    Result<int> set_download_connections(int connections);
    Result<int> set_download_recovery(int field, int value);
    Result<CatalogueStatus> catalogue_status();
    Result<CatalogueStatus> refresh_catalogue();
    // Updates: Orbit's own (/updates) and this app's (/tv-app).
    struct UpdateRequest
    {
        std::string action; // check, install; stop (Orbit only)
        std::string version;
        std::string checksum;
        bool automatic = false; // a check on opening, sharing the console's cooldown
        bool confirmed = false; // stop
        bool replace = false;   // install over a copy the owner placed (TV app only)
    };
    Result<ServiceUpdate> service_update();
    Result<ServiceUpdate> service_action(const UpdateRequest &request);
    // Installs the releases the owner was shown: this app first, then Orbit, which restarts.
    Result<ServiceUpdate> update_all(const ServiceUpdate &service, const TvAppStatus &tv);
    Result<bool> set_download_notifications(bool enabled);
    // The diagnostics shown on screen (the saved report has the same).
    Result<DiagnosticsReport> diagnostics();
    // Chooses one of kNotificationEvents; the reply holds all of them.
    Result<std::array<bool, 5>> set_notification_event(int index, bool on);
    Result<bool> set_delete_packages(bool enabled);
    Result<NtfySettings> ntfy();
    // The body is the whole settings object, password included; it is wiped after sending.
    Result<NtfySettings> save_ntfy(std::string body);
    Result<bool> test_ntfy();
    Result<TvAppStatus> tv_app();
    Result<TvAppStatus> tv_app_action(const UpdateRequest &request);

  private:
    void refresh_zero();
    http::Response zero_send(std::string path, const char *method = "GET", std::string body = {}, std::size_t limit = 0);
    bool zero_enabled_ = false;
    std::string zero_address_, zero_token_;
    int zero_port_ = 0;
    std::vector<Job> zero_jobs_;
    Result<BrowserSession> browser_request(std::string body = {});
    http::Response send(const char *method, std::string path, std::string body = {},
                        std::size_t max_bytes = 0);
    template <typename T> bool fail(const http::Response &response, Result<T> *result);

    http::Transport &transport_;
    http::Transport *companion_; // optional injected transport for offline protocol tests
    std::string token_;
};

// Characters allowed in IDs placed into request paths.
bool safe_path_segment(std::string_view value);

} // namespace orbit::api
