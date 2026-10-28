// Orbit Store TV app - Storefront rules shared with the browser storefront.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/model.hpp"

#include <gtest/gtest.h>

#include <tuple>
#include <algorithm>

namespace orbit
{
namespace
{

TEST(Downloads, ArchiveTransferUsesExtractedSizeAndNeverNegativeETA)
{
    Job job;
    job.delivery = "zero"; job.status = "downloading";
    job.total = 75.7e9; job.send_total = 124.9e9;
    job.received = 110.3e9; job.speed = 32e6;
    const auto p = job_progress(job);
    EXPECT_DOUBLE_EQ(p.total, 124.9e9);
    EXPECT_DOUBLE_EQ(p.done, 110.3e9);
    EXPECT_DOUBLE_EQ(p.remaining, 14.6e9);
    EXPECT_DOUBLE_EQ(p.minutes, 8);
    job.received = 125e9;
    EXPECT_DOUBLE_EQ(job_progress(job).minutes, 0);
    EXPECT_DOUBLE_EQ(job_progress(job).remaining, 0);
    job.status = "extracting"; job.extracted = 40; job.extract_total = 100;
    EXPECT_DOUBLE_EQ(job_progress(job).total, 100);
    EXPECT_DOUBLE_EQ(job_progress(job).done, 40);
    job.status = "downloading"; job.send_total = 0; job.received = 10e9;
    EXPECT_DOUBLE_EQ(job_progress(job).total, job.total);
}

Release make_release(const char *id, const char *game, const char *title, const char *date,
                     double gigabytes, const char *title_id = "PPSA00001")
{
    Release release;
    release.id = id;
    release.game_id = game;
    release.title = title;
    release.release_date = date;
    release.size_bytes = gigabytes * 1e9;
    release.title_id = title_id;
    release.format = "FFPFSC";
    release.filename = std::string(id) + ".ffpfsc";
    return release;
}

Job job_for(const Release &release, const char *status, const char *storage = "usb0")
{
    Job job;
    job.id = std::string("job-") + release.id;
    job.release_id = release.id;
    job.title_id = release.title_id;
    job.format = release.format;
    job.filename = release.filename;
    job.total = release.size_bytes;
    job.status = status;
    job.storage_id = storage;
    return job;
}

TEST(Addons, InstalledBaseDoesNotSuppressDLCAndSearchFindsItsBase)
{
    Release r = make_release("dlc", "base-dlc", "Additional campaign", "", .1, "CUSA00001");
    r.platform="ps4"; r.format="PKG"; r.filename="dlc.pkg";
    r.addon_id="base-dlc"; r.base_game_id="base"; r.base_title="Base game"; r.addon_type="expansion";
    r.content_id="UP0001-CUSA00001_00-CAMPAIGN0000000";
    r.required_title_ids={"CUSA00001"};
    LibraryGame installed; installed.title_id=r.title_id; installed.platform="ps4"; installed.installed=true;
    EXPECT_EQ(release_state(r, {}, {installed}).kind, CollectionState::Kind::none);
    auto games=group_games({r});
    EXPECT_EQ(games.front().addon_id,"base-dlc");
    BrowseFilters f; f.query="base game";
    EXPECT_EQ(browse(games,f,{}).size(),1u);
    f.addon_type="activation"; EXPECT_TRUE(browse(games,f,{}).empty());
    f.addon_type="expansion"; f.addon_base="base"; EXPECT_EQ(browse(games,f,{}).size(),1u);
    f.addon_base="another"; EXPECT_TRUE(browse(games,f,{}).empty());
    // As the browser's AddonInfo: the base game is confirmed only from an installed or saved copy.
    std::vector<LibraryGame> library(1);
    library[0].title_id="CUSA00001";
    const auto notice=addon_requirements(r, library);
    EXPECT_NE(notice.find("CUSA00001"),std::string::npos);
    EXPECT_NE(notice.find("Unknown"),std::string::npos);
    EXPECT_NE(notice.find("could not be confirmed"),std::string::npos);
    EXPECT_NE(notice.find("You can still download this add-on."),std::string::npos);
    library[0].on_drive=true;
    EXPECT_NE(addon_requirements(r, library).find("A matching base-game title ID was found."),std::string::npos);
    library[0].title_id="CUSA00002";
    EXPECT_NE(addon_requirements(r, library).find("could not be confirmed"),std::string::npos);
}

TEST(ArchiveContents, PackagingFiltersDoNotGuessTheGameFormat)
{
    Release archive = make_release("zip", "game", "Game", "", 2);
    archive.format = "Unknown"; archive.compression = "zip"; archive.archive_unverified = true;
    EXPECT_EQ(format_label(archive), "ZIP");
    const auto games = group_games({archive});
    BrowseFilters filters;
    EXPECT_EQ(browse(games, filters, {}).size(), 1u);
    filters.formats = {"ZIP"};
    EXPECT_EQ(browse(games, filters, {}).size(), 1u);
    filters.formats = {"exFAT"};
    EXPECT_TRUE(browse(games, filters, {}).empty());
    filters.formats.clear();
    EXPECT_EQ(browse(games, filters, {}).size(), 1u);
    const auto choices = format_choices(games);
    EXPECT_EQ(choices, (std::vector<std::string>{"ZIP"}));
    Job job = job_for(archive, "inspecting");
    EXPECT_TRUE(job_active(job));
}

TEST(Model, ArchivePasswordRetryUsesBackendCapability)
{
    json::Value value;
    ASSERT_TRUE(json::parse(R"([{"id":"archive-job","status":"error","canEnterArchivePassword":true}])", &value));
    const auto jobs = parse_jobs(value);
    ASSERT_EQ(jobs.size(), 1u);
    EXPECT_TRUE(jobs.front().can_enter_archive_password);
    ASSERT_TRUE(json::parse(R"([{"id":"old-job","status":"error"}])", &value));
    EXPECT_FALSE(parse_jobs(value).front().can_enter_archive_password);
}

TEST(Model, GridJumpKeepsTheColumnAndStopsAtTheEnds)
{
    EXPECT_EQ(grid_jump(1, 250, 6, 16), 97);
    EXPECT_EQ(grid_jump(193, 250, 6, 16), 247); // the last row, same column
    EXPECT_EQ(grid_jump(5, 250, 6, 100), 249);  // a short last row ends on its final tile
    EXPECT_EQ(grid_jump(151, 250, 6, -16), 55);
    EXPECT_EQ(grid_jump(55, 250, 6, -16), 1);
    EXPECT_EQ(grid_jump(3, 4, 6, 16), 3); // one row: nowhere to go
    EXPECT_EQ(grid_jump(0, 0, 6, 16), 0);
}

TEST(Model, DownloadLimitDefaultsAndReadsSharedConsoleSetting)
{
    json::Value value;
    ASSERT_TRUE(json::parse("{}", &value));
    EXPECT_EQ(parse_system(value).concurrent_downloads, 1);
    ASSERT_TRUE(json::parse(R"({"concurrentDownloads":8})", &value));
    EXPECT_EQ(parse_system(value).concurrent_downloads, 8);
    ASSERT_TRUE(json::parse(R"({"concurrentDownloads":0})", &value));
    EXPECT_EQ(parse_system(value).concurrent_downloads, 1);
    ASSERT_TRUE(json::parse(R"({"concurrentDownloads":9})", &value));
    EXPECT_EQ(parse_system(value).concurrent_downloads, 1);
}

TEST(Model, GroupsOptionsIntoOneCardPerGameInCatalogueOrder)
{
    const std::vector<Game> games = group_games({make_release("a1", "alpha", "Alpha", "", 1),
                                                 make_release("b1", "beta", "Beta", "", 2),
                                                 make_release("a2", "alpha", "Alpha EU", "", 3)});
    ASSERT_EQ(games.size(), 2u);
    EXPECT_EQ(games[0].id, "alpha");
    EXPECT_EQ(games[0].title, "Alpha"); // the first option names the card
    EXPECT_EQ(games[0].releases.size(), 2u);
    EXPECT_EQ(games[1].releases.size(), 1u);
}

TEST(Model, LatestReleasesAreDatedPastAndNewestFirst)
{
    std::vector<Release> releases;
    releases.push_back(make_release("old", "old", "Old", "2024-01-01", 1));
    releases.push_back(make_release("none", "none", "Undated", "", 1));
    releases.push_back(make_release("new", "new", "New", "2026-09-30", 1));
    releases.push_back(make_release("future", "future", "Future", "2027-01-01", 1));
    releases.push_back(make_release("tie-b", "tie-b", "b tie", "2025-05-05", 1));
    releases.push_back(make_release("tie-a", "tie-a", "A tie", "2025-05-05", 1));
    const std::vector<Game> games = group_games(releases);
    const std::vector<int> latest = latest_games(games, "2026-10-04");
    std::vector<std::string> ids;
    for (int index : latest)
        ids.push_back(games[static_cast<std::size_t>(index)].id);
    EXPECT_EQ(ids, (std::vector<std::string>{"new", "tie-a", "tie-b", "old"}));

    std::vector<Release> many;
    for (int i = 0; i < 30; ++i)
    {
        const std::string id = "g" + std::to_string(i);
        many.push_back(make_release(id.c_str(), id.c_str(), id.c_str(), "2026-01-01", 1));
    }
    EXPECT_EQ(latest_games(group_games(many), "2026-10-04").size(), 20u);
}

TEST(Model, Ps4EditionsFilterAndMatchOnlyTheirOwnPlatform)
{
    json::Value value;
    ASSERT_TRUE(json::parse(R"([
      {"id":"ps5","gameId":"same-ps5","title":"Same title","titleId":"PPSA12345","format":"FPKG"},
      {"id":"ps4","gameId":"same-ps4","title":"Same title","titleId":"CUSA12345","platform":"ps4","format":"PKG","filename":"CUSA12345.pkg","sizeBytes":123,"notes":"Languages: English"}
    ])", &value));
    const auto releases = parse_catalog(value);
    ASSERT_EQ(releases.size(), 2u);
    EXPECT_EQ(releases[0].platform, "ps5");
    EXPECT_EQ(releases[1].notes, "Languages: English");
    const auto games = group_games(releases);
    BrowseFilters filters;
    filters.platform = "ps4";
    EXPECT_TRUE(filters.narrowed());
    EXPECT_EQ(browse(games, filters, {}), (std::vector<int>{1}));
    filters.query = "PPSA12345";
    EXPECT_TRUE(browse(games, filters, {}).empty());
    LibraryGame copy;
    copy.platform = "ps4"; copy.title_id = "CUSA12345"; copy.format = "PKG";
    copy.path = "/data/homebrew/CUSA12345.pkg"; copy.on_drive = true; copy.size = 123;
    EXPECT_EQ(release_state(releases[1], {}, {copy}).kind, CollectionState::Kind::library);
    copy.platform = "ps5";
    EXPECT_EQ(release_state(releases[1], {}, {copy}).kind, CollectionState::Kind::none);
}

TEST(Model, NewlyAddedUsesCatalogueDatesWithTimezoneOffsetsAndCapsTheRow)
{
    Release old = make_release("old", "old", "Old", "2026-10-07", 2);
    old.added_at = "2026-10-03T22:00:00+05:30";
    Release newer = make_release("new", "new", "New", "2020-01-01", 2);
    newer.added_at = "2026-10-03T18:00:00Z";
    Release tie = make_release("tie", "tie", "A tied addition", "", 2);
    tie.added_at = "2026-10-03T20:00:00+02:00";
    Release invalid = make_release("bad", "bad", "Bad", "", 2);
    invalid.added_at = "not-a-date";
    EXPECT_EQ(newly_added_games(group_games({old, newer, tie, invalid,
                  make_release("none", "none", "None", "", 2)})), (std::vector<int>{0, 1, 2}));
    std::vector<Release> many;
    for (int i = 0; i < 45; ++i)
    {
        Release r = newer;
        r.game_id = std::to_string(i);
        many.push_back(r);
    }
    old.release_date = "2026-10-07";
    many.push_back(old); // an older addition cannot enter just because its game is newer
    const auto picked = newly_added_games(group_games(many));
    EXPECT_EQ(picked.size(), 40u);
    EXPECT_TRUE(std::find(picked.begin(), picked.end(), 45) == picked.end());
}

TEST(Model, NewOnOrbitPicksLeadRecentAdditionsWithoutDuplicates)
{
    json::Value value;
    ASSERT_TRUE(json::parse(R"([
      {"id":"second","gameId":"second","title":"A","newOnOrbitRank":2},
      {"id":"first","gameId":"first","title":"Z","newOnOrbitRank":1,"addedAt":"2020-01-01T00:00:00Z"},
      {"id":"bad","gameId":"bad","newOnOrbitRank":1.5},
      {"id":"string","gameId":"string","newOnOrbitRank":"1"}
    ])", &value));
    auto releases = parse_catalog(value);
    EXPECT_EQ(releases[2].new_on_orbit_rank, 0);
    EXPECT_EQ(releases[3].new_on_orbit_rank, 0);
    for (int i = 0; i < 45; ++i)
    {
        Release recent = make_release(("r" + std::to_string(i)).c_str(), ("g" + std::to_string(i)).c_str(), ("Game " + std::to_string(i)).c_str(), i == 5 ? "2025-01-01" : "", 2);
        recent.added_at = "2026-10-07T10:00:00Z";
        releases.push_back(recent);
    }
    const auto games = group_games(releases);
    const auto picked = newly_added_games(games);
    ASSERT_EQ(picked.size(), 40u);
    EXPECT_EQ(picked[0], 1);
    EXPECT_EQ(picked[1], 0);
    EXPECT_EQ(std::count(picked.begin(), picked.end(), 1), 1);
    EXPECT_EQ(std::count(picked.begin(), picked.end(), 0), 1);
}

TEST(Model, RegionFiltersMatchTheSameOptionAndKeepUnknownExplicit)
{
    json::Value parsed;
    ASSERT_TRUE(json::parse(R"([
      {"id":"image","gameId":"game","format":"FFPFSC","region":"EUR","sourceId":"archive","sizeBytes":10000000000},
      {"id":"pkg","gameId":"game","format":"FPKG","region":"USA","sourceId":"vikingfile","sizeBytes":60000000000},
      {"id":"missing","gameId":"unknown","format":"FPKG","sourceId":"vikingfile","sizeBytes":2000000000}
    ])", &parsed));
    const auto games = group_games(parse_catalog(parsed));
    BrowseFilters filters;
    filters.region = "EUR";
    filters.formats = {"FPKG"};
    EXPECT_TRUE(browse(games, filters, {}).empty());
    filters.region = "USA";
    filters.sources = {"vikingfile"};
    EXPECT_EQ(browse(games, filters, {}), (std::vector<int>{0}));
    filters.size = SizeFilter::small;
    EXPECT_TRUE(browse(games, filters, {}).empty());
    filters.region = "unknown";
    EXPECT_EQ(browse(games, filters, {}), (std::vector<int>{1}));
    EXPECT_TRUE(filters.narrowed());
    EXPECT_EQ(browse(games, BrowseFilters{}, {}).size(), 2u);
}

TEST(Model, SearchMatchesTitlesAndAnyOptionsTitleId)
{
    const std::vector<Game> games =
        group_games({make_release("a", "a", "Ember Hollow", "", 5, "PPSA90003"),
                     make_release("a-eu", "a", "Ember Hollow", "", 5, "PPSB90003"),
                     make_release("b", "b", "Tidewater", "", 1, "PPSA90001")});
    const auto search = [&](const char *query)
    {
        BrowseFilters filters;
        filters.query = query;
        return browse(games, filters, {});
    };
    EXPECT_EQ(search("  hollow ").size(), 1u);
    EXPECT_EQ(search("ppsb9"), (std::vector<int>{0}));
    EXPECT_EQ(search("").size(), 2u);
    EXPECT_TRUE(search("zelda").empty());
}

TEST(Model, BrowseSortsByTitleDateOrSmallestOption)
{
    const std::vector<Game> games = group_games(
        {make_release("c", "c", "charlie", "2024-01-01", 30),
         make_release("a", "a", "Alpha", "", 50), make_release("a2", "a", "Alpha", "", 2),
         make_release("b", "b", "Bravo", "2026-01-01", 10)});
    const auto titles = [&](Sort sort)
    {
        BrowseFilters filters;
        filters.sort = sort;
        std::vector<std::string> out;
        for (int index : browse(games, filters, {}))
            out.push_back(games[static_cast<std::size_t>(index)].id);
        return out;
    };
    // The browser's default is release date, newest first, undated last.
    EXPECT_EQ(BrowseFilters{}.sort, Sort::release);
    EXPECT_EQ(titles(Sort::release), (std::vector<std::string>{"b", "c", "a"}));
    EXPECT_EQ(titles(Sort::title), (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_EQ(titles(Sort::title_desc), (std::vector<std::string>{"c", "b", "a"}));
    EXPECT_EQ(titles(Sort::size), (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_EQ(titles(Sort::size_desc), (std::vector<std::string>{"c", "b", "a"}));
}

TEST(Model, BrowseFiltersMatchOneOptionAndFavourites)
{
    Release archive = make_release("a", "a", "Alpha", "", 60);
    archive.source_id = "archive";
    archive.provider = "Archive.org";
    Release viking = make_release("a-v", "a", "Alpha", "", 8);
    viking.source_id = "vikingfile";
    viking.provider = "Vikingfile";
    viking.format = "exFAT";
    Release bravo = make_release("b", "b", "Bravo", "", 120);
    bravo.source_id = "archive";
    bravo.provider = "Archive.org";
    const std::vector<Game> games = group_games({archive, viking, bravo});
    const auto ids = [&](const BrowseFilters &filters, const std::vector<std::string> &favourites)
    {
        std::vector<std::string> out;
        for (int index : browse(games, filters, favourites))
            out.push_back(games[static_cast<std::size_t>(index)].id);
        return out;
    };
    BrowseFilters filters;
    filters.sort = Sort::title;
    filters.sources = {"vikingfile"};
    EXPECT_EQ(ids(filters, {}), (std::vector<std::string>{"a"}));
    // Source, format and size must hold for the same option.
    filters.size = SizeFilter::large;
    EXPECT_TRUE(ids(filters, {}).empty());
    filters = BrowseFilters{};
    filters.sort = Sort::title;
    filters.size = SizeFilter::huge;
    EXPECT_EQ(ids(filters, {}), (std::vector<std::string>{"b"}));
    filters = BrowseFilters{};
    filters.formats = {"exFAT"};
    EXPECT_EQ(ids(filters, {}), (std::vector<std::string>{"a"}));
    // Size sorting uses the smallest option that matches the filters.
    filters = BrowseFilters{};
    filters.sources = {"archive"};
    filters.sort = Sort::size;
    EXPECT_EQ(ids(filters, {}), (std::vector<std::string>{"a", "b"}));
    filters.favourites = true;
    EXPECT_EQ(ids(filters, {"b", "gone"}), (std::vector<std::string>{"b"}));
    EXPECT_TRUE(filters.narrowed());
    EXPECT_FALSE(BrowseFilters{}.narrowed());
    EXPECT_EQ(source_choices(games).size(), 2u);
    EXPECT_EQ(format_choices(games), (std::vector<std::string>{"FFPFSC", "exFAT"}));
    EXPECT_STREQ(sort_label(Sort::release), "Release date (newest first)");
    EXPECT_STREQ(size_label(SizeFilter::small), "Under 10 GB");
}

TEST(Model, FpkgIsASeparateSelectableFormatWithinTheSameGame)
{
    Release image = make_release("image", "game", "Game", "", 10);
    image.source_id = "archive";
    Release pkg = image;
    pkg.id = "pkg";
    pkg.source_id = "vikingfile";
    pkg.format = "FPKG";
    pkg.filename = "PPSA00001.pkg";
    const auto games = group_games({image, pkg});
    ASSERT_EQ(games.size(), 1u);
    EXPECT_EQ(format_choices(games), (std::vector<std::string>{"FFPFSC", "FPKG"}));
    BrowseFilters filters;
    filters.formats = {"FPKG"};
    EXPECT_EQ(browse(games, filters, {}).size(), 1u);
    filters.sources = {"archive"};
    EXPECT_TRUE(browse(games, filters, {}).empty());
}

TEST(Model, AJobBelongsToAnOptionOnlyWhileItsIdentityMatches)
{
    const Release release = make_release("r", "g", "Game", "", 10);
    Job job = job_for(release, "downloading");
    std::vector<Job> jobs{job};
    EXPECT_NE(release_job(release, jobs), nullptr);
    // A catalogue revision that changes the file must not relabel the saved job.
    jobs[0].total = 11e9;
    EXPECT_EQ(release_job(release, jobs), nullptr);
    jobs[0] = job_for(release, "cancelled");
    EXPECT_EQ(release_job(release, jobs), nullptr);
    // The most urgent state wins.
    jobs = {job_for(release, "complete", "usb0"), job_for(release, "paused", "internal")};
    EXPECT_EQ(release_job(release, jobs)->status, "paused");
    const Game game = group_games({release})[0];
    EXPECT_STREQ(game_state(game, jobs, {}).label, "Paused");
    EXPECT_EQ(game_state(game, std::vector<Job>{}, {}).label, nullptr);
}

TEST(Model, CollectionStatesFollowTheBrowsersRules)
{
    const Release release = make_release("r", "g", "Game", "", 10, "PPSA00009");
    LibraryGame copy;
    copy.title_id = "PPSA00009";
    copy.platform = "ps5";
    copy.on_drive = true;
    copy.format = "FFPFSC";
    copy.path = "/mnt/usb0/homebrew/r.ffpfsc";
    copy.location = "USB 1";
    // An exact copy: same title ID, format and filename, no version.
    CollectionState state = release_state(release, {}, {copy});
    EXPECT_EQ(state.kind, CollectionState::Kind::library);
    EXPECT_STREQ(state.label, "In Library");
    // A different filename is only related.
    copy.path = "/mnt/usb0/homebrew/other.ffpfsc";
    state = release_state(release, {}, {copy});
    EXPECT_EQ(state.kind, CollectionState::Kind::related);
    EXPECT_STREQ(state.label, "Related copy in Library");
    // A versioned option is never an exact match.
    Release versioned = release;
    versioned.version = "1.05";
    copy.path = "/mnt/usb0/homebrew/r.ffpfsc";
    EXPECT_EQ(release_state(versioned, {}, {copy}).kind, CollectionState::Kind::related);
    // An unfinished download outranks the Library; a finished one comes after it.
    EXPECT_STREQ(release_state(release, {job_for(release, "queued")}, {copy}).label, "Queued");
    EXPECT_STREQ(release_state(release, {job_for(release, "complete")}, {copy}).label,
                 "In Library");
    EXPECT_STREQ(release_state(release, {job_for(release, "complete")}, {}).label, "Downloaded");
    // PS4 copies and missing sources don't count.
    copy.platform = "ps4";
    EXPECT_EQ(release_state(release, {}, {copy}).kind, CollectionState::Kind::none);
}

TEST(Model, FilesParse)
{
    json::Value value;
    ASSERT_TRUE(json::parse(R"({"storageId":"usb0","path":"homebrew","truncated":true,"entries":[
        {"name":"Lantern Keep","folder":true,"type":"folder","sizeBytes":null,"modifiedAt":1791000000,"locked":""},
        {"name":"Tidewater.ffpfsc","folder":false,"type":"image","sizeBytes":48200000000,"modifiedAt":1791000000,"locked":"game"},
        {"name":"","folder":false,"type":"file"}]})",
                            &value));
    const FileListing listing = parse_file_listing(value);
    EXPECT_EQ(listing.storage_id, "usb0");
    EXPECT_EQ(listing.path, "homebrew");
    EXPECT_TRUE(listing.truncated);
    ASSERT_EQ(listing.entries.size(), 2u); // a nameless entry is dropped
    EXPECT_TRUE(listing.entries[0].folder);
    EXPECT_EQ(listing.entries[0].size, -1.0);
    EXPECT_EQ(listing.entries[1].size, 48200000000.0);
    EXPECT_EQ(listing.entries[1].locked, "game");

    FileOperation operation;
    ASSERT_TRUE(json::parse(R"({"operation":{"id":"op1","action":"copy","state":"paused","name":"Starfall Rally.exfat",
        "storageId":"usb0","path":"homebrew/Starfall Rally.exfat","toStorageId":"internal","toPath":"homebrew",
        "processedBytes":14600000000,"totalBytes":38900000000,"speed":0,"error":"","sizeBytes":null}})",
                            &value));
    ASSERT_TRUE(parse_file_operation(value, &operation));
    EXPECT_EQ(operation.action, "copy");
    EXPECT_EQ(operation.to_storage_id, "internal");
    EXPECT_FALSE(operation.ended());
    EXPECT_EQ(operation.size, -1.0);
    ASSERT_TRUE(json::parse(R"({"operation":null})", &value));
    EXPECT_FALSE(parse_file_operation(value, &operation));
    EXPECT_TRUE(operation.id.empty());
}

