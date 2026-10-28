// Orbit Store TV app - Browse and Search in Crossbar: Filters and sort,
// Favourites as a folder, then the games; Search leads with the query.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/i18n.hpp"
#include "orbit/look.hpp"
#include "orbit/screens.hpp"

#include <algorithm>

namespace orbit
{

namespace
{

// A filter's name and icon, by filter_options() index; filter_order() has the
// design's order, then Reset filters.
constexpr const char *kLabels[] = {/* i18n */ "Source", /* i18n */ "Format", /* i18n */ "Region",
                                   /* i18n */ "Download size", /* i18n */ "Sort by", /* i18n */ "Platform",
                                   /* i18n */ "Installed games", /* i18n */ "Tags"};
constexpr icons::Icon kIcons[] = {icons::Icon::cloud, icons::Icon::archive, icons::Icon::globe, icons::Icon::drive,
                                  icons::Icon::sort,  icons::Icon::ps,      icons::Icon::books, icons::Icon::spark};

// How many games have an option matching, for the Platform and Source menus.
template <typename Match> long games_with(const std::vector<Game> &games, Match match)
{
    return static_cast<long>(std::count_if(games.begin(), games.end(), [&](const Game &game)
                                           { return std::any_of(game.releases.begin(), game.releases.end(), match); }));
}

} // namespace

std::string BrowseScreen::crossbar_summary() const
{
    // The Filters and sort row: the platform, source and order, or what is
    // applied and how many games match.
    const std::string sort = filters_.sort == Sort::release ? std::string(tr(addons_ ? "By game" : "Newest first")) : filter_value(4);
    if (!filters_.narrowed())
        return filter_value(5) + " \xC2\xB7 " + filter_value(0) + " \xC2\xB7 " + sort;
    std::string out;
    for (int filter : filter_order())
    {
        int selected = 0;
        const std::vector<std::string> options = filter_options(filter, &selected);
        if (filter != 4 && selected > 0 && selected < static_cast<int>(options.size()))
            out += (filter == 6 ? std::string(tr("Installed games hidden")) : filter_value(filter)) + " \xC2\xB7 ";
    }
    return out + sort + " \xC2\xB7 " + trn(static_cast<long>(results_.size()), addons_ && !grouped() ? "{count} add-on" : "{count} game", addons_ && !grouped() ? "{count} add-ons" : "{count} games");
}

xb::List BrowseScreen::crossbar_list() const
{
    xb::List list;
    const Snapshot &state = ctx_.store.state();
    // Search: the query first. Browse: Filters and sort, then Favourites (unless open).
    // Favourites with nothing saved: one row that says how to save one.
    if (filters_.favourites && !search_mode_ && state.favourites.empty())
    {
        list.size = 1;
        list.fill = [](int, xb::Row &row)
        {
            row.key = "no-favourites";
            row.has_icon = true;
            row.icon = icons::Icon::heart;
            row.title = tr("No favourites yet");
            row.sub = tr("Add favourites from a game\xE2\x80\x99s page.");
        };
        return list;
    }
    const bool query_row = search_mode_ || !filters_.query.empty();
    const bool filters_row = !search_mode_;
    const bool favourites_row = !addons_ && !search_mode_ && !filters_.favourites;
    const bool empty_row = results_.empty() && state.have_catalog;
    const bool reset_row = !search_mode_ && filters_.narrowed();
    const int lead = (query_row ? 1 : 0) + (filters_row ? 1 : 0) + (empty_row ? 1 : 0) + (reset_row ? 1 : 0) +
                     (favourites_row ? 1 : 0);
    list.size = lead + static_cast<int>(results_.size());
    list.fill = [this, query_row, filters_row, favourites_row, empty_row, reset_row, lead](int i, xb::Row &row)
    {
        const Snapshot &now = ctx_.store.state();
        int at = i;
        if (query_row && at-- == 0)
        {
            row.key = "q";
            row.has_icon = true;
            row.icon = icons::Icon::search;
            row.title = filters_.query.empty() ? std::string(tr("Search")) : "\xE2\x80\x9C" + filters_.query + "\xE2\x80\x9D";
            row.sub = trn(static_cast<long>(results_.size()), addons_ && !grouped() ? "{count} add-on" : "{count} game", addons_ && !grouped() ? "{count} add-ons" : "{count} games");
            if (filters_.query.empty())
                row.sub = tr(addons_ ? "Search add-ons or base games" : "Search games or title ID");
            return;
        }
        if (filters_row && at-- == 0)
        {
            row.key = "filters";
            row.has_icon = true;
            row.icon = icons::Icon::filter;
            row.title = tr("Filters and sort");
            row.sub = crossbar_summary();
            row.text = std::string(tr(addons_ ? /* i18n */ "Platform, source, type, region, size and sort order." : /* i18n */ "Platform, source, format, region, size and sort order.")) + " " +
                       (addons_ && !grouped() ? trn(static_cast<long>(results_.size()), "{count} add-on", "{count} add-ons") : trn(static_cast<long>(results_.size()), "{count} game matches.", "{count} games match."));
            if (filters_.sort == Sort::size || filters_.sort == Sort::size_desc)
                row.text += " " + std::string(tr("Sorted by the smallest download option that matches your filters."));
            return;
        }
        if (empty_row && at-- == 0)
        {
            row.key = "empty";
            row.has_icon = true;
            row.icon = icons::Icon::search;
            row.title = tr(addons_ ? "No add-ons match these filters." : "No games found");
            row.sub = filters_.favourites ? tr("Add favourites from a game\xE2\x80\x99s page.")
                                          : tr("Try another name, title ID or filter.");
            return;
        }
        if (reset_row && at-- == 0)
        {
            row.key = "reset";
            row.has_icon = true;
            row.icon = icons::Icon::refresh;
            row.title = tr("Reset filters");
            row.sub = tr(addons_ ? /* i18n */ "All add-ons" : /* i18n */ "Show every game again");
            return;
        }
        if (favourites_row && at-- == 0)
        {
            row.key = "favourites";
            row.has_icon = true;
            row.icon = icons::Icon::heart;
            row.folder = true;
            row.title = tr("Favourites");
            row.sub = trn(static_cast<long>(now.favourites.size()), addons_ ? "{count} add-on" : "{count} game", addons_ ? "{count} add-ons" : "{count} games");
            return;
        }
        const int game = results_[static_cast<std::size_t>(i - lead)];
        const Game &g = catalogue_games()[static_cast<std::size_t>(game)];
        row.key = "g:" + g.id;
        xb_game_row(now, g, row, false);
        if (!g.addon_id.empty() && !g.addon_type.empty()) row.sub += " \xC2\xB7 " + addon_type_label(g.addon_type);
    };
    return list;
}

std::string BrowseScreen::saved_summary(const BrowseFilters &f) const
{
    std::vector<std::string> parts;
    if (!f.query.empty())
        parts.push_back("\xE2\x80\x9C" + f.query + "\xE2\x80\x9D");
    if (f.favourites)
        parts.push_back(tr("Favourites"));
    if (!f.platform.empty())
        parts.push_back(f.platform == "ps4" ? "PS4" : "PS5");
    const auto sources = source_choices(catalogue_games());
    for (const std::string &id : f.sources)
    {
        const auto at = std::find_if(sources.begin(), sources.end(), [&](const auto &s) { return s.first == id; });
        parts.push_back(at != sources.end() ? at->second : id);
    }
    parts.insert(parts.end(), f.formats.begin(), f.formats.end());
    for (const std::string &tag : f.tags)
        parts.push_back(tag_label(tag) != nullptr ? std::string(tr(tag_label(tag))) : tag);
    if (!f.addon_type.empty())
        parts.push_back(addon_type_label(f.addon_type));
    if (f.hide_installed)
        parts.push_back(tr("Hide installed"));
    if (!f.region.empty())
        parts.push_back(f.region == "unknown" ? std::string(tr("Unknown region")) : f.region);
    if (f.size != SizeFilter::any)
        parts.push_back(size_label(f.size));
    if (f.sort != Sort::release)
        parts.push_back(sort_label(f.sort));
    std::string out;
    for (const std::string &part : parts)
        out += (out.empty() ? "" : " \xC2\xB7 ") + part;
    return out.empty() ? std::string(tr("No filters")) : out;
}

std::vector<xb::MenuItem> BrowseScreen::crossbar_filters() const
{
    std::vector<xb::MenuItem> items;
    for (int filter : filter_order())
    {
        xb::MenuItem item = xb::action(kIcons[filter], tr(addons_ && filter == 1 ? "Type" : kLabels[filter]));
        item.value = filter == 4 && filters_.sort == Sort::release ? std::string(tr(addons_ ? "By game" : "Newest first")) : filter_value(filter);
        items.push_back(item);
    }
    items.push_back(xb::action(icons::Icon::refresh, tr("Reset filters"),
                               filters_.narrowed() || filters_.favourites || !filters_.query.empty()));
    xb::MenuItem saved = xb::action(icons::Icon::star, tr("Saved filters"), ctx_.store.state().paired);
    saved.value = filter_value(kSavedFilters);
    items.push_back(saved);
    return items;
}

std::vector<xb::MenuItem> BrowseScreen::crossbar_values(int filter) const
{
    if (filter == kSavedFilters)
    {
        // Save these filters (what is set now), then each saved one with what it keeps.
        std::vector<xb::MenuItem> items;
        const bool set = resettable();
        items.push_back(xb::action(icons::Icon::star, tr("Save these filters"), set && !ctx_.store.saved_filters_action().busy));
        items.back().sub = set ? saved_summary(filters_) : std::string(tr("Choose some filters or a search first."));
        for (const SavedFilter *saved : saved_here())
            items.push_back(xb::choice(saved->name, false, saved_summary(saved->filters)));
        return items;
    }
    int selected = 0;
    std::vector<bool> checked;
    const std::vector<std::string> options = filter_options(filter, &selected, &checked);
    const std::vector<Game> &games = catalogue_games();
    const auto sources = source_choices(games);
    std::vector<xb::MenuItem> items;
    for (int i = 0; i < static_cast<int>(options.size()); ++i)
    {
        // Platform and Source say how many games each one has.
        std::string sub;
        if (i > 0 && filter == 5)
        {
            const std::string platform = i == 1 ? "ps5" : "ps4";
            sub = trn(games_with(games, [&](const Release &r) { return r.platform == platform; }), addons_ && !grouped() ? "{count} add-on" : "{count} game", addons_ && !grouped() ? "{count} add-ons" : "{count} games");
        }
        else if (i > 0 && filter == 0 && i <= static_cast<int>(sources.size()))
        {
            const std::string &id = sources[static_cast<std::size_t>(i - 1)].first;
            sub = trn(games_with(games, [&](const Release &r) { return r.source_id == id; }), addons_ && !grouped() ? "{count} add-on" : "{count} game", addons_ && !grouped() ? "{count} add-ons" : "{count} games");
        }
        // Source and Format tick several: boxes rather than radios.
        items.push_back(xb::choice(options[static_cast<std::size_t>(i)],
                                   multiple(filter) ? i < static_cast<int>(checked.size()) && checked[static_cast<std::size_t>(i)] : i == selected,
                                   sub));
        items.back().box = multiple(filter);
    }
    return items;
}

Intent BrowseScreen::update_crossbar(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    Intent intent;
    const Snapshot &state = ctx_.store.state();
    if (catalog_serial_ != state.catalog_serial ||
        ((filters_.favourites || filters_.hide_installed) && favourites_serial_ != state.serial))
        refilter();
    const xb::List list = crossbar_list();
    xb_.clamp(list);
    xb::Row row;
    if (list.size > 0)
        list.fill(xb_.index, row);
    if (focused && keyboard_open_ && naming_)
        update_keyboard(input, feedback);
    else if (focused && keyboard_open_)
    {
        update_keyboard(input, feedback);
        if (!keyboard_open_)
        {
            // Done goes to the first result; Close stays on the query.
            const int first = (search_mode_ || !filters_.query.empty() ? 1 : 0) + (search_mode_ ? 0 : 1) +
                              (!addons_ && !search_mode_ && !filters_.favourites ? 1 : 0);
            xb_.index = area_ == Area::grid && !results_.empty() ? first : 0;
        }
    }
    else if (focused && xb_values_.open && open_filter_ == kSavedFilters && input.is_pressed(Action::west))
    {
        if (delete_saved(xb_values_.active, feedback))
            xb_values_.active = std::max(0, std::min(xb_values_.active, static_cast<int>(saved_here().size()) - 1));
        else
            refuse(feedback, input);
    }
    else if (focused && xb_values_.open)
    {
        const int filter = open_filter_;
        const int chosen = xb_values_.update(input, crossbar_values(filter), feedback);
        if (chosen >= 0)
            choose(filter, chosen);
        if (chosen >= 0 && multiple(filter))
            xb_values_.show(chosen); // open for the next tick; Circle goes back
        else if (chosen >= 0 && filter == kSavedFilters)
        {
            // Applied: the results, from the top. Save these filters: the keys, alone.
            xb_menu_.open = false;
            xb_values_.open = false;
            if (chosen > 0)
            {
                xb_.index = 0;
                xb_.snap();
            }
        }
        else if (chosen != -1)
            xb_menu_.show(xb_filter_); // back to the filters
    }
    else if (focused && xb_menu_.open)
    {
        if (xb_filters_)
        {
            const std::vector<xb::MenuItem> items = crossbar_filters();
            const std::vector<int> order = filter_order();
            const int filters = static_cast<int>(order.size());
            const int chosen = xb_menu_.update(input, items, feedback);
            if (chosen >= 0 && chosen < filters)
            {
                xb_filter_ = chosen;
                open_filter_ = order[static_cast<std::size_t>(chosen)];
                int selected = 0;
                filter_options(open_filter_, &selected);
                xb_values_.show(selected);
            }
            else if (chosen == filters)
                reset_crossbar(feedback);
            else if (chosen == filters + 1)
            {
                xb_filter_ = chosen;
                open_filter_ = kSavedFilters;
                xb_values_.show(0);
            }
        }
        else if (row.game != nullptr)
        {
            const int chosen = xb_menu_.update(input, xb_crossbar_actions(state, *row.game), feedback);
            if (chosen >= 0)
                intent = xb_crossbar_action(ctx_, *row.game, chosen);
        }
        else
            xb_menu_.open = false;
    }
    else if (focused)
    {
        if (input.nav != Direction::none)
            xb_.move(list, input, feedback);
        else if (input.is_pressed(Action::confirm))
        {
            if (row.game != nullptr)
            {
                feedback.play(hui::audio::Cue::select);
                intent.kind = Intent::Kind::open_game;
                intent.id = row.game->id;
            }
            else if (row.key == "filters")
            {
                xb_filters_ = true;
                xb_menu_.show(0);
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (row.key == "favourites")
            {
                if (!state.paired)
                    refuse(feedback, input);
                else
                {
                    filters_.favourites = true;
                    index_ = 0;
                    refilter();
                    xb_.index = 0;
                    xb_.snap();
                    feedback.play(hui::audio::Cue::open);
                }
            }
            else if (row.key == "q")
            {
                feedback.play(hui::audio::Cue::modal_open);
                start_search();
            }
            else if (row.key == "reset" || row.key == "empty")
                reset_crossbar(feedback);
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::west))
        {
            feedback.play(hui::audio::Cue::modal_open);
            start_search();
        }
        else if (input.is_pressed(Action::back) && filters_.favourites && !search_mode_)
        {
            filters_.favourites = false;
            index_ = 0;
            refilter();
            xb_.focus_key(crossbar_list(), "favourites");
            feedback.play(hui::audio::Cue::back);
        }
        else if (row.game != nullptr && input.is_pressed(Action::menu))
        {
            xb_filters_ = false;
            xb_menu_.show(0);
            feedback.play(hui::audio::Cue::modal_open);
        }
        else if (row.game != nullptr && input.is_pressed(Action::north))
        {
            if (!addons_ && state.paired && state.link == Link::online && !ctx_.store.favourite_action().busy)
            {
                ctx_.store.set_favourite(row.game->id, !state.favourite(row.game->id));
                feedback.play(hui::audio::Cue::toggle);
            }
            else
                refuse(feedback, input);
        }
    }
    // The keyboard sits on the right, where the preview is, under its field.
    keyboard_.set_bounds(xb::keyboard_keys());
    keyboard_.set_active(keyboard_open_);
    keyboard_.update(dt);
    keyboard_in_.target = keyboard_open_ ? 1.0f : 0.0f;
    keyboard_in_.update(dt, 14.0f);
    xb_.animate(dt);
    xb_menu_.animate(dt);
    xb_values_.animate(dt);
    return intent;
}

void BrowseScreen::reset_crossbar(Feedback &feedback)
{
    // As Store's Reset filters: everything, the search and Favourites too.
    filters_ = BrowseFilters{};
    index_ = 0;
    refilter();
    xb_.index = 0;
    xb_.snap();
    feedback.play(hui::audio::Cue::select);
}

void BrowseScreen::draw_crossbar(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const Snapshot &state = ctx_.store.state();
    std::vector<xb::BarItem> bar = xb::sections(ctx_, false);
    int current = addons_ ? section::addons : apps_ ? section::apps : search_mode_ ? section::search : section::browse;
    if (filters_.favourites && !search_mode_)
    {
        xb::BarItem folder;
        folder.icon = icons::Icon::heart;
        folder.label = tr("Favourites");
        bar = {bar[section::browse], folder};
        current = 1;
    }
    xb::draw_bar(ctx_, list, bar, current);
    const xb::List rows = crossbar_list();
    xb_.draw(ctx_, list, rows);
    xb::Row row;
    if (rows.size > 0)
        rows.fill(std::clamp(xb_.index, 0, rows.size - 1), row);
    const float hide = std::max({xb_menu_.in.value, xb_values_.in.value, keyboard_in_.value});
    if (row.game != nullptr && hide < 0.99f)
    {
        list.push_opacity(1.0f - hide);
        xb::draw_preview(ctx_, list, *row.game);
        list.pop_opacity();
    }
    if (xb_filters_)
    {
        // A filter's values open beside the filters, which step left to make room.
        const float beside = xb_values_.in.value;
        xb::Menu filters = xb_menu_;
        filters.in.value = std::max(xb_menu_.in.value, beside);
        if (xb_values_.open)
            filters.active = xb_filter_;
        xb::draw_menu(ctx_, frame.overlay, tr("Filters and sort"), crossbar_filters(), filters,
                      {.x = 1110.0f - 410.0f * beside, .w = 560.0f - 40.0f * beside});
        if (beside > 0.01f && open_filter_ >= 0)
        {
            const int filter = open_filter_;
            xb::draw_menu(ctx_, frame.overlay, filter == kSavedFilters ? tr("Saved filters") : tr(addons_ && filter == 1 ? "Type" : kLabels[filter]), crossbar_values(filter), xb_values_,
                          {.x = 1250.0f, .w = 520.0f});
        }
    }
    else if (row.game != nullptr)
        xb::draw_menu(ctx_, frame.overlay, row.game->title, xb_crossbar_actions(state, *row.game), xb_menu_);
    if (naming_)
        xb::draw_keyboard(ctx_, frame, keyboard_, tr("Name these filters"), saved_name_, false, keyboard_in_.value);
    else
        xb::draw_keyboard(ctx_, frame, keyboard_, tr("Search"), filters_.query, false, keyboard_in_.value);
}

std::vector<hui::ui::Hint> BrowseScreen::crossbar_hints() const
{
    if (keyboard_open_)
        return {{hui::ui::Button::square, tr("Delete")},
                {hui::ui::Button::triangle, tr("Space")},
                {hui::ui::Button::circle, tr("Close")}};
    if (xb_values_.open && open_filter_ == kSavedFilters && xb_values_.active > 0)
        return {{hui::ui::Button::cross, tr("Apply")}, {hui::ui::Button::square, tr("Delete")}, {hui::ui::Button::circle, tr("Back")}};
    if (xb_menu_.open || xb_values_.open)
        return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, xb_values_.open ? tr("Back") : tr("Close")}};
    const xb::List rows = crossbar_list();
    xb::Row row;
    if (rows.size > 0)
        rows.fill(std::clamp(xb_.index, 0, rows.size - 1), row);
    std::vector<hui::ui::Hint> hints;
    if (row.key == "no-favourites")
        return {{hui::ui::Button::circle, tr("Back")}};
    if (row.key == "reset" || row.key == "empty")
        hints.push_back({hui::ui::Button::cross, tr("Reset filters")});
    else if (row.game != nullptr)
    {
        hints.push_back({hui::ui::Button::cross, tr("Open")});
        hints.push_back({hui::ui::Button::options, tr("Actions")});
        if (ctx_.store.state().paired)
            hints.push_back({hui::ui::Button::triangle, ctx_.store.state().favourite(row.game->id)
                                                             ? tr("Remove from favourites")
                                                             : tr("Favourite")});
    }
    else if (row.key == "q")
        hints.push_back({hui::ui::Button::cross, tr("Edit search")});
    else
        hints.push_back({hui::ui::Button::cross, row.key == "filters" ? tr("Change") : tr("Open")});
    if (row.game == nullptr && row.key != "reset" && row.key != "empty")
        hints.push_back({hui::ui::Button::square, tr("Search")});
    if (filters_.favourites && !search_mode_)
        hints.push_back({hui::ui::Button::circle, tr("Back")});
    hints.push_back({hui::ui::Button::l1, tr("Categories"), hui::ui::Button::r1});
    return hints;
}

