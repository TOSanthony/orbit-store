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
constexpr float kRowHeight = 174.0f; // the cover and its padding
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

namespace
{
// The DLC tab's rows: one header per game, then (when it is opened) that game's DLC.
// One game's DLC, summed from the list (a game's items sit side by side). `expanded` follows the screen's open list.
struct DlcGroup
{
    std::string base_id;
    std::string game_name;
    int count = 0;
    double size = 0.0;
    bool expanded = false;
};
std::vector<DlcGroup> dlc_groups(const std::vector<GameUpdateItem> &pending, const std::vector<std::string> &open)
{
    std::vector<DlcGroup> groups;
    for (const GameUpdateItem &item : pending)
    {
        auto at = std::find_if(groups.begin(), groups.end(),
                               [&](const DlcGroup &g) { return g.base_id == item.base_id; });
        if (at == groups.end())
        {
            DlcGroup group;
            group.base_id = item.base_id;
            group.game_name = item.game_name;
            group.expanded = std::find(open.begin(), open.end(), item.base_id) != open.end();
            groups.push_back(std::move(group));
            at = groups.end() - 1;
        }
        ++at->count;
        at->size += item.size;
    }
    return groups;
}
struct DlcRow
{
    const DlcGroup *group = nullptr;
    const GameUpdateItem *item = nullptr; // null on a header
};
std::vector<DlcRow> dlc_rows(const std::vector<DlcGroup> &groups, const std::vector<GameUpdateItem> &pending)
{
    std::vector<DlcRow> rows;
    for (const DlcGroup &group : groups)
    {
        rows.push_back({&group, nullptr});
        if (!group.expanded)
            continue;
        for (const GameUpdateItem &item : pending)
            if (item.base_id == group.base_id)
                rows.push_back({&group, &item});
    }
    return rows;
}
} // namespace

GameUpdatesScreen::GameUpdatesScreen(Context &context, bool dlc) : Screen(context), dlc_mode_(dlc)
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
    button_ = 0;
}

