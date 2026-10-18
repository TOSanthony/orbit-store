// Orbit Store TV app - Updates: installed games that have a newer patch on Sony's servers.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The console's own check (GET /api/v1/game-updates) decides what is listed. Choosing a row
// asks the console to download the patch from Sony and install it; choosing it again while it
// runs cancels. The console does the work and keeps going if this app is closed.

#include "orbit/i18n.hpp"
#include "orbit/screens.hpp"

#include <algorithm>
#include <cstdio>

namespace orbit
{

namespace
{
constexpr float kTitleBaseline = 176.0f; // as the Games, Library and Downloads titles
constexpr float kListTop = 236.0f;
constexpr float kRowHeight = 150.0f;
constexpr float kRowGap = 8.0f;
constexpr float kRowWidth = 1400.0f;
constexpr float kRowLeft = (1920.0f - kRowWidth) * 0.5f; // the list sits in the middle of the screen
constexpr float kRowPad = 22.0f;
constexpr float kCover = 106.0f;
constexpr float kListBottom = 1010.0f;

// The catalogue entry with this title ID, for its cover (null for apps and games it does not list).
const Game *catalogue_entry(const Snapshot &state, std::string_view title_id)
{
    for (const Game &game : state.games)
        for (const Release &release : game.releases)
            if (release.title_id == title_id)
                return &game;
    return nullptr;
}

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

void GameUpdatesScreen::focus()
{
    row_ = 0;
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
    if (input.nav == Direction::up)
    {
        if (row_ == 0)
        {
            intent.kind = Intent::Kind::top_bar;
            return intent;
        }
        --row_;
        feedback.play(hui::audio::Cue::focus);
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
        if (this_job && status.job.active)
        {
            feedback.play(hui::audio::Cue::select);
            ctx_.store.game_update_job("cancel", game.title_id);
        }
        else if (game.blocked)
        {
            refuse(feedback, input);
            intent.toast = tr("This update needs a newer firmware than this console has.");
        }
        else if (status.job.active)
        {
            refuse(feedback, input);
            intent.toast = tr("Another game update is already in progress.");
        }
        else if (!ctx_.store.setting_result().busy)
        {
            feedback.play(hui::audio::Cue::select);
            ctx_.store.game_update_job("download", game.title_id);
        }
    }
    return intent;
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
        if (focused && i == row_)
            list.rounded_rect(row, 18.0f, ui::Color::rgb(0xffffff, 0.06f));
        const ui::Rect cover{row.x + kRowPad, row.y + (row.h - kCover) * 0.5f, kCover, kCover};
        if (const Game *art = catalogue_entry(state, game.title_id))
            draw_cover(ctx_, list, *art, cover, 12.0f);
        else
            ui::cover_placeholder(list, type, cover, game.name.empty() ? game.title_id : game.name, 12.0f);

        const float tx = cover.x + cover.w + 28.0f;
        const float right_edge = row.x + row.w - kRowPad;
        ui::text(list, type.semibold, game.name.empty() ? game.title_id : game.name, tx, row.y + 54.0f, 28.0f,
                 ui::color::text);
        ui::text(list, type.regular, game.title_id, right_edge, row.y + 54.0f, 18.0f, ui::color::text3,
                 ui::Align::right);
        const std::string versions =
            tr("Installed {version}", {{"version", game.installed.empty() ? std::string("?") : game.installed}}) +
            "  \xE2\x86\x92  " + tr("Available {version}", {{"version", game.version}});
        ui::text(list, type.regular, versions, tx, row.y + 92.0f, 20.0f, ui::color::text2);
        if (game.size > 0.0)
        {
            // The size sits under the title ID, at the right edge, after a drive icon.
            const float width = ui::text(list, type.regular, format_bytes(game.size), right_edge, row.y + 92.0f,
                                         20.0f, ui::color::text2, ui::Align::right);
            ui::icon_storage(list, right_edge - width - 22.0f, row.y + 85.0f, 24.0f, ui::color::text2);
        }
        const GameUpdateJob &job = status.job;
        const bool this_job = job.present && job.title_id == game.title_id;
        if (this_job && !job.phase.empty())
        {
            const double fraction = job.total > 0.0 ? std::clamp(job.done / job.total, 0.0, 1.0) : 0.0;
            char percent[16];
            std::snprintf(percent, sizeof(percent), "%d", static_cast<int>(fraction * 100.0));
            std::string line;
            if (job.phase == "preparing")
                line = tr("Preparing the download\xE2\x80\xA6");
            else if (job.phase == "downloading")
                line = tr("Downloading {percent}% \xC2\xB7 {speed}",
                          {{"percent", percent}, {"speed", format_bytes(job.speed) + "/s"}});
            else if (job.phase == "verifying")
                line = tr("Checking the downloaded files\xE2\x80\xA6");
            else if (job.phase == "installing")
                line = tr("Installing {percent}%", {{"percent", std::to_string(job.install_percent)}});
            else if (job.phase == "installed")
                line = tr("Installed");
            else if (job.phase == "downloaded")
                line = job.error.empty() ? std::string(tr("Downloaded")) : explained(job);
            else if (job.phase == "cancelled")
                line = tr("Cancelled");
            else
                line = tr("Update failed: {error}", {{"error", explained(job)}});
            ui::text(list, type.regular, line, tx, row.y + 122.0f, 18.0f, ui::color::text2);
            if (job.phase == "downloading" || job.phase == "verifying")
                ui::progress_bar(list, {tx, row.y + row.h - 22.0f, right_edge - tx - 360.0f, 6.0f},
                                 static_cast<float>(fraction), ui::color::text);
            else if (job.phase == "installing")
                ui::progress_bar(list, {tx, row.y + row.h - 22.0f, right_edge - tx - 360.0f, 6.0f},
                                 static_cast<float>(job.install_percent) / 100.0f, ui::color::text);
        }
        else if (game.blocked)
            ui::text(list, type.regular,
                     tr("Needs firmware {firmware} or later", {{"firmware", game.firmware}}), tx,
                     row.y + 122.0f, 18.0f, ui::color::text3);
        if (focused && i == row_ && !game.blocked)
            ui::text(list, type.regular, this_job && job.active ? tr("Cancel") : tr("Download and install"),
                     right_edge, row.y + 122.0f, 18.0f, ui::color::text3, ui::Align::right);
    }
    list.pop_clip();
}

} // namespace orbit
