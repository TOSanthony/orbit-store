// Orbit Store TV app - Library's Files: Orbit's drives, then a folder of one.
// Only files: the games, their sizes and the scan are Library's.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The browser's Files (src/Files.tsx, src/FileActions.tsx, src/useFiles.ts and
// src/interfaces/*Files.tsx) for a controller. Store, and Cinema in its
// palette, lists a folder with an entry's details and actions in a panel;
// Crossbar lists it as rows with an entry's actions in a menu; Panorama lays
// it out as tiles. Every look shares the place, the clipboard (Copy and Move
// wait for a folder, then Paste here), the dialogs and the running operation,
// which shows in a dialog until it is hidden.

#include "orbit/i18n.hpp"
#include "orbit/look.hpp"
#include "orbit/screens.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace orbit
{

namespace
{

constexpr float kPanelWidth = 1000.0f;
constexpr float kRowHeight = 76.0f;
constexpr float kRowStep = 84.0f;
constexpr float kSortWidth = 200.0f;
constexpr float kSearchWidth = 580.0f;
constexpr const char *kSorts[] = {/* i18n */ "Name", /* i18n */ "Size", /* i18n */ "Date modified"};
// Activity buttons, by what they do.
constexpr int kPassword = 0, kPause = 1, kCancel = 2, kHide = 3;

const char *type_name(const std::string &type)
{
    if (type == "folder")
        return tr("Folder");
    if (type == "game")
        return tr("Game folder");
    if (type == "archive")
        return tr("Archive");
    if (type == "package")
        return tr("Package");
    if (type == "image")
        return tr("Game image");
    return tr("File");
}

icons::Icon type_icon(const std::string &type)
{
    if (type == "folder")
        return icons::Icon::folder;
    if (type == "game")
        return icons::Icon::ps;
    if (type == "archive")
        return icons::Icon::archive;
    if (type == "package")
        return icons::Icon::box;
    if (type == "image")
        return icons::Icon::layers;
    return icons::Icon::list;
}

// Small files in kilobytes; Orbit's own sizes start at megabytes.
std::string file_bytes(double n)
{
    if (n >= 1e6)
        return format_bytes(n);
    const std::string_view language = i18n::language();
    const char *unit = language == "fr" ? "ko" : language == "ru" ? "\xD0\xBA\xD0\x91" : "kB";
    return (n < 1e4 ? i18n::decimal(n / 1e3) : i18n::count(std::lround(n / 1e3))) + " " + unit;
}

std::string file_date(double seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0.0 || seconds > 253402214400.0)
        return {};
    // Same civil-date conversion as main.cpp; the PS5 does not export localtime_r.
    const long long local = static_cast<long long>(seconds) + look::utc_offset() * 60LL;
    const long long days = (local >= 0 ? local : local - 86399) / 86400;
    const long long z = days + 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const long long doe = z - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    const int day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    const int month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    const int year = static_cast<int>(yoe + era * 400 + (month <= 2 ? 1 : 0));
    return i18n::date(year, month, day);
}

std::string locked_text(const std::string &locked)
{
    if (locked == "drive")
        return tr("This is the top of the drive.");
    if (locked == "orbit")
        return tr("Orbit keeps its own settings and downloads list here.");
    if (locked == "download")
        return tr("A download or transfer is using this.");
    if (locked == "game")
        return tr("ShadowMount is using this game. Unmount or uninstall it first.");
    if (locked == "install")
        return tr("The PS5 is installing from this.");
    if (locked == "busy")
        return tr("Another file operation is using this.");
    return locked;
}

// Whole sentences per operation, so each language can phrase them its own way.
std::string operation_title(const FileOperation &o)
{
    struct Words
    {
        const char *action, *running, *completed, *cancelled, *failed;
    };
    static const Words kWords[] = {
        {"copy", /* i18n */ "Copying {title}", /* i18n */ "Copied {title}", /* i18n */ "Copy cancelled: {title}",
         /* i18n */ "Copy failed: {title}"},
        {"move", /* i18n */ "Moving {title}", /* i18n */ "Moved {title}", /* i18n */ "Move cancelled: {title}",
         /* i18n */ "Move failed: {title}"},
        {"delete", /* i18n */ "Deleting {title}", /* i18n */ "Deleted {title}", /* i18n */ "Delete cancelled: {title}",
         /* i18n */ "Delete failed: {title}"},
        {"unpack", /* i18n */ "Unpacking {title}", /* i18n */ "Unpacked {title}", /* i18n */ "Unpack cancelled: {title}",
         /* i18n */ "Unpack failed: {title}"},
        {"install", /* i18n */ "Installing {title}", /* i18n */ "Installed {title}",
         /* i18n */ "Install cancelled: {title}", /* i18n */ "Install failed: {title}"},
        {"measure", /* i18n */ "Measuring {title}", /* i18n */ "Measured {title}",
         /* i18n */ "Measuring cancelled: {title}", /* i18n */ "Measuring failed: {title}"},
    };
    for (const Words &w : kWords)
    {
        if (o.action == w.action)
            return tr(o.state == "complete"    ? w.completed
                      : o.state == "cancelled" ? w.cancelled
                      : o.state == "failed"    ? w.failed
                                               : w.running,
                      {{"title", o.name}});
    }
    return o.name;
}

float operation_progress(const FileOperation &o)
{
    return o.total > 0.0 ? static_cast<float>(std::clamp(o.processed / o.total, 0.0, 1.0)) : 0.0f;
}

std::string operation_line(const FileOperation &o)
{
    if (o.state == "measuring")
        return tr("Measuring\xE2\x80\xA6");
    if (o.state == "password")
        return tr("This archive needs a password.");
    if (o.state == "paused")
        return std::string(tr("Paused")) + " \xC2\xB7 " + format_bytes(o.processed) + " / " + format_bytes(o.total);
    if (o.state == "complete")
        return o.action == "measure" && o.size >= 0.0 ? format_bytes(o.size)
                                                      : format_bytes(o.processed) + " \xC2\xB7 " + tr("Complete");
    if (o.state == "cancelled")
        return tr("Operation stopped");
    if (o.state == "failed")
        return tr("Operation could not finish");
    return format_bytes(o.processed) + " / " + format_bytes(o.total);
}

// The percentage and speed under the bar.
std::string operation_meter(const FileOperation &o)
{
    std::string out = i18n::percent(operation_progress(o) * 100.0);
    if (o.state == "running" && o.speed > 0.0)
        out += " \xC2\xB7 " + format_bytes(o.speed) + "/s";
    return out;
}

bool matches(std::string_view text, std::string_view query)
{
    if (query.empty())
        return true;
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; };
    for (std::size_t i = 0; i + query.size() <= text.size(); ++i)
    {
        std::size_t j = 0;
        while (j < query.size() && lower(text[i + j]) == lower(query[j]))
            ++j;
        if (j == query.size())
            return true;
    }
    return false;
}

// Names as the browser sorts them: numbers by value, letters without case.
bool name_less(const std::string &a, const std::string &b)
{
    std::size_t i = 0, j = 0;
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; };
    while (i < a.size() && j < b.size())
    {
        if (digit(a[i]) && digit(b[j]))
        {
            std::size_t ie = i, je = j;
            while (ie < a.size() && digit(a[ie]))
                ++ie;
            while (je < b.size() && digit(b[je]))
                ++je;
            const std::string x = a.substr(i, ie - i), y = b.substr(j, je - j);
            const std::string xs = x.substr(std::min(x.find_first_not_of('0'), x.size()));
            const std::string ys = y.substr(std::min(y.find_first_not_of('0'), y.size()));
            if (xs.size() != ys.size())
                return xs.size() < ys.size();
            if (xs != ys)
                return xs < ys;
            i = ie;
            j = je;
            continue;
        }
        if (lower(a[i]) != lower(b[j]))
            return lower(a[i]) < lower(b[j]);
        ++i;
        ++j;
    }
    return a.size() - i < b.size() - j;
}

std::string drive_root(const Drive &drive)
{
    const std::string tail = "/homebrew";
    if (drive.path.size() > tail.size() && drive.path.compare(drive.path.size() - tail.size(), tail.size(), tail) == 0)
        return drive.path.substr(0, drive.path.size() - tail.size());
    return drive.path;
}

