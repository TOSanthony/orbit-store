// Orbit Store TV app - PC preview: runs the app headless and writes PNG pictures.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// usage: orbit_preview <assets dir> <output dir> [scenario|all] [width height]
//
// The same App, Store, ArtCache and drawing code as the console, rendered by
// Mesa's software OpenGL through surfaceless EGL, with a fixed 60 Hz clock.
// Data comes from the stand-in backend in fixture.cpp (invented games and
// generated art), or from a running Orbit backend when ORBIT_BACKEND is set
// to <ipv4>:<port> (for example the desktop preview container). Each
// scenario drives the app with scripted controller input and takes named
// pictures. Adapted from ps5-homebrew-ui's host/snapshot_main.cpp.

#include "fixture.hpp"

#include "gfx/gl_program.hpp"
#include "gfx/renderer.hpp"
#include "orbit/art.hpp"
#include "orbit/gl_textures.hpp"
#include "orbit/http.hpp"
#include "orbit/mark.hpp"
#include "orbit/screens.hpp"
#include "orbit/store.hpp"
#include "orbit/starter.hpp"
#include "orbit/i18n.hpp"
#include "orbit/look.hpp"
#include "ui/fonts.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>

#include "../third_party/stb/stb_image_write.h"

#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{

using hui::Action;
using hui::Direction;

bool open_context()
{
    auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display =
        get_platform_display != nullptr
            ? get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
            : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) ||
        !eglBindAPI(EGL_OPENGL_API))
        return false;
    const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION,
                                 4,
                                 EGL_CONTEXT_MINOR_VERSION,
                                 5,
                                 EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                 EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                 EGL_NONE};
    EGLContext context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attributes);
    return context != EGL_NO_CONTEXT &&
           eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
}

bool load_font(hui::gfx::Renderer &renderer, const std::string &path, hui::gfx::Font *font,
               hui::ui::FontRef *ref)
{
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return false;
    std::string data;
    char buffer[65536];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        data.append(buffer, n);
    std::fclose(file);
    if (!font->load(data))
    {
        std::fprintf(stderr, "cannot load font %s\n", path.c_str());
        return false;
    }
    ref->font = font;
    ref->texture = renderer.batch().create_font_texture(*font);
    return true;
}

struct Step
{
    float wait = 0.4f; // simulated seconds before the step acts
    std::uint32_t press = 0;
    Direction nav = Direction::none;
    const char *capture = nullptr; // picture taken after the wait, before the input
    std::function<void(orbit::App &)> act;
};

struct Scenario
{
    const char *name;
    orbit::host::FixtureMode mode;
    bool live_ok; // runs against a live backend too
    std::vector<Step> steps;
};

Step pause(float seconds, const char *capture = nullptr)
{
    Step step;
    step.wait = seconds;
    step.capture = capture;
    return step;
}

Step move(Direction direction, float seconds = 0.3f)
{
    Step step;
    step.wait = seconds;
    step.nav = direction;
    return step;
}

Step tap(Action action, float seconds = 0.3f)
{
    Step step;
    step.wait = seconds;
    step.press = hui::action_bit(action);
    return step;
}

Step run(float seconds, std::function<void(orbit::App &)> act)
{
    Step step;
    step.wait = seconds;
    step.act = std::move(act);
    return step;
}

// Stands in for the ELF loader: the scenario's name says what starting Orbit does.
class PreviewStarter final : public orbit::starter::Starter
{
  public:
    explicit PreviewStarter(orbit::starter::Outcome outcome) : outcome_(outcome)
    {
    }
    orbit::starter::Result start() override
    {
        orbit::starter::Result result;
        result.outcome = outcome_;
        result.detail = outcome_ == orbit::starter::Outcome::sent
                            ? "Sent Orbit 0.6.0 to the ELF loader."
                            : "No ELF loader is listening on port 9021.";
        return result;
    }

  private:
    orbit::starter::Outcome outcome_;
};

std::vector<Scenario> scenarios()
{
    using Mode = orbit::host::FixtureMode;
    std::vector<Scenario> list;
    // The Add-ons tab: the games that have add-ons, then one game's add-ons.
    list.push_back({"addons-grouped", Mode::dlc, false,
        {run(2.0f, [](orbit::App &app) { app.show_tab(orbit::tab::addons); }),
         pause(1.5f, "addons-grouped-games"), move(Direction::down), move(Direction::down), tap(Action::confirm), pause(1.0f, "addons-grouped-one"),
         tap(Action::back), pause(0.8f, "addons-grouped-back")}});
    list.push_back({"addons", Mode::normal, true,
        {run(2.0f, [](orbit::App &app) { app.show_tab(orbit::tab::addons); }),
         pause(2.0f, "addons-list"), move(Direction::down), move(Direction::right),
         pause(0.5f, "addons-navigation"),
         run(0.5f, [](orbit::App &app) { app.open_game("dlc-cuphead-ps4-932b3116db"); }),
         pause(1.0f, "addon-detail"), tap(Action::confirm), pause(0.5f, "addon-download-options"),
         tap(Action::back), tap(Action::back), pause(0.5f, "addons-back")}});
    list.push_back(
        {"opening", Mode::offline, false, {pause(0.9f, "opening"), pause(3.0f, "offline")}});
    list.push_back({"starting", Mode::offline, false, {pause(1.0f, "starting")}});
    list.push_back({"no-loader", Mode::offline, false, {pause(1.0f, "no-loader")}});
    // First run: choose sources in the app, then on to Discover.
    list.push_back({"setup",
                    Mode::setup,
                    true,
                    {pause(1.5f, "setup"), tap(Action::confirm), pause(0.8f, "setup-sources"),
                     move(Direction::right), tap(Action::confirm), move(Direction::down),
                     tap(Action::confirm), move(Direction::down), pause(0.8f, "setup-ready"),
                     tap(Action::confirm), pause(2.5f, "setup-done")}});
    list.push_back({"discover",
                    Mode::normal,
                    true,
                    {run(2.0f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); }),
                     pause(1.2f, "discover"), move(Direction::down), move(Direction::right),
                     move(Direction::right), pause(1.2f, "discover-rail"), move(Direction::down),
                     pause(1.0f, "discover-added"), move(Direction::down),
                     pause(1.0f, "discover-all"), move(Direction::up), move(Direction::up), move(Direction::up),
                     move(Direction::up), pause(0.6f, "top-bar")}});
    list.push_back({"favourite",
                    Mode::normal,
                    false,
                    {run(4.0f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); }),
                     move(Direction::down), move(Direction::up), pause(1.0f, "favourite-off"),
                     tap(Action::west), pause(2.5f, "favourite-on"), tap(Action::west), pause(2.5f, "favourite-again"),
                     run(1.5f, [](orbit::App &app) { app.open_game("ember-hollow"); }),
                     pause(1.2f, "favourite-game-off"),
                     run(1.5f, [](orbit::App &app) { app.open_game("tidewater"); }),
                     pause(1.2f, "favourite-game-on")}});
    list.push_back({"discover-heart",
                    Mode::normal,
                    false,
                    {run(4.0f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); }),
                     move(Direction::down), move(Direction::up), move(Direction::right),
                     pause(1.0f, "discover-heart-on"),
                     tap(Action::west), pause(2.5f, "discover-heart-off")}});
    list.push_back({"game-round",
                    Mode::normal,
                    false,
                    {run(4.0f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); }),
                     run(1.5f, [](orbit::App &app) { app.open_game("ember-hollow"); }),
                     move(Direction::right), pause(0.8f, "round-heart-off"),
                     move(Direction::right), pause(0.8f, "round-about"),
                     run(1.5f, [](orbit::App &app) { app.open_game("tidewater"); }),
                     move(Direction::right), pause(0.8f, "round-heart-on")}});
    list.push_back({"game", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.open_game("ember-hollow"); }),
         pause(1.2f, "game"), tap(Action::confirm), pause(0.8f, "game-options"),
         move(Direction::up), move(Direction::up), move(Direction::up), tap(Action::confirm),
         pause(0.8f, "game-source"), move(Direction::down), tap(Action::confirm),
         move(Direction::down), move(Direction::down), tap(Action::confirm),
         pause(0.8f, "game-drive-menu"), tap(Action::back), move(Direction::up), tap(Action::confirm),
         pause(0.8f, "game-delivery"), tap(Action::back), tap(Action::back),
         move(Direction::right), move(Direction::right), tap(Action::confirm), pause(0.8f, "game-about"),
         tap(Action::back), pause(0.8f, "game-about-closed")}});
    list.push_back({"game-browser", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.open_game("ironclad-skies"); }),
         pause(1.0f, "game-browser-hub"), tap(Action::confirm), pause(1.0f, "game-browser"),
         move(Direction::left), pause(0.6f, "game-source-notes"), move(Direction::down),
         pause(0.6f, "game-source-notes-scroll"), tap(Action::back), move(Direction::right),
         move(Direction::right), tap(Action::confirm), pause(0.6f, "game-browser-about")}});
    list.push_back({"game-ps4", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.open_game("copper-circuit-ps4"); }),
         pause(1.0f, "game-ps4"), tap(Action::confirm), pause(1.0f, "game-ps4-options"),
         tap(Action::back), move(Direction::right), move(Direction::right), tap(Action::confirm), pause(0.8f, "game-ps4-about")}});
    list.push_back({"game-library", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.open_game("quiet-harbour"); }),
         tap(Action::confirm), pause(1.0f, "game-library")}});
    list.push_back({"provider-browser", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.open_game("ironclad-skies"); }),
         tap(Action::confirm), pause(0.8f, "viking-options"), tap(Action::confirm),
         pause(1.5f, "viking-verification"), move(Direction::left), tap(Action::confirm),
         pause(1.2f, "viking-cancelled"), tap(Action::back), pause(0.8f, "viking-return")}});
    list.push_back({"debrid", Mode::debrid, false,
        {run(1.5f, [](orbit::App &app) { app.open_game("neon-courier"); }),
         tap(Action::confirm), move(Direction::up), move(Direction::left), tap(Action::confirm),
         pause(0.6f, "debrid-choice"), move(Direction::down), tap(Action::confirm),
         move(Direction::down), pause(0.8f, "debrid-ready"), tap(Action::confirm), pause(1.5f, "debrid-queued")}});
    list.push_back({"delete-cancelled",
                    Mode::normal,
                    false,
                    {run(1.5f, [](orbit::App &app) {
                         app.show_tab(orbit::tab::downloads);
                         app.downloads().show_job("job105");
                     }), pause(0.8f, "cancelled-download"),
                     move(Direction::right), tap(Action::confirm),
                     pause(0.8f, "delete-cancelled"), move(Direction::left),
                     move(Direction::left), tap(Action::confirm),
                     pause(0.8f, "forget-cancelled")}});
    list.push_back({"queued",
                    Mode::normal,
                    false,
                    {run(1.5f, [](orbit::App &app) { app.open_game("tidewater"); }),
                     pause(1.2f, "game-queued")}});
    list.push_back(
        {"download",
         Mode::normal,
         false,
         {run(1.5f, [](orbit::App &app) { app.open_game("neon-courier"); }),
          move(Direction::down, 0.6f), move(Direction::down), pause(0.6f, "download-ready"),
          tap(Action::confirm, 0.2f), pause(1.5f, "download-added")}});
    list.push_back(
        {"downloads",
         Mode::normal,
         true,
         {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::downloads); }),
          pause(1.2f, "downloads"), move(Direction::down), pause(0.8f, "downloads-row"),
          move(Direction::right), tap(Action::confirm), pause(0.8f, "downloads-dialog"),
          tap(Action::back), move(Direction::up), move(Direction::up), move(Direction::right),
          move(Direction::right), pause(0.8f, "downloads-failed")}});
    list.push_back({"downloads-finished", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) {
            app.show_tab(orbit::tab::downloads);
            app.downloads().show_job("job104");
        }), pause(1.0f, "downloads-finished"), tap(Action::west), pause(0.8f, "downloads-clear")}});
    list.push_back(
        {"browse",
         Mode::normal,
         true,
         {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::browse); }),
          pause(1.2f, "browse"), tap(Action::confirm),
          run(0.3f, [](orbit::App &app) { app.browse().set_query("o"); }),
          pause(0.9f, "browse-keyboard"), tap(Action::back), move(Direction::down),
          move(Direction::left), move(Direction::left), tap(Action::confirm),
          pause(0.8f, "browse-filters"), move(Direction::down), tap(Action::confirm),
          move(Direction::down), move(Direction::right), pause(1.0f, "browse-results")}});
    // The Store's focus ring: light spots turning around a tile, then the Search icon.
    list.push_back({"focus-spin", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::browse); }),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         pause(1.0f, "focus-spin-tile"), pause(0.7f, "focus-spin-tile-b"), move(Direction::up),
         move(Direction::up), move(Direction::up), move(Direction::up), move(Direction::up),
         move(Direction::right), move(Direction::right), move(Direction::right), move(Direction::right),
         move(Direction::right), move(Direction::right), move(Direction::right), move(Direction::right),
         move(Direction::right), move(Direction::right), move(Direction::right), move(Direction::right),
         pause(1.0f, "focus-spin-icon")}});
    // Saved filters in each look (ORBIT_INTERFACE): the list with a search set to save,
    // one focused, applied, then Save these filters naming them on the keyboard.