// ---------------------------------------------------------------- Cinema

bool BrowseScreen::hints_high() const
{
    return (look::is(look::Interface::cinema) || look::is(look::Interface::panorama)) && keyboard_open_;
}

bool BrowseScreen::page(int step)
{
    // L1 and R1: All games and Favourites, then the next chapter.
    if (addons_ || !look::is(look::Interface::cinema) || search_mode_ || keyboard_open_ || cn_sheet_)
        return false;
    const bool favourites = step > 0;
    if (favourites == filters_.favourites || (favourites && !ctx_.store.state().paired))
        return false;
    filters_.favourites = favourites;
    index_ = 0;
    cn_at_ = 0;
    refilter();
    return true;
}

namespace
{
// The stage sits under the collection line and the filter line (higher in
// Search, which has no filters); the rows under it stop above the hints.
float cinema_stage_y(bool search)
{
    return search ? 200.0f : 250.0f;
}
float cinema_rows_top(bool search)
{
    return cinema_stage_y(search) + cn::stage_height(cn::Stage{}.size) + 24.0f;
}
constexpr float kRowsBottom = 1000.0f;
} // namespace

CinemaShelf BrowseScreen::cinema_shelf() const
{
    // The results as a grid, named as the collection line names them.
    CinemaShelf shelf;
    shelf.label = search_mode_ ? tr("Results") : filters_.favourites ? tr("Favourites") : filters_.narrowed() ? tr("Filtered") : tr(addons_ ? "All add-ons" : "All games");
    shelf.games = &results_;
    shelf.catalogue = &catalogue_games();
    shelf.grid = true;
    shelf.at = std::clamp(cn_at_, 0, std::max(0, static_cast<int>(results_.size()) - 1));
    return shelf;
}

