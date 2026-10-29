// Orbit Store TV app - Discover in the other interfaces: the same rows
// (New on Orbit, Latest releases), then All games.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/i18n.hpp"
#include "orbit/look.hpp"
#include "orbit/screens.hpp"

#include <algorithm>
#include <cmath>

namespace orbit
{

namespace
{

// A tile's second line: what is happening to the game, else how it is
// described (the big tile: its genre and size, release date or platform).
std::string game_note(const Snapshot &state, const Game &game, bool big, char group)
{
    const CollectionState now = game_state(game, state.jobs, state.library_games());
    if (now.label != nullptr && now.label[0] != '\0')
        return pano::lower(now.label);
    if (group == 'l' && !game.release_date.empty() && !big)
        return format_date(game.release_date);
    if (!big)
        return {};
    std::string note = game.genre.empty() ? std::string() : i18n::genre(game.genre);
    std::string more;
    if (group == 'l')
        more = game.release_date.empty() ? std::string() : format_date(game.release_date);
    else if (group == 'a')
        more = !game.releases.empty() && game.releases.front().platform == "ps4" ? "PS4" : "PS5";
    else if (!game.releases.empty() && listed_size(game.releases.front()) > 0.0)
        more = format_bytes(listed_size(game.releases.front()));
    if (!more.empty())
        note += (note.empty() ? "" : " \xC2\xB7 ") + more;
    return note;
}

// What each of a game's four actions does, under its name (Cinema's cards and
// Panorama's app bar): the first as the game's state has it (its download's
// status, where its Library copy is, or where Download saves it), then the
// other three.
std::vector<std::string> action_notes(const Snapshot &state, const Game &game)
{
    const CollectionState now = game_state(game, state.jobs, state.library_games());
    const int drive = default_drive(state.drives, state.system.preferred_storage);
    const std::string first = now.job != nullptr && now.label != nullptr ? std::string(tr(now.label))
                              : now.kind == CollectionState::Kind::library && now.library != nullptr ? i18n::place(now.library->location)
                              : drive >= 0 ? tr("To {drive}", {{"drive", i18n::place(state.drives[static_cast<std::size_t>(drive)].label)}})
                                           : std::string();
    return {first, tr("Versions, drive and about"), tr("Saved on this console"), tr("Publisher, dates, sizes")};
}

} // namespace

pano::Page DiscoverScreen::panorama_page() const
{
    pano::Page page;
    // Each group opens on a large tile, then small ones. New on Orbit
    // shows the complete curated row; Latest releases keeps its compact preview.
    const auto group = [&](const std::string &title, std::string count, const std::vector<int> *games,
                           std::size_t most, const char *key)
    {
        pano::Group g;
        g.title = pano::lower(title);
        g.count = std::move(count);
        g.size = static_cast<int>(std::min(games->size(), most));
        g.pattern = pano::Pattern::lead;
        g.fill = [this, games, key](int i, pano::Tile &tile)
        {
            const Snapshot &now = ctx_.store.state();
            const int at = (*games)[static_cast<std::size_t>(i)];
            if (at < 0 || at >= static_cast<int>(now.games.size()))
                return;
            const Game &game = now.games[static_cast<std::size_t>(at)];
            tile.key = std::string(key) + game.id;
            tile.game = &game;
            tile.name_on_focus = true;
            tile.ps4 = !game.releases.empty() && game.releases.front().platform == "ps4";
            tile.star = now.favourite(game.id);
            tile.label = game.title;
            tile.sub = game_note(now, game, i == 0, key[0]);
            if (i == 0)
                tile.label_size = 30.0f;
            // A download: how it comes (TorBox) and, while it runs, how far
            // it is as measured, with the time left on a wide tile.
            const CollectionState state = game_state(game, now.jobs, now.library_games());
            const Job *job = state.job;
            if (job == nullptr || job_inactive(*job))
                return;
            if (job->delivery == "torbox")
                tile.sub += " \xC2\xB7 " + std::string(tr("via TorBox"));
            if (job->status != "downloading" || job->total <= 0.0)
                return;
            const double done = std::clamp(job->received / job->total, 0.0, 1.0);
            tile.bar = static_cast<float>(done);
            tile.sub += " \xC2\xB7 " + i18n::whole_percent(done * 100.0);
            if (i == 0 && job->speed > 0.0)
                tile.sub += " \xC2\xB7 " + tr("{minutes} min left",
                                               {{"minutes", i18n::count(static_cast<long>(std::ceil(
                                                                (job->total - job->received) / job->speed / 60.0)))}});
        };
        page.groups.push_back(std::move(g));
    };
    for (std::size_t r = 0; r < rails_.size(); ++r)
    {
        const Rail &rail = rails_[r];
        if (rail.grid)
            continue;
        const bool latest = rail.title != tr("New on Orbit");
        group(rail.title, std::string(), &rail.games, latest ? 9 : rail.games.size(), latest ? "l:" : "n:");
    }
    if (!all_games_.empty())
        group(tr("All games"), i18n::count(static_cast<long>(all_games_.size())), &all_games_, all_games_.size(), "a:");
    return page;
}

const Game *DiscoverScreen::panorama_game(const pano::Page &page) const
{
    if (page.groups.empty())
        return nullptr;
    const pano::Group &g = page.groups[static_cast<std::size_t>(std::clamp(pano_.focus.group, 0, static_cast<int>(page.groups.size()) - 1))];
    if (!g.fill || pano_.focus.index < 0 || pano_.focus.index >= g.size)
        return nullptr;
    pano::Tile tile;
    g.fill(pano_.focus.index, tile);
    return tile.game;
}

pano::AppBar DiscoverScreen::panorama_bar(const Game &game) const
{
    // The same four actions as Crossbar's menu, each with what it does, and
    // round shortcuts for download, favourite and about.
    const Snapshot &state = ctx_.store.state();
    const std::vector<xb::MenuItem> actions = xb_game_actions(state, game);
    pano::AppBar bar;
    bar.head = game.title;
    const std::vector<std::string> notes = action_notes(state, game);
    for (std::size_t i = 0; i < actions.size(); ++i)
    {
        pano::Item item;
        item.title = actions[i].label;
        item.sub = i < notes.size() ? notes[i] : std::string();
        item.off = !actions[i].enabled;
        bar.items.push_back(std::move(item));
    }
    bar.shortcuts = {{icons::Icon::down, pano::lower(tr("Download")), 0},
                     {state.favourite(game.id) ? icons::Icon::heart_fill : icons::Icon::heart, pano::lower(tr("Favourite")), 2},
                     {icons::Icon::info, pano::lower(tr("About")), 3}};
    bar.focus = pano_bar_focus_;
    return bar;
}

Intent DiscoverScreen::update_panorama(const InputFrame &input, float dt, Feedback &feedback,
                                       bool focused)
{
    Intent intent;
    if (catalog_serial_ != ctx_.store.state().catalog_serial)
        rebuild();
    const pano::Page page = panorama_page();
    pano_.clamp(page);
    const Snapshot &state = ctx_.store.state();
    const Game *game = panorama_game(page);
    if (pano_bar_open_ && game == nullptr)
        pano_bar_open_ = false;
    if (focused && pano_bar_open_)
    {
        pano::AppBar bar = panorama_bar(*game);
        const int chosen = pano::update_appbar(bar, input, feedback);
        pano_bar_focus_ = bar.focus;
        if (chosen != -1)
            pano_bar_open_ = false;
        if (chosen >= 0)
            intent = xb_game_action(ctx_, *game, chosen);
    }
    else if (focused && !page.groups.empty())
    {
        if (input.nav != Direction::none)
            pano_.move(page, input, feedback);
        else if (game != nullptr && input.is_pressed(Action::north))
        {
            if (state.paired && state.link == Link::online && !ctx_.store.favourite_action().busy)
            {
                ctx_.store.set_favourite(game->id, !state.favourite(game->id));
                feedback.play(hui::audio::Cue::toggle);
            }
            else
                refuse(feedback, input);
        }
        else if (game != nullptr && (input.is_pressed(Action::confirm) || input.is_pressed(Action::west)))
        {
            // Cross opens the game; so does Square (its details), as Controls says.
            feedback.play(hui::audio::Cue::select);
            intent.kind = Intent::Kind::open_game;
            intent.id = game->id;
            return intent;
        }
        else if (game != nullptr && input.is_pressed(Action::menu))
        {
            pano_bar_open_ = true;
            pano_bar_focus_ = 0;
            feedback.play(hui::audio::Cue::modal_open);
        }
        else if (input.is_pressed(Action::jump_prev) || input.is_pressed(Action::jump_next))
        {
            // L2 and R2: the next or previous group.
            const bool next = input.is_pressed(Action::jump_next);
            if (!pano_.jump(page, next ? 1 : -1, feedback))
                refuse(feedback, input);
        }
    }
    pano_.animate(page, dt);
    pano_bar_in_.target = pano_bar_open_ ? 1.0f : 0.0f;
    pano_bar_in_.update(dt, look::prefs().reduced_motion ? 40.0f : 16.0f);
    // The art behind the page follows the focus.
    if (const Game *now = panorama_game(page))
        featured_ = now->id;
    return intent;
}

void DiscoverScreen::draw_panorama(Frame &frame, bool focused) const
{
    hui::gfx::DrawList &list = frame.scene;
    const pano::Page page = panorama_page();
    pano::draw_header(ctx_, list, pano::lower(tr("Discover")), pano_.scroll() * 0.35f);
    pano_.draw(ctx_, list, page, focused);
    if (pano_bar_in_.value > 0.01f)
        if (const Game *game = panorama_game(page))
            pano::draw_appbar(ctx_, frame.overlay, panorama_bar(*game), pano_bar_in_.value);
}

std::vector<hui::ui::Hint> DiscoverScreen::panorama_hints() const
{
    if (pano_bar_open_)
        return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Close")}};
    const Snapshot &state = ctx_.store.state();
    const pano::Page page = panorama_page();
    const Game *game = panorama_game(page);
    std::vector<hui::ui::Hint> hints{{hui::ui::Button::cross, tr("Open")}};
    if (game != nullptr && state.paired)
        hints.push_back({hui::ui::Button::triangle, state.favourite(game->id) ? tr("Remove from favourites") : tr("Favourite")});
    if (game != nullptr)
        hints.push_back({hui::ui::Button::options, tr("Actions")});
    hints.push_back({hui::ui::Button::l1, tr("Sections"), hui::ui::Button::r1});
    return hints;
}

// ---------------------------------------------------------------- Crossbar

// A game as a Crossbar row: its platform, genre and what is happening to it.
void xb_game_row(const Snapshot &state, const Game &game, xb::Row &row, bool dated)
{
    row.game = &game;
    row.title = game.title;
    row.favourite = state.favourite(game.id);
    const bool ps4 = !game.releases.empty() && game.releases.front().platform == "ps4";
    if (ps4)
        row.tag = "PS4";
    std::string sub = ps4 ? "PS4" : "PS5";
    if (!game.addon_id.empty())
    {
        row.sub = game.base_title;
        row.text = std::string(addon_type_label(game.addon_type)) + " \xC2\xB7 " + sub;
        return;
    }
    if (dated && !game.release_date.empty())
    {
        // Latest releases: the platform and the date.
        row.sub = sub + " \xC2\xB7 " + tr("Released {date}", {{"date", format_date(game.release_date)}});
        return;
    }
    if (!game.genre.empty())
        sub += " \xC2\xB7 " + i18n::genre(game.genre);
    const CollectionState now = game_state(game, state.jobs, state.library_games());
    if (now.label != nullptr && now.label[0] != '\0')
    {
        sub += " \xC2\xB7 " + std::string(tr(now.label));
        if (now.job != nullptr && now.job->delivery == "torbox")
            sub += " \xC2\xB7 " + std::string(tr("via TorBox"));
    }
    else if (!game.releases.empty() && listed_size(game.releases.front()) > 0.0)
        row.size = format_bytes(listed_size(game.releases.front()));
    row.sub = std::move(sub);
}

std::vector<xb::MenuItem> xb_game_actions(const Snapshot &state, const Game &game)
{
    std::vector<xb::MenuItem> items;
    // The first action follows the game's state, as its page does: View
    // download while it's in the queue, View in Library once it's there,
    // otherwise "Download · <size>" (every version and interface agree).
    const CollectionState now = game_state(game, state.jobs, state.library_games());
    const bool sized = !game.releases.empty() && game.releases.front().size_bytes > 0.0;
    if (now.job != nullptr)
        items.push_back(xb::action(icons::Icon::down, tr("View download")));
    else if (now.kind == CollectionState::Kind::library && now.library != nullptr)
        items.push_back(xb::action(icons::Icon::books, tr("View in Library")));
    else
        items.push_back(xb::action(icons::Icon::down, sized ? std::string(tr("Download")) + " \xC2\xB7 " + format_bytes(game.releases.front().size_bytes)
                                                            : std::string(tr("Download"))));
    items.push_back(xb::action(icons::Icon::eye, tr("View game")));
    items.push_back(xb::action(state.favourite(game.id) ? icons::Icon::heart_fill : icons::Icon::heart,
                               state.favourite(game.id) ? tr("Remove from favourites") : tr("Add to favourites"),
                               game.addon_id.empty() && state.paired && state.link == Link::online));
    items.push_back(xb::action(icons::Icon::info, tr("About this game")));
    return items;
}

Intent xb_game_action(Context &ctx, const Game &game, int chosen)
{
    Intent intent;
    const Snapshot &state = ctx.store.state();
    if (chosen == 2)
    {
        if (game.addon_id.empty() && !ctx.store.favourite_action().busy)
            ctx.store.set_favourite(game.id, !state.favourite(game.id));
    }
    else if (chosen == 0)
        intent = game_primary(state, game);
    else if (chosen >= 0)
    {
        // View game opens the game; About opens it on About.
        intent.kind = Intent::Kind::open_game;
        intent.id = game.id;
        if (chosen == 3)
            intent.detail = "about";
    }
    return intent;
}

std::vector<xb::MenuItem> xb_crossbar_actions(const Snapshot &state, const Game &game)
{
    std::vector<xb::MenuItem> items = xb_game_actions(state, game);
    items.erase(items.begin() + 1);
    return items;
}

Intent xb_crossbar_action(Context &ctx, const Game &game, int chosen)
{
    return xb_game_action(ctx, game, chosen >= 1 ? chosen + 1 : chosen);
}

Intent game_primary(const Snapshot &state, const Game &game)
{
    // Its download in Downloads, its copy in Library, or its page, where a
    // new download is set up.
    Intent intent;
    const CollectionState now = game_state(game, state.jobs, state.library_games());
    if (now.job != nullptr)
    {
        intent.kind = Intent::Kind::show_job;
        intent.id = now.job->id;
    }
    else if (now.kind == CollectionState::Kind::library && now.library != nullptr)
    {
        intent.kind = Intent::Kind::show_library;
        intent.id = now.library->title_id;
        intent.detail = now.library->source_key;
    }
    else
    {
        intent.kind = Intent::Kind::open_game;
        intent.id = game.id;
    }
    return intent;
}

xb::List DiscoverScreen::crossbar_list() const
{
    xb::List list;
    const std::vector<int> *added = nullptr, *latest = nullptr;
    for (std::size_t r = 0; r < rails_.size(); ++r)
    {
        if (rails_[r].grid)
            continue;
        (rails_[r].title == tr("New on Orbit") ? added : latest) = &rails_[r].games;
    }
    if (xb_folder_ != 0)
    {
        const std::vector<int> *games = xb_folder_ == 1 ? latest : &all_games_;
        if (games == nullptr)
            return list;
        const bool dated = xb_folder_ == 1;
        list.size = static_cast<int>(games->size());
        list.fill = [this, games, dated](int i, xb::Row &row)
        {
            const Snapshot &now = ctx_.store.state();
            const int at = (*games)[static_cast<std::size_t>(i)];
            const Game &game = now.games[static_cast<std::size_t>(at)];
            row.key = "g:" + game.id;
            xb_game_row(now, game, row, dated);
        };
        return list;
    }
    // The section: New on Orbit's games, then the two folders.
    const int new_count = added != nullptr ? static_cast<int>(added->size()) : 0;
    const bool has_latest = latest != nullptr && !latest->empty();
    list.size = new_count + (has_latest ? 1 : 0) + (all_games_.empty() ? 0 : 1);
    list.fill = [this, added, latest, new_count, has_latest](int i, xb::Row &row)
    {
        const Snapshot &now = ctx_.store.state();
        if (i < new_count)
        {
            const Game &game = now.games[static_cast<std::size_t>((*added)[static_cast<std::size_t>(i)])];
            row.key = "g:" + game.id;
            xb_game_row(now, game, row, false);
            row.hint = tr("Actions");
            return;
        }
        row.has_icon = true;
        row.icon = icons::Icon::folder;
        row.folder = true;
        if (has_latest && i == new_count)
        {
            row.key = "f:latest";
            row.title = tr("Latest releases");
            row.sub = trn(static_cast<long>(latest->size()), "{count} game", "{count} games") + " \xC2\xB7 " +
                      std::string(tr("Newest first"));
            return;
        }
        row.key = "f:all";
        row.title = tr("All games");
        row.sub = trn(static_cast<long>(all_games_.size()), "{count} game", "{count} games");
    };
    return list;
}

std::vector<xb::MenuItem> DiscoverScreen::crossbar_actions(const Game &game) const
{
    return xb_crossbar_actions(ctx_.store.state(), game);
}

Intent DiscoverScreen::update_crossbar(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    Intent intent;
    if (catalog_serial_ != ctx_.store.state().catalog_serial)
        rebuild();
    const xb::List list = crossbar_list();
    xb_.clamp(list);
    const Snapshot &state = ctx_.store.state();
    xb::Row row;
    if (list.size > 0)
        list.fill(xb_.index, row);
    const Game *game = row.game;
    if (focused && xb_menu_.open && game != nullptr)
    {
        const int chosen = xb_menu_.update(input, crossbar_actions(*game), feedback);
        if (chosen >= 0)
            intent = xb_crossbar_action(ctx_, *game, chosen);
    }
    else if (focused && list.size > 0)
    {
        if (input.nav != Direction::none)
            xb_.move(list, input, feedback);
        else if (input.is_pressed(Action::confirm))
        {
            if (game != nullptr)
            {
                feedback.play(hui::audio::Cue::select);
                intent.kind = Intent::Kind::open_game;
                intent.id = game->id;
            }
            else if (row.folder)
            {
                feedback.play(hui::audio::Cue::open);
                xb_folder_ = row.key == "f:latest" ? 1 : 2;
                xb_.index = 0;
                xb_.snap();
            }
        }
        else if (xb_folder_ != 0 && (input.is_pressed(Action::jump_prev) || input.is_pressed(Action::jump_next)))
        {
            // L2 and R2: a page in either folder.
            const bool next = input.is_pressed(Action::jump_next);
            const int to = std::clamp(xb_.index + (next ? 5 : -5), 0, list.size - 1);
            if (to != xb_.index)
            {
                xb_.index = to;
                feedback.play(hui::audio::Cue::focus);
            }
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::back) && xb_folder_ != 0)
        {
            feedback.play(hui::audio::Cue::back);
            const std::string folder = xb_folder_ == 1 ? "f:latest" : "f:all";
            xb_folder_ = 0;
            xb_.focus_key(crossbar_list(), folder);
        }
        else if (game != nullptr && input.is_pressed(Action::menu))
        {
            xb_menu_.show(0);
            feedback.play(hui::audio::Cue::modal_open);
        }
        else if (game != nullptr && input.is_pressed(Action::north))
        {
            if (state.paired && state.link == Link::online && !ctx_.store.favourite_action().busy)
            {
                ctx_.store.set_favourite(game->id, !state.favourite(game->id));
                feedback.play(hui::audio::Cue::toggle);
            }
            else
                refuse(feedback, input);
        }
    }
    xb_.animate(dt);
    xb_menu_.animate(dt);
    if (game != nullptr)
        featured_ = game->id;
    return intent;
}