TEST(Model, LibraryAndFavouritesParse)
{
    json::Value value;
    ASSERT_TRUE(json::parse(R"({"status":"ready","provider":"ShadowMount","busy":false,
        "games":[{"titleId":"PPSA00009","title":"Game","platform":"ps5","format":"FFPFSC",
        "path":"/mnt/usb0/g.ffpfsc","sourceKey":"k","location":"USB 1","installed":true,
        "mounted":false,"onDrive":true,"managed":true,"canManageSource":true,
        "installedSizeBytes":null,"sizeBytes":1000,"sizeStatus":"ready"},{"title":"no id"},
        {"titleId":"CUSA00010","title":"Found","platform":"ps4","format":"PKG","path":"/user/app/CUSA00010/app.pkg",
        "sourceKey":"f","location":"Console storage","installed":true,"mounted":false,"onDrive":true,
        "managed":false,"canManageSource":false,"installedSizeBytes":null,"sizeBytes":5,"sizeStatus":"ready",
        "foundByOrbit":true}],
        "capabilities":["list_games","mount_game"],
        "action":{"id":"x","action":"mount","titleId":"PPSA00009","state":"running","message":"Mounting"},
        "storageJob":null})",
                            &value));
    const LibrarySnapshot library = parse_library(value);
    EXPECT_TRUE(library.ready());
    ASSERT_EQ(library.games.size(), 2u);
    EXPECT_FALSE(library.games[0].found_by_orbit);
    EXPECT_TRUE(library.games[1].found_by_orbit);
    EXPECT_EQ(library.games[0].installed_size, -1.0);
    EXPECT_EQ(library.games[0].size, 1000.0);
    EXPECT_TRUE(library.can("mount_game"));
    EXPECT_FALSE(library.can("move_game_source"));
    EXPECT_TRUE(library.working());
    EXPECT_NE(library.game("PPSA00009", "k"), nullptr);
    EXPECT_EQ(library.game("PPSA00009", "other"), nullptr);
    ASSERT_TRUE(json::parse(R"(["a","",7,"b","a"])", &value));
    EXPECT_EQ(parse_favourites(value), (std::vector<std::string>{"a", "b"}));
    ASSERT_TRUE(
        json::parse(R"([{"id":"x","gameId":"g","browserAvailable":true,"directAvailable":false},
        {"id":"y","gameId":"g"}])",
                    &value));
    const std::vector<Release> releases = parse_catalog(value);
    ASSERT_EQ(releases.size(), 2u);
    EXPECT_TRUE(releases[0].browser_only());
    EXPECT_FALSE(releases[1].browser_only());
}