Intent BrowseScreen::update_cinema(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    cn_focused_ = focused;
    Intent intent;
    const Snapshot &state = ctx_.store.state();
    cn::keyboard_look(keyboard_.style);
    keyboard_.set_bounds(cn::keyboard_keys());
    const int count = static_cast<int>(results_.size());
    cn_at_ = std::clamp(cn_at_, 0, std::max(0, count - 1));
    const auto reset = [&]
    {
        // As Store's Reset filters.
        filters_ = BrowseFilters{};
        index_ = 0;
        cn_at_ = 0;
        refilter();
        feedback.play(hui::audio::Cue::select);
    };
    if (focused && keyboard_open_ && naming_)
        update_keyboard(input, feedback);
    else if (focused && keyboard_open_)
    {
        update_keyboard(input, feedback);
        if (!keyboard_open_)
            cn_at_ = 0;
    }
    else if (focused && cn_values_ && open_filter_ == kSavedFilters && input.is_pressed(Action::west))
    {
        if (delete_saved(cn_value_, feedback))
            cn_value_ = std::max(0, std::min(cn_value_, static_cast<int>(saved_here().size()) - 1));
        else
            refuse(feedback, input);
    }
    else if (focused && cn_values_)
    {
        // A filter's values, beside the rows.
        const int filter = open_filter_;
        int selected = 0;
        const int options = static_cast<int>(filter_options(filter, &selected).size());
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int to = cn_value_ + (input.nav == Direction::down ? 1 : -1);
            if (to >= 0 && to < options)
            {
                cn_value_ = to;
                feedback.play(hui::audio::Cue::focus);
            }
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::confirm))
        {
            if (filter == kSavedFilters && cn_value_ == 0 && !crossbar_values(filter).front().enabled)
            {
                refuse(feedback, input);
                return intent;
            }
            choose(filter, cn_value_);
            // Source and Format stay open for the next tick; an applied saved filter shows its results.
            cn_values_ = multiple(filter);
            if (filter == kSavedFilters && cn_value_ > 0)
                cn_sheet_ = false;
            cn_at_ = 0;
            feedback.play(multiple(filter) ? hui::audio::Cue::toggle : hui::audio::Cue::select);
        }
        else if (input.is_pressed(Action::back) || input.nav == Direction::left)
        {
            cn_values_ = false;
            feedback.play(hui::audio::Cue::back);
        }
    }
    else if (focused && cn_sheet_)
    {
        // Filters and sort, reset, and search without a separate chapter.
        const std::vector<int> order = filter_order();
        const int filters = static_cast<int>(order.size());
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int to = cn_row_ + (input.nav == Direction::down ? 1 : -1);
            if (to >= 0 && to < filters + 3)
            {
                cn_row_ = to;
                feedback.play(hui::audio::Cue::focus);
            }
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::confirm) || (input.nav == Direction::right && (cn_row_ < filters || cn_row_ == filters + 1)))
        {
            if (cn_row_ < filters)
            {
                int selected = 0;
                open_filter_ = order[static_cast<std::size_t>(cn_row_)];
                filter_options(open_filter_, &selected);
                cn_value_ = selected;
                cn_values_ = true;
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (cn_row_ == filters + 1)
            {
                if (!state.paired)
                    refuse(feedback, input);
                else
                {
                    open_filter_ = kSavedFilters;
                    cn_value_ = 0;
                    cn_values_ = true;
                    feedback.play(hui::audio::Cue::modal_open);
                }
            }
            else if (cn_row_ == filters + 2)
            {
                cn_sheet_ = false;
                start_search();
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (filters_.narrowed() || filters_.favourites || !filters_.query.empty())
                reset();
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::back) || input.is_pressed(Action::west))
        {
            cn_sheet_ = false;
            feedback.play(hui::audio::Cue::modal_close);
        }
    }
    else if (focused)
    {
        const Game *game = count > 0 ? &catalogue_games()[static_cast<std::size_t>(results_[static_cast<std::size_t>(cn_at_)])] : nullptr;
        if (input.is_pressed(Action::west) || (game == nullptr && input.is_pressed(Action::confirm)))
        {
            // Square: the filters (Browse) or the keyboard (Search); with no
            // results, Cross does the same as the screen's button.
            if (search_mode_)
            {
                feedback.play(hui::audio::Cue::modal_open);
                start_search();
            }
            else if (game == nullptr && input.is_pressed(Action::confirm))
            {
                if (filters_.narrowed())
                    reset();
                else
                    refuse(feedback, input);
            }
            else
            {
                cn_sheet_ = true;
                cn_row_ = 0;
                feedback.play(hui::audio::Cue::modal_open);
            }
        }
        else if (game != nullptr && input.nav != Direction::none)
        {
            // Along a line of results, or to the line above or below.
            std::vector<CinemaShelf> shelves{cinema_shelf()};
            int shelf = 0;
            if (cinema_rows_move(shelves, shelf, input.nav))
            {
                cn_at_ = shelves.front().at;
                feedback.play(hui::audio::Cue::focus);
            }
            else if (input.nav == Direction::up)
                intent.kind = Intent::Kind::top_bar;
            else
                refuse(feedback, input);
        }
        else if (game == nullptr && input.nav == Direction::up)
            intent.kind = Intent::Kind::top_bar;
        else if (game != nullptr && input.is_pressed(Action::confirm))
        {
            feedback.play(hui::audio::Cue::select);
            intent.kind = Intent::Kind::open_game;
            intent.id = game->id;
        }
        else if (game != nullptr && input.is_pressed(Action::north))
        {
            if (!addons_ && state.paired && state.link == Link::online && !ctx_.store.favourite_action().busy)
            {
                ctx_.store.set_favourite(game->id, !state.favourite(game->id));
                feedback.play(hui::audio::Cue::toggle);
            }
            else
                refuse(feedback, input);
        }
    }
    keyboard_.set_active(keyboard_open_);
    keyboard_.update(dt);
    keyboard_in_.target = keyboard_open_ ? 1.0f : 0.0f;
    keyboard_in_.update(dt, 14.0f);
    cn_sheet_in_.target = cn_sheet_ ? 1.0f : 0.0f;
    cn_sheet_in_.update(dt, 16.0f);
    if (!results_.empty())
        cn_offset_.target = cinema_rows_offset({cinema_shelf()}, 0, kRowsBottom - cinema_rows_top(search_mode_));
    cn_offset_.update(dt, 14.0f);
    return intent;
}