void DiscoverScreen::draw_crossbar(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const Snapshot &state = ctx_.store.state();
    std::vector<xb::BarItem> bar = xb::sections(ctx_, false);
    int current = section::discover;
    if (xb_folder_ != 0)
    {
        xb::BarItem folder;
        folder.icon = icons::Icon::folder;
        folder.label = xb_folder_ == 1 ? tr("Latest releases") : tr("All games");
        bar = {bar[section::discover], folder};
        current = 1;
    }
    xb::draw_bar(ctx_, list, bar, current);
    const xb::List rows = crossbar_list();
    xb_.draw(ctx_, list, rows);
    const Game *game = state.game(featured_);
    if (game != nullptr && xb_menu_.in.value < 0.99f)
    {
        list.push_opacity(1.0f - xb_menu_.in.value);
        xb::draw_preview(ctx_, list, *game);
        list.pop_opacity();
    }
    if (game != nullptr)
        xb::draw_menu(ctx_, frame.overlay, game->title, crossbar_actions(*game), xb_menu_);
}

std::vector<hui::ui::Hint> DiscoverScreen::crossbar_hints() const
{
    if (xb_menu_.open)
        return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Close")}};
    const xb::List list = crossbar_list();
    xb::Row row;
    if (list.size > 0)
        list.fill(std::clamp(xb_.index, 0, list.size - 1), row);
    std::vector<hui::ui::Hint> hints;
    if (xb_folder_ != 0)
        return {{hui::ui::Button::cross, tr("Open")}, {hui::ui::Button::circle, tr("Back")},
                {hui::ui::Button::l2, tr("Page"), hui::ui::Button::r2}};
    if (row.game != nullptr)
    {
        hints.push_back({hui::ui::Button::cross, tr("Open game")});
        hints.push_back({hui::ui::Button::options, tr("Actions")});
        if (ctx_.store.state().paired)
            hints.push_back({hui::ui::Button::triangle, ctx_.store.state().favourite(row.game->id)
                                                             ? tr("Remove from favourites")
                                                             : tr("Favourite")});
    }
    else
        hints.push_back({hui::ui::Button::cross, tr("Open")});
    hints.push_back({hui::ui::Button::l1, tr("Categories"), hui::ui::Button::r1});
    return hints;
}