TEST(Model, SpaceIsPlannedAgainstTheDrivesQueue)
{
    const Release release = make_release("r", "g", "Game", "", 10);
    Drive drive;
    drive.id = "usb0";
    drive.projected_free_bytes = 12e9;
    SpacePlan plan = plan_space(release, drive, {});
    EXPECT_DOUBLE_EQ(plan.after, 2e9);
    EXPECT_TRUE(plan.enough);
    EXPECT_FALSE(plan.planned);

    // Already queued on this drive: the projection already counts it.
    plan = plan_space(release, drive, {job_for(release, "queued")});
    EXPECT_TRUE(plan.planned);
    EXPECT_DOUBLE_EQ(plan.after, 12e9);

    // Fresh downloads cannot reuse cancelled progress (it may be deleted).
    Job cancelled = job_for(release, "cancelled");
    cancelled.received = 9e9;
    plan = plan_space(release, drive, {cancelled});
    EXPECT_DOUBLE_EQ(plan.after, 2e9);
    EXPECT_FALSE(plan.planned);
    plan = plan_space(release, drive, {cancelled, job_for(release, "queued")});
    EXPECT_TRUE(plan.planned);
    EXPECT_DOUBLE_EQ(plan.after, 12e9);

    drive.projected_free_bytes = 10e9 + kSpaceMargin - 1.0;
    EXPECT_FALSE(plan_space(release, drive, {}).enough);
}

