// Orbit Store TV app - The app: top bar, tabs, the game page, status and dialogs.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/appearance.hpp"
#include "orbit/mark.hpp"
#include "orbit/screens.hpp"
#include "orbit/i18n.hpp"
#include "orbit/look.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace orbit
{

namespace
{

constexpr const char *kTabs[] = {/* i18n */ "Discover", /* i18n */ "Browse", /* i18n */ "Add-ons", /* i18n */ "Library", /* i18n */ "Downloads",
                                  /* i18n */ "DLC", /* i18n */ "Updates", /* i18n */ "Apps"}; // by tab::
// Left to right in the top bar.
constexpr int kOrder[] = {tab::discover, tab::browse, tab::apps, tab::addons, tab::updates, tab::library, tab::downloads};
constexpr int kTabCount = tab::count;
constexpr int kSearchItem = kTabCount;
constexpr int kSettingsItem = kTabCount + 1;
constexpr int kTopItems = kTabCount + 2; // the tabs, search, settings
// The browser's .header: 5.2 rem tall, everything on its centre line.
constexpr float kTopY = 52.0f;
constexpr float kTabSize = 22.0f; // nav buttons: 1.1 rem
constexpr float kTabPad = 22.0f;  // 1.1 rem each side
constexpr float kTabHeight = 46.0f;
constexpr float kTabGap = 6.0f;
constexpr float kIcon = 56.0f; // .top-icon: 2.8 rem
constexpr float kSearchX = 1724.0f;
constexpr float kSettingsX = 1792.0f;
constexpr const char *kBrand = "Orbit Store";
// How long the opening screen shows before "Orbit isn't running".
constexpr float kOpeningGrace = 2.5f;

// Where the BETA label starts and how wide it is.
float beta_x(const ui::Type &type)
{
    return ui::kGutter + 40.0f + 14.0f + type.regular.measure(kBrand, 22.0f) + 14.0f;
}
float beta_width(const ui::Type &type)
{
    return type.semibold.measure(tr("BETA"), 13.0f) + 4.0f + 2.0f * 7.6f;
}

// Updates shows only while an installed game has an update waiting, and DLC only while the NAS has a
// DLC an installed game lacks; each goes away again once they are all done.
bool tab_hidden(const Snapshot &state, int index)
{
    // Apps come from the NAS folders: no tab until the catalogue has some.
    if (index == tab::apps)
        return state.have_catalog && std::none_of(state.games.begin(), state.games.end(),
                                                  [](const Game &g) { return game_category(g) == Category::apps; });
    if (index == tab::dlc)
        return true; // the NAS DLC are add-ons now: no tab of their own
    if (index == tab::updates)
        return state.game_updates <= 0;
    return false;
}

// The number badge on a tab: its pill is at least 24 wide and grows with the digits.
float badge_width(const ui::Type &type, int number)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%d", number);
    return std::max(24.0f, type.semibold.measure(text, 14.0f) + 18.0f);
}

// A tab's badge: Updates the installed games with an update waiting, DLC the packs to install.
float tab_extra(const ui::Type &type, const Snapshot &state, int index)
{
    if (index == tab::updates && state.game_updates > 0)
        return 10.0f + badge_width(type, state.game_updates);
    if (index == tab::dlc && state.game_dlc > 0)
        return 10.0f + badge_width(type, state.game_dlc);
    return 0.0f;
}

ui::Rect tab_rect(const ui::Type &type, const Snapshot &state, int index)
{
    // The nav starts 3 rem after the brand button (whose padding ends 0.9 rem
    // after the label).
    float x = beta_x(type) + beta_width(type) + 18.0f + 60.0f;
    for (const int i : kOrder)
    {
        if (i == index)
            break;
        if (tab_hidden(state, i))
            continue;
        x += type.regular.measure(tr(kTabs[i]), kTabSize) + 2.0f * kTabPad + tab_extra(type, state, i) + kTabGap;
    }
    return {x, kTopY - kTabHeight * 0.5f,
            type.regular.measure(tr(kTabs[index]), kTabSize) + 2.0f * kTabPad + tab_extra(type, state, index),
            kTabHeight};
}

bool starting_now(const Snapshot &state)
{
    return state.starting == Starting::starting || state.starting == Starting::waiting;
}

int active_jobs(const Snapshot &state)
{
    int count = 0;
    for (const Job &job : state.jobs)
    {
        if (!job_inactive(job) && job.status != "error")
            ++count;
    }
    return count;
}

} // namespace

App::App(Context &context)
    : ctx_(context), discover_(context), browse_(context), addons_(context, true), apps_(context, false, true), dlc_(context, true), updates_(context), library_(context), downloads_(context),
      game_(context), settings_(context)
{
    ctx_.appearance = appearance::load();
    top_focus_.resize(kTopItems);
    backdrop_in_.snap(1.0f);
}

Screen &App::screen()
{
    switch (tab_)
    {
    case tab::apps:
        return apps_;
    case tab::dlc:
        return dlc_;
    case tab::updates:
        return updates_;
    case tab::discover:
        return discover_;
    case tab::addons:
        return addons_;
    case tab::library:
        return library_;
    case tab::downloads:
        return downloads_;
    default:
        return browse_;
    }
}

const Screen &App::screen() const
{
    return const_cast<App *>(this)->screen();
}

bool App::content_blocked() const
{
    // Library and Downloads don't depend on the download sources or the catalogue.
    if (tab_ == tab::downloads || tab_ == tab::library || tab_ == tab::dlc || tab_ == tab::updates)
        return false;
    const Snapshot &state = ctx_.store.state();
    return state.setup_needed() || (state.have_catalog && state.games.empty());
}

void App::open_game_updates(int view)
{
    show_tab(view == 2 ? tab::dlc : tab::updates);
}

std::vector<int> App::top_items() const
{
    const Snapshot &state = ctx_.store.state();
    std::vector<int> items;
    for (const int i : kOrder)
        if (!tab_hidden(state, i))
            items.push_back(i);
    items.push_back(kSearchItem);
    items.push_back(kSettingsItem);
    return items;
}

void App::show_tab(int tab)
{
    search_open_ = false;
    tab_ = std::clamp(tab, 0, kTabCount - 1);
    game_open_ = false;
    if (settings_open_)
        settings_.close();
    settings_open_ = false;
    area_ = Area::content;
    screen().focus();
}

void App::open_game(const std::string &game_id)
{
    game_.open(game_id);
    game_.set_section(section());
    game_open_ = true;
    game_in_.snap(0.0f);
}

void App::open_settings(SettingsScreen::Section section, bool content)
{
    area_ = Area::content;
    settings_open_ = true;
    game_open_ = false;
    settings_.open(section, content);
}

bool App::update_badge() const
{
    const Snapshot &state = ctx_.store.state();
    const std::string running = app_release_version(ctx_.app_version);
    const bool tv = state.have_tv_app && !state.tv_app.latest_version.empty() && !running.empty() &&
                    starter::compare_versions(state.tv_app.latest_version, running) > 0;
    return tv || (state.have_service_update && state.service.update_available);
}

void App::show_toast(std::string text, float seconds, bool update)
{
    toast_ = std::move(text);
    toast_time_ = seconds;
    toast_length_ = seconds;
    toast_update_ = update;
    toast_favourite_ = false;
}