// ---------------------------------------------------------------- Cinema

namespace
{
// Genre, year (or release date) and platform.
std::string cinema_kicker(const Game &game, bool dated)
{
    const bool ps4 = !game.releases.empty() && game.releases.front().platform == "ps4";
    std::string kicker;
    if (!game.genre.empty())
        kicker = i18n::genre(game.genre);
    if (dated && !game.release_date.empty())
        kicker += (kicker.empty() ? "" : " \xC2\xB7 ") + tr("Released {date}", {{"date", format_date(game.release_date)}});
    else if (game.release_date.size() >= 4)
        kicker += (kicker.empty() ? "" : " \xC2\xB7 ") + game.release_date.substr(0, 4);
    return kicker + (kicker.empty() ? "" : " \xC2\xB7 ") + std::string(ps4 ? "PS4" : "PS5");
}
} // namespace

cn::Stage cinema_stage(const Game &game, bool dated, float y)
{
    cn::Stage stage;
    stage.kicker = cinema_kicker(game, dated);
    stage.title = game.title;
    stage.tag = game.tagline;
    stage.y = y;
    return stage;
}

cn::Hero cinema_hero(Context &ctx, const Game &game, bool dated, bool focus)
{
    // The stage: genre, year (or release date) and platform; the title and
    // its line; the download (or where it is), Details and the heart.
    const Snapshot &state = ctx.store.state();
    const CollectionState now = game_state(game, state.jobs, state.library_games());
    cn::Hero hero;
    hero.kicker = cinema_kicker(game, dated);
    hero.title = game.title;
    hero.tag = game.tagline;
    const double size = game.releases.empty() ? 0.0 : game.releases.front().size_bytes;
    cn::Button primary = now.job != nullptr ? cn::button(tr("View download"), icons::Icon::down, true)
                         : now.kind == CollectionState::Kind::library
                             ? cn::button(tr("View in Library"), icons::Icon::books, true)
                             : cn::button(size > 0.0 ? std::string(tr("Download")) + " \xC2\xB7 " + format_bytes(size) : std::string(tr("Download")),
                                          icons::Icon::down, true);
    primary.focus = focus;
    cn::Button heart;
    heart.icon = state.favourite(game.id) ? icons::Icon::heart_fill : icons::Icon::heart;
    heart.has_icon = true;
    hero.buttons = {primary, cn::button(tr("View game"), icons::Icon::eye), heart};
    return hero;
}