void BrowseScreen::draw_cinema(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const Snapshot &state = ctx_.store.state();
    const ui::Type &type = ctx_.type;
    const int count = static_cast<int>(results_.size());
    const std::string games = trn(static_cast<long>(count), addons_ && !grouped() ? "{count} add-on" : "{count} game", addons_ && !grouped() ? "{count} add-ons" : "{count} games");
    if (naming_ && keyboard_open_)
    {
        // Naming the filters to save: the name written large over the keys.
        cn::backdrop(ctx_, list, nullptr, cn::Veil::dark);
        cn::draw_keyboard(ctx_, frame, keyboard_, tr("Name these filters"), saved_name_, tr("type a name"), false);
        return;
    }
    if (search_mode_ && keyboard_open_)
    {
        // Search: the query written large, the results strip under it, the keys below.
        cn::backdrop(ctx_, list, nullptr, cn::Veil::dark);
        if (count > 0 && !filters_.query.empty())
            cinema_strip(ctx_, list, results_, -1, tr("Results"), &catalogue_games());
        cn::draw_keyboard(ctx_, frame, keyboard_,
                          filters_.query.empty() ? std::string(tr(addons_ ? "Search add-ons or base games" : "Search games or title ID")) : std::string(tr("Search")) + " \xC2\xB7 " + games,
                          filters_.query, tr(addons_ ? "Search add-ons or base games" : "Search games or title ID"), false);
        return;
    }
    const Game *game = count > 0 ? &catalogue_games()[static_cast<std::size_t>(results_[static_cast<std::size_t>(std::clamp(cn_at_, 0, count - 1))])] : nullptr;
    cn::backdrop(ctx_, list, game, game != nullptr ? cn::Veil::hero : cn::Veil::dark);
    // The collection: the search, or All games / Favourites (filtered or not).
    std::string name;
    if (search_mode_)
        name = (filters_.query.empty() ? std::string(tr("Search")) : "\xE2\x80\x9C" + filters_.query + "\xE2\x80\x9D") + " \xC2\xB7 " + games;
    else if (filters_.favourites)
        name = std::string(tr("Favourites")) + " \xC2\xB7 " + i18n::count(count);
    else
        name = std::string(filters_.narrowed() ? tr("Filtered") : tr(addons_ ? "All add-ons" : "All games")) + " \xC2\xB7 " + i18n::count(count);
    cn::collection(ctx_, list, name, search_mode_ ? 0 : filters_.favourites ? 1 : 0, search_mode_ || addons_ ? 1 : 2);
    if (!search_mode_)
    {
        // The filter line: what's applied, and Reset filters beside it.
        const auto chip = [&](float x, icons::Icon icon, std::string_view text)
        {
            const float w = 18.0f + 20.0f + 10.0f + type.regular.measure(text, 19.0f) + 18.0f;
            list.rounded_rect({x, 178.0f, w, 48.4f}, 24.2f, ui::Color::rgb(0xf7f3ec, 0.1f));
            icons::draw(list, icon, x + 18.0f + 10.0f, 178.0f + 24.2f, 20.0f, cn::kInk, 1.8f);
            ui::text(list, type.regular, text, x + 18.0f + 20.0f + 10.0f, 178.0f + 24.2f + 19.0f * 0.35f, 19.0f, cn::kInk);
            return w;
        };
        const float w = chip(100.0f, icons::Icon::filter, crossbar_summary());
        if (filters_.narrowed())
            chip(100.0f + w + 12.0f, icons::Icon::refresh, tr("Reset filters"));
    }
    if (game == nullptr)
    {
        // Nothing to show: say why, and offer the way out.
        cn::Hero empty;
        empty.kicker = name;
        empty.size = 110.0f;
        empty.y = 380.0f;
        if (filters_.favourites && state.favourites.empty())
        {
            empty.title = tr("No favourites yet");
            empty.body = tr("Add favourites from a game\xE2\x80\x99s page.");
        }
        else
        {
            empty.title = tr(addons_ ? "No add-ons match these filters." : "No games found");
            empty.body = tr("Try another name, title ID or filter.");
            if (search_mode_)
                empty.buttons = {cn::button(tr("Edit search"), icons::Icon::search, true)};
            else if (filters_.narrowed())
                empty.buttons = {cn::button(tr("Reset filters"), icons::Icon::refresh, true)};
            for (cn::Button &b : empty.buttons)
                b.focus = true;
        }
        cn::draw_hero(ctx_, list, empty);
    }
    else
    {
        // The stage names the focused result; the results run under it in rows.
        list.push_opacity(1.0f - cn_sheet_in_.value);
        cn::draw_stage(ctx_, list, cinema_stage(*game, false, cinema_stage_y(search_mode_)));
        list.pop_opacity();
        draw_cinema_rows(ctx_, list, {cinema_shelf()}, 0, cinema_rows_top(search_mode_), kRowsBottom, cn_offset_.value, cn_focused_ && !cn_sheet_);
    }
    if (cn_sheet_in_.value > 0.01f)
    {
        // Filters and sort: the rows on the left, a filter's values on the right.
        hui::gfx::DrawList &over = frame.overlay;
        over.push_opacity(cn_sheet_in_.value);
        const float top = cn::draw_sheet(over, 850.0f) + 44.0f;
        cn::kick(ctx_, over, 100.0f, top, std::string(tr("Filters and sort")) + " \xC2\xB7 " + games);
        std::vector<cn::Row> rows;
        const std::vector<int> order = filter_order();
        for (int filter : order)
        {
            cn::Row row;
            row.icon = kIcons[filter];
            row.title = tr(addons_ && filter == 1 ? "Type" : kLabels[filter]);
            row.value = filter == 4 && filters_.sort == Sort::release ? std::string(tr(addons_ ? "By game" : "Newest first")) : filter_value(filter);
            rows.push_back(std::move(row));
        }
        cn::Row reset;
        reset.icon = icons::Icon::refresh;
        reset.title = tr("Reset filters");
        reset.off = !(filters_.narrowed() || filters_.favourites || !filters_.query.empty());
        rows.push_back(reset);
        cn::Row saved;
        saved.icon = icons::Icon::star;
        saved.title = tr("Saved filters");
        saved.value = filter_value(kSavedFilters);
        saved.off = !state.paired;
        rows.push_back(saved);
        cn::Row search;
        search.icon = icons::Icon::search;
        search.title = tr("Search");
        search.value = filters_.query;
        rows.push_back(search);
        // Tags and Installed games make the sheet longer than the screen: it
        // scrolls to keep the focused row above the hints, as Settings' rows do.
        const float rows_top = top + 25.6f + 10.0f;
        const std::vector<cn::Row> before(rows.begin(), rows.begin() + std::min<int>(cn_row_ + 1, static_cast<int>(rows.size())));
        const float shift = std::max(0.0f, cn::draw_rows(ctx_, nullptr, 100.0f, rows_top, 700.0f, before, -1) - 1000.0f);
        over.push_clip({0.0f, rows_top - 4.0f, 840.0f, 1004.0f - rows_top});
        cn::draw_rows(ctx_, &over, 100.0f, rows_top - shift, 700.0f, rows, cn_row_);
        over.pop_clip();
        if (cn_values_ && open_filter_ >= 0)
        {
            const int filter = open_filter_;
            const std::vector<xb::MenuItem> values = crossbar_values(filter);
            std::vector<cn::Card> cards;
            for (int i = 0; i < static_cast<int>(values.size()); ++i)
            {
                cn::Card card;
                card.title = values[static_cast<std::size_t>(i)].label;
                card.sub = values[static_cast<std::size_t>(i)].sub;
                card.chosen = values[static_cast<std::size_t>(i)].checked;
                card.focus = i == cn_value_;
                cards.push_back(std::move(card));
            }
            // A long list shows the part round the focus.
            const int first = std::clamp(cn_value_ - 3, 0, std::max(0, static_cast<int>(cards.size()) - 6));
            std::vector<cn::Card> shown(cards.begin() + first, cards.begin() + std::min<int>(static_cast<int>(cards.size()), first + 6));
            cn::kick(ctx_, over, 860.0f, top, filter == kSavedFilters ? tr("Saved filters") : tr(addons_ && filter == 1 ? "Type" : kLabels[filter]));
            cn::draw_cards(ctx_, &over, 860.0f, top + 25.6f + 18.0f, 960.0f, shown, 1);
        }
        over.pop_opacity();
    }
}