std::string parent_of(const std::string &path)
{
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string last_part(const std::string &path)
{
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string trimmed(const std::string &text)
{
    const std::size_t first = text.find_first_not_of(' ');
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

// Store's dialogs: where their parts sit, for their buttons (the focus) and their drawing.
constexpr float kDetailsTop = 150.0f, kDeleteTop = 250.0f, kActivityTop = 230.0f, kNameTop = 110.0f;

std::string delete_body(bool folder)
{
    return folder ? tr("The folder and everything in it will be deleted from your PS5. This can't be undone.")
                  : tr("The file will be deleted from your PS5. This can't be undone.");
}

// Delete: the body (19 px, 29 px lines from +142), the path, then the buttons.
float delete_buttons(const ui::Type &type, bool folder)
{
    const float width = kPanelWidth - 112.0f;
    const int lines = std::clamp(static_cast<int>(type.regular.measure(delete_body(folder), 19.0f) / (width - 60.0f)) + 1, 1, 3);
    return 142.0f + 29.0f * static_cast<float>(lines - 1) + 46.0f + 36.0f;
}

// The operation: from, to, its line, the bar and meter while it runs, an error, the buttons.
struct ActivityLayout
{
    float line, bar, meter, error, buttons, height;
};
ActivityLayout activity_layout(const FileOperation &o, bool error)
{
    ActivityLayout l{};
    l.line = (o.to_storage_id.empty() ? 160.0f : 186.0f) + 50.0f;
    l.bar = l.line + 30.0f;
    l.meter = l.line + 64.0f;
    float y = !o.ended() && o.state != "password" ? l.meter : l.line;
    if (error)
    {
        l.error = y + 40.0f;
        y = l.error;
    }
    l.buttons = y + 36.0f;
    l.height = l.buttons + 48.0f + 56.0f;
    return l;
}

// A question is open as a band (Crossbar, Panorama): a dialog, or the operation.
bool band_open(int dialog, bool activity)
{
    return dialog != 0 || activity;
}

// A pill with one line in it, as the header's actions are drawn.
void pill(hui::gfx::DrawList &list, const ui::Type &type, const ui::Rect &r, std::string_view text, float focus)
{
    ui::focus_ring(list, r, r.h * 0.5f, focus);
    list.bordered_rect(r, r.h * 0.5f, ui::color::night3.with_alpha(0.5f), 2.0f, ui::color::text2.with_alpha(0.45f));
    ui::text_fit(list, type.medium, text, r.x + 22.0f, r.cy() + 6.5f, 18.0f, r.w - 44.0f, ui::color::text);
}

} // namespace

// ---------------------------------------------------------------- shared

const Drive *LibraryScreen::files_drive() const
{
    for (const Drive &drive : ctx_.store.state().drives)
    {
        if (drive.id == files_drive_)
            return &drive;
    }
    return nullptr;
}

std::vector<const FileEntry *> LibraryScreen::file_entries() const
{
    std::vector<const FileEntry *> out;
    const Snapshot &state = ctx_.store.state();
    if (!state.have_files || files_drive_.empty() || state.files.storage_id != files_drive_ ||
        state.files.path != files_path_)
        return out;
    for (const FileEntry &entry : state.files.entries)
    {
        if (matches(entry.name, trimmed(files_query_)))
            out.push_back(&entry);
    }
    std::stable_sort(out.begin(), out.end(),
                     [&](const FileEntry *a, const FileEntry *b)
                     {
                         if (a->folder != b->folder)
                             return a->folder;
                         if (files_sort_ == 1 && file_size(*a) != file_size(*b))
                             return file_size(*a) > file_size(*b);
                         if (files_sort_ == 2 && a->modified != b->modified)
                             return a->modified > b->modified;
                         return name_less(a->name, b->name);
                     });
    return out;
}

const FileEntry *LibraryScreen::file_entry(const std::string &name) const
{
    for (const FileEntry *entry : file_entries())
    {
        if (entry->name == name)
            return entry;
    }
    return nullptr;
}

std::string LibraryScreen::file_path(const FileEntry &entry) const
{
    return files_path_.empty() ? entry.name : files_path_ + "/" + entry.name;
}

double LibraryScreen::file_size(const FileEntry &entry) const
{
    if (entry.size >= 0.0 || !entry.folder)
        return entry.size;
    const std::string key = files_drive_ + ":" + file_path(entry);
    for (const auto &[at, size] : files_sizes_)
    {
        if (at == key)
            return size;
    }
    return -1.0;
}

// "Archive · 12.4 GB · Oct 20, 2026"; without the date, "In use" first, for tiles.
std::string LibraryScreen::file_line(const FileEntry &entry, bool date) const
{
    std::vector<std::string> parts;
    if (!date && !entry.locked.empty())
        parts.push_back(tr("In use"));
    parts.push_back(type_name(entry.type));
    if (file_size(entry) >= 0.0)
        parts.push_back(file_bytes(file_size(entry)));
    if (date && entry.modified > 0.0)
        parts.push_back(file_date(entry.modified));
    std::string out;
    for (const std::string &part : parts)
        out += (out.empty() ? "" : " \xC2\xB7 ") + part;
    return out;
}

// The drive and folder as one line: "USB Extended Storage / homebrew / Games".
std::string LibraryScreen::files_place(const std::string &storage, const std::string &path) const
{
    std::string out = storage;
    for (const Drive &drive : ctx_.store.state().drives)
    {
        if (drive.id == storage)
            out = i18n::place(drive.label);
    }
    std::size_t at = 0;
    while (at < path.size())
    {
        const std::size_t slash = path.find('/', at);
        out += " / " + path.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
        if (slash == std::string::npos)
            break;
        at = slash + 1;
    }
    return out;
}

std::string LibraryScreen::files_title() const
{
    if (!files_path_.empty())
        return last_part(files_path_);
    const Drive *drive = files_drive();
    return drive != nullptr ? i18n::place(drive->label) : files_drive_;
}

// An entry's actions, the way in first: Open, Unpack here or Install on PS5. The
// browser's Save to this device has no place here: this app runs on the console.
std::vector<LibraryScreen::FileAct> LibraryScreen::file_actions(const FileEntry &entry) const
{
    const bool busy = files_running() || ctx_.store.file_action().busy;
    const bool writable = entry.locked.empty();
    // Reading is fine while others use it, not while something is still writing it.
    const bool written = entry.locked == "download" || entry.locked == "busy";
    std::vector<FileAct> out;
    if (entry.folder)
        out.push_back({'o', tr("Open"), icons::Icon::next, true, true});
    if (entry.type == "archive")
        out.push_back({'u', tr("Unpack here"), icons::Icon::archive, true, !busy && !written});
    if (entry.type == "package" || entry.type == "image" || entry.type == "game")
        out.push_back({'i', tr("Install on PS5"), icons::Icon::down, !entry.folder, !busy && !written && entry.locked != "install"});
    if (entry.folder && file_size(entry) < 0.0)
        out.push_back({'z', tr("Measure size"), icons::Icon::layers, false, !busy});
    out.push_back({'c', tr("Copy"), icons::Icon::copy, false, !busy && !written});
    out.push_back({'m', tr("Move"), icons::Icon::move, false, !busy && writable});
    out.push_back({'r', tr("Rename"), icons::Icon::pencil, false, !busy && writable});
    out.push_back({'d', tr("Delete"), icons::Icon::trash, false, !busy && writable});
    return out;
}

std::vector<std::pair<std::string, std::string>> LibraryScreen::file_facts(const FileEntry &entry) const
{
    const double size = file_size(entry);
    return {{tr("Type"), type_name(entry.type)},
            {tr("Size"), size >= 0.0 ? file_bytes(size) : entry.folder ? tr("Not measured") : tr("Unavailable")},
            {tr("Modified"), entry.modified > 0.0 ? file_date(entry.modified) : std::string(tr("Not reported"))},
            {tr("Location"), files_place(files_drive_, file_path(entry))}};
}

// The running or last operation, unless it is a folder's quick measuring or was closed.
const FileOperation *LibraryScreen::file_activity() const
{
    const Snapshot &state = ctx_.store.state();
    if (!state.have_file_operation || state.file_operation.action == "measure" ||
        state.file_operation.id == activity_done_)
        return nullptr;
    return &state.file_operation;
}

bool LibraryScreen::files_running() const
{
    const Snapshot &state = ctx_.store.state();
    return state.have_file_operation && !state.file_operation.ended();
}

bool LibraryScreen::activity_open() const
{
    const FileOperation *o = file_activity();
    return o != nullptr && o->id == activity_shown_;
}

std::string LibraryScreen::activity_line() const
{
    const FileOperation *o = file_activity();
    if (o == nullptr)
        return {};
    if (o->ended())
        return operation_title(*o);
    return operation_title(*o) + " \xC2\xB7 " +
           (o->state == "password" ? std::string(tr("Needs a password"))
            : o->state == "paused" ? std::string(tr("Paused"))
                                   : i18n::percent(operation_progress(*o) * 100.0));
}

std::string LibraryScreen::clip_text() const
{
    if (clip_.action.empty())
        return {};
    return tr(clip_.action == "copy" ? "Copy {name}: open the folder to copy it to, then choose Paste here."
                                     : "Move {name}: open the folder to move it to, then choose Paste here.",
              {{"name", clip_.name}});
}

bool LibraryScreen::can_paste() const
{
    if (clip_.action.empty() || files_drive_.empty() || files_running() || ctx_.store.file_action().busy)
        return false;
    const bool inside = clip_.storage == files_drive_ &&
                        (files_path_ == clip_.path || files_path_.rfind(clip_.path + "/", 0) == 0);
    const bool same = clip_.storage == files_drive_ && files_path_ == parent_of(clip_.path);
    return !inside && !(clip_.action == "move" && same);
}

void LibraryScreen::show_files(const std::string &storage, const std::string &path)
{
    selected_.clear();
    section_ = 1;
    ctx_.store.want_files(true);
    open_folder(storage, path);
}

void LibraryScreen::open_folder(std::string storage, std::string path)
{
    files_drive_ = storage;
    files_path_ = path;
    files_query_.clear();
    file_selected_.clear();
    files_pick_.clear();
    files_error_.clear();
    files_focus_pending_ = !storage.empty();
    ctx_.store.open_files(storage, path);
    xb_.index = 0;
    xb_.snap();
    xb_key_.clear();
    pano_.focus = {};
    pano_.snap();
}

bool LibraryScreen::files_up()
{
    if (files_drive_.empty())
        return false;
    const std::string from = files_path_.empty() ? files_drive_ : last_part(files_path_);
    const bool top = files_path_.empty();
    open_folder(top ? std::string() : files_drive_, top ? std::string() : parent_of(files_path_));
    // The focus goes back to what was open: its row, card or tile.
    files_focus_ = from;
    files_focus_pending_ = true;
    if (top)
    {
        const std::vector<Drive> &drives = ctx_.store.state().drives;
        for (int d = 0; d < static_cast<int>(drives.size()); ++d)
        {
            if (drives[static_cast<std::size_t>(d)].id == from)
            {
                focus_kind_ = Kind::files_drive;
                focus_index_ = d;
            }
        }
        if (xb_.focus_key(crossbar_list(), "f:" + from))
            xb_key_ = "f:" + from;
    }
    return true;
}

void LibraryScreen::leave_files()
{
    open_folder({}, {});
    section_ = 0;
    ctx_.store.want_files(false);
    keyboard_for_ = 0;
}

void LibraryScreen::run_file(api::Client::FileRequest request, Feedback &feedback)
{
    files_error_.clear();
    file_request_ = request.action;
    ctx_.store.run_file_action(std::move(request));
    feedback.play(hui::audio::Cue::select);
}

void LibraryScreen::file_act(char key, const FileEntry &entry, Feedback &feedback, const InputFrame &input)
{
    for (const FileAct &act : file_actions(entry))
    {
        if (act.key != key)
            continue;
        if (!act.enabled)
        {
            refuse(feedback, input);
            return;
        }
        api::Client::FileRequest request;
        request.storage_id = files_drive_;
        request.path = file_path(entry);
        switch (key)
        {
        case 'o':
            open_folder(files_drive_, file_path(entry));
            feedback.play(hui::audio::Cue::open);
            return;
        case 'u':
        case 'i':
        case 'z':
            request.action = key == 'u' ? "unpack" : key == 'i' ? "install" : "measure";
            if (key != 'z')
                file_selected_.clear();
            run_file(std::move(request), feedback);
            return;
        case 'c':
        case 'm':
            clip_ = {key == 'c' ? "copy" : "move", files_drive_, file_path(entry), entry.name};
            file_selected_.clear();
            feedback.play(hui::audio::Cue::select);
            return;
        case 'r':
            open_file_dialog(1, entry.name, feedback);
            return;
        case 'd':
            open_file_dialog(3, entry.name, feedback);
            return;
        default:
            break;
        }
    }
    refuse(feedback, input);
}

void LibraryScreen::paste_files(Feedback &feedback, const InputFrame &input)
{
    if (!can_paste())
    {
        refuse(feedback, input);
        return;
    }
    api::Client::FileRequest request;
    request.action = clip_.action;
    request.storage_id = clip_.storage;
    request.path = clip_.path;
    request.to_storage_id = files_drive_;
    request.to_path = files_path_;
    run_file(std::move(request), feedback);
}

void LibraryScreen::open_file_dialog(int dialog, const std::string &entry, Feedback &feedback)
{
    file_selected_.clear();
    file_dialog_ = dialog;
    file_dialog_entry_ = entry;
    file_text_ = dialog == 1 ? entry : std::string();
    files_error_.clear();
    file_focus_ = 0;
    xb_band_ = 0;
    if (dialog != 3)
    {
        // A name or a password: the keyboard, with the dialog over it.
        keyboard_for_ = 2;
        keyboard_open_ = true;
        keyboard_.style.max_length = dialog == 4 ? 128 : 200;
        keyboard_.enter();
        keyboard_.set_length(static_cast<int>(file_text_.size()));
    }
    feedback.play(hui::audio::Cue::modal_open);
}

void LibraryScreen::close_file_dialog(Feedback &feedback)
{
    file_dialog_ = 0;
    file_text_.clear();
    files_error_.clear();
    if (keyboard_for_ == 2)
    {
        keyboard_open_ = false;
        keyboard_for_ = 0;
        keyboard_.style.max_length = 40;
    }
    feedback.play(hui::audio::Cue::modal_close);
}

void LibraryScreen::submit_file_dialog(Feedback &feedback, const InputFrame &input)
{
    const std::string name = trimmed(file_text_);
    const bool named = file_dialog_ == 1 || file_dialog_ == 2;
    if (ctx_.store.file_action().busy ||
        (named && (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string::npos)) ||
        (file_dialog_ == 4 && file_text_.empty()))
    {
        refuse(feedback, input);
        return;
    }
    api::Client::FileRequest request;
    request.storage_id = files_drive_;
    request.path = file_dialog_ == 2 ? files_path_ : (files_path_.empty() ? file_dialog_entry_ : files_path_ + "/" + file_dialog_entry_);
    switch (file_dialog_)
    {
    case 1:
        request.action = "rename";
        request.name = name;
        break;
    case 2:
        request.action = "folder";
        request.name = name;
        break;
    case 3:
        request.action = "delete";
        break;
    default:
        request.action = "password";
        request.id = ctx_.store.state().file_operation.id;
        request.password = file_text_;
        break;
    }
    keyboard_open_ = false;
    run_file(std::move(request), feedback);
}

void LibraryScreen::show_activity(Feedback &feedback)
{
    if (const FileOperation *o = file_activity())
    {
        activity_shown_ = o->id;
        file_focus_ = -1; // Hide, or Done
        xb_band_ = -1;
        feedback.play(hui::audio::Cue::modal_open);
    }
}

void LibraryScreen::hide_activity(Feedback &feedback)
{
    if (const FileOperation *o = file_activity(); o != nullptr && o->ended())
        activity_done_ = o->id;
    activity_shown_.clear();
    feedback.play(hui::audio::Cue::modal_close);
}

void LibraryScreen::operate(const char *action, Feedback &feedback, const InputFrame &input)
{
    const FileOperation *o = file_activity();
    if (o == nullptr || o->ended() || ctx_.store.file_action().busy)
    {
        refuse(feedback, input);
        return;
    }
    api::Client::FileRequest request;
    request.action = action;
    request.id = o->id;
    run_file(std::move(request), feedback);
}

// Every frame while Files shows: a request's answer, the folder again once an
// operation finishes, a measured folder's size, and the focus once a folder is read.
void LibraryScreen::update_files()
{
    const Snapshot &state = ctx_.store.state();
    const ActionState &action = ctx_.store.file_action();
    if (action.serial != file_serial_ && !action.busy)
    {
        file_serial_ = action.serial;
        if (!action.error.empty())
        {
            files_error_ = action.error;
            // A name or password refused: type it again.
            if (file_dialog_ == 1 || file_dialog_ == 2 || file_dialog_ == 4)
            {
                keyboard_for_ = 2;
                keyboard_open_ = true;
                keyboard_.enter();
                keyboard_.set_length(static_cast<int>(file_text_.size()));
            }
        }
        else
        {
            const std::string &sent = file_request_;
            if (state.have_file_operation &&
                (sent == "copy" || sent == "move" || sent == "delete" || sent == "unpack" || sent == "install"))
            {
                activity_shown_ = state.file_operation.id;
                file_focus_ = -1;
                xb_band_ = -1;
            }
            if (sent == "copy" || sent == "move")
                clip_ = {};
            if (sent == "rename" || sent == "folder")
                ctx_.store.open_files(files_drive_, files_path_);
            if (file_dialog_ != 0 && sent != "pause" && sent != "resume" && sent != "cancel")
            {
                file_dialog_ = 0;
                file_text_.clear();
                if (keyboard_for_ == 2)
                    keyboard_for_ = 0;
                keyboard_.style.max_length = 40;
            }
        }
        file_request_.clear();
    }
    const bool running = files_running();
    if (files_were_running_ && !running && !files_drive_.empty())
        ctx_.store.open_files(files_drive_, files_path_);
    files_were_running_ = running;
    if (running)
        ctx_.store.set_fast(true);
    if (state.have_file_operation && state.file_operation.action == "measure" && state.file_operation.state == "complete" &&
        state.file_operation.size >= 0.0)
    {
        const std::string key = state.file_operation.storage_id + ":" + state.file_operation.path;
        if (std::none_of(files_sizes_.begin(), files_sizes_.end(), [&](const auto &s) { return s.first == key; }))
            files_sizes_.emplace_back(key, state.file_operation.size);
    }
    // Once the folder is read: the entry that was open, else the first.
    if (files_focus_pending_ && state.have_files && state.files.storage_id == files_drive_ && state.files.path == files_path_)
    {
        files_focus_pending_ = false;
        const std::vector<const FileEntry *> entries = file_entries();
        int at = entries.empty() ? -1 : 0;
        for (int i = 0; i < static_cast<int>(entries.size()); ++i)
        {
            if (entries[static_cast<std::size_t>(i)]->name == files_focus_)
                at = i;
        }
        if (at >= 0)
        {
            focus_kind_ = Kind::files_entry;
            focus_index_ = at;
            files_pick_ = entries[static_cast<std::size_t>(at)]->name;
            if (xb_.focus_key(crossbar_list(), "e:" + files_pick_))
                xb_key_ = "e:" + files_pick_;
            pano_.focus_key(files_panorama_page(), "e:" + files_pick_);
        }
        else
        {
            focus_kind_ = Kind::files_up;
            focus_index_ = 0;
        }
        files_focus_.clear();
    }
    else if (files_focus_pending_ && files_drive_.empty())
    {
        files_focus_pending_ = false;
        files_focus_.clear();
    }
}

// ---------------------------------------------------------------- Store

std::vector<LibraryScreen::Control> LibraryScreen::files_controls(float y) const
{
    const Snapshot &state = ctx_.store.state();
    const ui::Type &type = ctx_.type;
    std::vector<Control> out;
    int row = 1;
    const float right = 1920.0f - ui::kGutter;
    // The heading's actions on its right: the hidden operation, New folder, Up.
    {
        std::vector<Control> heading;
        float x = right;
        if (!files_drive_.empty())
        {
            const char *up = files_path_.empty() ? tr("All drives") : tr("Up one folder");
            const float w = ui::button_width(type, up, 18.0f, ui::ButtonKind::secondary);
            x -= w;
            heading.push_back({Kind::files_up, 0, row, {x, y + 8.0f, w, 48.0f}});
            const float nw = ui::button_width(type, tr("New folder"), 18.0f, ui::ButtonKind::secondary);
            x -= nw + 12.0f;
            heading.push_back({Kind::files_new, 0, row, {x, y + 8.0f, nw, 48.0f}});
            x -= 12.0f;
        }
        if (file_activity() != nullptr && !activity_open())
        {
            const float w = std::min(560.0f, type.medium.measure(activity_line(), 18.0f) + 44.0f);
            x -= w;
            heading.push_back({Kind::files_activity, 0, row, {x, y + 8.0f, w, 48.0f}});
        }
        std::reverse(heading.begin(), heading.end());
        out.insert(out.end(), heading.begin(), heading.end());
        if (!heading.empty())
            ++row;
    }
    y += 100.0f;
    if (!clip_.action.empty())
    {
        const float cw = ui::button_width(type, tr("Cancel"), 18.0f, ui::ButtonKind::secondary);
        const float pw = ui::button_width(type, tr("Paste here"), 18.0f);
        out.push_back({Kind::files_paste, 0, row, {right - 18.0f - cw - 12.0f - pw, y + 13.0f, pw, 48.0f}});
        out.push_back({Kind::files_unclip, 0, row, {right - 18.0f - cw, y + 13.0f, cw, 48.0f}});
        ++row;
        y += 94.0f;
    }
    if (files_drive_.empty())
    {
        const float card_w = 400.0f;
        for (int d = 0; d < static_cast<int>(state.drives.size()); ++d)
            out.push_back({Kind::files_drive,
                           d,
                           row + d / 4,
                           {ui::kGutter + static_cast<float>(d % 4) * (card_w + 40.0f), y + static_cast<float>(d / 4) * 190.0f,
                            card_w, 170.0f}});
        return out;
    }
    for (int s = 0; s < 3; ++s)
        out.push_back({Kind::files_sort, s, row, {ui::kGutter + static_cast<float>(s) * kSortWidth, y, kSortWidth, 58.0f}});
    out.push_back({Kind::files_search, 0, row, {right - kSearchWidth, y, kSearchWidth, 58.0f}});
    ++row;
    y += 58.0f + 52.0f;
    const std::vector<const FileEntry *> entries = file_entries();
    for (int i = 0; i < static_cast<int>(entries.size()); ++i)
    {
        const bool folder = entries[static_cast<std::size_t>(i)]->folder;
        const float top = y + static_cast<float>(i) * kRowStep;
        out.push_back({Kind::files_entry, i, row, {ui::kGutter, top, folder ? 1648.0f : 1720.0f, kRowHeight}});
        if (folder)
            out.push_back({Kind::files_more, i, row, {right - 56.0f, top + (kRowHeight - 48.0f) * .5f, 48.0f, 48.0f}});
        ++row;
    }
    return out;
}

// What Cross does on the page.
void LibraryScreen::update_files_page(const InputFrame &input, Feedback &feedback, Kind kind, int index)
{
    const Snapshot &state = ctx_.store.state();
    switch (kind)
    {
    case Kind::files_drive:
        if (index < static_cast<int>(state.drives.size()))
        {
            open_folder(state.drives[static_cast<std::size_t>(index)].id, {});
            feedback.play(hui::audio::Cue::open);
        }
        break;
    case Kind::files_entry:
    case Kind::files_more:
    {
        const std::vector<const FileEntry *> entries = file_entries();
        if (index < static_cast<int>(entries.size()))
        {
            const FileEntry &entry = *entries[static_cast<std::size_t>(index)];
            if (kind == Kind::files_entry && entry.folder)
            {
                open_folder(files_drive_, files_path_.empty() ? entry.name : files_path_ + "/" + entry.name);
                feedback.play(hui::audio::Cue::open);
            }
            else
            {
                file_selected_ = entry.name;
                file_focus_ = 0;
                feedback.play(hui::audio::Cue::modal_open);
            }
        }
        break;
    }
    case Kind::files_sort:
        files_sort_ = index;
        feedback.play(hui::audio::Cue::toggle);
        break;
    case Kind::files_search:
        keyboard_for_ = 1;
        keyboard_open_ = true;
        keyboard_.enter();
        keyboard_.set_length(static_cast<int>(files_query_.size()));
        feedback.play(hui::audio::Cue::modal_open);
        break;
    case Kind::files_new:
        if (files_running())
            refuse(feedback, input);
        else
            open_file_dialog(2, {}, feedback);
        break;
    case Kind::files_up:
        files_up();
        feedback.play(hui::audio::Cue::back);
        break;
    case Kind::files_activity:
        show_activity(feedback);
        break;
    case Kind::files_paste:
        paste_files(feedback, input);
        break;
    case Kind::files_unclip:
        clip_ = {};
        feedback.play(hui::audio::Cue::back);
        break;
    default:
        refuse(feedback, input);
        break;
    }
}

// The open panel's buttons: a dialog's, the operation's, or an entry's actions.
std::vector<LibraryScreen::Control> LibraryScreen::files_panel_controls() const
{
    const ui::Type &type = ctx_.type;
    std::vector<Control> out;
    const float left = 960.0f - kPanelWidth * 0.5f + 56.0f;
    const auto add_row = [&](const std::vector<std::pair<int, std::string>> &buttons, float y, int row)
    {
        float x = left;
        for (std::size_t i = 0; i < buttons.size(); ++i)
        {
            const float w = ui::button_width(type, buttons[i].second, 18.0f,
                                             i == 0 ? ui::ButtonKind::primary : ui::ButtonKind::secondary);
            out.push_back({Kind::action, buttons[i].first, row, {x, y, w, 48.0f}});
            x += w + 12.0f;
        }
    };
    if (file_dialog_ == 3)
    {
        const FileEntry *entry = file_entry(file_dialog_entry_);
        add_row({{0, tr("Delete")}, {1, tr("Back")}}, kDeleteTop + delete_buttons(type, entry != nullptr && entry->folder), 0);
        return out;
    }
    if (file_dialog_ != 0)
        return out;
    if (const FileOperation *o = file_activity(); o != nullptr && activity_open())
    {
        std::vector<std::pair<int, std::string>> buttons;
        const bool ended = o->ended();
        if (o->state == "password")
            buttons.push_back({kPassword, tr("Enter password")});
        if (!ended && o->state != "password" && (o->action == "copy" || o->action == "move"))
            buttons.push_back({kPause, o->state == "paused" ? tr("Resume") : tr("Pause")});
        // The PS5's installer can't be stopped once it has a package.
        if (!ended && o->action != "install")
            buttons.push_back({kCancel, tr("Cancel operation")});
        buttons.push_back({kHide, ended ? tr("Done") : tr("Hide")});
        add_row(buttons, kActivityTop + activity_layout(*o, !o->error.empty() || !files_error_.empty()).buttons, 0);
        return out;
    }
    const FileEntry *entry = file_entry(file_selected_);
    if (entry == nullptr)
        return out;
    // The actions, wrapping to a second row when they don't fit.
    float x = left, y = kDetailsTop + (entry->locked.empty() ? 168.0f : 206.0f);
    int row = 0;
    const std::vector<FileAct> actions = file_actions(*entry);
    for (int i = 0; i < static_cast<int>(actions.size()); ++i)
    {
        const FileAct &act = actions[static_cast<std::size_t>(i)];
        const float w = ui::button_width(type, act.label, 18.0f, act.primary ? ui::ButtonKind::primary : ui::ButtonKind::secondary);
        if (x + w > left + kPanelWidth - 112.0f)
        {
            x = left;
            y += 60.0f;
            ++row;
        }
        out.push_back({Kind::action, i, row, {x, y, w, 48.0f}});
        x += w + 12.0f;
    }
    return out;
}

void LibraryScreen::update_files_panel(const InputFrame &input, Feedback &feedback)
{
    const bool busy = ctx_.store.file_action().busy;
    if (file_dialog_ == 1 || file_dialog_ == 2 || file_dialog_ == 4)
    {
        // Sent, or refused: Circle closes it, Cross types again.
        if (input.is_pressed(Action::back) && !busy)
            close_file_dialog(feedback);
        else if (input.is_pressed(Action::confirm) && !busy)
        {
            keyboard_for_ = 2;
            keyboard_open_ = true;
            keyboard_.enter();
            keyboard_.set_length(static_cast<int>(file_text_.size()));
            feedback.play(hui::audio::Cue::modal_open);
        }
        return;
    }
    if (input.is_pressed(Action::back))
    {
        if (file_dialog_ == 3)
        {
            if (!busy)
                close_file_dialog(feedback);
        }
        else if (activity_open())
            hide_activity(feedback);
        else
        {
            file_selected_.clear();
            feedback.play(hui::audio::Cue::modal_close);
        }
        return;
    }
    const std::vector<Control> controls = files_panel_controls();
    if (controls.empty())
    {
        if (input.is_pressed(Action::confirm))
            refuse(feedback, input);
        return;
    }
    const int count = static_cast<int>(controls.size());
    int at = file_focus_ < 0 ? count - 1 : std::min(file_focus_, count - 1);
    if (input.nav != Direction::none)
    {
        int next = -1;
        const Control &now = controls[static_cast<std::size_t>(at)];
        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            const int to = at + (input.nav == Direction::right ? 1 : -1);
            if (to >= 0 && to < count && controls[static_cast<std::size_t>(to)].row == now.row)
                next = to;
        }
        else
        {
            const int row = now.row + (input.nav == Direction::down ? 1 : -1);
            for (int i = 0; i < count && next < 0; ++i)
            {
                if (controls[static_cast<std::size_t>(i)].row == row)
                    next = i;
            }
        }
        if (next >= 0)
        {
            at = next;
            feedback.play(hui::audio::Cue::focus);
        }
        else
            refuse(feedback, input);
    }
    file_focus_ = at;
    if (!input.is_pressed(Action::confirm))
        return;
    const int index = controls[static_cast<std::size_t>(at)].index;
    if (file_dialog_ == 3)
    {
        if (index == 0)
            submit_file_dialog(feedback, input);
        else if (!busy)
            close_file_dialog(feedback);
        return;
    }
    if (activity_open())
    {
        const FileOperation *o = file_activity();
        if (index == kPassword)
            open_file_dialog(4, {}, feedback);
        else if (index == kPause && o != nullptr)
            operate(o->state == "paused" ? "resume" : "pause", feedback, input);
        else if (index == kCancel)
            operate("cancel", feedback, input);
        else
            hide_activity(feedback);
        return;
    }
    if (const FileEntry *entry = file_entry(file_selected_))
    {
        const std::vector<FileAct> actions = file_actions(*entry);
        if (index < static_cast<int>(actions.size()))
        {
            const FileEntry copy = *entry;
            file_act(actions[static_cast<std::size_t>(index)].key, copy, feedback, input);
            if (actions[static_cast<std::size_t>(index)].key != 'z')
                file_selected_.clear();
        }
    }
}

void LibraryScreen::draw_files(Frame &frame, hui::gfx::DrawList &list, float y, const std::vector<Control> &controls) const
{
    (void)frame;
    const Snapshot &state = ctx_.store.state();
    const ui::Type &type = ctx_.type;
    const auto amount = [&](Kind kind, int index)
    {
        for (std::size_t i = 0; i < controls.size() && i < focus_amount_.size(); ++i)
        {
            if (controls[i].kind == kind && controls[i].index == index)
                return focus_amount_[i];
        }
        return 0.0f;
    };
    const auto rect = [&](Kind kind, int index)
    {
        for (const Control &c : controls)
        {
            if (c.kind == kind && c.index == index)
                return c.rect;
        }
        return ui::Rect{};
    };
    const float right = 1920.0f - ui::kGutter;
    // ---- .storage-overview-heading: the place, then its actions ----
    float actions_left = right;
    for (const Control &c : controls)
    {
        if (c.kind == Kind::files_up || c.kind == Kind::files_new || c.kind == Kind::files_activity)
            actions_left = std::min(actions_left, c.rect.x);
    }
    const Drive *drive = files_drive();
    if (files_drive_.empty())
    {
        ui::text(list, type.light, tr("Your drives"), ui::kGutter, y + 40.0f, 33.0f, ui::color::text);
        ui::text_fit(list, type.regular, tr("Browse, copy, move and delete files on your PS5\xE2\x80\x99s drives."), ui::kGutter,
                     y + 72.0f, 17.0f, actions_left - ui::kGutter - 30.0f, ui::color::text2);
    }
    else
    {
        ui::text_fit(list, type.light, files_title(), ui::kGutter, y + 40.0f, 33.0f, actions_left - ui::kGutter - 30.0f,
                     ui::color::text);
        ui::text_fit(list, type.regular,
                     files_path_.empty() ? (drive != nullptr ? drive_root(*drive) : std::string())
                                         : files_place(files_drive_, files_path_),
                     ui::kGutter, y + 72.0f, 17.0f, actions_left - ui::kGutter - 30.0f, ui::color::text2);
        const char *up = files_path_.empty() ? tr("All drives") : tr("Up one folder");
        ui::button(list, type, rect(Kind::files_up, 0), up, ui::ButtonKind::secondary, amount(Kind::files_up, 0), true, 18.0f);
        ui::button(list, type, rect(Kind::files_new, 0), tr("New folder"), ui::ButtonKind::secondary,
                   amount(Kind::files_new, 0), !files_running(), 18.0f);
    }
    if (rect(Kind::files_activity, 0).w > 0.0f)
        pill(list, type, rect(Kind::files_activity, 0), activity_line(), amount(Kind::files_activity, 0));
    y += 100.0f;
    // ---- the copy or move waiting for its folder ----
    if (!clip_.action.empty())
    {
        const ui::Rect box{ui::kGutter, y, 1720.0f, 74.0f};
        list.rounded_rect(box, 14.0f, ui::Color::rgb(0xffffff, 0.09f));
        const ui::Rect paste = rect(Kind::files_paste, 0);
        ui::text_fit(list, type.regular, clip_text(), box.x + 22.0f, box.cy() + 6.5f, 18.0f, paste.x - box.x - 50.0f,
                     ui::Color::rgb(0xd0d0d0));
        ui::button(list, type, paste, tr("Paste here"), ui::ButtonKind::primary, amount(Kind::files_paste, 0), can_paste(), 18.0f);
        ui::button(list, type, rect(Kind::files_unclip, 0), tr("Cancel"), ui::ButtonKind::secondary,
                   amount(Kind::files_unclip, 0), true, 18.0f);
        y += 94.0f;
    }
    if (files_drive_.empty())
    {
        // ---- .drive-grid ----
        if (state.drives.empty())
            ui::text(list, type.light, tr("No drives reported"), 960.0f, y + 120.0f, 32.0f, ui::color::text, ui::Align::center);
        for (int d = 0; d < static_cast<int>(state.drives.size()); ++d)
        {
            const Drive &it = state.drives[static_cast<std::size_t>(d)];
            const ui::Rect r = rect(Kind::files_drive, d);
            const float focus = amount(Kind::files_drive, d);
            ui::focus_ring(list, r, 18.0f, focus);
            list.rounded_rect(r, 18.0f, ui::Color::rgb(0xffffff, 0.06f + 0.06f * focus));
            icons::draw(list, it.external ? icons::Icon::drive : icons::Icon::layers, r.x + 38.0f, r.y + 36.0f, 24.0f,
                        ui::color::text, 1.8f);
            ui::text_fit(list, type.semibold, i18n::place(it.label), r.x + 62.0f, r.y + 43.0f, 20.0f, r.w - 88.0f, ui::color::text);
            ui::text_fit(list, type.regular, drive_root(it), r.x + 26.0f, r.y + 72.0f, 17.0f, r.w - 52.0f, ui::color::text2);
            const float used = it.total_bytes > 0.0 ? static_cast<float>(std::clamp((it.total_bytes - it.free_bytes) / it.total_bytes, 0.0, 1.0)) : 0.0f;
            ui::progress_bar(list, {r.x + 26.0f, r.y + 90.0f, r.w - 52.0f, 5.0f}, used, ui::color::accent);
            ui::text(list, type.medium, tr("{size} free", {{"size", format_bytes(it.free_bytes)}}), r.x + 26.0f, r.y + 126.0f,
                     18.0f, ui::color::text);
            ui::text_fit(list, type.regular,
                         tr("{used} used of {total}", {{"used", format_bytes(it.total_bytes - it.free_bytes)}, {"total", format_bytes(it.total_bytes)}}),
                         r.x + 26.0f, r.y + 152.0f, 17.0f, r.w - 52.0f, ui::color::text2);
        }
        return;
    }
    // ---- .library-toolbar: sort as tabs, then the search ----
    const ui::Rect first = rect(Kind::files_sort, 0);
    ui::surface(list, {first.x, first.y, 3.0f * kSortWidth, 58.0f}, 29.0f, ui::color::field);
    for (int s = 0; s < 3; ++s)
    {
        const ui::Rect r = rect(Kind::files_sort, s).inset(5.0f);
        const bool on = s == files_sort_;
        ui::focus_ring(list, r, r.h * 0.5f, amount(Kind::files_sort, s));
        if (on)
            list.rounded_rect(r, r.h * 0.5f, ui::color::text);
        ui::text_fit(list, type.medium, tr(kSorts[s]), r.cx(), r.cy() + 7.0f, 20.0f, r.w - 20.0f,
                     on ? ui::color::night1 : ui::color::text2, ui::Align::center);
    }
    const ui::Rect search = rect(Kind::files_search, 0);
    ui::focus_ring(list, search, search.h * 0.5f, amount(Kind::files_search, 0));
    ui::surface(list, search, search.h * 0.5f, keyboard_open_ && keyboard_for_ == 1 ? ui::color::field_open : ui::color::field);
    ui::icon_search(list, search.x + 34.0f, search.cy(), 24.0f, ui::color::text2);
    ui::text_fit(list, type.regular, files_query_.empty() ? tr("Search this folder") : files_query_, search.x + 60.0f,
                 search.cy() + 7.0f, 20.0f, search.w - 90.0f, files_query_.empty() ? ui::color::text3 : ui::color::text);
    y += 58.0f;
    const std::vector<const FileEntry *> entries = file_entries();
    std::string count = trn(static_cast<long>(entries.size()), "{count} item", "{count} items");
    if (state.files.truncated)
        count += std::string(" \xC2\xB7 ") + tr("Showing the first 2,000 items.");
    ui::text(list, type.regular, count, ui::kGutter, y + 34.0f, 17.0f, ui::color::text3);
    const std::string &error = !files_error_.empty() && file_dialog_ == 0 ? files_error_ : state.files_error;
    if (!error.empty())
        ui::text_fit(list, type.regular, tr(error), 960.0f, y + 34.0f, 17.0f, 860.0f, ui::color::bad);
    y += 52.0f;
    // ---- .storage-game-list: the folder ----
    if (entries.empty())
    {
        const bool reading = !state.have_files || state.files.storage_id != files_drive_ || state.files.path != files_path_;
        ui::text(list, type.light,
                 reading ? tr("Reading folder\xE2\x80\xA6") : !files_query_.empty() ? tr("No matching files") : tr("This folder is empty"),
                 960.0f, y + 90.0f, 32.0f, ui::color::text, ui::Align::center);
        return;
    }
    for (int i = 0; i < static_cast<int>(entries.size()); ++i)
    {
        const ui::Rect r = rect(Kind::files_entry, i);
        if (r.y - scroll_.value > 1080.0f || r.y + r.h - scroll_.value < 0.0f)
            continue;
        const FileEntry &entry = *entries[static_cast<std::size_t>(i)];
        const float focus = amount(Kind::files_entry, i);
        ui::focus_ring(list, r, 12.0f, focus);
        list.rounded_rect(r, 12.0f, ui::Color::rgb(0xffffff, 0.04f + 0.04f * focus));
        icons::draw(list, type_icon(entry.type), r.x + 40.0f, r.cy(), 26.0f, ui::color::text2, 1.8f);
        const std::string size = file_size(entry) >= 0.0 ? file_bytes(file_size(entry)) : std::string();
        const float size_w = size.empty() ? 0.0f : type.regular.measure(size, 17.0f) + 30.0f;
        ui::text_fit(list, type.medium, entry.name, r.x + 78.0f, r.y + 32.0f, 19.0f, r.w - 78.0f - 60.0f - size_w, ui::color::text);
        ui::text_fit(list, type.regular,
                     (entry.locked.empty() ? std::string() : std::string(tr("In use")) + " \xC2\xB7 ") + file_line(entry, true),
                     r.x + 78.0f, r.y + 57.0f, 16.0f, r.w - 78.0f - 60.0f - size_w, ui::color::text2);
        if (!size.empty())
            ui::text(list, type.regular, size, r.x + r.w - 48.0f, r.cy() + 6.0f, 17.0f, ui::color::text2, ui::Align::right);
        icons::draw(list, icons::Icon::next, r.x + r.w - 24.0f, r.cy(), 18.0f, ui::color::text2, 2.0f);
        if (entry.folder)
        {
            const ui::Rect more = rect(Kind::files_more, i);
            const float focused = amount(Kind::files_more, i);
            ui::focus_ring(list, more, 12.0f, focused);
            list.rounded_rect(more, 12.0f, ui::Color::rgb(0xffffff, .06f + .08f * focused));
            icons::draw(list, icons::Icon::more, more.cx(), more.cy(), 24.0f, ui::color::text, 1.8f);
        }
    }
}

void LibraryScreen::draw_files_panel(Frame &frame) const
{
    if (!files_modal())
        return;
    hui::gfx::DrawList &overlay = frame.overlay;
    const ui::Type &type = ctx_.type;
    const std::vector<Control> buttons = files_panel_controls();
    const int focused = buttons.empty() ? -1 : file_focus_ < 0 ? static_cast<int>(buttons.size()) - 1
                                                              : std::min(file_focus_, static_cast<int>(buttons.size()) - 1);
    const bool busy = ctx_.store.file_action().busy;
    const auto draw_buttons = [&](const std::vector<std::string> &labels, const std::vector<bool> &enabled)
    {
        for (std::size_t i = 0; i < buttons.size() && i < labels.size(); ++i)
            ui::button(overlay, type, buttons[i].rect, labels[i], i == 0 ? ui::ButtonKind::primary : ui::ButtonKind::secondary,
                       static_cast<int>(i) == focused && !keyboard_open_ ? 1.0f : 0.0f, enabled[i], 18.0f);
    };
    overlay.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, ui::color::night0.with_alpha(0.55f));
    const float left = 960.0f - kPanelWidth * 0.5f + 56.0f;
    const float width = kPanelWidth - 112.0f;
    const std::string &error = files_error_;
    if (file_dialog_ == 3)
    {
        // Delete: what goes, and that it can't be undone.
        const FileEntry *entry = file_entry(file_dialog_entry_);
        const bool folder = entry != nullptr && entry->folder;
        const float at = delete_buttons(type, folder);
        const ui::Rect panel{960.0f - kPanelWidth * 0.5f, kDeleteTop, kPanelWidth, at + 48.0f + (error.empty() ? 56.0f : 96.0f)};
        ui::modal(overlay, panel);
        ui::text_fit(overlay, type.light, tr("Delete {name}?", {{"name", file_dialog_entry_}}), left, panel.y + 92.0f, 36.0f,
                     width, ui::color::text);
        ui::paragraph(overlay, type.regular, delete_body(folder), left, panel.y + 142.0f, 19.0f, width, 29.0f, ui::color::text2, 3);
        ui::text_fit(overlay, type.regular,
                     files_place(files_drive_, files_path_.empty() ? file_dialog_entry_ : files_path_ + "/" + file_dialog_entry_),
                     left, panel.y + at - 36.0f, 16.0f, width, ui::color::text3);
        if (!error.empty())
            ui::text_fit(overlay, type.regular, tr(error), left, panel.y + at + 48.0f + 40.0f, 17.0f, width, ui::color::bad);
        draw_buttons({tr("Delete"), tr("Back")}, {!busy, !busy});
        return;
    }
    if (file_dialog_ != 0)
    {
        // A name or a password over the keyboard; the field shows what is typed.
        // The password's note takes two lines at most; the field and a line for an error follow.
        const float note = file_dialog_ == 4 ? 2.0f * 27.0f + 6.0f : 0.0f;
        const ui::Rect panel{960.0f - kPanelWidth * 0.5f, kNameTop, kPanelWidth, 130.0f + note + 44.0f + ui::kFieldHeight + 76.0f};
        ui::modal(overlay, panel);
        const std::string title = file_dialog_ == 1   ? tr("Rename {name}", {{"name", file_dialog_entry_}})
                                  : file_dialog_ == 2 ? std::string(tr("New folder"))
                                                      : std::string(tr("This archive needs a password"));
        ui::text_fit(overlay, type.light, title, left, panel.y + 92.0f, 36.0f, width, ui::color::text);
        const float y = panel.y + 130.0f + note;
        if (file_dialog_ == 4)
            ui::paragraph(overlay, type.regular,
                          tr("Orbit tried the passwords it knows. Enter the archive's password to unpack it."), left,
                          panel.y + 142.0f, 18.0f, width, 27.0f, ui::color::text2, 2);
        const ui::Rect field{left, y + 44.0f, width, ui::kFieldHeight};
        ui::field_label(overlay, type, field, file_dialog_ == 4 ? tr("Password") : tr("Name"));
        ui::surface(overlay, field, 12.0f, keyboard_open_ ? ui::color::field_open : ui::color::field);
        std::string shown = file_text_;
        if (file_dialog_ == 4)
        {
            shown.clear();
            for (std::size_t i = 0; i < file_text_.size(); ++i)
                shown += "\xE2\x80\xA2";
        }
        ui::text_fit(overlay, type.regular, shown, field.x + 20.0f, field.cy() + 7.0f, 20.0f, field.w - 40.0f, ui::color::text);
        if (!error.empty() || busy)
            ui::text_fit(overlay, type.regular, !error.empty() ? std::string(tr(error)) : std::string(tr("Saving\xE2\x80\xA6")), left,
                         field.y + field.h + 40.0f, 17.0f, width, !error.empty() ? ui::color::bad : ui::color::text3);
        return;
    }
    if (const FileOperation *o = file_activity(); o != nullptr && activity_open())
    {
        // .file-activity: what runs, from where to where, its progress and buttons.
        const ActivityLayout l = activity_layout(*o, !o->error.empty() || !error.empty());
        const ui::Rect panel{960.0f - kPanelWidth * 0.5f, kActivityTop, kPanelWidth, l.height};
        ui::modal(overlay, panel);
        ui::text(overlay, type.semibold, tr("FILE ACTIVITY"), left, panel.y + 64.0f, 14.4f, ui::color::accent);
        ui::text_fit(overlay, type.light, operation_title(*o), left, panel.y + 112.0f, 36.0f, width, ui::color::text);
        ui::text_fit(overlay, type.regular, files_place(o->storage_id, o->path), left, panel.y + 160.0f, 16.0f, width, ui::color::text3);
        if (!o->to_storage_id.empty())
            ui::text_fit(overlay, type.regular, tr("To {path}", {{"path", files_place(o->to_storage_id, o->to_path)}}), left,
                         panel.y + 186.0f, 16.0f, width, ui::color::text3);
        ui::text_fit(overlay, type.regular, operation_line(*o), left, panel.y + l.line, 19.0f, width, ui::color::text2);
        if (!o->ended() && o->state != "password")
        {
            ui::progress_bar(overlay, {left, panel.y + l.bar, width, 6.0f}, operation_progress(*o), ui::color::accent);
            ui::text(overlay, type.regular, operation_meter(*o), left, panel.y + l.meter, 16.0f, ui::color::text2);
        }
        if (!o->error.empty() || !error.empty())
            ui::text_fit(overlay, type.regular, tr(!o->error.empty() ? o->error : error), left, panel.y + l.error, 17.0f, width,
                         ui::color::bad);
        std::vector<std::string> labels;
        std::vector<bool> enabled;
        for (const Control &c : buttons)
        {
            labels.push_back(c.index == kPassword ? tr("Enter password")
                             : c.index == kPause  ? (o->state == "paused" ? tr("Resume") : tr("Pause"))
                             : c.index == kCancel ? tr("Cancel operation")
                             : o->ended()         ? tr("Done")
                                                  : tr("Hide"));
            enabled.push_back(c.index == kHide || !busy);
        }
        draw_buttons(labels, enabled);
        return;
    }
    const FileEntry *entry = file_entry(file_selected_);
    if (entry == nullptr)
        return;
    // As tall as its parts: the actions, then four facts.
    float bottom = kDetailsTop + 200.0f;
    for (const Control &c : buttons)
        bottom = std::max(bottom, c.rect.y + c.rect.h);
    const ui::Rect panel{960.0f - kPanelWidth * 0.5f, kDetailsTop, kPanelWidth,
                         bottom - kDetailsTop + 24.0f + 4.0f * 44.0f + (error.empty() ? 48.0f : 88.0f)};
    ui::modal(overlay, panel);
    // Like a Library game's details: the eyebrow, the name, the actions, the facts.
    const Drive *drive = files_drive();
    ui::text(overlay, type.semibold,
             std::string(type_name(entry->type)) + " \xC2\xB7 " + (drive != nullptr ? i18n::place(drive->label) : files_drive_),
             left, panel.y + 64.0f, 14.4f, ui::color::text3);
    ui::text_fit(overlay, type.light, entry->name, left, panel.y + 112.0f, 36.0f, width, ui::color::text);
    if (!entry->locked.empty())
        ui::text_fit(overlay, type.regular, locked_text(entry->locked), left, panel.y + 160.0f, 17.0f, width,
                     ui::Color::rgb(0xd0d0d0));
    const std::vector<FileAct> actions = file_actions(*entry);
    for (std::size_t i = 0; i < buttons.size() && i < actions.size(); ++i)
        ui::button(overlay, type, buttons[i].rect, actions[i].label,
                   actions[i].primary ? ui::ButtonKind::primary : ui::ButtonKind::secondary,
                   static_cast<int>(i) == focused ? 1.0f : 0.0f, actions[i].enabled, 18.0f);
    // .library-facts
    const std::vector<std::pair<std::string, std::string>> facts = file_facts(*entry);
    float label_w = 180.0f;
    for (const auto &fact : facts)
        label_w = std::max(label_w, type.regular.measure(fact.first, 17.0f) + 28.0f);
    for (std::size_t i = 0; i < facts.size(); ++i)
    {
        const float ry = bottom + 24.0f + static_cast<float>(i) * 44.0f;
        ui::text_fit(overlay, type.regular, facts[i].first, left, ry + 24.0f, 17.0f, label_w - 16.0f, ui::color::text3);
        ui::text_fit(overlay, type.regular, facts[i].second, left + label_w, ry + 24.0f, 18.0f, width - label_w, ui::color::text);
        overlay.rounded_rect({left, ry + 40.0f, width, 1.0f}, 0.0f, ui::color::line);
    }
    if (!error.empty())
        ui::text_fit(overlay, type.regular, tr(error), left, panel.y + panel.h - 44.0f, 17.0f, width, ui::color::bad);
}