TEST(Model, CompressedOptionsNeedRoomForTheArchiveAndItsFiles)
{
    Release release = make_release("r", "g", "Game", "", 10);
    release.compression = "7z";
    release.filename = "r.7z";
    release.extracted_bytes = 25e9;
    EXPECT_EQ(format_label(release), "7z \xC2\xB7 FFPFSC");
    release.compression = "zip";
    EXPECT_EQ(format_label(release), "ZIP \xC2\xB7 FFPFSC");
    release.format = "Game folder"; // an archive holds one game
    EXPECT_EQ(format_label(release), "ZIP \xC2\xB7 Game");
    release.compression = "7z";
    EXPECT_EQ(format_label(release), "7z \xC2\xB7 Game");
    Drive drive;
    drive.id = "usb0";
    drive.projected_free_bytes = 30e9;
    EXPECT_DOUBLE_EQ(plan_space(release, drive, {}).after, -5e9);
    EXPECT_FALSE(plan_space(release, drive, {}).enough);
    EXPECT_EQ(format_label(make_release("p", "g", "Game", "", 1)), "FFPFSC");
}

TEST(Model, ReadsTheInstallOfAFinishedPackage)
{
    json::Value value;
    ASSERT_TRUE(json::parse(R"([{"id":"a","status":"complete","install":{"state":"installing","progress":42,"error":"","code":0}},
        {"id":"b","status":"complete","install":{"state":"failed","progress":0,"error":"The console would not install this package.","code":-5}},
        {"id":"c","status":"complete"}])", &value));
    const auto jobs = parse_jobs(value);
    ASSERT_EQ(jobs.size(), 3u);
    EXPECT_EQ(jobs[0].install_state, "installing");
    EXPECT_DOUBLE_EQ(jobs[0].install_progress, 42.0);
    EXPECT_EQ(jobs[1].install_error, "The console would not install this package.");
    EXPECT_EQ(jobs[1].install_code, -5);
    // A service without the feature sends nothing, and the app shows nothing extra.
    EXPECT_TRUE(jobs[2].install_state.empty());
    EXPECT_EQ(jobs[2].install_code, 0);
}