std::vector<hui::ui::Hint> BrowseScreen::cinema_hints() const
{
    if (keyboard_open_)
        return {{hui::ui::Button::square, tr("Delete")}, {hui::ui::Button::triangle, tr("Space")}, {hui::ui::Button::circle, tr("Close")}};
    if (cn_values_ && open_filter_ == kSavedFilters && cn_value_ > 0)
        return {{hui::ui::Button::cross, tr("Apply")}, {hui::ui::Button::square, tr("Delete")}, {hui::ui::Button::circle, tr("Back")}};
    if (cn_values_)
        return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Back")}};
    if (cn_sheet_)
        return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Close")}};
    const Snapshot &state = ctx_.store.state();
    if (results_.empty())
        return search_mode_ ? std::vector<hui::ui::Hint>{{hui::ui::Button::square, tr("Edit search")}}
                            : std::vector<hui::ui::Hint>{{hui::ui::Button::square, tr("Filters and sort")},
                                                         {hui::ui::Button::l1, tr("Collections"), hui::ui::Button::r1}};
    const Game &game = catalogue_games()[static_cast<std::size_t>(results_[static_cast<std::size_t>(std::clamp(cn_at_, 0, static_cast<int>(results_.size()) - 1))])];
    std::vector<hui::ui::Hint> hints{{hui::ui::Button::cross, tr(addons_ ? "Open" : "Open game")},
                                     {hui::ui::Button::square, search_mode_ ? tr("Edit search") : tr("Filters and sort")}};
    if (state.paired && !addons_)
        hints.push_back({hui::ui::Button::triangle, state.favourite(game.id) ? tr("Remove from favourites") : tr("Favourite")});
    if (!search_mode_)
        hints.push_back({hui::ui::Button::l1, tr("Collections"), hui::ui::Button::r1});
    return hints;
}

