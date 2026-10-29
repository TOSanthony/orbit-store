// Orbit Store TV app - Crossbar: categories across, the list down.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The category bar runs left to right with the current one large in a fixed
// place; its list runs down from under it. The focused row stays where it
// is, larger and lit, and the list moves around it (the row before it fades
// above the bar). A folder or a game takes the category's place in the bar
// while it is open. On the right: the focused game's art, an info card or a
// menu of actions. Screens describe their rows; the View moves the focus and
// draws them.

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

namespace orbit::xb
{

// The design's 1920 x 1080 geometry.
constexpr float kBarY = 334.0f;   // the category icons' centre line
constexpr float kBarX = 418.0f;   // the current category's centre
constexpr float kBarStep = 196.0f;
constexpr float kRowTop = 452.0f; // the focused row's top: it never moves
constexpr float kListRight = 1040.0f;
constexpr float kPreviewX = 1230.0f;
constexpr float kPreviewW = 590.0f;

// A pill after a row's title: a platform or a state.
enum class Tag : std::uint8_t
{
    plain, // translucent ("PS4", "Retrying")
    light, // white ("Downloading")
    warn,  // amber ("Needs attention")
    route, // lilac: where it downloads ("TorBox", "Computer")
};

struct Row
{
    std::string key;            // what the focus remembers across rebuilds
    const Game *game = nullptr; // its cover is the row's picture
    bool has_icon = false;
    icons::Icon icon = icons::Icon::info;
    bool primary = false; // the main action: a white tile with a dark icon
    std::string title;
    std::string tag; // a pill after the title
    Tag tag_kind = Tag::plain;
    bool favourite = false; // a heart after the title
    std::string sub;        // the line under the title
    std::string size;       // a size after the sub line, behind the storage badge
    std::string meta;       // replaces sub while focused (where a download comes from and goes)
    std::string value;      // right-aligned at rest, under the title while focused
    int toggle = -1;        // a switch with On or Off (1 on, 0 off)
    int check = -1;         // a check box (1 ticked, 0 clear)
    std::string text;       // more, wrapped, while focused
    std::string detail;     // a line under the progress while focused (amounts, timings)
    float bar = -1.0f;      // progress under the sub line
    bool warn = false;      // the sub line in amber
    bool off = false;       // unavailable: faded
    bool folder = false;    // opens more rows
    std::string hint;       // "› Actions", "› Refresh library": what a button does, while focused
    bool mark = false;      // the tile shows the Orbit mark (an item with no art)
    bool dot = false;       // a small amber dot after the value (an update)
};

struct List
{
    int size = 0;
    std::function<void(int, Row &)> fill;
};

class View
{
  public:
    // Up and down move a row; returns true if the focus moved.
    bool move(const List &list, const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void clamp(const List &list);
    void animate(float dt);
    bool focus_key(const List &list, const std::string &key);
    std::string focused_key(const List &list) const;
    void snap();
    void draw(Context &ctx, hui::gfx::DrawList &out, const List &list) const;
    int index = 0;

  private:
    ui::Ease at_; // the focus position, eased, in rows
};

// One place in the category bar: a section, a folder or a game.
struct BarItem
{
    icons::Icon icon = icons::Icon::spark;
    const Game *game = nullptr; // a game page shows its cover
    std::string label;
    int badge = 0;    // a count in a white dot (Downloads)
    bool dot = false; // an amber dot (an update in Settings)
};
// The six sections, current one at kBarX (section:: indices).
std::vector<BarItem> sections(Context &ctx, bool update_dot);
// Draws the bar with current large and labelled. A nested bar is
// {category, folder}: the category steps left, the folder takes its place.
void draw_bar(Context &ctx, hui::gfx::DrawList &out, const std::vector<BarItem> &items, int current);
// The wordmark (top left) and the default drive's free space (top right).
void draw_header(Context &ctx, hui::gfx::DrawList &out, bool addons = false);
// The focused game's art, tagline, description and chips on the right.
void draw_preview(Context &ctx, hui::gfx::DrawList &out, const Game &game, float x = kPreviewX);
// A glass panel on the right, 630 px wide with 32 px of padding: body draws
// the content from (x, y) in width and returns where it ended. With a height
// (else it fits the content) the content is clipped and moved up by scroll.
// Returns how far the content can scroll.
float draw_panel(hui::gfx::DrawList &out, float top, float height, float scroll,
                 const std::function<float(hui::gfx::DrawList &, float, float, float)> &body);
// What panels hold; each takes its top and returns its bottom.
float draw_label(Context &ctx, hui::gfx::DrawList &out, float x, float y, std::string_view text);
// badge(i, cx, cy, size) draws fact i's badge, centred at (cx, cy); the text then starts to its right.
using FactBadge = std::function<void(hui::gfx::DrawList &, std::size_t, float, float, float)>;
float draw_facts(Context &ctx, hui::gfx::DrawList &out, float x, float y, float width,
                 const std::vector<std::pair<std::string, std::string>> &facts, int columns = 3, const FactBadge &badge = {});
// Numbered steps; a leading "1. " in a step's text becomes its number.
float draw_steps(Context &ctx, hui::gfx::DrawList &out, float x, float y, float width,
                 const std::vector<std::string> &steps);

// A menu of actions or choices beside the list (Options, or a setting's values).
struct MenuItem
{
    icons::Icon icon = icons::Icon::next;
    bool has_icon = false;
    std::string label;
    std::string value; // right-aligned
    std::string sub;   // a second, smaller line
    bool radio = false;
    bool box = false; // with radio: one of several that can be ticked, drawn as a box
    bool checked = false;
    bool enabled = true;
    bool danger = false;
};
// An action with its icon.
inline MenuItem action(icons::Icon icon, std::string label, bool enabled = true)
{
    MenuItem item;
    item.icon = icon;
    item.has_icon = true;
    item.label = std::move(label);
    item.enabled = enabled;
    return item;
}
// One of a setting's values, with a radio.
inline MenuItem choice(std::string label, bool checked, std::string sub = {}, bool enabled = true)
{
    MenuItem item;
    item.radio = true;
    item.checked = checked;
    item.label = std::move(label);
    item.sub = std::move(sub);
    item.enabled = enabled;
    return item;
}
struct Menu
{
    bool open = false;
    int active = 0;
    ui::Ease in;
    void show(int first = 0)
    {
        open = true;
        active = first;
    }
    // While open: the chosen index once Cross picks one, -2 when Circle closes it, else -1.
    int update(const hui::InputFrame &input, const std::vector<MenuItem> &items, hui::ui::Feedback &feedback);
    void animate(float dt);
};
// Under a menu's items: a note, or figures (a label and its value) over a
// bar and a note, set off by a line.
struct MenuFoot
{
    std::vector<std::pair<std::string, std::string>> figures;
    float bar = -1.0f;
    std::string note;
};
// Where a menu sits: beside the list, or (a second menu) beside another one.
struct MenuAt
{
    float x = 1110.0f;
    float y = 430.0f;
    float w = 560.0f;
    float row_height = 76.0f;
    float bottom = 1060.0f;
};
void draw_menu(Context &ctx, hui::gfx::DrawList &out, std::string_view title, const std::vector<MenuItem> &items,
               const Menu &menu, const MenuAt &at = {}, const MenuFoot &foot = {});

// A question across the dimmed screen (a confirmation): a small kicker, the
// title, the body, check rows to pick from (a destination), then buttons.
struct Band
{
    std::string kicker;
    std::string title;
    std::string body;
    std::string note;  // a smaller line under the body (where from)
    std::string error; // in amber, under the body
    std::vector<MenuItem> options; // label, checked, enabled
    std::vector<hui::ui::Hint> controls; // every button, in two columns (Controls)
    std::vector<std::string> buttons; // the first is the light one
    int focus = 0;     // an option, or a button counted after the options
    bool busy = false; // a spinner before the title
};
void draw_band(Context &ctx, hui::gfx::DrawList &out, const Band &band, float in);

// An on-screen keyboard as Crossbar draws it (translucent keys on the
// panel, no plate of their own), or as the Store does.
void keyboard_look(hui::ui::KeyboardStyle &style, bool crossbar);
// Where Crossbar's keys go: on the right, where the preview is.
inline ui::Rect keyboard_keys()
{
    return {1024.0f, 556.0f, 772.0f, 340.0f};
}
// The keyboard's panel: its label, a field with the text (dots when
// secret), then the keys.
void draw_keyboard(Context &ctx, Frame &frame, const hui::ui::Keyboard &keys, std::string_view label, std::string_view value,
                   bool secret, float in);

// A note at the top right: a coloured disc with its icon, the text and a
// smaller line under it. Returns its height, for the next one.
float draw_toast(Context &ctx, hui::gfx::DrawList &out, float top, icons::Icon icon, ui::Color tone, std::string_view text,
                 std::string_view sub = {});

// A screen of its own (opening, starting, not running): the mark or a
// spinner, a title, a line or two, small print and a button, centred.
struct Status
{
    bool spin = false;
    std::string title;
    std::string body;
    std::string small;
    std::string button; // focused: Cross presses it
};
void draw_status(Context &ctx, hui::gfx::DrawList &out, const Status &status);

// Crossbar's background: the ribbons over a gradient in the focused game's
// colours (its cover's colour wash), easing from the game before; with no
// game, the time of day's. art_wash lays the wash itself over it, lightly,
// so the game's colours show and the ribbons stay in sight.
void backdrop(Context &ctx, hui::gfx::BackdropSpec &spec, const Game *now = nullptr, const Game *before = nullptr,
              float in = 1.0f);
void art_wash(Context &ctx, hui::gfx::DrawList &out, const Game *now, const Game *before, float in);

} // namespace orbit::xb