void App::handle_intent(const Intent &intent, Feedback &feedback)
{
    switch (intent.kind)
    {
    case Intent::Kind::open_updates:
        show_tab(intent.tab == 2 ? tab::dlc : tab::updates);
        break;
    case Intent::Kind::provider_browser:
        provider_open_ = true;
        provider_start_pending_ = ctx_.store.browser_action().busy &&
                                  !ctx_.store.state().browser.active;
        provider_focus_ = 1;
        provider_seen_ = ctx_.store.state().browser.id;
        break;
    case Intent::Kind::top_bar:
        if (!look::is(look::Interface::store) && !look::is(look::Interface::cinema))
            break;
        area_ = Area::top;
        top_index_ = tab_;
        feedback.play(hui::audio::Cue::focus);
        break;
    case Intent::Kind::open_addons:
        addons_.for_base(intent.id);
        show_tab(tab::addons);
        break;
    case Intent::Kind::open_game:
        open_game(intent.id);
        if (intent.detail == "about")
            game_.show_about();
        feedback.play(hui::audio::Cue::open, 1.0f, 0.0f, 0.7f);
        break;
    case Intent::Kind::close:
        if (settings_open_)
        {
            settings_open_ = false;
            settings_.close();
            if (!look::is(look::Interface::store))
                show_section(section::discover);
            break;
        }
        game_open_ = false;
        if (tab_ == tab::discover)
            discover_.return_to(game_.game_id());
        else if (tab_ == tab::browse)
            browse_.return_to(game_.game_id());
        else if (tab_ == tab::addons) addons_.return_to(game_.game_id());
        else if (tab_ == tab::apps) apps_.return_to(game_.game_id());
        break;
    case Intent::Kind::show_job:
        game_open_ = false;
        tab_ = tab::downloads;
        area_ = Area::content;
        downloads_.show_job(intent.id);
        break;
    case Intent::Kind::show_tab:
        show_tab(intent.tab);
        feedback.play(hui::audio::Cue::tab);
        break;
    case Intent::Kind::show_library:
        game_open_ = false;
        tab_ = tab::library;
        area_ = Area::content;
        library_.focus();
        library_.show_game(intent.id, intent.detail);
        break;
    default:
        break;
    }
    if (!intent.toast.empty())
    {
        show_toast(intent.toast);
    }
}

int App::section() const
{
    if (settings_open_)
        return section::settings;
    if (search_open_)
        return section::search;
    // Updates and DLC are sections of their own in the looks without the Store top bar.
    static const int kOfTab[tab::count] = {section::discover, section::browse, section::addons, section::library,
                                           section::downloads, section::updates, section::updates, section::apps};
    return kOfTab[std::clamp(tab_, 0, tab::count - 1)];
}

void App::show_section(int next)
{
    // Cinema keeps search in Browse's filters, without a duplicate chapter.
    if (look::is(look::Interface::cinema) && next == section::search)
        next = section::browse;
    game_open_ = false;
    search_open_ = false;
    if (next == section::settings)
    {
        open_settings(SettingsScreen::Section::sources);
        return;
    }
    if (settings_open_)
        settings_.close();
    settings_open_ = false;
    area_ = Area::content;
    if (next == section::search)
    {
        search_open_ = true;
        tab_ = tab::browse;
        browse_.focus();
        browse_.open_search_section();
        return;
    }
    static const int kTabOf[section::count] = {tab::browse, tab::discover, tab::browse, tab::apps, tab::addons,
                                               tab::library, tab::downloads, tab::updates, tab::library};
    tab_ = kTabOf[std::clamp(next, 0, section::count - 1)];
    screen().focus();
}

int App::adjacent_section(int step) const
{
    const int first = look::is(look::Interface::cinema) ? section::discover : section::search;
    const int count = section::count - first;
    return first + (section() - first + step + count) % count;
}

bool App::update_section_nav(const InputFrame &input, float dt, Feedback &feedback)
{
    if (settings_open_ ? settings_.modal() : screen().modal())
        return false;
    const bool horizontal = input.nav == Direction::left || input.nav == Direction::right;
    if (look::is(look::Interface::crossbar) && horizontal)
    {
        show_section(adjacent_section(input.nav == Direction::right ? 1 : -1));
        feedback.play(hui::audio::Cue::tab);
        return true;
    }
    if (!look::is(look::Interface::cinema) || area_ != Area::top)
        return false;
    if (horizontal || input.is_pressed(Action::page_prev) || input.is_pressed(Action::page_next))
    {
        const int step = input.nav == Direction::right || input.is_pressed(Action::page_next) ? 1 : -1;
        show_section(adjacent_section(step));
        area_ = Area::top;
        feedback.play(hui::audio::Cue::tab);
    }
    else if (input.nav == Direction::down || input.is_pressed(Action::confirm) || input.is_pressed(Action::back))
    {
        area_ = Area::content;
        feedback.play(hui::audio::Cue::focus);
    }
    else if (input.nav == Direction::up)
        refuse(feedback, input);
    // Keep animation running, without delivering navbar input to a collection.
    const InputFrame idle{};
    if (settings_open_)
        settings_.update(idle, dt, feedback, false);
    else
        screen().update(idle, dt, feedback, false);
    return true;
}

bool App::update_sections(const InputFrame &input, Feedback &feedback)
{
    if (!input.is_pressed(Action::page_prev) && !input.is_pressed(Action::page_next))
        return false;
    // A dialog, keyboard or list keeps L1/R1 for itself.
    if (settings_open_ ? settings_.modal() : screen().modal())
        return false;
    const int step = input.is_pressed(Action::page_next) ? 1 : -1;
    // Panorama's Settings go through their pivots first.
    if (look::is(look::Interface::panorama) && settings_open_ && settings_.page(step))
    {
        feedback.play(hui::audio::Cue::tab);
        return true;
    }
    // Cinema steps through a chapter's collections, Panorama through a page's
    // groups (Downloads), before the next section.
    if ((look::is(look::Interface::cinema) || look::is(look::Interface::panorama)) && !settings_open_ && !game_open_ &&
        screen().page(step))
    {
        feedback.play(hui::audio::Cue::tab);
        return true;
    }
    show_section(adjacent_section(step));
    feedback.play(hui::audio::Cue::tab);
    return true;
}

bool App::update_keep(const InputFrame &input, Feedback &feedback)
{
    if (!look::pending())
    {
        keep_button_ = 0;
        return false;
    }
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const int next = input.nav == Direction::right ? 1 : 0;
        if (next != keep_button_)
        {
            keep_button_ = next;
            feedback.play(hui::audio::Cue::focus);
        }
        else
            refuse(feedback, input);
    }
    else if (input.nav != Direction::none)
        refuse(feedback, input);
    if (input.is_pressed(Action::confirm))
    {
        if (keep_button_ == 0)
            look::keep();
        else
            look::revert();
        feedback.play(hui::audio::Cue::select);
    }
    else if (input.is_pressed(Action::back))
    {
        look::revert();
        feedback.play(hui::audio::Cue::modal_close);
    }
    return true;
}

