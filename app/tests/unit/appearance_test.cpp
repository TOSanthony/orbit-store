// Orbit Store TV app - The chosen background: presets and the saved choice.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/appearance.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdlib.h>
#include <string>

namespace orbit
{
namespace
{

class AppearanceTest : public ::testing::Test
{
protected:
    AppearanceTest()
    {
        char name[] = "/tmp/orbit-appearance-XXXXXX";
        dir_ = mkdtemp(name);
        file_ = dir_ + "/sub/appearance.cfg";
        setenv("ORBIT_APPEARANCE_FILE", file_.c_str(), 1);
    }
    ~AppearanceTest() override
    {
        unsetenv("ORBIT_APPEARANCE_FILE");
        std::filesystem::remove_all(dir_);
    }
    std::string dir_;
    std::string file_;
};

TEST_F(AppearanceTest, ThePresetsAreDistinctAndTheFirstIsBlack)
{
    ASSERT_GT(appearance::count(), 6u);
    EXPECT_STREQ(appearance::preset(0).id, "black");
    for (std::size_t i = 0; i < appearance::count(); ++i)
        for (std::size_t j = i + 1; j < appearance::count(); ++j)
            EXPECT_STRNE(appearance::preset(i).id, appearance::preset(j).id);
}

TEST_F(AppearanceTest, NothingSavedMeansBlack)
{
    EXPECT_EQ(appearance::load(), 0u);
}

TEST_F(AppearanceTest, TheChoiceSurvivesARestart)
{
    const std::size_t aurora = appearance::find("aurora");
    ASSERT_GT(aurora, 0u);
    ASSERT_TRUE(appearance::save(aurora)); // creates the folder
    EXPECT_EQ(appearance::load(), aurora);
}

TEST_F(AppearanceTest, UnknownOrDamagedFilesFallBackToBlack)
{
    std::filesystem::create_directories(dir_ + "/sub");
    std::ofstream(file_) << "no-such-background\n";
    EXPECT_EQ(appearance::load(), 0u);
    EXPECT_EQ(appearance::find(""), 0u);
    EXPECT_EQ(appearance::preset(9999).id, std::string("black"));
}

TEST_F(AppearanceTest, AnimatedPresetsCarryTheClock)
{
    const std::size_t stars = appearance::find("stars");
    EXPECT_TRUE(appearance::preset(stars).animated);
    EXPECT_FLOAT_EQ(appearance::backdrop(stars, 12.5f).time, 12.5f);
}

} // namespace
} // namespace orbit
