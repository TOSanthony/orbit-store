// Orbit Store TV app - Image decoding, the mark, and the app driven by input.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixture.hpp"
#include "orbit/download_settings.hpp"
#include "orbit/i18n.hpp"
#include "orbit/image.hpp"
#include "orbit/mark.hpp"
#include "orbit/screens.hpp"
#include "support.hpp"

#include "../third_party/stb/stb_image_write.h"

#include <webp/encode.h>

#include <gtest/gtest.h>

#include <cmath>
#include <random>

namespace orbit
{
namespace
{

void append(void *context, void *data, int size)
{
    static_cast<std::string *>(context)->append(static_cast<const char *>(data),
                                                static_cast<std::size_t>(size));
}

std::vector<std::uint8_t> solid(int width, int height, std::uint8_t r, std::uint8_t g,
                                std::uint8_t b)
{
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 0; i < rgba.size(); i += 4)
    {
        rgba[i] = r;
        rgba[i + 1] = g;
        rgba[i + 2] = b;
        rgba[i + 3] = 255;
    }
    return rgba;
}

TEST(Image, DecodesPngAndFitsItToTheDrawingSize)
{
    const std::vector<std::uint8_t> rgba = solid(800, 400, 200, 40, 10);
    std::string png;
    stbi_write_png_to_func(append, &png, 800, 400, 4, rgba.data(), 800 * 4);
    EXPECT_EQ(image::sniff(png), image::Format::png);
    image::Pixels pixels;
    ASSERT_TRUE(image::decode(png, 400, 400, &pixels));
    EXPECT_EQ(pixels.width, 400);
    EXPECT_EQ(pixels.height, 200);
    EXPECT_NEAR(pixels.rgba[0], 200, 1);
    EXPECT_NEAR(pixels.rgba[1], 40, 1);
    // Small images are never enlarged.
    ASSERT_TRUE(image::decode(png, 4000, 4000, &pixels));
    EXPECT_EQ(pixels.width, 800);
}

TEST(Image, DecodesWebpAtTheRequestedSize)
{
    const std::vector<std::uint8_t> rgba = solid(512, 512, 30, 120, 240);
    std::uint8_t *encoded = nullptr;
    const std::size_t size = WebPEncodeLosslessRGBA(rgba.data(), 512, 512, 512 * 4, &encoded);
    ASSERT_GT(size, 0u);
    const std::string webp(reinterpret_cast<const char *>(encoded), size);
    WebPFree(encoded);
    EXPECT_EQ(image::sniff(webp), image::Format::webp);
    image::Pixels pixels;
    ASSERT_TRUE(image::decode(webp, 400, 400, &pixels));
    EXPECT_EQ(pixels.width, 400);
    EXPECT_EQ(pixels.height, 400);
    EXPECT_NEAR(pixels.rgba[2], 240, 2);
}

TEST(Image, RefusesWhatIsNotAnImage)
{
    image::Pixels pixels;
    std::string error;
    EXPECT_FALSE(image::decode("<html>blocked</html>", 100, 100, &pixels, &error));
    EXPECT_EQ(error, "unsupported image format");
    EXPECT_FALSE(image::decode(std::string("RIFF\0\0\0\0WEBPjunk", 16), 100, 100, &pixels, &error));
    EXPECT_FALSE(
        image::decode(std::string("\x89PNG\r\n\x1a\nbroken", 14), 100, 100, &pixels, &error));
    EXPECT_TRUE(pixels.empty());
}

TEST(Image, ResizeAveragesAndAmbientIsASoftDarkenedWash)
{
    image::Pixels checker;
    checker.width = checker.height = 4;
    checker.rgba.resize(64);
    for (int i = 0; i < 16; ++i)
    {
        const std::uint8_t v = (i + i / 4) % 2 ? 255 : 0;
        checker.rgba[static_cast<std::size_t>(i) * 4] = v;
        checker.rgba[static_cast<std::size_t>(i) * 4 + 3] = 255;
    }
    const image::Pixels one = image::resize(checker, 1, 1);
    EXPECT_NEAR(one.rgba[0], 128, 1);
    const image::Pixels wash = image::ambient(checker, 16);
    EXPECT_EQ(wash.width, 16);
    EXPECT_LT(wash.rgba[0], 128); // darkened
    EXPECT_EQ(wash.rgba[3], 255);
}

TEST(Mark, HasThePlanetTheGapAndTheHiddenBackRing)
{
    const image::Pixels mark = mark::render(512);
    const auto alpha = [&](int x, int y)
    { return mark.rgba[(static_cast<std::size_t>(y) * 512 + x) * 4 + 3]; };
    EXPECT_EQ(alpha(256, 200), 255); // the planet
    EXPECT_EQ(alpha(10, 10), 0);     // the corner is transparent
    EXPECT_EQ(alpha(454, 182), 255); // the ring's right end
    // The ring is tilted 22 degrees: its lowest point in front of the planet's
    // centre is at (256, 262) + 64 * (sin 22, cos 22).
    EXPECT_EQ(alpha(280, 321), 255); // the front ring crossing the planet
    // The gap cut around the front ring, inside the planet, is transparent.
    EXPECT_LT(alpha(272, 301), 128);
    const image::Pixels faint = mark::render(64, 0.3f);
    EXPECT_EQ(faint.width, 64);
}

// ---- the whole app, driven by controller input against the fixture ----

struct AppHarness
{
    test::TestFonts fonts;
    host::Fixture backend;
    test::FakeSink sink;
    test::ScriptedTransport transport;
    std::string storage_override;
    std::string browser_override;
    Store store{transport};
    ArtCache art{backend, sink};
    Context context{fonts.type, fonts.fonts, store, art};
    App app{context};
    Feedback feedback;

    explicit AppHarness(host::FixtureMode mode = host::FixtureMode::normal, bool with_addon = false, int addon_count = 1) : backend(mode)
    {
        transport.reply = [this, with_addon, addon_count](const http::Request &request)
        {
            if (with_addon && request.path == "/api/v1/addons")
            {
                std::string rows = "[";
                for (int i = 0; i < addon_count; ++i)
                {
                    const std::string suffix = i == 0 ? "" : "-" + std::to_string(i);
                    const std::string id = "neon-extra" + suffix;
                    if (i > 0) rows += ',';
                    rows += "{\"id\":\"" + id + "-file\",\"gameId\":\"" + id +
                            "\",\"addonId\":\"" + id + "\",\"title\":\"Extra campaign" + suffix +
                            R"(","titleId":"PPSA12345","sizeBytes":4096,"provider":"Archive.org","sourceId":"archive","format":"PKG","filename":"extra.pkg","platform":"ps5","baseGameId":"neon-courier","baseGameTitle":"Neon Courier","addonType":"expansion","contents":["Extra campaign"],"requirements":{"titleIds":["PPSA12345"],"minimumVersion":null,"edition":null}})";
                }
                return test::json_reply(200, rows + ']');
            }
            if (request.path == "/api/v1/storage" && !storage_override.empty())
                return test::json_reply(200, storage_override);
            if (request.path == "/api/v1/browser" && request.method == "GET" && !browser_override.empty())
                return test::json_reply(200, browser_override);
            return backend.send(request);
        };
        context.today = "2026-10-04";
        context.app_version = "01.000.000";
        store.poll_once();
        frame();
    }
    void frame(Direction nav = Direction::none, std::uint32_t press = 0)
    {
        InputFrame input;
        input.connected = true;
        input.nav = nav;
        input.pressed = press;
        frame(input);
    }
    void pad(std::uint32_t buttons)
    {
        hui::InputTracker tracker;
        hui::PadSample sample;
        sample.connected = true;
        sample.buttons = buttons;
        frame(tracker.update(std::span<const hui::PadSample>(&sample, 1), 100000));
    }
    void frame(const InputFrame &input)
    {
        feedback.clear();
        app.update(input, 1.0f / 60.0f, feedback);
        Frame drawn;
        app.draw(drawn);
        for (const auto &instance : drawn.scene.instances())
            ASSERT_TRUE(std::isfinite(instance.rect[0]) && std::isfinite(instance.rect[1]));
    }
    void press(Action action)
    {
        frame(Direction::none, hui::action_bit(action));
    }
    void settle(int frames = 30)
    {
        for (int i = 0; i < frames; ++i)
            frame();
    }
};