TEST(Model, ExtractingIsAnActiveDownloadState)
{
    const Release release = make_release("r", "g", "Game", "", 10);
    Job job = job_for(release, "extracting");
    EXPECT_TRUE(job_active(job));
    EXPECT_STREQ(status_label(job.status), "Extracting");
    // It outranks a paused copy of the same option on another drive.
    const std::vector<Job> jobs{job_for(release, "paused", "internal"), job};
    EXPECT_EQ(release_job(release, jobs)->status, "extracting");
}

TEST(Model, PrefersTheConsolesPreferredDriveThenExternal)
{
    std::vector<Drive> drives(3);
    drives[0].id = "internal";
    drives[1].id = "usb0";
    drives[1].external = true;
    drives[2].id = "usb1";
    drives[2].external = true;
    EXPECT_EQ(default_drive(drives, "usb1"), 2);
    EXPECT_EQ(default_drive(drives, "gone"), 1);
    drives[1].external = drives[2].external = false;
    EXPECT_EQ(default_drive(drives, ""), 0);
    EXPECT_EQ(default_drive({}, "usb0"), -1);
}

TEST(Model, JobViewsAndLabelsMatchTheWebStorefront)
{
    Job job;
    for (const auto &[status, view, label] :
         std::vector<std::tuple<const char *, JobView, const char *>>{
             {"downloading", JobView::active, "Downloading"},
             {"queued", JobView::active, "Queued"},
             {"paused", JobView::active, "Paused"},
             {"retrying", JobView::active, "Retrying"},
             {"error", JobView::failed, "Needs attention"},
             {"complete", JobView::finished, "Downloaded"},
             {"cancelled", JobView::cancelled, "Cancelled"}})
    {
        job.status = status;
        EXPECT_EQ(job_view(job), view) << status;
        EXPECT_STREQ(status_label(status), label);
    }
    job.status = "paused";
    EXPECT_FALSE(job_active(job));
    job.status = "verifying";
    EXPECT_TRUE(job_active(job));
}