void cinema_strip(Context &ctx, hui::gfx::DrawList &out, const std::vector<int> &games, int at, std::string label, const std::vector<Game> *catalogue)
{
    // A window of frames round the focused game, each with its state.
    const Snapshot &state = ctx.store.state();
    std::vector<cn::Still> stills;
    const int first = at < 0 ? 0 : std::max(0, at - 4);
    const int last = std::min(static_cast<int>(games.size()) - 1, (at < 0 ? 0 : at) + 8);
    for (int i = first; i <= last; ++i)
    {
        const Game &g = (catalogue ? *catalogue : state.games)[static_cast<std::size_t>(games[static_cast<std::size_t>(i)])];
        cn::Still still;
        still.game = &g;
        const CollectionState gs = game_state(g, state.jobs, state.library_games());
        if (gs.label != nullptr && gs.label[0] != '\0')
            still.badge = tr(gs.label);
        stills.push_back(std::move(still));
    }
    cn::Strip strip;
    strip.label = !label.empty() || at < 0
                      ? std::move(label)
                      : tr("{n} of {count}", {{"n", i18n::count(at + 1)}, {"count", i18n::count(static_cast<long>(games.size()))}});
    if (at < 0)
    {
        strip.x = 100.0f;
        strip.y = 460.0f;
    }
    cn::draw_strip(ctx, out, stills, at < 0 ? -1 : at - first, strip);
}