struct NativeLook
{
    explicit NativeLook(look::Interface interface)
    {
        look::load("");
        look::preview(interface);
        auto prefs = look::prefs();
        prefs.notice_seen = true;
        look::set(prefs);
    }
    ~NativeLook() { look::load(""); }
};

TEST(App, AddonDetailsAndControllerBackWorkInEveryInterfaceWithoutSavingItAsAGame)
{
    for (auto layout : {look::Interface::store, look::Interface::crossbar, look::Interface::cinema, look::Interface::panorama})
    {
        NativeLook appearance(layout);
        AppHarness h(host::FixtureMode::normal, true);
        ASSERT_EQ(h.store.state().addons.size(), 1u);
        const auto count = h.store.state().games.size();
        h.app.show_tab(tab::addons);
        h.settle();
        h.app.open_game("neon-extra");
        h.settle();
        h.press(Action::north);
        EXPECT_FALSE(h.store.favourite_action().busy);
        h.press(Action::back);
        h.settle();
        EXPECT_EQ(h.app.current_tab(), tab::addons);
        EXPECT_EQ(h.store.state().games.size(), count);
        h.app.open_game("neon-extra");
        h.press(Action::west);
        h.pad(hui::pad_bits::kDown);
        h.press(Action::back);
        h.settle();
        EXPECT_EQ(h.app.current_tab(), tab::addons);
    }
}

TEST(App, ConfirmOnFilteredAddonOpensThatAddonInEveryInterface)
{
    for (auto layout : {look::Interface::store, look::Interface::crossbar,
                        look::Interface::cinema, look::Interface::panorama})
    {
        SCOPED_TRACE(static_cast<int>(layout));
        NativeLook appearance(layout);
        AppHarness h(host::FixtureMode::normal, true);
        BrowseScreen screen(h.context, true);
        screen.focus();
        screen.for_base("neon-courier");
        screen.set_query("Extra campaign");
        const auto input = [&](Direction direction, bool confirm = false)
        {
            InputFrame frame;
            frame.connected = true;
            frame.nav = direction;
            if (confirm) frame.pressed = hui::action_bit(Action::confirm);
            const Intent intent = screen.update(frame, 1.0f / 60.0f, h.feedback, true);
            Frame drawn;
            screen.draw(drawn, true);
            return intent;
        };
        if (layout == look::Interface::store || layout == look::Interface::crossbar)
        {
            input(Direction::down); // Search/filter controls.
            input(Direction::down); // The only matching DLC tile.
            if (layout == look::Interface::crossbar)
                input(Direction::down); // Skip Reset filters after the base-game filter.
        }
        const Intent opened = input(Direction::none, true);
        EXPECT_EQ(opened.kind, Intent::Kind::open_game);
        EXPECT_EQ(opened.id, "neon-extra");
        ASSERT_NE(h.store.state().game(opened.id), nullptr);
        EXPECT_EQ(h.store.state().game(opened.id)->addon_id, "neon-extra");
        screen.return_to(opened.id);
        EXPECT_EQ(input(Direction::none, true).id, "neon-extra");
    }
}

TEST(App, AddonSelectionUsesItsOwnIndexWhenThereAreMoreAddonsThanGames)
{
    NativeLook appearance(look::Interface::store);
    AppHarness h(host::FixtureMode::normal, true, 40);
    ASSERT_GT(h.store.state().addons.size(), h.store.state().games.size());
    const Game &target = h.store.state().addons.back();
    BrowseScreen screen(h.context, true);
    screen.focus();
    screen.set_query(target.title);
    screen.return_to(target.id);
    InputFrame input;
    input.connected = true;
    input.pressed = hui::action_bit(Action::confirm);
    const Intent opened = screen.update(input, 1.0f / 60.0f, h.feedback, true);
    EXPECT_EQ(opened.kind, Intent::Kind::open_game);
    EXPECT_EQ(opened.id, target.id);
}

TEST(App, CinemaDpadReachesEveryChapterWithoutCyclingCollectionsOrSearch)
{
    NativeLook cinema(look::Interface::cinema);
    AppHarness h;
    h.pad(hui::pad_bits::kUp);
    h.pad(hui::pad_bits::kLeft);
    EXPECT_EQ(h.app.current_section(), section::settings);
    h.pad(hui::pad_bits::kRight);
    EXPECT_EQ(h.app.current_section(), section::discover);
    for (int next : {section::browse, section::addons, section::library, section::downloads, section::settings, section::discover})
    {
        h.pad(hui::pad_bits::kRight);
        EXPECT_EQ(h.app.current_section(), next);
    }
    h.pad(hui::pad_bits::kDown);
    h.pad(hui::pad_bits::kRight); // game row, not the navbar
    EXPECT_EQ(h.app.current_section(), section::discover);
    h.pad(hui::pad_bits::kUp);
    h.pad(hui::pad_bits::kR1);
    EXPECT_EQ(h.app.current_section(), section::browse);
    h.pad(hui::pad_bits::kR1); // skips Browse's Favourites collection on the navbar
    EXPECT_EQ(h.app.current_section(), section::addons);
}

TEST(App, CinemaSearchRemainsAvailableInsideBrowseFilters)
{
    NativeLook cinema(look::Interface::cinema);
    AppHarness h;
    h.app.show_tab(tab::browse);
    h.pad(hui::pad_bits::kSquare);
    for (int i = 0; i < 12; ++i) // past the filters and Reset filters: Search is last
        h.pad(hui::pad_bits::kDown);
    h.pad(hui::pad_bits::kCross);
    EXPECT_TRUE(h.app.browse().modal()); // Search keyboard
    h.pad(hui::pad_bits::kCircle);
    EXPECT_FALSE(h.app.browse().modal());
    h.pad(hui::pad_bits::kUp);
    h.pad(hui::pad_bits::kLeft);
    EXPECT_EQ(h.app.current_section(), section::discover);
}

