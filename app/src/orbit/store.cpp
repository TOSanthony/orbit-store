// Orbit Store TV app - The app's view of Orbit, kept in sync on a worker thread.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/store.hpp"
#include "orbit/i18n.hpp"

#include <cstdio>
#include <cstdlib>
#include <utility>
#include <algorithm>

namespace orbit
{

namespace
{

// How long Orbit may take to answer after the loader has it: it loads its
// catalogue and saves its runtime copy before it serves.
constexpr double kStartWait = 30.0;
// Restarting: Orbit closes its port first, then stops its workers and
// releases its instance lock; a copy started before that exits at once. So
// the app waits a little after the port closes (restart_grace_), and sends
// Orbit again once if it doesn't answer.
constexpr double kRestartAnswer = 15.0;
constexpr double kRestartStop = 30.0; // Orbit should have stopped by then
constexpr int kRestartAttempts = 2;
// Without Settings open, statuses are read every twentieth poll (a minute).
constexpr int kSettingsEvery = 20;

// A catalogue fetch is needed when anything that changes the list changes:
// the server, its catalogue revision, or the source choices.
std::string catalog_key(const System &system, const Sources &sources)
{
    std::string key = system.version;
    key += system.zero_enabled ? "|zero:" + system.zero_address : "|console";
    key += '|';
    key += std::to_string(system.catalogue_revision);
    key += '|';
    key += sources.acknowledged ? '1' : '0';
    key += '|';
    key += std::to_string(sources.notice_version);
    for (const std::string &id : sources.enabled)
    {
        key += '|';
        key += id;
    }
    for (const SourceOption &option : sources.options)
    {
        key += '|';
        key += option.id;
        key += ':';
        key += std::to_string(option.release_count);
    }
    return key;
}

} // namespace

const Game *Snapshot::game(std::string_view id) const
{
    for (const Game &game : games)
    {
        if (game.id == id)
            return &game;
    }
    for (const Game &game : addons) if (game.id == id) return &game;
    return nullptr;
}

bool Snapshot::favourite(std::string_view game_id) const
{
    return contains(favourites, game_id);
}

const std::vector<LibraryGame> &Snapshot::library_games() const
{
    static const std::vector<LibraryGame> kNone;
    return have_library && library.ready() ? library.games : kNone;
}

Store::Store(http::Transport &transport, starter::Starter *starter)
    : client_(transport), starter_(starter)
{
}

Store::~Store()
{
    stop();
}

void Store::start()
{
    thread_.start([this] { worker(); });
}

void Store::stop()
{
    {
        Lock lock(mutex_);
        stopping_ = true;
    }
    signal_.notify();
    thread_.join();
}

void Store::tick()
{
    inbox_.drain();
    const auto event = native_provider_.tick(state_.browser, browser_observed_ && state_.have_browser && state_.link == Link::online, now_seconds());
    browser_observed_ = false;
    if (provider_poll_.exchange(native_provider_.busy()) != native_provider_.busy())
        refresh_now(); // begin focused polling, or immediately refresh on return
    if (event.kind != NativeProvider::Event::none) {
        if (!event.error.empty()) provider_error_ = event.error;
        browser_action_.busy = true;
        {
            Lock lock(mutex_);
            Command command;
            command.kind = event.kind == NativeProvider::Event::opened ? Command::Kind::browser_opened : Command::Kind::browser_cancel;
            command.a = event.id; command.browser_client = event.client_id;
            command.browser_failure = event.failure; command.browser_error_code = event.error_code;
            commands_.push_back(std::move(command));
        }
        signal_.notify();
    }
}

void Store::refresh_now()
{
    {
        Lock lock(mutex_);
        poll_requested_ = true;
    }
    signal_.notify();
}

void Store::start_orbit()
{
    {
        Lock lock(mutex_);
        start_requested_ = true;
        poll_requested_ = true;
    }
    signal_.notify();
}

void Store::download(const std::string &release_id, const std::string &storage_id, bool torbox)
{
    ++pending_actions_;
    action_.busy = true;
    action_.error.clear();
    action_.created_job.clear();
    {
        Lock lock(mutex_);
        Command command;
        command.kind = Command::Kind::download;
        command.a = release_id;
        command.b = storage_id;
        command.flag = torbox;
        commands_.push_back(std::move(command));
    }
    signal_.notify();
}

void Store::job_action(const std::string &job_id, const std::string &action, bool delete_partial, const std::string &password)
{
    ++pending_actions_;
    action_.busy = true;
    action_.error.clear();
    {
        Lock lock(mutex_);
        Command command;
        command.kind = Command::Kind::job_action;
        command.a = job_id;
        command.b = action;
        command.password = password;
        command.flag = delete_partial;
        commands_.push_back(std::move(command));
    }
    signal_.notify();
}

void Store::clear_history(const std::string &status)
{
    ++pending_actions_;
    action_.busy = true;
    action_.error.clear();
    {
        Lock lock(mutex_);
        Command command;
        command.kind = Command::Kind::clear_history;
        command.a = status;
        commands_.push_back(std::move(command));
    }
    signal_.notify();
}

void Store::set_favourite(const std::string &game_id, bool favourite)
{
    favourite_.busy = true;
    favourite_.error.clear();
    {
        Lock lock(mutex_);
        Command command;
        command.kind = Command::Kind::favourite;
        command.a = game_id;
        command.flag = favourite;
        commands_.push_back(std::move(command));
    }
    signal_.notify();
}

void Store::change_saved_filters(std::string body)
{
    saved_filters_.busy = true;
    saved_filters_.error.clear();
    {
        Lock lock(mutex_);
        Command command;
        command.kind = Command::Kind::saved_filters;
        command.a = std::move(body);
        commands_.push_back(std::move(command));
    }
    signal_.notify();
}

void Store::start_browser(const std::string &release_id, const std::string &storage_id, bool account, bool check_account)
{
    if (browser_action_.busy || native_provider_.busy()) return;
    provider_error_.clear();
    std::string client;
    if (native_provider_.available() && state_.browser.native_dialog_available) {
        char text[64];
        std::snprintf(text, sizeof text, "tv-%llx-%llx",
            static_cast<unsigned long long>(unix_seconds() * 1000000.0),
            static_cast<unsigned long long>(++request_count_));
        client = text;
        native_provider_.begin(client, now_seconds());
        provider_poll_.store(true);
    }
    browser_action_.busy = true;
    browser_action_.error.clear();
    if (!state_.browser.active)
    {
        const bool available = state_.browser.available;
        const bool native_dialog = state_.browser.native_dialog_available;
        const bool account_available = state_.browser.viking_account_available;
        state_.browser = {};
        state_.browser.available = available;
        state_.browser.native_dialog_available = native_dialog;
        state_.browser.viking_account_available = account_available;
        state_.browser.account_page = account;
        state_.browser.state = "opening";
        for (const auto *games : {&state_.games, &state_.addons})
            for (const auto &game : *games)
                for (const auto &release : game.releases)
                    if (release.id == release_id) state_.browser.title = game.title;
    }
    {
        Lock lock(mutex_);
        Command command;
        command.kind = Command::Kind::browser_start;
        command.flag = account;
        command.browser_check_account = check_account;
        command.browser_client = client;
        command.a = release_id;
        command.b = storage_id;
        commands_.push_back(std::move(command));
    }
    signal_.notify();
}

void Store::cancel_browser(const std::string &session_id)
{
    if (browser_action_.busy || session_id.empty()) return;
    browser_action_.busy = true;
    browser_action_.error.clear();
    {
        Lock lock(mutex_);
        Command command;
        command.kind = Command::Kind::browser_cancel;
        command.a = session_id;
        commands_.push_back(std::move(command));
    }
    signal_.notify();
}

void Store::poll_browser()
{
    auto result = client_.browser_session();
    inbox_.post([this, result = std::move(result)]() mutable
    {
        state_.have_browser = result.ok();
        browser_observed_ = result.ok();
        state_.browser_error = result.status == 404
            ? tr("Update Orbit to open provider pages.") : result.error;
        if (result.ok()) state_.browser = std::move(result.value);
        ++state_.serial;
    });
}

void Store::run_browser(Command &command)
{
    auto result = command.kind == Command::Kind::browser_start
        ? client_.start_browser(command.a, command.b, command.browser_client, command.flag, command.browser_check_account)
        : command.kind == Command::Kind::browser_opened ? client_.browser_opened(command.a, command.browser_client)
        : client_.cancel_browser(command.a, command.browser_client, command.browser_failure, command.browser_error_code);
    // A lost POST response may still have started verification. Read status,
    // never replay the mutation or create an unvalidated queue item here.
    poll_browser();
    inbox_.post([this, starting = command.kind == Command::Kind::browser_start,
                 cancelling = command.kind == Command::Kind::browser_cancel, session_id = command.a,
                 result = std::move(result)]() mutable
    {
        if (starting && result.status >= 400 && result.status < 500) native_provider_.abandon();
        if (result.ok())
        {
            // The subsequent GET is fresher; keep it when it succeeded.
            if (!state_.have_browser)
            {
                state_.browser = std::move(result.value);
                state_.have_browser = true;
                state_.browser_error.clear();
            }
        }
        browser_action_.busy = false;
        const bool already_queued = cancelling && result.status == 409 && state_.have_browser &&
            state_.browser.id == session_id && state_.browser.state == "queued" && !state_.browser.job_id.empty();
        if (already_queued) provider_error_.clear();
        browser_action_.error = provider_error_.empty() ? (already_queued ? "" : result.error) : provider_error_;
        ++browser_action_.serial;
    });
    refresh_now();
}

void Store::refresh_library()
{
    {
        Lock lock(mutex_);
        Command command;
        command.kind = Command::Kind::library_refresh;
        commands_.push_back(std::move(command));
    }
    signal_.notify();
}

void Store::run_library_action(api::Client::LibraryRequest request)
{
    library_.busy = true;
    library_.error.clear();
    // Unique per action: the backend refuses to carry out a request twice.
    char id[48];
    std::snprintf(id, sizeof(id), "tv-%llx-%llx",
                  static_cast<unsigned long long>(unix_seconds() * 1000.0),
                  static_cast<unsigned long long>(++request_count_));
    request.request_id = id;
    {
        Lock lock(mutex_);
        Command command;
        command.kind = Command::Kind::library_action;
        command.library = std::move(request);
        commands_.push_back(std::move(command));
    }
    signal_.notify();
}

void Store::send_setting(Setting setting, std::string a, std::string b, bool flag)
{
    setting_.busy = true;
    setting_.setting = setting;
    setting_.error.clear();
    {
        Lock lock(mutex_);
        Command command;
        command.kind = Command::Kind::setting;
        command.setting = setting;
        command.a = std::move(a);
        command.b = std::move(b);
        command.flag = flag;
        commands_.push_back(std::move(command));
    }
    signal_.notify();
}

void Store::save_sources(std::vector<std::string> enabled, int notice_version)
{
    std::string list;
    for (const std::string &id : enabled)
    {
        if (!list.empty())
            list += '\n';
        list += id;
    }
    send_setting(Setting::sources, std::move(list), std::to_string(notice_version));
}

void Store::set_preferred_storage(const std::string &storage_id)
{
    send_setting(Setting::storage, storage_id);
}

void Store::refresh_catalogue()
{
    send_setting(Setting::catalogue);
}

void Store::check_service_update()
{
    send_setting(Setting::service_check);
}

void Store::install_service_update(const std::string &version, const std::string &checksum)
{
    send_setting(Setting::service_install, version, checksum);
}

void Store::restart_service()
{
    send_setting(Setting::service_restart);
}

void Store::check_tv_app()
{
    send_setting(Setting::tv_check);
}

void Store::install_tv_app(const std::string &version, const std::string &checksum, bool replace)
{
    send_setting(Setting::tv_install, version, checksum, replace);
}

void Store::want_settings(bool wanted)
{
    const bool was = want_settings_.exchange(wanted);
    if (wanted && !was)
        refresh_now();
}

void Store::refresh_session()
{
    session_refresh_.store(true);
    refresh_now();
}

void Store::open_files(const std::string &storage_id, const std::string &path)
{
    if (storage_id.empty())
    {
        state_.have_files = false;
        state_.files = FileListing{};
        state_.files_error.clear();
        ++state_.serial;
        return;
    }
    {
        Lock lock(mutex_);
        Command command;
        command.kind = Command::Kind::files_list;
        command.a = storage_id;
        command.b = path;
        commands_.push_back(std::move(command));
    }
    signal_.notify();
}

void Store::want_files(bool wanted)
{
    if (!want_files_.exchange(wanted) && wanted)
        refresh_now();
}

void Store::run_file_action(api::Client::FileRequest request)
{
    files_.busy = true;
    files_.error.clear();
    {
        Lock lock(mutex_);
        Command command;
        command.kind = Command::Kind::file_action;
        command.file = std::move(request);
        commands_.push_back(std::move(command));
    }
    signal_.notify();
}

void Store::want_library_storage(bool wanted, bool refresh)
{
    const bool was = want_storage_.exchange(wanted);
    if (refresh)
        storage_refresh_.store(true);
    if (wanted && (!was || refresh))
        refresh_now();
}

void Store::worker()
{
    double next_poll = 0.0;
    for (;;)
    {
        bool have_command = false;
        bool poll = false;
        {
            Lock lock(mutex_);
            for (;;)
            {
                if (stopping_)
                    return;
                have_command = !commands_.empty();
                poll = poll_requested_ || now_seconds() >= next_poll;
                if (have_command || poll)
                    break;
                signal_.wait(mutex_, next_poll - now_seconds());
            }
            poll_requested_ = false;
        }
        if (have_command)
        {
            run_next_action();
            continue; // the action already refreshed the queue
        }
        poll_once();
        const bool hurry = start_waiting_ || restart_ == Restart::stopping ||
                           restart_ == Restart::starting ||
                           (want_settings_.load() && settings_busy_);
        next_poll = now_seconds() + (provider_poll_.load() ? 0.25 : hurry ? 0.5 : fast_.load() ? 1.0 : 3.0);
    }
}

void Store::post_failure(const std::string &error, bool unreachable)
{
    inbox_.post(
        [this, error, unreachable]
        {
            state_.link_error = error;
            if (unreachable)
            {
                if (state_.link != Link::offline)
                    state_.offline_since = now_seconds();
                state_.link = Link::offline;
            }
            ++state_.serial;
        });
}

void Store::post_start(Starting starting, std::string detail)
{
    start_state_ = starting;
    inbox_.post(
        [this, starting, detail = std::move(detail)]() mutable
        {
            state_.starting = starting;
            state_.start_detail = std::move(detail);
            ++state_.serial;
        });
}

void Store::post_restart(Restart restart, std::string detail)
{
    inbox_.post(
        [this, restart, detail = std::move(detail)]() mutable
        {
            state_.restart = restart;
            state_.restart_detail = std::move(detail);
            ++state_.serial;
        });
}

// Runs on the worker while a restart is under way and Orbit isn't answering.
void Store::continue_restart(bool unreachable)
{
    const double now = now_seconds();
    bool retry = false;
    {
        Lock lock(mutex_);
        retry = start_requested_;
        start_requested_ = false;
    }
    if (restart_ == Restart::stopping)
    {
        if (!unreachable)
        {
            if (now - restart_since_ > kRestartStop)
            {
                restart_ = Restart::idle;
                post_restart(Restart::failed, tr("Orbit didn\xE2\x80\x99t stop. Try again."));
            }
            return;
        }
        // Orbit's port has closed; give it a moment to release its lock.
        restart_ = Restart::starting;
        restart_since_ = now;
        restart_send_at_ = now + (restart_self_ ? restart_self_wait_ : restart_grace_);
        restart_attempts_ = 0;
        restart_sent_ = false;
        post_restart(Restart::starting, tr("Orbit stopped."));
        return;
    }
    if ((restart_ == Restart::manual || restart_ == Restart::failed) && retry)
    {
        // Try again, from the start.
        restart_ = Restart::starting;
        restart_send_at_ = now;
        restart_attempts_ = 0;
        restart_sent_ = false;
        post_restart(Restart::starting, {});
    }
    if (restart_ != Restart::starting)
        return;
    // Orbit is starting itself; this app starts a copy only if it doesn't come back.
    if (restart_self_ && !restart_sent_ && now < restart_send_at_)
        return;
    if (starter_ == nullptr)
    {
        restart_ = Restart::manual;
        post_restart(Restart::manual, {});
        return;
    }
    if (restart_sent_ && now < restart_deadline_)
        return;
    if (restart_sent_ && restart_attempts_ >= kRestartAttempts)
    {
        restart_ = Restart::failed;
        post_restart(Restart::failed, tr("Orbit didn\xE2\x80\x99t answer after it was started."));
        return;
    }
    if (now < restart_send_at_)
        return;
    ++restart_attempts_;
    starter::Result result = starter_->start();
    switch (result.outcome)
    {
    case starter::Outcome::sent:
        // A newer bundled payload can supersede an older saved copy. Falling
        // back to an older bundle must not lower the version we promised to
        // restart, or a missing/corrupt saved update would look successful.
        if (!result.version.empty() &&
            (restart_expected_version_.empty() ||
             starter::compare_versions(result.version, restart_expected_version_) > 0))
            restart_expected_version_ = result.version;
        restart_sent_ = true;
        restart_deadline_ = now_seconds() + kRestartAnswer;
        post_restart(Restart::starting, std::move(result.detail));
        break;
    case starter::Outcome::no_loader:
    case starter::Outcome::no_payload:
        restart_ = Restart::manual;
        post_restart(Restart::manual, std::move(result.detail));
        break;
    case starter::Outcome::failed:
        if (restart_attempts_ < kRestartAttempts)
        {
            restart_send_at_ = now_seconds() + restart_grace_;
            post_restart(Restart::starting, std::move(result.detail));
        }
        else
        {
            restart_ = Restart::failed;
            post_restart(Restart::failed, std::move(result.detail));
        }
        break;
    }
}

// Runs on the worker after a poll found Orbit unreachable. Sends Orbit to the
// loader once per launch when nothing listens on its port (a refused
// connection, not a slow one), and whenever the player asks; then gives Orbit
// kStartWait seconds to answer.
void Store::start_if_needed(bool refused)
{
    bool requested = false;
    {
        Lock lock(mutex_);
        requested = start_requested_;
        start_requested_ = false;
    }
    if (starter_ == nullptr)
        return;
    if (start_waiting_)
    {
        if (now_seconds() < start_deadline_)
            return;
        start_waiting_ = false;
        post_start(Starting::failed, tr("Orbit didn\xE2\x80\x99t answer after it was started."));
        return;
    }
    const bool automatic = refused && !auto_start_used_;
    if (!requested && !automatic)
        return;
    auto_start_used_ = true;
    post_start(Starting::starting, {});
    starter::Result result = starter_->start();
    switch (result.outcome)
    {
    case starter::Outcome::sent:
        start_waiting_ = true;
        start_deadline_ = now_seconds() + kStartWait;
        post_start(Starting::waiting, std::move(result.detail));
        break;
    case starter::Outcome::no_loader:
        post_start(Starting::no_loader, std::move(result.detail));
        break;
    case starter::Outcome::no_payload:
        post_start(Starting::no_payload, std::move(result.detail));
        break;
    case starter::Outcome::failed:
        post_start(Starting::failed, std::move(result.detail));
        break;
    }
}

void Store::post_queue(std::vector<Job> jobs, std::vector<Drive> drives, bool have_drives)
{
    inbox_.post(
        [this, jobs = std::move(jobs), drives = std::move(drives), have_drives]() mutable
        {
            state_.jobs = std::move(jobs);
            if (have_drives)
                state_.drives = std::move(drives);
            state_.have_queue = true;
            ++state_.serial;
        });
}

void Store::poll_once()
{
    // While our provider dialog owns the screen, only its result matters.
    // Do not queue it behind catalogue, storage, library or update requests.
    // The frame thread requests a full refresh when the dialog finishes.
    if (provider_poll_.load()) {
        poll_browser();
        return;
    }
    api::Result<System> system = client_.system();
    if (!system.ok())
    {
        post_failure(system.error, system.unreachable());
        if (system.unreachable())
            worker_link_ = Link::offline;
        if (restart_ != Restart::idle)
            continue_restart(system.unreachable());
        else if (system.unreachable())
            start_if_needed(system.error == "connection refused");
        return;
    }
    if (system.value.stopping)
    {
        // A chosen shutdown is never answered by automatic relaunch. Keep
        // polling until the old service has flushed and released its workers.
        auto_start_used_ = true;
        // Unless it's the restart at the end of an update: /system says "stopping" from then on.
        if (restart_ == Restart::idle)
        {
            api::Result<ServiceUpdate> service = client_.service_update();
            if (service.ok())
                follow_update_restart(service.value);
        }
        if (restart_ != Restart::idle && now_seconds() - restart_since_ > kRestartStop)
        {
            restart_ = Restart::idle;
            post_restart(Restart::failed, tr("Orbit is still running. Try restarting it again."));
        }
        return;
    }
    if (restart_ == Restart::stopping &&
        (restart_previous_instance_.empty() || system.value.instance_id == restart_previous_instance_))
    {
        // Orbit answers for a moment after it agrees to stop.
        if (now_seconds() - restart_since_ > kRestartStop)
        {
            restart_ = Restart::idle;
            post_restart(Restart::failed, tr("Orbit is still running. Try restarting it again."));
        }
    }
    else if (restart_ != Restart::idle)
    {
        restart_ = Restart::idle;
        if (!restart_expected_version_.empty() && system.value.version != restart_expected_version_)
            post_restart(Restart::failed,
                tr("Orbit {running} is running, but {expected} was selected. Start the saved payload from /data/orbit-store/orbit_store.elf.",
                   {{"running", system.value.version}, {"expected", restart_expected_version_}}));
        else if (!restart_previous_instance_.empty() &&
                 (system.value.instance_id.empty() || system.value.instance_id == restart_previous_instance_))
            post_restart(Restart::failed, tr("The previous Orbit session is still running. Wait for it to stop, then start the saved payload."));
        else
            post_restart(Restart::done, system.value.version);
        session_refresh_.store(true); // a new pairing code
    }
    observed_version_ = system.value.version;
    observed_instance_ = system.value.instance_id;
    // Connecting to an existing service consumes automatic startup too. A
    // later stop from the browser must not immediately respawn the old copy.
    auto_start_used_ = true;
    if (worker_link_ == Link::offline)
        session_refresh_.store(true); // Orbit may have restarted: a new pairing code
    worker_link_ = Link::online;
    {
        Lock lock(mutex_);
        start_requested_ = false; // Orbit is answering: nothing to start
    }
    if (start_waiting_ || start_state_ != Starting::idle)
    {
        start_waiting_ = false;
        post_start(Starting::idle, {});
    }
    api::Result<Sources> sources = client_.sources();
    if (!sources.ok())
    {
        post_failure(sources.error, sources.unreachable());
        return;
    }

    const std::string key = catalog_key(system.value, sources.value);
    bool have_games = false;
    std::vector<Game> games, addons;
    if (key != catalog_key_)
    {
        api::Result<std::vector<Release>> catalog = client_.catalog();
        auto extra = client_.addons();
        if (catalog.ok() && (extra.ok() || extra.status == 404))
        {
            if(system.value.zero_enabled) {
                auto &rows=catalog.value;
                rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const Release &r){
                    return !sources.value.acknowledged || !contains(sources.value.enabled,r.source_id);
                }),rows.end());
            }
            if(system.value.zero_enabled) {
                auto &rows=extra.value;
                rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const Release &r){
                    return !sources.value.acknowledged || !contains(sources.value.enabled,r.source_id);
                }),rows.end());
            }
            addons = group_games(extra.value);
            games = group_games(catalog.value);
            have_games = true;
            catalog_key_ = key;
        }
    }

    // The app runs on the console, so it opens the loopback-only session
    // rather than pairing with a code. It is asked for again only if the
    // backend stops recognising the token.
    const bool need_token = !system.value.paired && now_seconds() >= next_session_;
    if (system.value.local_session && (need_token || session_refresh_.load()))
    {
        session_refresh_.store(false);
        api::Result<Session> session = client_.session();
        if (need_token)
            next_session_ = now_seconds() + 10.0; // a refusal is retried, but not every poll
        if (session.ok())
        {
            token_ = session.value.token;
            client_.set_token(token_);
            system.value.paired = true;
            inbox_.post(
                [this, code = session.value.pair_code, address = session.value.address]() mutable
                {
                    state_.pair_code = std::move(code);
                    state_.address = std::move(address);
                    ++state_.serial;
                });
        }
    }
    if (system.value.paired)
        next_session_ = 0.0; // a later loss of the token is repaired at once

    inbox_.post(
        [this, system = std::move(system.value), sources = std::move(sources.value),
         games = std::move(games), addons = std::move(addons), have_games, token = token_]() mutable
        {
            if (state_.link != Link::online)
                state_.link_error.clear();
            state_.link = Link::online;
            state_.system = std::move(system);
            state_.have_system = true;
            state_.sources = std::move(sources);
            state_.have_sources = true;
            if (have_games)
            {
                state_.games = std::move(games);
                state_.addons = std::move(addons);
                state_.have_catalog = true;
                ++state_.catalog_serial;
            }
            state_.paired = state_.system.paired;
            state_.token = token;
            ++state_.serial;
        });

    if (!system.value.paired && token_.empty())
        return;
    api::Result<std::vector<Job>> jobs = client_.downloads();
    if (!jobs.ok())
    {
        if (jobs.status == 401)
        {
            token_.clear();
            client_.set_token({});
        }
        post_failure(jobs.error, jobs.unreachable());
        return;
    }
    api::Result<std::vector<Drive>> drives = client_.storage();
    post_queue(std::move(jobs.value), std::move(drives.value), drives.ok());
    poll_browser();
    // Optional on older backends. A failed read clears availability so a
    // disconnected account cannot remain an apparently usable choice.
    api::Result<Debrid> debrid = client_.debrid();
    inbox_.post(
        [this, debrid = std::move(debrid)]() mutable
        {
            state_.debrid = debrid.ok() ? std::move(debrid.value) : Debrid{};
            ++state_.serial;
        });

    // Favourites and the Library (cheap reads: the backend answers from its
    // cache and limits ShadowMount reads itself).
    api::Result<std::vector<std::string>> favourites = client_.favourites();
    api::Result<std::vector<SavedFilter>> saved_filters = client_.saved_filters();
    api::Result<LibrarySnapshot> library = client_.library(false);
    api::Result<LibraryStorage> storage;
    const bool want_storage = want_storage_.load();
    if (want_storage)
        storage = client_.library_storage(storage_refresh_.exchange(false));
    api::Result<FileOperation> operation;
    const bool want_files = want_files_.load();
    if (want_files)
        operation = client_.files_status();
    inbox_.post(
        [this, favourites = std::move(favourites), saved_filters = std::move(saved_filters), library = std::move(library),
         storage = std::move(storage), want_storage, operation = std::move(operation), want_files]() mutable
        {
            if (want_files && operation.ok())
            {
                state_.have_file_operation = !operation.value.id.empty();
                state_.file_operation = std::move(operation.value);
            }
            if (favourites.ok())
            {
                state_.favourites = std::move(favourites.value);
                state_.have_favourites = true;
            }
            if (saved_filters.ok())
            {
                state_.saved_filters = std::move(saved_filters.value);
                state_.have_saved_filters = true;
            }
            if (library.ok())
            {
                state_.library = std::move(library.value);
                state_.have_library = true;
                state_.library_error.clear();
            }
            else if (!library.unreachable())
            {
                state_.library_error = library.error;
            }
            if (want_storage && storage.ok())
            {
                state_.library_storage = std::move(storage.value);
                state_.have_library_storage = true;
            }
            ++state_.serial;
        });

    // Opening the app looks for updates once, as the browser version does;
    // Orbit shares a six-hour cooldown between every client.
    if (!automatic_checks_sent_)
    {
        automatic_checks_sent_ = true;
        api::Client::UpdateRequest check;
        check.action = "check";
        check.automatic = true;
        api::Result<ServiceUpdate> service = client_.service_action(check);
        api::Result<TvAppStatus> tv = client_.tv_app_action(check);
        (void)service;
        (void)tv;
        settings_countdown_ = 0; // read the outcome at once
    }
    if (want_settings_.load() || settings_busy_ || --settings_countdown_ <= 0)
    {
        settings_countdown_ = kSettingsEvery;
        poll_settings();
    }
}

