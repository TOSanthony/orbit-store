// Orbit Store TV app - Library in Crossbar: the storage operation and any
// notice first, then search, location and the status views as folders, the
// games, Files and the scan. A game opens like a folder: its actions are
// rows and its details sit on the right. Confirmations are bands. Files'
// own rows are in files.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/i18n.hpp"
#include "orbit/look.hpp"
#include "orbit/screens.hpp"
#include "orbit/thread.hpp"

#include <algorithm>

namespace orbit
{

namespace
{

constexpr const char *kViewNames[] = {/* i18n */ "All games", /* i18n */ "Installed", /* i18n */ "On drive",
                                      /* i18n */ "Unavailable"};
constexpr icons::Icon kViewIcons[] = {icons::Icon::books, icons::Icon::check, icons::Icon::drive, icons::Icon::warn};

bool in_view(const LibraryGame &game, int view)
{
    return view == 1 ? game.installed : view == 2 ? game.on_drive : view == 3 ? !game.on_drive : true;
}

std::string status_of(const LibraryGame &game)
{
    return !game.on_drive ? tr("Unavailable") : game.mounted ? tr("Mounted") : game.installed ? tr("Installed") : tr("On drive");
}

// The catalogue game with this title ID, for its cover.
const Game *art_of(const Snapshot &state, std::string_view title_id)
{
    for (const Game &game : state.games)
        for (const Release &release : game.releases)
            if (release.title_id == title_id)
                return &game;
    return nullptr;
}

std::string game_key(const LibraryGame &game)
{
    return "g:" + game.title_id + "|" + game.source_key;
}

// Under Mount game: mounted and where, or whether the source is there and installed.
std::string status_of_line(const LibraryGame &game)
{
    if (game.mounted)
        return std::string(tr("Mounted")) + " \xC2\xB7 " + i18n::place(game.location);
    return std::string(game.on_drive ? tr("Available on drive") : tr("Unavailable")) + " \xC2\xB7 " +
           (game.installed ? tr("Registered on the console") : tr("Not installed on the console"));
}

std::string last_refreshed(const LibrarySnapshot &library)
{
    return library.updated_at > 0.0 ? tr("Saved snapshot \xC2\xB7 Last refreshed {time}",
                                         {{"time", format_age(library.updated_at, unix_seconds())}})
                                    : std::string(tr("Saved snapshot \xC2\xB7 Not refreshed yet"));
}

} // namespace

std::vector<LibraryScreen::XbEntry> LibraryScreen::crossbar_entries() const
{
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    std::vector<XbEntry> out;
    if (!selected_.empty())
    {
        // A game: mount or unmount, copy, move; or details only.
        const LibraryGame *game = selected_game();
        if (game == nullptr)
            return {{'x', 0}};
        const bool transfer = game->managed && game->can_manage_source && game->on_drive;
        if (game->managed && library.can(game->mounted ? "unmount_game" : "mount_game"))
            out.push_back({'M', 0});
        if (transfer && library.can("copy_game_source") && library.can("storage_job_status"))
            out.push_back({'C', 0});
        if (transfer && library.can("move_game_source") && library.can("storage_job_status"))
            out.push_back({'V', 0});
        if (out.empty())
            out.push_back({'D', 0});
        return out;
    }
    if (section_ == 1)
        return files_crossbar_entries();
    // Files works without ShadowMount: it stays under Connect your library.
    if (!state.have_library || !library.ready())
        return {{'L', 0}, {'S', 0}};
    if (library.has_storage_job && library.storage_job.id > 0)
        out.push_back({'j', 0});
    if (library.stale)
        out.push_back({'s', 0});
    if (!state.library_error.empty() || !library.message.empty())
        out.push_back({'m', 0});
    if (view_ == 0)
    {
        out.push_back({'q', 0});
        out.push_back({'l', 0});
        for (int v = 1; v < 4; ++v)
            out.push_back({'v', v});
    }
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
        out.push_back({'g', i});
    if (rows_.empty())
        out.push_back({library.games.empty() ? 'n' : 'e', 0});
    if (view_ == 0)
    {
        // Game updates and DLC, as the Library's buttons are in the Store look.
        out.push_back({'U', 0});
        if (state.game_dlc > 0)
            out.push_back({'P', 0});
        out.push_back({'S', 0});
        if (library.can("rescan"))
            out.push_back({'c', 0});
    }
    return out;
}

xb::List LibraryScreen::crossbar_list() const
{
    xb::List list;
    const std::vector<XbEntry> entries = crossbar_entries();
    list.size = static_cast<int>(entries.size());
    list.fill = [this, entries](int i, xb::Row &row)
    {
        const Snapshot &state = ctx_.store.state();
        const LibrarySnapshot &library = state.library;
        const XbEntry entry = entries[static_cast<std::size_t>(i)];
        row.has_icon = true;
        if (section_ == 1 && selected_.empty() && entry.kind != 'j')
        {
            fill_files_row(entry, row);
            return;
        }
        switch (entry.kind)
        {
        case 'L':
        {
            // Not read yet, or not answering.
            const bool busy = !state.have_library || library.busy;
            row.key = "connect";
            row.icon = icons::Icon::books;
            row.title = busy ? tr("Finding your games") : tr("Connect your library");
            row.sub = busy ? tr("Reading installed titles and drive availability from ShadowMount.")
                           : tr("Library uses ShadowMount\xE2\x80\x99s local games API on your PS5. "
                                "Start a version with that API enabled, then select Try again.");
            if (!busy)
            {
                row.text = tr("Games are not inferred from download history. An unavailable library "
                              "does not mean your games are missing.");
                if (!state.library_error.empty())
                    row.detail = tr(std::string_view(state.library_error));
                row.hint = tr("Try again");
            }
            break;
        }
        case 'j':
        {
            // The copy or move under way, pinned at the top.
            const StorageJob &job = library.storage_job;
            std::string title = job.title_id;
            for (const LibraryGame &g : library.games)
                if (g.title_id == job.title_id)
                    title = g.title;
            row.key = "job";
            row.primary = true;
            row.icon = job.operation == "move" ? icons::Icon::move : icons::Icon::copy;
            row.title = job.operation == "move" ? tr("Move {title}", {{"title", title}}) : tr("Copy {title}", {{"title", title}});
            if (!job.destination.empty())
                row.sub = tr("To {path}", {{"path", job.destination}});
            const float progress = job.state == "completed" ? 1.0f
                                   : job.total > 0.0       ? static_cast<float>(std::min(1.0, job.processed / job.total))
                                                           : 0.0f;
            row.bar = progress;
            row.detail = (job.total > 0.0 ? tr("{done} of {total}", {{"done", format_bytes(job.processed)},
                                                                      {"total", format_bytes(job.total)}}) +
                                                " \xC2\xB7 "
                                          : std::string()) +
                         i18n::percent(progress * 100.0);
            if (job.cancel_requested)
                row.detail += " \xC2\xB7 " + std::string(tr("Cancelling\xE2\x80\xA6"));
            if (!job.error.empty())
            {
                row.text = tr(std::string_view(job.error));
                row.warn = true;
            }
            break;
        }
        case 's':
            row.key = "stale";
            row.icon = icons::Icon::history;
            row.title = last_refreshed(library);
            row.sub = tr("Showing the last successful result. Game and drive status may have changed.");
            row.warn = true;
            break;
        case 'm':
            row.key = "message";
            row.icon = icons::Icon::warn;
            row.title = tr(std::string_view(!state.library_error.empty() ? state.library_error : library.message));
            row.warn = true;
            break;
        case 'q':
            row.key = "search";
            row.icon = icons::Icon::search;
            row.title = query_.empty() ? std::string(tr("Search your library")) : "\xE2\x80\x9C" + query_ + "\xE2\x80\x9D";
            if (!query_.empty())
                row.sub = trn(static_cast<long>(rows_.size()), "{count} game", "{count} games");
            break;
        case 'l':
            row.key = "location";
            row.icon = icons::Icon::drive;
            row.title = tr("Location");
            row.value = location_.empty() ? std::string(tr("All locations")) : i18n::place(location_);
            break;
        case 'v':
        {
            row.key = "v:" + std::to_string(entry.index);
            row.icon = kViewIcons[entry.index];
            row.folder = true;
            row.title = tr(kViewNames[entry.index]);
            const long count = std::count_if(library.games.begin(), library.games.end(),
                                             [&](const LibraryGame &g) { return in_view(g, entry.index); });
            row.sub = trn(count, "{count} game", "{count} games");
            break;
        }
        case 'g':
        {
            const LibraryGame &game = library.games[static_cast<std::size_t>(rows_[static_cast<std::size_t>(entry.index)])];
            row.key = game_key(game);
            row.game = art_of(state, game.title_id);
            row.has_icon = row.game == nullptr;
            row.mark = row.game == nullptr;
            row.title = game.title;
            if (game.platform == "ps5" || game.platform == "ps4")
                row.tag = game.platform == "ps5" ? "PS5" : "PS4";
            row.sub = status_of(game) + " \xC2\xB7 " + (game.on_drive ? i18n::place(game.location) : std::string(tr("Drive disconnected")));
            row.warn = !game.on_drive;
            break;
        }
        case 'e':
            row.key = "empty";
            row.icon = icons::Icon::search;
            row.title = tr("No matching games");
            row.sub = tr("Try a different title, status or location.");
            row.hint = tr("Reset library filters");
            break;
        case 'n':
            row.key = "none";
            row.icon = icons::Icon::books;
            row.title = tr("No games reported yet");
            row.sub = tr("ShadowMount has not reported any installed or on-drive games yet.");
            break;
        case 'S':
        {
            double free = 0.0;
            for (const Drive &drive : state.drives)
                free += drive.free_bytes;
            row.key = "files";
            row.icon = icons::Icon::folder;
            row.folder = true;
            row.title = tr("Files");
            row.sub = trn(static_cast<long>(state.drives.size()), "{count} drive", "{count} drives") + " \xC2\xB7 " +
                      tr("{size} free", {{"size", format_bytes(free)}});
            break;
        }
        case 'U':
            row.key = "updates";
            row.icon = icons::Icon::update;
            row.title = state.game_update_status.checking ? tr("Checking\xE2\x80\xA6")
                        : state.game_updates > 0           ? std::string(tr("Updates")) + " (" + std::to_string(state.game_updates) + ")"
                                                           : tr("Check for updates");
            row.sub = tr("Installed games with a patch on Sony\xE2\x80\x99s servers");
            row.folder = state.game_updates > 0;
            row.off = state.game_update_status.checking;
            break;
        case 'P':
            row.key = "dlc";
            row.icon = icons::Icon::box;
            row.title = std::string(tr("DLC")) + " (" + std::to_string(state.game_dlc) + ")";
            row.sub = tr("DLC on your NAS that your installed games do not have yet");
            row.folder = true;
            break;
        case 'c':
            row.key = "scan";
            row.icon = icons::Icon::scan;
            row.title = tr("Scan for games");
            row.sub = library.scanning ? tr("Looking for new games\xE2\x80\xA6") : tr("Search your drives with ShadowMount");
            row.off = library.working();
            break;
        case 'x':
            row.key = "gone";
            row.icon = icons::Icon::warn;
            row.title = tr("This game is no longer reported");
            row.sub = tr("Go back to see the current list.");
            row.warn = true;
            break;
        case 'D':
            row.key = "details";
            row.icon = icons::Icon::info;
            row.title = tr("Details");
            row.sub = tr("This item supports viewing details only. ShadowMount does not manage "
                         "its source.");
            break;
        default:
        {
            // A game's actions: mount or unmount first, then copy and move.
            const LibraryGame *game = selected_game();
            const bool blocked = library.working() || ctx_.store.library_action().busy || library.stale || game == nullptr ||
                                 !game->on_drive;
            if (entry.kind == 'M')
            {
                row.key = "mount";
                row.primary = true;
                row.icon = game != nullptr && game->mounted ? icons::Icon::eject : icons::Icon::play;
                row.title = game != nullptr && game->mounted ? tr("Unmount game") : tr("Mount game");
                if (game != nullptr)
                    row.sub = std::string(game->mounted ? tr("Mounted") : tr("Not mounted")) + " \xC2\xB7 " +
                              (game->on_drive ? tr("Available on drive") : tr("Unavailable; the drive or source may be missing"));
                row.off = blocked;
            }
            else
            {
                row.key = entry.kind == 'C' ? "copy" : "move";
                row.icon = entry.kind == 'C' ? icons::Icon::copy : icons::Icon::move;
                row.title = entry.kind == 'C' ? tr("Copy to drive") : tr("Move to drive");
                row.off = blocked || (game != nullptr && game->mounted);
                if (game != nullptr && game->mounted)
                    row.sub = tr("Unmount this game before copying or moving its source.");
            }
            break;
        }
        }
    };
    return list;
}

xb::Band LibraryScreen::crossbar_band() const
{
    // The scan, or a game's unmount, copy or move, as Store's dialogs ask them.
    const Snapshot &state = ctx_.store.state();
    xb::Band band;
    if (scan_open_)
    {
        band.title = tr("Scan your drives?");
        band.body = tr("ShadowMount will search its configured locations for games. It may "
                       "register games on the console or mount detected sources. If a game is "
                       "running, the scan may wait until it is safe.");
        band.buttons = {tr("Start scan"), tr("Back")};
    }
    else if (const LibraryGame *game = selected_game(); game != nullptr && mode_ != 0)
    {
        if (mode_ == 1)
        {
            band.title = tr("Unmount this game?");
            band.body = tr("Close the game on your PS5 before unmounting its source.");
        }
        else
        {
            band.kicker = mode_ == 2 ? tr("Copy {title}", {{"title", game->title}}) : tr("Move {title}", {{"title", game->title}});
            band.title = mode_ == 2 ? tr("Copy to drive") : tr("Move to drive");
            band.body = std::string(mode_ == 3 ? tr("Move the source to another location. The original is removed "
                                                    "only after the move succeeds.")
                                               : tr("Create a second copy on the selected drive. The original stays "
                                                    "in place.")) +
                        " " +
                        tr("Orbit checks the source and free space before starting. Pause "
                           "active and queued downloads first. Existing destination files "
                           "are not overwritten.");
            band.note = tr("From {path}", {{"path", game->path}});
            for (const LibraryDrive *drive : destinations())
            {
                xb::MenuItem option;
                option.label = i18n::place(drive->label) + " \xC2\xB7 " + drive->path + " \xC2\xB7 " +
                               tr("{size} free", {{"size", format_bytes(drive->free)}});
                option.checked = drive->id == destination_id_;
                band.options.push_back(std::move(option));
            }
            if (band.options.empty())
            {
                const bool fresh = state.have_library_storage && !state.library_storage.busy;
                band.busy = !fresh && state.library_storage.error.empty();
                band.error = !state.library_storage.error.empty()
                                 ? std::string(tr(std::string_view(state.library_storage.error)))
                             : fresh ? std::string(tr("Connect another writable drive, or configure another scan "
                                                      "location in ShadowMount."))
                                     : std::string(tr("Checking available destinations\xE2\x80\xA6"));
            }
        }
        const char *confirm = ctx_.store.library_action().busy ? tr("Starting\xE2\x80\xA6")
                              : mode_ == 1                     ? tr("Confirm unmount")
                              : mode_ == 2                     ? tr("Start copy")
                                                               : tr("Confirm move");
        band.buttons = {confirm, tr("Back")};
    }
    if (!error_.empty())
        band.error = tr(std::string_view(error_));
    band.focus = xb_band_;
    return band;
}

std::vector<xb::MenuItem> LibraryScreen::crossbar_menu() const
{
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    std::vector<xb::MenuItem> items;
    if (xb_menu_kind_ == 0)
    {
        // Location, with how many games each holds.
        const std::vector<std::string> values = [&]
        {
            std::vector<std::string> out;
            for (const LibraryGame &game : library.games)
                if (!game.location.empty() && std::find(out.begin(), out.end(), game.location) == out.end())
                    out.push_back(game.location);
            std::sort(out.begin(), out.end());
            return out;
        }();
        items.push_back(xb::choice(tr("All locations"), location_.empty()));
        for (const std::string &value : values)
        {
            const long count = std::count_if(library.games.begin(), library.games.end(),
                                             [&](const LibraryGame &g) { return g.location == value; });
            items.push_back(xb::choice(i18n::place(value), value == location_, trn(count, "{count} game", "{count} games")));
        }
    }
    else if (xb_menu_kind_ == 1)
    {
        const StorageJob &job = library.storage_job;
        items.push_back(xb::action(icons::Icon::stop, tr("Cancel operation"),
                                   job.active && job.cancellable && !job.cancel_requested && library.can("storage_job_cancel") &&
                                       !ctx_.store.library_action().busy));
        items.push_back(xb::action(icons::Icon::info, tr("Details"),
                                   std::any_of(library.games.begin(), library.games.end(),
                                               [&](const LibraryGame &g) { return g.title_id == job.title_id; })));
    }
    else if (const FileEntry *entry = file_entry(files_pick_))
    {
        // An entry in Files: every action, the way in first.
        for (const FileAct &act : file_actions(*entry))
        {
            xb::MenuItem item = xb::action(act.icon, act.label, act.enabled);
            item.danger = act.key == 'd';
            items.push_back(std::move(item));
        }
    }
    return items;
}

void LibraryScreen::leave_crossbar(Feedback &feedback)
{
    // Out of a game, a status view or Files, back to the row that opened it.
    std::string to;
    if (!selected_.empty())
    {
        if (const LibraryGame *game = selected_game())
            to = game_key(*game);
        selected_.clear();
        mode_ = 0;
        error_.clear();
    }
    else if (section_ == 1)
    {
        leave_files();
        to = "files";
    }
    else if (view_ != 0)
    {
        to = "v:" + std::to_string(view_);
        view_ = 0;
        refilter();
    }
    xb_.focus_key(crossbar_list(), to);
    feedback.play(hui::audio::Cue::back);
}

void LibraryScreen::update_band(const InputFrame &input, Feedback &feedback)
{
    // A question: its options (destinations) down, its buttons across.
    const LibrarySnapshot &library = ctx_.store.state().library;
    const xb::Band question = crossbar_band();
    const int options = static_cast<int>(question.options.size());
    const int count = options + static_cast<int>(question.buttons.size());
    int to = xb_band_;
    if (input.nav == Direction::down)
        to = xb_band_ < options ? xb_band_ + 1 : -1;
    else if (input.nav == Direction::up)
        to = xb_band_ >= options ? options - 1 : xb_band_ - 1;
    else if (input.nav == Direction::right)
        to = xb_band_ >= options && xb_band_ + 1 < count ? xb_band_ + 1 : -1;
    else if (input.nav == Direction::left)
        to = xb_band_ > options ? xb_band_ - 1 : -1;
    if (input.nav != Direction::none)
    {
        if (to >= 0 && to < count)
        {
            xb_band_ = to;
            feedback.play(hui::audio::Cue::focus);
        }
        else
            refuse(feedback, input);
    }
    const bool back = input.is_pressed(Action::back) || (input.is_pressed(Action::confirm) && xb_band_ == count - 1);
    if (back)
    {
        scan_open_ = false;
        mode_ = 0;
        error_.clear();
        feedback.play(hui::audio::Cue::modal_close);
    }
    else if (input.is_pressed(Action::confirm) && xb_band_ < options)
    {
        const std::vector<const LibraryDrive *> targets = destinations();
        destination_id_ = targets[static_cast<std::size_t>(xb_band_)]->id;
        feedback.play(hui::audio::Cue::toggle);
    }
    else if (input.is_pressed(Action::confirm))
    {
        const LibraryGame *game = selected_game();
        const bool blocked = library.working() || ctx_.store.library_action().busy;
        if (scan_open_)
        {
            if (blocked)
                refuse(feedback, input);
            else
                run_library("scan", true, nullptr, {}, 0, feedback);
        }
        else if (game == nullptr || blocked || library.stale || !game->on_drive || (mode_ >= 2 && destination_id_.empty()))
            refuse(feedback, input);
        else
            run_library(mode_ == 1 ? "unmount" : mode_ == 2 ? "copy" : "move", true, game,
                        mode_ >= 2 ? destination_id_ : std::string(), 0, feedback);
    }
}

Intent LibraryScreen::update_crossbar(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    Intent intent;
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    // Rows that come and go above the focus in Files (Paste here, the operation)
    // leave it on its row, whether they change between frames or with this input.
    const std::string place = files_drive_ + ":" + files_path_;
    std::string before;
    if (section_ == 1)
    {
        const xb::List now = crossbar_list();
        if (!xb_key_.empty() && now.size > 0 && xb_.focused_key(now) != xb_key_)
            xb_.focus_key(now, xb_key_);
        before = xb_.focused_key(crossbar_list());
    }
    const xb::List list = crossbar_list();
    xb_.clamp(list);
    xb::Row row;
    if (list.size > 0)
        list.fill(xb_.index, row);
    const bool band = scan_open_ || (!selected_.empty() && mode_ != 0);
    if (focused && keyboard_open_)
        update_keyboard(input, feedback);
    else if (focused && band)
        update_band(input, feedback);
    else if (focused && section_ == 1 && selected_.empty() && update_files_crossbar(input, feedback, row))
    {
        // Files used it.
    }
    else if (focused && xb_menu_.open)
    {
        const int chosen = xb_menu_.update(input, crossbar_menu(), feedback);
        if (chosen >= 0 && xb_menu_kind_ == 0)
        {
            std::vector<std::string> values;
            for (const LibraryGame &game : library.games)
                if (!game.location.empty() && std::find(values.begin(), values.end(), game.location) == values.end())
                    values.push_back(game.location);
            std::sort(values.begin(), values.end());
            location_ = chosen > 0 && chosen <= static_cast<int>(values.size()) ? values[static_cast<std::size_t>(chosen - 1)]
                                                                                 : std::string();
            refilter();
        }
        else if (chosen == 0 && xb_menu_kind_ == 1)
            run_library("cancel", false, nullptr, {}, library.storage_job.id, feedback);
        else if (chosen == 1 && xb_menu_kind_ == 1)
        {
            for (const LibraryGame &game : library.games)
                if (game.title_id == library.storage_job.title_id && selected_.empty())
                {
                    select_game(game.title_id, game.source_key);
                    xb_.index = 0;
                    xb_.snap();
                }
        }
    }
    else if (focused)
    {
        if (input.nav == Direction::up || input.nav == Direction::down)
            xb_.move(list, input, feedback);
        else if (input.is_pressed(Action::back) && (!selected_.empty() || section_ == 1 || view_ != 0))
            leave_crossbar(feedback);
        else if (input.is_pressed(Action::west) && selected_.empty() && section_ == 0 && state.have_library && library.ready())
        {
            keyboard_open_ = true;
            keyboard_.enter();
            keyboard_.set_length(static_cast<int>(query_.size()));
            feedback.play(hui::audio::Cue::modal_open);
        }
        else if (input.is_pressed(Action::confirm) && list.size > 0)
        {
            const std::string &key = row.key;
            const LibraryGame *game = selected_game();
            if (row.off)
                refuse(feedback, input);
            else if (key == "connect")
            {
                if (!state.have_library || library.busy)
                    refuse(feedback, input);
                else
                {
                    ctx_.store.refresh_library();
                    feedback.play(hui::audio::Cue::select);
                }
            }
            else if (key == "job")
            {
                xb_menu_kind_ = 1;
                xb_menu_.show(0);
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (key == "search")
            {
                keyboard_open_ = true;
                keyboard_.enter();
                keyboard_.set_length(static_cast<int>(query_.size()));
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (key == "location")
            {
                xb_menu_kind_ = 0;
                int at = 0;
                const std::vector<xb::MenuItem> items = crossbar_menu();
                for (int i = 0; i < static_cast<int>(items.size()); ++i)
                    if (items[static_cast<std::size_t>(i)].checked)
                        at = i;
                xb_menu_.show(at);
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (key.rfind("v:", 0) == 0)
            {
                view_ = key[2] - '0';
                refilter();
                xb_.index = 0;
                xb_.snap();
                feedback.play(hui::audio::Cue::open);
            }
            else if (key.rfind("g:", 0) == 0)
            {
                const std::size_t bar = key.find('|');
                show_game(key.substr(2, bar - 2), key.substr(bar + 1));
                xb_.index = 0;
                xb_.snap();
                feedback.play(hui::audio::Cue::open);
            }
            else if (key == "empty")
            {
                view_ = 0;
                query_.clear();
                location_.clear();
                format_.clear();
                refilter();
                feedback.play(hui::audio::Cue::toggle);
            }
            else if (key == "updates")
            {
                if (state.game_updates > 0)
                {
                    intent.kind = Intent::Kind::open_updates;
                    intent.tab = 1;
                    feedback.play(hui::audio::Cue::open);
                }
                else if (!state.game_update_status.checking && !ctx_.store.setting_result().busy)
                {
                    ctx_.store.check_game_updates();
                    intent.toast = tr("Checking\xE2\x80\xA6");
                    feedback.play(hui::audio::Cue::select);
                }
                else
                    refuse(feedback, input);
            }
            else if (key == "dlc")
            {
                intent.kind = Intent::Kind::open_updates;
                intent.tab = 2;
                feedback.play(hui::audio::Cue::open);
            }
            else if (key == "files")
            {
                section_ = 1;
                ctx_.store.want_files(true);
                open_folder({}, {});
                feedback.play(hui::audio::Cue::open);
            }
            else if (key == "scan")
            {
                scan_open_ = true;
                xb_band_ = 0;
                error_.clear();
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (key == "mount" && game != nullptr)
            {
                if (!game->mounted)
                    run_library("mount", false, game, {}, 0, feedback);
                else
                {
                    mode_ = 1;
                    xb_band_ = 0;
                    feedback.play(hui::audio::Cue::modal_open);
                }
            }
            else if ((key == "copy" || key == "move") && game != nullptr)
            {
                mode_ = key == "copy" ? 2 : 3;
                destination_id_.clear();
                xb_band_ = 0;
                ctx_.store.want_library_storage(true, true);
                feedback.play(hui::audio::Cue::modal_open);
            }
            else
                refuse(feedback, input);
        }
    }
    keyboard_.set_bounds(xb::keyboard_keys());
    keyboard_.set_active(keyboard_open_);
    keyboard_.update(dt);
    keyboard_in_.target = keyboard_open_ ? 1.0f : 0.0f;
    keyboard_in_.update(dt, 14.0f);
    xb_band_in_.target = !keyboard_open_ && (scan_open_ || (!selected_.empty() && mode_ != 0) || file_dialog_ != 0 || activity_open()) ? 1.0f : 0.0f;
    xb_band_in_.update(dt, 16.0f);
    xb_.animate(dt);
    xb_menu_.animate(dt);
    if (section_ == 1 && selected_.empty() && input.nav == Direction::none && !before.empty() &&
        place == files_drive_ + ":" + files_path_)
    {
        const xb::List after = crossbar_list();
        if (xb_.focused_key(after) != before)
            xb_.focus_key(after, before);
    }
    xb_key_ = section_ == 1 ? xb_.focused_key(crossbar_list()) : std::string();
    return intent;
}

void LibraryScreen::draw_crossbar(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    const ui::Type &type = ctx_.type;
    // The bar: a game, a status view or Files takes Library's place.
    std::vector<xb::BarItem> bar = xb::sections(ctx_, false);
    int current = section::library;
    const LibraryGame *game = selected_game();
    if (!selected_.empty() || section_ == 1 || view_ != 0)
    {
        xb::BarItem here;
        if (!selected_.empty())
        {
            here.game = game != nullptr ? art_of(state, game->title_id) : nullptr;
            here.icon = icons::Icon::box;
            here.label = game != nullptr ? game->title : std::string();
        }
        else if (section_ == 1)
        {
            here.icon = icons::Icon::folder;
            here.label = files_drive_.empty() ? std::string(tr("Files")) : files_title();
        }
        else
        {
            here.icon = kViewIcons[view_];
            here.label = tr(kViewNames[view_]);
        }
        bar = {bar[section::library], here};
        current = 1;
    }
    xb::draw_bar(ctx_, list, bar, current);
    const xb::List rows = crossbar_list();
    xb_.draw(ctx_, list, rows);
    xb::Row row;
    if (rows.size > 0)
        rows.fill(std::clamp(xb_.index, 0, rows.size - 1), row);
    const float hide = std::max({xb_menu_.in.value, keyboard_in_.value});
    list.push_opacity(1.0f - hide);
    if (!selected_.empty() && game != nullptr)
    {
        // The game's statuses, as Store's details list them.
        std::vector<std::pair<std::string, std::string>> facts = {
            {tr("Installation"), game->installed ? tr("Registered on the console") : tr("Not installed on the console")},
            {tr("Mount status"), game->mounted ? tr("Mounted") : tr("Not mounted")},
            {tr("Source"), game->on_drive ? tr("Available on drive") : tr("Unavailable; the drive or source may be missing")},
            {tr("Format"), game->format},
            {tr("Location"), i18n::place(game->location)},
            {tr("Source size"), game->size >= 0.0                     ? format_bytes(game->size)
                                : game->size_status == "unavailable" ? std::string(tr("Unavailable"))
                                : library.measuring                  ? std::string(tr("Measuring\xE2\x80\xA6"))
                                                                     : std::string(tr("Not measured"))},
            {tr("Size reported by console"), game->installed_size < 0.0 ? std::string(tr("Not reported")) : format_bytes(game->installed_size)}};
        const std::string kicker = (game->platform == "ps5"   ? std::string("PS5")
                                    : game->platform == "ps4" ? std::string("PS4")
                                                              : std::string(tr("Platform unknown"))) +
                                   " \xC2\xB7 " + game->title_id;
        std::vector<std::string> notes;
        if (library.has_action && library.action.title_id == game->title_id && !library.action.message.empty())
            notes.push_back(std::string(tr(std::string_view(library.action.message))));
        if (library.stale)
            notes.push_back(tr("Saved snapshot. These statuses may have changed."));
        if (game->found_by_orbit)
            notes.push_back(tr("Orbit found this game itself because your ShadowMount version doesn\xE2\x80\x99t list games "
                               "installed from PKG files. For the most complete list, update ShadowMount to 1.7beta5 or newer."));
        if (!error_.empty() && mode_ == 0)
            notes.push_back(std::string(tr(std::string_view(error_))));
        xb::draw_panel(list, 400.0f, 0.0f, 0.0f, [&](hui::gfx::DrawList &out, float x, float y, float w)
        {
            y = xb::draw_label(ctx_, out, x, y, kicker);
            y = xb::draw_facts(ctx_, out, x, y + 18.0f, w, facts, 2);
            ui::text(out, type.regular, tr("Source path"), x, y + 22.0f + 14.4f + 5.9f, 18.0f, ui::Color::rgb(0xffffff, 0.62f));
            y += 22.0f + 28.8f + 4.0f;
            const std::string path = game->path.empty() ? std::string(tr("Not reported")) : game->path;
            y = ui::paragraph(out, type.regular, path, x, y + 14.4f + 5.9f, 18.0f, w, 26.0f, ui::Color::rgb(0xffffff), 3) - 20.3f + 26.0f;
            // What the statuses mean is on Library's own panel; here, only what needs attention.
            for (const std::string &note : notes)
                y = ui::paragraph(out, type.regular, note, x, y + 16.0f + 12.0f + 5.3f, 17.0f, w, 24.0f, ui::Color::rgb(0xffd27a), 3) -
                    17.3f + 24.0f + 4.0f;
            return y;
        });
    }
    else if (section_ == 1)
        draw_files_crossbar(frame, row);
    else if (library.ready())
    {
        // The snapshot: when it was read, what the statuses mean, the drives' space.
        std::vector<std::pair<std::string, std::string>> drives;
        for (const Drive &drive : state.drives)
            drives.emplace_back(i18n::place(drive.label), tr("{size} free", {{"size", format_bytes(drive.free_bytes)}}));
        xb::draw_panel(list, 430.0f, 0.0f, 0.0f, [&](hui::gfx::DrawList &out, float x, float y, float w)
        {
            y = xb::draw_label(ctx_, out, x, y, last_refreshed(library));
            const std::string text = tr("Installed means registered on the console. Mounted means its source is "
                                        "currently mounted. On drive means the source is available. These "
                                        "statuses do not confirm that a game will launch.");
            y = ui::paragraph(out, type.regular, text, x, y + 12.0f + 15.5f + 6.6f, 20.0f, w, 31.0f, ui::Color::rgb(0xffffff, 0.82f), 5) -
                22.1f + 31.0f;
            y += 22.0f;
            for (const auto &[label, value] : drives)
            {
                ui::text_fit(out, type.regular, label, x, y + 12.8f + 5.3f, 16.0f, w, ui::Color::rgb(0xffffff, 0.62f));
                ui::text_fit(out, type.regular, value, x, y + 29.6f + 15.2f + 6.3f, 19.0f, w, ui::Color::rgb(0xffffff));
                y += 60.0f + 14.0f;
            }
            return y - 14.0f;
        });
    }
    list.pop_opacity();
    if (xb_menu_.in.value > 0.01f)
    {
        std::string title = tr("Location");
        if (xb_menu_kind_ == 1)
            title = tr("Storage");
        else if (xb_menu_kind_ == 3)
            title = files_pick_;
        xb::draw_menu(ctx_, frame.overlay, title, crossbar_menu(), xb_menu_);
    }
    xb::draw_band(ctx_, frame.overlay, file_dialog_ != 0 || activity_open() ? files_band() : crossbar_band(), xb_band_in_.value);
    if (keyboard_for_ == 2)
        xb::draw_keyboard(ctx_, frame, keyboard_,
                          file_dialog_ == 1   ? tr("Rename {name}", {{"name", file_dialog_entry_}})
                          : file_dialog_ == 2 ? std::string(tr("New folder"))
                                              : std::string(tr("Password")),
                          file_text_, file_dialog_ == 4, keyboard_in_.value);
    else if (keyboard_for_ == 1)
        xb::draw_keyboard(ctx_, frame, keyboard_, tr("Search this folder"), files_query_, false, keyboard_in_.value);
    else
        xb::draw_keyboard(ctx_, frame, keyboard_, tr("Search your library"), query_, false, keyboard_in_.value);
}

std::vector<hui::ui::Hint> LibraryScreen::crossbar_hints() const
{
    if (keyboard_open_)
        return {{hui::ui::Button::square, tr("Delete")},
                {hui::ui::Button::triangle, tr("Space")},
                {hui::ui::Button::circle, tr("Close")}};
    if (scan_open_ || (!selected_.empty() && mode_ != 0) || file_dialog_ != 0 || activity_open())
        return {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Back")}};
    if (xb_menu_.open)
        return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Close")}};
    const xb::List rows = crossbar_list();
    xb::Row row;
    if (rows.size > 0)
        rows.fill(std::clamp(xb_.index, 0, rows.size - 1), row);
    if (section_ == 1 && selected_.empty())
    {
        // Files: Cross opens, Options lists every action, Square searches the folder.
        std::vector<hui::ui::Hint> hints = {{hui::ui::Button::cross, tr("Open")}};
        if (row.key.rfind("e:", 0) == 0)
            hints.push_back({hui::ui::Button::options, tr("Actions")});
        if (!files_drive_.empty())
            hints.push_back({hui::ui::Button::square, tr("Search this folder")});
        hints.push_back({hui::ui::Button::triangle, tr("Refresh")});
        hints.push_back({hui::ui::Button::circle, tr("Back")});
        return hints;
    }
    if (!selected_.empty() || section_ == 1)
        return {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Back")}};
    std::vector<hui::ui::Hint> hints;
    if (row.key == "connect")
    {
        if (!row.hint.empty())
            hints.push_back({hui::ui::Button::cross, tr("Try again")});
    }
    else if (row.key == "empty")
        hints.push_back({hui::ui::Button::cross, tr("Reset library filters")});
    else if (row.key == "job")
        hints.push_back({hui::ui::Button::cross, tr("Actions")});
    else if (row.key == "location")
        hints.push_back({hui::ui::Button::cross, tr("Change")});
    else if (row.key != "none" && row.key != "message" && row.key != "stale")
        hints.push_back({hui::ui::Button::cross, tr("Open")});
    if (view_ != 0)
        hints.push_back({hui::ui::Button::circle, tr("Back")});
    else
        hints.push_back({hui::ui::Button::l1, tr("Categories"), hui::ui::Button::r1});
    return hints;
}

// Cinema shows Store's Library in its palette (ui::color::use_cinema), so L1 and
// R1 switch the app's tabs as they do in Store.
bool LibraryScreen::page(int step)
{
    (void)step;
    return false;
}

// ---------------------------------------------------------------- Panorama

const LibraryGame *LibraryScreen::panorama_game() const
{
    // The game chosen last in the games group, else the first one shown.
    const LibrarySnapshot &library = ctx_.store.state().library;
    for (const LibraryGame &g : library.games)
        if (game_key(g) == pn_game_)
            return &g;
    return rows_.empty() ? nullptr : &library.games[static_cast<std::size_t>(rows_.front())];
}

std::vector<std::pair<char, std::string>> LibraryScreen::panorama_actions(const LibraryGame &g) const
{
    // A game's own group: mount or unmount, copy, move, Details; an item
    // ShadowMount doesn't manage has Details only.
    const LibrarySnapshot &library = ctx_.store.state().library;
    std::vector<std::pair<char, std::string>> out;
    const bool transfer = g.managed && g.can_manage_source && g.on_drive;
    if (g.managed && library.can(g.mounted ? "unmount_game" : "mount_game"))
        out.emplace_back('M', g.mounted ? tr("Unmount game") : tr("Mount game"));
    if (transfer && library.can("copy_game_source") && library.can("storage_job_status"))
        out.emplace_back('C', tr("Copy to drive"));
    if (transfer && library.can("move_game_source") && library.can("storage_job_status"))
        out.emplace_back('V', tr("Move to drive"));
    out.emplace_back('D', tr("Details"));
    return out;
}

pano::Page LibraryScreen::panorama_page() const
{
    pano::Page page;
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    if (!state.have_library || !library.ready() || rows_.empty())
        return page;
    const bool blocked = library.working() || ctx_.store.library_action().busy || library.stale;

    // The games, then Search your library.
    pano::Group games;
    games.title = !query_.empty()    ? "\xE2\x80\x9C" + query_ + "\xE2\x80\x9D"
                  : !location_.empty() ? pano::lower(i18n::place(location_))
                                       : pano::lower(tr("All games"));
    games.count = i18n::count(static_cast<long>(rows_.size()));
    games.pattern = pano::Pattern::lead;
    games.size = static_cast<int>(rows_.size()) + 1;
    games.fill = [this](int i, pano::Tile &tile)
    {
        const Snapshot &now = ctx_.store.state();
        if (i >= static_cast<int>(rows_.size()))
        {
            tile.key = "search";
            tile.has_icon = true;
            tile.icon = icons::Icon::search;
            tile.label = tr("Search your library");
            return;
        }
        const LibraryGame &g = now.library.games[static_cast<std::size_t>(rows_[static_cast<std::size_t>(i)])];
        tile.key = game_key(g);
        tile.game = art_of(now, g.title_id);
        tile.name_on_focus = tile.game != nullptr;
        tile.label = g.title;
        tile.sub = status_of(g) + " \xC2\xB7 " + (g.on_drive ? i18n::place(g.location) : std::string(tr("Drive disconnected")));
        if (tile.game == nullptr)
        {
            // An item without catalogue art: a warm tile with its box.
            tile.has_bg = true;
            tile.bg = ui::Color::rgb(0x3a3326);
            tile.has_icon = true;
            tile.icon = icons::Icon::box;
        }
        tile.off = !g.on_drive;
        if (i == 0)
            tile.label_size = 30.0f;
    };
    page.groups.push_back(std::move(games));

    // The selected game's own group, or the copy or move under way.
    const LibraryGame *game = panorama_game();
    if (library.has_storage_job && library.storage_job.id > 0)
    {
        const StorageJob &j = library.storage_job;
        const LibraryGame *of = nullptr;
        for (const LibraryGame &g : library.games)
            if (g.title_id == j.title_id)
                of = &g;
        pano::Group job;
        job.title = pano::lower(of != nullptr ? of->title : j.title_id);
        job.pattern = pano::Pattern::custom;
        job.places = {{0, 0, 2, 2}, {0, 2, 1, 1}, {1, 2, 1, 1}};
        job.size = of != nullptr ? 3 : 2;
        const bool can_cancel = j.active && j.cancellable && !j.cancel_requested && library.can("storage_job_cancel") &&
                                !ctx_.store.library_action().busy;
        job.fill = [this, can_cancel](int i, pano::Tile &tile)
        {
            const Snapshot &now = ctx_.store.state();
            const StorageJob &jj = now.library.storage_job;
            if (i == 1 || i == 2)
            {
                tile.key = i == 1 ? "job:cancel" : "job:details";
                tile.has_icon = true;
                tile.icon = i == 1 ? icons::Icon::stop : icons::Icon::info;
                tile.label = i == 1 ? tr("Cancel operation") : tr("Details");
                if (i == 1)
                {
                    tile.has_bg = true;
                    tile.bg = look::accent_color();
                    tile.off = !can_cancel;
                }
                return;
            }
            tile.key = "job";
            tile.game = art_of(now, jj.title_id);
            tile.dim = 0.3f;
            tile.label_size = 26.0f;
            tile.kicker = std::string(jj.operation == "move" ? tr("Move") : tr("Copy")) +
                          (jj.destination.empty() ? "" : " \xC2\xB7 " + tr("To {path}", {{"path", jj.destination}}));
            for (const LibraryGame &g : now.library.games)
                if (g.title_id == jj.title_id)
                    tile.label = g.title;
            const float progress = jj.state == "completed" ? 1.0f : jj.total > 0.0 ? static_cast<float>(std::min(1.0, jj.processed / jj.total)) : 0.0f;
            tile.number = i18n::whole_percent(progress * 100.0f);
            tile.number_size = 110.0f;
            tile.sub = (jj.total > 0.0 ? tr("{done} of {total}", {{"done", format_bytes(jj.processed)}, {"total", format_bytes(jj.total)}}) : std::string()) +
                       (jj.cancel_requested ? " \xC2\xB7 " + std::string(tr("Cancelling\xE2\x80\xA6")) : std::string()) +
                       (jj.error.empty() ? std::string() : " \xC2\xB7 " + std::string(tr(std::string_view(jj.error))));
            tile.bar = progress;
            tile.bar_bad = !jj.error.empty();
        };
        page.groups.push_back(std::move(job));
    }
    else if (game != nullptr)
    {
        const std::vector<std::pair<char, std::string>> actions = panorama_actions(*game);
        pano::Group g;
        g.title = pano::lower(game->title);
        g.pattern = pano::Pattern::custom;
        // The first action wide in the accent; the rest under it, two by two.
        g.places = {{0, 0, 2, 1}, {0, 1, 1, 1}, {1, 1, 1, 1}, {0, 2, 1, 1}, {1, 2, 1, 1}};
        // With details open, only the game's own actions stay.
        int shown = static_cast<int>(actions.size());
        if (pn_details_)
            shown = static_cast<int>(std::count_if(actions.begin(), actions.end(), [](const auto &a) { return a.first != 'D'; }));
        g.size = std::max(1, std::min(shown, 5));
        const std::string status = (game->platform == "ps5" ? std::string("PS5") : game->platform == "ps4" ? std::string("PS4") : std::string(tr("Platform unknown"))) +
                                   " \xC2\xB7 " + status_of(*game) + " \xC2\xB7 " + game->title_id;
        const std::string line = status_of_line(*game);
        const bool mounted = game->mounted, on_drive = game->on_drive, managed = game->managed;
        const bool stale = library.stale;
        g.fill = [actions, status, line, blocked, mounted, on_drive, stale](int i, pano::Tile &tile)
        {
            if (i >= static_cast<int>(actions.size()))
                return;
            const char kind = actions[static_cast<std::size_t>(i)].first;
            tile.key = std::string("act:") + kind;
            tile.has_icon = true;
            tile.label = actions[static_cast<std::size_t>(i)].second;
            tile.icon = kind == 'M'   ? (mounted ? icons::Icon::eject : icons::Icon::play)
                        : kind == 'C' ? icons::Icon::copy
                        : kind == 'V' ? icons::Icon::move
                                      : icons::Icon::info;
            if (i == 0)
            {
                // The first: what it does to this game, and where the game is.
                tile.sub = stale ? std::string(tr("Showing the last successful result. Game and drive status may have changed."))
                           : kind == 'D' ? status
                                         : line;
                tile.has_bg = !stale;
                tile.bg = look::accent_color();
            }
            tile.off = (kind == 'M' && (blocked || !on_drive)) || ((kind == 'C' || kind == 'V') && (blocked || mounted));
        };
        if (!managed)
        {
            g.below_rows = 2;
            g.cols = 4;
            g.below = [this](hui::gfx::DrawList &list, float x, float top, float width)
            {
                pano::draw_text(ctx_, &list, x, top, width, tr("This item supports viewing details only. ShadowMount does not manage its source."),
                                19.0f, ui::Color::rgb(0xffffff, 0.8f), pano::Face::body, 1.55f, 4);
            };
        }
        page.groups.push_back(std::move(g));
    }

    if (pn_details_ && game != nullptr)
    {
        // The statuses, where the game lives (the one being copied or moved,
        // while that runs), its path and what the words mean.
        const LibraryGame copy = *game;
        pano::Group details;
        details.title = pano::lower(tr("Library game details"));
        details.cols = 4;
        details.below_rows = pano::kRows;
        details.below = [this, copy](hui::gfx::DrawList &list, float x, float top, float width)
        {
            const LibraryGame &gg = copy;
            const std::vector<std::pair<std::string, std::string>> status = {
                {tr("Installation"), gg.installed ? tr("Registered on the console") : tr("Not installed on the console")},
                {tr("Mount status"), gg.mounted ? tr("Mounted") : tr("Not mounted")},
                {tr("Source"), gg.on_drive ? tr("Available on drive") : tr("Unavailable; the drive or source may be missing")},
                {tr("Format"), gg.format}};
            const std::vector<std::pair<std::string, std::string>> where = {
                {tr("Location"), i18n::place(gg.location)},
                {tr("Source size"), gg.size >= 0.0 ? format_bytes(gg.size)
                                    : gg.size_status == "unavailable" ? std::string(tr("Unavailable"))
                                    : ctx_.store.state().library.measuring ? std::string(tr("Measuring\xE2\x80\xA6"))
                                                                           : std::string(tr("Not measured"))},
                {tr("Size reported by console"), gg.installed_size < 0.0 ? std::string(tr("Not reported")) : format_bytes(gg.installed_size)}};
            float y = top - 10.0f;
            y += pano::draw_caps(ctx_, &list, x, y, tr("Status"));
            y += pano::draw_pairs(ctx_, &list, x, y - 16.0f, width, status, 4) - 16.0f + 26.0f;
            y += pano::draw_caps(ctx_, &list, x, y, tr("Where it lives"));
            y += pano::draw_pairs(ctx_, &list, x, y - 16.0f, width, where, 4) - 16.0f + 20.0f;
            y += pano::draw_text(ctx_, &list, x, y, width, tr("Source path"), 15.0f, pano::kFaint);
            y += 4.0f + pano::draw_text(ctx_, &list, x, y + 4.0f, width, gg.path.empty() ? std::string(tr("Not reported")) : gg.path, 20.0f,
                                        pano::kInk, pano::Face::medium, 1.6f, 2);
            if (gg.found_by_orbit)
                y += 18.0f + pano::draw_text(ctx_, &list, x, y + 18.0f, width,
                                             tr("Orbit found this game itself because your ShadowMount version doesn\xE2\x80\x99t list games "
                                                "installed from PKG files. For the most complete list, update ShadowMount to 1.7beta5 or newer."),
                                             17.0f, pano::kAmber, pano::Face::body, 1.5f, 3);
            pano::draw_text(ctx_, &list, x, y + 18.0f, width,
                            tr("Installed means registered on the console. Mounted means its source is currently mounted. On drive means "
                               "the source is available. These statuses do not confirm that a game will launch."),
                            17.0f, pano::kFaint, pano::Face::body, 1.5f, 4);
        };
        page.groups.push_back(std::move(details));
    }

    // Files' drives (Cross opens one), then the games' scan and sizes.
    files_drive_groups(page, true);
    return page;
}

void LibraryScreen::panorama_run(char action, const LibraryGame *game, Feedback &feedback, const InputFrame &input)
{
    // What a tile (or the app bar) does, as the other interfaces do it.
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    const bool blocked = library.working() || ctx_.store.library_action().busy || library.stale;
    switch (action)
    {
    case 'M':
        if (game == nullptr || blocked || !game->on_drive)
            refuse(feedback, input);
        else if (!game->mounted)
            run_library("mount", false, game, {}, 0, feedback);
        else
        {
            show_game(game->title_id, game->source_key);
            mode_ = 1;
            xb_band_ = 0;
            feedback.play(hui::audio::Cue::modal_open);
        }
        break;
    case 'C':
    case 'V':
        if (game == nullptr || blocked || game->mounted)
            refuse(feedback, input);
        else
        {
            show_game(game->title_id, game->source_key);
            mode_ = action == 'C' ? 2 : 3;
            destination_id_.clear();
            xb_band_ = 0;
            ctx_.store.want_library_storage(true, true);
            feedback.play(hui::audio::Cue::modal_open);
        }
        break;
    case 'D':
        // The statuses open beside the game's own actions, the first one focused.
        pn_details_ = true;
        pano_.focus = {1, 0};
        feedback.play(hui::audio::Cue::open);
        break;
    case 'S':
        pn_picker_ = 0;
        feedback.play(hui::audio::Cue::modal_open);
        break;
    case 'c':
        if (library.working())
            refuse(feedback, input);
        else
        {
            scan_open_ = true;
            xb_band_ = 0;
            error_.clear();
            feedback.play(hui::audio::Cue::modal_open);
        }
        break;
    case 'o':
        // A drive's tile: Files, in that drive.
        if (files_pick_.empty())
            refuse(feedback, input);
        else
        {
            section_ = 1;
            ctx_.store.want_files(true);
            open_folder(files_pick_, {});
            feedback.play(hui::audio::Cue::open);
        }
        break;
    case 'a':
        show_activity(feedback);
        break;
    case 'x':
    {
        const StorageJob &j = library.storage_job;
        if (!j.active || !j.cancellable || j.cancel_requested || !library.can("storage_job_cancel") || ctx_.store.library_action().busy)
            refuse(feedback, input);
        else
            run_library("cancel", false, nullptr, {}, j.id, feedback);
        break;
    }
    default:
        refuse(feedback, input);
        break;
    }
}

pano::AppBar LibraryScreen::panorama_bar(const pano::Page &page) const
{
    // Everything for the focused place: a game's actions, a drive's, the
    // copy under way; then search and the scan.
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    const std::string key = pano_.focused_key(page);
    pano::AppBar bar;
    const auto add = [&](char kind, std::string title, bool off = false, std::string sub = {})
    {
        pano::Item item;
        item.title = std::move(title);
        item.sub = std::move(sub);
        item.off = off;
        item.tag = kind;
        bar.items.push_back(std::move(item));
    };
    if (key.rfind("drive:", 0) == 0)
    {
        bar.head = tr("Files");
        add('o', tr("Open drive"));
    }
    else if (key.rfind("job", 0) == 0)
    {
        bar.head = tr("Library");
        add('x', tr("Cancel operation"));
    }
    else if (const LibraryGame *g = panorama_game())
    {
        bar.head = g->title;
        for (const auto &a : panorama_actions(*g))
            add(a.first, a.second);
    }
    add('U', state.game_updates > 0 ? std::string(tr("Updates")) + " (" + std::to_string(state.game_updates) + ")"
                                    : std::string(tr("Check for updates")),
        state.game_update_status.checking);
    if (state.game_dlc > 0)
        add('P', std::string(tr("DLC")) + " (" + std::to_string(state.game_dlc) + ")");
    add('S', tr("Search your library"), false, query_.empty() ? std::string() : "\xE2\x80\x9C" + query_ + "\xE2\x80\x9D");
    if (library.can("rescan"))
        add('c', tr("Scan for games"), library.working());
    bar.focus = pn_bar_focus_;
    return bar;
}

Intent LibraryScreen::update_panorama(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    Intent intent;
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    pano::keyboard_look(keyboard_.style);
    keyboard_.set_bounds(pano::keyboard_keys());
    // The page shows Files' drives and the operation too: keep them coming while it's here.
    ctx_.store.want_library_storage(true);
    ctx_.store.want_files(true);
    const bool band = scan_open_ || (!selected_.empty() && mode_ != 0);
    if (!band && !selected_.empty())
        selected_.clear();
    pano::Page page = panorama_page();
    pano_.clamp(page);
    const std::string key = pano_.focused_key(page);
    const LibraryGame *game = panorama_game();
    if (focused && keyboard_open_)
        update_keyboard(input, feedback);
    else if (focused && band)
    {
        // A question: destination tiles across, its buttons under them.
        const xb::Band question = crossbar_band();
        const int options = static_cast<int>(question.options.size());
        InputFrame step = input;
        if (input.nav != Direction::none)
        {
            int to = xb_band_;
            if (xb_band_ < options)
                to = input.nav == Direction::left ? xb_band_ - 1 : input.nav == Direction::right ? xb_band_ + 1 : input.nav == Direction::down ? options : -1;
            else
                to = input.nav == Direction::up ? (options > 0 ? 0 : -1)
                     : input.nav == Direction::left ? (xb_band_ > options ? xb_band_ - 1 : -1)
                     : input.nav == Direction::right ? xb_band_ + 1
                                                     : -1;
            if (to >= 0 && to < options + static_cast<int>(question.buttons.size()) && (xb_band_ >= options || to < options || input.nav == Direction::down))
            {
                xb_band_ = to;
                feedback.play(hui::audio::Cue::focus);
            }
            else
                refuse(feedback, input);
            step.nav = Direction::none;
        }
        update_band(step, feedback);
    }
    else if (focused && (file_dialog_ != 0 || activity_open()))
        update_files_band(input, feedback);
    else if (focused && section_ == 1)
        update_files_panorama(input, feedback);
    else if (focused && pn_picker_ >= 0)
    {
        // Search your library: the field, then every location.
        std::vector<std::string> values;
        for (const LibraryGame &g : library.games)
            if (!g.location.empty() && std::find(values.begin(), values.end(), g.location) == values.end())
                values.push_back(g.location);
        std::sort(values.begin(), values.end());
        const int count = 2 + static_cast<int>(values.size());
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int to = pn_picker_ + (input.nav == Direction::down ? 1 : -1);
            if (to >= 0 && to < count)
            {
                pn_picker_ = to;
                feedback.play(hui::audio::Cue::focus);
            }
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::back))
        {
            pn_picker_ = -1;
            feedback.play(hui::audio::Cue::modal_close);
        }
        else if (input.is_pressed(Action::confirm))
        {
            if (pn_picker_ == 0)
            {
                keyboard_open_ = true;
                keyboard_.enter();
                keyboard_.set_length(static_cast<int>(query_.size()));
                feedback.play(hui::audio::Cue::modal_open);
            }
            else
            {
                location_ = pn_picker_ > 1 ? values[static_cast<std::size_t>(pn_picker_ - 2)] : std::string();
                refilter();
                pano_.focus = {};
                pn_picker_ = -1;
                feedback.play(hui::audio::Cue::select);
            }
        }
    }
    else if (focused && pn_bar_open_)
    {
        pano::AppBar bar = panorama_bar(page);
        const int chosen = pano::update_appbar(bar, input, feedback);
        pn_bar_focus_ = bar.focus;
        if (chosen != -1)
            pn_bar_open_ = false;
        if (chosen >= 0)
        {
            const char kind = static_cast<char>(bar.items[static_cast<std::size_t>(chosen)].tag);
            if (kind == 'U' && state.game_updates > 0)
            {
                intent.kind = Intent::Kind::open_updates;
                intent.tab = 1;
                feedback.play(hui::audio::Cue::open);
            }
            else if (kind == 'U')
            {
                if (!state.game_update_status.checking && !ctx_.store.setting_result().busy)
                {
                    ctx_.store.check_game_updates();
                    intent.toast = tr("Checking\xE2\x80\xA6");
                }
                else
                    refuse(feedback, input);
            }
            else if (kind == 'P')
            {
                intent.kind = Intent::Kind::open_updates;
                intent.tab = 2;
                feedback.play(hui::audio::Cue::open);
            }
            else
                panorama_run(kind, game, feedback, input);
        }
    }
    else if (focused)
    {
        if (!state.have_library || !library.ready() || rows_.empty())
        {
            // Not answering: Cross tries again; nothing matches: Cross resets the
            // filters. Files, beside it (or alone), opens the drives.
            const bool only_files = !state.have_library || library.busy || (library.ready() && library.games.empty());
            if (input.nav == Direction::left || input.nav == Direction::right)
            {
                const int to = input.nav == Direction::right ? 1 : 0;
                if (to != pn_empty_ && !only_files)
                {
                    pn_empty_ = to;
                    feedback.play(hui::audio::Cue::focus);
                }
                else
                    refuse(feedback, input);
            }
            else if (input.is_pressed(Action::confirm) && (pn_empty_ == 1 || only_files))
            {
                section_ = 1;
                open_folder({}, {});
                feedback.play(hui::audio::Cue::open);
            }
            else if (input.is_pressed(Action::confirm))
            {
                if (state.have_library && library.ready() && !library.games.empty())
                {
                    view_ = 0;
                    query_.clear();
                    location_.clear();
                    format_.clear();
                    refilter();
                    feedback.play(hui::audio::Cue::toggle);
                }
                else if (library.busy || (state.have_library && library.ready()))
                    refuse(feedback, input);
                else
                {
                    ctx_.store.refresh_library();
                    feedback.play(hui::audio::Cue::select);
                }
            }
        }
        else if (input.nav != Direction::none)
        {
            pano_.move(page, input, feedback);
        }
        else if (input.is_pressed(Action::back) && pn_details_)
        {
            pn_details_ = false;
            feedback.play(hui::audio::Cue::back);
        }
        else if (input.is_pressed(Action::west))
            panorama_run('S', game, feedback, input);
        else if (input.is_pressed(Action::menu))
        {
            pn_bar_open_ = true;
            pn_bar_focus_ = 0;
            feedback.play(hui::audio::Cue::modal_open);
        }
        else if (input.is_pressed(Action::confirm))
        {
            if (key.rfind("drive:", 0) == 0)
                panorama_run('o', game, feedback, input);
            else if (key == "activity")
                panorama_run('a', game, feedback, input);
            else if (key.rfind("g:", 0) == 0)
            {
                // A game: Cross does its first action (mount or unmount, else Details).
                const std::vector<std::pair<char, std::string>> actions = game != nullptr ? panorama_actions(*game)
                                                                                          : std::vector<std::pair<char, std::string>>{};
                if (!actions.empty())
                    panorama_run(actions.front().first, game, feedback, input);
            }
            else if (key.rfind("act:", 0) == 0)
                panorama_run(key[4], game, feedback, input);
            else if (key == "search")
                panorama_run('S', game, feedback, input);
            else if (key == "scan")
                panorama_run('c', game, feedback, input);
            else if (key == "job:cancel")
                panorama_run('x', game, feedback, input);
            else if (key == "job:details" || key == "job")
            {
                // The game being copied or moved: its statuses.
                for (const LibraryGame &g : library.games)
                    if (g.title_id == library.storage_job.title_id)
                        pn_game_ = game_key(g);
                pn_details_ = true;
                feedback.play(hui::audio::Cue::open);
            }
            else
                refuse(feedback, input);
        }
    }
    if (section_ == 1)
    {
        // Files keeps its own page.
        page = files_panorama_page();
        pano_.show_group(-1);
        pano_.animate(page, dt);
    }
    else
    {
        // The selected game follows the focus in the games group; details close
        // when the focus leaves the game's own group. A focused drive is the one Options opens.
        page = panorama_page();
        const std::string now_key = pano_.focused_key(page);
        if (now_key.rfind("g:", 0) == 0 && now_key != pn_game_)
        {
            pn_game_ = now_key;
            pn_details_ = false;
        }
        else if (pn_details_ && now_key.rfind("act:", 0) != 0 && now_key.rfind("job", 0) != 0 && !band && !pn_bar_open_)
            pn_details_ = false;
        if (now_key.rfind("drive:", 0) == 0)
            files_pick_ = now_key.substr(6);
        page = panorama_page();
        if (!now_key.empty() && pano_.focused_key(page) != now_key)
            pano_.focus_key(page, now_key);
        pano_.show_group(pn_details_ ? 2 : -1);
        pano_.animate(page, dt);
    }
    keyboard_.set_active(keyboard_open_);
    keyboard_.update(dt);
    keyboard_in_.target = keyboard_open_ ? 1.0f : 0.0f;
    keyboard_in_.update(dt, 14.0f);
    xb_band_in_.target = !keyboard_open_ && (scan_open_ || (!selected_.empty() && mode_ != 0) || file_dialog_ != 0 || activity_open()) ? 1.0f : 0.0f;
    xb_band_in_.update(dt, 16.0f);
    pn_picker_in_.target = pn_picker_ >= 0 ? 1.0f : 0.0f;
    pn_picker_in_.update(dt, look::prefs().reduced_motion ? 40.0f : 16.0f);
    pn_bar_in_.target = pn_bar_open_ ? 1.0f : 0.0f;
    pn_bar_in_.update(dt, look::prefs().reduced_motion ? 40.0f : 16.0f);
    return intent;
}

void LibraryScreen::draw_panorama(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    if (keyboard_open_ && keyboard_for_ == 2)
    {
        pano::draw_keyboard(ctx_, frame, keyboard_,
                            file_dialog_ == 1   ? tr("Rename {name}", {{"name", file_dialog_entry_}})
                            : file_dialog_ == 2 ? std::string(tr("New folder"))
                                                : std::string(tr("This archive needs a password")),
                            file_text_, file_dialog_ == 4 ? tr("Password") : tr("Name"), file_dialog_ == 4);
        return;
    }
    if (keyboard_open_)
    {
        if (keyboard_for_ == 1)
            pano::draw_keyboard(ctx_, frame, keyboard_, tr("Search this folder"), files_query_, tr("type a name"), false);
        else
            pano::draw_keyboard(ctx_, frame, keyboard_, tr("Search your library"), query_, tr("type a name"), false);
        return;
    }
    const pano::Page page = panorama_page();
    // Files keeps its own empty-state button: Try again (or Reset) then Files.
    const auto with_files = [&](std::vector<pano::Button> buttons)
    {
        for (std::size_t i = 0; i < buttons.size(); ++i)
            buttons[i].focus = pn_empty_ == 0 && i == 0;
        pano::Button files = pano::button(tr("Files"), icons::Icon::folder);
        files.focus = pn_empty_ == 1 || buttons.empty();
        buttons.push_back(files);
        return buttons;
    };
    if (section_ == 0)
        pano::draw_header(ctx_, list, pano::lower(tr("Library")), pano_.scroll() * 0.35f);
    if (section_ == 1)
        draw_files_panorama(frame);
    else if (!state.have_library || !library.ready())
    {
        const bool busy = !state.have_library || library.busy;
        if (busy)
            pano::draw_empty(ctx_, list, tr("Library"), tr("Finding your games"),
                             tr("Reading installed titles and drive availability from ShadowMount."), with_files({}), true);
        else
            pano::draw_empty(ctx_, list, tr("ShadowMount isn\xE2\x80\x99t answering"), tr("Connect your library"),
                             std::string(tr("Library uses ShadowMount\xE2\x80\x99s local games API on your PS5. Start a version with that API "
                                            "enabled, then select Try again.")) +
                                 " " + tr("Games are not inferred from download history."),
                             with_files({pano::button(tr("Try again"), icons::Icon::refresh, true)}));
    }
    else if (rows_.empty())
    {
        if (library.games.empty())
            pano::draw_empty(ctx_, list, tr("Library"), tr("No games reported yet"),
                             tr("ShadowMount has not reported any installed or on-drive games yet."), with_files({}));
        else
            pano::draw_empty(ctx_, list, query_.empty() ? std::string(tr("Library")) : "\xE2\x80\x9C" + query_ + "\xE2\x80\x9D",
                             tr("No matching games"), tr("Try a different title, status or location."),
                             with_files({pano::button(tr("Reset library filters"), icons::Icon::refresh, true)}));
    }
    else
    {
        // Under the title: the selected game's status, or the snapshot's age when it's old;
        // then a scan ShadowMount is running on its own.
        const LibraryGame *game = panorama_game();
        std::string line;
        if (library.stale)
            line = last_refreshed(library) + " \xC2\xB7 " + tr("Saved snapshot. These statuses may have changed.");
        else if (game != nullptr)
            line = status_of(*game) + " \xC2\xB7 " + (game->on_drive ? i18n::place(game->location) : std::string(tr("Drive disconnected")));
        if (library.scanning)
            line += (line.empty() ? "" : " \xC2\xB7 ") + std::string(tr("Looking for new games\xE2\x80\xA6"));
        if (!line.empty())
            ui::text_fit(list, ctx_.type.regular, line, pano::kLeft, pano::baseline(256.0f, 22.0f), 22.0f, 1720.0f,
                         library.stale ? pano::kAmber : pano::kDim);
        pano_.draw(ctx_, list, page, pn_picker_ < 0 && !pn_bar_open_);
        std::vector<std::string> notes;
        if (!state.library_error.empty())
            notes.push_back(std::string(tr(std::string_view(state.library_error))));
        if (!library.message.empty())
            notes.push_back(std::string(tr(std::string_view(library.message))));
        if (!error_.empty() && mode_ == 0)
            notes.push_back(std::string(tr(std::string_view(error_))));
        if (!notes.empty())
            ui::text_fit(list, ctx_.type.regular, notes.front(), pano::kLeft, 1080.0f - 30.0f - 15.0f + 19.0f * 0.364f, 19.0f, 900.0f, pano::kAmber);
    }
    hui::gfx::DrawList &over = frame.overlay;
    if (pn_picker_in_.value > 0.01f)
    {
        // Search and Location as one list.
        pano::Picker picker;
        picker.head = tr("Search your library");
        picker.field = true;
        picker.field_value = query_;
        picker.field_placeholder = tr("Search your library");
        picker.field_focus = pn_picker_ == 0;
        picker.note_above = tr("Location");
        picker.size = 40.0f;
        std::vector<std::string> values;
        for (const LibraryGame &g : library.games)
            if (!g.location.empty() && std::find(values.begin(), values.end(), g.location) == values.end())
                values.push_back(g.location);
        std::sort(values.begin(), values.end());
        pano::Item all;
        all.title = tr("All locations");
        all.chosen = location_.empty();
        picker.items.push_back(all);
        for (const std::string &v : values)
        {
            pano::Item item;
            item.title = i18n::place(v);
            item.sub = trn(std::count_if(library.games.begin(), library.games.end(), [&](const LibraryGame &g) { return g.location == v; }),
                           "{count} game", "{count} games");
            item.chosen = v == location_;
            picker.items.push_back(std::move(item));
        }
        picker.focus = pn_picker_ - 1;
        pano::draw_picker(ctx_, over, picker, pn_picker_in_.value);
    }
    if (pn_bar_in_.value > 0.01f && section_ == 0)
        pano::draw_appbar(ctx_, over, panorama_bar(page), pn_bar_in_.value);
    if (xb_band_in_.value > 0.01f && (file_dialog_ != 0 || activity_open()))
    {
        // Files' question, or the operation with its progress as a tile.
        const xb::Band question = files_band();
        pano::Band band;
        band.kicker = question.kicker;
        band.title = question.title;
        band.body = question.body + (question.note.empty() ? std::string() : "\n" + question.note);
        band.error = question.error;
        band.busy = question.busy;
        int tiles = 0;
        const Snapshot &now = ctx_.store.state();
        if (activity_open() && file_dialog_ == 0 && !now.file_operation.ended() && now.file_operation.state != "password")
        {
            // Running: the amounts and speed on a tile with its bar; the body says where.
            const FileOperation &o = now.file_operation;
            pano::Tile tile;
            tile.has_icon = true;
            tile.icon = icons::Icon::clock;
            tile.number = i18n::whole_percent(o.total > 0.0 ? static_cast<float>(100.0 * o.processed / o.total) : 0.0f);
            tile.label = format_bytes(o.processed) + " / " + format_bytes(o.total);
            if (o.state == "running" && o.speed > 0.0)
                tile.sub = format_bytes(o.speed) + "/s";
            tile.bar = o.total > 0.0 ? static_cast<float>(std::clamp(o.processed / o.total, 0.0, 1.0)) : 0.0f;
            band.tiles.push_back(std::move(tile));
            band.body = question.note;
            tiles = 1;
        }
        for (const std::string &b : question.buttons)
            band.buttons.push_back(pano::button(b));
        band.focus = tiles + question.focus;
        pano::draw_band(ctx_, over, band, xb_band_in_.value);
    }
    else if (xb_band_in_.value > 0.01f)
    {
        // A question: Store's words in a message band, destinations as tiles.
        const xb::Band question = crossbar_band();
        pano::Band band;
        band.kicker = question.kicker;
        band.title = question.title;
        band.body = question.body + (question.note.empty() ? std::string() : " " + question.note);
        band.error = question.error;
        band.busy = question.busy;
        if (!question.options.empty())
        {
            band.tiles_label = tr("Destination drive");
            const std::vector<const LibraryDrive *> targets = destinations();
            for (std::size_t i = 0; i < targets.size() && i < question.options.size(); ++i)
            {
                pano::Tile tile;
                tile.has_icon = true;
                tile.icon = icons::Icon::drive;
                tile.label = i18n::place(targets[i]->label);
                tile.sub = tr("{size} free", {{"size", format_bytes(targets[i]->free)}});
                tile.check = tile.selected = question.options[i].checked;
                band.tiles.push_back(std::move(tile));
            }
        }
        for (const std::string &b : question.buttons)
            band.buttons.push_back(pano::button(b));
        band.focus = xb_band_;
        pano::draw_band(ctx_, over, band, xb_band_in_.value);
    }
}

std::vector<hui::ui::Hint> LibraryScreen::panorama_hints() const
{
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    if (keyboard_open_)
        return {{hui::ui::Button::square, tr("Delete")}, {hui::ui::Button::triangle, tr("Space")}, {hui::ui::Button::circle, tr("Close")}};
    if (scan_open_ || (!selected_.empty() && mode_ != 0))
        return {};
    if (section_ == 1)
        return files_panorama_hints();
    if (file_dialog_ != 0 || activity_open())
        return {};
    if (pn_picker_ >= 0 || pn_bar_open_)
        return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Close")}};
    if (!state.have_library || !library.ready() || rows_.empty())
    {
        // Nothing to show: Cross presses the focused button (Files beside the first).
        const bool only_files = !state.have_library || library.busy || (library.ready() && library.games.empty());
        std::vector<hui::ui::Hint> hints;
        if (pn_empty_ == 1 || only_files)
            hints.push_back({hui::ui::Button::cross, tr("Files")});
        else if (!only_files)
            hints.push_back({hui::ui::Button::cross, library.ready() ? tr("Reset library filters") : tr("Try again")});
        hints.push_back({hui::ui::Button::l1, tr("Sections"), hui::ui::Button::r1});
        return hints;
    }
    std::vector<hui::ui::Hint> hints{{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::options, tr("Actions")}};
    if (pn_details_)
        hints.push_back({hui::ui::Button::circle, tr("Back")});
    else
        hints.push_back({hui::ui::Button::l1, tr("Sections"), hui::ui::Button::r1});
    return hints;
}

} // namespace orbit