// ---------------------------------------------------------------- Panorama

pano::Page BrowseScreen::panorama_page() const
{
    // Browse: the results (all games, or filtered) then the favourites among
    // them. Search: the results led by the query, then Edit search and Filters.
    pano::Page page;
    const Snapshot &state = ctx_.store.state();
    if (results_.empty())
        return page;
    const auto game_group = [this](std::string title, std::string count, std::vector<int> games, bool search, const char *key)
    {
        pano::Group g;
        g.title = std::move(title);
        g.count = std::move(count);
        g.size = static_cast<int>(games.size());
        // One large tile, then small ones, however many games (as the
        // browser version and Zero lay them out).
        g.pattern = pano::Pattern::lead;
        g.fill = [this, games = std::move(games), key, search](int i, pano::Tile &tile)
        {
            const Snapshot &now = ctx_.store.state();
            const Game &game = catalogue_games()[static_cast<std::size_t>(games[static_cast<std::size_t>(i)])];
            tile.key = std::string(key) + game.id;
            tile.game = &game;
            tile.name_on_focus = true;
            tile.ps4 = !game.releases.empty() && game.releases.front().platform == "ps4";
            tile.star = now.favourite(game.id);
            tile.label = game.title;
            if (!game.addon_id.empty())
            {
                tile.sub = game.addon_type.empty() ? game.base_title : game.base_title + " \xC2\xB7 " + addon_type_label(game.addon_type);
                tile.label_size = 24.0f;
                return;
            }
            const bool big = i == 0;
            if (big)
            {
                // The big tile: what is happening to it, else its genre and
                // platform (Search: its size).
                const CollectionState gs = game_state(game, now.jobs, now.library_games());
                const bool ps4 = !game.releases.empty() && game.releases.front().platform == "ps4";
                std::string more = ps4 ? "PS4" : "PS5";
                if (search)
                    more = !game.releases.empty() && game.releases.front().size_bytes > 0.0 ? format_bytes(game.releases.front().size_bytes)
                                                                                            : std::string();
                if (gs.label != nullptr && gs.label[0] != '\0')
                    tile.sub = pano::lower(gs.label);
                else
                {
                    tile.sub = game.genre.empty() ? std::string() : i18n::genre(game.genre);
                    if (!more.empty())
                        tile.sub += (tile.sub.empty() ? "" : " \xC2\xB7 ") + more;
                }
                tile.label_size = 30.0f;
            }
        };
        return g;
    };
    const std::string count = i18n::count(static_cast<long>(results_.size()));
    if (search_mode_)
    {
        const std::string title = filters_.query.empty() ? pano::lower(tr(addons_ ? "All add-ons" : "All games"))
                                                         : "\xE2\x80\x9C" + filters_.query + "\xE2\x80\x9D \xC2\xB7 " +
                                                               trn(static_cast<long>(results_.size()), addons_ && !grouped() ? "{count} add-on" : "{count} game", addons_ && !grouped() ? "{count} add-ons" : "{count} games");
        page.groups.push_back(game_group(title, filters_.query.empty() ? count : std::string(), results_, true, "r:"));
        pano::Group tools;
        tools.title = pano::lower(tr("Search"));
        tools.size = 2;
        tools.fill = [](int i, pano::Tile &tile)
        {
            tile.key = i == 0 ? "s:edit" : "s:filters";
            tile.has_icon = true;
            tile.icon = i == 0 ? icons::Icon::search : icons::Icon::filter;
            tile.label = i == 0 ? tr("Edit search") : tr("Filters");
        };
        page.groups.push_back(std::move(tools));
        return page;
    }
    page.groups.push_back(game_group(pano::lower(filters_.narrowed() || !filters_.query.empty() ? tr("Filtered") : tr(addons_ ? "All add-ons" : "All games")), count,
                                     results_, false, "r:"));
    if (state.paired && !addons_)
    {
        std::vector<int> favourites;
        for (int at : results_)
            if (state.favourite(catalogue_games()[static_cast<std::size_t>(at)].id))
                favourites.push_back(at);
        if (favourites.empty())
        {
            // None yet: one tile that says how to save one.
            pano::Group none;
            none.title = pano::lower(tr("Favourites"));
            none.size = 1;
            none.pattern = pano::Pattern::custom;
            none.places = {{0, 0, 2, 1}};
            none.fill = [](int, pano::Tile &tile)
            {
                tile.key = "f:none";
                tile.has_icon = true;
                tile.icon = icons::Icon::heart;
                tile.label = tr("No favourites yet");
                tile.sub = tr("Add favourites from a game\xE2\x80\x99s page.");
            };
            page.groups.push_back(std::move(none));
        }
        else
        {
            const std::string n = i18n::count(static_cast<long>(favourites.size()));
            page.groups.push_back(game_group(pano::lower(tr("Favourites")), n, std::move(favourites), false, "f:"));
        }
    }
    return page;
}