// An update started anywhere (here, the browser or a phone) restarts Orbit at its end: follow
// it as this app's own restart, so the screen reconnects by itself.
void Store::follow_update_restart(const ServiceUpdate &service)
{
    if (!service.update_all || service.phase != "restarting" || restart_ != Restart::idle)
        return;
    restart_expected_version_ = service.saved_version.empty() ? service.running_version : service.saved_version;
    restart_previous_instance_ = service.instance_id;
    restart_ = Restart::stopping;
    restart_self_ = true;
    restart_since_ = now_seconds();
    post_restart(Restart::stopping, {});
}

// The catalogue and update statuses (Settings, and the update badge).
void Store::poll_settings()
{
    api::Result<CatalogueStatus> catalogue = client_.catalogue_status();
    api::Result<ServiceUpdate> service = client_.service_update();
    api::Result<TvAppStatus> tv = client_.tv_app();
    api::Result<ArchiveAccount> archive = client_.archive_account();
    api::Result<NtfySettings> ntfy = client_.ntfy();
    settings_busy_ = (catalogue.ok() && catalogue.value.busy) ||
                     (service.ok() && service.value.busy) || (tv.ok() && tv.value.busy);
    if (service.ok())
    {
        service_self_restart_ = service.value.self_restart;
        follow_update_restart(service.value);
    }
    inbox_.post(
        [this, catalogue = std::move(catalogue), service = std::move(service), tv = std::move(tv),
         archive = std::move(archive), ntfy = std::move(ntfy)]() mutable
        {
            // An older download service has no Archive.org sign-in, or no ntfy.
            if (archive.ok() || archive.status == 404)
                state_.archive_account = archive.ok() ? std::move(archive.value) : ArchiveAccount{};
            if (ntfy.ok() || ntfy.status == 404)
                state_.ntfy = ntfy.ok() ? std::move(ntfy.value) : NtfySettings{};
            if (catalogue.ok())
            {
                state_.catalogue = std::move(catalogue.value);
                state_.have_catalogue_status = true;
            }
            if (service.ok())
            {
                state_.service = std::move(service.value);
                state_.have_service_update = true;
            }
            if (tv.ok())
            {
                state_.tv_app = std::move(tv.value);
                state_.have_tv_app = true;
            }
            ++state_.serial;
        });
}