Intent GameUpdatesScreen::update(const InputFrame &input, float, Feedback &feedback, bool focused)
{
    Intent intent;
    if (!focused)
        return intent;
    const GameUpdateStatus &status = ctx_.store.state().game_update_status;
    const std::vector<GameUpdateItem> games = pending_game_items(status, dlc_mode_, true);
    const std::vector<DlcGroup> groups = dlc_mode_ ? dlc_groups(games, open_) : std::vector<DlcGroup>();
    const std::vector<DlcRow> rows = dlc_mode_ ? dlc_rows(groups, games) : std::vector<DlcRow>();
    const int count = dlc_mode_ ? static_cast<int>(rows.size()) : static_cast<int>(games.size());
    // The DLC of the focused row, null on a game's header.
    const auto item_at = [&](int at) -> const GameUpdateItem *
    {
        return dlc_mode_ ? rows[static_cast<std::size_t>(at)].item : &games[static_cast<std::size_t>(at)];
    };
    const DlcGroup *header = nullptr;
    // A refused request (another update running, nothing to install) is told once.
    const SettingResult &result = ctx_.store.setting_result();
    if (result.serial != result_serial_ && !result.busy)
    {
        result_serial_ = result.serial;
        if (result.setting == Setting::game_update_job && !result.error.empty())
            intent.toast = tr(result.error);
    }
    // The outcome of picking a version in the browser is told once.
    if (status.pick_state != pick_seen_)
    {
        if (!pick_seen_.empty() && (status.pick_state == "captured" || status.pick_state == "failed") &&
            !status.pick_message.empty())
            intent.toast = tr(status.pick_message);
        pick_seen_ = status.pick_state;
    }
    row_ = std::clamp(row_, 0, std::max(0, count - 1));
    header = dlc_mode_ && count > 0 && item_at(row_) == nullptr ? rows[static_cast<std::size_t>(row_)].group : nullptr;
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
        ctx_.store.game_update_job(dlc_mode_ ? "queueAllDlc" : "queueAll", "");
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
    else if ((input.nav == Direction::left || input.nav == Direction::right) && count > 0)
    {
        // Left/Right move between the row's buttons (Pause/Resume and Cancel; Expand and Install all).
        const GameUpdateItem *g = item_at(row_);
        const bool mine = g != nullptr && job_of(status).present && job_of(status).title_id == g->title_id;
        const bool two = header != nullptr || (mine && ((job_of(status).active && job_of(status).phase != "installing") ||
                                  job_of(status).phase == "paused"));
        const int next = std::clamp(button_ + (input.nav == Direction::right ? 1 : -1), 0, two ? 1 : 0);
        if (next != button_)
            feedback.play(hui::audio::Cue::focus);
        button_ = next;
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
            button_ = 0;
            feedback.play(hui::audio::Cue::focus);
        }
    }
    else if (input.nav == Direction::down)
    {
        if (row_ + 1 < count)
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
    else if (input.is_pressed(Action::confirm) && header != nullptr)
    {
        feedback.play(hui::audio::Cue::select);
        if (button_ == 0)
        {
            const auto at = std::find(open_.begin(), open_.end(), header->base_id);
            if (at == open_.end())
                open_.push_back(header->base_id);
            else
                open_.erase(at);
        }
        else
            for (const GameUpdateItem &item : games) // the game's DLC all go to the download queue
                if (item.base_id == header->base_id)
                    ctx_.store.game_update_job("queue", item.title_id);
    }
    else if (input.is_pressed(Action::confirm) && count > 0)
    {
        const GameUpdateItem &game = *item_at(row_);
        const bool this_job = job_of(status).present && job_of(status).title_id == game.title_id;
        const bool installing = this_job && job_of(status).active && job_of(status).phase == "installing";
        const bool paused_here = this_job && !job_of(status).active && job_of(status).phase == "paused";
        if (button_ == 1 && (paused_here || (this_job && job_of(status).active && !installing)))
        {
            // The second button: cancel and delete what was downloaded.
            feedback.play(hui::audio::Cue::select);
            ctx_.store.game_update_job("cancel", game.title_id);
            button_ = 0;
        }
        else if (paused_here)
        {
            // Resume where it stopped.
            feedback.play(hui::audio::Cue::select);
            ctx_.store.game_update_job("download", game.title_id);
        }
        else if (this_job && job_of(status).active && !installing)
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
            // Its newest patch needs a newer firmware: the version this console can install is picked on
            // prosperopatches.com, in the console's browser.
            if (status.pick_state == "waiting")
            {
                refuse(feedback, input);
                intent.toast = tr("Finish or cancel the current selection first.");
            }
            else
            {
                feedback.play(hui::audio::Cue::select);
                ctx_.store.game_update_job("pickLink", game.title_id);
                intent.toast = tr("Pick your version on prosperopatches.com, then select its CRC link.");
            }
        }
        else if (queue_place(status, game.title_id) > 0)
        {
            // X on a waiting update takes it out of the queue.
            feedback.play(hui::audio::Cue::select);
            ctx_.store.game_update_job("dequeue", game.title_id);
        }
        else if (job_of(status).active || (job_of(status).present && job_of(status).phase == "paused"))
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
    else if (input.is_pressed(Action::back) && count > 0 && header == nullptr)
    {
        // Circle: cancel the running or paused update and delete what was downloaded.
        const GameUpdateItem &game = *item_at(row_);
        const bool this_job = job_of(status).present && job_of(status).title_id == game.title_id;
        if (this_job && (job_of(status).active || job_of(status).phase == "paused"))
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
    const std::vector<GameUpdateItem> pending = pending_game_items(status, dlc_mode_, true);
    const std::vector<DlcGroup> groups = dlc_mode_ ? dlc_groups(pending, open_) : std::vector<DlcGroup>();
    const std::vector<DlcRow> rows = dlc_mode_ ? dlc_rows(groups, pending) : std::vector<DlcRow>();
    const std::vector<GameUpdateItem> listed = dlc_mode_ ? std::vector<GameUpdateItem>() : pending;
    const int shown_rows = dlc_mode_ ? static_cast<int>(rows.size()) : static_cast<int>(listed.size());
    const DlcRow *dlc_row = dlc_mode_ && shown_rows > 0 ? &rows[static_cast<std::size_t>(std::clamp(row_, 0, shown_rows - 1))] : nullptr;
    std::vector<hui::ui::Hint> items;
    if (bar_ >= 0)
    {
        items.push_back({hui::ui::Button::cross, tr("Select")});
        items.push_back({hui::ui::Button::triangle, tr("Check")});
        items.push_back({hui::ui::Button::square, dlc_mode_ ? tr("Install all") : tr("Update all")});
        if (!status.queue.empty())
            items.push_back({hui::ui::Button::r2, tr("Clear queue")});
    }
    else if (dlc_row != nullptr && dlc_row->item == nullptr)
    {
        items.push_back({hui::ui::Button::cross, button_ == 0 ? (dlc_row->group->expanded ? tr("Collapse") : tr("Expand"))
                                                              : tr("Install all")});
        items.push_back({hui::ui::Button::triangle, tr("Check")});
        items.push_back({hui::ui::Button::square, tr("Install all")});
        if (!status.queue.empty())
            items.push_back({hui::ui::Button::r2, tr("Clear queue")});
    }
    else if (dlc_row != nullptr || !listed.empty())
    {
        const GameUpdateItem &game =
            dlc_row != nullptr ? *dlc_row->item
                               : listed[static_cast<std::size_t>(std::clamp(row_, 0, static_cast<int>(listed.size()) - 1))];
        const bool this_job = job_of(status).present && job_of(status).title_id == game.title_id;
        const bool paused = this_job && !job_of(status).active && job_of(status).phase == "paused";
        const bool installing = this_job && job_of(status).active && job_of(status).phase == "installing";
        if ((paused || (this_job && job_of(status).active && !installing)) && button_ == 1)
            items.push_back({hui::ui::Button::cross, tr("Cancel")});
        else if (paused)
            items.push_back({hui::ui::Button::cross, tr("Resume")});
        else if (this_job && job_of(status).active && !installing)
            items.push_back({hui::ui::Button::cross, tr("Pause")});
        else if (queue_place(status, game.title_id) > 0)
            items.push_back({hui::ui::Button::cross, tr("Remove from queue")});
        else if (game.blocked)
            items.push_back({hui::ui::Button::cross, tr("Choose update")});
        else if (!installing)
            items.push_back({hui::ui::Button::cross,
                             job_of(status).active || job_of(status).phase == "paused" ? tr("Add to queue") : tr("Download")});
        if (paused || (this_job && job_of(status).active))
            items.push_back({hui::ui::Button::circle, tr("Cancel")});
        items.push_back({hui::ui::Button::triangle, tr("Check")});
        items.push_back({hui::ui::Button::square, dlc_mode_ ? tr("Install all") : tr("Update all")});
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
    const std::vector<GameUpdateItem> games = pending_game_items(status, dlc_mode_, true);
    const std::vector<DlcGroup> groups = dlc_mode_ ? dlc_groups(games, open_) : std::vector<DlcGroup>();
    const std::vector<DlcRow> rows = dlc_mode_ ? dlc_rows(groups, games) : std::vector<DlcRow>();
    const int row_count = dlc_mode_ ? static_cast<int>(rows.size()) : static_cast<int>(games.size());
    // What the tab lists: the DLC of every game, opened or not.
    std::size_t listed_count = games.size();
    double listed_bytes = 0.0;
    if (dlc_mode_)
    {
        listed_count = 0;
        for (const DlcGroup &group : groups)
        {
            listed_count += static_cast<std::size_t>(group.count);
            listed_bytes += group.size;
        }
    }
    else
        for (const GameUpdateItem &game : games)
            if (!game.blocked) // a patch this console cannot install is not part of "Update all"
                listed_bytes += game.size;
    if (!dlc_mode_)
        listed_count = static_cast<std::size_t>(std::count_if(games.begin(), games.end(), [](const GameUpdateItem &game) { return !game.blocked; }));

    const float title = ui::text(list, type.semibold, dlc_mode_ ? tr("Add-on content") : tr("Updates"),
                                 ui::kGutter, kTitleBaseline, 60.0f, ui::color::text);
    ui::text(list, type.regular,
             dlc_mode_ ? tr("DLC found: {count}", {{"count", std::to_string(listed_count)}})
                       : tr("Updates found: {count}", {{"count", std::to_string(state.game_updates)}}),
             ui::kGutter + title + 24.0f, kTitleBaseline, 20.0f, ui::color::text3);

    if (row_count == 0)
    {
        ui::text(list, type.light, dlc_mode_ ? tr("No DLC") : tr("No game updates"), 960.0f, 462.0f, 32.0f,
                 ui::color::text, ui::Align::center);
        ui::text(list, type.regular,
                 dlc_mode_ ? tr("DLC on your NAS that your installed games do not have yet appear here.")
                           : tr("Installed games with a newer patch on Sony\xE2\x80\x99s servers appear here."),
                 960.0f,
                 508.0f, 20.0f, ui::color::text2, ui::Align::center);
        return;
    }

    {
        // The actions of this page, on the row where Games and Applications keep their filters.
        const double total_bytes = listed_bytes;
        std::vector<std::string> labels;
        labels.push_back(status.checking ? std::string(tr("Checking\xE2\x80\xA6")) : std::string(dlc_mode_ ? tr("Check for DLC") : tr("Check for updates")));
        labels.push_back(std::string(dlc_mode_ ? tr("Install all") : tr("Update all")) + " (" + std::to_string(listed_count) + ") \xC2\xB7 " +
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
    for (int i = 0; i < row_count; ++i)
    {
        const DlcRow *dlc_row = dlc_mode_ ? &rows[static_cast<std::size_t>(i)] : nullptr;
        const bool child = dlc_row != nullptr && dlc_row->item != nullptr;
        // The DLC of an opened game sit a step in, on a bar.
        const float indent = child ? 60.0f : 0.0f;
        const ui::Rect row{kRowLeft + indent, kListTop + static_cast<float>(i) * row_step - scroll, kRowWidth - indent,
                           kRowHeight};
        if (row.y + row.h < kListTop - 6.0f || row.y > kListBottom)
            continue;
        if (focused && bar_ < 0 && i == row_)
            list.rounded_rect(row, 18.0f, ui::Color::rgb(0xffffff, 0.06f));
        if (child)
            list.rounded_rect({row.x - 14.0f, row.y, 3.0f, row.h + kRowGap}, 1.5f, ui::Color::rgb(0xffffff, 0.14f));
        if (dlc_row != nullptr && !child)
        {
            // A game's header: its name, how many DLC and their size, then Expand and Install all.
            const DlcGroup &group = *dlc_row->group;
            Game local;
            local.id = "local-" + group.base_id;
            local.title = group.game_name.empty() ? group.base_id : group.game_name;
            draw_cover(ctx_, list, local, {row.x + kRowPad, row.y + kRowPad, kCover, kCover}, 12.0f);
            const float hx = row.x + kRowPad + kCover + 28.0f;
            const float edge = row.x + row.w - kRowPad;
            const float id_width = type.regular.measure(group.base_id, 16.0f);
            ui::text(list, type.regular, group.base_id, edge, row.y + kRowPad + 21.0f, 16.0f, ui::color::text2,
                     ui::Align::right);
            ui::text_fit(list, type.medium, local.title, hx, row.y + kRowPad + 22.0f, 23.0f, edge - hx - id_width - 24.0f,
                         ui::color::text);
            const std::string count_text = tr("{count} DLC", {{"count", std::to_string(group.count)}}) + " \xC2\xB7";
            const float count_width = type.regular.measure(count_text, 16.0f);
            const float line_y = row.y + kRowPad + 51.0f;
            ui::text(list, type.regular, count_text, hx, line_y, 16.0f, ui::color::text3);
            float after = hx + count_width + 8.0f;
            if (group.size > 0.0)
            {
                ui::icon_storage(list, after + 9.0f, line_y - 5.0f, 18.0f, ui::color::text3);
                ui::text(list, type.regular, format_bytes(group.size), after + 24.0f, line_y, 16.0f, ui::color::text3);
            }
            const std::string labels[2] = {group.expanded ? tr("Collapse") : tr("Expand"), tr("Install all")};
            float bx = hx;
            for (int b = 0; b < 2; ++b)
            {
                const float width = type.medium.measure(labels[b], 16.0f) + 2.0f * 22.0f;
                const bool on = focused && bar_ < 0 && i == row_ && b == button_;
                ui::button(list, type, {bx, row.y + kRowPad + 90.0f, width, 38.0f}, labels[b],
                           on ? ui::ButtonKind::primary : ui::ButtonKind::secondary, on ? 1.0f : 0.0f, true, 16.0f);
                bx += width + 12.0f;
            }
            continue;
        }
        const GameUpdateItem &game = dlc_row != nullptr ? *dlc_row->item : games[static_cast<std::size_t>(i)];
        const ui::Rect cover{row.x + kRowPad, row.y + kRowPad, kCover, kCover};
        // The console's own icon for the game, else the catalogue's cover: Orbit decides which
        // (GET /api/v1/art/local-<title id>/cover); the placeholder shows until it arrives.
        Game local;
        // A DLC shows the icon from its own package (Orbit falls back to the game's when it has none).
        local.id = "local-" + (game.base_id.empty() ? game.title_id
                               : game.label.empty() ? game.base_id : "dlc-" + game.base_id + "-" + game.label);
        local.title = game.name.empty() ? game.title_id : game.name;
        draw_cover(ctx_, list, local, cover, 12.0f);

        // The same rows as Downloads: name and status, where it comes from, the progress bar,
        // the transfer, then the actions.
        const float tx = cover.x + cover.w + 28.0f;
        const float right_edge = row.x + row.w - kRowPad;
        const float info_width = right_edge - tx;
        const GameUpdateJob &job = job_of(status);
        const bool this_job = job.present && job.title_id == game.title_id && !job.phase.empty() &&
                              job.phase != "cancelled"; // a cancelled update goes back to "Update available"
        const int place = queue_place(status, game.title_id);
        const bool paused = this_job && job.phase == "paused";
        const bool running = this_job && job.active && job.phase != "installing";
        const bool installing = this_job && job.phase == "installing";

        std::string state = place > 0 ? std::string(tr("Waiting")) + " \xC2\xB7 #" + std::to_string(place)
                                      : std::string(dlc_mode_ ? tr("DLC available") : game.blocked ? tr("Newer firmware needed") : tr("Update available"));
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
            dlc_mode_ ? game.base_id
                      :
            game.title_id + " \xC2\xB7 " +
            tr("Installed {version}", {{"version", game.installed.empty() ? std::string("?") : game.installed}}) +
            "  \xE2\x86\x92  " +
            (game.blocked ? tr("Needs firmware {version}", {{"version", game.firmware}})
                          : tr("Available {version}", {{"version", game.picked ? tr("your selection") : game.version}}));
        ui::text_fit(list, type.regular, versions, tx, row.y + kRowPad + 51.0f, 16.0f, info_width,
                     ui::color::text3);

        // The size to download, with the storage icon, on the line of the buttons; a failure shows its message there instead.
        const double total_bytes = this_job && job.total > 0.0 ? job.total : game.size;
        const float button_y = row.y + kRowPad + 90.0f;
        if (this_job && (job.phase == "failed" || (job.phase == "downloaded" && !job.error.empty())))
            ui::text_fit(list, type.regular, explained(job), right_edge, button_y + 25.0f, 16.0f, 620.0f,
                         ui::color::bad, ui::Align::right);
        else if (total_bytes > 0.0)
        {
            const std::string size_text = format_bytes(total_bytes);
            ui::text(list, type.regular, size_text, right_edge, button_y + 25.0f, 18.0f, ui::color::text2,
                     ui::Align::right);
            ui::icon_storage(list, right_edge - type.regular.measure(size_text, 18.0f) - 22.0f, button_y + 19.0f,
                             20.0f, ui::color::text2);
        }

        // Small buttons, always shown; the first one of the focused row rings.
        std::vector<std::string> actions;
        if (paused)
            actions = {tr("Resume"), tr("Cancel")};
        else if (running)
            actions = {tr("Pause"), tr("Cancel")};
        else if (place > 0)
            actions = {tr("Remove from queue")};
        else if (game.blocked)
            actions = {tr("Choose update")};
        else if (!installing)
            actions = {job_of(status).active || job_of(status).phase == "paused" ? tr("Add to queue") : tr("Download")};
        float ax = tx;
        for (std::size_t a = 0; a < actions.size(); ++a)
        {
            const float width = type.medium.measure(actions[a], 16.0f) + 2.0f * 22.0f;
            const bool on = focused && bar_ < 0 && i == row_ && a == static_cast<std::size_t>(button_);
            ui::button(list, type, {ax, button_y, width, 38.0f}, actions[a],
                       on ? ui::ButtonKind::primary : ui::ButtonKind::secondary, on ? 1.0f : 0.0f, true, 16.0f);
            ax += width + 12.0f;
        }
    }
    list.pop_clip();
}

} // namespace orbit