std::vector<hui::ui::Hint> LibraryScreen::files_hints() const
{
    if (file_dialog_ == 1 || file_dialog_ == 2 || file_dialog_ == 4)
        return {{hui::ui::Button::cross, tr("Change")}, {hui::ui::Button::circle, tr("Back")}};
    if (files_modal())
        return {{hui::ui::Button::cross, tr("Select")},
                {hui::ui::Button::circle, activity_open() && file_dialog_ == 0 ? tr("Hide") : tr("Close")}};
    std::vector<hui::ui::Hint> items = {{hui::ui::Button::cross, tr("Select")}};
    if (!files_drive_.empty())
        items.push_back({hui::ui::Button::circle, files_path_.empty() ? tr("All drives") : tr("Up one folder")});
    items.push_back({hui::ui::Button::l1, tr("Tabs"), hui::ui::Button::r1});
    return items;
}

// ---------------------------------------------------------------- Crossbar

std::vector<LibraryScreen::XbEntry> LibraryScreen::files_crossbar_entries() const
{
    // The hidden operation first; the drives, or a folder's entries between
    // Paste here and New folder.
    const Snapshot &state = ctx_.store.state();
    std::vector<XbEntry> out;
    if (file_activity() != nullptr && !activity_open())
        out.push_back({'a', 0});
    if (files_drive_.empty())
    {
        if (state.library.has_storage_job && state.library.storage_job.id > 0)
            out.push_back({'j', 0});
        for (int d = 0; d < static_cast<int>(state.drives.size()); ++d)
            out.push_back({'f', d});
        if (state.drives.empty())
            out.push_back({'K', 0});
        return out;
    }
    if (!clip_.action.empty())
        out.push_back({'p', 0});
    const int count = static_cast<int>(file_entries().size());
    for (int i = 0; i < count; ++i)
        out.push_back({'e', i});
    if (count == 0)
        out.push_back({'E', 0});
    out.push_back({'n', 0});
    return out;
}

