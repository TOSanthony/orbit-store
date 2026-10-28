// Orbit Store TV app - Panorama: one long page of live tiles.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Each page is a huge lowercase title over groups of square tiles laid out
// left to right (docs: the Panorama interface design). A screen describes its
// groups; the View lays them out, moves the focus between tiles and slides
// the page. Tiles are filled only when they are on screen, so a group can
// hold the whole catalogue.

#pragma once

#include "core/input.hpp"
#include "orbit/icons.hpp"
#include "orbit/ui.hpp"
#include "ui/components/keyboard.hpp"
#include "ui/feedback.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace orbit
{
struct Context;
struct Frame;
struct Game;
} // namespace orbit

namespace orbit::pano
{

// The design's 1920 x 1080 geometry.
constexpr float kLeft = 100.0f;      // page margin
constexpr float kTilesTop = 386.0f;  // first row of tiles
constexpr float kUnit = 196.0f;      // a 1 x 1 tile
constexpr float kGap = 12.0f;        // between tiles
constexpr float kStep = kUnit + kGap;
constexpr float kGroupGap = 52.0f;   // between groups
constexpr float kGroupTitle = 318.0f; // top of a group's title
constexpr float kGroupTitleSize = 38.0f;
constexpr float kFont = 21.0f;       // a tile's label
constexpr float kHeaderSize = 236.0f;
constexpr float kHeaderTop = 18.0f;
constexpr int kRows = 3;

inline const hui::gfx::Color kTile = hui::gfx::Color::rgb(0x25272c);
inline const hui::gfx::Color kBand = hui::gfx::Color::rgb(0x1d1e22);
inline const hui::gfx::Color kPage = hui::gfx::Color::rgb(0x0c0d10);
inline const hui::gfx::Color kMagenta = hui::gfx::Color::rgb(0xd80073);
inline const hui::gfx::Color kRed = hui::gfx::Color::rgb(0xe51400);
inline const hui::gfx::Color kAmber = hui::gfx::Color::rgb(0xf0a30a);
inline const hui::gfx::Color kGreen = hui::gfx::Color::rgb(0x339933);

// Where a tile sits in its group, in tile units.
struct Place
{
    int col = 0;
    int row = 0;
    int w = 1;
    int h = 1;
};

// The designs' arrangements, repeated as a group grows.
enum class Pattern : std::uint8_t
{
    lead,     // a 2 x 2 tile, two small ones under it, then 1 x 1 tiles three high (as the browser's grid)
    wide,     // 2 x 1 tiles stacked three high (3 per 2 columns)
    small,    // 1 x 1 tiles three high
    custom,   // Group::places
    pixels,   // Group::rects: placed by pixel inside the group (a button under text)
};

struct Tile
{
    std::string key; // what the focus remembers across rebuilds
    const Game *game = nullptr; // its art fills the tile
    float dim = 0.0f;           // darkens the art
    bool has_bg = false;
    hui::gfx::Color bg = kTile;
    bool has_icon = false;
    icons::Icon icon = icons::Icon::info;
    std::string kicker; // small capitals, top left
    std::string number; // large and light, top right
    float number_size = 0.0f;
    std::string label;
    std::string sub;
    float label_size = 0.0f; // 0: the group's size for this tile
    float bar = -1.0f;       // progress along the bottom edge
    bool bar_warn = false;   // amber (a drive that's short, an archive that needs a password)
    bool bar_bad = false;    // red (a failed download)
    bool check = false;   // the chosen one: a corner check
    bool selected = false; // an inset outline
    bool off = false;      // unavailable: faded
    bool button = false; // drawn as a flat button (its icon and label) rather than a tile
    bool name_on_focus = false; // a game's art tile: its label and line only while it has the focus
    bool ps4 = false;  // the PS4 badge, top left (as the browser version and Zero mark tiles)
    bool star = false; // a favourite's star, top right
};

struct Group
{
    std::string title;
    std::string count; // beside the title, faint
    int size = 0;
    Pattern pattern = Pattern::small;
    std::vector<Place> places; // Pattern::custom
    std::vector<ui::Rect> rects; // Pattern::pixels, from the group's top-left corner
    int cols = 0;                // at least this many columns wide (free content)
    // Fills tile i (only for tiles on screen, and the focused one).
    std::function<void(int, Tile &)> fill;
    // Tiles that can't take the focus (a figure), when set.
    std::function<bool(int)> focusable;
    // Free content under the tiles (a game's description): rows of tiles it
    // takes, and how to draw it at its top-left corner.
    int below_rows = 0;
    std::function<void(hui::gfx::DrawList &, float, float, float)> below;
};

struct Page
{
    std::vector<Group> groups;
};

Place place(const Group &group, int index);
int columns(const Group &group);

struct Focus
{
    int group = 0;
    int index = 0;
};

class View
{
  public:
    // Arrows move between tiles, across groups too; returns true if it moved.
    bool move(const Page &page, const hui::InputFrame &input, hui::ui::Feedback &feedback);
    // L1/R1 on pages with groups to jump between (a game, Downloads).
    bool jump(const Page &page, int delta, hui::ui::Feedback &feedback);
    void clamp(const Page &page);
    void animate(const Page &page, float dt);
    // Focus a tile by its key; returns false if no tile has it.
    bool focus_key(const Page &page, const std::string &key);
    std::string focused_key(const Page &page) const;
    // The page's left edge in screen pixels, after sliding.
    float scroll() const
    {
        return scroll_.value;
    }
    void snap()
    {
        scroll_.snap(scroll_.target);
    }
    // Slides the page to show a group whole (a game's About), whatever has
    // the focus; -1 follows the focus again.
    void show_group(int group)
    {
        shown_ = group;
    }
    void draw(Context &ctx, hui::gfx::DrawList &list, const Page &page, bool focused) const;
    Focus focus;