TEST(Model, FormatsSizesAndDatesLikeTheWebStorefront)
{
    EXPECT_EQ(format_bytes(61.7e9), "61.7 GB");
    EXPECT_EQ(format_bytes(512e6), "512.0 MB");
    EXPECT_EQ(format_bytes(1420e9), "1,420.0 GB"); // grouped, as the browser shows it
    EXPECT_EQ(format_date("2026-07-18"), "Jul 18, 2026");
    EXPECT_EQ(format_date("2026-13-01"), "2026-13-01");
    EXPECT_EQ(format_date("soon"), "soon");
}

TEST(Model, ParsesTolerantlyFromTheApi)
{
    json::Value value;
    ASSERT_TRUE(json::parse(R"([{"id":"j2","order":2,"status":"queued","total":5},
                                {"id":"j1","order":1,"status":"complete","received":5,"total":5,"path":"/data/homebrew/[DLPSGAME.COM]-PPSA19068"},
                                {"status":"orphan"}])",
                            &value));
    const std::vector<Job> jobs = parse_jobs(value);
    ASSERT_EQ(jobs.size(), 2u);
    EXPECT_EQ(jobs[0].id, "j1");
    EXPECT_EQ(jobs[0].path, "/data/homebrew/[DLPSGAME.COM]-PPSA19068");
    EXPECT_TRUE(jobs[1].path.empty());
    ASSERT_TRUE(json::parse(
        R"([{"id":"r","gameId":"g","version":null,"releaseDate":null,"sizeBytes":3},{"id":"x"}])",
        &value));
    const std::vector<Release> releases = parse_catalog(value);
    ASSERT_EQ(releases.size(), 1u);
    EXPECT_EQ(releases[0].version, "");
    ASSERT_TRUE(
        json::parse(R"({"enabled":["archive",5],"acknowledged":true,"noticeVersion":1})", &value));
    const Sources sources = parse_sources(value);
    EXPECT_EQ(sources.enabled, (std::vector<std::string>{"archive"}));
}

TEST(Model, TheAppsVersionComesFromItsContentVersion)
{
    // As the release tooling numbers the TV app (tools/release-artifacts.mjs).
    EXPECT_EQ(app_release_version("01.000.000"), "1.0.0");
    EXPECT_EQ(app_release_version("01.002.010"), "1.2.10");
    EXPECT_EQ(app_release_version("12.345.678"), "12.345.678");
    EXPECT_EQ(app_release_version("1.0.0"), "");
    EXPECT_EQ(app_release_version("01.00a.000"), "");
    EXPECT_EQ(app_release_version("01-000-000"), "");
    EXPECT_EQ(app_release_version(""), "");
}

TEST(Model, AgesAndChecksAreRelativeToNow)
{
    const double now = 1'800'000'000.0;
    EXPECT_EQ(format_age(0.0, now), "never");
    EXPECT_EQ(format_age(now - 20.0, now), "just now");
    EXPECT_EQ(format_age(now + 50.0, now), "just now"); // a clock ahead of the console
    EXPECT_EQ(format_age(now - 25 * 60.0, now), "25 min ago");
    EXPECT_EQ(format_age(now - 3 * 3600.0, now), "3 h ago");
    EXPECT_EQ(format_age(now - 2 * 86400.0, now), "2 days ago");
    EXPECT_TRUE(check_fresh(now - 900.0, now));
    EXPECT_FALSE(check_fresh(now - 901.0, now));
    EXPECT_FALSE(check_fresh(0.0, now));
}