void LibraryScreen::fill_files_row(const XbEntry &entry, xb::Row &row) const
{
    const Snapshot &state = ctx_.store.state();
    switch (entry.kind)
    {
    case 'a':
    {
        const FileOperation &o = *file_activity();
        row.key = "activity";
        row.primary = true;
        row.icon = o.action == "move" ? icons::Icon::move : o.action == "copy" ? icons::Icon::copy : icons::Icon::clock;
        row.title = operation_title(o);
        row.sub = operation_line(o);
        if (!o.ended() && o.state != "password")
        {
            row.bar = operation_progress(o);
            row.detail = operation_meter(o);
        }
        row.warn = o.state == "failed" || o.state == "password";
        row.hint = tr("Open");
        break;
    }
    case 'f':
    {
        const Drive &drive = state.drives[static_cast<std::size_t>(entry.index)];
        row.key = "f:" + drive.id;
        row.icon = icons::Icon::drive;
        row.folder = true;
        row.title = i18n::place(drive.label);
        row.sub = tr("{free} free of {total}", {{"free", format_bytes(drive.free_bytes)}, {"total", format_bytes(drive.total_bytes)}});
        row.bar = drive.total_bytes > 0.0 ? static_cast<float>(std::clamp((drive.total_bytes - drive.free_bytes) / drive.total_bytes, 0.0, 1.0)) : 0.0f;
        row.detail = drive_root(drive) + " \xC2\xB7 " +
                     tr("{used} used of {total}", {{"used", format_bytes(drive.total_bytes - drive.free_bytes)}, {"total", format_bytes(drive.total_bytes)}});
        break;
    }
    case 'K':
        row.key = "no-drives";
        row.icon = icons::Icon::drive;
        row.title = tr("No drives reported");
        break;
    case 'p':
        row.key = "paste";
        row.icon = clip_.action == "copy" ? icons::Icon::copy : icons::Icon::move;
        row.title = tr("Paste here");
        row.sub = clip_text();
        row.off = !can_paste();
        break;
    case 'e':
    {
        const std::vector<const FileEntry *> entries = file_entries();
        const FileEntry &it = *entries[static_cast<std::size_t>(entry.index)];
        row.key = "e:" + it.name;
        row.icon = type_icon(it.type);
        row.title = it.name;
        if (!it.locked.empty())
        {
            row.tag = tr("In use");
            row.tag_kind = xb::Tag::warn;
        }
        row.sub = file_line(it, true);
        row.folder = it.folder;
        break;
    }
    case 'E':
    {
        const bool reading = !state.have_files || state.files.storage_id != files_drive_ || state.files.path != files_path_;
        row.key = "empty";
        row.icon = icons::Icon::folder;
        row.title = reading ? tr("Reading folder\xE2\x80\xA6") : !files_query_.empty() ? tr("No matching files") : tr("This folder is empty");
        if (!state.files_error.empty())
        {
            row.sub = tr(std::string_view(state.files_error));
            row.warn = true;
        }
        break;
    }
    default:
        row.key = "new-folder";
        row.icon = icons::Icon::folder;
        row.title = tr("New folder");
        row.off = files_running();
        break;
    }
}