TEST(App, CrossbarDpadSwitchesCategoriesButLeavesDialogsInControl)
{
    NativeLook crossbar(look::Interface::crossbar);
    AppHarness h;
    h.pad(hui::pad_bits::kRight);
    EXPECT_EQ(h.app.current_section(), section::browse);
    h.app.browse().start_search();
    h.pad(hui::pad_bits::kLeft);
    EXPECT_EQ(h.app.current_section(), section::browse);
    EXPECT_TRUE(h.app.browse().modal());
    h.pad(hui::pad_bits::kCircle);
    for (int next : {section::addons, section::library, section::downloads, section::settings, section::search, section::discover})
    {
        h.pad(hui::pad_bits::kRight);
        EXPECT_EQ(h.app.current_section(), next);
        if (next == section::search)
            h.pad(hui::pad_bits::kCircle); // close Search's keyboard
    }
}

TEST(App, CinemaShowsStoresLibraryAndDownloadsInItsOwnPalette)
{
    NativeLook cinema(look::Interface::cinema);
    AppHarness h;
    h.app.show_tab(tab::library);
    h.settle();
    ASSERT_TRUE(h.store.state().library.ready());
    // L1 and R1 switch the app's tabs, as in Store, not Cinema's old collections.
    EXPECT_FALSE(h.app.library().page(1));
    // Store's details panel.
    h.app.library().show_game("PPSA90004", std::string(64, '4')); // Glass Orchard
    EXPECT_TRUE(h.app.library().details_open());
    h.pad(hui::pad_bits::kCircle);
    EXPECT_FALSE(h.app.library().details_open());
    // Cinema's paper and gold, then back to Store's palette.
    ui::color::use_cinema(true);
    EXPECT_NEAR(ui::color::accent.r, 0xdc / 255.0f, 1e-4f);
    EXPECT_NEAR(ui::color::text.b, 0xec / 255.0f, 1e-4f);
    ui::color::use_cinema(false);
    EXPECT_NEAR(ui::color::accent.r, 1.0f, 1e-4f);
    EXPECT_NEAR(ui::color::text.b, 1.0f, 1e-4f);
}

TEST(App, OpensAGameAndQueuesADownloadToTheDownloadsTab)
{
    AppHarness h;
    h.app.open_game("neon-courier");
    h.settle();
    h.press(Action::confirm); // Review options, focused on the enabled Download button.
    EXPECT_FALSE(h.store.action().busy);
    EXPECT_EQ(h.transport.count("/api/v1/downloads"), 1); // initial GET only
    h.press(Action::confirm); // Explicitly start.
    ASSERT_TRUE(h.store.action().busy);
    h.store.run_next_action();
    h.settle(5);
    EXPECT_EQ(h.store.state().jobs.size(), 7u);
    // The new job is shown in Downloads with a confirmation.
    bool queued = false;
    for (const Job &job : h.store.state().jobs)
        queued = queued || (job.release_id == "neon-courier-ffpfsc" && job.status == "queued");
    EXPECT_TRUE(queued);
}

TEST(App, GameDetailsQueuesTheSelectedEditionAndDrive)
{
    AppHarness h;
    h.app.open_game("ember-hollow");
    h.settle();
    h.press(Action::confirm); // Download options
    h.frame(Direction::up); // Save to
    h.frame(Direction::left); // Delivery
    h.frame(Direction::left); // Source
    h.press(Action::confirm);
    h.frame(Direction::down); // version 1.05
    h.press(Action::confirm); // Return to Source setting
    h.frame(Direction::right);h.frame(Direction::right); // Save to
    h.press(Action::confirm);
    h.frame(Direction::down); // Internal storage
    h.press(Action::confirm);
    h.frame(Direction::down); // Disabled Download is skipped.
    h.press(Action::confirm); // Drive choices remain available.
    EXPECT_FALSE(h.store.action().busy);
    h.frame(Direction::up); // USB has enough room.
    h.press(Action::confirm);
    h.frame(Direction::down); // Download
    h.press(Action::confirm);
    ASSERT_TRUE(h.store.action().busy);
    h.store.run_next_action();
    h.settle(5);
    bool matched = false;
    for (const Job &job : h.store.state().jobs)
        matched = matched || (job.release_id == "ember-hollow-ffpfsc-105" &&
                              job.storage_id == "usb0" && job.status == "queued");
    EXPECT_TRUE(matched);
}

TEST(App, NativeVikingStartsVerificationWithTheChosenDriveWithoutOpeningOrbitBrowser)
{
    AppHarness h;
    h.storage_override = R"([{"id":"usb0","label":"USB Extended Storage","path":"/mnt/usb0/homebrew","freeBytes":1400000000000,"totalBytes":2000000000000,"projectedFreeBytes":1400000000000,"external":true},{"id":"internal","label":"Internal storage","path":"/data/homebrew","freeBytes":500000000000,"totalBytes":1000000000000,"projectedFreeBytes":500000000000,"external":false}])";
    h.store.poll_once();
    h.settle();
    int browser_opens = 0;
    h.context.open_download = [&](const BrowserSelection &selection)
    {
        (void)selection;
        ++browser_opens;
        return true;
    };
    h.app.open_game("ironclad-skies");
    h.settle();
    h.press(Action::confirm); // Review does not open the browser.
    EXPECT_EQ(browser_opens,0);
    h.frame(Direction::up); // Save to
    h.press(Action::confirm);
    h.frame(Direction::down); // Internal storage, not the default USB
    h.press(Action::confirm);
    h.frame(Direction::down); // Open Vikingfile on PS5
    h.press(Action::confirm);
    EXPECT_EQ(browser_opens,0);
    EXPECT_FALSE(h.store.action().busy);
    EXPECT_TRUE(h.store.browser_action().busy);
    h.store.run_next_action();
    h.settle();
    bool started = false;
    for (const auto &request : h.transport.seen)
        if (request.method == "POST" && request.path == "/api/v1/browser")
        {
            EXPECT_EQ(request.body,R"({"action":"start","releaseId":"ironclad-skies-viking","storageId":"internal","returnTarget":"native"})");
            started = true;
        }
    EXPECT_TRUE(started);
    EXPECT_TRUE(h.store.state().browser.active);
    EXPECT_EQ(h.store.state().jobs.size(),6u); // A page opening is not a download.
    h.press(Action::back); // Close progress, keep original selection.
    const std::string original_storage = h.storage_override;
    h.storage_override = "[]"; // Reopening/cancelling still works if the drive disappears.
    h.store.poll_once();h.settle();
    h.press(Action::confirm); // Reopen current verification; no second POST.
    EXPECT_FALSE(h.store.run_next_action());
    h.frame(Direction::left);
    h.press(Action::confirm); // Cancel exactly this verification.
    EXPECT_TRUE(h.store.run_next_action());
    h.settle();
    EXPECT_EQ(h.store.state().browser.state,"cancelled");
    h.press(Action::back);
    h.storage_override = original_storage;
    h.store.poll_once();h.settle(); // Dismissed terminal result must not reappear.
    h.press(Action::confirm); // Original option and chosen drive can be retried.
    EXPECT_TRUE(h.store.browser_action().busy);
}

