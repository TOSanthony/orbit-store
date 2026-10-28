// Orbit Store TV app - Cinema: one game at a time, full screen.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The chapters run across the top with the downloads capsule on the right;
// a chapter's collections step with L1 and R1 under them. The focused game
// fills the screen behind a gold kicker, a bold title (Inter Tight) and a
// row of pill buttons; frames to choose from sit under or beside it.
// Details rise as a glass sheet; decisions are title cards across the
// dimmed screen. These are the parts; screens arrange them.

#pragma once

#include "orbit/icons.hpp"
#include "orbit/ui.hpp"
#include "ui/components/keyboard.hpp"

#include <string>
#include <vector>

namespace orbit
{
struct Context;
struct Frame;
struct Game;
} // namespace orbit

namespace orbit::cn
{

// The design's palette.
const ui::Color kInk = ui::Color::rgb(0xf7f3ec);
const ui::Color kGold = ui::Color::rgb(0xdcbc84);
const ui::Color kDarkGold = ui::Color::rgb(0x8a6a2e);
const ui::Color kDim = ui::Color::rgb(0xf7f3ec, 0.66f);
const ui::Color kFaint = ui::Color::rgb(0xf7f3ec, 0.45f);
const ui::Color kDark = ui::Color::rgb(0x0b0907);
const ui::Color kWarn = ui::Color::rgb(0xe8c27a);
const ui::Color kOk = ui::Color::rgb(0xa8dcc0);

// What lies over the art: the stage's two veils, a detail sheet's, a title
// card's, the keyboard's.
enum class Veil : std::uint8_t
{
    hero,
    detail,
    dim,
    dark,
};
// The game's art across the screen under a veil; no game: a warm dark glow.
void backdrop(Context &ctx, hui::gfx::DrawList &out, const Game *game, Veil veil, float opacity = 1.0f);

// The wordmark and chapters (section:: indices) at the top left; at the top
// right, the downloads capsule while anything is downloading.
void chapters(Context &ctx, hui::gfx::DrawList &out, int current, bool update_dot, bool capsule = true, bool focused = false);
// Under the chapters: the collection's name between chevrons, and dots.
void collection(Context &ctx, hui::gfx::DrawList &out, std::string_view name, int index, int count, float y = 124.0f);

// Gold capitals (a kicker or a label); returns the line's height.
float kick(Context &ctx, hui::gfx::DrawList &out, float x, float y, std::string_view text, float size = 16.0f,
           ui::Color ink = kGold);

struct Button
{
    std::string label; // empty: a round icon button
    icons::Icon icon = icons::Icon::info;
    bool has_icon = false;
    bool primary = false;
    bool focus = false;
    bool enabled = true;
    bool danger = false;
    bool small = false;
};
Button button(std::string label, icons::Icon icon, bool primary = false);
float button_width(Context &ctx, const Button &b);
void draw_button(Context &ctx, hui::gfx::DrawList &out, float x, float y, const Button &b);
// A row of buttons from x; returns its width.
float draw_buttons(Context &ctx, hui::gfx::DrawList &out, float x, float y, const std::vector<Button> &buttons,
                   bool centred = false);

// The stage: a kicker, the title, a tagline, a paragraph and buttons.
struct Hero
{
    std::string kicker;
    bool warn = false; // the kicker in amber
    std::string title;
    std::string tag;
    std::string body;
    std::vector<Button> buttons;
    float x = 100.0f;
    float y = 430.0f;
    float width = 1180.0f;
    float size = 150.0f;
};
// Returns where the buttons' row starts (for a bar or line under the text).
float draw_hero(Context &ctx, hui::gfx::DrawList &out, const Hero &hero);

// Discover's and Browse's stage over rows: the kicker, the title on one line
// (its type stepping down to half size; longer still, two lines at half size in the
// same height) and a line of tagline. No buttons: the focus is on a frame.
// Every game takes the same height, so the rows under it never move.
struct Stage
{
    std::string kicker;
    std::string title;
    std::string tag;
    float y = 224.0f; // the top: a good part of the art shows above it
    float size = 124.0f;
};
// The stage's height for a title size; draw_stage returns its bottom.
float stage_height(float size);
float draw_stage(Context &ctx, hui::gfx::DrawList &out, const Stage &stage);

// Rows under the stage, as the live Store has them: five frames across the
// gutters, a strip running on past the right edge, a grid in lines.
constexpr int kRowColumns = 5;
constexpr float kRowFrameW = 324.0f;
constexpr float kRowFrameH = 182.0f;
constexpr float kRowGap = 25.0f;     // between frames
constexpr float kRowLabel = 40.0f;   // a row's label, above its frames
constexpr float kRowLineGap = 26.0f; // between a grid's lines
constexpr float kRowSpace = 30.0f;   // under a row

// The film strip: frames of art, the focused one larger and ringed.
struct Still
{
    const Game *game = nullptr;
    icons::Icon icon = icons::Icon::box; // a frame without art: its icon and title
    std::string title;
    std::string sub;
    std::string badge;
    float progress = -1.0f;
    bool dim = false;
};
struct Strip
{
    bool focused = true;
    float x = 1090.0f;
    float y = 820.0f;
    float frame_w = 220.0f;
    float frame_h = 124.0f;
    float gap = 22.0f;
    std::string label;
    std::string right;
};
void draw_strip(Context &ctx, hui::gfx::DrawList &out, const std::vector<Still> &frames, int focused, const Strip &strip);
// A row's label (faint capitals), its top at y.
void draw_row_label(Context &ctx, hui::gfx::DrawList &out, std::string_view label, float y);
// A line of frames from x = 100, their tops at y, the focused one ringed.
void draw_row_frames(Context &ctx, hui::gfx::DrawList &out, const std::vector<Still> &frames, int focused, float y);

// The glass sheet rising from the bottom, height h; returns its top.
float draw_sheet(hui::gfx::DrawList &out, float h);
void glass(hui::gfx::DrawList &out, const ui::Rect &r, float radius);

// Choice cards in a grid: the focused one light, the chosen one outlined.
struct Card
{
    std::string kicker; // a small line above (the source)
    std::string title;
    std::string sub;
    std::string foot; // small print at the bottom
    float bar = -1.0f;
    bool chosen = false;
    bool focus = false;
    bool enabled = true;
    bool big = false;
};
// Lays out (and draws, when out isn't null) cards in columns from (x, y)
// across width; returns the bottom.
float draw_cards(Context &ctx, hui::gfx::DrawList *out, float x, float y, float width, const std::vector<Card> &cards,
                 int columns, float min_height = 0.0f);

// Settings-style rows: an icon, a title and a line, and a value, switch,
// check or small button on the right; the focused row is light.
struct Row
{
    icons::Icon icon = icons::Icon::info;
    bool has_icon = true;
    std::string title;
    std::string sub;
    std::string value;
    std::string button; // a small pill on the right
    int toggle = -1;
    int check = -1;
    bool dot = false;
    bool warn = false;
    bool off = false;
};
// Returns the bottom; out may be null to measure.
float draw_rows(Context &ctx, hui::gfx::DrawList *out, float x, float y, float width, const std::vector<Row> &rows,
                int focused);

// A title card across the dimmed screen: kicker, a gold rule, the title,
// the body, check rows and buttons, centred.
struct Option
{
    std::string label;
    bool checked = false;
    bool focus = false;
    bool enabled = true;
};
struct Title
{
    std::string kicker;
    std::string title;
    std::string body;
    std::string small;
    std::vector<Option> options;
    std::vector<hui::ui::Hint> controls; // every button, in two columns (Controls)
    std::vector<Button> buttons;
    bool spin = false;
};
void draw_title(Context &ctx, hui::gfx::DrawList &out, const Title &title, float in);

// A notice growing out of the capsule (top right); returns its height.
float draw_notice(Context &ctx, hui::gfx::DrawList &out, float top, icons::Icon icon, ui::Color tone, std::string_view text,
                  std::string_view sub = {});

// A screen of its own before Orbit answers: the wordmark (or a spinner), a
// gold rule, the title, the body, small print and buttons.
void draw_status(Context &ctx, hui::gfx::DrawList &out, std::string_view title, std::string_view body,
                 std::string_view small, const std::vector<Button> &buttons, bool spin);

// A spinner: a faint ring with a light arc going round.
void spinner(Context &ctx, hui::gfx::DrawList &out, float cx, float cy, float size, float width);

// Cinema's keys: warm translucent caps, the focused one light; and where
// they sit, in the keyboard sheet at the bottom.
void keyboard_look(hui::ui::KeyboardStyle &style);
inline ui::Rect keyboard_keys()
{
    return {495.0f, 690.0f, 930.0f, 350.0f};
}
// The keyboard view: its label, the text written large with a gold caret
// (dots when secret; the placeholder faint when empty), the keys' sheet.
void draw_keyboard(Context &ctx, orbit::Frame &frame, const hui::ui::Keyboard &keys, std::string_view label,
                   std::string_view value, std::string_view placeholder, bool secret);

// Where Cinema's hints sit (bottom right, 19 px, 30 px glyphs).
ui::HintStyle hint_style(float bottom = 34.0f);

} // namespace orbit::cn