namespace
{
int shelf_lines(const CinemaShelf &shelf)
{
    const int n = static_cast<int>(shelf.games->size());
    return shelf.grid ? std::max(1, (n + cn::kRowColumns - 1) / cn::kRowColumns) : 1;
}
float shelf_height(const CinemaShelf &shelf)
{
    const int lines = shelf_lines(shelf);
    return cn::kRowLabel + static_cast<float>(lines) * cn::kRowFrameH + static_cast<float>(lines - 1) * cn::kRowLineGap +
           cn::kRowSpace;
}
cn::Still cinema_still(const Snapshot &state, const Game &game)
{
    cn::Still still;
    still.game = &game;
    const CollectionState gs = game_state(game, state.jobs, state.library_games());
    if (gs.label != nullptr && gs.label[0] != '\0')
        still.badge = tr(gs.label);
    return still;
}
} // namespace

bool cinema_rows_move(std::vector<CinemaShelf> &shelves, int &shelf, Direction direction)
{
    if (shelf < 0 || shelf >= static_cast<int>(shelves.size()))
        return false;
    constexpr int kCols = cn::kRowColumns;
    CinemaShelf &here = shelves[static_cast<std::size_t>(shelf)];
    const int n = static_cast<int>(here.games->size());
    if (direction == Direction::left || direction == Direction::right)
    {
        // Along the line: a strip to its ends, a grid's line to its edges.
        const int step = direction == Direction::right ? 1 : -1;
        const int to = here.at + step;
        if (to < 0 || to >= n || (here.grid && to / kCols != here.at / kCols))
            return false;
        here.at = to;
        if (!here.grid)
            here.start = std::clamp(here.start, std::max(0, to - (kCols - 1)), to);
        return true;
    }
    if (direction != Direction::up && direction != Direction::down)
        return false;
    // Up and down keep the column the frame is in on screen.
    const int column = here.grid ? here.at % kCols : here.at - here.start;
    if (here.grid)
    {
        const int line = here.at / kCols + (direction == Direction::down ? 1 : -1);
        if (line >= 0 && line < shelf_lines(here))
        {
            here.at = std::min(n - 1, line * kCols + column);
            return true;
        }
    }
    const int to = shelf + (direction == Direction::down ? 1 : -1);
    if (to < 0 || to >= static_cast<int>(shelves.size()))
        return false;
    CinemaShelf &next = shelves[static_cast<std::size_t>(to)];
    const int count = static_cast<int>(next.games->size());
    if (next.grid)
        next.at = std::min(count - 1, (direction == Direction::down ? 0 : (shelf_lines(next) - 1) * kCols) + column);
    else
        next.at = std::min(count - 1, next.start + column);
    shelf = to;
    return true;
}