TEST(App, BrowserHandoffNeverSubstitutesADisconnectedDrive)
{
    AppHarness h;
    int opened = 0;
    h.context.open_download = [&](const BrowserSelection &)
    {
        ++opened;
        return true;
    };
    h.app.open_game("ironclad-skies"); // USB selected initially
    h.settle();
    h.storage_override = R"([{"id":"internal","label":"Internal storage","path":"/data/homebrew","freeBytes":500000000000,"totalBytes":1000000000000,"projectedFreeBytes":500000000000,"external":false}])";
    h.store.poll_once();
    h.settle();
    h.press(Action::confirm); // Disabled primary falls back to Source.
    EXPECT_EQ(opened,0);
    EXPECT_FALSE(h.store.action().busy);
    h.frame(Direction::right);h.frame(Direction::right); // Save to
    h.press(Action::confirm);
    h.press(Action::confirm); // Explicitly select remaining internal drive.
    h.frame(Direction::down);
    h.press(Action::confirm);
    EXPECT_EQ(opened,0);
    EXPECT_TRUE(h.store.browser_action().busy);
    EXPECT_FALSE(h.store.action().busy);
}

TEST(App, NativeVikingQueuedResultOpensTheExactValidatedJob)
{
    AppHarness h;
    h.app.open_game("ironclad-skies");h.settle();
    h.press(Action::confirm);h.press(Action::confirm);
    h.store.run_next_action();h.settle();
    h.browser_override=R"({"available":true,"active":false,"id":"verification-1","title":"Ironclad Skies","state":"queued","jobId":"j1","message":"Your verified file has been added to Downloads."})";
    h.store.poll_once();h.settle();
    h.frame(Direction::left);h.press(Action::confirm);
    EXPECT_EQ(h.app.current_tab(),tab::downloads);
    EXPECT_FALSE(h.store.action().busy);
}

TEST(App, NativeVikingLostSessionWaitsForExplicitRetry)
{
    AppHarness h;
    h.app.open_game("ironclad-skies");h.settle();
    h.press(Action::confirm);h.press(Action::confirm);
    ASSERT_TRUE(h.store.run_next_action());h.settle();
    // The service restarted: there is no session to resume or replay.
    h.browser_override=R"({"available":true,"active":false,"id":"","state":"idle","jobId":"","message":""})";
    h.store.poll_once();h.settle();
    EXPECT_FALSE(h.store.run_next_action());
    EXPECT_FALSE(h.store.state().browser.active);
    h.press(Action::back);
    h.store.poll_once();h.settle();
    EXPECT_FALSE(h.store.run_next_action());
    h.press(Action::confirm); // Same game/drive; only the user's click retries.
    EXPECT_TRUE(h.store.browser_action().busy);
}

TEST(App, NativeVikingDelayedStartDoesNotUndoClosingTheProgressPanel)
{
    AppHarness h;
    h.app.open_game("ironclad-skies");h.settle();
    h.press(Action::confirm);h.press(Action::confirm);
    h.press(Action::back); // Close before the POST/status response arrives.
    ASSERT_TRUE(h.store.run_next_action());h.settle();
    ASSERT_TRUE(h.store.state().browser.active);
    h.press(Action::confirm); // Explicitly reopen View verification.
    h.frame(Direction::left);h.press(Action::confirm);
    EXPECT_TRUE(h.store.run_next_action()); // Cancel, not an underlying game control.
    h.settle();
    EXPECT_EQ(h.store.state().browser.state,"cancelled");
}

TEST(App, CircleLeavesTheGamePageAndTabsSwitchWithL1R1)
{
    AppHarness h;
    EXPECT_EQ(h.app.current_tab(), orbit::tab::discover);
    h.app.open_game("tidewater");
    h.settle();
    h.press(Action::back);
    h.settle();
    // From Discover, Browse and Add-ons precede Library and Downloads.
    h.press(Action::page_next);
    EXPECT_EQ(h.app.current_tab(), orbit::tab::browse);
    h.press(Action::page_next);
    EXPECT_EQ(h.app.current_tab(), orbit::tab::addons);
    h.press(Action::page_next);
    h.press(Action::page_next);
    h.settle();
    EXPECT_EQ(h.app.current_tab(), orbit::tab::downloads);
    h.press(Action::page_next);
    EXPECT_EQ(h.app.current_tab(), orbit::tab::downloads);
    EXPECT_FALSE(h.feedback.cues.empty());
}

TEST(App, AFavouriteFromAGamePageShowsUnderBrowseFavourites)
{
    AppHarness h;
    h.app.open_game("neon-courier");
    h.settle();
    h.frame(Direction::right); // Favourite beside the main button
    h.press(Action::confirm); // Add to favourites
    ASSERT_TRUE(h.store.favourite_action().busy);
    h.store.run_next_action();
    h.settle(3);
    EXPECT_TRUE(h.store.state().favourite("neon-courier"));
    // (Circle would return to the game's tile; start from the search field.)
    h.app.show_tab(orbit::tab::browse);
    h.settle();
    h.frame(Direction::down); // Download-size filter beneath Search
    h.frame(Direction::left); // Format
    h.frame(Direction::left); // Source
    h.frame(Direction::left); // Favourites, on the same toolbar
    h.press(Action::confirm);
    h.settle(3);
    EXPECT_TRUE(h.app.browse().filters().favourites);
    EXPECT_EQ(h.app.browse().filters().query, "");
}

TEST(App, BrowseRegionPickerReturnsToMatchingGrid)
{
    AppHarness h;
    BrowseScreen screen(h.context);
    screen.focus();
    const auto input = [&](Direction direction, bool confirm = false)
    {
        InputFrame frame;
        frame.connected = true;
        frame.nav = direction;
        if (confirm)
            frame.pressed = hui::action_bit(Action::confirm);
        return screen.update(frame, 1.0f / 60.0f, h.feedback, true);
    };
    input(Direction::down); // Region, directly below Search
    input(Direction::none, true);
    input(Direction::down); // EUR
    input(Direction::none, true);
    EXPECT_EQ(screen.filters().region, "EUR");
    input(Direction::down); // leave closed dropdown for matching grid
    EXPECT_EQ(input(Direction::none, true).id, "ember-hollow");
}

TEST(App, DiscoverFavouriteTogglesFromTheHeroWithoutOpeningAGame)
{
    AppHarness h;
    h.app.show_tab(orbit::tab::discover);
    h.settle();
    const bool before = h.store.state().favourite("tidewater");
    h.frame(Direction::right); // Heart beside View game
    h.press(Action::confirm);
    ASSERT_TRUE(h.store.favourite_action().busy);
    h.store.run_next_action();
    h.settle(3);
    EXPECT_EQ(h.store.state().favourite("tidewater"), !before);
    h.press(Action::confirm); // The same control can remove it again.
    ASSERT_TRUE(h.store.favourite_action().busy);
    h.store.run_next_action();
    h.settle(3);
    EXPECT_EQ(h.store.state().favourite("tidewater"), before);
}