#define ORBIT_SAVED_SCENARIO(P)                                                                                       \
    list.push_back({P "browse-saved", Mode::normal, false,                                                           \
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::browse); app.browse().set_query("ne"); }),           \
         run(0.8f, [](orbit::App &app) { app.browse().show_saved_filters(); }), pause(0.8f, P "browse-saved"),        \
         move(Direction::down), pause(0.6f, P "browse-saved-focus"), tap(Action::confirm),                            \
         pause(1.0f, P "browse-saved-applied"),                                                                        \
         run(0.8f, [](orbit::App &app) { app.browse().set_query("ne"); app.browse().show_saved_filters(); }),          \
         tap(Action::confirm), pause(0.9f, P "browse-saved-name")}})
    ORBIT_SAVED_SCENARIO("");
    ORBIT_SAVED_SCENARIO("xb-");
    ORBIT_SAVED_SCENARIO("cn-");
    ORBIT_SAVED_SCENARIO("pn-");
#undef ORBIT_SAVED_SCENARIO
    // Crossbar (ORBIT_INTERFACE=crossbar): the list, its Actions menu, folders and search.
    list.push_back({"xb-discover", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); }),
         pause(1.0f, "xb-discover"), tap(Action::menu), pause(0.6f, "xb-discover-actions"), tap(Action::back),
         move(Direction::down), move(Direction::down), pause(0.8f, "xb-discover-down"),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         move(Direction::up), pause(0.8f, "xb-discover-folders"), tap(Action::confirm),
         pause(0.8f, "xb-discover-latest"), tap(Action::back), move(Direction::down), tap(Action::confirm),
         move(Direction::down), move(Direction::down), tap(Action::jump_next), pause(0.8f, "xb-discover-all"),
         tap(Action::back), pause(0.6f, "xb-discover-back")}});
    // Crossbar Discover on the real catalogue (ORBIT_BACKEND): New on Orbit's
    // 20 games, then the Latest releases and All games folders.
    {
        std::vector<Step> steps{run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); }),
                                pause(1.5f, "xb-live-1-top")};
        const auto down = [&steps](int count, const char *capture)
        {
            for (int i = 0; i < count; ++i)
                steps.push_back(move(Direction::down));
            steps.push_back(pause(1.5f, capture));
        };
        down(3, "xb-live-2-new");
        down(16, "xb-live-3-new-last");
        down(1, "xb-live-4-latest-folder");
        steps.push_back(tap(Action::confirm));
        steps.push_back(pause(1.5f, "xb-live-5-latest"));
        down(3, "xb-live-6-latest-down");
        steps.push_back(tap(Action::back));
        steps.push_back(pause(0.8f));
        down(1, "xb-live-7-all-folder");
        steps.push_back(tap(Action::confirm));
        steps.push_back(pause(1.5f, "xb-live-8-all"));
        down(3, "xb-live-9-all-down");
        // On to about the 100th game (the lists scroll on, with no pages).
        for (int i = 0; i < 19; ++i)
            steps.push_back(tap(Action::jump_next, 0.15f));
        steps.push_back(pause(1.5f, "xb-live-10-all-100"));
        list.push_back({"xb-discover-live", Mode::normal, true, steps});
    }
    // Cinema and Panorama Discover on the real catalogue, the same walk: New on
    // Orbit, then Latest releases, then All games (Down steps Cinema's rows;
    // R2 steps Panorama's groups).
    // `along`: how many to the right inside a group (Panorama's nine-tile
    // groups are four columns wide, so two keeps it in the group).
    const auto walk = [](const char *name, Step next_group, int along, const std::vector<const char *> &shots,
                         const std::vector<Step> &far)
    {
        std::vector<Step> steps{run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); }),
                                pause(1.5f, shots[0])};
        const auto right = [&steps](int count, const char *capture)
        {
            for (int i = 0; i < count; ++i)
                steps.push_back(move(Direction::right));
            steps.push_back(pause(1.5f, capture));
        };
        right(along, shots[1]);
        steps.push_back(next_group);
        steps.push_back(pause(1.5f, shots[2]));
        right(along, shots[3]);
        steps.push_back(next_group);
        steps.push_back(pause(1.5f, shots[4]));
        right(along, shots[5]);
        // On to about the 100th game (the lists scroll on, with no pages).
        steps.insert(steps.end(), far.begin(), far.end());
        steps.push_back(pause(1.5f, shots[6]));
        return Scenario{name, Mode::normal, true, steps};
    };
    list.push_back(walk("cn-discover-live", move(Direction::down), 3,
                        {"cn-live-1-top", "cn-live-2-new", "cn-live-3-latest", "cn-live-4-latest-right", "cn-live-5-all",
                         "cn-live-6-all-right", "cn-live-7-all-100"},
                        std::vector<Step>(19, tap(Action::jump_next, 0.15f))));
    list.push_back(walk("pn-discover-live", tap(Action::jump_next), 2,
                        {"pn-live-1-top", "pn-live-2-new", "pn-live-3-latest", "pn-live-4-latest-right", "pn-live-5-all",
                         "pn-live-6-all-right", "pn-live-7-all-100"},
                        std::vector<Step>(31, move(Direction::right, 0.15f))));
    list.push_back({"xb-browse", Mode::normal, true,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::browse); }),
         pause(1.0f, "xb-browse"), tap(Action::confirm), move(Direction::down), pause(0.6f, "xb-browse-filters"),
         tap(Action::confirm), move(Direction::down), pause(0.6f, "xb-browse-source"), tap(Action::confirm),
         tap(Action::back), tap(Action::back), pause(0.8f, "xb-browse-results"), move(Direction::down), pause(0.8f, "xb-browse-reset"),
         tap(Action::confirm), move(Direction::down), tap(Action::confirm), pause(0.8f, "xb-browse-favourites"),
         tap(Action::back), move(Direction::down), move(Direction::down), pause(0.8f, "xb-browse-games")}});
    list.push_back({"xb-search", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::browse); app.browse().open_search_section(); }),
         run(0.3f, [](orbit::App &app) { app.browse().set_query("ne"); }),
         pause(0.9f, "xb-search"), tap(Action::back), move(Direction::down), pause(0.9f, "xb-search-results")}});
    list.push_back({"xb-downloads", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::downloads); }),
         pause(1.0f, "xb-downloads"), tap(Action::confirm), pause(0.6f, "xb-downloads-actions"),
         move(Direction::down), tap(Action::confirm), pause(0.6f, "xb-downloads-cancel"), tap(Action::back),
         move(Direction::down), move(Direction::down), move(Direction::down), pause(0.8f, "xb-downloads-folders"),
         tap(Action::confirm), pause(0.8f, "xb-downloads-failed"), tap(Action::confirm),
         pause(0.6f, "xb-downloads-failed-actions"), tap(Action::back), tap(Action::back),
         move(Direction::down), tap(Action::confirm), pause(0.8f, "xb-downloads-finished"), tap(Action::confirm),
         pause(0.6f, "xb-downloads-finished-actions"), tap(Action::back), move(Direction::down),
         pause(0.6f, "xb-downloads-clear-row"), tap(Action::confirm), pause(0.6f, "xb-downloads-clear"), move(Direction::right), pause(0.6f, "xb-downloads-clear-keep"), tap(Action::back),
         tap(Action::back), move(Direction::down),
         tap(Action::confirm), tap(Action::confirm), pause(0.8f, "xb-downloads-cancelled")}});
    list.push_back({"xb-game", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); app.open_game("ember-hollow"); }),
         pause(1.0f, "xb-game"), move(Direction::down), tap(Action::confirm), pause(0.6f, "xb-game-version"),
         tap(Action::back), move(Direction::down), move(Direction::down), tap(Action::confirm), pause(0.6f, "xb-game-saveto"),
         tap(Action::back), move(Direction::down), move(Direction::down), tap(Action::confirm), pause(0.6f, "xb-game-about")}});
    // A pad in a choice menu: Down past its end, Left and Right, then Circle
    // closes the menu (the page stays), and Circle again leaves the page.
    list.push_back({"xb-game-pad", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); app.open_game("ember-hollow"); }),
         pause(1.0f), move(Direction::down), tap(Action::confirm), pause(0.6f, "pad-1-menu"),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         move(Direction::down), pause(0.6f, "pad-2-menu-down"), move(Direction::right), move(Direction::left),
         pause(0.6f, "pad-3-menu-sides"), tap(Action::back), pause(0.8f, "pad-4-menu-closed"), tap(Action::back),
         pause(0.8f, "pad-5-page-closed")}});
    list.push_back({"xb-game-viking", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); app.open_game("ironclad-skies"); }),
         pause(1.0f, "xb-game-viking"), move(Direction::down), move(Direction::down), move(Direction::down),
         move(Direction::down), move(Direction::down), pause(0.8f, "xb-game-notes")}});
    list.push_back({"xb-library", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::library); }),
         pause(1.2f, "xb-library"), move(Direction::down), move(Direction::down), move(Direction::down),
         move(Direction::down), move(Direction::down), pause(0.8f, "xb-library-games"), tap(Action::confirm),
         pause(0.8f, "xb-library-game"), move(Direction::down), tap(Action::confirm), pause(1.0f, "xb-library-copy"),
         tap(Action::back), tap(Action::back), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), pause(0.8f, "xb-library-end"), tap(Action::confirm),
         pause(0.8f, "xb-library-scan"), tap(Action::back), move(Direction::up), tap(Action::confirm),
         pause(1.2f, "xb-library-storage"), tap(Action::confirm), pause(0.8f, "xb-library-drive")}});
    list.push_back({"xb-download-recovery", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.open_settings(); }),
         tap(Action::confirm), tap(Action::back), move(Direction::down), tap(Action::confirm),
         move(Direction::down), move(Direction::down), pause(0.6f, "download-recovery"),
         tap(Action::confirm), pause(0.6f, "download-retry-count"),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         move(Direction::down), move(Direction::down), pause(0.6f, "download-retry-count-end"),
         tap(Action::back), move(Direction::down), tap(Action::confirm), pause(0.6f, "download-retry-delay")}});
    list.push_back({"xb-settings", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.open_settings(); }),
         pause(1.0f, "xb-settings"), tap(Action::confirm), pause(0.8f, "xb-settings-sources"), tap(Action::back),
         move(Direction::down), tap(Action::confirm), pause(0.8f, "xb-settings-storage"), tap(Action::confirm),
         pause(0.8f, "xb-settings-drive"), move(Direction::down), tap(Action::confirm), pause(0.8f, "xb-settings-saved"),
         move(Direction::down), tap(Action::confirm),
         pause(0.8f, "xb-settings-limit"), tap(Action::back), tap(Action::back), move(Direction::down),
         tap(Action::confirm), pause(0.8f, "xb-settings-notifications"), tap(Action::back), move(Direction::down),
         tap(Action::confirm), pause(0.8f, "xb-settings-pairing"), tap(Action::back), move(Direction::down),
         tap(Action::confirm), pause(0.8f, "xb-settings-catalogue"), tap(Action::back), move(Direction::down),
         tap(Action::confirm), pause(0.8f, "xb-settings-updates"), tap(Action::confirm), pause(0.8f, "xb-settings-tvapp"),
         tap(Action::back), move(Direction::down), tap(Action::confirm), pause(0.8f, "xb-settings-service"),
         tap(Action::back), tap(Action::back), move(Direction::down), tap(Action::confirm), pause(0.8f, "xb-settings-zero"),
         tap(Action::back), move(Direction::down), tap(Action::confirm), pause(0.8f, "xb-settings-appearance"),
         tap(Action::confirm), pause(0.8f, "xb-settings-interface"), move(Direction::down),
         pause(0.8f, "xb-settings-interface-preview"), tap(Action::back), tap(Action::back),
         move(Direction::down), tap(Action::confirm), pause(0.8f, "xb-settings-more")}});
    // Cinema (ORBIT_INTERFACE=cinema): the stage, the game's sheet and its views.
    list.push_back({"cn-navigation", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); }),
         move(Direction::up), pause(0.6f, "cn-navbar"), move(Direction::left),
         pause(0.6f, "cn-navbar-settings"),
         run(0.5f, [](orbit::App &app) { app.show_tab(orbit::tab::downloads); }),
         move(Direction::right), move(Direction::up), move(Direction::right),
         pause(0.6f, "cn-download-buttons"),
         run(0.5f, [](orbit::App &app) { app.show_tab(orbit::tab::library); }),
         move(Direction::right), move(Direction::up), move(Direction::right),
         pause(0.6f, "cn-library-buttons"),
         run(0.5f, [](orbit::App &app) { app.show_tab(orbit::tab::browse); }),
         tap(Action::west), move(Direction::down), move(Direction::down), move(Direction::down),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         pause(0.6f, "cn-browse-search")} });
    list.push_back({"cn-discover", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); }),
         pause(1.0f, "cn-discover"), move(Direction::right), move(Direction::right), move(Direction::right),
         pause(0.8f, "cn-discover-down"), tap(Action::menu), pause(0.6f, "cn-discover-actions"), tap(Action::back),
         move(Direction::down), pause(0.8f, "cn-discover-latest"), move(Direction::down), move(Direction::down),
         move(Direction::right), pause(0.8f, "cn-discover-all")}});
    list.push_back({"cn-game", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); app.open_game("ember-hollow"); }),
         pause(1.0f, "cn-game"), tap(Action::confirm), pause(0.6f, "cn-game-sheet"), move(Direction::right),
         pause(0.6f, "cn-game-version"), move(Direction::down), move(Direction::down), pause(0.6f, "cn-game-saveto"),
         tap(Action::back), pause(0.6f, "cn-game-closed"), move(Direction::right), move(Direction::right),
         tap(Action::confirm), pause(0.6f, "cn-game-about"), tap(Action::back), tap(Action::back),
         pause(0.6f, "cn-game-left")}});
    list.push_back({"cn-browse", Mode::normal, true,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::browse); }),
         pause(1.0f, "cn-browse"), tap(Action::west), pause(0.6f, "cn-browse-filters"), move(Direction::down),
         tap(Action::confirm), move(Direction::down), pause(0.6f, "cn-browse-source"), tap(Action::confirm),
         tap(Action::back), tap(Action::back), pause(0.8f, "cn-browse-results"), move(Direction::down), move(Direction::down),
         move(Direction::right), pause(0.8f, "cn-browse-down"), tap(Action::page_next), pause(0.8f, "cn-browse-favourites")}});
    list.push_back({"cn-search", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_section(orbit::section::search); app.browse().set_query("ne"); }),
         pause(1.0f, "cn-search"), tap(Action::back), pause(0.8f, "cn-search-results")}});
    list.push_back({"cn-downloads", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::downloads); }),
         pause(1.0f, "cn-downloads"), tap(Action::west), pause(0.6f, "cn-downloads-actions"), move(Direction::right),
         tap(Action::confirm), pause(0.6f, "cn-downloads-cancel"), tap(Action::back), tap(Action::page_next),
         pause(0.8f, "cn-downloads-failed"), tap(Action::page_next), pause(0.8f, "cn-downloads-finished"),
         move(Direction::up), move(Direction::right), move(Direction::right), move(Direction::right),
         pause(0.6f, "cn-downloads-clear-button"), tap(Action::confirm), pause(0.6f, "cn-downloads-clear"), tap(Action::back),
         move(Direction::down), tap(Action::page_next), pause(0.8f, "cn-downloads-cancelled")}});
    list.push_back({"cn-library", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::library); }),
         pause(1.2f, "cn-library"), tap(Action::west), pause(0.6f, "cn-library-game"), move(Direction::right),
         tap(Action::confirm), pause(0.8f, "cn-library-copy"), tap(Action::back), tap(Action::back), tap(Action::menu),
         pause(0.6f, "cn-library-search"), tap(Action::back), tap(Action::page_next), tap(Action::page_next),
         tap(Action::page_next), tap(Action::page_next), pause(1.2f, "cn-library-storage")}});
    list.push_back({"cn-settings", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.open_settings(); }),
         pause(1.0f, "cn-settings"), tap(Action::confirm), pause(0.6f, "cn-settings-sources"), tap(Action::back),
         move(Direction::down), tap(Action::confirm), tap(Action::confirm), pause(0.6f, "cn-settings-drive"),
         tap(Action::back), tap(Action::back), move(Direction::down), pause(0.6f, "cn-settings-notifications"),
         move(Direction::down), pause(0.6f, "cn-settings-pairing"),
         move(Direction::down), move(Direction::down), tap(Action::confirm), pause(0.6f, "cn-settings-updates"),
         tap(Action::confirm), pause(0.6f, "cn-settings-tvapp"), tap(Action::back), tap(Action::back),
         move(Direction::down), move(Direction::down), tap(Action::confirm), tap(Action::confirm),
         pause(0.6f, "cn-settings-interface")}});
    list.push_back({"cn-game-viking", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); app.open_game("ironclad-skies"); }),
         pause(1.0f, "cn-game-viking"), move(Direction::right), move(Direction::right), move(Direction::right),
         move(Direction::left), move(Direction::left), tap(Action::confirm), pause(0.6f, "cn-game-notes")}});
    // Panorama (ORBIT_INTERFACE=panorama): tile pages, the app bar, pickers and bands.
    list.push_back({"pn-discover", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); }),
         pause(1.0f, "pn-discover"), move(Direction::right), move(Direction::down), pause(0.8f, "pn-discover-down"),
         tap(Action::menu), pause(0.6f, "pn-discover-actions"), tap(Action::back), move(Direction::right),
         move(Direction::right), move(Direction::right), move(Direction::right), move(Direction::right),
         pause(0.8f, "pn-discover-latest"), move(Direction::right), move(Direction::right), move(Direction::right),
         move(Direction::right), move(Direction::right), move(Direction::right), pause(0.8f, "pn-discover-all"),
         tap(Action::jump_next), pause(0.8f, "pn-discover-letter")}});
    list.push_back({"pn-browse", Mode::normal, true,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::browse); }),
         pause(1.0f, "pn-browse"), tap(Action::west), move(Direction::down), pause(0.6f, "pn-browse-filters"),
         tap(Action::confirm), move(Direction::down), pause(0.6f, "pn-browse-source"), tap(Action::confirm),
         tap(Action::back), tap(Action::back), pause(0.8f, "pn-browse-results"), tap(Action::west), move(Direction::down), move(Direction::down),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         tap(Action::confirm), pause(0.6f, "pn-browse-sort"),
         tap(Action::back), tap(Action::back), move(Direction::right), move(Direction::right), move(Direction::right),
         move(Direction::right), move(Direction::right), move(Direction::right), pause(0.8f, "pn-browse-favourites")}});
    list.push_back({"pn-search", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_section(orbit::section::search); }), pause(0.8f, "pn-keyboard"),
         run(0.5f, [](orbit::App &app) { app.browse().set_query("ne"); }), pause(0.8f, "pn-search"), tap(Action::back),
         pause(0.8f, "pn-search-results"), run(0.5f, [](orbit::App &app) { app.browse().set_query("zzq"); }),
         pause(0.8f, "pn-search-empty")}});
    list.push_back({"pn-game", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); app.open_game("ember-hollow"); }),
         pause(1.0f, "pn-game"), move(Direction::right), pause(0.6f, "pn-game-using"), move(Direction::right),
         pause(0.6f, "pn-game-saveto"), move(Direction::right), pause(0.6f, "pn-game-version"), move(Direction::left),
         move(Direction::left), move(Direction::left), move(Direction::down), move(Direction::right), tap(Action::confirm),
         pause(0.8f, "pn-game-about")}});
    list.push_back({"pn-game-viking", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); app.open_game("ironclad-skies"); }),
         pause(1.0f, "pn-game-viking-top"), tap(Action::page_next), tap(Action::page_next), tap(Action::page_next),
         tap(Action::page_next), pause(0.8f, "pn-game-viking"), tap(Action::confirm), pause(0.8f, "pn-game-notes")}});
    list.push_back({"pn-downloads", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::downloads); }),
         pause(1.0f, "pn-downloads"), tap(Action::menu), pause(0.6f, "pn-downloads-actions"), move(Direction::down),
         tap(Action::confirm), pause(0.6f, "pn-downloads-cancel"), tap(Action::back), tap(Action::back), move(Direction::right),
         tap(Action::menu), pause(0.6f, "pn-downloads-order"), tap(Action::back), tap(Action::page_next),
         pause(0.8f, "pn-downloads-failed"), tap(Action::page_next), pause(0.8f, "pn-downloads-finished"),
         move(Direction::down), pause(0.6f, "pn-downloads-clear-tile"), tap(Action::confirm), pause(0.6f, "pn-downloads-clear"),
         tap(Action::back), tap(Action::page_next), pause(0.8f, "pn-downloads-cancelled"), move(Direction::right), move(Direction::down),
         tap(Action::confirm), pause(0.6f, "pn-downloads-delete")}});
    list.push_back({"pn-library", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::library); }),
         pause(1.2f, "pn-library"), tap(Action::west), pause(0.6f, "pn-library-search"), tap(Action::back),
         tap(Action::menu), pause(0.6f, "pn-library-actions"), tap(Action::back), move(Direction::right), move(Direction::right),
         pause(0.8f, "pn-library-game"), move(Direction::down), move(Direction::down), tap(Action::confirm),
         pause(0.8f, "pn-library-details"), tap(Action::back), move(Direction::left), move(Direction::left),
         move(Direction::left), move(Direction::left), tap(Action::menu), move(Direction::down), tap(Action::confirm),
         pause(1.0f, "pn-library-copy"), tap(Action::back),
         move(Direction::right), move(Direction::right), move(Direction::up), move(Direction::up), pause(1.2f, "pn-library-storage")}});
    list.push_back({"pn-settings", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.open_settings(); }),
         pause(1.0f, "pn-settings"), tap(Action::confirm), pause(0.6f, "pn-settings-sources"), tap(Action::back),
         tap(Action::page_next), tap(Action::confirm), tap(Action::confirm), pause(0.6f, "pn-settings-drive"), tap(Action::back),
         move(Direction::down), tap(Action::confirm), pause(0.6f, "pn-settings-limit"), tap(Action::back), tap(Action::back),
         tap(Action::page_next), pause(0.6f, "pn-settings-notifications"), tap(Action::page_next),
         pause(0.6f, "pn-settings-pairing"), tap(Action::page_next), tap(Action::page_next),
         tap(Action::confirm), pause(0.6f, "pn-settings-updates"), tap(Action::confirm), pause(0.6f, "pn-settings-tvapp"),
         tap(Action::back), tap(Action::back), tap(Action::page_next), tap(Action::page_next), tap(Action::confirm),
         pause(0.6f, "pn-settings-appearance"), tap(Action::confirm), pause(0.6f, "pn-settings-interface"),
         move(Direction::up), move(Direction::up), move(Direction::up), pause(0.6f, "pn-settings-interface-preview"),
         tap(Action::back), move(Direction::down), tap(Action::confirm), pause(0.6f, "pn-settings-accent"), tap(Action::back),
         tap(Action::back), tap(Action::page_next), tap(Action::confirm), pause(0.6f, "pn-settings-more")}});
    list.push_back({"pn-provider", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); app.open_game("ironclad-skies"); }),
         tap(Action::confirm), pause(1.5f, "pn-viking-verify"), move(Direction::left), tap(Action::confirm),
         pause(1.2f, "pn-viking-cancelled")}});
    list.push_back({"pn-keep", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); }),
         run(0.5f, [](orbit::App &) { orbit::look::preview(orbit::look::Interface::store);
                                      orbit::look::choose(orbit::look::Interface::panorama, 2.0f); }),
         pause(1.0f, "pn-keep")}});
    list.push_back({"cn-keep", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); }),
         run(0.5f, [](orbit::App &) { orbit::look::preview(orbit::look::Interface::store);
                                      orbit::look::choose(orbit::look::Interface::cinema, 2.0f); }),
         pause(1.0f, "cn-keep")}});
    list.push_back({"xb-keep", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); }),
         run(0.5f, [](orbit::App &) { orbit::look::preview(orbit::look::Interface::store);
                                      orbit::look::choose(orbit::look::Interface::crossbar, 2.0f); }),
         pause(1.0f, "xb-keep")}});
    // Update Orbit: both parts in one press. The fixture has no ELF loader, so the
    // download service stays saved for a restart.
    list.push_back({"update-orbit", Mode::updates, false,
        {run(2.5f, [](orbit::App &app) { app.open_settings(orbit::SettingsScreen::Section::updates, true); }),
         tap(Action::confirm), pause(1.5f, "settings-update-orbit")}});
    // Storage, scrolled to its last settings: installed packages, download
    // notifications and ntfy.
    list.push_back({"notifications", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.open_settings(orbit::SettingsScreen::Section::storage, true); }),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         pause(0.8f, "settings-notifications"), move(Direction::down), move(Direction::down),
         move(Direction::down), pause(0.8f, "settings-ntfy")}});
    // Appearance in the Store look: sounds, keyboard and the background.
    list.push_back({"appearance", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.open_settings(orbit::SettingsScreen::Section::appearance, true); }),
         pause(0.8f, "settings-appearance-top"), move(Direction::down), move(Direction::down),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         pause(0.8f, "settings-appearance-keyboard"), move(Direction::down), move(Direction::down),
         move(Direction::down), pause(0.8f, "settings-appearance-background")}});
    // A background picked in Store: the page behind everything changes.
    list.push_back({"appearance-pick", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.open_settings(orbit::SettingsScreen::Section::appearance, true); }),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         tap(Action::confirm), pause(0.6f, "appearance-pick-menu"), move(Direction::down), move(Direction::down),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         move(Direction::down), tap(Action::confirm), pause(1.0f, "appearance-pick-after")}});
    // A background picked in a look without a top bar (set ORBIT_INTERFACE).
    list.push_back({"look-background", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.open_settings(orbit::SettingsScreen::Section::appearance, true); }),
         move(Direction::down), move(Direction::down), move(Direction::down),
         tap(Action::confirm), pause(0.6f, "look-background-menu"), move(Direction::down), move(Direction::down), move(Direction::down),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         move(Direction::down), tap(Action::confirm), pause(1.0f, "look-background-waves")}});
    list.push_back({"look-background-pn", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.open_settings(orbit::SettingsScreen::Section::appearance, true); }),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         tap(Action::confirm), pause(0.6f, "look-background-menu"), move(Direction::down), move(Direction::down), move(Direction::down),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         move(Direction::down), tap(Action::confirm), pause(1.0f, "look-background-waves")}});
    list.push_back({"pn-updates", Mode::updates, false,
        {pause(2.5f, "pn-update-badge"),
         run(0.5f, [](orbit::App &app) { app.open_settings(orbit::SettingsScreen::Section::updates, true); }),
         pause(1.0f, "pn-settings-updates"), move(Direction::down), // past Update Orbit
         move(Direction::down), tap(Action::confirm), pause(0.8f, "pn-settings-service"),
         move(Direction::down), tap(Action::confirm), pause(0.8f, "pn-settings-restart"), move(Direction::left),
         tap(Action::confirm), pause(0.6f, "pn-settings-restarting")}});
    // Controls: Options where nothing uses it (a game page's tiles in Panorama, Settings' pivots elsewhere).
    list.push_back({"controls", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.open_settings(); }), tap(Action::menu), pause(0.8f, "controls")}});
    // Browse: two formats ticked at once, a tag, then Installed games hidden (each look
    // has its own: browse-ticks in Store, and xb-, cn- and pn-ticks).
    list.push_back({"browse-ticks", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::browse); }),
         move(Direction::down), move(Direction::left), tap(Action::confirm), move(Direction::down), tap(Action::confirm),
         move(Direction::down), tap(Action::confirm), pause(0.6f, "browse-formats-menu"), tap(Action::back),
         pause(0.8f, "browse-formats"), move(Direction::right), move(Direction::right), move(Direction::right),
         move(Direction::right), move(Direction::right), tap(Action::confirm), move(Direction::down), tap(Action::confirm),
         pause(0.6f, "browse-tags-menu"), tap(Action::back), move(Direction::right), tap(Action::confirm),
         pause(0.8f, "browse-hide-installed")}});
    list.push_back({"xb-ticks", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::browse); }),
         tap(Action::confirm), move(Direction::down), move(Direction::down), tap(Action::confirm), move(Direction::down),
         tap(Action::confirm), move(Direction::down), tap(Action::confirm), pause(0.6f, "xb-formats"), tap(Action::back),
         move(Direction::down), tap(Action::confirm), move(Direction::down), tap(Action::confirm), pause(0.6f, "xb-tags"),
         tap(Action::back), move(Direction::down), tap(Action::confirm), move(Direction::down), tap(Action::confirm),
         pause(0.6f, "xb-installed"), tap(Action::back), pause(0.8f, "xb-ticks-results")}});
    list.push_back({"cn-ticks", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::browse); }),
         tap(Action::west), move(Direction::down), move(Direction::down), tap(Action::confirm), move(Direction::down),
         tap(Action::confirm), move(Direction::down), tap(Action::confirm), pause(0.6f, "cn-formats"), tap(Action::back),
         move(Direction::down), tap(Action::confirm), move(Direction::down), tap(Action::confirm), pause(0.6f, "cn-tags"),
         tap(Action::back), move(Direction::down), tap(Action::confirm), move(Direction::down), tap(Action::confirm),
         pause(0.6f, "cn-installed")}});
    list.push_back({"pn-ticks", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::browse); }),
         tap(Action::west), move(Direction::down), move(Direction::down), tap(Action::confirm), move(Direction::down),
         tap(Action::confirm), move(Direction::down), tap(Action::confirm), pause(0.6f, "pn-formats"), tap(Action::back),
         move(Direction::down), tap(Action::confirm), move(Direction::down), tap(Action::confirm), pause(0.6f, "pn-tags"),
         tap(Action::back), move(Direction::down), tap(Action::confirm), move(Direction::down), tap(Action::confirm),
         pause(0.6f, "pn-installed")}});
    list.push_back({"browse-platform", Mode::normal, true,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::browse); }),
         move(Direction::down), move(Direction::right), move(Direction::right), move(Direction::right),
         tap(Action::confirm), pause(0.5f, "browse-platform-menu"), move(Direction::down), move(Direction::down),
         tap(Action::confirm), move(Direction::down), pause(0.8f, "browse-ps4"),
         tap(Action::confirm), pause(0.8f, "browse-ps4-details")}});
    list.push_back({"game-dlc",
                    Mode::dlc,
                    false,
                    {run(2.0f, [](orbit::App &app) { app.open_game_updates(2); }),
                     pause(1.5f, "game-dlc"), tap(Action::confirm), pause(1.5f, "game-dlc-expanded"),
                     move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
                     move(Direction::down), move(Direction::down), pause(1.0f, "game-dlc-bottom")}});
    list.push_back({"game-updates-in-downloads",
                    Mode::dlc,
                    false,
                    {run(2.0f, [](orbit::App &app) { app.open_game_updates(2); }),
                     tap(Action::confirm), pause(1.5f, "gu-dlc-after"),
                     run(0.5f, [](orbit::App &app) { app.show_tab(orbit::tab::downloads); }),
                     pause(1.5f, "gu-downloads")}});
    list.push_back({"game-updates-pick",
                    Mode::normal,
                    false,
                    {run(2.0f, [](orbit::App &app) { app.open_game_updates(1); }),
                     pause(1.0f, "pick-list"), move(Direction::down), move(Direction::down), move(Direction::down),
                     pause(1.0f, "pick-blocked-row")}});
    // The first row of the list in Crossbar and Panorama checks again (set ORBIT_INTERFACE).
    list.push_back({"look-game-updates", Mode::normal, false,
                    {run(2.0f, [](orbit::App &app) { app.open_game_updates(1); }),
                     pause(1.5f, "look-game-updates"), move(Direction::down), pause(0.8f, "look-game-updates-down")}});
    list.push_back({"game-updates",
                    Mode::normal,
                    false,
                    {run(2.0f, [](orbit::App &app) { app.open_game_updates(1); }),
                     pause(1.5f, "game-updates"), tap(Action::confirm), pause(1.5f, "game-updates-downloading"),
                     move(Direction::up), pause(0.8f, "game-updates-bar"), move(Direction::right), pause(0.8f, "game-updates-bar2"), move(Direction::down),
                     tap(Action::west), pause(1.2f, "game-updates-queue"), move(Direction::down),
                     pause(0.8f, "game-updates-queue-focus"), tap(Action::confirm), pause(0.8f, "game-updates-dequeued"),
                     tap(Action::jump_next), pause(0.8f, "game-updates-cleared"), move(Direction::up),
                     tap(Action::confirm), pause(1.5f, "game-updates-paused"), tap(Action::confirm),
                     pause(1.2f, "game-updates-resumed"), tap(Action::back), pause(1.2f, "game-updates-cancelled")}});
    list.push_back(
        {"library",
         Mode::normal,
         false,
         {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::library); }),
          pause(1.2f, "library"), move(Direction::down), move(Direction::down),
          move(Direction::left), move(Direction::left), move(Direction::left),
          pause(0.8f, "library-grid"), tap(Action::confirm), pause(0.8f, "library-details"),
          move(Direction::right), tap(Action::confirm), pause(1.2f, "library-copy"),
          tap(Action::back), tap(Action::back), move(Direction::up), move(Direction::up),
          tap(Action::confirm), pause(1.2f, "library-storage")}});
    // Library → Files in each look (ORBIT_INTERFACE picks it; Cinema draws Store's):
    // the way in, the drives, a drive's top, a folder, an entry's actions, the
    // clipboard, Rename and Delete, then Unpack here with its activity dialog, hidden.
    const auto files_at = [](const char *storage, const char *path)
    {
        return run(0.3f, [storage, path](orbit::App &app) { app.library().show_files(storage, path); });
    };
    const auto downs = [](int count)
    {
        std::vector<Step> steps;
        for (int i = 0; i < count; ++i)
            steps.push_back(move(Direction::down, 0.15f));
        return steps;
    };
    // Start at the same folder in every look, independently of Library's
    // game count. The older Crossbar scenario's fixed row count can land on
    // a game's details when the fixture inventory changes.
    list.push_back({"files-navigation", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::library); }),
         files_at("usb0", "homebrew"), pause(1.2f, "files-navigation-folder"),
         tap(Action::confirm), pause(1.0f, "files-navigation-open"),
         tap(Action::back), pause(1.0f, "files-navigation-parent")}});
    {
        std::vector<Step> steps{run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::library); }),
                                pause(1.0f, "files-games"), tap(Action::confirm), pause(1.0f, "files-drives"),
                                move(Direction::down), tap(Action::confirm), pause(1.2f, "files-drive"),
                                tap(Action::confirm), pause(0.8f, "files-details"), tap(Action::back),
                                files_at("usb0", "homebrew"), pause(1.2f, "files-folder")};
        for (Step &s : downs(4))
            steps.push_back(s);
        for (Step s : {tap(Action::confirm), tap(Action::confirm), pause(0.8f, "files-clip"), move(Direction::down),
                       tap(Action::confirm), pause(0.8f, "files-image"), move(Direction::right), move(Direction::right),
                       move(Direction::right), tap(Action::confirm), pause(0.8f, "files-rename"), tap(Action::back),
                       tap(Action::confirm), move(Direction::right), move(Direction::right), move(Direction::right),
                       move(Direction::right), tap(Action::confirm), pause(0.8f, "files-delete"), tap(Action::back),
                       move(Direction::up), move(Direction::up), tap(Action::confirm), tap(Action::confirm),
                       pause(1.2f, "files-activity"), tap(Action::back), pause(0.8f, "files-hidden")})
            steps.push_back(s);
        list.push_back({"files", Mode::normal, false, steps});
    }
    {
        std::vector<Step> steps{run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::library); }), pause(1.0f)};
        for (Step &s : downs(30))
            steps.push_back(s);
        for (Step s : {move(Direction::up), move(Direction::up), pause(0.8f, "xb-files-row"), tap(Action::confirm), pause(1.0f, "xb-files-drives"),
                       tap(Action::confirm), pause(1.2f, "xb-files-drive"), move(Direction::down), tap(Action::confirm),
                       pause(1.2f, "xb-files-folder")})
            steps.push_back(s);
        for (Step &s : downs(4))
            steps.push_back(s);
        for (Step s : {tap(Action::menu), pause(0.8f, "xb-files-menu"), tap(Action::confirm), pause(0.8f, "xb-files-clip"),
                       tap(Action::menu), move(Direction::down), move(Direction::down), tap(Action::confirm),
                       pause(0.8f, "xb-files-rename"), tap(Action::back), tap(Action::menu), move(Direction::down),
                       move(Direction::down), move(Direction::down), tap(Action::confirm), pause(0.8f, "xb-files-delete"),
                       tap(Action::back), move(Direction::up), tap(Action::confirm), tap(Action::confirm),
                       pause(1.2f, "xb-files-activity"), tap(Action::back), pause(0.8f, "xb-files-hidden")})
            steps.push_back(s);
        list.push_back({"xb-files", Mode::normal, false, steps});
    }
    {
        std::vector<Step> steps{run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::library); }), pause(1.0f)};
        for (Step s : {move(Direction::right), pause(0.8f, "pn-files-library-1"), move(Direction::right), pause(0.8f, "pn-files-library-2"),
                       move(Direction::right), pause(0.8f, "pn-files-library-3"), files_at("usb0", ""), pause(1.2f, "pn-files-drive"),
                       files_at("usb0", "homebrew"), pause(1.2f, "pn-files-folder"), move(Direction::down),
                       move(Direction::down), pause(0.8f, "pn-files-package"), move(Direction::right), pause(0.8f, "pn-files-next"),
                       tap(Action::menu), pause(0.8f, "pn-files-bar"), tap(Action::back), tap(Action::confirm),
                       pause(0.8f, "pn-files-actions"), tap(Action::confirm), pause(1.2f, "pn-files-activity"),
                       tap(Action::back), pause(0.8f, "pn-files-hidden")})
            steps.push_back(s);
        list.push_back({"pn-files", Mode::normal, false, steps});
    }
    list.push_back({"settings",
                    Mode::normal,
                    true,
                    {run(1.5f, [](orbit::App &app) { app.open_settings(); }),
                     pause(0.8f, "settings"),
                     move(Direction::down),
                     pause(0.8f, "settings-storage"),
                     move(Direction::right),
                     move(Direction::down),
                     move(Direction::down),
                     tap(Action::confirm),
                     pause(1.0f, "settings-storage-saved"),
                     move(Direction::left),
                     move(Direction::down),
                     pause(1.0f, "settings-notifications"),
                     move(Direction::down),
                     pause(1.0f, "settings-pairing"),
                     move(Direction::down),
                     move(Direction::right),
                     pause(0.8f, "settings-catalogue"),
                     tap(Action::confirm),
                     pause(0.8f, "settings-catalogue-refreshed"),
                     move(Direction::left),
                     move(Direction::down),
                     pause(1.0f, "settings-updates-current"),
                     move(Direction::down),
                     pause(0.8f, "settings-more")}});
    // App settings → Download sources → Archive.org: the row, the sign-in form
    // (refused, then signed in) and signing out. The fixture accepts "orbit".
    list.push_back({"archive-account", Mode::normal, false,
        {run(1.5f, [](orbit::App &app)
             { app.open_settings(orbit::SettingsScreen::Section::sources, true); }),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         pause(1.0f, "archive-row"), tap(Action::confirm), pause(0.8f, "archive-form"),
         tap(Action::confirm), pause(0.8f, "archive-keyboard"), tap(Action::back),
         run(0.6f, [](orbit::App &app) { app.settings().open_archive_sign_in("player@example.com", "wrong"); }),
         move(Direction::down), move(Direction::down), tap(Action::confirm), pause(1.0f, "archive-refused"),
         tap(Action::back),
         run(0.6f, [](orbit::App &app) { app.settings().open_archive_sign_in("player@example.com", "orbit"); }),
         move(Direction::down), move(Direction::down), tap(Action::confirm), pause(1.2f, "archive-signed-in"),
         tap(Action::confirm), pause(0.8f, "archive-sign-out")}});
    // My NAS address: the same popup as the Archive.org email.
    list.push_back({"nas-popup", Mode::normal, false,
        {run(1.5f, [](orbit::App &app)
             { app.open_settings(orbit::SettingsScreen::Section::sources, true); }),
         run(1.0f, [](orbit::App &app) { app.settings().open_nas_address("https://nas.example.com/index.json"); }),
         pause(1.0f, "nas-popup")}});
    // App settings → Appearance → Language: the choices, then German at once. One
    // scenario per look, as each lists Appearance's rows in its own order.
    // Pictures are named by literals: a step keeps only the pointer.
    const auto language = [](int rows, bool menu, const char *row, const char *choices, const char *german)
    {
        std::vector<Step> steps{run(1.5f, [](orbit::App &app)
                                    { app.open_settings(orbit::SettingsScreen::Section::appearance, true); })};
        for (int i = 0; i < rows; ++i)
            steps.push_back(move(Direction::down));
        steps.push_back(pause(0.8f, row));
        if (menu)
            steps.push_back(tap(Action::confirm));
        steps.push_back(pause(0.8f, choices));
        const Direction next = menu ? Direction::down : Direction::right;
        steps.insert(steps.end(), {move(next), move(next), tap(Action::confirm), pause(1.2f, german)});
        return steps;
    };
    list.push_back({"language", Mode::normal, false,
                    language(2, false, "language-row", "language-choices", "language-german")});
    list.push_back({"xb-language", Mode::normal, false,
                    language(3, true, "xb-language-row", "xb-language-choices", "xb-language-german")});
    list.push_back({"cn-language", Mode::normal, false,
                    language(3, true, "cn-language-row", "cn-language-choices", "cn-language-german")});
    list.push_back({"pn-language", Mode::normal, false,
                    language(4, true, "pn-language-row", "pn-language-choices", "pn-language-german")});
    // App settings → More settings → View diagnostics: the report over the screen,
    // scrolled, then read again (ORBIT_INTERFACE picks the look behind it).
    list.push_back({"diagnostics", Mode::normal, false,
        {run(1.5f, [](orbit::App &app)
             { app.open_settings(orbit::SettingsScreen::Section::more, true); }),
         pause(0.8f, "diagnostics-more"),
         run(1.0f, [](orbit::App &app) { app.settings().open_diagnostics(); }), pause(0.8f, "diagnostics"),
         move(Direction::down), move(Direction::down), move(Direction::down), pause(0.6f, "diagnostics-scrolled"),
         tap(Action::confirm), pause(0.8f, "diagnostics-refreshed"), tap(Action::back), pause(0.6f, "diagnostics-closed")}});
    // App settings → Storage → ntfy notifications: the row, the form, the keyboard,
    // saving and the test notification.
    list.push_back({"ntfy", Mode::normal, false,
        {run(1.5f, [](orbit::App &app)
             { app.open_settings(orbit::SettingsScreen::Section::storage, true); }),
         run(0.8f, [](orbit::App &app) { app.settings().open_ntfy("", "", false); }), pause(0.8f, "ntfy-form"),
         move(Direction::down), tap(Action::confirm), pause(0.8f, "ntfy-keyboard"), tap(Action::back),
         run(0.6f, [](orbit::App &app) { app.settings().open_ntfy("https://ntfy.sh", "orbit-downloads", true); }),
         move(Direction::down), move(Direction::down), move(Direction::down), move(Direction::down),
         tap(Action::confirm), pause(1.2f, "ntfy-saved"), move(Direction::right), tap(Action::confirm),
         pause(1.2f, "ntfy-tested"), move(Direction::right), tap(Action::confirm), pause(1.0f, "ntfy-row")}});
    // App settings → Appearance: the cards, switching, and the keep check.
    list.push_back({"appearance", Mode::normal, true,
        {run(1.5f, [](orbit::App &app)
             { app.open_settings(orbit::SettingsScreen::Section::appearance, true); }),
         pause(0.8f, "appearance"), move(Direction::right), move(Direction::right),
         move(Direction::right), pause(0.6f, "appearance-panorama"), tap(Action::confirm),
         pause(1.2f, "appearance-keep"), tap(Action::confirm), pause(0.8f, "appearance-kept")}});
    // A favourite toggled twice (added then removed, or the reverse, as the
    // backend has it): every look's notice (ORBIT_INTERFACE picks the look).
    // Triangle on Discover's focused game once the app has settled; Store
    // saves one from the game's page, so favourite-store opens it first.
    list.push_back({"favourite", Mode::normal, true,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); }),
         pause(7.0f), tap(Action::north), pause(1.0f, "favourite-1"), pause(3.0f), tap(Action::north),
         pause(1.0f, "favourite-2")}});
    list.push_back({"favourite-store", Mode::normal, true,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); }),
         pause(7.0f), tap(Action::confirm), pause(1.5f), tap(Action::north), pause(1.0f, "favourite-1"),
         pause(3.0f), tap(Action::north), pause(1.0f, "favourite-2")}});
    // Store's one-time notice about Appearance.
    list.push_back({"notice", Mode::normal, false,
        {run(1.5f, [](orbit::App &app) { app.show_tab(orbit::tab::discover); }),
         pause(2.0f, "notice")}});
    // Newer releases of both parts: the badge, guided installs and a restart.
    list.push_back(
        {"updates",
         Mode::updates,
         false,
         {pause(2.5f, "updates-badge"),
          run(0.5f, [](orbit::App &app)
              { app.open_settings(orbit::SettingsScreen::Section::updates, true); }),
          pause(1.0f, "settings-updates"), move(Direction::down), // past Update Orbit
          move(Direction::right), tap(Action::confirm),
          pause(1.0f, "settings-tv-installed"), move(Direction::down), move(Direction::right),
          tap(Action::confirm), pause(1.2f, "settings-service-saved"), tap(Action::confirm),
          pause(0.8f, "settings-restart-confirm"), move(Direction::left), tap(Action::confirm),
          pause(0.6f, "settings-restarting"), pause(40.0f, "settings-restarted")}});
    // My NAS: the address field and its keyboard.
    list.push_back(
        {"nas",
         Mode::updates,
         false,
         {pause(1.0f, "nas-start"),
          run(0.5f, [](orbit::App &app)
              { app.open_settings(orbit::SettingsScreen::Section::sources, true); }),
          move(Direction::right), move(Direction::right), tap(Action::confirm),
          pause(0.8f, "settings-nas-field"), move(Direction::down), tap(Action::confirm),
          pause(1.2f, "settings-nas-keyboard"), tap(Action::confirm), pause(0.6f, "settings-nas-typed")}});
    // The background chooser, then an animated choice.
    list.push_back(
        {"appearance",
         Mode::updates,
         false,
         {pause(1.5f, "appearance-start"),
          run(0.5f, [](orbit::App &app)
              { app.open_settings(orbit::SettingsScreen::Section::appearance, true); }),
          pause(0.8f, "settings-appearance"), move(Direction::down), move(Direction::down),
          move(Direction::down), move(Direction::down), move(Direction::down),
          move(Direction::down), tap(Action::confirm), pause(2.0f, "settings-appearance-aurora")}});
    list.push_back(
        {"volume",
         Mode::updates,
         false,
         {pause(1.5f, "volume-start"),
          run(0.5f, [](orbit::App &app)
              { app.open_settings(orbit::SettingsScreen::Section::appearance, true); }),
          pause(0.8f, "settings-volume"), move(Direction::right), move(Direction::right),
          pause(0.6f, "settings-volume-70"), move(Direction::down),
          pause(0.6f, "settings-volume-toggle")}});
    list.push_back({"language",
                    Mode::normal,
                    false,
                    {pause(1.5f, "language-start"),
                     run(1.0f, [](orbit::App &app)
                         { app.open_settings(orbit::SettingsScreen::Section::appearance, true); }),
                     pause(0.8f, "settings-language"), move(Direction::right), move(Direction::right),
                     tap(Action::confirm), pause(0.6f, "settings-language-azerty"),
                     move(Direction::down), move(Direction::down),
                     move(Direction::down), tap(Action::confirm), pause(1.0f, "settings-language-french"),
                     move(Direction::down), pause(0.6f, "settings-language-next"),
                     move(Direction::up), move(Direction::up), move(Direction::up), move(Direction::up),
                     move(Direction::up), tap(Action::confirm), pause(1.0f, "settings-language-auto")}});
    return list;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s <assets dir> <output dir> [scenario|all] [width height]\n",
                     argv[0]);
        return 2;
    }
    const std::string assets = argv[1];
    const std::string output = argv[2];
    const std::string only = argc > 3 && std::string(argv[3]) != "all" ? argv[3] : "";
    const int width = argc > 5 ? std::atoi(argv[4]) : 1920;
    const int height = argc > 5 ? std::atoi(argv[5]) : 1080;
    // The chosen background is kept beside the pictures, not in /data.
    setenv("ORBIT_APPEARANCE_FILE", "build/preview/appearance.cfg", 0);
    const char *backend = std::getenv("ORBIT_BACKEND");
    // ORBIT_LANG=de renders the screens in German, from the repository's catalogues;
    // English otherwise, from the same folder, so App settings can switch.
    const char *language = std::getenv("ORBIT_LANG");
    if (!orbit::i18n::load(assets + "/../../i18n", language != nullptr ? language : "en"))
        std::fprintf(stderr, "language %s unavailable; using English\n", language);

    if (!open_context())
    {
        std::fprintf(stderr, "no surfaceless EGL OpenGL 4.5 context\n");
        return 1;
    }
    hui::gfx::set_glsl_prefix("#version 450 core\n");
    hui::gfx::Renderer renderer;
    hui::gfx::Font light;
    hui::gfx::Font regular;
    hui::gfx::Font medium;
    hui::gfx::Font semibold;
    orbit::ui::Type type;
    if (!renderer.init() ||
        !load_font(renderer, assets + "/fonts/inter-display-light.huifont", &light, &type.light) ||
        !load_font(renderer, assets + "/fonts/inter-regular.huifont", &regular, &type.regular) ||
        !load_font(renderer, assets + "/fonts/inter-medium.huifont", &medium, &type.medium) ||
        !load_font(renderer, assets + "/fonts/inter-semibold.huifont", &semibold, &type.semibold))
        return 1;
    hui::gfx::Font xlight;
    hui::gfx::Font tight_light;
    hui::gfx::Font tight_medium;
    hui::gfx::Font sora;
    hui::gfx::Font sora_semibold;
    hui::gfx::Font tight_semibold;
    if (!load_font(renderer, assets + "/fonts/inter-tight-extralight.huifont", &xlight, &type.xlight) ||
        !load_font(renderer, assets + "/fonts/inter-tight-light.huifont", &tight_light, &type.tight_light) ||
        !load_font(renderer, assets + "/fonts/inter-tight-medium.huifont", &tight_medium, &type.tight_medium) ||
        !load_font(renderer, assets + "/fonts/sora-regular.huifont", &sora, &type.sora) ||
        !load_font(renderer, assets + "/fonts/sora-semibold.huifont", &sora_semibold, &type.sora_semibold) ||
        !load_font(renderer, assets + "/fonts/inter-tight-semibold.huifont", &tight_semibold, &type.cinema))
        return 1;
    // ORBIT_INTERFACE=crossbar|cinema|panorama draws every scenario in that interface.
    orbit::look::Interface interface = orbit::look::Interface::store;
    if (const char *name = std::getenv("ORBIT_INTERFACE"))
    {
        for (int i = 0; i < static_cast<int>(orbit::look::Interface::count); ++i)
        {
            const auto candidate = static_cast<orbit::look::Interface>(i);
            std::string title = orbit::look::title(candidate);
            for (char &c : title)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (title == name)
                interface = candidate;
        }
    }
    hui::ui::Fonts fonts;
    fonts.regular = type.regular;
    fonts.semibold = type.semibold;
    fonts.display = type.light;
    fonts.mono = type.medium;
    fonts.pixel = type.regular;
    fonts.hand = type.regular;
    orbit::GlTextures textures(renderer.batch());
    const std::uint32_t mark = textures.create(orbit::mark::render(128));
    const std::uint32_t loader_mark = textures.create(orbit::mark::render(256, 0.32f));

    GLuint framebuffer = 0;
    GLuint color = 0;
    glGenFramebuffers(1, &framebuffer);
    glGenRenderbuffers(1, &color);
    glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 1;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
    stbi_flip_vertically_on_write(1);

    bool ok = true;
    int ran = 0;
    for (const Scenario &scenario : scenarios())
    {
        if (!only.empty() && only != scenario.name)
            continue;
        if (backend != nullptr && !scenario.live_ok)
            continue;
        ++ran;
        std::unique_ptr<orbit::http::Transport> transport;
        std::unique_ptr<orbit::http::Transport> art_transport;
        if (backend != nullptr)
        {
            std::string address = backend;
            const std::size_t colon = address.find(':');
            const auto port = static_cast<std::uint16_t>(
                colon == std::string::npos ? 34177 : std::atoi(address.c_str() + colon + 1));
            address = address.substr(0, colon);
            transport = std::make_unique<orbit::http::SocketTransport>(address, port);
            art_transport = std::make_unique<orbit::http::SocketTransport>(address, port, 30000);
        }
        else
        {
            transport = std::make_unique<orbit::host::Fixture>(scenario.mode);
            art_transport = std::make_unique<orbit::host::Fixture>(scenario.mode);
        }
        const std::string name = scenario.name;
        std::unique_ptr<PreviewStarter> starter;
        if (backend == nullptr && (name == "starting" || name == "no-loader" || name == "updates"))
            starter = std::make_unique<PreviewStarter>(name == "no-loader"
                                                           ? orbit::starter::Outcome::no_loader
                                                           : orbit::starter::Outcome::sent);
        orbit::Store store(*transport, starter.get());
        orbit::ArtCache art(*art_transport, textures);
        store.start();
        art.start();
        orbit::look::load("");
        orbit::look::preview(interface);
        if (std::getenv("ORBIT_NOTICE_SEEN") != nullptr)
        {
            // Store renders without the Appearance notice, to compare with older builds.
            orbit::look::Prefs prefs = orbit::look::prefs();
            prefs.notice_seen = true;
            orbit::look::set(prefs);
        }
        if (const char *ribbons = std::getenv("ORBIT_RIBBONS"))
        {
            // ORBIT_RIBBONS=dawn|day|dusk|night fixes Crossbar's colours, which
            // otherwise follow the clock, to compare with the browser and Zero.
            const std::string choice = ribbons;
            orbit::look::Prefs prefs = orbit::look::prefs();
            prefs.ribbons = choice == "dawn"  ? orbit::look::Ribbons::dawn
                            : choice == "day" ? orbit::look::Ribbons::day
                            : choice == "dusk" ? orbit::look::Ribbons::dusk
                            : choice == "night" ? orbit::look::Ribbons::night
                                                : orbit::look::Ribbons::follow_time;
            orbit::look::set(prefs);
        }
        orbit::Context context(type, fonts, store, art);
        context.mark = mark;
        context.loader_mark = loader_mark;
        context.today = "2026-10-04";
        context.language_dir = assets + "/../../i18n";
        const char *preview_version = std::getenv("ORBIT_APP_VERSION");
        context.app_version = preview_version != nullptr ? preview_version : "01.000.001";
        context.open_browser = [] { return true; };
        // Preview callbacks never open websites or submit a real download.
        context.open_download = [](const orbit::BrowserSelection &) { return true; };
        orbit::App app(context);
        orbit::Frame frame;
        frame.glass_texture = renderer.glass_texture();

        constexpr float kDt = 1.0f / 60.0f;
        hui::ui::Feedback feedback;
        const auto tick = [&](const hui::InputFrame &input)
        {
            feedback.clear();
            app.update(input, kDt, feedback);
            // The workers run in real time: give them a moment every frame.
            usleep(1500);
        };
        const auto render = [&](const std::string &name)
        {
            // Draw once so the screen asks for its art, then wait (in real
            // time) until it has arrived, so pictures are complete.
            frame.reset();
            app.draw(frame);
            // A live backend may need several real-time polling rounds for
            // artwork. The old sub-second window captured loading placeholders.
            for (int i = 0; i < 6000 && !art.idle(); ++i)
            {
                tick({});
                frame.reset();
                app.draw(frame);
            }
            // Art fades in over real time: let it finish.
            for (int i = 0; i < 30; ++i)
                tick({});
            usleep(300000);
            tick({});
            frame.reset();
            app.draw(frame);
            renderer.begin();
            renderer.backdrop(frame.backdrop);
            renderer.draw(frame.scene);
            if (frame.glass)
                renderer.glass();
            renderer.draw(frame.overlay);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            renderer.present(framebuffer, width, height);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            for (std::size_t i = 3; i < pixels.size(); i += 4)
                pixels[i] = 255;
            const std::string path = output + "/" + name + ".png";
            ok =
                stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4) != 0 && ok;
            std::fprintf(stderr, "wrote %s: %zu shapes, %zu draw calls, GL error 0x%x\n",
                         path.c_str(), renderer.last_instances(), renderer.last_draw_calls(),
                         glGetError());
        };

        for (const Step &step : scenario.steps)
        {
            const int frames = static_cast<int>(step.wait / kDt);
            for (int i = 0; i < frames; ++i)
                tick({});
            if (step.act)
                step.act(app);
            if (step.capture != nullptr)
                render(step.capture);
            hui::InputFrame input;
            input.connected = true;
            input.pressed = step.press;
            input.nav = step.nav;
            tick(input);
        }
        art.stop();
        store.stop();
        art.clear();
    }
    if (ran == 0)
    {
        std::fprintf(stderr, "no scenario named '%s'\n", only.c_str());
        return 2;
    }
    return ok ? 0 : 1;
}