TEST(Model, ParsesSettingsAndUpdateStatuses)
{
    json::Value value;
    ASSERT_TRUE(
        json::parse(R"({"token":"t","pairCode":"482915","address":"192.168.1.20"})", &value));
    const Session session = parse_session(value);
    EXPECT_EQ(session.pair_code, "482915");
    EXPECT_EQ(session.address, "192.168.1.20");
    ASSERT_TRUE(json::parse(R"({"available":true,"busy":false,"revision":10,"gameCount":581,
                                "checkedAt":1800000000,"checkAfter":1800000060,"error":""})",
                            &value));
    const CatalogueStatus catalogue = parse_catalogue_status(value);
    EXPECT_EQ(catalogue.revision, 10);
    EXPECT_EQ(catalogue.game_count, 581);
    EXPECT_EQ(catalogue.check_after, 1800000060.0);
    ASSERT_TRUE(json::parse(R"({"available":true,"updateAvailable":true,"runningVersion":"0.6.0",
                                "savedVersion":"0.6.1","latestVersion":"0.6.1","checksum":"c",
                                "phase":"saved","restartRequired":true,"busy":false})",
                            &value));
    const ServiceUpdate service = parse_service_update(value);
    EXPECT_TRUE(service.update_available);
    EXPECT_TRUE(service.restart_required);
    EXPECT_EQ(service.saved_version, "0.6.1");
    EXPECT_EQ(service.phase, "saved");
    ASSERT_TRUE(json::parse(R"({"available":true,"installed":"manual","installedVersion":"",
                                "latestVersion":"1.1.0","size":71303168,"phase":"checked"})",
                            &value));
    const TvAppStatus tv = parse_tv_app(value);
    EXPECT_EQ(tv.installed, "manual");
    EXPECT_EQ(tv.latest_version, "1.1.0");
    EXPECT_EQ(tv.size, 71303168.0);
    // Older Orbits without these endpoints read as empty.
    ASSERT_TRUE(json::parse(R"({"error":"Unknown endpoint."})", &value));
    EXPECT_FALSE(parse_tv_app(value).available);
    EXPECT_TRUE(parse_session(value).address.empty());
}

TEST(Model, ParsesTheArchiveOrgSignIn)
{
    json::Value value;
    ASSERT_TRUE(json::parse(R"({"signedIn":true,"name":"Player One","busy":false})", &value));
    ArchiveAccount account = parse_archive_account(value);
    EXPECT_TRUE(account.known);
    EXPECT_TRUE(account.signed_in);
    EXPECT_EQ(account.name, "Player One");
    ASSERT_TRUE(json::parse(R"({"signedIn":false,"name":"","busy":false})", &value));
    account = parse_archive_account(value);
    EXPECT_TRUE(account.known);
    EXPECT_FALSE(account.signed_in);
    // A download service without the sign-in hides it.
    ASSERT_TRUE(json::parse(R"({"error":"Unknown endpoint."})", &value));
    EXPECT_FALSE(parse_archive_account(value).known);
}

} // namespace
} // namespace orbit

TEST(Model, DownloadRecoverySettingsKeepLegacyDefaultsAndRejectInvalidSavedValues)
{
    orbit::json::Value value;
    ASSERT_TRUE(orbit::json::parse("{}", &value));
    auto system = orbit::parse_system(value);
    EXPECT_EQ(system.download_recovery, (std::array<int, 4>{3, 10, 60, 1}));
    ASSERT_TRUE(orbit::json::parse(R"({"downloadRetries":0,"downloadRetryDelay":31,"downloadStallTimeout":120,"downloadMinSpeed":512})", &value));
    system = orbit::parse_system(value);
    EXPECT_EQ(system.download_recovery, (std::array<int, 4>{0, 31, 120, 512}));
    ASSERT_TRUE(orbit::json::parse(R"({"downloadRetries":11,"downloadRetryDelay":0,"downloadStallTimeout":1.5,"downloadMinSpeed":null})", &value));
    EXPECT_EQ(orbit::parse_system(value).download_recovery, (std::array<int, 4>{3, 10, 60, 1}));
}

namespace orbit {
TEST(Model, DownloadInstallationProgressIsIndependentOfCompletion)
{
    json::Value value;
    ASSERT_TRUE(json::parse(R"([{"id":"package-job","status":"complete","installation":{"available":true,"state":"ready","shadowMount":"requested","shadowMountMessage":"Waiting for registration","packages":[{"id":"pkg-id","filename":"folder/game.pkg","platform":"ps4","state":"installing","received":1024,"total":4096,"message":"Installing"}]}}])", &value));
    const auto jobs = parse_jobs(value);
    ASSERT_EQ(jobs.size(), 1u);
    EXPECT_EQ(jobs[0].status, "complete");
    EXPECT_TRUE(jobs[0].installation_available);
    EXPECT_EQ(jobs[0].shadowmount_state, "requested");
    ASSERT_EQ(jobs[0].packages.size(), 1u);
    EXPECT_EQ(jobs[0].packages[0].filename, "folder/game.pkg");
    EXPECT_EQ(jobs[0].packages[0].state, "installing");
    EXPECT_EQ(jobs[0].packages[0].received, 1024);
}

}