TEST(App, BrowsePlatformPickerOpensOnlyTheMatchingEdition)
{
    AppHarness h;
    BrowseScreen screen(h.context);
    screen.focus();
    const auto input = [&](Direction direction, bool confirm = false)
    {
        InputFrame frame;
        frame.connected = true;
        frame.nav = direction;
        if (confirm) frame.pressed = hui::action_bit(Action::confirm);
        return screen.update(frame, 1.0f / 60.0f, h.feedback, true);
    };
    input(Direction::down); // Region, below Search
    input(Direction::right); input(Direction::right); input(Direction::right); // Platform
    input(Direction::none, true);
    input(Direction::down); input(Direction::down); // PS4
    input(Direction::none, true);
    EXPECT_EQ(screen.filters().platform, "ps4");
    input(Direction::down); // Closed picker returns to the matching grid.
    EXPECT_EQ(input(Direction::none, true).id, "copper-circuit-ps4");
    // The same title exists as a separate PS5 edition; the filter must not open it.
    input(Direction::up); input(Direction::up); // Collection, then Search.
    input(Direction::down);
    input(Direction::right); input(Direction::right); input(Direction::right);
    input(Direction::none, true);
    input(Direction::up); input(Direction::none, true); // PS5
    EXPECT_EQ(screen.filters().platform, "ps5");
    input(Direction::down);
    EXPECT_NE(input(Direction::none, true).id, "copper-circuit-ps4");
}

TEST(App, SquareTogglesTheHighlightedBrowseGameAndIgnoresRepeatedPendingPresses)
{
    AppHarness h;
    BrowseScreen screen(h.context);
    screen.focus();
    screen.return_to("neon-courier");
    InputFrame input;
    input.connected = true;
    input.pressed = hui::action_bit(Action::west);
    EXPECT_EQ(screen.update(input, 1.0f / 60, h.feedback, true).kind, Intent::Kind::none);
    ASSERT_TRUE(h.store.favourite_action().busy);
    screen.update(input, 1.0f / 60, h.feedback, true);
    ASSERT_TRUE(h.store.run_next_action());
    EXPECT_FALSE(h.store.run_next_action());
    h.store.tick();
    EXPECT_TRUE(h.store.state().favourite("neon-courier"));
    screen.update(input, 1.0f / 60, h.feedback, true);
    ASSERT_TRUE(h.store.run_next_action());
    h.store.tick();
    EXPECT_FALSE(h.store.state().favourite("neon-courier"));
    screen.update(input, 1.0f / 60, h.feedback, false);
    EXPECT_FALSE(h.store.run_next_action());
}

TEST(App, SquareOnDiscoverTogglesTheHeroAndTheSelectedTileWithoutOpeningDetails)
{
    AppHarness h;
    DiscoverScreen screen(h.context);
    screen.focus();
    InputFrame input;
    input.connected = true;
    input.pressed = hui::action_bit(Action::west);
    const auto before = h.store.state().favourites;
    EXPECT_EQ(screen.update(input, 1.0f / 60, h.feedback, true).kind, Intent::Kind::none);
    ASSERT_TRUE(h.store.run_next_action());
    h.store.tick();
    EXPECT_NE(h.store.state().favourites, before);
    screen.return_to("neon-courier");
    const bool tile_before = h.store.state().favourite("neon-courier");
    EXPECT_EQ(screen.update(input, 1.0f / 60, h.feedback, true).kind, Intent::Kind::none);
    ASSERT_TRUE(h.store.run_next_action());
    h.store.tick();
    EXPECT_EQ(h.store.state().favourite("neon-courier"), !tile_before);
}

TEST(App, BrowseSquareKeepsSearchClearAndDropdownBehaviour)
{
    AppHarness h;
    BrowseScreen screen(h.context);
    screen.focus();
    screen.set_query("neon");
    InputFrame input;
    input.connected = true;
    input.pressed = hui::action_bit(Action::west);
    screen.update(input, 1.0f / 60, h.feedback, true);
    EXPECT_TRUE(screen.filters().query.empty());
    EXPECT_FALSE(h.store.run_next_action());
    input.pressed = 0;
    input.nav = Direction::down;
    screen.update(input, 1.0f / 60, h.feedback, true);
    input.nav = Direction::none;
    input.pressed = hui::action_bit(Action::confirm);
    screen.update(input, 1.0f / 60, h.feedback, true);
    input.pressed = hui::action_bit(Action::west);
    screen.update(input, 1.0f / 60, h.feedback, true);
    EXPECT_FALSE(h.store.run_next_action());
}

TEST(App, LibraryMountsAGameFromItsDetails)
{
    AppHarness h;
    h.app.show_tab(orbit::tab::library);
    h.settle();
    ASSERT_TRUE(h.store.state().library.ready());
    h.app.library().show_game("PPSA90004", std::string(64, '4')); // Glass Orchard
    h.settle();
    ASSERT_TRUE(h.app.library().details_open());
    h.press(Action::confirm); // Mount game
    ASSERT_TRUE(h.store.library_action().busy);
    h.store.run_next_action();
    h.settle(3);
    EXPECT_EQ(h.store.state().library.action.action, "mount");
    EXPECT_EQ(h.store.state().library.action.title_id, "PPSA90004");
    h.press(Action::back);
    h.settle();
    EXPECT_FALSE(h.app.library().details_open());
}

// Files: a folder opens directly, its row menu offers actions, Circle goes back up,
// and Delete asks first: nothing is sent until Delete is pressed in the question.
TEST(App, LibraryFilesBrowsesAndDeletesOnlyAfterConfirming)
{
    AppHarness h;
    h.app.show_tab(orbit::tab::library);
    h.settle();
    h.app.library().show_files("usb0", "homebrew");
    while (h.store.run_next_action())
        ;
    h.settle();
    ASSERT_EQ(h.store.state().files.storage_id, "usb0");
    ASSERT_EQ(h.store.state().files.entries.size(), 7u);
    h.frame(Direction::right); // the folder's separate More icon
    h.press(Action::confirm);
    EXPECT_TRUE(h.app.library().modal());
    EXPECT_EQ(h.store.state().files.path, "homebrew");
    h.press(Action::back); // close actions without leaving the folder
    h.frame(Direction::left); // folder row
    h.press(Action::confirm); // open Lantern Keep directly
    EXPECT_FALSE(h.app.library().modal());
    while (h.store.run_next_action())
        ;
    h.settle();
    EXPECT_EQ(h.store.state().files.path, "homebrew/Lantern Keep");
    h.press(Action::back); // up one folder
    while (h.store.run_next_action())
        ;
    h.settle();
    EXPECT_EQ(h.store.state().files.path, "homebrew");
    EXPECT_EQ(h.app.current_tab(), orbit::tab::library);
    // Lantern Keep is focused again; notes.txt is four rows down.
    for (int i = 0; i < 4; ++i)
        h.frame(Direction::down);
    h.press(Action::confirm); // its details: Copy, Move, Rename, Delete
    for (int i = 0; i < 3; ++i)
        h.frame(Direction::right);
    h.press(Action::confirm); // Delete: the question
    while (h.store.run_next_action())
        ;
    EXPECT_EQ(h.transport.count("/api/v1/files/action"), 0);
    EXPECT_TRUE(h.app.library().modal());
    h.press(Action::confirm); // Delete
    while (h.store.run_next_action())
        ;
    h.settle();
    EXPECT_EQ(h.transport.count("/api/v1/files/action"), 1);
    EXPECT_EQ(h.store.state().file_operation.action, "delete");
    EXPECT_EQ(h.store.state().file_operation.path, "homebrew/notes.txt");
}

