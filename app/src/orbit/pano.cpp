// Orbit Store TV app - Panorama: one long page of live tiles.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/pano.hpp"

#include "orbit/screens.hpp"

#include "orbit/i18n.hpp"
#include "orbit/look.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace orbit::pano
{

Place place(const Group &group, int index)
{
    switch (group.pattern)
    {
    case Pattern::wide:
        return {2 * (index / kRows), index % kRows, 2, 1};
    case Pattern::lead:
        if (index < 3)
            return index == 0 ? Place{0, 0, 2, 2} : Place{index - 1, 2, 1, 1};
        return {2 + (index - 3) / kRows, (index - 3) % kRows, 1, 1};
    case Pattern::custom:
        if (index < static_cast<int>(group.places.size()))
            return group.places[static_cast<std::size_t>(index)];
        return {};
    case Pattern::pixels:
        return {};
    default:
        return {index / kRows, index % kRows, 1, 1};
    }
}

int columns(const Group &group)
{
    if (group.size <= 0)
        return std::max(1, group.cols);
    int cols = 1;
    switch (group.pattern)
    {
    case Pattern::lead:
        cols = group.size <= 3 ? 2 : 2 + (group.size - 3 + kRows - 1) / kRows;
        break;
    case Pattern::pixels:
        for (const ui::Rect &r : group.rects)
            cols = std::max(cols, static_cast<int>(std::ceil((r.x + r.w + kGap) / kStep)));
        break;
    default:
        for (int i = 0; i < group.size; ++i)
        {
            const Place p = place(group, i);
            cols = std::max(cols, p.col + p.w);
        }
        break;
    }
    // Free content takes the room it asks for.
    return std::max(cols, group.cols);
}

float View::group_x(const Page &page, int group) const
{
    float x = kLeft;
    for (int g = 0; g < group && g < static_cast<int>(page.groups.size()); ++g)
        x += static_cast<float>(columns(page.groups[static_cast<std::size_t>(g)])) * kStep - kGap +
             kGroupGap;
    return x;
}

ui::Rect View::rect(const Page &page, int group, int index) const
{
    const Group &g = page.groups[static_cast<std::size_t>(group)];
    if (g.pattern == Pattern::pixels)
    {
        ui::Rect r = index < static_cast<int>(g.rects.size()) ? g.rects[static_cast<std::size_t>(index)] : ui::Rect{};
        r.x += group_x(page, group);
        r.y += kTilesTop;
        return r;
    }
    const Place p = place(g, index);
    return {group_x(page, group) + static_cast<float>(p.col) * kStep,
            kTilesTop + static_cast<float>(p.row) * kStep, static_cast<float>(p.w) * kStep - kGap,
            static_cast<float>(p.h) * kStep - kGap};
}

void View::clamp(const Page &page)
{
    if (page.groups.empty())
    {
        focus = {};
        return;
    }
    focus.group = std::clamp(focus.group, 0, static_cast<int>(page.groups.size()) - 1);
    // An empty group can't hold the focus: the nearest one with tiles.
    if (page.groups[static_cast<std::size_t>(focus.group)].size == 0)
    {
        for (int d = 1; d < static_cast<int>(page.groups.size()); ++d)
        {
            for (int g : {focus.group + d, focus.group - d})
            {
                if (g >= 0 && g < static_cast<int>(page.groups.size()) &&
                    page.groups[static_cast<std::size_t>(g)].size > 0)
                {
                    focus.group = g;
                    focus.index = 0;
                    return;
                }
            }
        }
    }
    focus.index = std::clamp(focus.index, 0,
                             std::max(0, page.groups[static_cast<std::size_t>(focus.group)].size - 1));
}

bool View::move(const Page &page, const InputFrame &input, Feedback &feedback)
{
    if (input.nav == Direction::none || page.groups.empty())
        return false;
    clamp(page);
    const ui::Rect here = rect(page, focus.group, focus.index);
    const float hx = here.cx();
    const float hy = here.cy();
    Focus best = focus;
    float best_score = std::numeric_limits<float>::max();
    const bool horizontal = input.nav == Direction::left || input.nav == Direction::right;
    for (int g = 0; g < static_cast<int>(page.groups.size()); ++g)
    {
        // Up and down stay in the group; left and right may cross to a neighbour.
        if (!horizontal && g != focus.group)
            continue;
        if (horizontal && std::abs(g - focus.group) > 1)
            continue;
        const Group &group = page.groups[static_cast<std::size_t>(g)];
        for (int i = 0; i < group.size; ++i)
        {
            if ((g == focus.group && i == focus.index) || (group.focusable && !group.focusable(i)))
                continue;
            const ui::Rect r = rect(page, g, i);
            float along = 0.0f;
            float across = 0.0f;
            switch (input.nav)
            {
            case Direction::right:
                if (r.x < here.x + here.w - 1.0f)
                    continue;
                along = r.x - (here.x + here.w);
                across = std::max(0.0f, std::max(r.y - (here.y + here.h), here.y - (r.y + r.h)));
                break;
            case Direction::left:
                if (r.x + r.w > here.x + 1.0f)
                    continue;
                along = here.x - (r.x + r.w);
                across = std::max(0.0f, std::max(r.y - (here.y + here.h), here.y - (r.y + r.h)));
                break;
            case Direction::down:
                if (r.y < here.y + here.h - 1.0f)
                    continue;
                along = r.y - (here.y + here.h);
                across = std::fabs(r.cx() - hx) > here.w * 0.5f + r.w * 0.5f
                             ? std::fabs(r.cx() - hx)
                             : std::fabs(r.x - here.x) * 0.25f;
                break;
            case Direction::up:
                if (r.y + r.h > here.y + 1.0f)
                    continue;
                along = here.y - (r.y + r.h);
                across = std::fabs(r.cx() - hx) > here.w * 0.5f + r.w * 0.5f
                             ? std::fabs(r.cx() - hx)
                             : std::fabs(r.x - here.x) * 0.25f;
                break;
            default:
                break;
            }
            // Prefer the tile straight ahead; on a tie, the one nearer the centre line.
            const float score = along + across * 3.0f +
                                0.01f * (horizontal ? std::fabs(r.cy() - hy) : std::fabs(r.cx() - hx));
            if (score < best_score)
            {
                best_score = score;
                best = {g, i};
            }
        }
    }
    if (best.group == focus.group && best.index == focus.index)
    {
        refuse(feedback, input);
        return false;
    }
    focus = best;
    feedback.play(hui::audio::Cue::focus, 1.0f, 0.0f, 0.8f);
    return true;
}

bool View::jump(const Page &page, int delta, Feedback &feedback)
{
    // The next group with tiles to focus; groups of text are passed over.
    int next = focus.group + delta;
    while (next >= 0 && next < static_cast<int>(page.groups.size()) && page.groups[static_cast<std::size_t>(next)].size == 0)
        next += delta;
    if (next < 0 || next >= static_cast<int>(page.groups.size()))
        return false;
    focus = {next, 0};
    feedback.play(hui::audio::Cue::focus);
    return true;
}

bool View::focus_key(const Page &page, const std::string &key)
{
    if (key.empty())
        return false;
    for (int g = 0; g < static_cast<int>(page.groups.size()); ++g)
    {
        const Group &group = page.groups[static_cast<std::size_t>(g)];
        if (!group.fill)
            continue;
        for (int i = 0; i < group.size; ++i)
        {
            Tile tile;
            group.fill(i, tile);
            if (tile.key == key)
            {
                focus = {g, i};
                return true;
            }
        }
    }
    return false;
}

std::string View::focused_key(const Page &page) const
{
    if (focus.group < 0 || focus.group >= static_cast<int>(page.groups.size()))
        return {};
    const Group &group = page.groups[static_cast<std::size_t>(focus.group)];
    if (focus.index < 0 || focus.index >= group.size || !group.fill)
        return {};
    Tile tile;
    group.fill(focus.index, tile);
    return tile.key;
}

void View::animate(const Page &page, float dt)
{
    if (page.groups.empty())
        return;
    clamp(page);
    // The page slides so the focused group starts at the left edge when it fits,
    // and otherwise keeps the focused tile in view with a column to spare.
    const Group &group = page.groups[static_cast<std::size_t>(focus.group)];
    const float start = group_x(page, focus.group);
    const float width = static_cast<float>(columns(group)) * kStep - kGap;
    const ui::Rect r = rect(page, focus.group, focus.index);
    const float right_edge = 1920.0f - kLeft;
    float target = scroll_.target;
    if (shown_ >= 0 && shown_ < static_cast<int>(page.groups.size()))
    {
        // The first group boundary from which the shown group fits whole.
        const float end = group_x(page, shown_) + static_cast<float>(columns(page.groups[static_cast<std::size_t>(shown_)])) * kStep - kGap;
        target = 0.0f;
        for (int g = 0; g <= shown_ && end - target > right_edge; ++g)
            target = group_x(page, g + 1) - kLeft;
    }
    else if (focus.group == 0 && r.x + r.w <= right_edge)
        target = 0.0f;
    else if (width <= right_edge - kLeft && r.x + r.w - (start - kLeft) <= right_edge)
        target = start - kLeft;
    else if (r.x - target < kLeft + kStep)
        target = std::max(0.0f, r.x - kLeft - (r.x > start ? kStep : 0.0f));
    else if (r.x + r.w - target > right_edge - kStep)
        target = r.x + r.w - right_edge + kStep;
    scroll_.target = std::max(0.0f, target);
    const bool still = look::prefs().reduced_motion;
    scroll_.update(dt, still ? 40.0f : 9.0f);
    if (focus.group != last_.group || focus.index != last_.index)
    {
        pop_.snap(still ? 1.0f : 0.0f);
        last_ = focus;
    }
    pop_.target = 1.0f;
    pop_.update(dt, 14.0f);
}

void View::draw(Context &ctx, hui::gfx::DrawList &list, const Page &page, bool focused) const
{
    const float shift = scroll_.value;
    Tile focused_tile;
    ui::Rect focused_rect{};
    bool have_focused = false;
    for (int g = 0; g < static_cast<int>(page.groups.size()); ++g)
    {
        const Group &group = page.groups[static_cast<std::size_t>(g)];
        const float x = group_x(page, g) - shift;
        const float width = static_cast<float>(columns(group)) * kStep - kGap;
        if (x > 1920.0f || x + width < 0.0f)
            continue;
        if (!group.title.empty())
        {
            // .pn-g: Inter Tight 300, the count after it, faint.
            const auto &face = font(ctx, Face::light, group.title);
            const float base = baseline(kGroupTitle, kGroupTitleSize);
            const float w = ui::text(list, face, group.title, x, base, kGroupTitleSize, ui::Color::rgb(0xffffff, 0.92f));
            if (!group.count.empty())
                ui::text(list, font(ctx, Face::light, group.count), group.count,
                         x + w + face.measure(" ", kGroupTitleSize), base, kGroupTitleSize, ui::Color::rgb(0xffffff, 0.92f * 0.45f));
        }
        for (int i = 0; i < group.size; ++i)
        {
            ui::Rect r = rect(page, g, i);
            r.x -= shift;
            if (r.x > 1920.0f || r.x + r.w < 0.0f)
                continue;
            Tile tile;
            if (group.fill)
                group.fill(i, tile);
            const bool on = focused && g == focus.group && i == focus.index;
            if (on)
            {
                focused_tile = std::move(tile);
                focused_rect = r;
                have_focused = true;
                continue;
            }
            draw_tile(ctx, list, tile, r, 0.0f);
        }
        if (group.below && group.below_rows > 0)
        {
            const float top = kTilesTop + static_cast<float>(kRows - group.below_rows) * kStep;
            group.below(list, x, top, width);
        }
    }
    // The focused tile last, so its ring and growth sit over its neighbours.
    if (have_focused)
        draw_tile(ctx, list, focused_tile, focused_rect, 0.6f + 0.4f * pop_.value);
}

void draw_tile(Context &ctx, hui::gfx::DrawList &list, const Tile &tile, const ui::Rect &r,
               float focus)
{
    using ui::Color;
    const ui::Type &type = ctx.type;
    if (tile.button)
    {
        // A flat button in the tile's place: white once focused.
        Button b = tile.has_icon ? button(tile.label, tile.icon, focus > 0.5f) : button(tile.label, focus > 0.5f);
        b.enabled = !tile.off;
        draw_button(ctx, list, r.x, r.y, b);
        return;
    }
    const float scale = 1.0f + 0.03f * focus;
    list.push_transform(scale, r.cx(), r.cy(), 0.0f, 0.0f);
    if (focus > 0.01f)
        list.rounded_rect(r.inset(-6.0f), 0.0f, Color::rgb(0xffffff, std::min(1.0f, focus * 1.4f)));
    list.push_opacity(tile.off ? 0.4f : 1.0f);
    const Color fill = tile.has_bg ? tile.bg : kTile;
    list.rounded_rect(r, 0.0f, fill);
    // Sizes follow the tile: wider tiles carry larger type.
    const float fs = tile.label_size > 0.0f ? tile.label_size : (r.w >= 2.0f * kUnit ? std::round(kFont * 1.18f) : kFont);
    list.push_clip(r);
    // A game's name (and its line) only on the focused tile; the rest are art.
    const bool named = !tile.name_on_focus || focus > 0.01f;
    if (tile.game != nullptr)
    {
        draw_cover(ctx, list, *tile.game, r, 0.0f);
        if (!tile.label.empty() && named)
            list.gradient_rect({r.x, r.y + r.h * 0.4f, r.w, r.h * 0.6f}, 0.0f, Color::rgb(0x000000, 0.0f),
                               Color::rgb(0x000000, 0.72f));
    }
    if (tile.dim > 0.0f)
        list.rounded_rect(r, 0.0f, Color::rgb(0x000000, tile.dim));
    // A game's marks on every tile, as the browser version draws them: PS4
    // top left, a favourite's heart top right.
    if (tile.ps4)
    {
        const ui::Rect badge{r.x + 16.0f, r.y + 16.0f, type.medium.measure("PS4", 19.0f) + 24.0f, 36.0f};
        list.rounded_rect(badge, 0.0f, Color::rgb(0x141414, 0.9f));
        ui::text(list, type.medium, "PS4", badge.x + 12.0f, badge.cy() + 19.0f * 0.35f, 19.0f, Color::rgb(0xf4f4f4));
    }
    if (tile.star)
    {
        const float cx = r.x + r.w - 16.0f - 23.5f, cy = r.y + 16.0f + 23.5f;
        list.circle(cx, cy, 23.5f, Color::rgb(0x101010, 0.875f));
        icons::draw(list, icons::Icon::heart_fill, cx, cy, 28.0f, Color::rgb(0xffffff));
    }
    const float pad = std::round(fs * 0.62f);
    float kick_x = r.x + pad;
    if (tile.has_icon)
    {
        const float size = std::round(fs * 1.5f);
        icons::draw(list, tile.icon, r.x + pad + size * 0.5f, r.y + std::round(fs * 0.55f) + size * 0.5f, size,
                    Color::rgb(0xffffff), 1.6f);
        kick_x += size + std::round(fs * 0.5f);
    }
    if (!tile.kicker.empty())
    {
        // Capitals, wrapping inside the tile (half of it beside a number).
        const float size = std::round(fs * 0.58f);
        const float right = tile.number.empty() ? r.x + r.w - pad : r.x + r.w * 0.52f + (kick_x - r.x);
        float top = r.y + std::round(fs * 0.6f);
        for (const std::string &line : wrap_caps(ctx, tile.kicker, size, right - kick_x, 3))
        {
            draw_caps(ctx, &list, kick_x, top - size * 0.125f, line, size, Color::rgb(0xffffff, 0.8f));
            top += size * 1.35f;
        }
    }
    if (!tile.number.empty())
    {
        // .pn-num: extra-light, set tight, 0.9 line height, at the top right.
        const float size = tile.number_size > 0.0f ? tile.number_size : std::round(fs * 3.6f);
        hui::ui::text(list, font(ctx, Face::xlight, tile.number), tile.number, r.x + r.w - std::round(fs * 0.55f) + 0.05f * size,
                      r.y + std::round(fs * 0.35f) + size * 0.45f + size * 0.364f, size, Color::rgb(0xffffff), ui::Align::right,
                      -0.05f * size);
    }
    if (tile.check)
    {
        // A white corner with a dark check, top right.
        const float c = std::round(fs * 1.7f);
        const float corner[] = {r.x + r.w - c, r.y, r.x + r.w, r.y, r.x + r.w, r.y + c};
        list.polygon(corner, 3, Color::rgb(0xffffff));
        icons::draw(list, icons::Icon::check, r.x + r.w - fs * 0.52f, r.y + fs * 0.48f, std::round(fs * 0.8f),
                    Color::rgb(0x000000), 3.0f);
    }
    if (tile.selected)
        list.bordered_rect(r, 0.0f, Color{0, 0, 0, 0}, 3.0f, Color::rgb(0xffffff));
    const bool barred = tile.bar >= 0.0f;
    if (!tile.label.empty() && named)
    {
        // .pn-tl from the bottom: the label (Inter Tight 500, 1.15) and its
        // line under it (Inter, 0.66 of the size, 1.35, 3 px apart).
        const float width = r.w - 2.0f * pad;
        const float sub_size = std::round(fs * 0.66f);
        const bool tall = r.h > kUnit * 1.5f;
        const auto &label_font = font(ctx, Face::medium, tile.label);
        std::vector<std::string> label_lines = label_font.font->wrap(tile.label, fs, width);
        if (label_lines.size() > 2)
        {
            label_lines.resize(2);
            label_lines[1] = label_font.font->fit(label_lines[1] + " \xE2\x80\xA6", fs, width);
        }
        std::vector<std::string> sub_lines;
        if (!tile.sub.empty())
        {
            const std::size_t most = tall ? 5 : 3;
            sub_lines = type.regular.font->wrap(tile.sub, sub_size, width);
            if (sub_lines.size() > most)
            {
                sub_lines.resize(most);
                sub_lines[most - 1] = type.regular.font->fit(sub_lines[most - 1] + " \xE2\x80\xA6", sub_size, width);
            }
        }
        float bottom = r.y + r.h - std::round(fs * (barred ? 0.75f : 0.5f));
        for (auto it = sub_lines.rbegin(); it != sub_lines.rend(); ++it)
        {
            ui::text(list, type.regular, *it, r.x + pad, bottom - sub_size * 1.35f + baseline(0.0f, sub_size, 1.35f), sub_size,
                     Color::rgb(0xffffff, 0.82f));
            bottom -= sub_size * 1.35f;
        }
        if (!sub_lines.empty())
            bottom -= 3.0f;
        for (auto it = label_lines.rbegin(); it != label_lines.rend(); ++it)
        {
            ui::text(list, label_font, *it, r.x + pad, bottom - fs * 1.15f + baseline(0.0f, fs, 1.15f), fs, Color::rgb(0xffffff));
            bottom -= fs * 1.15f;
        }
    }
    if (barred)
    {
        const float h = std::max(4.0f, std::round(fs * 0.26f));
        list.rounded_rect({r.x, r.y + r.h - h, r.w, h}, 0.0f, Color::rgb(0xffffff, 0.22f));
        list.rounded_rect({r.x, r.y + r.h - h, r.w * std::clamp(tile.bar, 0.0f, 1.0f), h}, 0.0f,
                          tile.bar_bad ? kRed : tile.bar_warn ? kAmber : Color::rgb(0xffffff));
    }
    list.pop_clip();
    list.pop_opacity();
    list.pop_transform();
}

void draw_header(Context &ctx, hui::gfx::DrawList &list, std::string_view word, float shift,
                 float size, float top)
{
    const ui::Type &type = ctx.type;
    const auto &face = ui::face(type.xlight, type.light, word);
    const float left = kLeft - std::round(size * 0.07f) - shift;
    // The design's title is set tight (letter-spacing -0.05em).
    hui::ui::text(list, face, word, left, top + size * 0.86f, size, ui::Color::rgb(0xffffff),
                  ui::Align::left, -0.05f * size);
}

void draw_tray(Context &ctx, hui::gfx::DrawList &list, bool update_dot)
{
    // Top right, 26 px down: the update dot, the arrow and "2 · 24.6 MB/s", 10 px apart.
    const ui::Type &type = ctx.type;
    const Snapshot &state = ctx.store.state();
    int active = 0;
    double speed = 0.0;
    for (const Job &job : state.jobs)
    {
        if (!job_inactive(job) && job.status != "error")
        {
            ++active;
            speed += job.speed;
        }
    }
    float x = 1920.0f - kLeft;
    const float mid = 26.0f + 16.0f;
    if (active > 0)
    {
        std::string text = i18n::count(active);
        if (speed > 0.0)
            text += " \xC2\xB7 " + format_bytes(speed) + "/s";
        x -= ui::text(list, type.medium, text, x, mid + 20.0f * 0.364f, 20.0f, ui::color::text, ui::Align::right) + 10.0f;
        icons::draw(list, icons::Icon::down, x - 10.0f, mid, 20.0f, ui::color::text, 2.0f);
        x -= 20.0f + 10.0f;
    }
    if (update_dot)
        list.circle(x - 5.0f, mid, 5.0f, look::accent_color());
}

void draw_backdrop(Context &ctx, hui::gfx::DrawList &list, const Game *game, float opacity)
{
    // The cover itself, wider than the screen and very faint (the design's backArt).
    if (game == nullptr || opacity <= 0.01f)
        return;
    const Artwork *art = ctx.art.get(game->id, api::ArtKind::cover);
    if (art == nullptr)
        return;
    ui::image_cover(list, art->texture, art->width, art->height, {-260.0f, 0.0f, 2440.0f, 1080.0f},
                    ui::Color::rgb(0xffffff, opacity));
}

namespace
{
// The lowercase of a two-byte UTF-8 letter: accented Latin, Greek and Cyrillic.
unsigned lower_point(unsigned cp)
{
    if ((cp >= 0x00C0 && cp <= 0x00DE && cp != 0x00D7) || (cp >= 0x0391 && cp <= 0x03A9 && cp != 0x03A2) ||
        (cp >= 0x0410 && cp <= 0x042F))
        return cp + 0x20;
    if (cp >= 0x0400 && cp <= 0x040F)
        return cp + 0x50;
    if ((cp >= 0x0100 && cp <= 0x012F) || (cp >= 0x0132 && cp <= 0x0137) || (cp >= 0x014A && cp <= 0x0177))
        return cp % 2 == 0 ? cp + 1 : cp;
    if ((cp >= 0x0139 && cp <= 0x0148) || (cp >= 0x0179 && cp <= 0x017E))
        return cp % 2 == 1 ? cp + 1 : cp;
    switch (cp)
    {
    case 0x0178:
        return 0x00FF;
    case 0x0386:
        return 0x03AC;
    case 0x038C:
        return 0x03CC;
    default:
        break;
    }
    if (cp >= 0x0388 && cp <= 0x038A)
        return cp + 0x25;
    if (cp == 0x038E || cp == 0x038F)
        return cp + 0x3F;
    return cp;
}
} // namespace

std::string lower(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c >= 'A' && c <= 'Z')
            out += static_cast<char>(c - 'A' + 'a');
        else if (c >= 0xC2 && c <= 0xDF && i + 1 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80)
        {
            const unsigned cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3Fu);
            if (cp == 0x0130)
                out += 'i'; // Turkish dotted I
            else
            {
                const unsigned to = lower_point(cp);
                out += static_cast<char>(0xC0 | (to >> 6));
                out += static_cast<char>(0x80 | (to & 0x3F));
            }
            ++i;
        }
        else
            out += static_cast<char>(c);
    }
    return out;
}

} // namespace orbit::pano