void App::update(const InputFrame &input, float dt, Feedback &feedback)
{
    ctx_.clock += dt;
    look::tick(ctx_.clock);
    keep_in_.target = look::pending() ? 1.0f : 0.0f;
    keep_in_.update(dt, 14.0f);
    if (look::is(look::Interface::store))
        controls_open_ = false;
    controls_in_.target = controls_open_ ? 1.0f : 0.0f;
    controls_in_.update(dt, 16.0f);
    notice_time_ = std::max(0.0f, notice_time_ - dt);
    ctx_.store.tick();
    {
        // The choices the download service keeps for this console (the app cannot keep files itself).
        const Snapshot &saved = ctx_.store.state();
        if (!keyboard_synced_ && saved.have_system)
        {
            keyboard_synced_ = true;
            if (!saved.system.keyboard.empty())
                ctx_.keyboard_choice = saved.system.keyboard;
        }
        if (!appearance_synced_ && saved.have_system)
        {
            appearance_synced_ = true;
            if (!saved.system.appearance.empty())
                ctx_.appearance = appearance::find(saved.system.appearance);
        }
        ctx_.azerty = ctx_.keyboard_choice == "azerty" ||
                      (ctx_.keyboard_choice != "qwerty" && i18n::language() == "fr");
    }
    ctx_.sounds_on = !ctx_.store.state().system.sounds_off;
    ctx_.sound_volume = ctx_.store.state().system.sound_volume;
    if (language_ != i18n::revision())
    {
        language_ = i18n::revision();
        for (Screen *screen : std::initializer_list<Screen *>{&discover_, &browse_, &addons_, &apps_, &library_, &downloads_,
                                                              &game_, &settings_})
            screen->retranslate();
    }
    const Snapshot &state = ctx_.store.state();
    ctx_.art.set_token(state.token);
    ctx_.art.tick();
    ctx_.store.set_fast(false);
    toast_time_ = std::max(0.0f, toast_time_ - dt);
    status_age_ += dt;

    const InputFrame idle{};
    const bool ready = state.have_system && state.link != Link::connecting &&
                       (state.have_catalog || state.setup_needed());
    if (!ready)
    {
        // The opening and offline screens move on by themselves. Once Orbit is
        // known not to be running, Cross asks the store to start it again.
        if (status_offline(state) && ctx_.store.can_start() && input.is_pressed(Action::confirm))
        {
            ctx_.store.start_orbit();
            feedback.play(hui::audio::Cue::launch);
        }
        return;
    }

    // A finished restart of the download service, wherever the player is.
    if (state.restart == Restart::done)
    {
        bool paused = false;
        for (const Job &job : state.jobs)
            paused = paused || job.status == "paused";
        show_toast(paused ? tr("Download service {version} is running. Resume your downloads in "
                               "Downloads.",
                               {{"version", state.restart_detail}})
                          : tr("Download service {version} is running",
                               {{"version", state.restart_detail}}),
                   4.5f);
        ctx_.store.dismiss_restart();
    }
    // Every look confirms a favourite (Store too, as the browser and Zero
    // do); the count before the request and after it says whether it was
    // added or removed. The count is held while the request is out, so a
    // refresh that brings the new list first doesn't hide the notice.
    if (ctx_.store.favourite_action().busy)
        favourite_waiting_ = true;
    if (ctx_.store.favourite_action().serial != favourite_serial_)
    {
        favourite_serial_ = ctx_.store.favourite_action().serial;
        if (ctx_.store.favourite_action().error.empty() && state.favourites.size() != favourite_count_)
        {
            show_toast(state.favourites.size() > favourite_count_ ? tr("Added to favourites") : tr("Removed from favourites"));
            toast_favourite_ = true;
        }
        favourite_waiting_ = false;
    }
    if (!favourite_waiting_)
        favourite_count_ = state.favourites.size();
    // An update for either part shows once, then waits under App settings.
    if (!update_told_ && update_badge() && !settings_open_)
    {
        update_told_ = true;
        show_toast(tr("An update is available in App settings"), 4.5f, true);
    }

    // Recover active verification after reopening the native app. Historical
    // terminal results never reopen themselves or undo a dismissal.
    if (state.have_browser && state.browser.active && !state.browser.account_page && state.browser.id != provider_seen_)
    {
        provider_seen_ = state.browser.id;
        // A locally requested start may return after the user closed this
        // panel. Record its ID without undoing that dismissal.
        if (!provider_start_pending_)
        {
            provider_open_ = true;
            provider_focus_ = 1;
        }
    }
    if (provider_start_pending_ && !ctx_.store.browser_action().busy)
        provider_start_pending_ = false;
    // Store says once where the other interfaces are.
    if (look::is(look::Interface::store) && !look::prefs().notice_seen && !settings_open_ &&
        !game_open_ && !provider_open_ && !content_blocked() && status_age_ > 2.0f)
    {
        look::Prefs prefs = look::prefs();
        prefs.notice_seen = true;
        look::set(prefs);
        notice_time_ = 8.0f;
    }
    const InputFrame none{};
    if (update_keep(input, feedback))
    {
        // The keep check takes every button; the page behind still animates.
        if (settings_open_)
            settings_.update(none, dt, feedback, false);
        else if (game_open_)
            game_.update(none, dt, feedback, false);
        else
            screen().update(none, dt, feedback, false);
    }
    else if (notice_time_ > 0.0f && input.is_pressed(Action::menu))
    {
        notice_time_ = 0.0f;
        open_settings(SettingsScreen::Section::appearance, true);
        feedback.play(hui::audio::Cue::modal_open);
    }
    else if (controls_open_)
    {
        // Controls: Cross, Circle or Options closes them; the page behind still animates.
        if (input.is_pressed(Action::confirm) || input.is_pressed(Action::back) || input.is_pressed(Action::menu))
        {
            controls_open_ = false;
            feedback.play(hui::audio::Cue::modal_close);
        }
        if (settings_open_)
            settings_.update(none, dt, feedback, false);
        else if (game_open_)
            game_.update(none, dt, feedback, false);
        else
            screen().update(none, dt, feedback, false);
    }
    else if (!look::is(look::Interface::store) && !provider_open_ && input.is_pressed(Action::menu) &&
             !(settings_open_ ? settings_.modal() : game_open_ ? game_.modal() : screen().modal()) &&
             !(settings_open_ || game_open_ || content_blocked() ? false : screen().options_used()) &&
             [&]
             {
                 // Options with nothing using it here: every button, once.
                 for (const hui::ui::Hint &hint : current_hints())
                     if (hint.button == hui::ui::Button::options || hint.second == hui::ui::Button::options)
                         return false;
                 return true;
             }())
    {
        controls_open_ = true;
        feedback.play(hui::audio::Cue::modal_open);
    }
    else if (!game_open_ && !provider_open_ && update_section_nav(input, dt, feedback))
    {
        // Cinema's navbar and Crossbar's category axis own their directions.
    }
    else if (!look::is(look::Interface::store) && !game_open_ && !provider_open_ &&
             update_sections(input, feedback))
    {
        // A new section shows; it gets the controller from the next frame.
    }
    else if (provider_open_)
    {
        ctx_.store.set_fast(true);
        update_provider(input, feedback);
    }
    else if (settings_open_)
    {
        handle_intent(settings_.update(input, dt, feedback, true), feedback);
    }
    else if (game_open_)
    {
        game_in_.target = 1.0f;
        handle_intent(game_.update(input, dt, feedback, true), feedback);
    }
    else if (!look::is(look::Interface::store))
    {
        if (content_blocked())
        {
            if (input.nav == Direction::up && look::is(look::Interface::cinema))
            {
                Intent intent;
                intent.kind = Intent::Kind::top_bar;
                handle_intent(intent, feedback);
            }
            else if (input.is_pressed(Action::confirm))
            {
                open_settings(SettingsScreen::Section::sources, true);
                feedback.play(hui::audio::Cue::modal_open);
            }
            screen().update(idle, dt, feedback, false);
        }
        else
        {
            handle_intent(screen().update(input, dt, feedback, true), feedback);
        }
    }
    else
    {
        // L1 / R1 switch tabs from anywhere on a tab, as on the console's own apps.
        if (!screen().modal() &&
            (input.is_pressed(Action::page_prev) || input.is_pressed(Action::page_next)))
        {
            const std::vector<int> items = top_items();
            const auto here = std::find(items.begin(), items.end(), tab_);
            const int at = static_cast<int>(here - items.begin());
            const int next = at + (input.is_pressed(Action::page_next) ? 1 : -1);
            // Search and App settings end the row; L1 and R1 stay on the tabs.
            if (here != items.end() && next >= 0 && items[static_cast<std::size_t>(next)] < kTabCount)
            {
                tab_ = items[static_cast<std::size_t>(next)];
                top_index_ = tab_;
                screen().focus();
                feedback.play(hui::audio::Cue::tab);
            }
            else
            {
                refuse(feedback, input);
            }
        }

        if (area_ == Area::top)
        {
            if (input.nav == Direction::left || input.nav == Direction::right)
            {
                const std::vector<int> items = top_items();
                const auto here = std::find(items.begin(), items.end(), top_index_);
                const int at = static_cast<int>(here - items.begin());
                const int next = at + (input.nav == Direction::right ? 1 : -1);
                if (here != items.end() && next >= 0 && next < static_cast<int>(items.size()))
                {
                    top_index_ = items[static_cast<std::size_t>(next)];
                    if (top_index_ < kTabCount && top_index_ != tab_)
                    {
                        // Moving along the tabs shows each one, like the console's store.
                        tab_ = top_index_;
                        screen().focus();
                        feedback.play(hui::audio::Cue::tab);
                    }
                    else
                    {
                        feedback.play(hui::audio::Cue::focus);
                    }
                }
                else
                {
                    refuse(feedback, input);
                }
            }
            else if (input.nav == Direction::down)
            {
                area_ = Area::content;
                screen().focus();
                feedback.play(hui::audio::Cue::focus);
            }
            else if (input.nav == Direction::up)
            {
                refuse(feedback, input);
            }
            if (input.is_pressed(Action::confirm))
            {
                if (top_index_ < kTabCount)
                {
                    area_ = Area::content;
                    screen().focus();
                    feedback.play(hui::audio::Cue::select);
                }
                else if (top_index_ == kSearchItem)
                {
                    tab_ = tab::browse;
                    area_ = Area::content;
                    browse_.focus();
                    browse_.start_search();
                    feedback.play(hui::audio::Cue::modal_open);
                }
                else
                {
                    open_settings();
                    feedback.play(hui::audio::Cue::modal_open);
                }
            }
            screen().update(idle, dt, feedback, false);
        }
        else if (content_blocked())
        {
            if (input.nav == Direction::up)
            {
                Intent intent;
                intent.kind = Intent::Kind::top_bar;
                handle_intent(intent, feedback);
            }
            else if (input.is_pressed(Action::confirm))
            {
                open_settings(SettingsScreen::Section::sources, true);
                feedback.play(hui::audio::Cue::modal_open);
            }
            screen().update(idle, dt, feedback, false);
        }
        else
        {
            handle_intent(screen().update(input, dt, feedback, true), feedback);
        }
    }

    // Updates or DLC finished while they were open: back to the Library they belong beside.
    if (look::is(look::Interface::store) &&
        (tab_ == tab::apps ? ctx_.store.state().have_catalog : ctx_.store.state().have_game_updates) &&
        tab_hidden(ctx_.store.state(), tab_))
    {
        show_tab(tab::library);
        top_index_ = tab_;
    }
    if (tab_ != last_tab_)
    {
        if (last_tab_ == tab::library)
            library_.blur();
        last_tab_ = tab_;
    }

    // ---- animation ----
    // Panorama's Settings have no art behind them.
    const std::string wanted = game_open_                                                   ? game_.game_id()
                               : content_blocked()                                          ? std::string()
                               : settings_open_ && look::is(look::Interface::panorama)       ? std::string()
                                                                                            : screen().backdrop_game();
    // On Discover the art waits for the focus to rest for 300 ms, so holding
    // the D-pad doesn't fetch a banner for every cover it passes (main 0.5.1).
    // A game page shows its art at once.
    if (wanted != settling_)
    {
        settling_ = wanted;
        settled_for_ = 0.0f;
    }
    else
    {
        settled_for_ += dt;
    }
    const bool wait = !game_open_ && (tab_ == tab::discover || look::is(look::Interface::panorama)) && !backdrop_.empty();
    if (wanted != backdrop_ && (!wait || settled_for_ >= 0.3f))
    {
        previous_backdrop_ = backdrop_;
        backdrop_ = wanted;
        backdrop_in_.snap(0.0f);
    }
    backdrop_in_.target = 1.0f;
    backdrop_in_.update(dt, 4.0f);
    game_in_.update(dt, 10.0f);
    for (int i = 0; i < kTopItems; ++i)
    {
        top_focus_[static_cast<std::size_t>(i)].target =
            area_ == Area::top && !game_open_ && !settings_open_ && i == top_index_ ? 1.0f : 0.0f;
        top_focus_[static_cast<std::size_t>(i)].update(dt, 16.0f);
    }
}