// Settings commands: each reports to setting_ and refreshes what it changed.
void Store::run_setting(Command &command)
{
    std::string error;
    switch (command.setting)
    {
    case Setting::zero:
        error = client_.set_zero(command.flag).error;
        catalog_key_.clear();
        break;
    case Setting::download_recovery:
    {
        const int field = std::atoi(command.a.c_str());
        const auto result = client_.set_download_recovery(field, std::atoi(command.b.c_str()));
        error = result.status == 404 ? tr("Update the download service to change this here.") : result.error;
        if (result.ok()) inbox_.post([this, field, value = result.value]() {
            state_.system.download_recovery[field] = value;
            ++state_.serial;
        });
        break;
    }
    case Setting::download_connections:
    {
        const auto result = client_.set_download_connections(std::atoi(command.a.c_str()));
        error = result.status == 404 ? tr("Update the download service to change this here.") : result.error;
        if (result.ok()) inbox_.post([this, connections = result.value]() {
            state_.system.download_connections = connections;
            ++state_.serial;
        });
        break;
    }
    case Setting::download_limit:
    {
        const int limit = command.a.size() == 1 ? command.a[0] - '0' : 0;
        const auto result = client_.set_download_limit(limit);
        error = result.status == 404 ? tr("Update the download service to change this here.") : result.error;
        if (result.ok()) inbox_.post([this, limit = result.value]() {
            state_.system.concurrent_downloads = limit;
            ++state_.serial;
        });
        break;
    }
    case Setting::archive_sign_in:
    case Setting::archive_sign_out:
    {
        api::Result<ArchiveAccount> result = command.setting == Setting::archive_sign_in
                                                 ? client_.archive_sign_in(command.a, command.b)
                                                 : client_.archive_sign_out();
        // The password was needed once.
        std::fill(command.b.begin(), command.b.end(), '\0');
        command.b.clear();
        error = result.status == 404 ? tr("Update the download service to change this here.") : result.error;
        if (result.ok())
            inbox_.post(
                [this, account = std::move(result.value)]() mutable
                {
                    state_.archive_account = std::move(account);
                    ++state_.serial;
                });
        break;
    }
    case Setting::notifications:
    {
        api::Result<bool> result = client_.set_download_notifications(command.flag);
        error = result.status == 404 ? tr("Update the download service to change this here.") : result.error;
        if (result.ok())
            inbox_.post(
                [this, enabled = result.value]()
                {
                    state_.system.download_notifications = enabled;
                    ++state_.serial;
                });
        break;
    }
    case Setting::notification_event:
    {
        api::Result<std::array<bool, 5>> result = client_.set_notification_event(std::atoi(command.a.c_str()), command.flag);
        error = result.status == 404 ? tr("Update the download service to change this here.") : result.error;
        if (result.ok())
            inbox_.post(
                [this, events = result.value]()
                {
                    state_.system.notification_events = events;
                    ++state_.serial;
                });
        break;
    }
    case Setting::ntfy_save:
    {
        api::Result<NtfySettings> result = client_.save_ntfy(std::move(command.a));
        error = result.status == 404 ? tr("Update the download service to change this here.") : result.error;
        if (result.ok())
            inbox_.post(
                [this, ntfy = std::move(result.value)]() mutable
                {
                    state_.ntfy = std::move(ntfy);
                    state_.system.ntfy_enabled = state_.ntfy.enabled;
                    ++state_.serial;
                });
        break;
    }
    case Setting::ntfy_test:
    {
        api::Result<bool> result = client_.test_ntfy();
        error = result.status == 404 ? tr("Update the download service to change this here.") : result.error;
        break;
    }
    case Setting::packages:
    {
        api::Result<bool> result = client_.set_delete_packages(command.flag);
        error = result.status == 404 ? tr("Update the download service to change this here.") : result.error;
        if (result.ok())
            inbox_.post(
                [this, enabled = result.value]()
                {
                    state_.system.delete_packages = enabled;
                    ++state_.serial;
                });
        break;
    }
    case Setting::diagnostics_report:
    {
        api::Result<DiagnosticsReport> result = client_.diagnostics();
        error = result.status == 404 ? std::string(tr("Update the download service to change this here.")) : result.error;
        if (result.ok())
            inbox_.post(
                [this, report = std::move(result.value)]() mutable
                {
                    state_.diagnostics = std::move(report);
                    ++state_.serial;
                });
        break;
    }
    case Setting::diagnostics:
    {
        const std::size_t cut = command.b.find('|');
        api::Result<std::string> result = client_.save_diagnostics(
            command.a, command.b.substr(0, cut), cut == std::string::npos ? std::string() : command.b.substr(cut + 1));
        error = result.status == 404     ? std::string(tr("Update the download service to save a report here."))
                : !result.ok()            ? result.error
                : result.value.empty() ? std::string(tr("Could not save the diagnostic report on that drive."))
                                          : std::string();
        // Only the last save's file is shown: none after a failed one.
        inbox_.post(
            [this, path = error.empty() ? result.value : std::string()]()
            {
                state_.diagnostics_path = path;
                ++state_.serial;
            });
        break;
    }
    case Setting::archives:
    {
        api::Result<bool> result = client_.set_delete_archives(command.flag);
        error = result.status == 404 ? tr("Update the download service to change this here.") : result.error;
        if (result.ok())
        {
            inbox_.post(
                [this, enabled = result.value]()
                {
                    state_.system.delete_archives = enabled;
                    ++state_.serial;
                });
        }
        break;
    }
    case Setting::sources:
    {
        std::vector<std::string> enabled;
        std::size_t start = 0;
        while (start < command.a.size())
        {
            std::size_t end = command.a.find('\n', start);
            if (end == std::string::npos)
                end = command.a.size();
            enabled.push_back(command.a.substr(start, end - start));
            start = end + 1;
        }
        api::Result<Sources> result = client_.save_sources(enabled, std::atoi(command.b.c_str()));
        error = result.error;
        if (result.ok())
        {
            inbox_.post(
                [this, sources = std::move(result.value)]() mutable
                {
                    state_.sources = std::move(sources);
                    state_.have_sources = true;
                    ++state_.serial;
                });
        }
        break;
    }
    case Setting::storage:
    {
        api::Result<std::string> result = client_.set_preferred_storage(command.a);
        error = result.status == 404 ? tr("Update the download service to choose a default drive here.")
                                     : result.error;
        if (result.ok())
        {
            inbox_.post(
                [this, id = std::move(result.value)]() mutable
                {
                    state_.system.preferred_storage = std::move(id);
                    ++state_.serial;
                });
        }
        break;
    }
    case Setting::catalogue:
    {
        api::Result<CatalogueStatus> result = client_.refresh_catalogue();
        error = result.error;
        break;
    }
    case Setting::service_check:
    case Setting::service_install:
    case Setting::service_restart:
    {
        api::Client::UpdateRequest request;
        request.action = command.setting == Setting::service_check     ? "check"
                         : command.setting == Setting::service_install ? "install"
                         : service_self_restart_                       ? "restart"
                                                                       : "stop";
        request.version = command.a;
        request.checksum = command.b;
        request.confirmed = command.setting == Setting::service_restart;
        api::Result<ServiceUpdate> result = client_.service_action(request);
        if (request.action == "restart" && result.status == 409)
        {
            // No ELF loader answers for Orbit: stop it, and this app starts it as before.
            request.action = "stop";
            result = client_.service_action(request);
        }
        error = result.error;
        if (result.ok() && command.setting == Setting::service_restart)
        {
            restart_expected_version_ = result.value.saved_version.empty()
                ? (result.value.running_version.empty() ? observed_version_ : result.value.running_version)
                : result.value.saved_version;
            restart_previous_instance_ = result.value.instance_id.empty()
                ? observed_instance_ : result.value.instance_id;
            restart_ = Restart::stopping;
            restart_self_ = request.action == "restart";
            restart_since_ = now_seconds();
            post_restart(Restart::stopping, {});
        }
        break;
    }
    case Setting::update_all:
    {
        // Releases found more than about fourteen minutes ago are checked again first;
        // Orbit installs only what it checked in the last fifteen.
        api::Result<ServiceUpdate> service = client_.service_update();
        api::Result<TvAppStatus> tv = client_.tv_app();
        const auto stale = [](double checked_at) { return unix_seconds() - checked_at > 840.0; };
        // Waits up to a minute for a check to finish (other requests wake the wait early).
        const auto settle = [this](auto read)
        {
            const double deadline = now_seconds() + 60.0;
            for (;;)
            {
                auto status = read();
                if (!status.ok() || !status.value.busy || now_seconds() >= deadline)
                    return status;
                Lock lock(mutex_);
                if (stopping_)
                    return status;
                signal_.wait(mutex_, 0.5);
            }
        };
        bool service_checked = false, tv_checked = false;
        if (service.ok() && service.value.needs_install() && stale(service.value.checked_at))
        {
            api::Client::UpdateRequest check;
            check.action = "check";
            client_.service_action(check);
            service = settle([this] { return client_.service_update(); });
            service_checked = true;
        }
        if (tv.ok() && tv.value.update_available && stale(tv.value.checked_at))
        {
            api::Client::UpdateRequest check;
            check.action = "check";
            client_.tv_app_action(check);
            tv = settle([this] { return client_.tv_app(); });
            tv_checked = true;
        }
        if (!service.ok())
            error = service.error;
        else if (!tv.ok() && tv.status != 404)
            error = tv.error;
        // A check that failed says why (no network, GitHub's limit) better than the install would.
        else if (service_checked && service.value.phase == "error" && !service.value.error.empty())
            error = service.value.error;
        else if (tv_checked && tv.value.phase == "error" && !tv.value.error.empty())
            error = tv.value.error;
        else if (!service.value.needs_install() && !(tv.ok() && tv.value.update_available))
            error = tr("You\xE2\x80\x99re using the latest version.");
        else
            error = client_.update_all(service.value, tv.ok() ? tv.value : TvAppStatus{}).error;
        break;
    }
    case Setting::tv_check:
    case Setting::tv_install:
    {
        api::Client::UpdateRequest request;
        request.action = command.setting == Setting::tv_check ? "check" : "install";
        request.version = command.a;
        request.checksum = command.b;
        request.replace = command.flag;
        api::Result<TvAppStatus> result = client_.tv_app_action(request);
        error = result.error;
        break;
    }
    case Setting::none:
        break;
    }
    if (command.setting != Setting::service_restart || !error.empty())
        poll_settings();
    {
        Lock lock(mutex_);
        poll_requested_ = true;
    }
    inbox_.post(
        [this, error]
        {
            setting_.busy = false;
            setting_.error = error;
            ++setting_.serial;
        });
}

