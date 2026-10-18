// Orbit Store TV app - Updates: installed games that have a newer patch on Sony's servers.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The console's own check (GET /api/v1/game-updates) decides what is listed; this screen only
// shows it. Downloading and installing a patch is the next step and is not wired here yet.

#include "orbit/i18n.hpp"
#include "orbit/screens.hpp"

#include <algorithm>

namespace orbit
{

namespace
{
constexpr float kTitleBaseline = 176.0f; // as the Games, Library and Downloads titles
constexpr float kListTop = 236.0f;
constexpr float kRowHeight = 150.0f;
constexpr float kRowGap = 8.0f;
constexpr float kRowWidth = 1400.0f;
constexpr float kRowPad = 22.0f;
constexpr float kCover = 106.0f;
constexpr float kListBottom = 1010.0f;
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
    const std::vector<GameUpdateItem> &games = ctx_.store.state().game_update_status.games;
    const int count = static_cast<int>(games.size());
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
        feedback.play(hui::audio::Cue::select);
        intent.toast = tr("Downloading game updates is not available yet.");
    }
    return intent;
}

void GameUpdatesScreen::draw(Frame &frame, bool focused) const
{
    hui::gfx::DrawList &list = frame.scene;
    const Snapshot &state = ctx_.store.state();
    const ui::Type &type = ctx_.type;
    const std::vector<GameUpdateItem> &games = state.game_update_status.games;

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
        const ui::Rect row{ui::kGutter, kListTop + static_cast<float>(i) * row_step - scroll, kRowWidth,
                           kRowHeight};
        if (row.y + row.h < kListTop - 6.0f || row.y > kListBottom)
            continue;
        if (focused && i == row_)
            list.rounded_rect(row, 18.0f, ui::Color::rgb(0xffffff, 0.06f));
        const ui::Rect cover{row.x + kRowPad, row.y + (row.h - kCover) * 0.5f, kCover, kCover};
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
        if (game.blocked)
            ui::text(list, type.regular,
                     tr("Needs firmware {firmware} or later", {{"firmware", game.firmware}}), tx,
                     row.y + 122.0f, 18.0f, ui::color::text3);
    }
    list.pop_clip();
}

} // namespace orbit