void App::draw_top_bar(hui::gfx::DrawList &list) const
{
    const ui::Type &type = ctx_.type;
    const Snapshot &state = ctx_.store.state();
    // The brand: mark, name and the BETA label, as on the web.
    if (ctx_.mark != 0)
        list.image(ctx_.mark, {ui::kGutter - 2.0f, kTopY - 22.0f, 44.0f, 44.0f}, hui::gfx::kFullUv,
                   ui::Color::rgb(0xffffff));
    ui::text(list, type.regular, kBrand, ui::kGutter + 54.0f, kTopY + 8.0f, 22.0f, ui::color::text);
    const ui::Rect beta{beta_x(type), kTopY - 12.5f, beta_width(type), 25.0f};
    list.bordered_rect(beta, 4.0f, ui::Color{0, 0, 0, 0}, 1.0f, ui::Color::rgb(0xffffff, 0.3f));
    ui::text(list, type.semibold, tr("BETA"), beta.cx(), kTopY + 4.7f, 13.0f, ui::color::text,
             ui::Align::center);

    // Tabs: text buttons, the current one in a pale pill.
    const int active = active_jobs(state);
    for (const int i : kOrder)
    {
        if (tab_hidden(state, i))
            continue;
        ui::Rect r = tab_rect(type, state, i);
        // Downloads counts its running jobs, Updates the installed games with an update waiting, DLC the packs.
        const int number = i == tab::downloads ? active
                           : i == tab::updates ? state.game_updates
                           : i == tab::dlc     ? state.game_dlc
                                               : 0;
        const bool counted = number > 0;
        const float count_width = badge_width(type, number);
        char count[16];
        std::snprintf(count, sizeof(count), "%d", number);
        // .queue-count sits inside the tab, which widens for it (tab_rect already did so for Updates and DLC).
        if (counted && i == tab::downloads)
            r.w += 10.0f + count_width;
        const bool current = i == tab_;
        const float focus = top_focus_[static_cast<std::size_t>(i)].value;
        ui::focus_ring(list, r, r.h * 0.5f, focus);
        if (current)
            list.bordered_rect(r, r.h * 0.5f, ui::Color::rgb(0xffffff, 0.13f), 1.0f,
                               ui::Color::rgb(0xffffff, 0.2f));
        ui::text(list, type.regular, tr(kTabs[i]), r.x + kTabPad, kTopY + 8.0f, kTabSize,
                 current || focus > 0.5f ? ui::color::text : ui::color::text2);
        if (counted)
        {
            const ui::Rect pill{r.x + r.w - kTabPad - count_width, kTopY - 11.0f, count_width,
                                22.0f};
            list.rounded_rect(pill, 11.0f, ui::color::action);
            ui::text(list, type.semibold, count, pill.cx(), kTopY + 5.0f, 14.0f, ui::color::night1,
                     ui::Align::center);
        }
    }

    // Round tools on the right; each names itself when it has the focus.
    const struct
    {
        float x;
        const char *label;
        int index;
    } icons[] = {{kSearchX, tr("Search"), kSearchItem}, {kSettingsX, tr("App settings"), kSettingsItem}};
    for (const auto &icon : icons)
    {
        const float focus = top_focus_[static_cast<std::size_t>(icon.index)].value;
        ui::focus_ring(list, {icon.x - kIcon * 0.5f, kTopY - kIcon * 0.5f, kIcon, kIcon},
                       kIcon * 0.5f, focus);
        list.circle(icon.x, kTopY, kIcon * 0.5f, ui::Color::rgb(0xffffff, 0.08f));
        if (icon.index == kSearchItem)
        {
            ui::icon_search(list, icon.x, kTopY, 26.0f, ui::color::text);
        }
        else
        {
            ui::icon_settings(list, icon.x, kTopY, 26.0f, ui::color::text);
            if (update_badge())
            {
                // An update is waiting under App settings.
                list.circle(icon.x + 19.0f, kTopY - 19.0f, 8.5f, ui::color::night1);
                list.circle(icon.x + 19.0f, kTopY - 19.0f, 6.5f, ui::color::action_hi);
            }
        }
        if (focus > 0.01f)
        {
            list.push_opacity(focus);
            const float width = type.regular.measure(icon.label, 15.0f) + 26.0f;
            const ui::Rect tag{
                std::min(icon.x - width * 0.5f, 1920.0f - ui::kGutter - width + 28.0f),
                kTopY + kIcon * 0.5f + 11.0f, width, 28.0f};
            list.rounded_rect(tag, 14.0f, ui::Color::rgb(0x141414, 0.92f));
            ui::text(list, type.regular, icon.label, tag.cx(), tag.cy() + 5.4f, 15.0f,
                     ui::color::text, ui::Align::center);
            list.pop_opacity();
        }
    }
}

// The offline screen shows once Orbit has had time to answer, or as soon as an
// attempt to start it has ended.
bool App::status_offline(const Snapshot &state) const
{
    return state.link == Link::offline && !starting_now(state) &&
           (status_age_ > kOpeningGrace || state.starting != Starting::idle);
}