xb::Band LibraryScreen::files_band() const
{
    // Delete, a name or password on its way, or the operation, as Store's dialogs say them.
    xb::Band band;
    const bool busy = ctx_.store.file_action().busy;
    if (file_dialog_ == 3)
    {
        const FileEntry *entry = file_entry(file_dialog_entry_);
        band.title = tr("Delete {name}?", {{"name", file_dialog_entry_}});
        band.body = delete_body(entry != nullptr && entry->folder);
        band.note = files_place(files_drive_, files_path_.empty() ? file_dialog_entry_ : files_path_ + "/" + file_dialog_entry_);
        band.buttons = {tr("Delete"), tr("Back")};
        band.busy = busy;
    }
    else if (file_dialog_ != 0)
    {
        band.title = file_dialog_ == 1   ? tr("Rename {name}", {{"name", file_dialog_entry_}})
                     : file_dialog_ == 2 ? std::string(tr("New folder"))
                                         : std::string(tr("This archive needs a password"));
        if (file_dialog_ != 4)
            band.body = file_text_;
        band.busy = busy;
        band.buttons = {file_dialog_ == 1 ? tr("Rename") : file_dialog_ == 2 ? tr("Create folder") : tr("Unpack"), tr("Back")};
    }
    else if (const FileOperation *o = file_activity(); o != nullptr && activity_open())
    {
        band.kicker = tr("File activity");
        band.title = operation_title(*o);
        band.body = operation_line(*o);
        if (!o->ended() && o->state != "password")
            band.body += " \xC2\xB7 " + operation_meter(*o);
        band.note = files_place(o->storage_id, o->path);
        if (!o->to_storage_id.empty())
            band.note += "  \xE2\x86\x92  " + files_place(o->to_storage_id, o->to_path);
        if (!o->error.empty())
            band.error = tr(std::string_view(o->error));
        for (const Control &c : files_panel_controls())
            band.buttons.push_back(c.index == kPassword ? tr("Enter password")
                                   : c.index == kPause  ? (o->state == "paused" ? tr("Resume") : tr("Pause"))
                                   : c.index == kCancel ? tr("Cancel operation")
                                   : o->ended()         ? tr("Done")
                                                        : tr("Hide"));
    }
    if (!files_error_.empty())
        band.error = tr(std::string_view(files_error_));
    const int count = static_cast<int>(band.buttons.size());
    band.focus = xb_band_ < 0 ? count - 1 : std::min(xb_band_, std::max(0, count - 1));
    return band;
}