float cinema_rows_offset(const std::vector<CinemaShelf> &shelves, int shelf, float area)
{
    float offset = 0.0f, total = 0.0f;
    for (int i = 0; i < static_cast<int>(shelves.size()); ++i)
    {
        const CinemaShelf &s = shelves[static_cast<std::size_t>(i)];
        if (i < shelf)
            offset += shelf_height(s);
        else if (i == shelf && s.grid && s.at / cn::kRowColumns > 0)
            // A grid's later line at the top, its ring clear of the edge.
            offset += cn::kRowLabel + static_cast<float>(s.at / cn::kRowColumns) * (cn::kRowFrameH + cn::kRowLineGap) - 12.0f;
        total += shelf_height(s);
    }
    return std::clamp(offset, 0.0f, std::max(0.0f, total - area));
}

void draw_cinema_rows(Context &ctx, hui::gfx::DrawList &out, const std::vector<CinemaShelf> &shelves, int shelf, float top,
                      float bottom, float offset, bool page_focused)
{
    const Snapshot &state = ctx.store.state();
    constexpr int kCols = cn::kRowColumns;
    out.push_clip({0.0f, top, 1920.0f, bottom - top});
    float y = top - offset;
    for (int i = 0; i < static_cast<int>(shelves.size()) && y < bottom; ++i)
    {
        const CinemaShelf &s = shelves[static_cast<std::size_t>(i)];
        const float h = shelf_height(s);
        if (y + h < top)
        {
            y += h;
            continue;
        }
        // "NEW ON ORBIT · 3 OF 20" on the focused row, its count on the others.
        const long count = static_cast<long>(s.games->size());
        cn::draw_row_label(ctx, out,
                           s.label + " \xC2\xB7 " +
                               (i == shelf ? tr("{n} of {count}", {{"n", i18n::count(s.at + 1)}, {"count", i18n::count(count)}})
                                           : i18n::count(count)),
                           y);
        const float frames_y = y + cn::kRowLabel;
        const float pitch = cn::kRowFrameH + cn::kRowLineGap;
        const int first = s.grid ? std::max(0, static_cast<int>((top - frames_y) / pitch)) : 0;
        const int last = s.grid ? std::min(shelf_lines(s) - 1, static_cast<int>((bottom - frames_y) / pitch)) : 0;
        for (int line = first; line <= last; ++line)
        {
            // A grid's line, or the strip from its first frame on screen.
            const int from = s.grid ? line * kCols : s.start;
            const int to = s.grid ? std::min<int>(static_cast<int>(count), from + kCols)
                                  : std::min<int>(static_cast<int>(count), from + kCols + 1);
            std::vector<cn::Still> stills;
            for (int g = from; g < to; ++g)
                stills.push_back(cinema_still(state, (s.catalogue ? *s.catalogue : state.games)[static_cast<std::size_t>((*s.games)[static_cast<std::size_t>(g)])]));
            const int focused = page_focused && i == shelf && s.at >= from && s.at < to ? s.at - from : -1;
            cn::draw_row_frames(ctx, out, stills, focused, frames_y + static_cast<float>(line) * pitch);
        }
        y += h;
    }
    out.pop_clip();
}