void App::draw_status(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const ui::Type &type = ctx_.type;
    const Snapshot &state = ctx_.store.state();
    const bool offline = status_offline(state);
    // The opening screen is the same in every look: the large mark over the aurora.
    const float size = 400.0f;
    constexpr float kMarkY = 340.0f; // the mark's centre; the texts sit below it
    const float x = 960.0f - size * 0.5f;
    const float y = kMarkY - size * 0.5f;
    // A breathing blue glow behind the mark, and the light running round its ring.
    const float breathe = 0.5f + 0.5f * std::sin(ctx_.clock * 2.6f);
    list.glow({x + 40.0f, y + 40.0f, size - 80.0f, size - 80.0f}, size * 0.5f, 120.0f,
              ui::Color::rgb(0xffffff, (offline ? 0.18f : 0.30f) + 0.16f * breathe));
    const float in = std::clamp(ctx_.clock / 0.7f, 0.0f, 1.0f);
    list.push_opacity(in);
    list.push_transform(0.88f + 0.12f * in, 960.0f, kMarkY, 0.0f, 0.0f);
    if (ctx_.loader_mark != 0)
        list.image(offline ? ctx_.mark : ctx_.loader_mark, {x, y, size, size}, hui::gfx::kFullUv,
                   ui::Color::rgb(0xffffff, offline ? 0.55f : 1.0f));
    if (!offline)
        mark::draw_light(list, x, y, size, std::fmod(ctx_.clock / 2.4f, 1.0f));
    list.pop_transform();
    list.pop_opacity();

    if (!offline)
    {
        if (starting_now(state))
        {
            ui::text(list, type.light, tr("Starting Orbit\xE2\x80\xA6"), 960.0f, 590.0f, 44.0f,
                     ui::color::text, ui::Align::center);
            ui::text(list, type.regular,
                     tr("Orbit keeps running in the background after you close this app."), 960.0f,
                     646.0f, 26.0f, ui::color::text2, ui::Align::center);
            return;
        }
        ui::text(list, type.light, tr("Opening Orbit Store\xE2\x80\xA6"), 960.0f, 590.0f, 44.0f,
                 ui::color::text, ui::Align::center);
        return;
    }
    const char *title = state.starting == Starting::failed ? tr("Orbit didn\xE2\x80\x99t start")
                                                           : tr("Orbit isn\xE2\x80\x99t running");
    const char *body = tr("Start Orbit from your payload manager. This screen reconnects by itself.");
    if (state.starting == Starting::no_loader)
        body = tr("Start Orbit from your payload manager, or run your jailbreak again so this app "
               "can start it.");
    else if (state.starting == Starting::failed)
        body = tr("Check that Orbit is set up in your payload manager, then try again.");
    ui::text(list, type.light, title, 960.0f, 590.0f, 52.0f, ui::color::text, ui::Align::center);
    ui::text(list, type.regular, body, 960.0f, 646.0f, 26.0f, ui::color::text2, ui::Align::center);
    // The connection error itself stays as the system reports it.
    const std::string detail =
        !state.start_detail.empty()
            ? state.start_detail
            : tr("Waiting for Orbit on this console ({detail})",
                 {{"detail", state.link_error.empty() ? std::string(tr("no answer"))
                                                      : state.link_error}});
    ui::text(list, type.regular, detail, 960.0f, 694.0f, 21.0f, ui::color::text3,
             ui::Align::center);
    if (ctx_.store.can_start())
    {
        const char *label = tr("Try again");
        const float width = ui::button_width(type, label);
        ui::button(list, type, {960.0f - width * 0.5f, 760.0f, width, ui::kButtonHeight}, label,
                   ui::ButtonKind::primary, 1.0f);
    }
}

void App::draw_blocked(hui::gfx::DrawList &list) const
{
    const ui::Type &type = ctx_.type;
    const Snapshot &state = ctx_.store.state();
    const bool setup = state.setup_needed() && !state.sources.acknowledged;
    const char *title = setup                           ? tr("Finish setting up Orbit")
                        : state.sources.enabled.empty() ? tr("No sources enabled")
                                                        : tr("No games from your selected sources");
    const char *body = setup ? tr("Choose where Orbit gets your downloads and accept the download "
                               "notice. It takes a moment, and you can change it anytime.")
                             : tr("Choose another source in App settings. This screen updates by "
                               "itself.");
    if (look::is(look::Interface::panorama))
    {
        // Panorama asks with a message band over the section's title.
        pano::draw_header(ctx_, list, pano::lower(tr(section_title(section()))), 0.0f);
        pano::Band band;
        band.kicker = setup ? tr("Welcome to Orbit") : std::string();
        band.title = title;
        band.body = body;
        band.buttons = {pano::button(tr("Choose sources"))};
        band.focus = area_ == Area::content ? 0 : -1;
        pano::draw_band(ctx_, list, band, 1.0f);
        return;
    }
    if (look::is(look::Interface::cinema))
    {
        // Cinema asks with a title card.
        cn::backdrop(ctx_, list, nullptr, cn::Veil::dark);
        cn::Title card;
        card.kicker = setup ? tr("Welcome to Orbit") : std::string();
        card.title = title;
        card.body = body;
        cn::Button choose;
        choose.label = tr("Choose sources");
        choose.primary = true;
        choose.focus = area_ == Area::content;
        card.buttons = {choose};
        cn::draw_title(ctx_, list, card, 1.0f);
        return;
    }
    if (look::is(look::Interface::crossbar))
    {
        // Crossbar asks over the bar, in a band.
        xb::draw_bar(ctx_, list, xb::sections(ctx_, update_badge()), section());
        xb::Band band;
        band.kicker = setup ? tr("Welcome to Orbit") : std::string();
        band.title = title;
        band.body = body;
        band.buttons = {tr("Choose sources")};
        band.focus = area_ == Area::content ? 0 : -1;
        xb::draw_band(ctx_, list, band, 1.0f);
        return;
    }
    ui::text(list, type.light, title, ui::kGutter, 380.0f, 60.0f, ui::color::text);
    ui::paragraph(list, type.regular, body, ui::kGutter, 446.0f, 27.0f, 980.0f, 40.0f,
                  ui::color::text2, 3);
    ui::button(list, type,
               {ui::kGutter, 560.0f, ui::button_width(type, tr("Choose sources")), ui::kButtonHeight},
               tr("Choose sources"), ui::ButtonKind::primary, area_ == Area::content ? 1.0f : 0.0f);
}

void App::draw_game_header(hui::gfx::DrawList &list) const
{
    // A game page is its own screen: Back and the brand, no tabs or tools.
    const ui::Type &type = ctx_.type;
    list.circle(ui::kGutter + kIcon * 0.5f, kTopY, kIcon * 0.5f, ui::Color::rgb(0xffffff, 0.08f));
    ui::icon_back(list, ui::kGutter + kIcon * 0.5f, kTopY, 24.0f, ui::color::text);
    const float x = ui::kGutter + kIcon + 36.0f;
    if (ctx_.mark != 0)
        list.image(ctx_.mark, {x - 2.0f, kTopY - 22.0f, 44.0f, 44.0f}, hui::gfx::kFullUv,
                   ui::Color::rgb(0xffffff));
    const float name =
        ui::text(list, type.regular, kBrand, x + 54.0f, kTopY + 8.0f, 22.0f, ui::color::text);
    const ui::Rect beta{x + 54.0f + name + 14.0f, kTopY - 12.5f, beta_width(type), 25.0f};
    list.bordered_rect(beta, 4.0f, ui::Color{0, 0, 0, 0}, 1.0f, ui::Color::rgb(0xffffff, 0.3f));
    ui::text(list, type.semibold, tr("BETA"), beta.cx(), kTopY + 4.7f, 13.0f, ui::color::text,
             ui::Align::center);
}