  private:
    ui::Rect rect(const Page &page, int group, int index) const; // page coordinates
    float group_x(const Page &page, int group) const;
    ui::Ease scroll_;
    ui::Ease pop_; // the focused tile grows in
    Focus last_;
    int shown_ = -1;
};

// One tile at r (screen coordinates); focus 0..1 rings and grows it.
void draw_tile(Context &ctx, hui::gfx::DrawList &list, const Tile &tile, const ui::Rect &r,
               float focus);
// The page's title, huge and light, sliding slower than the tiles.
void draw_header(Context &ctx, hui::gfx::DrawList &list, std::string_view word, float shift,
                 float size = kHeaderSize, float top = kHeaderTop);
// Downloads at a glance, top right: count and speed.
void draw_tray(Context &ctx, hui::gfx::DrawList &list, bool update_dot = false);
// The art behind a page: the game's cover, very faint (draw a veil over it after).
void draw_backdrop(Context &ctx, hui::gfx::DrawList &list, const Game *game, float opacity);
// ---------------------------------------------------------------- controls (pano_ui.cpp)

inline const hui::gfx::Color kInk = hui::gfx::Color::rgb(0xffffff);
inline const hui::gfx::Color kDim = hui::gfx::Color::rgb(0xffffff, 0.66f);
inline const hui::gfx::Color kFaint = hui::gfx::Color::rgb(0xffffff, 0.44f);
inline const hui::gfx::Color kKey = hui::gfx::Color::rgb(0x34363c);

// The design's faces: Inter for body text and capitals, Inter Tight for
// everything set large (light lists, extra-light titles, medium labels).
enum class Face : std::uint8_t
{
    body,     // Inter 400
    semibold, // Inter 600
    light,    // Inter Tight 300
    xlight,   // Inter Tight 200
    medium,   // Inter Tight 500
};
const hui::ui::FontRef &font(Context &ctx, Face face, std::string_view text);
// A line's baseline in a line box from top (Inter's metrics).
float baseline(float top, float size, float line = 1.6f);
// Wrapped text from its top; returns its height. list may be null to measure.
float draw_text(Context &ctx, hui::gfx::DrawList *list, float x, float top, float width, std::string_view text, float size,
                hui::gfx::Color ink, Face face = Face::body, float line = 1.6f, int max_lines = 8);
// Capitals spaced .14em (the design's .caps); returns the line's height.
float draw_caps(Context &ctx, hui::gfx::DrawList *list, float x, float top, std::string_view text, float size = 16.0f,
                hui::gfx::Color ink = hui::gfx::Color::rgb(0xffffff, 0.7f));
float caps_width(Context &ctx, std::string_view text, float size);
// Capitals wrapped to width, at most `most` lines (the last shortened).
std::vector<std::string> wrap_caps(Context &ctx, std::string_view text, float size, float width, int most);

// A flat button: outlined, the primary in the accent, the focused one white.
struct Button
{
    std::string label; // empty: a square icon button
    icons::Icon icon = icons::Icon::info;
    bool has_icon = false;
    bool primary = false;
    bool focus = false;
    bool enabled = true;
    bool small = false;
};
Button button(std::string label, bool focus = false);
Button button(std::string label, icons::Icon icon, bool focus = false);
float button_width(Context &ctx, const Button &b);
float button_height(const Button &b);
void draw_button(Context &ctx, hui::gfx::DrawList &list, float x, float y, const Button &b);
// A row of buttons from x, 14 px apart; returns its width.
float draw_buttons(Context &ctx, hui::gfx::DrawList &list, float x, float y, const std::vector<Button> &buttons);

// A switch with On or Off beside it (and its width), and a check box.
float toggle_width(Context &ctx, bool on);
void draw_toggle(Context &ctx, hui::gfx::DrawList &list, float right, float cy, bool on);
void draw_box(hui::gfx::DrawList &list, float x, float cy, bool on);

// A Metro list row: a big light name, its value under it in the accent, a
// line of text, and a switch, box or button on the right.
struct Item
{
    icons::Icon icon = icons::Icon::info;
    bool has_icon = false;
    std::string title;
    std::string value; // in the accent (amber when warn)
    std::string sub;
    std::string button;
    int toggle = -1;
    int check = -1;
    bool dot = false;    // an accent dot after the value (an update)
    bool warn = false;   // the title, icon and value in amber
    bool off = false;    // faded
    bool chosen = false; // a picker's current value: the accent and a check
    bool danger = false; // an app bar action that removes something
    float bar = -1.0f;   // a picker's drive: its bar under the line
    bool bar_warn = false;
    float size = 0.0f;   // the title's size; 0: the list's
    int tag = 0;         // the caller's: what the item does
    bool has_swatch = false; // a picker's colour: a square before the name
    hui::gfx::Color swatch{};
};
// Returns the bottom; list may be null to measure. focus -1 for none.
float draw_list(Context &ctx, hui::gfx::DrawList *list, float x, float y, float width, const std::vector<Item> &items, int focus,
                float size = 32.0f);
// Where row i of a list starts, relative to its top.
float list_offset(Context &ctx, float width, const std::vector<Item> &items, int index, float size = 32.0f);

// Lowercase pivots across, the current one bright; shift slides them left.
void draw_pivots(Context &ctx, hui::gfx::DrawList &list, float x, float y, const std::vector<std::string> &items, int current,
                 float size = 44.0f, float shift = 0.0f);

// A full-screen list of choices: its heading, a line under it, the items
// and a note. Narrow: a list on the left with the screen showing beside it.
struct Picker
{
    std::string head;
    std::string sub;
    std::vector<Item> items;
    int focus = -1;
    std::string note;
    float size = 50.0f;
    bool field = false; // a text field under the heading (Library's search)
    std::string field_value;
    std::string field_placeholder;
    bool field_focus = false;
    std::string note_above; // a caps label between the field and the items
    bool narrow = false;
};
void draw_picker(Context &ctx, hui::gfx::DrawList &list, const Picker &picker, float in);

// Options: the app bar rises from the bottom with lowercase actions and,
// under them, round shortcuts. focus counts the actions, then the shortcuts.
struct AppBar
{
    std::string head;
    std::vector<Item> items; // title, sub, off, danger
    struct Shortcut
    {
        icons::Icon icon = icons::Icon::info;
        std::string label;
        int item = 0; // the action it runs
    };
    std::vector<Shortcut> shortcuts;
    int focus = 0;
    int count() const
    {
        return static_cast<int>(items.size() + shortcuts.size());
    }
    // The action the focus runs.
    int action() const
    {
        if (focus < static_cast<int>(items.size()))
            return focus;
        return shortcuts[static_cast<std::size_t>(focus) - items.size()].item;
    }
};
// Up and down through the actions, left and right along the shortcuts.
// Returns the chosen action once Cross picks one, -2 when Circle closes it, else -1.
int update_appbar(AppBar &bar, const hui::InputFrame &input, hui::ui::Feedback &feedback);
void draw_appbar(Context &ctx, hui::gfx::DrawList &list, const AppBar &bar, float in);

// A notice: a strip across the top in a solid colour; returns its height.
float draw_notice(Context &ctx, hui::gfx::DrawList &list, float y, icons::Icon icon, hui::gfx::Color tone, std::string_view text,
                  std::string_view sub = {});
// Five dots in the accent, the light running along them; returns the width.
float draw_dots(Context &ctx, hui::gfx::DrawList &list, float x, float cy, float size,
                hui::gfx::Color tone = hui::gfx::Color{0, 0, 0, 0});

// A message band across the dimmed screen: kicker, title, body, an error,
// check options, every button (Controls), tiles to choose from, then
// buttons. focus counts the options, then the tiles, then the buttons.
struct Band
{
    std::string kicker;
    std::string title;
    std::string body;
    std::string error;
    std::vector<Item> options; // title, check, off
    std::vector<hui::ui::Hint> controls; // every button, in two columns (Controls)
    std::string tiles_label;
    std::vector<Tile> tiles;
    std::vector<Button> buttons;
    int focus = -1;
    bool busy = false;
};
void draw_band(Context &ctx, hui::gfx::DrawList &list, const Band &band, float in);

// The opening screens: the wordmark in light type on black, running dots
// while it works, the title, the body, small print and buttons.
void draw_splash(Context &ctx, hui::gfx::DrawList &list, std::string_view title, std::string_view body, std::string_view small,
                 const std::vector<Button> &buttons, bool busy);
// A page with nothing to show: a kicker, a huge light line, a paragraph
// and buttons, at the left under the title.
void draw_empty(Context &ctx, hui::gfx::DrawList &list, std::string_view kicker, std::string_view title, std::string_view body,
                const std::vector<Button> &buttons, bool busy = false);

// Key and value pairs in columns from top; returns the height.
float draw_pairs(Context &ctx, hui::gfx::DrawList *list, float x, float top, float width,
                 const std::vector<std::pair<std::string, std::string>> &pairs, int columns = 3);
// Numbered steps (a leading "1. " in a step's text is dropped); returns the height.
float draw_steps(Context &ctx, hui::gfx::DrawList *list, float x, float top, float width, const std::vector<std::string> &steps);

// Panorama's keys: flat, the focused one in the accent, on the band at the
// bottom; and the keyboard view: the label, the text as a huge title with
// an accent caret.
void keyboard_look(hui::ui::KeyboardStyle &style);
inline ui::Rect keyboard_keys()
{
    return {483.0f, 716.0f, 954.0f, 334.0f};
}
void draw_keyboard(Context &ctx, orbit::Frame &frame, const hui::ui::Keyboard &keys, std::string_view label, std::string_view value,
                   std::string_view placeholder, bool secret);
// Where Panorama's hints sit: bottom right, 19 px, 30 px glyphs.
ui::HintStyle hint_style(float bottom = 30.0f);

// The lowercase text of a label: ASCII, accented Latin, Greek and Cyrillic.
std::string lower(std::string_view text);

} // namespace orbit::pano