TEST(App, ViewInLibraryOpensTheGamesLibraryDetails)
{
    AppHarness h;
    h.app.open_game("quiet-harbour"); // an exact copy is in the Library
    h.settle();
    h.press(Action::confirm); // Review
    h.press(Action::confirm); // View in Library
    h.settle();
    EXPECT_EQ(h.app.current_tab(), orbit::tab::library);
    EXPECT_TRUE(h.app.library().details_open());
}

TEST(App, DownloadsCancelDialogKeepsThePartial)
{
    AppHarness h;
    h.app.show_tab(orbit::tab::downloads);
    h.settle();
    h.frame(Direction::right); // Cancel on the first (downloading) row
    h.press(Action::confirm);  // opens the dialog
    h.settle();
    h.press(Action::confirm); // Keep partial file
    ASSERT_TRUE(h.store.action().busy);
    h.store.run_next_action();
    h.settle(3);
    EXPECT_EQ(h.store.state().jobs[0].status, "cancelled");
}

TEST(App, CancelledDownloadDeletionRequiresConfirmationAndRemovesTheEntry)
{
    AppHarness h;
    const std::string id = h.store.state().jobs.back().id;
    ASSERT_EQ(h.store.state().jobs.back().status, "cancelled");
    h.app.show_tab(orbit::tab::downloads);
    h.app.downloads().show_job(id);
    h.settle();
    h.frame(Direction::right); // Delete download
    h.press(Action::confirm);
    EXPECT_TRUE(h.app.downloads().modal());
    EXPECT_FALSE(h.store.action().busy);
    h.frame(Direction::left); // Go back is initially focused; select Delete.
    h.press(Action::confirm);
    ASSERT_TRUE(h.store.action().busy);
    h.store.run_next_action(); h.settle(3);
    EXPECT_EQ(h.transport.count("/api/v1/downloads/" + id + "/delete"), 1);
    EXPECT_EQ(h.store.state().jobs.size(), 5u);
    EXPECT_FALSE(h.app.downloads().modal());
}

TEST(App, CancelledDeletionFailureStaysVisibleAndHistoryOnlyNeedsAnotherConfirmation)
{
    AppHarness h;
    const std::string id = h.store.state().jobs.back().id;
    h.transport.reply = [&](const http::Request &request)
    {
        if (request.path == "/api/v1/downloads/" + id + "/delete")
            return test::json_reply(409, R"({"error":"Reconnect the original drive before deleting the partial file."})");
        return h.backend.send(request);
    };
    h.app.show_tab(orbit::tab::downloads);
    h.app.downloads().show_job(id);
    h.settle();
    h.frame(Direction::right); h.press(Action::confirm);
    h.frame(Direction::left); h.press(Action::confirm);
    h.store.run_next_action(); h.settle(3);
    EXPECT_TRUE(h.app.downloads().modal());
    EXPECT_FALSE(h.store.action().error.empty());
    EXPECT_EQ(h.store.state().jobs.size(), 6u);
    h.frame(Direction::left); // Remove from history
    h.press(Action::confirm); // Opens a second confirmation; no request yet.
    EXPECT_FALSE(h.store.action().busy);
    EXPECT_EQ(h.transport.count("/api/v1/downloads/" + id + "/forget"), 0);
    h.frame(Direction::right); h.press(Action::confirm);
    ASSERT_TRUE(h.store.action().busy);
    h.store.run_next_action(); h.settle(3);
    EXPECT_EQ(h.transport.count("/api/v1/downloads/" + id + "/forget"), 1);
    EXPECT_EQ(h.store.state().jobs.size(), 5u);
    EXPECT_FALSE(h.app.downloads().modal());
}

// ---- App settings ----

TEST(App, VikingfileAccountIsReachableFromSourcesInEveryLook) {
    for (auto theme : {look::Interface::store, look::Interface::crossbar, look::Interface::cinema, look::Interface::panorama}) {
        SCOPED_TRACE(static_cast<int>(theme));
        NativeLook appearance(theme);
        AppHarness h;
        // Reopening Orbit while the account page exists must not recover it as
        // a download-verification overlay. Settings stays usable after closure.
        h.browser_override=R"({"available":true,"vikingAccountAvailable":true,"nativeDialogAvailable":true,"accountPage":true,"id":"account-1","active":true,"state":"waiting"})";
        h.store.poll_once(); h.settle();
        ASSERT_TRUE(h.store.state().browser.viking_account_available);
        h.app.open_settings(SettingsScreen::Section::sources,true); h.settle();
        h.browser_override=R"({"available":true,"vikingAccountAvailable":true,"nativeDialogAvailable":true,"accountPage":true,"id":"account-1","active":false,"state":"idle"})";
        h.store.poll_once(); h.settle();
        const int steps=theme==look::Interface::store ? 3 : static_cast<int>(h.store.state().sources.options.size())+2;
        for (int i=0;i<steps;++i) h.frame(Direction::down);
        h.press(Action::confirm);
        ASSERT_TRUE(h.store.browser_action().busy);
        h.store.run_next_action(); h.settle(3);
        bool opened=false;
        for (const auto &request:h.transport.seen) {
            if(request.method=="POST" && request.path=="/api/v1/browser") {
                EXPECT_NE(request.body.find("viking-account"),std::string::npos);
                EXPECT_EQ(request.body.find("releaseId"),std::string::npos);
                opened=true;
            }
        }
        EXPECT_TRUE(opened);
    }
}

TEST(App, FirstRunChoosesSourcesInTheAppAndOpensDiscover)
{
    AppHarness h(host::FixtureMode::setup);
    h.settle();
    h.press(Action::confirm); // Choose sources
    h.settle();
    h.frame(Direction::right); // Archive.org (after Vikingfile)
    h.press(Action::confirm);
    h.frame(Direction::down); // the download notice
    h.press(Action::confirm);
    h.frame(Direction::down); // Save sources
    h.press(Action::confirm);
    ASSERT_TRUE(h.store.setting_result().busy);
    h.store.run_next_action();
    h.settle(3);
    EXPECT_FALSE(h.store.state().setup_needed());
    EXPECT_EQ(h.store.state().sources.enabled, (std::vector<std::string>{"archive"}));
    // Saving finished setting up: back to Discover.
    h.store.poll_once();
    h.settle();
    EXPECT_EQ(h.app.current_tab(), orbit::tab::discover);
    h.press(Action::back); // nothing left open to close
    h.settle();
    EXPECT_EQ(h.store.state().games.size(), 13u);
}

TEST(App, SavingSourcesNeedsTheNoticeAccepted)
{
    AppHarness h(host::FixtureMode::setup);
    h.settle();
    h.press(Action::confirm);
    h.settle();
    h.press(Action::confirm); // Vikingfile
    h.frame(Direction::down); // the notice, left unaccepted
    h.frame(Direction::down); // Save sources
    h.press(Action::confirm); // refused
    EXPECT_FALSE(h.store.setting_result().busy);
}

TEST(App, SettingsChoosesTheDefaultDrive)
{
    AppHarness h;
    h.app.open_settings(SettingsScreen::Section::storage, true);
    h.settle();
    h.frame(Direction::down); // USB Extended Storage
    h.frame(Direction::down); // Internal storage
    h.press(Action::confirm);
    ASSERT_TRUE(h.store.setting_result().busy);
    h.store.run_next_action();
    h.settle(3);
    EXPECT_EQ(h.store.state().system.preferred_storage, "internal");
    // Circle leaves the section, then closes App settings.
    h.press(Action::back);
    h.press(Action::back);
    h.settle();
    EXPECT_EQ(h.app.current_tab(), orbit::tab::discover);
}

