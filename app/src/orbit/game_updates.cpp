// Orbit Store TV app - Updates: installed games that have a newer patch on Sony's servers.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The console's own check (GET /api/v1/game-updates) decides what is listed. Choosing a row
// asks the console to download the patch from Sony and install it; choosing it again while it
// runs cancels. The console does the work and keeps going if this app is closed.

#include "orbit/i18n.hpp"
#include "orbit/screens.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace orbit
{

namespace
{
constexpr float kTitleBaseline = 176.0f; // as the Games, Library and Downloads titles
constexpr float kListTop = 308.0f; // as the grid of Games, under the filter row
constexpr float kBarY = 216.0f;    // the filter row of Games and Applications
constexpr float kBarGap = 14.0f;
constexpr float kRowHeight = 210.0f; // as a download
constexpr float kRowGap = 8.0f;
constexpr float kRowWidth = 1920.0f - 2.0f * ui::kGutter; // as the grid of Games and Applications
constexpr float kRowLeft = (1920.0f - kRowWidth) * 0.5f; // the list sits in the middle of the screen
constexpr float kRowPad = 22.0f;
constexpr float kCover = 130.0f;
constexpr float kListBottom = 1010.0f;

// The console's message in the person's language, then its technical detail if it has one.
std::string explained(const GameUpdateJob &job)
{
    std::string text(tr(job.error));
    if (!job.detail.empty())
        text += " (" + job.detail + ")";
    return text;
}
} // namespace

GameUpdatesScreen::GameUpdatesScreen(Context &context) : Screen(context)
{
}

// 1-based place in the queue, 0 when the update is not waiting.
static int queue_place(const GameUpdateStatus &status, const std::string &title_id)
{
    for (std::size_t i = 0; i < status.queue.size(); ++i)
        if (status.queue[i] == title_id)
            return static_cast<int>(i) + 1;
    return 0;
}

void GameUpdatesScreen::focus()
{
    row_ = 0;
    bar_ = -1;
}

Intent GameUpdatesScreen::update(const InputFrame &input, float, Feedback &feedback, bool focused)
{
    Intent intent;
    if (!focused)
        return intent;
    const GameUpdateStatus &status = ctx_.store.state().game_update_status;
    const std::vector<GameUpdateItem> &games = status.games;
    const int count = static_cast<int>(games.size());
    // A refused request (another update running, nothing to install) is told once.
    const SettingResult &result = ctx_.store.setting_result();
    if (result.serial != result_serial_ && !result.busy)
    {
        result_serial_ = result.serial;
        if (result.setting == Setting::game_update_job && !result.error.empty())
            intent.toast = tr(result.error);
    }
    row_ = std::clamp(row_, 0, std::max(0, count - 1));
    // The three buttons above the list: Check, Update all and (with a queue) Clear queue.
    const int buttons = count > 0 ? (status.queue.empty() ? 2 : 3) : 0;
    bar_ = buttons > 0 ? std::min(bar_, buttons - 1) : -1;
    const auto do_check = [&]
    {
        // A game installed since the last check shows up after this.
        if (!status.checking && !ctx_.store.setting_result().busy)
        {
            feedback.play(hui::audio::Cue::select);
            ctx_.store.check_game_updates();
        }
    };
    const auto do_all = [&]
    {
        // Download every update, one after the other.
        feedback.play(hui::audio::Cue::select);
        ctx_.store.game_update_job("queueAll", "");
    };
    const auto do_clear = [&]
    {
        // Forget the waiting updates; the running one carries on.
        feedback.play(hui::audio::Cue::select);
        ctx_.store.game_update_job("clearQueue", "");
    };
    if (input.is_pressed(Action::north))
        do_check();
    else if (input.is_pressed(Action::west) && count > 0)
        do_all();
    else if (input.is_pressed(Action::jump_next) && !status.queue.empty())
        do_clear();
    else if (bar_ >= 0)
    {
        if (input.nav == Direction::up)
        {
            intent.kind = Intent::Kind::top_bar;
            return intent;
        }
        else if (input.nav == Direction::down)
        {
            bar_ = -1;
            feedback.play(hui::audio::Cue::focus);
        }
        else if (input.nav == Direction::left || input.nav == Direction::right)
        {
            const int next = std::clamp(bar_ + (input.nav == Direction::right ? 1 : -1), 0, buttons - 1);
            if (next != bar_)
                feedback.play(hui::audio::Cue::focus);
            bar_ = next;
        }
        else if (input.is_pressed(Action::confirm))
        {
            if (bar_ == 0)
                do_check();
            else if (bar_ == 1)
                do_all();
            else
                do_clear();
        }
    }
    else if (input.nav == Direction::up)
    {
        if (row_ == 0)
        {
            if (buttons > 0)
            {
                bar_ = 0;
                feedback.play(hui::audio::Cue::focus);
            }
            else
            {
                intent.kind = Intent::Kind::top_bar;
                return intent;
            }
        }
        else
        {
            --row_;
            feedback.play(hui::audio::Cue::focus);
        }
    }
    else if (input.nav == Direction::down)
    {
        if (row_ + 1 < count)
        {
            ++row_;
            feedback.play(hui::audio::Cue::focus);
        }
        else
        {
            refuse(feedback, input);
        }
    }
    else if (input.is_pressed(Action::confirm) && count > 0)
    {
        const GameUpdateItem &game = games[static_cast<std::size_t>(row_)];
        const bool this_job = status.job.present && status.job.title_id == game.title_id;
        const bool installing = this_job && status.job.active && status.job.phase == "installing";
        if (this_job && status.job.active && !installing)
        {
            // X while it downloads: pause, keeping everything already received.
            feedback.play(hui::audio::Cue::select);
            ctx_.store.game_update_job("pause", game.title_id);
        }
        else if (installing)
        {
            refuse(feedback, input);
        }
        else if (game.blocked)
        {
            refuse(feedback, input);
            intent.toast = tr("This update needs a newer firmware than this console has.");
        }
        else if (queue_place(status, game.title_id) > 0)
        {
            // X on a waiting update takes it out of the queue.
            feedback.play(hui::audio::Cue::select);
            ctx_.store.game_update_job("dequeue", game.title_id);
        }
        else if (status.job.active || (status.job.present && status.job.phase == "paused"))
        {
            // Another update has the line: this one waits for its turn.
            feedback.play(hui::audio::Cue::select);
            ctx_.store.game_update_job("queue", game.title_id);
        }
        else if (!ctx_.store.setting_result().busy)
        {
            // X when idle starts the download; X when paused resumes it where it stopped.
            feedback.play(hui::audio::Cue::select);
            ctx_.store.game_update_job("download", game.title_id);
        }
    }
    else if (input.is_pressed(Action::back) && count > 0)
    {
        // Circle: cancel the running or paused update and delete what was downloaded.
        const GameUpdateItem &game = games[static_cast<std::size_t>(row_)];
        const bool this_job = status.job.present && status.job.title_id == game.title_id;
        if (this_job && (status.job.active || status.job.phase == "paused"))
        {
            feedback.play(hui::audio::Cue::select);
            ctx_.store.game_update_job("cancel", game.title_id);
        }
        else if (queue_place(status, game.title_id) > 0)
        {
            feedback.play(hui::audio::Cue::select);
            ctx_.store.game_update_job("dequeue", game.title_id);
        }
    }
    return intent;
}

std::vector<hui::ui::Hint> GameUpdatesScreen::hints() const
{
    const GameUpdateStatus &status = ctx_.store.state().game_update_status;
    std::vector<hui::ui::Hint> items;
    if (bar_ >= 0)
    {
        items.push_back({hui::ui::Button::cross, tr("Select")});
        items.push_back({hui::ui::Button::triangle, tr("Check")});
        items.push_back({hui::ui::Button::square, tr("Update all")});
        if (!status.queue.empty())
            items.push_back({hui::ui::Button::r2, tr("Clear queue")});
    }
    else if (!status.games.empty())
    {
        const GameUpdateItem &game =
            status.games[static_cast<std::size_t>(std::clamp(row_, 0, static_cast<int>(status.games.size()) - 1))];
        const bool this_job = status.job.present && status.job.title_id == game.title_id;
        const bool paused = this_job && !status.job.active && status.job.phase == "paused";
        const bool installing = this_job && status.job.active && status.job.phase == "installing";
        if (paused)
            items.push_back({hui::ui::Button::cross, tr("Resume")});
        else if (this_job && status.job.active && !installing)
            items.push_back({hui::ui::Button::cross, tr("Pause")});
        else if (queue_place(status, game.title_id) > 0)
            items.push_back({hui::ui::Button::cross, tr("Remove from queue")});
        else if (!installing)
            items.push_back({hui::ui::Button::cross,
                             status.job.active || status.job.phase == "paused" ? tr("Add to queue") : tr("Download")});
        if (paused || (this_job && status.job.active))
            items.push_back({hui::ui::Button::circle, tr("Cancel")});
        items.push_back({hui::ui::Button::triangle, tr("Check")});
        items.push_back({hui::ui::Button::square, tr("Update all")});
        if (!status.queue.empty())
            items.push_back({hui::ui::Button::r2, tr("Clear queue")});
    }
    items.push_back({hui::ui::Button::l1, tr("Tabs"), hui::ui::Button::r1});
    return items;
}

void GameUpdatesScreen::draw(Frame &frame, bool focused) const
{
    hui::gfx::DrawList &list = frame.scene;
    const Snapshot &state = ctx_.store.state();
    const ui::Type &type = ctx_.type;
    const GameUpdateStatus &status = state.game_update_status;
    const std::vector<GameUpdateItem> &games = status.games;

    const float title =
        ui::text(list, type.semibold, tr("Game updates"), ui::kGutter, kTitleBaseline, 60.0f, ui::color::text);
    ui::text(list, type.regular, tr("Updates found: {count}", {{"count", std::to_string(state.game_updates)}}),
             ui::kGutter + title + 24.0f, kTitleBaseline, 20.0f, ui::color::text3);

    if (games.empty())
    {
        ui::text(list, type.light, tr("No game updates"), 960.0f, 462.0f, 32.0f, ui::color::text,
                 ui::Align::center);
        ui::text(list, type.regular,
                 tr("Installed games with a newer patch on Sony\xE2\x80\x99s servers appear here."), 960.0f,
                 508.0f, 20.0f, ui::color::text2, ui::Align::center);
        return;
    }

    {
        // The actions of this page, on the row where Games and Applications keep their filters.
        double total_bytes = 0.0;
        for (const GameUpdateItem &game : games)
            total_bytes += game.size;
        std::vector<std::string> labels;
        labels.push_back(status.checking ? std::string(tr("Checking\xE2\x80\xA6")) : std::string(tr("Check for updates")));
        labels.push_back(std::string(tr("Update all")) + " (" + std::to_string(games.size()) + ") \xC2\xB7 " +
                         format_bytes(total_bytes));
        if (!status.queue.empty())
            labels.push_back(std::string(tr("Clear queue")));
        float x = 1920.0f - ui::kGutter;
        for (int k = static_cast<int>(labels.size()) - 1; k >= 0; --k)
        {
            const std::string &label = labels[static_cast<std::size_t>(k)];
            const float w = ui::button_width(type, label, 20.0f, ui::ButtonKind::secondary);
            x -= w;
            ui::button(list, type, {x, kBarY, w, ui::kFieldHeight}, label, ui::ButtonKind::secondary,
                       focused && bar_ == k ? 1.0f : 0.0f, !(k == 0 && status.checking));
            x -= kBarGap;
        }
    }

    // Keep the focused row on screen: the list moves up once the cursor passes the bottom.
    const float row_step = kRowHeight + kRowGap;
    const float visible = kListBottom - kListTop;
    const float scroll = std::max(0.0f, (static_cast<float>(row_) + 1.0f) * row_step - visible);
    list.push_clip({0.0f, kListTop - 6.0f, 1920.0f, kListBottom - kListTop + 6.0f});
    for (int i = 0; i < static_cast<int>(games.size()); ++i)
    {
        const GameUpdateItem &game = games[static_cast<std::size_t>(i)];
        const ui::Rect row{kRowLeft, kListTop + static_cast<float>(i) * row_step - scroll, kRowWidth,
                           kRowHeight};
        if (row.y + row.h < kListTop - 6.0f || row.y > kListBottom)
            continue;
        if (focused && bar_ < 0 && i == row_)
            list.rounded_rect(row, 18.0f, ui::Color::rgb(0xffffff, 0.06f));
        const ui::Rect cover{row.x + kRowPad, row.y + kRowPad, kCover, kCover};
        // The console's own icon for the game, else the catalogue's cover: Orbit decides which
        // (GET /api/v1/art/local-<title id>/cover); the placeholder shows until it arrives.
        Game local;
        local.id = "local-" + game.title_id;
        local.title = game.name.empty() ? game.title_id : game.name;
        draw_cover(ctx_, list, local, cover, 12.0f);

        // The same rows as Downloads: name and status, where it comes from, the progress bar,
        // the transfer, then the actions.
        const float tx = cover.x + cover.w + 28.0f;
        const float right_edge = row.x + row.w - kRowPad;
        const float info_width = right_edge - tx;
        const GameUpdateJob &job = status.job;
        const bool this_job = job.present && job.title_id == game.title_id && !job.phase.empty();
        const int place = queue_place(status, game.title_id);
        const bool paused = this_job && job.phase == "paused";
        const bool running = this_job && job.active && job.phase != "installing";
        const bool installing = this_job && job.phase == "installing";
        const double fraction =
            this_job && job.total > 0.0 ? std::clamp(job.done / job.total, 0.0, 1.0) : 0.0;
        const float progress = installing ? static_cast<float>(job.install_percent) / 100.0f
                                          : static_cast<float>(fraction);

        std::string state = place > 0 ? std::string(tr("Waiting")) + " \xC2\xB7 #" + std::to_string(place)
                                      : std::string(tr("Update available"));
        ui::Color state_ink = ui::color::text2;
        if (this_job)
        {
            if (job.phase == "preparing")
                state = tr("Preparing the download\xE2\x80\xA6");
            else if (job.phase == "downloading")
                state = tr("Downloading");
            else if (job.phase == "verifying")
                state = tr("Checking the downloaded files\xE2\x80\xA6");
            else if (job.phase == "installing")
                state = tr("Installing");
            else if (job.phase == "installed")
                state = tr("Installed");
            else if (job.phase == "paused")
                state = tr("Paused");
            else if (job.phase == "cancelled")
                state = tr("Cancelled");
            else if (job.phase == "downloaded")
                state = tr("Downloaded");
            else
                state = tr("Update failed");
            if (job.phase == "failed")
                state_ink = ui::color::bad;
            else if (job.phase == "installed")
                state_ink = ui::color::ok;
            else if (job.active)
                state_ink = ui::Color::rgb(0xd0d0d0);
        }
        const float state_width = type.regular.measure(state, 16.0f);
        ui::text(list, type.regular, state, right_edge, row.y + kRowPad + 21.0f, 16.0f, state_ink,
                 ui::Align::right);
        ui::text_fit(list, type.medium, game.name.empty() ? game.title_id : game.name, tx,
                     row.y + kRowPad + 22.0f, 23.0f, info_width - state_width - 24.0f, ui::color::text);
        const std::string versions =
            game.title_id + " \xC2\xB7 " +
            tr("Installed {version}", {{"version", game.installed.empty() ? std::string("?") : game.installed}}) +
            "  \xE2\x86\x92  " + tr("Available {version}", {{"version", game.version}});
        ui::text_fit(list, type.regular, versions, tx, row.y + kRowPad + 51.0f, 16.0f, info_width,
                     ui::color::text3);

        const ui::Rect bar{tx, row.y + kRowPad + 74.0f, info_width, 6.0f};
        if (installing && job.waiting)
            ui::progress_bar_busy(list, bar, ctx_.clock, ui::color::action);
        else
            ui::progress_bar(list, bar, progress,
                             job.phase == "failed" && this_job ? ui::color::bad
                             : job.phase == "installed" && this_job ? ui::color::ok
                                                                    : ui::color::action);
        const double received = this_job ? job.done : 0.0;
        const double total_bytes = this_job && job.total > 0.0 ? job.total : game.size;
        const std::string left =
            installing && job.waiting ? std::string(tr("Installing\xE2\x80\xA6 waiting for the console"))
            : installing ? std::string(tr("Installing")) + " \xC2\xB7 " + std::to_string(job.install_percent) + "%"
                       : format_bytes(received) + " / " + format_bytes(total_bytes) + " \xC2\xB7 " +
                             i18n::percent(fraction * 100.0);
        const float transfer_y = row.y + kRowPad + 104.0f;
        ui::text(list, type.regular, left, tx, transfer_y, 16.0f, ui::color::text3);
        std::string right;
        if (this_job && (job.phase == "failed" || (job.phase == "downloaded" && !job.error.empty())))
        {
            ui::text_fit(list, type.regular, explained(job), right_edge, transfer_y, 16.0f, 620.0f,
                         ui::color::bad, ui::Align::right);
        }
        else if (!installing)
        {
            if (running && job.speed > 0.0)
            {
                const double minutes = std::ceil((job.total - job.done) / job.speed / 60.0);
                right = tr("{speed}/s \xC2\xB7 {minutes} min remaining",
                           {{"speed", format_bytes(job.speed)}, {"minutes", i18n::count(static_cast<long>(minutes))}});
            }
            else
            {
                right = tr("{size} remaining", {{"size", format_bytes(std::max(0.0, total_bytes - received))}});
            }
            ui::text(list, type.regular, right, right_edge, transfer_y, 16.0f, ui::color::text3,
                     ui::Align::right);
        }

        // Small buttons, always shown; the first one of the focused row rings.
        std::vector<std::string> actions;
        if (paused)
            actions = {tr("Resume"), tr("Cancel")};
        else if (running)
            actions = {tr("Pause"), tr("Cancel")};
        else if (place > 0)
            actions = {tr("Remove from queue")};
        else if (!installing)
            actions = {status.job.active || status.job.phase == "paused" ? tr("Add to queue") : tr("Download")};
        float ax = tx;
        for (std::size_t a = 0; a < actions.size(); ++a)
        {
            const float width = type.medium.measure(actions[a], 16.0f) + 2.0f * 22.0f;
            const bool on = focused && bar_ < 0 && i == row_ && a == 0;
            ui::button(list, type, {ax, row.y + kRowPad + 128.0f, width, 38.0f}, actions[a],
                       on ? ui::ButtonKind::primary : ui::ButtonKind::secondary, on ? 1.0f : 0.0f, true, 16.0f);
            ax += width + 12.0f;
        }
    }
    list.pop_clip();
}

} // namespace orbit