// Favourites and Library commands: each reports to its own ActionState.
// Files: a folder read, or one action; an accepted action returns the operation.
void Store::run_files(Command &command)
{
    if (command.kind == Command::Kind::files_list)
    {
        api::Result<FileListing> result = client_.files_list(command.a, command.b);
        inbox_.post(
            [this, storage = command.a, path = command.b, result = std::move(result)]() mutable
            {
                if (result.ok())
                {
                    state_.files = std::move(result.value);
                    state_.files.storage_id = storage;
                    state_.files.path = path;
                    state_.have_files = true;
                    state_.files_error.clear();
                }
                else
                {
                    state_.files_error = result.error;
                }
                ++state_.serial;
            });
        return;
    }
    api::Result<FileOperation> result = client_.files_action(command.file);
    inbox_.post(
        [this, result = std::move(result)]() mutable
        {
            if (result.ok())
            {
                state_.have_file_operation = !result.value.id.empty();
                state_.file_operation = std::move(result.value);
                ++state_.serial;
            }
            files_.busy = false;
            files_.error = result.ok() ? std::string() : result.error;
            ++files_.serial;
        });
}

void Store::run_aside(Command &command)
{
    std::string error;
    if (command.kind == Command::Kind::favourite)
    {
        api::Result<std::vector<std::string>> result =
            client_.set_favourite(command.a, command.flag);
        if (!result.ok())
            error = result.error;
        inbox_.post(
            [this, error, result = std::move(result)]() mutable
            {
                if (result.ok())
                {
                    state_.favourites = std::move(result.value);
                    state_.have_favourites = true;
                    ++state_.serial;
                }
                favourite_.busy = false;
                favourite_.error = error;
                ++favourite_.serial;
            });
        return;
    }
    if (command.kind == Command::Kind::saved_filters)
    {
        api::Result<std::vector<SavedFilter>> result = client_.change_saved_filters(command.a);
        if (!result.ok())
            error = result.error;
        inbox_.post(
            [this, error, result = std::move(result)]() mutable
            {
                if (result.ok())
                {
                    state_.saved_filters = std::move(result.value);
                    state_.have_saved_filters = true;
                    ++state_.serial;
                }
                saved_filters_.busy = false;
                saved_filters_.error = error;
                ++saved_filters_.serial;
            });
        return;
    }
    api::Result<LibrarySnapshot> result = command.kind == Command::Kind::library_refresh
                                              ? client_.library(true)
                                              : client_.library_action(command.library);
    if (!result.ok())
        error = result.error;
    const bool report = command.kind == Command::Kind::library_action;
    inbox_.post(
        [this, error, report, result = std::move(result)]() mutable
        {
            if (result.ok())
            {
                state_.library = std::move(result.value);
                state_.have_library = true;
                ++state_.serial;
            }
            if (report)
            {
                library_.busy = false;
                library_.error = error;
                ++library_.serial;
            }
        });
}