// A band's buttons, across: Cross presses one, Circle closes it (Crossbar, Panorama).
void LibraryScreen::update_files_band(const InputFrame &input, Feedback &feedback)
{
    const bool busy = ctx_.store.file_action().busy;
    const xb::Band band = files_band();
    const int count = static_cast<int>(band.buttons.size());
    int at = band.focus;
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const int to = at + (input.nav == Direction::right ? 1 : -1);
        if (to >= 0 && to < count)
        {
            at = to;
            feedback.play(hui::audio::Cue::focus);
        }
        else
            refuse(feedback, input);
    }
    xb_band_ = at;
    const bool back = input.is_pressed(Action::back) || (input.is_pressed(Action::confirm) && at == count - 1);
    if (back)
    {
        if (file_dialog_ != 0)
        {
            if (!busy)
                close_file_dialog(feedback);
        }
        else
            hide_activity(feedback);
    }
    else if (input.is_pressed(Action::confirm))
    {
        if (file_dialog_ == 3)
            submit_file_dialog(feedback, input);
        else if (file_dialog_ != 0)
        {
            if (busy)
                refuse(feedback, input);
            else
            {
                keyboard_for_ = 2;
                keyboard_open_ = true;
                keyboard_.enter();
                keyboard_.set_length(static_cast<int>(file_text_.size()));
                feedback.play(hui::audio::Cue::modal_open);
            }
        }
        else
        {
            const std::vector<Control> controls = files_panel_controls();
            const int index = at < static_cast<int>(controls.size()) ? controls[static_cast<std::size_t>(at)].index : kHide;
            const FileOperation *o = file_activity();
            if (index == kPassword)
                open_file_dialog(4, {}, feedback);
            else if (index == kPause && o != nullptr)
                operate(o->state == "paused" ? "resume" : "pause", feedback, input);
            else if (index == kCancel)
                operate("cancel", feedback, input);
        }
    }
}

