// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/save_file.hpp"
#include "orbit/look.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include <unistd.h>

namespace orbit
{
TEST(Appearance, ConfirmedChoiceSurvivesRelaunchInTitleWritableStorage)
{
    const std::filesystem::path console_path(look::kPrefsPath);
    ASSERT_EQ(console_path.parent_path(), "/download0/orbit-store");
    char temporary[] = "/tmp/orbit-title-appearance-XXXXXX";
    ASSERT_NE(mkdtemp(temporary), nullptr);
    const std::filesystem::path root(temporary);
    const auto mount = root / "download0";
    std::filesystem::create_directory(mount);
    const auto preferences = root / console_path.relative_path();
    for (const auto interface : {look::Interface::crossbar, look::Interface::cinema, look::Interface::panorama})
    {
        look::load(preferences.string());
        look::choose(interface, 0.0f);
        look::keep();
        look::load(preferences.string());
        EXPECT_EQ(look::current(), interface);
        EXPECT_FALSE(look::pending());
    }
    EXPECT_FALSE(std::filesystem::exists(root / "data"));
    look::load("");
    std::filesystem::remove_all(root);
}

TEST(Appearance, ClosingDuringPreviewKeepsTheLastConfirmedChoice)
{
    char temporary[] = "/tmp/orbit-preview-appearance-XXXXXX";
    ASSERT_NE(mkdtemp(temporary), nullptr);
    const std::filesystem::path root(temporary);
    const auto preferences = root / "appearance.json";
    look::load(preferences.string());
    look::choose(look::Interface::crossbar, 0.0f);
    look::keep();
    look::choose(look::Interface::cinema, 1.0f);
    look::load(preferences.string());
    EXPECT_EQ(look::current(), look::Interface::crossbar);
    // Saving another preference during the preview must not confirm it.
    look::choose(look::Interface::panorama, 2.0f);
    auto prefs = look::prefs();
    prefs.reduced_motion = true;
    look::set(prefs);
    look::load(preferences.string());
    EXPECT_EQ(look::current(), look::Interface::crossbar);
    EXPECT_TRUE(look::prefs().reduced_motion);
    look::load("");
    std::filesystem::remove_all(root);
}

TEST(Appearance, SavingPreferencesPreservesTheInstalledPackageRecord)
{
    char temporary[] = "/tmp/orbit-appearance-XXXXXX";
    ASSERT_NE(mkdtemp(temporary), nullptr);
    const std::filesystem::path directory(temporary);
    const auto record = directory / "tv-app.json";
    const std::string original = "{\"version\":\"1.2.0\",\"size\":1234,\"sha256\":\"installed\"}";
    ASSERT_TRUE(hui::save::write_atomic(record.string(), original).empty());
    const auto preferences = directory / std::filesystem::path(look::kPrefsPath).filename();
    look::load(preferences.string());
    auto prefs = look::prefs();
    prefs.notice_seen = true;
    prefs.reduced_motion = true;
    look::set(prefs);
    look::choose(look::Interface::cinema, 10.0f);
    look::keep();
    look::load(preferences.string());
    EXPECT_EQ(look::current(), look::Interface::cinema);
    EXPECT_TRUE(look::prefs().reduced_motion);
    std::string actual;
    ASSERT_TRUE(hui::save::read_file(record.string(), &actual, 4096));
    EXPECT_EQ(actual, original);
    look::load("");
    std::filesystem::remove_all(directory);
}

TEST(Appearance, TheChosenLanguageIsKeptWithTheOtherPreferences)
{
    char temporary[] = "/tmp/orbit-language-XXXXXX";
    ASSERT_NE(mkdtemp(temporary), nullptr);
    const std::filesystem::path directory(temporary);
    const auto preferences = directory / "appearance.json";
    look::load(preferences.string());
    EXPECT_EQ(look::prefs().language, ""); // follows the console
    auto prefs = look::prefs();
    prefs.language = "pt-BR";
    look::set(prefs);
    look::load(preferences.string());
    EXPECT_EQ(look::prefs().language, "pt-BR");
    // A code this build doesn't ship follows the console again.
    ASSERT_TRUE(hui::save::write_atomic(preferences.string(), "{\"language\":\"ja\"}").empty());
    look::load(preferences.string());
    EXPECT_EQ(look::prefs().language, "");
    look::load("");
    std::filesystem::remove_all(directory);
}

TEST(Appearance, AnUnconfirmedSwitchRevertsAfterTheDeadline)
{
    look::load("");
    look::choose(look::Interface::crossbar, 10.0f);
    look::tick(24.0f);
    EXPECT_TRUE(look::pending());
    EXPECT_EQ(look::current(), look::Interface::crossbar);
    look::tick(25.0f);
    EXPECT_FALSE(look::pending());
    EXPECT_EQ(look::current(), look::Interface::store);
    look::load("");
}
} // namespace orbit
