// Orbit Store TV app - Downloads: the console's queue, with progress and actions.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Views, labels and actions follow the browser storefront (src/Downloads.tsx):
// Active / Finished / Failed / Cancelled; Pause, Resume, Retry download;
// Cancel asks whether to keep or delete the partial file.

#include "orbit/screens.hpp"
#include "orbit/i18n.hpp"
#include "orbit/look.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace orbit
{

namespace
{

// The browser's Downloads page at 1920 x 1080: .page-heading, the
// .download-tabs chips, then .queue rows (70 rem at most, 0.4 rem apart).
constexpr float kTitleBaseline = 176.0f; // as the Games and Library titles
constexpr float kChipsY = 218.0f;
constexpr float kChipHeight = 42.0f;
constexpr float kListTop = 284.0f;
constexpr float kRowHeight = 210.0f;
constexpr float kRowGap = 8.0f;
constexpr float kRowWidth = 1920.0f - 2.0f * ui::kGutter; // as the grid of Games and Applications
constexpr float kRowLeft = (1920.0f - kRowWidth) * 0.5f; // the list sits in the middle of the screen, as Game updates
constexpr float kRowPad = 22.0f;
constexpr float kCover = 130.0f;
constexpr float kInfoWidth = kRowWidth - 2 * kRowPad - kCover - 28.0f;
constexpr float kPathLineHeight = 24.0f;
constexpr const char *kDialogButtons[] = {/* i18n */ "Keep partial file", /* i18n */ "Delete partial file", /* i18n */ "Go back"};
constexpr const char *kDeleteButtons[] = {/* i18n */ "Remove from history", /* i18n */ "Delete download", /* i18n */ "Go back"};
constexpr const char *kForgetButtons[] = {/* i18n */ "Go back", /* i18n */ "Remove from history"};

const Game *game_for(const Snapshot &state, const Job &job)
{
    if (const Game *game = state.game(job.game_id))
        return game;
    for (const Game &game : state.games)
    {
        for (const Release &release : game.releases)
        {
            if (release.id == job.release_id)
                return &game;
        }
    }
    return nullptr;
}

const Release *release_for(const Game *game, const Job &job)
{
    if (game == nullptr)
        return nullptr;
    for (const Release &release : game->releases)
    {
        if (release.id == job.release_id)
            return &release;
    }
    return nullptr;
}

// Paths often have no spaces. Wrap at UTF-8 boundaries without removing or
// abbreviating any part of the saved location.
std::vector<std::string> location_lines(const Job &job, const ui::Type &type)
{
    std::vector<std::string> lines;
    if (job.status != "complete" || job.path.empty()) return lines;
    std::string line;
    std::string previous;
    float width = 0.0f;
    for (std::size_t i = 0; i < job.path.size();)
    {
        std::size_t end = i + 1;
        while (end < job.path.size() && (static_cast<unsigned char>(job.path[end]) & 0xc0) == 0x80)
            ++end;
        const std::string next = job.path.substr(i, end - i);
        const float next_width = type.regular.measure(next, 18.0f);
        const float advance = previous.empty() ? next_width
            : type.regular.measure(previous + next, 18.0f) - type.regular.measure(previous, 18.0f);
        if (!line.empty() && width + advance > kInfoWidth)
        {
            lines.push_back(std::move(line));
            line.clear();
            width = next_width;
        }
        else width += advance;
        line += next;
        previous = next;
        i = end;
    }
    if (!line.empty()) lines.push_back(std::move(line));
    return lines;
}

float location_height(const std::vector<std::string> &lines)
{
    return lines.empty() ? 0.0f : 28.0f + kPathLineHeight * static_cast<float>(lines.size());
}

} // namespace

DownloadsScreen::DownloadsScreen(Context &context) : Screen(context)
{
    xb::keyboard_look(password_keyboard_.style, false);
    password_keyboard_.style.bindings = hui::ui::KeyboardBindings::standard();
    password_keyboard_.style.max_length = 128;
    password_keyboard_.style.done_label = tr("Retry extraction");
    password_keyboard_.set_bounds({320.0f, 590.0f, 1280.0f, 400.0f});
}

int DownloadsScreen::count(JobView view) const
{
    int n = 0;
    for (const Job &job : ctx_.store.state().jobs)
    {
        if (job_view(job) == view)
            ++n;
    }
    return n;
}

std::vector<const Job *> DownloadsScreen::visible() const
{
    std::vector<const Job *> jobs;
    for (const Job &job : ctx_.store.state().jobs)
    {
        if (job_view(job) == view_)
            jobs.push_back(&job);
    }
    return jobs;
}

std::vector<DownloadsScreen::RowAction> DownloadsScreen::actions_for(const Job &job) const
{
    if(job.delivery=="zero") {
        if(job.status=="complete")return {{tr("Downloaded files"),"packages"}, {tr("Clear computer cache"),"clear-cache"}};
        if(job.status=="cancelled")return {{tr("View in Orbit Zero"),"view-zero"}};
    }
    if (job.status == "complete" && job.install_state == "failed")
        return {{tr("Retry installation"), "install"}, {tr("Remove from history"), "remove"}};
    if (job.status == "complete")
        return {{job.packages.empty() ? tr("Downloaded files") : tr("Install on PS5"), "packages"}, {tr("Remove from history"), "remove"}};
    if (job.status == "cancelled")
    {
        return {{job.received > 0.0 ? tr("Resume") : tr("Start again"), "resume"},
                {tr("Delete download"), "dialog"}};
    }
    const char *primary = job_active(job)         ? tr("Pause")
                          : job.status == "error" ? tr("Retry download")
                                                  : tr("Resume");
    const char *action = job_active(job) ? "pause" : job.status == "error" ? "retry" : "resume";
    if (job.can_enter_archive_password)
        return {{tr("Enter archive password"), "archive-password"}, {primary, action}, {tr("Cancel"), "dialog"}};
    if (job.can_refresh_link)
        return {{primary, action}, {tr("Refresh download link"), "refresh-link"}, {tr("Cancel"), "dialog"}};
    return {{primary, action}, {tr("Cancel"), "dialog"}};
}

void DownloadsScreen::focus()
{
    on_views_ = visible().empty();
}

void DownloadsScreen::show_job(const std::string &job_id)
{
    pending_job_ = job_id;
    for (const Job &job : ctx_.store.state().jobs)
    {
        if (job.id != job_id)
            continue;
        view_ = job_view(job);
        const std::vector<const Job *> jobs = visible();
        for (int i = 0; i < static_cast<int>(jobs.size()); ++i)
        {
            if (jobs[static_cast<std::size_t>(i)]->id == job_id)
            {
                row_ = i;
                button_ = 0;
                on_views_ = false;
                pending_job_.clear();
            }
        }
    }
    // The other interfaces move their own focus to it on their next update.
    if (!look::is(look::Interface::store))
        pending_job_ = job_id;
}

Intent DownloadsScreen::update(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    if (install_open_) { ctx_.store.set_fast(true); return focused ? update_installation(input, feedback) : Intent{}; }
    if (clear_open_) { ctx_.store.set_fast(true); return focused ? update_clear(input, feedback) : Intent{}; }
    // The password's keys: Crossbar's panel on the right, or the Store's under the question.
    const bool crossbar = look::is(look::Interface::crossbar);
    xb::keyboard_look(password_keyboard_.style, crossbar);
    password_keyboard_.style.max_length = 128;
    password_keyboard_.style.done_label = tr("Retry extraction");
    password_keyboard_.set_bounds(crossbar ? xb::keyboard_keys() : ui::Rect{320.0f, 590.0f, 1280.0f, 400.0f});
    if (crossbar)
        return update_crossbar(input, dt, feedback, focused);
    if (look::is(look::Interface::panorama))
    {
        pano::keyboard_look(password_keyboard_.style);
        password_keyboard_.style.max_length = 128;
        password_keyboard_.style.done_label = tr("Retry extraction");
        password_keyboard_.set_bounds(pano::keyboard_keys());
        return update_panorama(input, dt, feedback, focused);
    }
    return update_store(input, dt, feedback, focused);
}

Intent DownloadsScreen::update_store(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    Intent intent;
    // A job created a moment ago appears with the next queue refresh.
    if (!pending_job_.empty())
        show_job(pending_job_);

    std::vector<const Job *> jobs = visible();
    row_ = std::clamp(row_, 0, std::max(0, static_cast<int>(jobs.size()) - 1));
    if (jobs.empty())
        on_views_ = true;
    // The empty state's button exists only in Active, and only while nothing is listed.
    if (!jobs.empty() || view_ != JobView::active)
        on_button_ = false;
    const bool busy = ctx_.store.action().busy;
    if (dialog_pending_ && !busy)
    {
        dialog_pending_ = false;
        dialog_error_ = ctx_.store.action().error;
        if (dialog_error_.empty()) dialog_open_ = false;
    }

    if (focused && password_open_)
    {
        const auto event = password_keyboard_.handle(input, feedback);
        for (int i = 0; i < password_keyboard_.erased() && !password_.empty(); ++i) {
            auto pos = password_.size() - 1;
            while (pos && (static_cast<unsigned char>(password_[pos]) & 0xc0) == 0x80) --pos;
            password_.resize(pos);
        }
        if (password_.size() + password_keyboard_.typed().size() <= 128)
            password_ += password_keyboard_.typed();
        password_keyboard_.set_length(static_cast<int>(password_.size()));
        if (event == hui::ui::Event::cancelled || (event == hui::ui::Event::activated && !password_.empty())) {
            if (event == hui::ui::Event::activated)
                ctx_.store.job_action(password_job_, "archive-password", false, password_);
            password_.clear(); password_job_.clear(); password_open_ = false;
        }
    }
    else if (focused && dialog_open_)
    {
        if (input.is_pressed(Action::back) && !busy)
        {
            if (dialog_forget_)
            {
                dialog_forget_ = false;
                dialog_button_ = 2;
            }
            else dialog_open_ = false;
            feedback.play(hui::audio::Cue::modal_close);
        }
        else if (input.nav == Direction::left || input.nav == Direction::right)
        {
            const int next = dialog_button_ + (input.nav == Direction::right ? 1 : -1);
            if (next >= 0 && next < (dialog_forget_ ? 2 : 3))
            {
                dialog_button_ = next;
                feedback.play(hui::audio::Cue::focus);
            }
            else
            {
                refuse(feedback, input);
            }
        }
        else if (input.is_pressed(Action::confirm) && !busy)
        {
            if (dialog_button_ < 2)
            {
                if (dialog_cancelled_ && dialog_button_ == 0)
                {
                    dialog_forget_ = !dialog_forget_;
                    dialog_button_ = 0;
                    dialog_error_.clear();
                }
                else
                {
                    ctx_.store.job_action(dialog_job_, dialog_cancelled_
                        ? (dialog_forget_ ? "forget" : "delete") : "cancel", !dialog_cancelled_ && dialog_button_ == 1);
                    dialog_pending_ = true;
                    dialog_error_.clear();
                }
                feedback.play(dialog_button_ == 1 ? hui::audio::Cue::erase
                                                  : hui::audio::Cue::select);
            }
            else
            {
                dialog_open_ = false;
                feedback.play(hui::audio::Cue::modal_close);
            }
        }
    }
    else if (focused && input.is_pressed(Action::west) && clearable(view_))
        open_clear(view_, feedback);
    else if (focused)
    {
        if (on_views_ && on_button_)
        {
            // The empty state's Browse games button: up returns to the views, Cross opens Games.
            if (input.nav == Direction::up)
            {
                on_button_ = false;
                feedback.play(hui::audio::Cue::focus);
            }
            else if (input.nav != Direction::none)
            {
                refuse(feedback, input);
            }
            if (input.is_pressed(Action::confirm))
            {
                feedback.play(hui::audio::Cue::select);
                intent.kind = Intent::Kind::show_tab;
                intent.tab = tab::browse;
                return intent;
            }
        }
        else if (on_views_)
        {
            if (input.nav == Direction::up)
            {
                intent.kind = Intent::Kind::top_bar;
                return intent;
            }
            if (input.nav == Direction::left || input.nav == Direction::right)
            {
                const int next = static_cast<int>(view_) + (input.nav == Direction::right ? 1 : -1);
                if (next >= 0 && next < static_cast<int>(JobView::count))
                {
                    view_ = static_cast<JobView>(next);
                    row_ = 0;
                    button_ = 0;
                    feedback.play(hui::audio::Cue::tab);
                }
                else
                {
                    refuse(feedback, input);
                }
            }
            else if (input.nav == Direction::down)
            {
                if (!visible().empty())
                {
                    on_views_ = false;
                    row_ = 0;
                    button_ = 0;
                    feedback.play(hui::audio::Cue::focus);
                }
                else if (view_ == JobView::active)
                {
                    on_button_ = true; // down from the views, onto Browse games
                    feedback.play(hui::audio::Cue::focus);
                }
                else
                {
                    refuse(feedback, input);
                }
            }
        }
        else if (!jobs.empty())
        {
            const Job &job = *jobs[static_cast<std::size_t>(row_)];
            const std::vector<RowAction> actions = actions_for(job);
            button_ = std::clamp(button_, 0, static_cast<int>(actions.size()) - 1);
            if (input.nav == Direction::up)
            {
                if (row_ == 0)
                    on_views_ = true;
                else
                    --row_;
                button_ = 0;
                feedback.play(hui::audio::Cue::focus);
            }
            else if (input.nav == Direction::down)
            {
                if (row_ + 1 < static_cast<int>(jobs.size()))
                {
                    ++row_;
                    button_ = 0;
                    feedback.play(hui::audio::Cue::focus);
                }
                else
                {
                    refuse(feedback, input);
                }
            }
            else if (input.nav == Direction::left || input.nav == Direction::right)
            {
                const int next = button_ + (input.nav == Direction::right ? 1 : -1);
                if (next >= 0 && next < static_cast<int>(actions.size()))
                {
                    button_ = next;
                    feedback.play(hui::audio::Cue::focus);
                }
                else
                {
                    refuse(feedback, input);
                }
            }
            if (input.is_pressed(Action::confirm))
            {
                const RowAction &action = actions[static_cast<std::size_t>(button_)];
                if (busy)
                {
                    refuse(feedback, input);
                }
                else if (std::string_view(action.action) == "packages")
                {
                    install_open_ = true; install_job_ = job.id; install_package_ = 0; install_confirm_.clear(); install_reading_ = 0;
                    feedback.play(hui::audio::Cue::modal_open);
                }
                else if (std::string_view(action.action) == "archive-password")
                {
                    password_.clear(); password_job_ = job.id; password_open_ = true;
                    password_keyboard_.enter(); password_keyboard_.set_length(0);
                    feedback.play(hui::audio::Cue::modal_open);
                }
                else if (std::string_view(action.action) == "view-zero")
                {
                    intent.toast="This cancelled transfer is saved in Orbit Zero on your computer.";
                }
                else if (std::string_view(action.action) == "dialog")
                {
                    dialog_open_ = true;
                    dialog_cancelled_ = job.status == "cancelled";
                    dialog_forget_ = false;
                    dialog_pending_ = false;
                    dialog_error_.clear();
                    dialog_button_ = dialog_cancelled_ ? 2 : 0;
                    dialog_job_ = job.id;
                    dialog_in_.snap(0.0f);
                    feedback.play(hui::audio::Cue::modal_open);
                }
                else
                {
                    ctx_.store.job_action(job.id, action.action, false);
                    feedback.play(hui::audio::Cue::select);
                }
            }
        }
    }

    // Live progress while this screen is visible.
    ctx_.store.set_fast(true);
    // Completed rows grow to fit the full location. Keep the focused row's
    // actions in view rather than assuming that every row has the same height.
    const auto shown = visible();
    float focused_bottom = 0.0f;
    for (int i = 0; i <= row_ && i < static_cast<int>(shown.size()); ++i)
        focused_bottom += kRowHeight + location_height(location_lines(*shown[i], ctx_.type)) + kRowGap;
    scroll_.target = on_views_ ? 0.0f : std::max(0.0f, focused_bottom - (1080.0f - kListTop - 76.0f));
    scroll_.update(dt, 11.0f);
    dialog_in_.target = dialog_open_ ? 1.0f : 0.0f;
    dialog_in_.update(dt, 16.0f);
    password_keyboard_.set_azerty(ctx_.azerty);
    password_keyboard_.style.space_label = tr("Space");
    password_keyboard_.style.done_label = tr("Retry extraction");
    password_keyboard_.set_active(password_open_);
    password_keyboard_.update(dt);
    return intent;
}

void DownloadsScreen::draw(Frame &frame, bool focused) const
{
    if (install_open_) { draw_installation(frame); return; }
    if (clear_open_)
        draw_clear(frame); // on the overlay, above the look drawn below
    if (look::is(look::Interface::crossbar))
    {
        draw_crossbar(frame);
        return;
    }
    if (look::is(look::Interface::panorama))
    {
        draw_panorama(frame);
        return;
    }
    hui::gfx::DrawList &list = frame.scene;
    const Snapshot &state = ctx_.store.state();
    const ui::Type &type = ctx_.type;

    const float title = ui::text(list, type.semibold, tr("Downloads"), ui::kGutter, kTitleBaseline, 60.0f,
                                 ui::color::text);
    int active = 0;
    int attention = 0;
    for (const Job &job : state.jobs)
    {
        if (job_view(job) == JobView::active)
            ++active;
        if (job.status == "error")
            ++attention;
    }
    const std::string summary =
        trn(active, "{count} active", "{count} active") + " \xC2\xB7 " +
        trn(attention, "{count} needs attention", "{count} need attention");
    ui::text(list, type.regular, summary, ui::kGutter + title + 24.0f, kTitleBaseline, 20.0f,
             ui::color::text3);

    // ---- view switcher: .download-tabs ----
    float x = ui::kGutter;
    for (int v = 0; v < static_cast<int>(JobView::count); ++v)
    {
        const JobView view = static_cast<JobView>(v);
        const char *label = job_view_label(view);
        char number[16];
        std::snprintf(number, sizeof(number), "%d", count(view));
        const float label_width = type.medium.measure(label, 18.0f);
        const float width = label_width + 7.0f + type.medium.measure(number, 18.0f) + 2.0f * 18.0f;
        const ui::Rect pill{x, kChipsY, width, kChipHeight};
        const bool current = view == view_;
        const float chip_focus = focused && on_views_ && !on_button_ && current && !dialog_open_ ? 1.0f : 0.0f;
        // Chosen view = active; white only while the cursor is on the chips.
        ui::state_fill(list, pill, pill.h * 0.5f, chip_focus, current);
        const ui::Color ink = ui::state_ink(chip_focus, current);
        ui::text(list, type.medium, label, pill.x + 18.0f, pill.cy() + 6.5f, 18.0f, ink);
        ui::text(list, type.medium, number, pill.x + 18.0f + label_width + 7.0f, pill.cy() + 6.5f,
                 18.0f, ink.with_alpha(0.65f));
        x += width + 8.0f;
    }

    // ---- rows ----
    const std::vector<const Job *> jobs = visible();
    if (jobs.empty())
    {
        // .empty: centred icon, heading, note and the way out.
        static const char *const kEmptyTitle[] = {/* i18n */ "Nothing in progress", /* i18n */ "No finished downloads",
                                                  /* i18n */ "No failed downloads", /* i18n */ "No cancelled downloads"};
        static const char *const kEmptyBody[] = {
            /* i18n */ "Choose a game to add it to your queue.",
            /* i18n */ "Successfully completed downloads appear here.",
            /* i18n */ "Downloads that need your attention appear here.",
            /* i18n */ "Cancelled downloads appear here. Kept partial files can be resumed."};
        const int v = static_cast<int>(view_);
        ui::icon_download(list, 960.0f, 375.0f, 52.0f, ui::color::text2);
        ui::text(list, type.light, tr(kEmptyTitle[v]), 960.0f, 462.0f, 32.0f, ui::color::text,
                 ui::Align::center);
        ui::text(list, type.regular, tr(kEmptyBody[v]), 960.0f, 508.0f, 20.0f, ui::color::text2,
                 ui::Align::center);
        if (view_ == JobView::active)
        {
            const float width = ui::button_width(type, tr("Browse games"));
            ui::button(list, type, {960.0f - width * 0.5f, 550.0f, width, ui::kButtonHeight},
                       tr("Browse games"), ui::ButtonKind::primary,
                       focused && on_button_ && !dialog_open_ ? 1.0f : 0.0f);
        }
        return;
    }

    // Rows scrolling up disappear under the view switcher, not over it.
    list.push_clip({0.0f, kListTop - 6.0f, 1920.0f, 1080.0f - kListTop - 70.0f});
    const double now = unix_seconds();
    float row_offset = 0.0f;
    for (int i = 0; i < static_cast<int>(jobs.size()); ++i)
    {
        const Job &job = *jobs[static_cast<std::size_t>(i)];
        const auto location = location_lines(job, type);
        const float extra_height = location_height(location);
        const ui::Rect row{
            kRowLeft, kListTop + row_offset - scroll_.value,
            kRowWidth, kRowHeight + extra_height};
        row_offset += row.h + kRowGap;
        if (row.y > 1080.0f || row.y + row.h < 0.0f)
            continue;
        const bool is_focused = focused && !on_views_ && i == row_ && !dialog_open_;
        if (is_focused)
            list.rounded_rect(row, 18.0f, ui::Color::rgb(0xffffff, 0.06f));

        const Game *game = game_for(state, job);
        const Release *release = release_for(game, job);
        const ui::Rect cover{row.x + kRowPad, row.y + kRowPad, kCover, kCover};
        if (game != nullptr)
            draw_cover(ctx_, list, *game, cover, 12.0f);
        else if (is_game_update_job(job.id))
        {
            Game local; // the console's own icon of the game, or of the DLC
            local.id = job.game_id;
            local.title = job.title;
            draw_cover(ctx_, list, local, cover, 12.0f);
        }
        else
            ui::cover_placeholder(list, type, cover, job.title.empty() ? job.filename : job.title,
                                  12.0f);

        // .queue-info: the name and status, where it comes from and goes,
        // the progress, the transfer, then the actions.
        const float tx = cover.x + cover.w + 28.0f;
        const float right_edge = row.x + row.w - kRowPad;
        const float info_width = right_edge - tx;
        const std::string name = game != nullptr     ? game->title
                                 : job.title.empty() ? job.filename
                                                     : job.title;
        const char *status = job.status == "complete" ? tr("Complete") : status_label(job.status);
        if (job.delivery == "torbox" && job.status == "queued" && !job.delivery_phase.empty())
            status = tr(job.delivery_phase.c_str());
        const ui::Color status_ink = job.status == "error"      ? ui::color::bad
                                     : job.status == "complete" ? ui::color::ok
                                     : job.status == "downloading" || job.status == "verifying" ||
                                               job.status == "extracting"
                                         ? ui::Color::rgb(0xd0d0d0)
                                         : ui::color::text2;
        const float status_width = type.regular.measure(status, 16.0f);
        ui::text(list, type.regular, status, right_edge, row.y + kRowPad + 21.0f, 16.0f, status_ink,
                 ui::Align::right);
        ui::text_fit(list, type.medium, name, tx, row.y + kRowPad + 22.0f, 23.0f,
                     info_width - status_width - 24.0f, ui::color::text);

        const Drive *drive = nullptr;
        for (const Drive &d : state.drives)
        {
            if (d.id == job.storage_id)
                drive = &d;
        }
        std::string meta;
        if (release != nullptr)
            meta = release->provider + " \xC2\xB7 " + format_label(*release) + " \xC2\xB7 ";
        const bool from_updates = is_game_update_job(job.id); // an update or DLC: no drive of its own to name
        if (from_updates)
            meta = job.title_id + " \xC2\xB7 " + (job.format == "DLC" ? std::string("DLC") : std::string(tr("Update")));
        else
            meta += drive != nullptr ? i18n::place(drive->label) : job.storage_id;
        if (job.delivery == "torbox")
            meta += std::string(" \xC2\xB7 ") + tr("via TorBox");
        if(job.delivery=="zero") meta += " / Orbit Zero";
        if (drive == nullptr && !from_updates && job.status != "complete")
            meta += std::string(" \xC2\xB7 ") + tr("Reconnect this drive");
        ui::text_fit(list, type.regular, meta, tx, row.y + kRowPad + 51.0f, 16.0f, info_width,
                     ui::color::text3);

        // While extracting, the bar shows the extraction; Orbit Zero sends the
        // files inside an archive, which can total more than the download.
        const bool extracting = job.status == "extracting";
        // A game update the console is installing, or whose install failed (see game_update_jobs).
        const bool installing = job.status == "complete" && job.install_state == "installing";
        const bool install_failed = job.status == "complete" && job.install_state == "failed";
        const auto amounts = job_progress(job);
        const double done = amounts.done, total = amounts.total;
        const float progress =
            installing ? static_cast<float>(std::clamp(job.install_progress / 100.0, 0.0, 1.0))
            : total > 0.0 ? static_cast<float>(std::clamp(done / total, 0.0, 1.0)) : 0.0f;
        const ui::Rect bar{tx, row.y + kRowPad + 74.0f, info_width, 6.0f};
        ui::progress_bar(list, bar, progress,
                         job.status == "error"      ? ui::color::bad
                         : installing               ? ui::color::action
                         : job.status == "complete" ? ui::color::ok
                                                    : ui::color::action);
        const std::string left = installing ? std::string(tr("Installing")) + " \xC2\xB7 " + i18n::percent(progress * 100.0)
                                 : extracting ? std::string(tr("Extracting")) + " \xC2\xB7 " + i18n::percent(progress * 100.0)
                                 : (job.delivery=="zero" ? std::string("To PS5 ") : std::string()) + format_bytes(job.received) + " / " + format_bytes(total) +
                                 " \xC2\xB7 " + i18n::percent(progress * 100.0);
        const float transfer_y = row.y + kRowPad + 104.0f;
        ui::text(list, type.regular, left, tx, transfer_y, 16.0f, ui::color::text3);
        std::string right;
        if (install_failed)
        {
            std::string message{tr(job.install_error)};
            if (job.install_code != 0)
                message += " (" + std::to_string(job.install_code) + ")";
            ui::text_fit(list, type.regular, message, right_edge, transfer_y, 16.0f, 620.0f,
                         ui::color::bad, ui::Align::right);
        }
        else if (!job.error.empty())
        {
            ui::text_fit(list, type.regular, tr(job.error), right_edge, transfer_y, 16.0f, 620.0f,
                         ui::color::bad, ui::Align::right);
        }
        else
        {
            if (job.delivery == "zero")
            {
                right="Internet "+format_bytes(job.download_speed)+"/s / To PS5 "+format_bytes(job.speed)+"/s";
            }
            else if (job.status == "retrying")
            {
                const int seconds = static_cast<int>(std::max(0.0, std::ceil(job.retry_at - now)));
                right = seconds > 0 ? tr("Retrying in {seconds} s",
                                         {{"seconds", std::to_string(seconds)}})
                                    : std::string(tr("Waiting to retry\xE2\x80\xA6"));
            }
            else if (job.speed > 0.0)
            {
                const double minutes = amounts.minutes;
                right = tr("{speed}/s \xC2\xB7 {minutes} min remaining",
                           {{"speed", format_bytes(job.speed)},
                            {"minutes", i18n::count(static_cast<long>(minutes))}});
            }
            else if (job.status == "complete")
            {
                right = job.verification == "sha256" ? tr("SHA-256 verified") : tr("Size verified");
                if (job.install_state == "base")
                    right += std::string(" \xC2\xB7 ") + tr("Waiting for the base game");
                else if (job.install_state == "waiting")
                    right += std::string(" \xC2\xB7 ") + tr("Waiting to install");
                else if (job.install_state == "installed")
                    right += std::string(" \xC2\xB7 ") + tr("Installed");
            }
            else
            {
                right = tr("{size} remaining",
                           {{"size", format_bytes(amounts.remaining)}});
            }
            ui::text(list, type.regular, right, right_edge, transfer_y, 16.0f, ui::color::text3,
                     ui::Align::right);
        }

        if (!location.empty())
        {
            ui::text(list, type.regular, tr("Location"), tx, row.y + kRowPad + 135.0f,
                     16.0f, ui::color::text3);
            float path_y = row.y + kRowPad + 159.0f;
            for (const auto &line : location)
            {
                ui::text(list, type.regular, line, tx, path_y, 18.0f, ui::color::text2);
                path_y += kPathLineHeight;
            }
        }

        // .queue-actions: small buttons, always shown; the focused one rings.
        const std::vector<RowAction> actions = actions_for(job);
        float ax = tx;
        for (int a = 0; a < static_cast<int>(actions.size()); ++a)
        {
            const char *label = actions[static_cast<std::size_t>(a)].label;
            const float width = type.medium.measure(label, 16.0f) + 2.0f * 22.0f;
            const bool on = is_focused && a == button_;
            ui::button(list, type, {ax, row.y + kRowPad + 128.0f + extra_height, width, 38.0f}, label,
                       on ? ui::ButtonKind::primary : ui::ButtonKind::secondary,
                       on ? 1.0f : 0.0f, !ctx_.store.action().busy, 16.0f);
            ax += width + 12.0f;
        }
    }
    list.pop_clip();

    if (!ctx_.store.action().error.empty())
        ui::text_fit(list, type.regular, tr(ctx_.store.action().error), ui::kGutter + 760.0f,
                     kChipsY + 28.0f, 17.0f, 1060.0f - ui::kGutter, ui::color::bad);

    if (password_open_) {
        auto &overlay = frame.overlay;
        overlay.rounded_rect({0, 0, 1920, 1080}, 0, ui::color::night0.with_alpha(0.9f));
        ui::text_fit(overlay, type.light, tr("Enter archive password"), 320, 395, 46, 1280, ui::color::text);
        ui::paragraph(overlay, type.regular, tr("Retry extraction using the downloaded archive. Nothing will be downloaded again."),
                      320, 450, 24, 1280, 36, ui::color::text2, 2);
        ui::text_fit(overlay, type.regular, std::string(password_.size(), '*'), 320, 540, 28, 1280, ui::color::text);
        hui::ui::Canvas canvas{overlay, ctx_.fonts, frame.glass_texture, ctx_.clock};
        password_keyboard_.draw(canvas);
    }
    // ---- the cancel dialog ----
    if (dialog_in_.value > 0.01f)
    {
        hui::gfx::DrawList &overlay = frame.overlay;
        const float in = dialog_in_.value;
        overlay.push_opacity(in);
        overlay.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f,
                             ui::color::night0.with_alpha(0.55f));
        const ui::Rect panel{360.0f, 290.0f - 16.0f * (1.0f - in), 1200.0f, 500.0f};
        frame.glass = true;
        ui::glass_modal(overlay, panel, frame.glass_texture);
        ui::text_fit(overlay, type.light,
                 dialog_cancelled_ ? (dialog_forget_ ? tr("Remove from history?") : tr("Delete this cancelled download?")) : tr("Cancel this download?"), panel.x + 56.0f,
                 panel.y + 100.0f, 46.0f, panel.w - 112.0f, ui::color::text);
        ui::paragraph(overlay, type.regular,
                      dialog_cancelled_ ? (dialog_forget_
                      ? tr("Only the history entry will be removed. Any partial file stays on its drive and must be deleted before downloading there again.")
                      : tr("Delete the partial file and remove this entry. If the file is already gone, the entry will still be removed. Completed files are kept."))
                      : tr("Keep the partial file to resume later, or delete only the partial file. "
                      "Downloaded files are never deleted through history cleanup."),
                      panel.x + 56.0f, panel.y + 160.0f, 24.0f, panel.w - 112.0f, 36.0f,
                      ui::color::text2, 3);
        if (!dialog_error_.empty())
            ui::paragraph(overlay, type.regular, tr(dialog_error_), panel.x + 56.0f,
                          panel.y + 284.0f, 20.0f, panel.w - 112.0f, 28.0f, ui::color::bad, 3);
        const char *const *buttons = dialog_forget_ ? kForgetButtons : dialog_cancelled_ ? kDeleteButtons : kDialogButtons;
        float bx = panel.x + 56.0f;
        for (int b = 0; b < (dialog_forget_ ? 2 : 3); ++b)
        {
            const ui::ButtonKind kind = b == dialog_button_ ? ui::ButtonKind::primary : (b == 1 ? ui::ButtonKind::danger : ui::ButtonKind::secondary);
            const float width = ui::button_width(ctx_.type, tr(buttons[b]), 18.0f, kind);
            ui::button(overlay, type, {bx, panel.y + panel.h - 104.0f, width, 48.0f},
                       tr(buttons[b]), kind, b == dialog_button_ ? 1.0f : 0.0f, !ctx_.store.action().busy, 18.0f);
            bx += width + 16.0f;
        }
        overlay.pop_opacity();
    }
}