std::string BrowseScreen::backdrop_game() const
{
    if (!look::is(look::Interface::panorama) || keyboard_open_)
        return {};
    const pano::Tile tile = panorama_tile(panorama_page());
    return tile.game != nullptr ? tile.game->id : std::string();
}

pano::Tile BrowseScreen::panorama_tile(const pano::Page &page) const
{
    pano::Tile tile;
    if (page.groups.empty())
        return tile;
    const pano::Group &g = page.groups[static_cast<std::size_t>(std::clamp(pano_.focus.group, 0, static_cast<int>(page.groups.size()) - 1))];
    if (g.fill && pano_.focus.index >= 0 && pano_.focus.index < g.size)
        g.fill(pano_.focus.index, tile);
    return tile;
}

Intent BrowseScreen::update_panorama(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    Intent intent;
    const Snapshot &state = ctx_.store.state();
    pano::keyboard_look(keyboard_.style);
    keyboard_.set_bounds(pano::keyboard_keys());
    // Favourites are a group of their own here, not a collection.
    if (filters_.favourites && !search_mode_)
    {
        filters_.favourites = false;
        index_ = 0;
        refilter();
    }
    const pano::Page page = panorama_page();
    pano_.clamp(page);
    const pano::Tile tile = panorama_tile(page);
    const bool can_reset = filters_.narrowed() || filters_.favourites || !filters_.query.empty();
    const auto reset = [&]
    {
        // As Store's Reset filters: everything, the search too.
        filters_ = BrowseFilters{};
        index_ = 0;
        refilter();
        pano_.focus = {};
        feedback.play(hui::audio::Cue::select);
    };
    const auto step = [&](int &at, int count)
    {
        const int to = at + (input.nav == Direction::down ? 1 : -1);
        if (to >= 0 && to < count)
        {
            at = to;
            feedback.play(hui::audio::Cue::focus);
        }
        else
            refuse(feedback, input);
    };
    if (focused && keyboard_open_ && naming_)
        update_keyboard(input, feedback);
    else if (focused && keyboard_open_)
    {
        update_keyboard(input, feedback);
        if (!keyboard_open_)
        {
            pano_.focus = {};
            pano_.snap();
        }
    }
    else if (focused && pn_picker_ == 2 && open_filter_ == kSavedFilters && input.is_pressed(Action::west))
    {
        if (delete_saved(pn_value_, feedback))
            pn_value_ = std::max(0, std::min(pn_value_, static_cast<int>(saved_here().size()) - 1));
        else
            refuse(feedback, input);
    }
    else if (focused && pn_picker_ == 2)
    {
        // A filter's values: Cross picks one and goes back to the filters, or
        // ticks one of Source's or Format's and stays.
        const int filter = open_filter_;
        int selected = 0;
        const int options = static_cast<int>(filter_options(filter, &selected).size());
        if (input.nav == Direction::up || input.nav == Direction::down)
            step(pn_value_, options);
        else if (input.is_pressed(Action::confirm))
        {
            if (filter == kSavedFilters && pn_value_ == 0 && !crossbar_values(filter).front().enabled)
            {
                refuse(feedback, input);
                return intent;
            }
            choose(filter, pn_value_);
            pano_.focus = {};
            if (!multiple(filter))
                pn_picker_ = filter == kSavedFilters && pn_value_ > 0 ? 0 : 1; // applied: its results
            feedback.play(multiple(filter) ? hui::audio::Cue::toggle : hui::audio::Cue::select);
        }
        else if (input.is_pressed(Action::back))
        {
            pn_picker_ = 1;
            feedback.play(hui::audio::Cue::back);
        }
    }
    else if (focused && pn_picker_ == 1)
    {
        // Filters and sort: the filters, then Reset filters and Saved filters.
        const std::vector<int> order = filter_order();
        const int filters = static_cast<int>(order.size());
        if (input.nav == Direction::up || input.nav == Direction::down)
            step(pn_row_, filters + 2);
        else if (input.is_pressed(Action::confirm))
        {
            if (pn_row_ < filters)
            {
                open_filter_ = order[static_cast<std::size_t>(pn_row_)];
                filter_options(open_filter_, &pn_value_);
                pn_picker_ = 2;
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (pn_row_ == filters + 1)
            {
                if (!state.paired)
                    refuse(feedback, input);
                else
                {
                    open_filter_ = kSavedFilters;
                    pn_value_ = 0;
                    pn_picker_ = 2;
                    feedback.play(hui::audio::Cue::modal_open);
                }
            }
            else if (can_reset)
            {
                reset();
                pn_picker_ = 0;
            }
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::back) || input.is_pressed(Action::west))
        {
            pn_picker_ = 0;
            feedback.play(hui::audio::Cue::modal_close);
        }
    }
    else if (focused)
    {
        const auto open_filters = [&]
        {
            pn_picker_ = 1;
            pn_row_ = 0;
            feedback.play(hui::audio::Cue::modal_open);
        };
        if (input.nav != Direction::none && !page.groups.empty())
            pano_.move(page, input, feedback);
        else if (input.is_pressed(Action::confirm))
        {
            if (tile.game != nullptr)
            {
                feedback.play(hui::audio::Cue::select);
                intent.kind = Intent::Kind::open_game;
                intent.id = tile.game->id;
            }
            else if (tile.key == "s:edit" || (page.groups.empty() && search_mode_))
            {
                feedback.play(hui::audio::Cue::modal_open);
                start_search();
            }
            else if (tile.key == "s:filters")
                open_filters();
            else if (page.groups.empty() && can_reset)
                reset();
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::west))
        {
            // Square: Edit search in Search, the filters in Browse.
            if (search_mode_)
            {
                feedback.play(hui::audio::Cue::modal_open);
                start_search();
            }
            else
                open_filters();
        }
        else if (tile.game != nullptr && input.is_pressed(Action::north))
        {
            if (!addons_ && state.paired && state.link == Link::online && !ctx_.store.favourite_action().busy)
            {
                ctx_.store.set_favourite(tile.game->id, !state.favourite(tile.game->id));
                feedback.play(hui::audio::Cue::toggle);
            }
            else
                refuse(feedback, input);
        }
    }
    keyboard_.set_active(keyboard_open_);
    keyboard_.update(dt);
    keyboard_in_.target = keyboard_open_ ? 1.0f : 0.0f;
    keyboard_in_.update(dt, 14.0f);
    pn_picker_in_.target = pn_picker_ != 0 ? 1.0f : 0.0f;
    pn_picker_in_.update(dt, look::prefs().reduced_motion ? 40.0f : 16.0f);
    pano_.animate(panorama_page(), dt);
    return intent;
}