namespace orbit
{
TEST(Model, BrowseMatchesAnyTickedSourceOrFormatOnTheSameOption)
{
    std::vector<Game> games(3);
    games[0].id = "a";
    games[0].title = "A";
    games[0].releases.resize(1);
    games[0].releases[0].source_id = "archive";
    games[0].releases[0].format = "exFAT";
    games[0].releases[0].size_bytes = 5e9;
    games[1].id = "b";
    games[1].title = "B";
    games[1].releases.resize(1);
    games[1].releases[0].source_id = "vikingfile";
    games[1].releases[0].format = "FFPFSC";
    games[1].releases[0].size_bytes = 5e9;
    games[2].id = "c";
    games[2].title = "C";
    games[2].releases.resize(1);
    games[2].releases[0].source_id = "akirabox";
    games[2].releases[0].format = "PKG";
    games[2].releases[0].size_bytes = 5e9;
    BrowseFilters filters;
    filters.sort = Sort::title;
    filters.formats = {"exFAT", "FFPFSC"};
    EXPECT_EQ(browse(games, filters, {}), (std::vector<int>{0, 1}));
    filters.sources = {"vikingfile", "akirabox"};
    EXPECT_EQ(browse(games, filters, {}), (std::vector<int>{1}));
    filters.formats.clear();
    EXPECT_EQ(browse(games, filters, {}), (std::vector<int>{1, 2}));
    EXPECT_TRUE(filters.narrowed());
}

TEST(Model, TagsMatchAnyTickedTagAndOnlyLabelledOnesAreOffered)
{
    std::vector<Game> games(3);
    for (int i = 0; i < 3; ++i)
    {
        games[static_cast<std::size_t>(i)].id = std::string(1, static_cast<char>('a' + i));
        games[static_cast<std::size_t>(i)].title = games[static_cast<std::size_t>(i)].id;
        games[static_cast<std::size_t>(i)].releases.resize(1);
        games[static_cast<std::size_t>(i)].releases[0].size_bytes = 1e9;
    }
    games[0].releases[0].tags = {"exclusive"};
    games[1].releases[0].tags = {"future-tag", "local-coop", "split-screen"};
    games[2].releases[0].tags = {"open-world", "psvr2-supported"};
    BrowseFilters filters;
    filters.sort = Sort::title;
    EXPECT_EQ(browse(games, filters, {}), (std::vector<int>{0, 1, 2}));
    filters.tags = {"exclusive"};
    EXPECT_TRUE(filters.narrowed());
    EXPECT_EQ(browse(games, filters, {}), (std::vector<int>{0}));
    EXPECT_STREQ(tag_label("exclusive"), "PlayStation exclusive");
    EXPECT_EQ(tag_label("future-tag"), nullptr);
    EXPECT_EQ(catalogue_tags(games), (std::vector<std::string>{"exclusive", "local-coop", "split-screen", "open-world", "psvr2-supported"}));
    filters.tags = {"local-coop", "psvr2-supported"};
    EXPECT_EQ(browse(games, filters, {}), (std::vector<int>{1, 2}));
    filters.tags = {"psvr2-supported"};
    EXPECT_EQ(browse(games, filters, {}), (std::vector<int>{2}));
    EXPECT_STREQ(tag_label("psvr-supported"), "PSVR supported");
    EXPECT_STREQ(tag_label("psvr2-supported"), "PSVR2 supported");
    json::Value value;
    ASSERT_TRUE(json::parse(R"([{"id":"r","gameId":"g","title":"G","titleId":"PPSA00001","tags":["exclusive","x"]}])", &value));
    EXPECT_EQ(parse_catalog(value).front().tags, (std::vector<std::string>{"exclusive", "x"}));
}

TEST(Model, HideInstalledLeavesOutGamesInLibrary)
{
    std::vector<Game> games(2);
    games[0].id = "installed";
    games[0].title = "Installed";
    games[0].releases.resize(1);
    games[0].releases[0].title_id = "PPSA00001";
    games[0].releases[0].platform = "ps5";
    games[0].releases[0].size_bytes = 1e9;
    games[1].id = "other";
    games[1].title = "Other";
    games[1].releases.resize(1);
    games[1].releases[0].title_id = "PPSA00002";
    games[1].releases[0].platform = "ps5";
    games[1].releases[0].size_bytes = 1e9;
    LibraryGame copy;
    copy.title_id = "PPSA00001";
    copy.platform = "ps5";
    copy.on_drive = true;
    BrowseFilters filters;
    filters.sort = Sort::title;
    EXPECT_EQ(browse(games, filters, {}, {copy}), (std::vector<int>{0, 1}));
    filters.hide_installed = true;
    EXPECT_TRUE(filters.narrowed());
    EXPECT_EQ(browse(games, filters, {}, {copy}), (std::vector<int>{1}));
    copy.on_drive = false;
    EXPECT_EQ(browse(games, filters, {}, {copy}), (std::vector<int>{0, 1})); // neither installed nor on a drive
    copy.installed = true;
    copy.platform = "ps4";
    EXPECT_FALSE(in_library(games[0], {copy}));
    games[0].releases[0].addon_id = "dlc";
    copy.platform = "ps5";
    EXPECT_FALSE(in_library(games[0], {copy})); // add-ons never count
}
TEST(Model, SavedFiltersReadAndSaveEveryFilterAndTheSearch)
{
    json::Value value;
    ASSERT_TRUE(json::parse(R"([
        {"id":"f0000000000000001","name":"Small PS5","page":"games","filters":{"query":"rally","source":["archive","archive"],
         "format":["exFAT"],"tags":["open-world"],"region":"EUR","platform":"ps5","size":"small","collection":"favorites",
         "hideInstalled":true,"sort":"title-desc","addonType":""}},
        {"id":"f0000000000000002","name":"New add-ons","page":"addons","filters":{"sort":"added","addonType":"expansion"}},
        {"id":"","name":"No ID","page":"games","filters":{}},
        {"id":"f0000000000000003","name":"No filters","page":"games"}])",
                            &value));
    const std::vector<SavedFilter> saved = parse_saved_filters(value);
    ASSERT_EQ(saved.size(), 2u);
    const BrowseFilters &f = saved[0].filters;
    EXPECT_EQ(saved[0].name, "Small PS5");
    EXPECT_FALSE(saved[0].addons);
    EXPECT_EQ(f.query, "rally");
    EXPECT_EQ(f.sources, (std::vector<std::string>{"archive"}));
    EXPECT_EQ(f.formats, (std::vector<std::string>{"exFAT"}));
    EXPECT_EQ(f.tags, (std::vector<std::string>{"open-world"}));
    EXPECT_EQ(f.region, "EUR");
    EXPECT_EQ(f.platform, "ps5");
    EXPECT_EQ(f.size, SizeFilter::small);
    EXPECT_TRUE(f.favourites);
    EXPECT_TRUE(f.hide_installed);
    EXPECT_EQ(f.sort, Sort::title_desc);
    // The Add-ons page's newest first is "added"; it is Sort::release here and saves back as "added".
    EXPECT_TRUE(saved[1].addons);
    EXPECT_EQ(saved[1].filters.sort, Sort::release);
    EXPECT_EQ(saved[1].filters.addon_type, "expansion");
    ASSERT_TRUE(json::parse(saved_filter_body("New \"add-ons\"", true, saved[1].filters), &value));
    EXPECT_EQ(value.text("action"), "save");
    EXPECT_EQ(value.text("name"), "New \"add-ons\"");
    EXPECT_EQ(value.text("page"), "addons");
    EXPECT_EQ(value["filters"].text("sort"), "added");
    EXPECT_EQ(value["filters"].text("addonType"), "expansion");
    // A Browse filter saves every value as it was read.
    ASSERT_TRUE(json::parse(saved_filter_body("Small PS5", false, f), &value));
    EXPECT_EQ(value.text("page"), "games");
    EXPECT_EQ(value["filters"].text("query"), "rally");
    EXPECT_EQ(value["filters"]["source"].items().size(), 1u);
    EXPECT_EQ(value["filters"].text("collection"), "favorites");
    EXPECT_TRUE(value["filters"].flag("hideInstalled"));
    EXPECT_EQ(value["filters"].text("size"), "small");
    EXPECT_EQ(value["filters"].text("sort"), "title-desc");
}

} // namespace orbit