std::vector<hui::ui::Hint> DownloadsScreen::hints() const
{
    if (install_open_) return {{hui::ui::Button::dpad, tr("Choose package")}, {hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Close")}};
    if (clear_open_) return {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Go back")}};
    if (look::is(look::Interface::crossbar))
        return crossbar_hints();
    if (look::is(look::Interface::panorama))
        return panorama_hints();
    if (dialog_open_ || password_open_)
        return {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Go back")}};
    std::vector<hui::ui::Hint> hints;
    if (on_views_)
        hints = {{hui::ui::Button::dpad, tr("Switch view")}};
    else
        hints = {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::dpad, tr("Actions")}};
    // Square clears the view's history, as the browser's button does.
    if (clearable(view_))
        hints.push_back({hui::ui::Button::square,
                         view_ == JobView::cancelled ? tr("Clear cancelled history") : tr("Clear finished history")});
    hints.push_back({hui::ui::Button::l1, tr("Tabs"), hui::ui::Button::r1});
    return hints;
}


// ---------------------------------------------------------------- Crossbar

namespace
{

const char *view_title(JobView view)
{
    switch (view)
    {
    case JobView::failed:
        return /* i18n */ "Failed";
    case JobView::finished:
        return /* i18n */ "Finished";
    case JobView::cancelled:
        return /* i18n */ "Cancelled";
    default:
        return /* i18n */ "Active";
    }
}

icons::Icon view_icon(JobView view)
{
    return view == JobView::failed ? icons::Icon::warn : view == JobView::finished ? icons::Icon::check : icons::Icon::close;
}

// Waiting downloads can move in the queue (the browser's Move up / Move down).
bool waiting(const Job &job)
{
    return job.status == "queued" || job.status == "paused" || job.status == "retrying";
}

// A download as a row: its state and amounts, more when focused.
void job_row(const Snapshot &state, const Job &job, xb::Row &row)
{
    const Game *game = game_for(state, job);
    const Release *release = release_for(game, job);
    row.key = "j:" + job.id;
    row.game = game;
    if (game == nullptr)
    {
        row.has_icon = true;
        row.icon = icons::Icon::down;
    }
    row.title = game != nullptr ? game->title : !job.title.empty() ? job.title : job.filename;
    const std::string status = job.status == "complete" ? tr("Complete") : tr(status_label(job.status));
    // The design's tags: the running download in white, problems in amber,
    // where it downloads in lilac, other states plain; waiting and history rows have none.
    if (job.can_enter_archive_password)
        row.tag = tr("Archive");
    else if (job.status == "error")
    {
        row.tag = status;
        row.tag_kind = xb::Tag::warn;
    }
    else if (job.delivery == "zero" && !job_inactive(job))
    {
        row.tag = tr("Computer");
        row.tag_kind = xb::Tag::route;
    }
    else if (job.delivery == "torbox" && job.status == "queued" && !job.delivery_phase.empty())
    {
        row.tag = "TorBox";
        row.tag_kind = xb::Tag::route;
    }
    else if (job.status == "downloading")
    {
        row.tag = status;
        row.tag_kind = xb::Tag::light;
    }
    else if (job.status == "verifying" || job.status == "retrying" || job.status == "inspecting" || job.status == "extracting")
        row.tag = status;
    const bool extracting = job.status == "extracting";
    const auto amounts = job_progress(job);
    const double done = amounts.done, total = amounts.total;
    const Drive *drive = nullptr;
    for (const Drive &d : state.drives)
        if (d.id == job.storage_id)
            drive = &d;
    // At rest: the state and its amounts. Focused: where it comes from and goes.
    std::string brief = status;
    if (job.status == "complete")
        brief += " \xC2\xB7 " + std::string(job.verification == "sha256" ? tr("SHA-256 verified") : tr("Size verified")) +
                 " \xC2\xB7 " + format_bytes(total);
    else if (job.delivery == "torbox")
        brief += " \xC2\xB7 " + std::string(tr("via TorBox")) + " \xC2\xB7 " + format_bytes(job.total);
    else if (done > 0.0 && total > 0.0)
        brief += " \xC2\xB7 " + tr("{done} of {total}", {{"done", format_bytes(done)}, {"total", format_bytes(total)}});
    else if (total > 0.0)
        brief += " \xC2\xB7 " + format_bytes(total);
    row.sub = brief;
    row.warn = job.status == "error" || (drive == nullptr && job.status != "complete");
    std::string where;
    if (release != nullptr)
        where = release->provider + " \xC2\xB7 " + format_label(*release);
    if (job.delivery == "torbox")
        where += (where.empty() ? "" : " \xC2\xB7 ") + std::string(tr("via TorBox"));
    where += (where.empty() ? "" : " \xC2\xB7 ") + (drive != nullptr ? i18n::place(drive->label) : job.storage_id);
    if (drive == nullptr && job.status != "complete")
        where += " \xC2\xB7 " + std::string(tr("Reconnect this drive"));
    // Focused, the row says where it comes from and goes; a problem keeps its own line.
    if (job.status == "complete")
        row.meta = brief;
    else if (job.status != "error")
        row.meta = where;
    if (job.status == "complete")
        row.bar = 1.0f;
    else if (job.status != "cancelled" && total > 0.0)
        row.bar = static_cast<float>(std::clamp(done / total, 0.0, 1.0));
    else if (job.status == "cancelled" && done > 0.0 && total > 0.0)
        row.bar = static_cast<float>(std::clamp(done / total, 0.0, 1.0));
    // The focused line: amounts and timing, as Store's row shows them.
    std::string detail;
    if (job.status == "complete")
        detail = job.path;
    else if (job.status == "error" && !job.error.empty())
        detail = tr(std::string_view(job.error));
    else if (job.status == "retrying")
    {
        const double seconds = std::max(0.0, std::ceil(job.retry_at - static_cast<double>(std::time(nullptr))));
        detail = seconds > 0 ? tr("Retrying in {seconds} s", {{"seconds", i18n::count(static_cast<long>(seconds))}})
                             : std::string(tr("Waiting to retry\xE2\x80\xA6"));
    }
    else if (total > 0.0)
    {
        detail = (extracting ? std::string(tr("Extracting")) + " \xC2\xB7 " : std::string()) +
                 tr("{done} of {total}", {{"done", format_bytes(done)}, {"total", format_bytes(total)}});
        if (job.speed > 0.0 && job.status == "downloading")
            detail += " \xC2\xB7 " + tr("{speed}/s \xC2\xB7 {minutes} min remaining",
                                        {{"speed", format_bytes(job.speed)},
                                         {"minutes", i18n::count(static_cast<long>(amounts.minutes))}});
    }
    if (!job.delivery_phase.empty() && (job.status == "queued" || job.status == "downloading"))
        detail += (detail.empty() ? "" : " \xC2\xB7 ") + std::string(tr(std::string_view(job.delivery_phase)));
    row.detail = detail;
    if (job.status == "cancelled" && job.received > 0.0)
        row.text = tr("Partial file kept. Resume it, or delete the partial file before removing this entry.");
}

} // namespace

xb::List DownloadsScreen::crossbar_list() const
{
    xb::List list;
    const Snapshot &state = ctx_.store.state();
    const JobView view = xb_folder_ < 0 ? JobView::active : static_cast<JobView>(xb_folder_);
    std::vector<const Job *> jobs;
    for (const Job &job : state.jobs)
        if (job_view(job) == view)
            jobs.push_back(&job);
    const bool root = xb_folder_ < 0;
    // An empty view says so; the section's empty row leads to Browse.
    const int lead = jobs.empty() ? 1 : static_cast<int>(jobs.size());
    const bool clear = !root && clearable(view); // the browser's row after the downloads
    list.size = lead + (root ? 3 : 0) + (clear ? 1 : 0);
    list.fill = [this, jobs, root, view, lead, clear](int i, xb::Row &row)
    {
        const Snapshot &now = ctx_.store.state();
        if (clear && i == lead)
        {
            row.key = "clear";
            row.has_icon = true;
            row.icon = icons::Icon::history;
            row.title = view == JobView::cancelled ? tr("Clear cancelled history") : tr("Clear finished history");
            row.sub = view == JobView::cancelled ? tr("Kept partial files stay listed") : tr("Your downloaded files stay on the drive");
            return;
        }
        if (i < lead)
        {
            if (!jobs.empty())
            {
                job_row(now, *jobs[static_cast<std::size_t>(i)], row);
                return;
            }
            row.key = "empty";
            row.has_icon = true;
            row.icon = root ? icons::Icon::down : view_icon(view);
            if (root)
            {
                row.title = tr("Nothing in progress");
                row.sub = tr("Choose a game to add it to your queue.");
            }
            else
            {
                row.title = view == JobView::failed ? tr("No failed downloads")
                            : view == JobView::finished ? tr("No finished downloads")
                                                        : tr("No cancelled downloads");
                row.sub = view == JobView::failed ? tr("Downloads that need your attention appear here.")
                          : view == JobView::finished ? tr("Successfully completed downloads appear here.")
                                                      : tr("Cancelled downloads appear here. Kept partial files can be resumed.");
            }
            return;
        }
        static const JobView kFolders[] = {JobView::failed, JobView::finished, JobView::cancelled};
        const JobView folder = kFolders[i - lead];
        const int n = count(folder);
        row.key = std::string("f:") + view_title(folder);
        row.has_icon = true;
        row.icon = view_icon(folder);
        row.folder = true;
        row.title = tr(view_title(folder));
        row.sub = folder == JobView::failed ? trn(n, "{count} needs attention", "{count} need attention")
                                            : trn(n, "{count} download", "{count} downloads");
        if (n == 0)
            row.sub = folder == JobView::failed ? tr("No failed downloads")
                      : folder == JobView::finished ? tr("No finished downloads")
                                                    : tr("No cancelled downloads");
    };
    return list;
}

std::vector<DownloadsScreen::XbAction> DownloadsScreen::crossbar_actions(const Job &job) const
{
    const Snapshot &state = ctx_.store.state();
    const bool busy = ctx_.store.action().busy;
    std::vector<XbAction> out;
    if (job.status == "complete" && job.delivery != "zero")
        out.push_back({icons::Icon::books, tr("View in Library"), "library", {}, true, false});
    // Store's actions for this download, in its order.
    for (const RowAction &a : actions_for(job))
    {
        const std::string_view action = a.action;
        icons::Icon icon = icons::Icon::next;
        if (action == "pause")
            icon = icons::Icon::pause;
        else if (action == "resume")
            icon = icons::Icon::play;
        else if (action == "retry" || action == "refresh-link")
            icon = action == "retry" ? icons::Icon::refresh : icons::Icon::link;
        else if (action == "archive-password")
            icon = icons::Icon::key;
        else if (action == "remove")
            icon = icons::Icon::history;
        else if (action == "dialog")
            icon = job.status == "cancelled" ? icons::Icon::trash : icons::Icon::close;
        std::string label = a.label;
        if (action == "dialog" && job.status != "cancelled")
            label = tr("Cancel download");
        out.push_back({icon, label, std::string(action),
                       action == "remove" ? std::string(tr("The file stays on the drive.")) : std::string(), !busy,
                       false});
    }
    if (job.status == "cancelled")
        out.push_back({icons::Icon::history, tr("Remove from history"), "forget", {}, !busy, false});
    // Waiting downloads move in the queue, as in the browser.
    if (waiting(job))
    {
        std::vector<const Job *> queued;
        for (const Job &j : state.jobs)
            if (waiting(j))
                queued.push_back(&j);
        int at = 0;
        for (int i = 0; i < static_cast<int>(queued.size()); ++i)
            if (queued[static_cast<std::size_t>(i)]->id == job.id)
                at = i;
        out.push_back({icons::Icon::up, tr("\xE2\x86\x91 Move up"), "move-up", at <= 0 ? std::string(tr("Already first")) : std::string(),
                       !busy && at > 0, false});
        out.push_back({icons::Icon::arrow_down, tr("\xE2\x86\x93 Move down"), "move-down",
                       at + 1 >= static_cast<int>(queued.size()) ? std::string(tr("Already last")) : std::string(),
                       !busy && at + 1 < static_cast<int>(queued.size()), false});
    }
    // Details (the game's page) for downloads still under way, as the design has it.
    if (job_view(job) == JobView::active && game_for(state, job) != nullptr)
        out.push_back({icons::Icon::info, tr("Details"), "game", {}, true, false});
    return out;
}

bool DownloadsScreen::clearable(JobView view) const
{
    return (view == JobView::finished || view == JobView::cancelled) && count(view) > 0;
}

void DownloadsScreen::open_clear(JobView view, Feedback &feedback)
{
    clear_open_ = true;
    clear_pending_ = false;
    clear_view_ = view;
    clear_button_ = 0;
    clear_error_.clear();
    xb_menu_.open = false;
    pn_bar_open_ = false;
    feedback.play(hui::audio::Cue::modal_open);
}

Intent DownloadsScreen::update_clear(const InputFrame &input, Feedback &feedback)
{
    const bool busy = ctx_.store.action().busy;
    if (clear_pending_ && !busy)
    {
        clear_pending_ = false;
        clear_error_ = ctx_.store.action().error;
        if (clear_error_.empty())
        {
            clear_open_ = false;
            return {};
        }
    }
    if (input.is_pressed(Action::back) && !busy)
    {
        clear_open_ = false;
        feedback.play(hui::audio::Cue::modal_close);
    }
    else if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const int next = clear_button_ + (input.nav == Direction::right ? 1 : -1);
        if (next >= 0 && next < 2)
        {
            clear_button_ = next;
            feedback.play(hui::audio::Cue::focus);
        }
        else
            refuse(feedback, input);
    }
    else if (input.is_pressed(Action::confirm) && !busy)
    {
        if (clear_button_ == 0)
        {
            ctx_.store.clear_history(clear_view_ == JobView::cancelled ? "cancelled" : "complete");
            clear_pending_ = true;
            clear_error_.clear();
            feedback.play(hui::audio::Cue::erase);
        }
        else
        {
            clear_open_ = false;
            feedback.play(hui::audio::Cue::modal_close);
        }
    }
    return {};
}

void DownloadsScreen::draw_clear(Frame &frame) const
{
    // src/Downloads.tsx's question, the same in every look.
    hui::gfx::DrawList &list = frame.overlay;
    const ui::Type &type = ctx_.type;
    const bool cancelled = clear_view_ == JobView::cancelled;
    const bool busy = ctx_.store.action().busy;
    list.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, ui::color::night0.with_alpha(0.86f));
    const ui::Rect panel{460.0f, 330.0f, 1000.0f, 420.0f};
    ui::modal(list, panel);
    ui::text_fit(list, type.light, cancelled ? tr("Clear cancelled history?") : tr("Clear finished history?"),
                 panel.x + 60.0f, panel.y + 92.0f, 40.0f, panel.w - 120.0f, ui::color::text);
    ui::paragraph(list, type.regular,
                  cancelled ? tr("Remove cancelled entries with no partial file. Kept partials stay listed for recovery.")
                            : tr("Remove completed entries from history. Your downloaded files stay on the drive."),
                  panel.x + 60.0f, panel.y + 150.0f, 24.0f, panel.w - 120.0f, 34.0f, ui::color::text2, 3);
    if (!clear_error_.empty())
        ui::paragraph(list, type.regular, tr(std::string_view(clear_error_)), panel.x + 60.0f, panel.y + 262.0f, 20.0f,
                      panel.w - 120.0f, 28.0f, ui::color::bad, 2);
    const std::string clear = tr("Clear history"), keep = tr("Keep history");
    const float w0 = ui::button_width(type, clear), w1 = ui::button_width(type, keep);
    const float y = panel.y + panel.h - 60.0f - ui::kButtonHeight;
    ui::button(list, type, {panel.x + 60.0f, y, w0, ui::kButtonHeight}, clear, ui::ButtonKind::primary,
               clear_button_ == 0 ? 1.0f : 0.0f, !busy);
    ui::button(list, type, {panel.x + 60.0f + w0 + 16.0f, y, w1, ui::kButtonHeight}, keep, ui::ButtonKind::secondary,
               clear_button_ == 1 ? 1.0f : 0.0f, !busy);
}

Intent DownloadsScreen::run_action(const Job &job, const std::string &action, Feedback &feedback)
{
    Intent intent;
    const Snapshot &state = ctx_.store.state();
    if (action == "packages") {
        install_open_ = true; install_job_ = job.id; install_package_ = 0; install_confirm_.clear(); install_reading_ = 0;
        xb_menu_.open = false; pn_bar_open_ = false;
        feedback.play(hui::audio::Cue::modal_open); return intent;
    }
    if (action == "library")
    {
        intent.kind = Intent::Kind::show_library;
        intent.id = job.title_id;
    }
    else if (action == "game")
    {
        if (const Game *game = game_for(state, job))
        {
            intent.kind = Intent::Kind::open_game;
            intent.id = game->id;
        }
    }
    else if (action == "archive-password")
    {
        password_.clear();
        password_job_ = job.id;
        password_open_ = true;
        password_keyboard_.enter();
        password_keyboard_.set_length(0);
        feedback.play(hui::audio::Cue::modal_open);
    }
    else if (action == "clear-history")
        open_clear(job_view(job), feedback);
    else if (action == "view-zero")
        intent.toast = tr("This cancelled transfer is saved in Orbit Zero on your computer.");
    else if (action == "dialog" || action == "forget")
    {
        dialog_open_ = true;
        dialog_cancelled_ = job.status == "cancelled";
        // Remove from history goes straight to its own question.
        dialog_forget_ = action == "forget";
        dialog_pending_ = false;
        dialog_error_.clear();
        dialog_button_ = dialog_forget_ ? 0 : dialog_cancelled_ ? 2 : 0;
        dialog_job_ = job.id;
        dialog_in_.snap(0.0f);
        feedback.play(hui::audio::Cue::modal_open);
    }
    else
    {
        ctx_.store.job_action(job.id, action, false);
        feedback.play(hui::audio::Cue::select);
    }
    return intent;
}

Intent DownloadsScreen::update_crossbar(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    Intent intent;
    if (password_open_ || dialog_open_)
    {
        // Store's dialogs and keyboard, with their own answers.
        intent = update_store(input, dt, feedback, focused);
        xb_.animate(dt);
        xb_menu_.animate(dt);
        return intent;
    }
    if (!pending_job_.empty())
    {
        // Show a new download once it appears in its view.
        for (const Job &job : ctx_.store.state().jobs)
            if (job.id == pending_job_)
            {
                const JobView view = job_view(job);
                xb_folder_ = view == JobView::active ? -1 : static_cast<int>(view);
                if (xb_.focus_key(crossbar_list(), "j:" + job.id))
                    pending_job_.clear();
            }
    }
    const xb::List list = crossbar_list();
    xb_.clamp(list);
    xb::Row row;
    if (list.size > 0)
        list.fill(xb_.index, row);
    const Job *job = nullptr;
    for (const Job &j : ctx_.store.state().jobs)
        if (row.key == "j:" + j.id)
            job = &j;
    if (focused && xb_menu_.open && job != nullptr)
    {
        const std::vector<XbAction> actions = crossbar_actions(*job);
        std::vector<xb::MenuItem> items;
        for (const XbAction &a : actions)
        {
            xb::MenuItem item = xb::action(a.icon, a.label, a.enabled);
            item.sub = a.sub;
            item.danger = a.danger;
            items.push_back(item);
        }
        const int chosen = xb_menu_.update(input, items, feedback);
        if (chosen >= 0)
            intent = run_action(*job, actions[static_cast<std::size_t>(chosen)].action, feedback);
    }
    else if (focused)
    {
        xb_menu_.open = false;
        if (input.nav != Direction::none)
            xb_.move(list, input, feedback);
        else if (input.is_pressed(Action::confirm))
        {
            if (job != nullptr)
            {
                xb_menu_.show(0);
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (row.folder)
            {
                static const JobView kFolders[] = {JobView::failed, JobView::finished, JobView::cancelled};
                for (JobView v : kFolders)
                    if (row.key == std::string("f:") + view_title(v))
                        xb_folder_ = static_cast<int>(v);
                xb_.index = 0;
                xb_.snap();
                feedback.play(hui::audio::Cue::open);
            }
            else if (row.key == "clear" && xb_folder_ >= 0)
                open_clear(static_cast<JobView>(xb_folder_), feedback);
            else if (row.key == "empty" && xb_folder_ < 0)
            {
                feedback.play(hui::audio::Cue::select);
                intent.kind = Intent::Kind::show_tab;
                intent.tab = tab::browse;
            }
        }
        else if (input.is_pressed(Action::back) && xb_folder_ >= 0)
        {
            const std::string folder = std::string("f:") + view_title(static_cast<JobView>(xb_folder_));
            xb_folder_ = -1;
            xb_.focus_key(crossbar_list(), folder);
            feedback.play(hui::audio::Cue::back);
        }
        else if (job != nullptr && input.is_pressed(Action::north))
        {
            // Triangle pauses or resumes, as the design says.
            const std::vector<RowAction> actions = actions_for(*job);
            const std::string_view first = actions.empty() ? std::string_view() : actions.front().action;
            if (!ctx_.store.action().busy && (first == "pause" || first == "resume" || first == "retry"))
                intent = run_action(*job, std::string(first), feedback);
            else
                refuse(feedback, input);
        }
    }
    ctx_.store.set_fast(true);
    xb_.animate(dt);
    xb_menu_.animate(dt);
    dialog_in_.target = dialog_open_ ? 1.0f : 0.0f;
    dialog_in_.update(dt, 16.0f);
    return intent;
}

void DownloadsScreen::draw_crossbar(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    std::vector<xb::BarItem> bar = xb::sections(ctx_, false);
    int current = section::downloads;
    if (xb_folder_ >= 0)
    {
        xb::BarItem folder;
        folder.icon = view_icon(static_cast<JobView>(xb_folder_));
        folder.label = tr(view_title(static_cast<JobView>(xb_folder_)));
        bar = {bar[section::downloads], folder};
        current = 1;
    }
    xb::draw_bar(ctx_, list, bar, current);
    const xb::List rows = crossbar_list();
    xb_.draw(ctx_, list, rows);
    xb::Row row;
    if (rows.size > 0)
        rows.fill(std::clamp(xb_.index, 0, rows.size - 1), row);
    const Snapshot &state = ctx_.store.state();
    for (const Job &job : state.jobs)
    {
        if (row.key != "j:" + job.id)
            continue;
        std::vector<xb::MenuItem> items;
        for (const XbAction &a : crossbar_actions(job))
        {
            xb::MenuItem item = xb::action(a.icon, a.label, a.enabled);
            item.sub = a.sub;
            items.push_back(item);
        }
        xb::draw_menu(ctx_, frame.overlay, row.title, items, xb_menu_);
    }
    if (!ctx_.store.action().error.empty())
        ui::text_fit(list, ctx_.type.regular, tr(ctx_.store.action().error), xb::kPreviewX, 470.0f, 20.0f, xb::kPreviewW,
                     ui::Color::rgb(0xffd27a));
    // Cancel and delete: Store's questions as a band.
    if (dialog_in_.value > 0.01f)
    {
        const char *const *buttons = dialog_forget_ ? kForgetButtons : dialog_cancelled_ ? kDeleteButtons : kDialogButtons;
        std::vector<std::string> labels;
        for (int b = 0; b < (dialog_forget_ ? 2 : 3); ++b)
            labels.push_back(tr(buttons[b]));
        xb::Band band;
        band.title = dialog_cancelled_ ? (dialog_forget_ ? tr("Remove from history?") : tr("Delete this cancelled download?"))
                                       : tr("Cancel this download?");
        band.body = dialog_cancelled_ ? (dialog_forget_
                                             ? tr("Only the history entry will be removed. Any partial file stays on its drive and must be deleted before downloading there again.")
                                             : tr("Delete the partial file and remove this entry. If the file is already gone, the entry will still be removed. Completed files are kept."))
                                      : tr("Keep the partial file to resume later, or delete only the partial file. "
                                           "Downloaded files are never deleted through history cleanup.");
        band.error = tr(std::string_view(dialog_error_));
        band.buttons = labels;
        band.focus = dialog_button_;
        xb::draw_band(ctx_, frame.overlay, band, dialog_in_.value);
    }
    if (password_open_)
    {
        // The design's keyboard panel, labelled with the game.
        std::string label = tr("Enter archive password");
        for (const Job &job : ctx_.store.state().jobs)
            if (job.id == password_job_)
            {
                const Game *game = game_for(ctx_.store.state(), job);
                label += " \xC2\xB7 " + (game != nullptr ? game->title : !job.title.empty() ? job.title : job.filename);
            }
        xb::draw_keyboard(ctx_, frame, password_keyboard_, label, password_, true, 1.0f);
    }
}

std::vector<hui::ui::Hint> DownloadsScreen::crossbar_hints() const
{
    if (password_open_)
        return {{hui::ui::Button::square, tr("Delete")}, {hui::ui::Button::circle, tr("Close")}};
    if (dialog_open_)
        return {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Go back")}};
    if (xb_menu_.open)
        return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, xb_folder_ >= 0 ? tr("Back") : tr("Close")}};
    const xb::List rows = crossbar_list();
    xb::Row row;
    if (rows.size > 0)
        rows.fill(std::clamp(xb_.index, 0, rows.size - 1), row);
    std::vector<hui::ui::Hint> hints;
    if (row.key.rfind("j:", 0) == 0)
    {
        hints.push_back({hui::ui::Button::cross, tr("Actions")});
        // Triangle's shortcut, when the download's first action is one.
        for (const Job &job : ctx_.store.state().jobs)
            if ("j:" + job.id == row.key)
            {
                const std::vector<RowAction> actions = actions_for(job);
                const std::string_view first = actions.empty() ? std::string_view() : actions.front().action;
                if (first == "pause" || first == "resume")
                    hints.push_back({hui::ui::Button::triangle, tr("Pause or resume")});
                else if (first == "retry")
                    hints.push_back({hui::ui::Button::triangle, tr("Retry download")});
            }
    }
    else if (row.key == "empty" && xb_folder_ < 0)
        hints.push_back({hui::ui::Button::cross, tr("Browse games")});
    else if (row.key == "clear")
        hints.push_back({hui::ui::Button::cross, tr("Clear history")});
    else if (row.folder)
        hints.push_back({hui::ui::Button::cross, tr("Open")});
    if (xb_folder_ >= 0)
        hints.push_back({hui::ui::Button::circle, tr("Back")});
    else
        hints.push_back({hui::ui::Button::l1, tr("Categories"), hui::ui::Button::r1});
    return hints;
}

bool DownloadsScreen::page(int step)
{
    // L1 and R1 jump between Panorama's groups, then on to the next section.
    // Cinema shows Store's Downloads, where they switch the app's tabs.
    if (!look::is(look::Interface::panorama) || pn_bar_open_ || dialog_open_ || password_open_)
        return false;
    const pano::Page page = panorama_page();
    int next = pano_.focus.group + step;
    while (next >= 0 && next < static_cast<int>(page.groups.size()) && page.groups[static_cast<std::size_t>(next)].size == 0)
        next += step;
    if (next < 0 || next >= static_cast<int>(page.groups.size()))
        return false;
    pano_.focus = {next, 0};
    return true;
}

// ---------------------------------------------------------------- Panorama

namespace
{
// The actions shown as tiles beside a group's first download: its own, not
// the queue's order or its game (those are in the app bar).
bool tile_action(const std::string &action)
{
    return action != "move-up" && action != "move-down" && action != "game";
}
// The views in the order Panorama's groups show them.
constexpr JobView kViewOrder[] = {JobView::active, JobView::failed, JobView::finished, JobView::cancelled};
// An action's label for the app bar's round shortcuts.
std::string short_label(std::string label)
{
    for (const char *arrow : {"\xE2\x86\x91 ", "\xE2\x86\x93 "})
        if (label.rfind(arrow, 0) == 0)
            label.erase(0, std::char_traits<char>::length(arrow));
    return pano::lower(label);
}
} // namespace

pano::Page DownloadsScreen::panorama_page() const
{
    pano::Page page;
    const Snapshot &state = ctx_.store.state();
    for (JobView view : kViewOrder)
    {
        std::vector<const Job *> jobs;
        for (const Job &job : state.jobs)
            if (job_view(job) == view)
                jobs.push_back(&job);
        // Active stays, with a way to Browse when nothing runs; empty views go.
        if (jobs.empty() && view != JobView::active)
            continue;
        pano::Group g;
        g.title = pano::lower(tr(view_title(view)));
        g.pattern = pano::Pattern::custom;
        std::vector<PnEntry> entries;
        if (jobs.empty())
        {
            entries.push_back({'b', 0});
            g.places = {{0, 0, 2, 1}};
        }
        else
        {
            g.count = i18n::count(static_cast<long>(jobs.size()));
            const Job &lead = *jobs.front();
            // The first download, large; beside it its actions, unless it's
            // simply running (then the next downloads take their place).
            const bool plain = view == JobView::active && lead.status == "downloading" && !lead.can_refresh_link &&
                               !lead.can_enter_archive_password && lead.delivery != "torbox";
            std::vector<int> actions;
            if (!plain)
            {
                const std::vector<XbAction> all = crossbar_actions(lead);
                for (int i = 0; i < static_cast<int>(all.size()) && actions.size() < 3; ++i)
                    if (tile_action(all[static_cast<std::size_t>(i)].action))
                        actions.push_back(i);
            }
            entries.push_back({'j', 0});
            g.places.push_back({0, 0, 2, 2});
            const int n = static_cast<int>(actions.size());
            for (int k = 0; k < n; ++k)
            {
                entries.push_back({'a', actions[static_cast<std::size_t>(k)]});
                g.places.push_back(n == 3 ? (k == 0 ? pano::Place{2, 0, 2, 1} : pano::Place{1 + k, 1, 1, 1}) : pano::Place{2, k, 1, 1});
            }
            // The rest: wide tiles under and after; Active's figures after the first ones.
            std::vector<pano::Place> wide = n == 0 ? std::vector<pano::Place>{{2, 0, 2, 1}, {2, 1, 2, 1}, {0, 2, 2, 1}}
                                                   : std::vector<pano::Place>{{0, 2, 2, 1}, {2, 2, 2, 1}};
            int column = 4;
            const bool figures = view == JobView::active;
            bool placed_figures = false;
            const auto place_figures = [&](int col, int row)
            {
                entries.push_back({'l', 0});
                g.places.push_back({col, row, 1, 1});
                entries.push_back({'f', 0});
                g.places.push_back({col + 1, row, 1, 1});
                placed_figures = true;
            };
            for (int j = 1; j < static_cast<int>(jobs.size()); ++j)
            {
                const int k = j - 1;
                pano::Place at;
                if (k < static_cast<int>(wide.size()))
                    at = wide[static_cast<std::size_t>(k)];
                else
                {
                    if (figures && n == 0 && !placed_figures)
                        place_figures(2, 2);
                    const int beyond = k - static_cast<int>(wide.size());
                    at = {column + 2 * (beyond / pano::kRows), beyond % pano::kRows, 2, 1};
                }
                entries.push_back({'j', j});
                g.places.push_back(at);
            }
            if (figures && !placed_figures)
            {
                if (n == 0)
                    place_figures(2, 2);
                else
                {
                    // After the last column in use.
                    int right = 0;
                    for (const pano::Place &p : g.places)
                        right = std::max(right, p.col + p.w);
                    place_figures(right, 0);
                }
            }
            if (clearable(view))
            {
                // The browser's dark tile that clears this view's history, in the first free cell.
                int right = 0;
                for (const pano::Place &p : g.places)
                    right = std::max(right, p.col + p.w);
                const auto taken = [&](int col, int row)
                {
                    for (const pano::Place &p : g.places)
                        if (col >= p.col && col < p.col + p.w && row >= p.row && row < p.row + p.h)
                            return true;
                    return false;
                };
                pano::Place at{right, 0, 1, 1};
                for (int col = 0; col < right && at.col == right; ++col)
                    for (int row = 0; row < pano::kRows; ++row)
                        if (!taken(col, row))
                        {
                            at = {col, row, 1, 1};
                            break;
                        }
                entries.push_back({'c', 0});
                g.places.push_back(at);
            }
        }
        g.size = static_cast<int>(entries.size());
        g.focusable = [entries](int i) { return entries[static_cast<std::size_t>(i)].kind != 'f'; };
        g.fill = [this, entries, jobs, view](int i, pano::Tile &tile)
        {
            const Snapshot &now = ctx_.store.state();
            const PnEntry &e = entries[static_cast<std::size_t>(i)];
            if (e.kind == 'b')
            {
                tile.key = "browse";
                tile.has_icon = true;
                tile.icon = icons::Icon::down;
                tile.label = tr("Nothing in progress");
                tile.sub = tr("Choose a game to add it to your queue.");
                return;
            }
            if (e.kind == 'l')
            {
                // How many download at once, and what the default drive keeps free.
                tile.key = "limit";
                tile.number = i18n::count(now.system.concurrent_downloads);
                tile.number_size = 80.0f;
                tile.label = tr("at once");
                tile.sub = pano::lower(tr("Simultaneous downloads"));
                return;
            }
            if (e.kind == 'f')
            {
                tile.key = "free";
                tile.has_icon = true;
                tile.icon = icons::Icon::drive;
                const int at = default_drive(now.drives, now.system.preferred_storage);
                if (at >= 0)
                {
                    const Drive &d = now.drives[static_cast<std::size_t>(at)];
                    tile.label = format_bytes(std::max(0.0, d.free_bytes - d.pending_bytes));
                    tile.sub = tr("free after your queue");
                }
                return;
            }
            if (e.kind == 'c')
            {
                tile.key = view == JobView::cancelled ? "clear:cancelled" : "clear:finished";
                tile.has_icon = true;
                tile.icon = icons::Icon::history;
                tile.label = view == JobView::cancelled ? tr("Clear cancelled history") : tr("Clear finished history");
                tile.off = ctx_.store.action().busy;
                return;
            }
            const Job &lead = *jobs.front();
            if (e.kind == 'a')
            {
                const std::vector<XbAction> all = crossbar_actions(lead);
                if (e.index >= static_cast<int>(all.size()))
                    return;
                const XbAction &a = all[static_cast<std::size_t>(e.index)];
                tile.key = "a:" + lead.id + ":" + a.action;
                tile.has_icon = true;
                tile.icon = a.icon;
                tile.label = a.label;
                // The first action stands out (red for a failed download).
                const bool first = i == 1;
                if (first)
                {
                    tile.has_bg = true;
                    tile.bg = view == JobView::failed ? pano::kRed : look::accent_color();
                }
                tile.off = !a.enabled;
                return;
            }
            const Job &job = *jobs[static_cast<std::size_t>(e.index)];
            xb::Row row;
            job_row(now, job, row);
            tile.key = "j:" + job.id;
            tile.game = row.game;
            tile.label = row.title;
            if (row.game == nullptr)
            {
                tile.has_icon = true;
                tile.icon = job.delivery == "zero" ? icons::Icon::laptop : icons::Icon::down;
            }
            tile.bar = row.bar;
            tile.bar_bad = job.status == "error";
            tile.bar_warn = job.can_enter_archive_password;
            const bool big = e.index == 0;
            if (!big)
            {
                tile.dim = 0.45f;
                tile.sub = row.sub;
                return;
            }
            // The first: its state as the kicker, its amounts (or the problem) under its name.
            tile.dim = 0.28f;
            tile.label_size = 28.0f;
            const bool running = job.status == "downloading" || job.status == "extracting" || job.status == "verifying" ||
                                 job.status == "inspecting";
            const Game *game = row.game;
            const Release *release = release_for(game, job);
            // A download simply running says it with its percentage; the others name their state.
            const bool plain = job.status == "downloading" && job.delivery != "torbox" && !job.can_refresh_link;
            if (!plain)
                tile.kicker = running && release != nullptr ? tr(status_label(job.status)) + std::string(" \xC2\xB7 ") + release->provider +
                                                                  " \xC2\xB7 " + format_label(*release)
                                                            : row.sub;
            if (job.delivery == "torbox" && !job.delivery_phase.empty())
                tile.kicker = std::string(tr("via TorBox")) + " \xC2\xB7 " + std::string(tr(std::string_view(job.delivery_phase)));
            tile.sub = !row.detail.empty() ? row.detail : !row.text.empty() ? row.text : row.meta;
            if (job.status == "downloading" && row.bar >= 0.0f)
            {
                tile.number = i18n::whole_percent(row.bar * 100.0f);
                tile.number_size = 128.0f;
            }
        };
        page.groups.push_back(std::move(g));
    }
    return page;
}

const Job *DownloadsScreen::panorama_job(const pano::Page &page, std::string *action) const
{
    // The focused tile's download (an action tile's is its group's first).
    const std::string key = pano_.focused_key(page);
    std::string id;
    if (key.rfind("j:", 0) == 0)
        id = key.substr(2);
    else if (key.rfind("a:", 0) == 0)
    {
        const std::size_t colon = key.find(':', 2);
        id = key.substr(2, colon - 2);
        if (action != nullptr && colon != std::string::npos)
            *action = key.substr(colon + 1);
    }
    for (const Job &job : ctx_.store.state().jobs)
        if (!id.empty() && job.id == id)
            return &job;
    return nullptr;
}

pano::AppBar DownloadsScreen::panorama_bar(const Job &job) const
{
    // Every action for this download; round shortcuts for the first three that can run.
    xb::Row row;
    job_row(ctx_.store.state(), job, row);
    pano::AppBar bar;
    bar.head = row.title + (waiting(job) ? " \xC2\xB7 " + row.sub : std::string());
    const std::vector<XbAction> actions = crossbar_actions(job);
    for (int i = 0; i < static_cast<int>(actions.size()); ++i)
    {
        const XbAction &a = actions[static_cast<std::size_t>(i)];
        pano::Item item;
        item.title = short_label(a.label);
        item.sub = a.sub;
        item.off = !a.enabled;
        item.danger = a.danger;
        bar.items.push_back(std::move(item));
        if (a.enabled && a.action != "game" && bar.shortcuts.size() < 3)
            bar.shortcuts.push_back({a.icon, short_label(a.label), i});
    }
    bar.focus = pn_bar_focus_;
    return bar;
}

Intent DownloadsScreen::update_panorama(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    Intent intent;
    if (password_open_ || dialog_open_)
    {
        // Store's questions and keyboard, with their own answers.
        intent = update_store(input, dt, feedback, focused);
        pn_bar_in_.update(dt, 16.0f);
        return intent;
    }
    pano::Page page = panorama_page();
    if (!pending_job_.empty() && pano_.focus_key(page, "j:" + pending_job_))
    {
        // Show a new download once it appears.
        pending_job_.clear();
        pano_.snap();
    }
    pano_.clamp(page);
    std::string action;
    const Job *job = panorama_job(page, &action);
    if (pn_bar_open_ && job == nullptr)
        pn_bar_open_ = false;
    if (focused && pn_bar_open_)
    {
        pano::AppBar bar = panorama_bar(*job);
        const int chosen = pano::update_appbar(bar, input, feedback);
        pn_bar_focus_ = bar.focus;
        if (chosen != -1)
            pn_bar_open_ = false;
        if (chosen >= 0)
            intent = run_action(*job, crossbar_actions(*job)[static_cast<std::size_t>(chosen)].action, feedback);
    }
    else if (focused)
    {
        const std::string key = pano_.focused_key(page);
        if (input.nav != Direction::none)
            pano_.move(page, input, feedback);
        else if (input.is_pressed(Action::confirm))
        {
            if (key == "browse")
            {
                feedback.play(hui::audio::Cue::select);
                intent.kind = Intent::Kind::show_tab;
                intent.tab = tab::browse;
            }
            else if (key.rfind("clear:", 0) == 0)
                open_clear(key == "clear:cancelled" ? JobView::cancelled : JobView::finished, feedback);
            else if (job != nullptr)
            {
                // An action tile runs its action; a download, its first one.
                const std::vector<XbAction> actions = crossbar_actions(*job);
                const XbAction *a = nullptr;
                for (const XbAction &x : actions)
                    if (action.empty() ? a == nullptr : x.action == action)
                        a = &x;
                if (a == nullptr || !a->enabled)
                    refuse(feedback, input);
                else
                    intent = run_action(*job, a->action, feedback);
            }
            else
                refuse(feedback, input);
        }
        else if (job != nullptr && input.is_pressed(Action::menu))
        {
            pn_bar_open_ = true;
            pn_bar_focus_ = 0;
            feedback.play(hui::audio::Cue::modal_open);
        }
    }
    ctx_.store.set_fast(true);
    pano_.animate(panorama_page(), dt);
    pn_bar_in_.target = pn_bar_open_ ? 1.0f : 0.0f;
    pn_bar_in_.update(dt, look::prefs().reduced_motion ? 40.0f : 16.0f);
    dialog_in_.target = dialog_open_ ? 1.0f : 0.0f;
    dialog_in_.update(dt, 16.0f);
    password_keyboard_.set_active(password_open_);
    password_keyboard_.update(dt);
    return intent;
}

void DownloadsScreen::draw_panorama(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const Snapshot &state = ctx_.store.state();
    if (password_open_)
    {
        // The password as the title, the keys below.
        std::string label = tr("Enter archive password");
        for (const Job &j : state.jobs)
            if (j.id == password_job_)
            {
                const Game *g = game_for(state, j);
                label += " \xC2\xB7 " + (g != nullptr ? g->title : !j.title.empty() ? j.title : j.filename);
            }
        pano::draw_keyboard(ctx_, frame, password_keyboard_, label, password_, {}, true);
        return;
    }
    const pano::Page page = panorama_page();
    pano::draw_header(ctx_, list, pano::lower(tr("Downloads")), pano_.scroll() * 0.35f);
    if (state.jobs.empty())
    {
        pano::draw_empty(ctx_, list, tr("Active"), tr("Nothing in progress"), tr("Choose a game to add it to your queue."),
                         {pano::button(tr("Browse games"), icons::Icon::grid, true)});
    }
    else
        pano_.draw(ctx_, list, page, !pn_bar_open_ && !dialog_open_);
    if (!ctx_.store.action().error.empty())
        ui::text_fit(list, ctx_.type.regular, tr(ctx_.store.action().error), pano::kLeft, 300.0f, 20.0f, 1720.0f, pano::kAmber);
    std::string action;
    if (pn_bar_in_.value > 0.01f)
        if (const Job *job = panorama_job(page, &action))
            pano::draw_appbar(ctx_, frame.overlay, panorama_bar(*job), pn_bar_in_.value);
    if (dialog_in_.value > 0.01f)
    {
        // Cancel and delete: Store's questions as a message band.
        const char *const *labels = dialog_forget_ ? kForgetButtons : dialog_cancelled_ ? kDeleteButtons : kDialogButtons;
        pano::Band band;
        band.title = dialog_cancelled_ ? (dialog_forget_ ? tr("Remove from history?") : tr("Delete this cancelled download?"))
                                       : tr("Cancel this download?");
        band.body = dialog_cancelled_ ? (dialog_forget_
                                             ? tr("Only the history entry will be removed. Any partial file stays on its drive and must be deleted before downloading there again.")
                                             : tr("Delete the partial file and remove this entry. If the file is already gone, the entry will still be removed. Completed files are kept."))
                                      : tr("Keep the partial file to resume later, or delete only the partial file. "
                                           "Downloaded files are never deleted through history cleanup.");
        band.error = tr(std::string_view(dialog_error_));
        for (int b = 0; b < (dialog_forget_ ? 2 : 3); ++b)
            band.buttons.push_back(pano::button(tr(labels[b])));
        band.focus = dialog_button_;
        band.busy = dialog_pending_;
        pano::draw_band(ctx_, frame.overlay, band, dialog_in_.value);
    }
}

std::vector<hui::ui::Hint> DownloadsScreen::panorama_hints() const
{
    if (password_open_)
        return {{hui::ui::Button::square, tr("Delete")}, {hui::ui::Button::circle, tr("Close")}};
    if (dialog_open_)
        return {};
    if (pn_bar_open_)
        return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Close")}};
    const Snapshot &state = ctx_.store.state();
    if (state.jobs.empty())
        return {{hui::ui::Button::cross, tr("Browse games")}, {hui::ui::Button::l1, tr("Sections"), hui::ui::Button::r1}};
    const pano::Page page = panorama_page();
    std::string action;
    const Job *job = panorama_job(page, &action);
    std::vector<hui::ui::Hint> hints;
    const std::string key = pano_.focused_key(page);
    if (key == "browse")
        hints.push_back({hui::ui::Button::cross, tr("Browse games")});
    else if (key.rfind("clear:", 0) == 0)
        hints.push_back({hui::ui::Button::cross, tr("Clear history")});
    else if (job != nullptr)
    {
        // A hint holds a pointer: the label lives on in the screen.
        const std::vector<XbAction> actions = crossbar_actions(*job);
        pn_hint_.clear();
        for (const XbAction &a : actions)
            if (action.empty() ? pn_hint_.empty() : a.action == action)
                pn_hint_ = a.label;
        if (!pn_hint_.empty())
            hints.push_back({hui::ui::Button::cross, pn_hint_.c_str()});
        hints.push_back({hui::ui::Button::options, tr("Actions")});
    }
    // L1 and R1 go through the groups, then on to the next section.
    pn_groups_.clear();
    for (const pano::Group &g : page.groups)
        pn_groups_ += (pn_groups_.empty() ? "" : " \xC2\xB7 ") + g.title;
    if (page.groups.size() > 1)
    {
        for (std::size_t i = 0; i < pn_groups_.size(); ++i)
            if (i == 0 || pn_groups_[i - 1] == ' ')
                pn_groups_[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(pn_groups_[i])));
        hints.push_back({hui::ui::Button::l1, pn_groups_.c_str(), hui::ui::Button::r1});
    }
    else
        hints.push_back({hui::ui::Button::l1, tr("Sections"), hui::ui::Button::r1});
    return hints;
}

