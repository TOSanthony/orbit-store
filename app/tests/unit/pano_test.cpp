// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/i18n.hpp"
#include "orbit/pano.hpp"
#include "orbit/screens.hpp"
#include <gtest/gtest.h>

namespace orbit
{
namespace
{

TEST(Pano, LowerCoversTheTranslatedScripts)
{
    // Panorama sets its titles in lowercase in every language Orbit ships.
    EXPECT_EQ(pano::lower("Downloads"), "downloads");
    EXPECT_EQ(pano::lower("\xC3\x9C" "ber"), "\xC3\xBC" "ber");                         // Über
    EXPECT_EQ(pano::lower("\xD0\x97\xD0\xB0\xD0\xB3\xD1\x80\xD1\x83\xD0\xB7\xD0\xBA\xD0\xB8"), // Загрузки
              "\xD0\xB7\xD0\xB0\xD0\xB3\xD1\x80\xD1\x83\xD0\xB7\xD0\xBA\xD0\xB8");
    EXPECT_EQ(pano::lower("\xC5\x81\xC3\xB3\xC5\xBA"), "\xC5\x82\xC3\xB3\xC5\xBA");          // Łóź
    EXPECT_EQ(pano::lower("\xC5\xBB" "ycie"), "\xC5\xBC" "ycie");                           // Życie
    EXPECT_EQ(pano::lower("\xC4\xB0ndir"), "indir");                                       // İndir
    EXPECT_EQ(pano::lower("12 \xE2\x80\x9CNe\xE2\x80\x9D"), "12 \xE2\x80\x9Cne\xE2\x80\x9D"); // three-byte quotes kept
}

TEST(Pano, WholePercentRoundsDown)
{
    i18n::load({}, "en");
    EXPECT_EQ(i18n::whole_percent(38.2), "38%");
    EXPECT_EQ(i18n::whole_percent(99.9), "99%");
    EXPECT_EQ(i18n::whole_percent(-3.0), "0%");
}

TEST(Pano, LeadPatternIsTheBrowsersGrid)
{
    // One large tile, two small ones under it, then small ones three high:
    // nine tiles fill four columns, as on the browser version and Zero.
    pano::Group g;
    g.pattern = pano::Pattern::lead;
    g.size = 9;
    const pano::Place big = pano::place(g, 0), under = pano::place(g, 1), beside = pano::place(g, 2),
                      next = pano::place(g, 3), last = pano::place(g, 8);
    EXPECT_EQ(big.w, 2);
    EXPECT_EQ(big.h, 2);
    EXPECT_EQ(under.col, 0);
    EXPECT_EQ(under.row, 2);
    EXPECT_EQ(beside.col, 1);
    EXPECT_EQ(beside.row, 2);
    EXPECT_EQ(next.col, 2);
    EXPECT_EQ(next.row, 0);
    EXPECT_EQ(last.col, 3);
    EXPECT_EQ(last.row, 2);
    EXPECT_EQ(pano::columns(g), 4);
    g.size = 10;
    EXPECT_EQ(pano::columns(g), 5);
    g.size = 2;
    EXPECT_EQ(pano::columns(g), 2);
}

TEST(Pano, TextGroupsTakeTheColumnsTheyAskFor)
{
    pano::Group text;
    text.cols = 4;
    EXPECT_EQ(pano::columns(text), 4);
    pano::Group button;
    button.pattern = pano::Pattern::pixels;
    button.size = 1;
    button.cols = 3;
    button.rects = {{0.0f, 300.0f, 200.0f, 64.0f}};
    EXPECT_EQ(pano::columns(button), 3);
}

TEST(Pano, JumpPassesOverGroupsOfText)
{
    pano::Page page;
    pano::Group tiles;
    tiles.size = 2;
    pano::Group text;
    text.cols = 2;
    page.groups = {tiles, text, tiles};
    pano::View view;
    Feedback feedback;
    EXPECT_TRUE(view.jump(page, 1, feedback));
    EXPECT_EQ(view.focus.group, 2);
    EXPECT_FALSE(view.jump(page, 1, feedback));
    EXPECT_TRUE(view.jump(page, -1, feedback));
    EXPECT_EQ(view.focus.group, 0);
}

} // namespace
} // namespace orbit