namespace
{
constexpr const char *kCinemaCollections[] = {/* i18n */ "New on Orbit", /* i18n */ "Latest releases", /* i18n */ "All games"};
// Where the rows sit under the stage, and where they stop above the hints.
constexpr float kStageY = 224.0f;
constexpr float kRowsBottom = 1000.0f;
float rows_top(float stage_y)
{
    return stage_y + cn::stage_height(cn::Stage{}.size) + 24.0f;
}
} // namespace

const std::vector<int> *DiscoverScreen::cinema_games(int collection) const
{
    if (collection == 2)
        return &all_games_;
    for (const Rail &rail : rails_)
        if (!rail.grid && (rail.title == tr("New on Orbit")) == (collection == 0))
            return &rail.games;
    return nullptr;
}

std::vector<CinemaShelf> DiscoverScreen::cinema_shelves(std::vector<int> *ids) const
{
    std::vector<CinemaShelf> shelves;
    for (int c = 0; c < 3; ++c)
    {
        const std::vector<int> *games = cinema_games(c);
        if (games == nullptr || games->empty())
            continue;
        CinemaShelf shelf;
        shelf.label = tr(kCinemaCollections[c]);
        shelf.games = games;
        shelf.grid = c == 2;
        shelf.at = std::clamp(cn_at_[c], 0, static_cast<int>(games->size()) - 1);
        shelf.start = std::clamp(cn_start_[c], 0, shelf.at);
        shelves.push_back(std::move(shelf));
        if (ids != nullptr)
            ids->push_back(c);
    }
    return shelves;
}

int DiscoverScreen::cinema_shelf(const std::vector<int> &ids) const
{
    const auto it = std::find(ids.begin(), ids.end(), cn_collection_);
    return it == ids.end() ? 0 : static_cast<int>(it - ids.begin());
}

const Game *DiscoverScreen::cinema_game() const
{
    std::vector<int> ids;
    const std::vector<CinemaShelf> shelves = cinema_shelves(&ids);
    if (shelves.empty())
        return nullptr;
    const CinemaShelf &shelf = shelves[static_cast<std::size_t>(cinema_shelf(ids))];
    return &ctx_.store.state().games[static_cast<std::size_t>((*shelf.games)[static_cast<std::size_t>(shelf.at)])];
}

bool DiscoverScreen::page(int)
{
    // The rows are all on the page: L1 and R1 go straight to the next chapter.
    return false;
}