void App::draw_backdrop_look(Frame &frame) const
{
    const Snapshot &state = ctx_.store.state();
    hui::gfx::DrawList &scene = frame.scene;
    const Game *now = backdrop_.empty() ? nullptr : state.game(backdrop_);
    const Game *before = previous_backdrop_.empty() ? nullptr : state.game(previous_backdrop_);
    frame.backdrop.mode = hui::gfx::BackdropMode::gradient;
    frame.backdrop.params[2] = 0.0f;
    frame.backdrop.time = ctx_.clock;
    switch (look::current())
    {
    case look::Interface::panorama:
        frame.backdrop.colors[0] = pano::kPage;
        frame.backdrop.colors[1] = pano::kPage;
        // A game page shows its art a little stronger than the sections do.
        if (ctx_.appearance != 0)
            frame.backdrop = appearance::backdrop(ctx_.appearance, ctx_.clock);
        else
            scene.rounded_rect({0, 0, 1920, 1080}, 0.0f, pano::kPage);
        if (look::prefs().art_behind)
        {
            // The design's strengths: a game .3, Browse .16, Library .18, the rest .2.
            const float strength = game_open_                     ? 0.3f
                                   : section() == section::browse  ? 0.16f
                                   : section() == section::library ? 0.18f
                                                                   : 0.2f;
            if (before != nullptr && backdrop_in_.value < 1.0f)
                pano::draw_backdrop(ctx_, scene, before, strength * (1.0f - backdrop_in_.value));
            if (now != nullptr)
                pano::draw_backdrop(ctx_, scene, now, strength * backdrop_in_.value);
            scene.gradient_rect({0, 0, 1920, 1080}, 0.0f, pano::kPage.with_alpha(0.2f),
                                pano::kPage.with_alpha(0.9f));
        }
        break;
    case look::Interface::crossbar:
    {
        // The ribbons over the focused game's colours on Search, Discover,
        // Browse and a game's page, as the browser version and Zero; the
        // time of day's elsewhere.
        const int at = section();
        const bool tinted = game_open_ || (!settings_open_ && (at == section::search || at == section::discover || at == section::browse));
        if (ctx_.appearance != 0)
        {
            // A background chosen in Settings replaces the ribbons.
            frame.backdrop = appearance::backdrop(ctx_.appearance, ctx_.clock);
            break;
        }
        xb::backdrop(ctx_, frame.backdrop, tinted ? now : nullptr, tinted ? before : nullptr, backdrop_in_.value);
        if (tinted)
            xb::art_wash(ctx_, scene, now, before, backdrop_in_.value);
        break;
    }
    case look::Interface::cinema:
        // Black; each screen lays its game's art over it.
        frame.backdrop.colors[0] = ui::Color::rgb(0x000000);
        frame.backdrop.colors[1] = ui::Color::rgb(0x000000);
        if (ctx_.appearance != 0)
            frame.backdrop = appearance::backdrop(ctx_.appearance, ctx_.clock);
        break;
    default:
        // The background chosen in Settings > Appearance (black by default).
        frame.backdrop = appearance::backdrop(ctx_.appearance, ctx_.clock);
        break;
    }
}

void App::draw_notice(hui::gfx::DrawList &list) const
{
    const ui::Type &type = ctx_.type;
    const char *label = tr("New: choose how Orbit looks in App settings \xE2\x86\x92 Appearance");
    const float alpha = std::clamp(notice_time_ / 0.3f, 0.0f, 1.0f) *
                        std::clamp((8.0f - notice_time_) / 0.3f, 0.0f, 1.0f);
    const float width = type.regular.measure(label, 23.0f) + 104.0f;
    const ui::Rect pill{960.0f - width * 0.5f, 1080.0f - 110.0f - 72.0f, width, 72.0f};
    list.push_opacity(alpha);
    list.shadow({pill.x, pill.y + 14.0f, pill.w, pill.h}, 36.0f, 40.0f, ui::Color::rgb(0x000000, 0.5f));
    list.bordered_rect(pill, 36.0f, ui::Color::rgb(0x141414, 0.88f), 1.0f,
                       ui::Color::rgb(0xffffff, 0.12f));
    // A palette: a white disc with four dots of paint.
    const float cx = pill.x + 38.0f;
    list.circle(cx, pill.cy(), 20.0f, ui::Color::rgb(0xffffff));
    const float dots[4][2] = {{-7.0f, -5.0f}, {0.0f, -9.0f}, {7.0f, -5.0f}, {-6.0f, 4.0f}};
    for (const auto &d : dots)
        list.circle(cx + d[0], pill.cy() + d[1], 2.6f, ui::Color::rgb(0x000000));
    ui::text(list, type.regular, label, pill.x + 74.0f, pill.cy() + 8.0f, 23.0f, ui::color::text);
    list.pop_opacity();
}