TEST(App, UpdatesInstallTheTvAppAndRestartTheServiceAfterConfirming)
{
    AppHarness h(host::FixtureMode::updates);
    h.store.poll_once();
    h.app.open_settings(SettingsScreen::Section::updates, true);
    h.settle();
    h.frame(Direction::down);  // past Update Orbit, to the TV app's buttons
    h.frame(Direction::right); // Update TV app
    h.press(Action::confirm);
    ASSERT_TRUE(h.store.setting_result().busy);
    h.store.run_next_action();
    h.settle(3);
    EXPECT_EQ(h.store.state().tv_app.installed_version, "1.1.0");
    h.frame(Direction::down);  // the download service's buttons
    h.frame(Direction::right); // Install update
    h.press(Action::confirm);
    h.store.run_next_action();
    h.settle(3);
    EXPECT_TRUE(h.store.state().service.restart_required);
    // Restart needs a second, explicit choice; Circle backs out of it.
    h.frame(Direction::right); // Restart download service
    h.press(Action::confirm);
    EXPECT_FALSE(h.store.setting_result().busy);
    h.press(Action::back);
    h.settle();
    EXPECT_EQ(h.store.state().restart, Restart::idle);
    h.press(Action::confirm); // Restart download service again
    h.frame(Direction::left); // Restart now
    h.press(Action::confirm);
    ASSERT_TRUE(h.store.setting_result().busy);
    h.store.run_next_action();
    h.settle(3);
    EXPECT_EQ(h.store.state().restart, Restart::stopping);
}

TEST(App, UpdateOrbitInstallsBothPartsInOnePress)
{
    AppHarness h(host::FixtureMode::updates);
    h.store.poll_once();
    h.app.open_settings(SettingsScreen::Section::updates, true);
    h.settle();
    h.press(Action::confirm); // Update Orbit, focused first
    ASSERT_TRUE(h.store.setting_result().busy);
    h.store.run_next_action();
    h.settle(3);
    int sent = 0;
    for (const http::Request &request : h.transport.seen)
        if (request.path == "/api/v1/updates" && request.body.find("\"update-all\"") != std::string::npos)
        {
            ++sent;
            EXPECT_NE(request.body.find("\"serviceVersion\":\"0.6.1\""), std::string::npos) << request.body;
            EXPECT_NE(request.body.find("\"tvAppVersion\":\"1.1.0\""), std::string::npos) << request.body;
        }
    EXPECT_EQ(sent, 1);
    EXPECT_EQ(h.store.state().tv_app.installed_version, "1.1.0");
    EXPECT_TRUE(h.store.state().service.update_all);
    EXPECT_TRUE(h.store.state().service.restart_required); // no loader here: the service waits for a restart
}

TEST(App, ClearFinishedHistoryRemovesOnlyFinishedDownloads)
{
    AppHarness h;
    h.store.poll_once();
    h.settle();
    const auto count = [&](const char *status)
    {
        int n = 0;
        for (const orbit::Job &job : h.store.state().jobs)
            n += job.status == status;
        return n;
    };
    ASSERT_GT(count("complete"), 0);
    const int others = static_cast<int>(h.store.state().jobs.size()) - count("complete");
    h.store.clear_history("complete");
    h.store.run_next_action();
    h.settle();
    EXPECT_EQ(count("complete"), 0);
    EXPECT_EQ(static_cast<int>(h.store.state().jobs.size()), others);
    EXPECT_EQ(h.transport.count("/api/v1/downloads/clear-history"), 1);
    EXPECT_TRUE(h.store.action().error.empty());
}

TEST(App, NotificationEventsAreChosenForThePs5AndNtfy)
{
    AppHarness h;
    h.store.poll_once();
    h.settle();
    ASSERT_TRUE(h.store.state().system.has_notification_events);
    for (bool on : h.store.state().system.notification_events)
        EXPECT_TRUE(on);
    h.store.set_notification_event(1, false); // downloadFailed
    h.store.run_next_action();
    h.settle();
    const auto &events = h.store.state().system.notification_events;
    EXPECT_TRUE(events[0]);
    EXPECT_FALSE(events[1]);
    EXPECT_TRUE(events[4]);
    EXPECT_EQ(h.transport.count("/api/v1/settings/notifications"), 1);
}

TEST(App, DownloadNotificationsToggleFromNotifications)
{
    AppHarness h;
    h.store.poll_once();
    h.settle();
    ASSERT_TRUE(h.store.state().system.has_download_notifications);
    ASSERT_TRUE(h.store.state().system.download_notifications);
    h.store.set_download_notifications(false);
    h.store.run_next_action();
    h.settle();
    EXPECT_FALSE(h.store.state().system.download_notifications);
    EXPECT_EQ(h.transport.count("/api/v1/settings/notifications"), 1);
}

TEST(App, DeletePackagesToggleFromStorage)
{
    AppHarness h;
    h.store.poll_once();
    h.settle();
    ASSERT_TRUE(h.store.state().system.has_delete_packages);
    ASSERT_FALSE(h.store.state().system.delete_packages);
    h.store.set_delete_packages(true);
    h.store.run_next_action();
    h.settle();
    EXPECT_TRUE(h.store.state().system.delete_packages);
    EXPECT_EQ(h.transport.count("/api/v1/settings/installation"), 1);
    h.store.poll_once();
    h.settle();
    EXPECT_TRUE(h.store.state().system.delete_packages); // the service keeps it
}

TEST(App, ConnectionsPerDownloadStepThroughPresets)
{
    AppHarness h;
    h.store.poll_once();
    h.settle();
    ASSERT_TRUE(h.store.state().system.has_download_connections);
    EXPECT_EQ(h.store.state().system.download_connections, 8);
    EXPECT_EQ(orbit::connection_choices(8), (std::vector<int>{1, 2, 4, 8, 12, 16, 24, 32, 48, 64}));
    EXPECT_EQ(orbit::connection_choices(20).size(), 11u); // a value set in the browser stays a choice
    h.store.set_download_connections(64);
    h.store.run_next_action();
    h.settle();
    EXPECT_EQ(h.store.state().system.download_connections, 64);
    h.store.set_download_connections(65); // refused before it is sent
    h.store.run_next_action();
    h.settle();
    EXPECT_EQ(h.store.state().system.download_connections, 64);
    EXPECT_EQ(h.transport.count("/api/v1/settings/downloads"), 1);
}