Intent DiscoverScreen::update_cinema(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    cn_focused_ = focused;
    Intent intent;
    if (catalog_serial_ != ctx_.store.state().catalog_serial)
        rebuild();
    const Snapshot &state = ctx_.store.state();
    std::vector<int> ids;
    std::vector<CinemaShelf> shelves = cinema_shelves(&ids);
    int shelf = cinema_shelf(ids);
    const Game *game = cinema_game();
    if (focused && cn_actions_ && game != nullptr)
    {
        // The sheet: four actions side by side.
        const int actions = static_cast<int>(xb_game_actions(state, *game).size());
        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            const int to = cn_action_ + (input.nav == Direction::right ? 1 : -1);
            if (to >= 0 && to < actions)
            {
                cn_action_ = to;
                feedback.play(hui::audio::Cue::focus);
            }
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::back))
        {
            cn_actions_ = false;
            feedback.play(hui::audio::Cue::modal_close);
        }
        else if (input.is_pressed(Action::confirm))
        {
            cn_actions_ = false;
            intent = xb_game_action(ctx_, *game, cn_action_);
        }
    }
    else if (focused && game != nullptr)
    {
        if (input.nav != Direction::none)
        {
            // Along a row, or to the row above or below.
            if (cinema_rows_move(shelves, shelf, input.nav))
                feedback.play(hui::audio::Cue::focus);
            else if (input.nav == Direction::up)
                intent.kind = Intent::Kind::top_bar;
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::confirm))
        {
            // The focus is on the frame: Cross opens its game.
            feedback.play(hui::audio::Cue::select);
            intent.kind = Intent::Kind::open_game;
            intent.id = game->id;
        }
        else if (input.is_pressed(Action::north))
        {
            if (state.paired && state.link == Link::online && !ctx_.store.favourite_action().busy)
            {
                ctx_.store.set_favourite(game->id, !state.favourite(game->id));
                feedback.play(hui::audio::Cue::toggle);
            }
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::menu))
        {
            cn_actions_ = true;
            cn_action_ = 0;
            feedback.play(hui::audio::Cue::modal_open);
        }
        else if (input.is_pressed(Action::jump_prev) || input.is_pressed(Action::jump_next))
        {
            // L2 and R2: five frames along a strip, a line in the grid.
            CinemaShelf &here = shelves[static_cast<std::size_t>(shelf)];
            const int was = here.at;
            const int count = static_cast<int>(here.games->size());
            here.at = std::clamp(here.at + (input.is_pressed(Action::jump_next) ? 5 : -5), 0, std::max(0, count - 1));
            if (!here.grid)
                here.start = std::clamp(here.start, std::max(0, here.at - (cn::kRowColumns - 1)), here.at);
            if (here.at != was)
                feedback.play(hui::audio::Cue::focus);
            else
                refuse(feedback, input);
        }
        // Keep where every row is.
        for (std::size_t i = 0; i < shelves.size(); ++i)
        {
            cn_at_[ids[i]] = shelves[i].at;
            cn_start_[ids[i]] = shelves[i].start;
        }
        if (!ids.empty())
            cn_collection_ = ids[static_cast<std::size_t>(shelf)];
    }
    if (focused && game == nullptr && input.nav == Direction::up)
        intent.kind = Intent::Kind::top_bar;
    cn_offset_.target = cinema_rows_offset(shelves, shelf, kRowsBottom - rows_top(kStageY));
    cn_offset_.update(dt, 14.0f);
    cn_actions_in_.target = cn_actions_ ? 1.0f : 0.0f;
    cn_actions_in_.update(dt, 16.0f);
    if (const Game *now = cinema_game())
        featured_ = now->id;
    return intent;
}

void DiscoverScreen::draw_cinema(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const Snapshot &state = ctx_.store.state();
    const Game *game = cinema_game();
    cn::backdrop(ctx_, list, game, cn::Veil::hero);
    if (game == nullptr)
        return;
    std::vector<int> ids;
    const std::vector<CinemaShelf> shelves = cinema_shelves(&ids);
    // The stage names the focused game (Latest releases: its date); the rows
    // run under it, New on Orbit, Latest releases and All games.
    cn::draw_stage(ctx_, list, cinema_stage(*game, cn_collection_ == 1, kStageY));
    draw_cinema_rows(ctx_, list, shelves, cinema_shelf(ids), rows_top(kStageY), kRowsBottom, cn_offset_.value, cn_focused_ && !cn_actions_);
    if (cn_actions_in_.value > 0.01f)
    {
        // Options: everything for this game, as cards in a short sheet.
        hui::gfx::DrawList &over = frame.overlay;
        over.push_opacity(cn_actions_in_.value);
        const float top = cn::draw_sheet(over, 330.0f);
        cn::kick(ctx_, over, 100.0f, top + 56.0f, game->title);
        std::vector<cn::Card> cards;
        const std::vector<xb::MenuItem> actions = xb_game_actions(state, *game);
        const std::vector<std::string> notes = action_notes(state, *game);
        for (int i = 0; i < static_cast<int>(actions.size()); ++i)
        {
            cn::Card card;
            card.title = actions[static_cast<std::size_t>(i)].label;
            card.sub = static_cast<std::size_t>(i) < notes.size() ? notes[static_cast<std::size_t>(i)] : std::string();
            card.focus = i == cn_action_;
            card.enabled = actions[static_cast<std::size_t>(i)].enabled;
            cards.push_back(std::move(card));
        }
        cn::draw_cards(ctx_, &over, 100.0f, top + 56.0f + 25.6f + 18.0f, 1720.0f, cards, static_cast<int>(cards.size()));
        over.pop_opacity();
    }
}

std::vector<hui::ui::Hint> DiscoverScreen::cinema_hints() const
{
    if (cn_actions_)
        return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Close")}};
    const Snapshot &state = ctx_.store.state();
    const Game *game = cinema_game();
    if (game == nullptr)
        return {{hui::ui::Button::l1, tr("Tabs"), hui::ui::Button::r1}};
    std::vector<hui::ui::Hint> hints{{hui::ui::Button::cross, tr("Open game")}};
    if (state.paired)
        hints.push_back({hui::ui::Button::triangle, state.favourite(game->id) ? tr("Remove from favourites") : tr("Favourite")});
    hints.push_back({hui::ui::Button::l1, tr("Tabs"), hui::ui::Button::r1});
    return hints;
}

} // namespace orbit