std::vector<hui::ui::Hint> App::current_hints() const
{
    std::vector<hui::ui::Hint> items;
    if (look::pending())
        items = {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Switch back")}};
    else if (provider_open_)
        items = {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Back")}};
    else if (area_ == Area::top && look::is(look::Interface::cinema) && !game_open_)
        items = {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Back")},
                 {hui::ui::Button::l1, tr("Tabs"), hui::ui::Button::r1}};
    else if (settings_open_)
        items = settings_.hints();
    else if (game_open_)
        items = game_.hints();
    else if (area_ == Area::top)
        items = {{hui::ui::Button::cross, tr("Select")},
                 {hui::ui::Button::l1, tr("Tabs"), hui::ui::Button::r1}};
    else if (content_blocked() && !look::is(look::Interface::store))
        items = {{hui::ui::Button::cross, tr("Choose sources")}};
    else if (content_blocked())
        items = {{hui::ui::Button::cross, tr("Choose sources")},
                 {hui::ui::Button::l1, tr("Tabs"), hui::ui::Button::r1}};
    else
        items = screen().hints();
    if (notice_time_ > 0.0f && !look::pending())
        items.push_back({hui::ui::Button::options, tr("Open App settings")});
    return items;
}

void App::draw_controls(Frame &frame) const
{
    // Every button, once, in the interface's own words: Crossbar's band,
    // Cinema's title card, Panorama's message band.
    hui::gfx::DrawList &list = frame.overlay;
    const float in = controls_in_.value;
    using hui::ui::Button;
    if (look::is(look::Interface::crossbar))
    {
        xb::Band band;
        band.title = tr("Controls");
        band.controls = {{Button::l1, tr("Switch category"), Button::r1}, {Button::cross, tr("Open or choose")},
                         {Button::circle, tr("Back")},                     {Button::options, tr("Actions for the selection")},
                         {Button::triangle, tr("Favourite")},              {Button::square, tr("Search")},
                         {Button::l2, tr("Page up and down"), Button::r2}};
        band.buttons = {tr("Close")};
        band.focus = 0;
        xb::draw_band(ctx_, list, band, in);
    }
    else if (look::is(look::Interface::cinema))
    {
        cn::Title card;
        card.kicker = tr("Controls");
        card.title = tr("Every button, once");
        card.controls = {{Button::l1, tr("Change chapter or collection"), Button::r1}, {Button::cross, tr("The main action")},
                         {Button::circle, tr("Back, one level at a time")},           {Button::square, tr("Details, filters or delete")},
                         {Button::triangle, tr("Favourite")},                         {Button::options, tr("Every action for this item")},
                         {Button::l2, tr("Jump a letter or a page"), Button::r2}};
        cn::Button close;
        close.label = tr("Close");
        close.primary = true;
        close.focus = true;
        card.buttons = {close};
        cn::draw_title(ctx_, list, card, in);
    }
    else
    {
        pano::Band band;
        band.kicker = tr("Controls");
        band.title = tr("Every button, once");
        band.controls = {{Button::l1, tr("Next or previous section"), Button::r1}, {Button::cross, tr("Open or choose")},
                         {Button::circle, tr("Back, one level at a time")},       {Button::square, tr("Details, filters or delete")},
                         {Button::triangle, tr("Favourite")},                     {Button::options, tr("Every action for this tile")},
                         {Button::l2, tr("Jump a letter or a group"), Button::r2}};
        band.buttons = {pano::button(tr("Close"))};
        band.focus = 0;
        pano::draw_band(ctx_, list, band, in);
    }
}

void App::draw_keep(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.overlay;
    const ui::Type &type = ctx_.type;
    const float in = keep_in_.value;
    const look::Interface now = look::current();
    const std::string name = tr(look::title(now));
    const std::string previous = tr(look::title(look::previous()));
    const int seconds = static_cast<int>(std::ceil(look::seconds_left(ctx_.clock)));
    const std::string title = tr("Keep this interface?");
    const std::string body = tr("Orbit switches back to {name} in {seconds} s if you don\xE2\x80\x99t answer.",
                                {{"name", previous}, {"seconds", std::to_string(seconds)}});
    const std::string keep = tr("Keep {name}", {{"name", name}});
    const std::string back = tr("Switch back");
    list.push_opacity(in);
    switch (now)
    {
    case look::Interface::panorama:
    {
        // Panorama's message band, as every decision there is asked.
        list.pop_opacity();
        pano::Band question;
        question.title = title;
        question.body = body;
        question.buttons = {pano::button(keep), pano::button(back)};
        question.focus = keep_button_;
        pano::draw_band(ctx_, list, question, in);
        list.push_opacity(in);
        break;
    }
    case look::Interface::crossbar:
    {
        // Crossbar's band, as every question there is asked.
        list.pop_opacity();
        xb::Band question;
        question.title = title;
        question.body = body;
        question.buttons = {keep, back};
        question.focus = keep_button_;
        xb::draw_band(ctx_, list, question, in);
        list.push_opacity(in);
        break;
    }
    case look::Interface::cinema:
    {
        // Cinema's title card, as every decision there is asked.
        list.pop_opacity();
        cn::Title question;
        question.title = title;
        question.body = body;
        cn::Button keep_button;
        keep_button.label = keep;
        keep_button.primary = true;
        keep_button.focus = keep_button_ == 0;
        cn::Button back_button;
        back_button.label = back;
        back_button.focus = keep_button_ == 1;
        question.buttons = {keep_button, back_button};
        cn::draw_title(ctx_, list, question, in);
        list.push_opacity(in);
        break;
    }
    default:
    {
        list.rounded_rect({0, 0, 1920, 1080}, 0.0f, ui::Color::rgb(0x000000, 0.45f));
        const float width = 900.0f;
        const std::vector<std::string> lines = type.regular.font->wrap(body, 22.0f, width - 88.0f);
        const float height = 40.0f + 46.0f + 14.0f + 33.0f * static_cast<float>(lines.size()) + 30.0f + 62.0f + 40.0f;
        const ui::Rect panel{960.0f - width * 0.5f, 540.0f - height * 0.5f, width, height};
        list.shadow({panel.x, panel.y + 30.0f, panel.w, panel.h}, 28.0f, 80.0f, ui::Color::rgb(0x000000, 0.6f));
        list.bordered_rect(panel, 28.0f, ui::Color::rgb(0x141414, 0.94f), 1.0f, ui::Color::rgb(0xffffff, 0.1f));
        ui::text(list, type.medium, title, panel.x + 44.0f, panel.y + 40.0f + 38.0f, 38.0f, ui::color::text);
        float y = panel.y + 40.0f + 46.0f + 14.0f + 22.0f;
        for (const std::string &line : lines)
        {
            ui::text(list, type.regular, line, panel.x + 44.0f, y, 22.0f, ui::color::text2);
            y += 33.0f;
        }
        float x = panel.x + 44.0f;
        for (int i = 0; i < 2; ++i)
        {
            const std::string &label = i == 0 ? keep : back;
            const float w = ui::button_width(type, label);
            ui::button(list, type, {x, y + 8.0f, w, ui::kButtonHeight}, label,
                       i == 0 ? ui::ButtonKind::light : ui::ButtonKind::secondary,
                       keep_button_ == i ? 1.0f : 0.0f);
            x += w + 16.0f;
        }
        break;
    }
    }
    list.pop_opacity();
}

void App::draw(Frame &frame) const
{
    // Cinema draws Store's Library, Files and Downloads in its own palette.
    ui::color::use_cinema(look::is(look::Interface::cinema));
    const Snapshot &state = ctx_.store.state();
    // The browser's body: navy to near-black, lit from the top left towards
    // #133070 (the shader adds its highlight, so it gets the difference).
    frame.backdrop.mode = hui::gfx::BackdropMode::gradient;
    frame.backdrop.colors[0] = ui::color::night1;
    frame.backdrop.colors[1] = ui::color::night0;
    frame.backdrop.colors[2] = ui::Color::rgb(0x101010);
    frame.backdrop.params[0] = 0.08f;
    frame.backdrop.params[1] = 0.0f;
    frame.backdrop.params[2] = 1.0f;
    frame.backdrop.time = ctx_.clock;

    hui::gfx::DrawList &scene = frame.scene;
    const bool ready = state.have_system && state.link != Link::connecting &&
                       (state.have_catalog || state.setup_needed());
    if (!ready)
    {
        // The opening and offline screens always show the aurora, whatever the look: the chosen
        // background is only known once the download service has answered.
        static const std::size_t aurora = appearance::find("aurora");
        frame.backdrop = appearance::backdrop(aurora, ctx_.clock);
        draw_status(frame);
        return;
    }

    if (!look::is(look::Interface::store))
    {
        draw_backdrop_look(frame);
        if (settings_open_)
            settings_.draw(frame, !provider_open_);
        else if (game_open_)
            game_.draw(frame, !provider_open_);
        else if (content_blocked())
            draw_blocked(scene);
        else
            screen().draw(frame, !provider_open_);
        // Panorama's tray: on every page but Discover and Downloads (which show
        // downloads themselves) and the keyboard.
        if (look::is(look::Interface::panorama) &&
            (game_open_ || (section() != section::discover && section() != section::downloads && !screen().hints_high())))
            pano::draw_tray(ctx_, scene, !game_open_ && section() == section::settings && update_badge());
        if (look::is(look::Interface::crossbar))
            xb::draw_header(ctx_, scene, tab_ == tab::addons);
        if (look::is(look::Interface::cinema))
            cn::chapters(ctx_, scene, section(), update_badge(), settings_open_ || game_open_ || !screen().hints_high(),
                         area_ == Area::top && !game_open_ && !provider_open_ && !look::pending() && !controls_open_);
    }
    else
    {
    // The background chosen in Settings > Appearance; the default keeps Store's navy gradient.
    if (ctx_.appearance != 0)
        frame.backdrop = appearance::backdrop(ctx_.appearance, ctx_.clock);
    // ---- art behind everything, crossfading between games ----
    const bool discover_art = !game_open_ && tab_ == tab::discover;
    const float art_height = discover_art ? 800.0f : 1080.0f;
    if (backdrop_in_.value < 1.0f && !previous_backdrop_.empty())
    {
        if (const Game *game = state.game(previous_backdrop_))
            draw_backdrop_art(ctx_, scene, *game, 1.0f - backdrop_in_.value, art_height);
    }
    if (!backdrop_.empty())
    {
        if (const Game *game = state.game(backdrop_))
            draw_backdrop_art(ctx_, scene, *game, backdrop_in_.value, art_height);
    }
    // The veils are always there, as the web's .backdrop is: with no art
    // behind them they darken the navy page.
    if (game_open_ || discover_art)
        draw_veils(scene, discover_art);

    if (settings_open_)
    {
        settings_.draw(frame, !provider_open_);
        draw_game_header(scene);
    }
    else if (game_open_)
    {
        game_.draw(frame, !provider_open_);
        draw_game_header(scene);
    }
    else
    {
        if (content_blocked())
            draw_blocked(scene);
        else
            screen().draw(frame, area_ == Area::content && !settings_open_ && !provider_open_);
        draw_top_bar(scene);
    }
    }

    hui::gfx::DrawList &overlay = frame.overlay;

    // ---- banners and the toast ----
    const ui::Type &type = ctx_.type;
    const char *banner = nullptr;
    const bool restarting =
        state.restart == Restart::stopping || state.restart == Restart::starting;
    if (restarting && !settings_open_)
        banner = tr("Restarting the download service\xE2\x80\xA6");
    else if (state.link == Link::offline && !restarting)
        banner = tr("Connection lost. Reconnecting to Orbit\xE2\x80\xA6");
    else if (!state.system.state_healthy)
        banner = tr("Orbit cannot save its queue. Check free space on the console\xE2\x80\x99s "
                 "internal storage.");
    if (look::is(look::Interface::panorama))
    {
        // Solid strips across the top: amber for problems (a lost connection
        // runs its dots), blue for news, green for done, magenta for favourites.
        float top = 0.0f;
        if (banner != nullptr)
        {
            const bool lost = state.link == Link::offline && !restarting;
            const float h = pano::draw_notice(ctx_, overlay, top, restarting ? icons::Icon::info : lost ? icons::Icon::wifi : icons::Icon::warn,
                                              restarting ? look::accent_color() : pano::kAmber, banner);
            if (lost)
                pano::draw_dots(ctx_, overlay, 148.0f + type.semibold.measure(banner, 23.0f) + 14.0f, top + h * 0.5f, 7.0f,
                                ui::Color::rgb(0x000000));
            top += h + 3.0f;
        }
        if (toast_time_ > 0.0f && !toast_.empty())
        {
            const float alpha = std::min(1.0f, toast_time_ / 0.3f) * std::min(1.0f, (toast_length_ - toast_time_) / 0.2f);
            const bool failed = toast_ == tr("The browser could not be opened.");
            overlay.push_opacity(std::clamp(alpha, 0.0f, 1.0f));
            pano::draw_notice(ctx_, overlay, top,
                              toast_update_      ? icons::Icon::update
                              : toast_favourite_ ? icons::Icon::heart_fill
                              : failed           ? icons::Icon::warn
                                                 : icons::Icon::check,
                              toast_update_      ? look::accent_color()
                              : toast_favourite_ ? pano::kMagenta
                              : failed           ? pano::kAmber
                                                 : pano::kGreen,
                              toast_);
            overlay.pop_opacity();
        }
    }
    else if (look::is(look::Interface::cinema))
    {
        // Cinema's notices grow out of the capsule; a lost connection is a
        // capsule of its own across the top.
        float top = 120.0f;
        if (banner != nullptr && state.link == Link::offline && !restarting)
        {
            const float w = 26.0f + 24.0f + 14.0f + type.regular.measure(banner, 22.0f) + 26.0f;
            const ui::Rect pill{960.0f - w * 0.5f, 120.0f, w, 56.0f};
            overlay.bordered_rect(pill, 28.0f, ui::Color::rgb(0x000000, 0.5f), 1.0f, ui::Color::rgb(0xffffff, 0.14f));
            cn::spinner(ctx_, overlay, pill.x + 26.0f + 12.0f, pill.cy(), 24.0f, 3.0f);
            ui::text(overlay, type.regular, banner, pill.x + 26.0f + 24.0f + 14.0f, pill.cy() + 22.0f * 0.35f, 22.0f, cn::kInk);
            top += 56.0f + 18.0f;
        }
        else if (banner != nullptr)
            top += cn::draw_notice(ctx_, overlay, top, restarting ? icons::Icon::info : icons::Icon::warn, restarting ? cn::kInk : cn::kWarn,
                                   banner) +
                   18.0f;
        if (toast_time_ > 0.0f && !toast_.empty())
        {
            const float alpha = std::min(1.0f, toast_time_ / 0.3f) * std::min(1.0f, (toast_length_ - toast_time_) / 0.2f);
            const bool failed = toast_ == tr("The browser could not be opened.");
            overlay.push_opacity(std::clamp(alpha, 0.0f, 1.0f));
            cn::draw_notice(ctx_, overlay, top,
                            toast_update_      ? icons::Icon::update
                            : toast_favourite_ ? icons::Icon::heart_fill
                            : failed           ? icons::Icon::warn
                                               : icons::Icon::check,
                            toast_update_ || toast_favourite_ ? cn::kInk : failed ? cn::kWarn : cn::kOk, toast_);
            overlay.pop_opacity();
        }
    }
    else if (look::is(look::Interface::crossbar))
    {
        // Crossbar's notes, stacked at the top right.
        float top = 108.0f;
        if (banner != nullptr)
            top += xb::draw_toast(ctx_, overlay, top, restarting ? icons::Icon::info : icons::Icon::warn,
                                  restarting ? ui::Color::rgb(0xffffff) : ui::Color::rgb(0xffd27a), banner) +
                   18.0f;
        if (toast_time_ > 0.0f && !toast_.empty())
        {
            const float alpha = std::min(1.0f, toast_time_ / 0.3f) * std::min(1.0f, (toast_length_ - toast_time_) / 0.2f);
            const bool failed = toast_ == tr("The browser could not be opened.");
            overlay.push_opacity(std::clamp(alpha, 0.0f, 1.0f));
            xb::draw_toast(ctx_, overlay, top,
                           toast_update_      ? icons::Icon::update
                           : toast_favourite_ ? icons::Icon::heart_fill
                           : failed           ? icons::Icon::warn
                                              : icons::Icon::check,
                           toast_update_ || toast_favourite_ ? ui::Color::rgb(0xffffff)
                           : failed                          ? ui::Color::rgb(0xffd27a)
                                                             : ui::Color::rgb(0x8fe8c4),
                           toast_);
            overlay.pop_opacity();
        }
    }
    else if (banner != nullptr)
    {
        const float width = type.medium.measure(banner, 21.0f) + 64.0f;
        const ui::Rect pill{960.0f - width * 0.5f, 150.0f, width, 52.0f};
        overlay.rounded_rect(pill, 26.0f,
                             restarting ? ui::Color::rgb(0x242424, 0.95f)
                                        : ui::Color::rgb(0x242424, 0.92f));
        ui::text(overlay, type.medium, banner, pill.cx(), pill.cy() + 7.0f, 21.0f,
                 restarting ? ui::color::text : ui::color::bad, ui::Align::center);
    }
    if (toast_time_ > 0.0f && !toast_.empty() && look::is(look::Interface::store))
    {
        const float alpha = std::min(1.0f, toast_time_ / 0.3f) *
                            std::min(1.0f, (toast_length_ - toast_time_) / 0.2f);
        const float width = type.medium.measure(toast_, 23.0f) + 110.0f;
        const ui::Rect pill{960.0f - width * 0.5f, 900.0f, width, 64.0f};
        overlay.push_opacity(std::clamp(alpha, 0.0f, 1.0f));
        overlay.shadow({pill.x, pill.y + 10.0f, pill.w, pill.h}, 32.0f, 30.0f,
                       ui::Color::rgb(0x000000, 0.5f));
        overlay.rounded_rect(pill, 32.0f, ui::color::night3);
        if (toast_update_)
        {
            overlay.circle(pill.x + 40.0f, pill.cy(), 16.0f, ui::color::action);
            ui::icon_download(overlay, pill.x + 40.0f, pill.cy(), 18.0f, ui::color::text);
        }
        else if (toast_favourite_)
        {
            // A favourite added or removed: a heart, as every look shows it.
            overlay.circle(pill.x + 40.0f, pill.cy(), 16.0f, ui::color::action);
            icons::draw(overlay, icons::Icon::heart_fill, pill.x + 40.0f, pill.cy(), 18.0f, ui::color::night1, 2.0f);
        }
        else
        {
            overlay.circle(pill.x + 40.0f, pill.cy(), 16.0f, ui::color::ok);
            ui::icon_check(overlay, pill.x + 40.0f, pill.cy(), 16.0f, ui::color::night1, 3.0f);
        }
        ui::text(overlay, type.medium, toast_, pill.x + 70.0f, pill.cy() + 8.0f, 23.0f,
                 ui::color::text);
        overlay.pop_opacity();
    }

    if (provider_open_) draw_provider(frame);
    if (notice_time_ > 0.0f)
        draw_notice(overlay);
    if (keep_in_.value > 0.01f)
        draw_keep(frame);

    if (controls_in_.value > 0.01f)
        draw_controls(frame);

    // ---- controller hints, over a fade that lets content scroll beneath ----
    const std::vector<hui::ui::Hint> items = controls_open_ ? std::vector<hui::ui::Hint>{} : current_hints();
    if (items.empty())
        return;
    if (look::is(look::Interface::store))
    {
        // A pill of the popups' frosted glass behind the controls.
        frame.glass = true;
        ui::hints_pill(overlay, type, items, frame.glass_texture);
        return;
    }
    ui::HintStyle style;
    if (look::is(look::Interface::crossbar))
        style = {1024.0f, 19.0f, 32.0f, 12.0f};
    else if (look::is(look::Interface::cinema))
        style = cn::hint_style(!settings_open_ && !game_open_ && screen().hints_high() ? 450.0f : 34.0f);
    else if (look::is(look::Interface::panorama))
        style = pano::hint_style(!settings_open_ && !game_open_ && screen().hints_high() ? 450.0f : 30.0f);
    ui::hints(overlay, type, items, style);
}

} // namespace orbit