bool LibraryScreen::update_files_crossbar(const InputFrame &input, Feedback &feedback, const xb::Row &row)
{
    // Returns true when Files used the input.
    if (band_open(file_dialog_, activity_open()))
    {
        update_files_band(input, feedback);
        return true;
    }
    if (xb_menu_.open)
    {
        const int chosen = xb_menu_.update(input, crossbar_menu(), feedback);
        if (chosen < 0)
            return true;
        if (xb_menu_kind_ == 3)
        {
            // An entry's actions.
            if (const FileEntry *entry = file_entry(files_pick_))
            {
                const std::vector<FileAct> actions = file_actions(*entry);
                if (chosen < static_cast<int>(actions.size()))
                {
                    const FileEntry copy = *entry;
                    file_act(actions[static_cast<std::size_t>(chosen)].key, copy, feedback, input);
                }
            }
        }
        return true;
    }
    const std::string &key = row.key;
    if (input.is_pressed(Action::back))
    {
        if (!files_up())
        {
            leave_files();
            xb_.focus_key(crossbar_list(), "files");
        }
        feedback.play(hui::audio::Cue::back);
        return true;
    }
    if (input.is_pressed(Action::north))
    {
        // Triangle reads the folder (or the drives) again.
        if (files_drive_.empty())
            ctx_.store.refresh_now();
        else
            ctx_.store.open_files(files_drive_, files_path_);
        feedback.play(hui::audio::Cue::select);
        return true;
    }
    if (input.is_pressed(Action::west) && !files_drive_.empty())
    {
        keyboard_for_ = 1;
        keyboard_open_ = true;
        keyboard_.enter();
        keyboard_.set_length(static_cast<int>(files_query_.size()));
        feedback.play(hui::audio::Cue::modal_open);
        return true;
    }
    const bool menu = input.is_pressed(Action::menu);
    if (!input.is_pressed(Action::confirm) && !menu)
        return false;
    if (key.rfind("e:", 0) == 0)
    {
        files_pick_ = key.substr(2);
        const FileEntry *entry = file_entry(files_pick_);
        if (entry != nullptr && entry->folder && !menu)
        {
            open_folder(files_drive_, file_path(*entry));
            feedback.play(hui::audio::Cue::open);
        }
        else if (entry != nullptr)
        {
            xb_menu_kind_ = 3;
            xb_menu_.show(0);
            feedback.play(hui::audio::Cue::modal_open);
        }
        return true;
    }
    if (key.rfind("f:", 0) == 0 && !menu)
    {
        open_folder(key.substr(2), {});
        feedback.play(hui::audio::Cue::open);
        return true;
    }
    if (menu)
    {
        refuse(feedback, input);
        return true;
    }
    if (key == "activity")
        show_activity(feedback);
    else if (key == "paste")
        paste_files(feedback, input);
    else if (key == "new-folder")
    {
        if (files_running())
            refuse(feedback, input);
        else
            open_file_dialog(2, {}, feedback);
    }
    else if (key == "job")
        return false;
    else
        refuse(feedback, input);
    return true;
}

void LibraryScreen::draw_files_crossbar(Frame &frame, const xb::Row &row) const
{
    // Beside the list: the focused drive's space, or the focused entry's details.
    hui::gfx::DrawList &list = frame.scene;
    const Snapshot &state = ctx_.store.state();
    const ui::Type &type = ctx_.type;
    // The hidden operation's line (its row is first in the list), then an error.
    std::vector<std::string> status;
    if (file_activity() != nullptr && !activity_open())
        status.push_back(activity_line());
    if (!files_error_.empty())
        status.emplace_back(tr(std::string_view(files_error_)));
    if (files_drive_.empty())
    {
        const Drive *drive = nullptr;
        for (const Drive &d : state.drives)
            if (row.key == "f:" + d.id)
                drive = &d;
        if (drive == nullptr && !state.drives.empty())
            drive = &state.drives.front();
        if (drive == nullptr)
            return;
        xb::draw_panel(list, 430.0f, 0.0f, 0.0f, [&](hui::gfx::DrawList &out, float x, float y, float w)
        {
            // The drive itself: its space and where it is.
            y = xb::draw_label(ctx_, out, x, y, i18n::place(drive->label));
            y = ui::paragraph(out, type.regular, tr("{size} free", {{"size", format_bytes(drive->free_bytes)}}), x,
                              y + 12.0f + 15.5f + 6.6f, 20.0f, w, 31.0f, ui::Color::rgb(0xffffff), 2) - 22.1f + 31.0f;
            y = ui::paragraph(out, type.regular,
                              tr("{used} used of {total}", {{"used", format_bytes(drive->total_bytes - drive->free_bytes)},
                                                           {"total", format_bytes(drive->total_bytes)}}),
                              x, y + 4.0f + 15.5f + 6.6f, 18.0f, w, 28.0f, ui::Color::rgb(0xffffff, 0.7f), 2) - 22.1f + 28.0f;
            y = ui::paragraph(out, type.regular, drive_root(*drive), x, y + 4.0f + 15.5f + 6.6f, 18.0f, w, 28.0f,
                              ui::Color::rgb(0xffffff, 0.7f), 2) - 22.1f + 28.0f;
            if (!clip_.action.empty())
                y = ui::paragraph(out, type.regular, clip_text(), x, y + 14.0f + 11.6f + 5.3f, 16.0f, w, 23.2f,
                                  ui::Color::rgb(0xffd27a), 3) - 16.9f + 23.2f;
            for (const std::string &note : status)
                y = ui::paragraph(out, type.regular, note, x, y + 14.0f + 11.6f + 5.3f, 16.0f, w, 23.2f,
                                  ui::Color::rgb(0xffd27a), 3) - 16.9f + 23.2f;
            return y;
        });
        return;
    }
    const FileEntry *entry = row.key.rfind("e:", 0) == 0 ? file_entry(row.key.substr(2)) : nullptr;
    xb::draw_panel(list, 400.0f, 0.0f, 0.0f, [&](hui::gfx::DrawList &out, float x, float y, float w)
    {
        if (entry == nullptr)
        {
            y = xb::draw_label(ctx_, out, x, y, trn(static_cast<long>(file_entries().size()), "{count} item", "{count} items"));
            y = ui::paragraph(out, type.regular,
                              clip_.action.empty() ? std::string(tr("Browse, copy, move and delete files on your PS5\xE2\x80\x99s drives."))
                                                   : clip_text(),
                              x, y + 12.0f + 15.5f + 6.6f, 20.0f, w, 31.0f, ui::Color::rgb(0xffffff, 0.82f), 4) -
                22.1f + 31.0f;
        }
        else
        {
            const Drive *drive = files_drive();
            y = xb::draw_label(ctx_, out, x, y,
                               std::string(type_name(entry->type)) + " \xC2\xB7 " + (drive != nullptr ? i18n::place(drive->label) : files_drive_));
            y = xb::draw_facts(ctx_, out, x, y + 18.0f, w, {file_facts(*entry)[0], file_facts(*entry)[1], file_facts(*entry)[2]}, 2);
            ui::text(out, type.regular, tr("Location"), x, y + 22.0f + 14.4f + 5.9f, 18.0f, ui::Color::rgb(0xffffff, 0.62f));
            y += 22.0f + 28.8f + 4.0f;
            y = ui::paragraph(out, type.regular, files_place(files_drive_, file_path(*entry)), x, y + 14.4f + 5.9f, 18.0f, w, 26.0f,
                              ui::Color::rgb(0xffffff), 3) - 20.3f + 26.0f;
            if (!entry->locked.empty())
                y = ui::paragraph(out, type.regular, locked_text(entry->locked), x, y + 16.0f + 12.0f + 5.3f, 17.0f, w, 24.0f,
                                  ui::Color::rgb(0xffd27a), 3) - 17.3f + 24.0f + 4.0f;
        }
        for (const std::string &note : status)
            y = ui::paragraph(out, type.regular, note, x, y + 16.0f + 12.0f + 5.3f, 17.0f, w, 24.0f, ui::Color::rgb(0xffd27a), 3) -
                17.3f + 24.0f + 4.0f;
        return y;
    });
}

// ---------------------------------------------------------------- Panorama

// The drives as wide tiles (Cross opens one): Files' own first page, and the
// way into Files on Library's page, where the games' scan and sizes follow.
void LibraryScreen::files_drive_groups(pano::Page &page, bool tools) const
{
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    pano::Group drives;
    drives.title = pano::lower(tr("Files"));
    drives.pattern = pano::Pattern::wide;
    const int nd = static_cast<int>(state.drives.size());
    const bool activity = file_activity() != nullptr && !activity_open();
    drives.size = std::max(1, nd) + (activity ? 1 : 0);
    drives.focusable = [nd](int i) { return nd > 0 || i > 0; };
    drives.fill = [this, nd](int i, pano::Tile &tile)
    {
        const Snapshot &now = ctx_.store.state();
        tile.has_icon = true;
        if (i >= std::max(1, nd))
        {
            if (const FileOperation *o = file_activity())
            {
                tile.key = "activity";
                tile.icon = icons::Icon::clock;
                tile.label = activity_line();
                tile.has_bg = true;
                tile.bg = look::accent_color();
                if (!o->ended() && o->state != "password")
                    tile.bar = operation_progress(*o);
            }
            return;
        }
        if (nd == 0)
        {
            tile.key = "no-drives";
            tile.icon = icons::Icon::drive;
            tile.label = tr("No drives reported");
            tile.off = true;
            return;
        }
        const Drive &d = now.drives[static_cast<std::size_t>(i)];
        tile.key = "drive:" + d.id;
        tile.icon = icons::Icon::drive;
        tile.label = i18n::place(d.label);
        tile.sub = tr("{free} free of {total}", {{"free", format_bytes(d.free_bytes)}, {"total", format_bytes(d.total_bytes)}}) +
                   " \xC2\xB7 " + drive_root(d);
        tile.bar = d.total_bytes > 0.0 ? static_cast<float>(std::clamp((d.total_bytes - d.free_bytes) / d.total_bytes, 0.0, 1.0)) : 0.0f;
    };
    page.groups.push_back(std::move(drives));
    if (!tools || !state.have_library || !library.ready())
        return;

    pano::Group storage;
    storage.title = pano::lower(tr("Storage"));
    storage.pattern = pano::Pattern::small;
    // The scan, for games Orbit didn't add itself; sizes are measured on their own.
    if (!library.can("rescan"))
        return;
    storage.size = 1;
    storage.fill = [this](int, pano::Tile &tile)
    {
        const LibrarySnapshot &lib = ctx_.store.state().library;
        tile.has_icon = true;
        tile.key = "scan";
        tile.icon = icons::Icon::scan;
        tile.label = tr("Scan for games");
        tile.off = lib.working();
    };
    page.groups.push_back(std::move(storage));

}