TEST(App, NtfySettingsSaveAndSendATest)
{
    AppHarness h;
    h.store.poll_once();
    h.store.want_settings(true);
    h.store.poll_once();
    h.settle();
    ASSERT_TRUE(h.store.state().ntfy.known);
    ASSERT_FALSE(h.store.state().ntfy.enabled);
    h.store.test_ntfy();
    h.store.run_next_action();
    h.settle();
    EXPECT_EQ(h.store.setting_result().error, "Save an ntfy server and topic first.");
    h.store.save_ntfy("{\"enabled\":true,\"server\":\"https://ntfy.sh\",\"topic\":\"orbit-downloads\",\"auth\":false,"
                      "\"username\":\"\",\"password\":\"\"}");
    h.store.run_next_action();
    h.settle();
    EXPECT_TRUE(h.store.setting_result().error.empty());
    EXPECT_TRUE(h.store.state().ntfy.enabled);
    EXPECT_EQ(orbit::ntfy_address(h.store.state().ntfy), "ntfy.sh/orbit-downloads");
    h.store.test_ntfy();
    h.store.run_next_action();
    h.settle();
    EXPECT_TRUE(h.store.setting_result().error.empty());
    EXPECT_EQ(h.transport.count("/api/v1/settings/ntfy/test"), 2);
}

TEST(App, MoreSettingsSavesADiagnosticReportToADrive)
{
    AppHarness h;
    h.store.poll_once();
    h.app.open_settings(SettingsScreen::Section::more, true);
    h.settle();
    h.store.save_diagnostics("1.9.0", "de|crossbar");
    h.store.run_next_action();
    h.settle();
    EXPECT_EQ(h.store.state().diagnostics_path, "/mnt/usb0/orbit-diagnostics/orbit-diagnostics-20261004-120000.txt");
    int sent = 0;
    for (const http::Request &request : h.transport.seen)
        if (request.path == "/api/v1/diagnostics/export")
        {
            ++sent;
            EXPECT_EQ(request.body, R"({"client":{"app":"tv","version":"1.9.0","language":"de","interface":"crossbar"}})");
        }
    EXPECT_EQ(sent, 1);
}

TEST(App, AppearanceLanguageSwitchesAtOnceAndIsKept)
{
    NativeLook appearance(look::Interface::crossbar);
    ASSERT_TRUE(i18n::load(std::string(HUI_SOURCE_DIR) + "/../i18n", "en"));
    i18n::set_system("en");
    AppHarness h;
    h.app.open_settings(SettingsScreen::Section::appearance, true);
    h.settle();
    // Interface, Ribbons, Motion, then Language.
    for (int i = 0; i < 3; ++i)
        h.frame(Direction::down);
    h.press(Action::confirm);
    h.settle(10);
    h.frame(Direction::down); // English
    h.frame(Direction::down); // Deutsch
    h.press(Action::confirm);
    h.settle();
    EXPECT_EQ(look::prefs().language, "de");
    EXPECT_EQ(i18n::language(), "de");
    EXPECT_STREQ(tr("Language"), "Sprache");
    // Automatic goes back to the console's language.
    h.press(Action::confirm);
    h.settle(10);
    h.frame(Direction::up);
    h.frame(Direction::up);
    h.press(Action::confirm);
    h.settle();
    EXPECT_EQ(look::prefs().language, "");
    EXPECT_EQ(i18n::language(), "en");
}

TEST(App, MoreSettingsShowsDiagnosticsOnScreen)
{
    AppHarness h;
    h.store.poll_once();
    h.app.open_settings(SettingsScreen::Section::more, true);
    h.settle();
    h.press(Action::confirm); // View diagnostics
    ASSERT_TRUE(h.app.settings().modal());
    h.store.run_next_action();
    h.settle();
    const DiagnosticsReport &report = h.store.state().diagnostics;
    ASSERT_TRUE(report.known);
    EXPECT_EQ(report.platform, "ps5");
    EXPECT_TRUE(report.state_healthy);
    EXPECT_EQ(report.launcher_status, "installed");
    ASSERT_EQ(report.downloads.size(), 2u);
    EXPECT_EQ(report.downloads[0].reason, "source-unavailable");
    ASSERT_EQ(report.events.size(), 3u);
    EXPECT_EQ(report.events[1].http_status, 503);
    EXPECT_EQ(report.events[1].received, 1048576.0);
    h.press(Action::confirm); // Refresh report
    h.store.run_next_action();
    h.settle();
    EXPECT_EQ(h.transport.count("/api/v1/diagnostics"), 2);
    h.press(Action::back);
    EXPECT_FALSE(h.app.settings().modal());
}

TEST(App, MoreSettingsSaveButtonSendsTheReportAndShowsOnlyTheLastFile)
{
    AppHarness h;
    h.store.poll_once();
    h.app.open_settings(SettingsScreen::Section::more, true);
    h.settle();
    h.frame(Direction::right);
    h.press(Action::confirm); // Save diagnostic report, after View diagnostics
    ASSERT_TRUE(h.store.setting_result().busy);
    h.store.run_next_action();
    h.settle(3);
    EXPECT_FALSE(h.store.state().diagnostics_path.empty());
    bool described = false;
    for (const http::Request &request : h.transport.seen)
        if (request.path == "/api/v1/diagnostics/export")
            described = request.body.find(R"("app":"tv")") != std::string::npos &&
                        request.body.find(R"("interface":"store")") != std::string::npos;
    EXPECT_TRUE(described);
    // A save that fails, or answers without a file, never shows the earlier file.
    for (const char *reply : {R"({"error":"Connect a writable drive to your PS5."})", R"({})"})
    {
        const std::string body = reply;
        const auto service = h.transport.reply;
        h.transport.reply = [&](const http::Request &request)
        {
            if (request.path == "/api/v1/diagnostics/export")
                return test::json_reply(body.find("error") != std::string::npos ? 409 : 200, body);
            return service(request);
        };
        h.press(Action::confirm);
        h.store.run_next_action();
        h.settle(3);
        h.transport.reply = service;
        EXPECT_TRUE(h.store.state().diagnostics_path.empty());
        EXPECT_FALSE(h.store.setting_result().error.empty());
    }
}

TEST(App, SurvivesRandomInputEverywhere)
{
    AppHarness h;
    std::mt19937 random(7);
    const Direction directions[] = {Direction::none, Direction::up, Direction::down,
                                    Direction::left, Direction::right};
    const Action actions[] = {Action::confirm,   Action::back,      Action::north,
                              Action::west,      Action::page_prev, Action::page_next,
                              Action::jump_prev, Action::jump_next};
    for (int i = 0; i < 3000; ++i)
    {
        const Direction nav = directions[random() % 5];
        const std::uint32_t press = random() % 4 == 0 ? hui::action_bit(actions[random() % 8]) : 0;
        h.frame(nav, press);
        if (h.store.action().busy)
            h.store.run_next_action();
        if (i % 500 == 0)
            h.store.poll_once();
    }
    SUCCEED();
}

} // namespace
} // namespace orbit

TEST(App, BrowserHandoffCannotChangeTheLocalOriginOrSmuggleACommand)
{
    EXPECT_EQ(orbit::browser_handoff_url({"game-a", "ppsa12345-viking", "usb1"}),
              "http://127.0.0.1:34177/#download?game=game-a&release=ppsa12345-viking&storage=usb1");
    EXPECT_TRUE(orbit::browser_handoff_url({"game-a", "r&start=true", "usb1"}).empty());
    EXPECT_TRUE(orbit::browser_handoff_url({"https://example.com", "r", "usb1"}).empty());
    EXPECT_TRUE(orbit::browser_handoff_url({"game-a", "r", ""}).empty());
    EXPECT_TRUE(orbit::browser_handoff_url({std::string(64, 'a'), "r", "usb1"}).empty());
}