bool Store::run_next_action()
{
    Command command;
    {
        Lock lock(mutex_);
        if (commands_.empty())
            return false;
        command = std::move(commands_.front());
        commands_.pop_front();
    }
    std::string error;
    std::string created;
    std::vector<Job> jobs;
    bool have_jobs = false;
    if (command.kind == Command::Kind::favourite || command.kind == Command::Kind::saved_filters ||
        command.kind == Command::Kind::library_refresh ||
        command.kind == Command::Kind::library_action)
    {
        run_aside(command);
        return true;
    }
    if (command.kind == Command::Kind::setting)
    {
        run_setting(command);
        return true;
    }
    if (command.kind == Command::Kind::files_list || command.kind == Command::Kind::file_action)
    {
        run_files(command);
        return true;
    }
    if (command.kind == Command::Kind::browser_start || command.kind == Command::Kind::browser_cancel || command.kind == Command::Kind::browser_opened)
    {
        run_browser(command);
        return true;
    }
    if (command.kind == Command::Kind::download)
    {
        api::Result<std::string> result =
            client_.create_download(command.a, command.b, command.flag);
        if (result.ok())
            created = result.value;
        else
            error = result.error;
    }
    else if (command.kind == Command::Kind::clear_history)
    {
        api::Result<std::vector<Job>> result = client_.clear_history(command.a);
        if (result.ok())
        {
            jobs = std::move(result.value);
            have_jobs = true;
        }
        else
        {
            error = result.error;
        }
    }
    else
    {
        api::Result<std::vector<Job>> result =
            client_.job_action(command.a, command.b, command.flag, command.password);
        command.password.clear();
        if (result.ok())
        {
            jobs = std::move(result.value);
            have_jobs = true;
        }
        else
        {
            error = result.error;
        }
    }
    if (!have_jobs)
    {
        api::Result<std::vector<Job>> queue = client_.downloads();
        if (queue.ok())
        {
            jobs = std::move(queue.value);
            have_jobs = true;
        }
    }
    api::Result<std::vector<Drive>> drives = client_.storage();
    if (have_jobs)
        post_queue(std::move(jobs), std::move(drives.value), drives.ok());
    inbox_.post(
        [this, error, created]
        {
            if (pending_actions_ > 0)
                --pending_actions_;
            action_.busy = pending_actions_ > 0;
            action_.error = error;
            action_.created_job = created;
            ++action_.serial;
        });
    return true;
}

} // namespace orbit