pano::Page LibraryScreen::files_panorama_page() const
{
    // A folder: its entries as wide tiles after Up, the picked entry's own
    // group (its details, then its actions), then the folder's tools. With
    // no drive open: the drives.
    pano::Page page;
    if (files_drive_.empty())
    {
        files_drive_groups(page, false);
        return page;
    }
    const std::vector<const FileEntry *> entries = file_entries();
    pano::Group folder;
    folder.title = pano::lower(files_title());
    folder.count = i18n::count(static_cast<long>(entries.size()));
    folder.pattern = pano::Pattern::wide;
    folder.size = 1 + std::max(1, static_cast<int>(entries.size()));
    const bool empty = entries.empty();
    folder.focusable = [empty](int i) { return !(empty && i == 1); };
    folder.fill = [this](int i, pano::Tile &tile)
    {
        tile.has_icon = true;
        if (i == 0)
        {
            tile.key = "up";
            tile.icon = icons::Icon::back;
            tile.label = files_path_.empty() ? tr("All drives") : tr("Up one folder");
            return;
        }
        const std::vector<const FileEntry *> now = file_entries();
        if (now.empty())
        {
            const Snapshot &state = ctx_.store.state();
            const bool reading = !state.have_files || state.files.storage_id != files_drive_ || state.files.path != files_path_;
            tile.key = "empty";
            tile.icon = icons::Icon::folder;
            tile.label = reading ? tr("Reading folder\xE2\x80\xA6") : !files_query_.empty() ? tr("No matching files") : tr("This folder is empty");
            tile.off = true;
            return;
        }
        if (i - 1 >= static_cast<int>(now.size()))
            return;
        const FileEntry &entry = *now[static_cast<std::size_t>(i - 1)];
        tile.key = "e:" + entry.name;
        tile.icon = type_icon(entry.type);
        tile.label = entry.name;
        tile.sub = file_line(entry, false);
        tile.selected = entry.name == files_pick_;
        tile.bar_warn = !entry.locked.empty();
    };
    page.groups.push_back(std::move(folder));

    if (const FileEntry *entry = file_entry(files_pick_))
    {
        const std::vector<FileAct> actions = file_actions(*entry);
        pano::Group own;
        own.title = pano::lower(entry->name);
        own.pattern = pano::Pattern::custom;
        own.places = {{0, 0, 2, 2}, {2, 0, 2, 1}, {2, 1, 1, 1}, {3, 1, 1, 1}, {2, 2, 1, 1}, {3, 2, 1, 1}, {4, 0, 1, 1}, {4, 1, 1, 1}, {4, 2, 1, 1}};
        own.size = 1 + std::min(8, static_cast<int>(actions.size()));
        own.focusable = [](int i) { return i != 0; };
        const FileEntry copy = *entry;
        own.fill = [this, actions, copy](int i, pano::Tile &tile)
        {
            if (i == 0)
            {
                // Its details: the kind, the size large, when it changed and whether it's in use.
                const double size = file_size(copy);
                tile.key = "details";
                tile.kicker = type_name(copy.type);
                tile.label = size >= 0.0 ? file_bytes(size) : copy.folder ? tr("Not measured") : tr("Unavailable");
                tile.label_size = 40.0f;
                tile.sub = (copy.modified > 0.0 ? tr("Modified") + std::string(" ") + file_date(copy.modified) : std::string()) +
                           (copy.locked.empty() ? std::string() : " \xC2\xB7 " + locked_text(copy.locked));
                return;
            }
            const FileAct &act = actions[static_cast<std::size_t>(i - 1)];
            tile.key = std::string("act:") + act.key;
            tile.has_icon = true;
            tile.icon = act.icon;
            tile.label = act.label;
            tile.off = !act.enabled;
            if (i == 1 && act.primary)
            {
                tile.has_bg = true;
                tile.bg = look::accent_color();
            }
        };
        page.groups.push_back(std::move(own));
    }

    // This folder: the hidden operation, Paste here, New folder, Search and Refresh.
    pano::Group tools;
    tools.title = pano::lower(tr("This folder"));
    tools.pattern = pano::Pattern::custom;
    std::vector<char> kinds;
    if (file_activity() != nullptr && !activity_open())
        kinds.push_back('a');
    if (!clip_.action.empty())
        kinds.push_back('p');
    kinds.push_back('n');
    kinds.push_back('s');
    kinds.push_back('r');
    int col = 0, at = 0;
    for (char k : kinds)
    {
        if (k == 'a')
        {
            tools.places.push_back({0, 0, 2, 1});
            continue;
        }
        const int base = kinds.front() == 'a' ? 1 : 0;
        tools.places.push_back({col, base + at % (pano::kRows - base), 1, 1});
        if (++at % (pano::kRows - base) == 0)
            ++col;
    }
    tools.size = static_cast<int>(kinds.size());
    tools.fill = [this, kinds](int i, pano::Tile &tile)
    {
        tile.has_icon = true;
        switch (kinds[static_cast<std::size_t>(i)])
        {
        case 'a':
            if (const FileOperation *o = file_activity())
            {
                tile.key = "activity";
                tile.icon = icons::Icon::clock;
                tile.label = activity_line();
                tile.has_bg = true;
                tile.bg = look::accent_color();
                if (!o->ended() && o->state != "password")
                    tile.bar = operation_progress(*o);
            }
            break;
        case 'p':
            tile.key = "paste";
            tile.icon = clip_.action == "copy" ? icons::Icon::copy : icons::Icon::move;
            tile.label = tr("Paste here");
            tile.has_bg = true;
            tile.bg = look::accent_color();
            tile.off = !can_paste();
            break;
        case 'n':
            tile.key = "new-folder";
            tile.icon = icons::Icon::folder;
            tile.label = tr("New folder");
            tile.off = files_running();
            break;
        case 's':
            tile.key = "search";
            tile.icon = icons::Icon::search;
            tile.label = files_query_.empty() ? std::string(tr("Search this folder")) : "\xE2\x80\x9C" + files_query_ + "\xE2\x80\x9D";
            break;
        default:
            tile.key = "refresh";
            tile.icon = icons::Icon::refresh;
            tile.label = tr("Refresh");
            break;
        }
    };
    page.groups.push_back(std::move(tools));
    return page;
}

pano::AppBar LibraryScreen::files_panorama_bar() const
{
    // Options: the picked entry's actions, then the folder's.
    pano::AppBar bar;
    const auto add = [&](int tag, std::string title, bool off = false, bool danger = false)
    {
        pano::Item item;
        item.title = std::move(title);
        item.off = off;
        item.danger = danger;
        item.tag = tag;
        bar.items.push_back(std::move(item));
    };
    if (const FileEntry *entry = file_entry(files_pick_))
    {
        bar.head = entry->name;
        for (const FileAct &act : file_actions(*entry))
            add(act.key, act.label, !act.enabled, act.key == 'd');
    }
    else
        bar.head = files_title();
    if (!clip_.action.empty())
        add('P', tr("Paste here"), !can_paste());
    add('N', tr("New folder"), files_running());
    add('S', tr("Search this folder"));
    add('R', tr("Refresh"));
    bar.focus = pn_bar_focus_;
    return bar;
}

void LibraryScreen::files_panorama_run(char action, Feedback &feedback, const InputFrame &input)
{
    switch (action)
    {
    case 'P':
        paste_files(feedback, input);
        break;
    case 'N':
        if (files_running())
            refuse(feedback, input);
        else
            open_file_dialog(2, {}, feedback);
        break;
    case 'S':
        keyboard_for_ = 1;
        keyboard_open_ = true;
        keyboard_.enter();
        keyboard_.set_length(static_cast<int>(files_query_.size()));
        feedback.play(hui::audio::Cue::modal_open);
        break;
    case 'R':
        ctx_.store.open_files(files_drive_, files_path_);
        feedback.play(hui::audio::Cue::select);
        break;
    case 'a':
        show_activity(feedback);
        break;
    default:
        if (const FileEntry *entry = file_entry(files_pick_))
        {
            const FileEntry copy = *entry;
            file_act(action, copy, feedback, input);
        }
        else
            refuse(feedback, input);
        break;
    }
}

void LibraryScreen::update_files_panorama(const InputFrame &input, Feedback &feedback)
{
    // A folder's page, or the drives when Library has nothing else to show.
    pano::Page page = files_panorama_page();
    pano_.clamp(page);
    const std::string key = pano_.focused_key(page);
    if (files_drive_.empty() && !pn_bar_open_)
    {
        if (input.nav != Direction::none)
            pano_.move(page, input, feedback);
        else if (input.is_pressed(Action::back))
        {
            leave_files();
            feedback.play(hui::audio::Cue::back);
        }
        else if (input.is_pressed(Action::confirm))
        {
            if (key.rfind("drive:", 0) == 0)
            {
                open_folder(key.substr(6), {});
                feedback.play(hui::audio::Cue::open);
            }
            else if (key == "activity")
                show_activity(feedback);
            else
                refuse(feedback, input);
        }
        return;
    }
    if (pn_bar_open_)
    {
        pano::AppBar bar = files_panorama_bar();
        const int chosen = pano::update_appbar(bar, input, feedback);
        pn_bar_focus_ = bar.focus;
        if (chosen != -1)
            pn_bar_open_ = false;
        if (chosen >= 0)
            files_panorama_run(static_cast<char>(bar.items[static_cast<std::size_t>(chosen)].tag), feedback, input);
    }
    else if (input.nav != Direction::none)
        pano_.move(page, input, feedback);
    else if (input.is_pressed(Action::back))
    {
        if (!files_up() || files_drive_.empty())
        {
            // Out of the drive: Library's page, with the drive's tile in focus.
            const std::string drive = files_focus_;
            leave_files();
            pano_.focus_key(panorama_page(), "drive:" + drive);
        }
        feedback.play(hui::audio::Cue::back);
    }
    else if (input.is_pressed(Action::north))
        files_panorama_run('R', feedback, input);
    else if (input.is_pressed(Action::west))
        files_panorama_run('S', feedback, input);
    else if (input.is_pressed(Action::menu))
    {
        pn_bar_open_ = true;
        pn_bar_focus_ = 0;
        feedback.play(hui::audio::Cue::modal_open);
    }
    else if (input.is_pressed(Action::confirm))
    {
        if (key == "up")
        {
            const std::string drive = files_drive_;
            if (files_path_.empty())
            {
                leave_files();
                pano_.focus_key(panorama_page(), "drive:" + drive);
            }
            else
                files_up();
            feedback.play(hui::audio::Cue::back);
        }
        else if (key.rfind("e:", 0) == 0)
        {
            // A folder opens; a file's group takes the focus.
            const FileEntry *entry = file_entry(key.substr(2));
            if (entry != nullptr && entry->folder)
            {
                open_folder(files_drive_, file_path(*entry));
                feedback.play(hui::audio::Cue::open);
            }
            else if (entry != nullptr)
            {
                pano_.focus = {1, 1};
                feedback.play(hui::audio::Cue::open);
            }
        }
        else if (key.rfind("act:", 0) == 0)
            files_panorama_run(key[4], feedback, input);
        else if (key == "activity")
            files_panorama_run('a', feedback, input);
        else if (key == "paste")
            files_panorama_run('P', feedback, input);
        else if (key == "new-folder")
            files_panorama_run('N', feedback, input);
        else if (key == "search")
            files_panorama_run('S', feedback, input);
        else if (key == "refresh")
            files_panorama_run('R', feedback, input);
        else
            refuse(feedback, input);
    }
    // The picked entry follows the focus in the folder group.
    page = files_panorama_page();
    const std::string now_key = pano_.focused_key(page);
    if (now_key.rfind("e:", 0) == 0 && now_key.substr(2) != files_pick_)
    {
        files_pick_ = now_key.substr(2);
        page = files_panorama_page();
        pano_.focus_key(page, now_key);
    }
}

void LibraryScreen::draw_files_panorama(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const pano::Page page = files_panorama_page();
    pano::draw_header(ctx_, list, pano::lower(tr("Files")), pano_.scroll() * 0.35f);
    // Under the title: where this is, and what waits for a folder.
    std::string line = files_drive_.empty() ? std::string(tr("Browse, copy, move and delete files on your PS5\xE2\x80\x99s drives."))
                                            : files_place(files_drive_, files_path_);
    if (!clip_.action.empty())
        line += "  \xC2\xB7  " + clip_text();
    ui::text_fit(list, ctx_.type.regular, line, pano::kLeft, pano::baseline(256.0f, 22.0f), 22.0f, 1720.0f,
                 clip_.action.empty() ? pano::kDim : pano::kAmber);
    pano_.draw(ctx_, list, page, !pn_bar_open_);
    const std::string &error = !files_error_.empty() ? files_error_ : ctx_.store.state().files_error;
    if (!error.empty())
        ui::text_fit(list, ctx_.type.regular, tr(error), pano::kLeft, 1080.0f - 30.0f - 15.0f + 19.0f * 0.364f, 19.0f, 900.0f,
                     pano::kAmber);
    if (pn_bar_in_.value > 0.01f)
        pano::draw_appbar(ctx_, frame.overlay, files_panorama_bar(), pn_bar_in_.value);
}

std::vector<hui::ui::Hint> LibraryScreen::files_panorama_hints() const
{
    if (band_open(file_dialog_, activity_open()))
        return {};
    if (pn_bar_open_)
        return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Close")}};
    return {{hui::ui::Button::cross, tr("Select")},
            {hui::ui::Button::options, tr("Actions")},
            {hui::ui::Button::triangle, tr("Refresh")},
            {hui::ui::Button::circle, files_path_.empty() ? tr("All drives") : tr("Up one folder")}};
}

} // namespace orbit