void BrowseScreen::draw_panorama(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const int count = static_cast<int>(results_.size());
    const std::string games = trn(static_cast<long>(count), addons_ && !grouped() ? "{count} add-on" : "{count} game", addons_ && !grouped() ? "{count} add-ons" : "{count} games");
    if (naming_ && keyboard_open_)
    {
        // Naming the filters to save: the name as the title, the keys below.
        pano::draw_keyboard(ctx_, frame, keyboard_, tr("Name these filters"), saved_name_, tr("type a name"), false);
        return;
    }
    if (search_mode_ && keyboard_open_)
    {
        // The query as the title, a row of results under it, the keys below.
        if (count > 0 && !filters_.query.empty())
        {
            for (int i = 0; i < std::min(count, 8); ++i)
            {
                pano::Tile tile;
                tile.game = &catalogue_games()[static_cast<std::size_t>(results_[static_cast<std::size_t>(i)])];
                tile.label = tile.game->title;
                pano::draw_tile(ctx_, frame.overlay, tile, {100.0f + static_cast<float>(i) * pano::kStep, 330.0f, pano::kUnit, pano::kUnit},
                                0.0f);
            }
        }
        pano::draw_keyboard(ctx_, frame, keyboard_,
                            filters_.query.empty() ? std::string(tr(addons_ ? "Search add-ons or base games" : "Search games or title ID"))
                                                   : std::string(tr("Search")) + " \xC2\xB7 " + games,
                            filters_.query, tr("type a name"), false);
        return;
    }
    const pano::Page page = panorama_page();
    pano::draw_header(ctx_, list, pano::lower(tr(addons_ ? "Add-ons" : apps_ ? "Apps" : search_mode_ ? "Search" : "Browse")), pano_.scroll() * 0.35f);
    if (!search_mode_ && count > 0)
    {
        // What's applied, under the title, with Reset filters beside it.
        std::string line = crossbar_summary();
        if (!filters_.query.empty())
            line = "\xE2\x80\x9C" + filters_.query + "\xE2\x80\x9D \xC2\xB7 " + line;
        const float mid = pano::baseline(244.0f, 28.0f) - 28.0f * 0.364f;
        list.push_opacity(0.85f);
        icons::draw(list, icons::Icon::filter, 100.0f + 13.0f, mid, 26.0f, pano::kInk, 1.6f);
        const float w = ui::text_fit(list, pano::font(ctx_, pano::Face::light, line), line, 100.0f + 26.0f + 18.0f,
                                     pano::baseline(244.0f, 28.0f), 28.0f, 1300.0f, pano::kInk);
        if (filters_.narrowed() || !filters_.query.empty())
        {
            const float rx = 100.0f + 26.0f + 18.0f + w + 18.0f + 14.0f;
            const ui::Color accent = look::accent_color();
            icons::draw(list, icons::Icon::refresh, rx + 11.0f, mid, 22.0f, accent, 2.0f);
            ui::text(list, pano::font(ctx_, pano::Face::light, tr("Reset filters")), tr("Reset filters"), rx + 22.0f + 18.0f,
                     pano::baseline(244.0f, 28.0f), 28.0f, accent);
        }
        list.pop_opacity();
    }
    if (page.groups.empty())
    {
        // Nothing matched: say why, and offer the way out.
        std::vector<pano::Button> buttons;
        if (search_mode_)
            buttons.push_back(pano::button(tr("Edit search"), icons::Icon::search, true));
        else if (filters_.narrowed() || !filters_.query.empty())
            buttons.push_back(pano::button(tr("Reset filters"), icons::Icon::refresh, true));
        std::string kicker = search_mode_ ? "\xE2\x80\x9C" + filters_.query + "\xE2\x80\x9D" : crossbar_summary();
        pano::draw_empty(ctx_, list, kicker, tr(addons_ ? "No add-ons match these filters." : "No games found"), tr("Try another name, title ID or filter."), buttons);
    }
    else
        pano_.draw(ctx_, list, page, pn_picker_ == 0);
    if (pn_picker_in_.value > 0.01f)
    {
        // Filters and sort as one list; a filter's values as the next.
        pano::Picker picker;
        const std::string head = std::string(tr("Filters and sort")) + " \xC2\xB7 " + games;
        if (pn_picker_ == 2 && open_filter_ >= 0)
        {
            // Sort by is a short list of orders; the others say where they are.
            const int filter = open_filter_;
            picker.head = filter == kSavedFilters ? tr("Saved filters") : tr(addons_ && filter == 1 ? "Type" : kLabels[filter]);
            if (filter == 4)
                picker.size = 44.0f;
            else
                picker.sub = head;
            for (const xb::MenuItem &value : crossbar_values(filter))
            {
                pano::Item item;
                item.title = value.label;
                item.sub = value.sub;
                item.chosen = value.checked;
                picker.items.push_back(std::move(item));
            }
            picker.focus = pn_value_;
        }
        else
        {
            picker.head = head;
            picker.size = 44.0f;
            for (const xb::MenuItem &filter : crossbar_filters())
            {
                pano::Item item;
                item.title = filter.label;
                item.sub = filter.value;
                item.off = !filter.enabled;
                picker.items.push_back(std::move(item));
            }
            picker.focus = pn_row_;
            if (filters_.sort == Sort::size || filters_.sort == Sort::size_desc)
                picker.note = tr("Sorted by the smallest download option that matches your filters.");
        }
        pano::draw_picker(ctx_, frame.overlay, picker, pn_picker_in_.value);
    }
}

std::vector<hui::ui::Hint> BrowseScreen::panorama_hints() const
{
    if (keyboard_open_)
        return {{hui::ui::Button::square, tr("Delete")}, {hui::ui::Button::triangle, tr("Space")}, {hui::ui::Button::circle, tr("Close")}};
    if (pn_picker_ == 2 && open_filter_ == kSavedFilters && pn_value_ > 0)
        return {{hui::ui::Button::cross, tr("Apply")}, {hui::ui::Button::square, tr("Delete")}, {hui::ui::Button::circle, tr("Back")}};
    if (pn_picker_ != 0)
        return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, pn_picker_ == 2 ? tr("Back") : tr("Close")}};
    const Snapshot &state = ctx_.store.state();
    const pano::Page page = panorama_page();
    if (page.groups.empty())
    {
        if (search_mode_)
            return {{hui::ui::Button::square, tr("Edit search")}};
        std::vector<hui::ui::Hint> hints;
        if (filters_.narrowed() || !filters_.query.empty())
            hints.push_back({hui::ui::Button::cross, tr("Reset filters")});
        hints.push_back({hui::ui::Button::square, tr("Filters")});
        hints.push_back({hui::ui::Button::l1, tr("Sections"), hui::ui::Button::r1});
        return hints;
    }
    const pano::Tile tile = panorama_tile(page);
    std::vector<hui::ui::Hint> hints;
    if (tile.game != nullptr || tile.key == "s:filters")
        hints.push_back({hui::ui::Button::cross, tr("Open")});
    else if (tile.key == "s:edit")
        hints.push_back({hui::ui::Button::cross, tr("Edit search")});
    hints.push_back({hui::ui::Button::square, search_mode_ ? tr("Edit search") : tr("Filters")});
    if (tile.game != nullptr && state.paired && !addons_)
        hints.push_back({hui::ui::Button::triangle, state.favourite(tile.game->id) ? tr("Remove from favourites") : tr("Favourite")});
    hints.push_back({hui::ui::Button::l1, tr("Sections"), hui::ui::Button::r1});
    return hints;
}

} // namespace orbit