const Job *DownloadsScreen::installation_job() const
{
    for (const auto &job : ctx_.store.state().jobs) if (job.id == install_job_) return &job;
    return nullptr;
}
Intent DownloadsScreen::update_installation(const InputFrame &input, Feedback &feedback)
{
    Intent intent;
    const Job *job = installation_job();
    if (!job) { install_open_ = false; install_confirm_.clear(); return intent; }
    if (!install_confirm_.empty()) {
        if (input.is_pressed(Action::back)) { install_confirm_.clear(); return intent; }
        const auto lines = ctx_.type.regular.font->wrap(addon_requirements(*job, ctx_.store.state().library_games()), 24, 1280);
        if (input.nav == Direction::down) install_reading_ = std::min(install_reading_ + 1, std::max(0, static_cast<int>(lines.size()) - 13));
        if (input.nav == Direction::up) install_reading_ = std::max(0, install_reading_ - 1);
        if (input.is_pressed(Action::confirm) && !ctx_.store.action().busy) {
            ctx_.store.job_action(job->id, install_confirm_, false); install_confirm_.clear();
            feedback.play(hui::audio::Cue::select);
        }
        return intent;
    }
    if (input.is_pressed(Action::back)) { install_open_ = false; return intent; }
    const int count = static_cast<int>(job->packages.size());
    install_package_ = std::clamp(install_package_, 0, std::max(0, count - 1));
    if (count > 1 && input.nav != Direction::none) {
        const int step = input.nav == Direction::down || input.nav == Direction::right ? 1 : -1;
        install_package_ = (install_package_ + count + step) % count;
        feedback.play(hui::audio::Cue::focus);
    }
    if (input.is_pressed(Action::confirm) && !ctx_.store.action().busy) {
        std::string command;
        if (count) {
            const auto &pkg = job->packages[static_cast<std::size_t>(install_package_)];
            if ((pkg.state == "ready" || pkg.state == "error") && job->installation_available) command = "install-" + pkg.id;
            else if (pkg.state == "unconfirmed") command = "check-" + pkg.id;
        } else if (job->installation_state.empty() || job->installation_state == "error") command = "check-packages";
        else if (!job->shadowmount_state.empty() && job->shadowmount_state != "none" && job->shadowmount_state != "pending" && job->shadowmount_state != "requested" && job->shadowmount_state != "registered") command = "scan-library";
        if (!command.empty()) {
            if (!job->addon_id.empty() && command.rfind("install-", 0) == 0) {
                install_confirm_ = command; install_reading_ = 0; feedback.play(hui::audio::Cue::modal_open);
            } else { ctx_.store.job_action(job->id, command, false); feedback.play(hui::audio::Cue::select); }
        }
    }
    return intent;
}
void DownloadsScreen::draw_installation(Frame &frame) const
{
    const Job *job = installation_job(); if (!job) return;
    auto &list = frame.overlay; const auto &type = ctx_.type;
    list.rounded_rect({0, 0, 1920, 1080}, 0, ui::color::night0.with_alpha(0.92f));
    const ui::Rect panel{260, 170, 1400, 740}; ui::modal(list, panel);
    if (!install_confirm_.empty()) {
        ui::text_fit(list, type.light, tr("Base-game requirements"), 320, 245, 42, 1280, ui::color::text);
        const auto lines = type.regular.font->wrap(addon_requirements(*job, ctx_.store.state().library_games()), 24, 1280);
        for (int i=install_reading_; i < static_cast<int>(lines.size()) && i<install_reading_+13; ++i)
            ui::text(list, type.regular, lines[static_cast<std::size_t>(i)], 320, 310+34.0f*(i-install_reading_), 24, ui::color::text2);
        ui::button(list, type, {320,810,650,52}, tr("Install selected package"), ui::ButtonKind::primary, 1.0f, !ctx_.store.action().busy, 22);
        ui::text(list, type.regular, tr("Up / Down: Read · Circle: Back"), 1010,845,20,ui::color::text2);
        return;
    }
    ui::text_fit(list, type.light, tr("Downloaded files"), 320, 245, 42, 1280, ui::color::text);
    ui::text_fit(list, type.regular, job->title, 320, 295, 26, 1280, ui::color::text2);
    const PackageInstallation *pkg = job->packages.empty() ? nullptr : &job->packages[static_cast<std::size_t>(std::clamp(install_package_, 0, static_cast<int>(job->packages.size()) - 1))];
    std::string label, message = job->installation_message;
    bool enabled = !ctx_.store.action().busy;
    if (pkg) {
        ui::text_fit(list, type.regular, pkg->filename, 320, 365, 25, 1280, ui::color::text);
        ui::text(list, type.regular, std::to_string(install_package_ + 1) + " / " + std::to_string(job->packages.size()), 320, 405, 20, ui::color::text3);
        ui::paragraph(list, type.regular, ctx_.store.state().system.delete_packages
                          ? tr("The PS5 installs the selected package to its configured installation storage. Once it’s installed, Orbit deletes the downloaded package.")
                          : tr("The PS5 installs the selected package to its configured installation storage. Your downloaded file stays on its drive."), 320, 455, 23, 1280, 34, ui::color::text2, 3);
        message = pkg->message;
        if (pkg->state == "installing" && pkg->total > 0)
            message += " " + format_bytes(pkg->received) + " / " + format_bytes(pkg->total);
        label = pkg->state == "unconfirmed" ? tr("Check installation") : pkg->state == "installed" ? tr("Installed on PS5") : tr("Install on PS5");
        enabled = enabled && ((job->installation_available && (pkg->state == "ready" || pkg->state == "error")) || pkg->state == "unconfirmed");
        if (pkg->platform == "ps5") ui::paragraph(list, type.regular, tr("PS5 game packages require compatible firmware and jailbreak support. Installation does not change game compatibility."), 320, 650, 20, 1280, 30, ui::color::text3, 2);
    } else {
        if (!job->shadowmount_message.empty()) message = job->shadowmount_message;
        else if (job->installation_state == "ready" && message.empty()) message = tr("No installable packages were found in this download.");
        label = job->installation_state.empty() || job->installation_state == "error" ? tr("Check downloaded files") : tr("Scan with ShadowMount");
        enabled = enabled && (job->installation_state.empty() || job->installation_state == "error" ||
            (!job->shadowmount_state.empty() && job->shadowmount_state != "none" && job->shadowmount_state != "registered" && job->shadowmount_state != "pending" && job->shadowmount_state != "requested"));
    }
    ui::paragraph(list, type.regular, tr(message), 320, 555, 24, 1280, 34, ui::color::text, 3);
    if (!ctx_.store.action().error.empty()) ui::paragraph(list, type.regular, tr(ctx_.store.action().error), 320, 730, 20, 1280, 29, ui::color::bad, 2);
    ui::button(list, type, {320, 810, 650, 52}, label, ui::ButtonKind::primary, 1.0f, enabled, 22.0f);
    ui::text(list, type.regular, tr("Circle: Close"), 1100, 845, 22, ui::color::text2);
}

} // namespace orbit
