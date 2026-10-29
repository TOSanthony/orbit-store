// Orbit Store TV app - Crossbar: categories across, the list down.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/xb.hpp"

#include "orbit/i18n.hpp"
#include "orbit/look.hpp"
#include "orbit/screens.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace orbit::xb
{

using ui::Color;

namespace
{

const Color kText2 = Color::rgb(0xffffff, 0.72f);
const Color kText3 = Color::rgb(0xffffff, 0.55f);
const Color kAmber = Color::rgb(0xffd27a);
const Color kGlass = Color::rgb(0x0a0c2c, 0.62f);
const Color kMenu = Color::rgb(0x16163f, 0.96f);

// The design's rows: covers of 96 px at rest and 176 px focused, icon tiles
// of 80 and 128; the next row starts 128 px down after a cover, 106 after a
// tile. Text has a 1.6 line height, so a line's baseline sits 0.33 of its
// size below the middle of its line.
constexpr float kAboveTop = 150.0f; // the row before the focused one, faded over the bar
constexpr float kFocusedX = 330.0f;
constexpr float kRestX = 370.0f;
constexpr float kGap = 28.0f; // between the picture and the text

float baseline(float top, float size, float line = 1.6f)
{
    return top + size * line * 0.5f + size * 0.33f;
}
float pic_size(const Row &row, bool on)
{
    return row.game != nullptr ? (on ? 176.0f : 96.0f) : (on ? 128.0f : 80.0f);
}
float title_size(const Row &row, bool on)
{
    return row.game != nullptr ? (on ? 46.0f : 30.0f) : (on ? 40.0f : 28.0f);
}
float pitch(const Row &row)
{
    return row.game != nullptr ? 128.0f : 106.0f;
}
const std::string &focused_sub(const Row &row)
{
    return row.meta.empty() ? row.sub : row.meta;
}
int lines(const hui::ui::FontRef &font, std::string_view text, float size, float width, int most)
{
    return text.empty() ? 0 : std::min(most, static_cast<int>(font.font->wrap(text, size, width).size()));
}

const hui::ui::FontRef &regular(const ui::Type &type, std::string_view text)
{
    return ui::face(type.sora, type.regular, text);
}
const hui::ui::FontRef &bold(const ui::Type &type, std::string_view text)
{
    return ui::face(type.sora_semibold, type.semibold, text);
}

// The focused row's text column: its height as laid out, to centre it on
// the picture, and the room the design gives it before the next row.
struct Block
{
    float text = 0.0f;
    float room = 0.0f;
};
Block focused_block(const ui::Type &type, const Row &row)
{
    const float size = pic_size(row, true);
    const float width = kListRight - kFocusedX - size - kGap;
    const float title = title_size(row, true);
    const std::string &sub = focused_sub(row);
    const int sub_lines = lines(regular(type, sub), sub, 22.0f, width, 2);
    const int text_lines = lines(regular(type, row.text), row.text, 20.0f, width, 6);
    Block block;
    block.text = title * 1.6f;
    block.room = title * 1.25f;
    if (!row.value.empty())
    {
        block.text += 8.0f + 24.0f * 1.6f;
        block.room += 38.0f;
    }
    if (sub_lines > 0)
    {
        block.text += 6.0f + 22.0f * 1.6f * static_cast<float>(sub_lines);
        block.room += 6.0f + 30.0f * static_cast<float>(sub_lines);
    }
    if (row.bar >= 0.0f)
    {
        block.text += 28.0f;
        block.room += 32.0f;
    }
    if (!row.detail.empty())
    {
        block.text += 12.0f + 19.0f * 1.6f;
        block.room += 32.0f;
    }
    if (text_lines > 0)
    {
        block.text += 14.0f + 30.0f * static_cast<float>(text_lines);
        block.room += 14.0f + 30.0f * static_cast<float>(text_lines);
    }
    if (!row.hint.empty())
    {
        block.text += 52.0f;
        block.room += 52.0f;
    }
    block.room = std::max(size, block.room);
    return block;
}

// The focus ring with its glow outside the shape only, as CSS draws a
// box-shadow: a filled glow would show through a translucent tile.
void outer_ring(hui::gfx::DrawList &out, const ui::Rect &r, float radius, float amount)
{
    for (int k = 0; k < 14; ++k)
    {
        const float d = 3.0f + 3.0f * static_cast<float>(k);
        const float fade = (d + 1.5f) / 20.0f;
        out.bordered_rect(r.inset(-(d + 3.0f)), radius + d + 3.0f, Color{0, 0, 0, 0}, 3.0f,
                          Color::rgb(0xffffff, 0.3f * amount * std::exp(-fade * fade)));
    }
    out.bordered_rect(r.inset(-3.0f), radius + 3.0f, Color{0, 0, 0, 0}, 3.0f, Color::rgb(0xffffff, 0.95f * amount));
}

void draw_icon_tile(Context &ctx, hui::gfx::DrawList &out, const Row &row, const ui::Rect &r, float big)
{
    const float radius = 20.0f + 8.0f * big;
    if (big > 0.01f)
    {
        if (row.primary)
            ui::focus_ring(out, r, radius, big);
        else
            outer_ring(out, r, radius, big);
    }
    if (row.primary)
    {
        out.shadow({r.x, r.y + 12.0f + 8.0f * big, r.w, r.h}, radius, 30.0f + 10.0f * big, Color::rgb(0x000000, 0.3f));
        out.rounded_rect(r, radius, Color::rgb(0xffffff));
    }
    else
    {
        out.rounded_rect(r, radius, Color::rgb(0xffffff, 0.12f));
        if (big < 0.5f)
            out.bordered_rect(r, radius, Color{0, 0, 0, 0}, 1.0f, Color::rgb(0xffffff, 0.16f));
    }
    if (row.mark && ctx.mark != 0)
    {
        const float m = r.w * 0.5f;
        out.image(ctx.mark, {r.cx() - m * 0.5f, r.cy() - m * 0.5f, m, m}, hui::gfx::kFullUv, Color::rgb(0xffffff));
        return;
    }
    icons::draw(out, row.icon, r.cx(), r.cy(), r.w * 0.45f, row.primary ? Color::rgb(0x141433) : Color::rgb(0xffffff), 1.5f);
}

// The pill after a title, centred on cy; returns its width.
float draw_tag(const ui::Type &type, hui::gfx::DrawList &out, const Row &row, float x, float cy, bool on)
{
    const float size = on ? 16.0f : 14.0f;
    const float pad = on ? 10.0f : 8.0f;
    const float w = type.semibold.measure(row.tag, size) + pad * 2.0f;
    const float h = size * 1.2f + (on ? 10.0f : 6.0f);
    Color fill = Color::rgb(0xffffff, 0.12f);
    Color ink = Color::rgb(0xffffff);
    if (row.tag_kind == Tag::light)
    {
        fill = Color::rgb(0xffffff);
        ink = Color::rgb(0x141433);
    }
    else if (row.tag_kind == Tag::warn)
    {
        fill = kAmber;
        ink = Color::rgb(0x2a1a00);
    }
    else if (row.tag_kind == Tag::route)
    {
        fill = Color::rgb(0xb8a6ff);
        ink = Color::rgb(0x1a1040);
    }
    out.rounded_rect({x, cy - h * 0.5f, w, h}, h * 0.5f, fill);
    ui::text(out, type.semibold, row.tag, x + w * 0.5f, cy + size * 0.36f, size, ink, ui::Align::center);
    return w;
}

// A switch with On or Off before it, or a check box, from x; returns its width.
float control_width(const ui::Type &type, const Row &row, bool on)
{
    if (row.check >= 0)
        return 40.0f;
    if (row.toggle < 0)
        return 0.0f;
    const std::string_view label = row.toggle > 0 ? tr("On") : tr("Off");
    return regular(type, label).measure(label, on ? 24.0f : 21.0f) + 14.0f + 64.0f;
}
void draw_control(const ui::Type &type, hui::gfx::DrawList &out, const Row &row, float x, float cy, bool on)
{
    if (row.check >= 0)
    {
        const ui::Rect box{x, cy - 20.0f, 40.0f, 40.0f};
        if (row.check > 0)
        {
            out.rounded_rect(box, 8.0f, Color::rgb(0xffffff));
            icons::draw(out, icons::Icon::check, box.cx(), box.cy(), 26.0f, Color::rgb(0x141433), 3.0f);
        }
        else
            out.bordered_rect(box, 8.0f, Color{0, 0, 0, 0}, 2.5f, Color::rgb(0xffffff, 0.7f));
        return;
    }
    if (row.toggle < 0)
        return;
    const std::string_view label = row.toggle > 0 ? tr("On") : tr("Off");
    const float size = on ? 24.0f : 21.0f;
    x += ui::text(out, regular(type, label), label, x, cy + size * 0.33f, size, Color::rgb(0xffffff, on ? 1.0f : 0.72f)) + 14.0f;
    const ui::Rect track{x, cy - 18.0f, 64.0f, 36.0f};
    out.rounded_rect(track, 18.0f, row.toggle > 0 ? Color::rgb(0x8fe8c4) : Color::rgb(0xffffff, 0.22f));
    out.circle(track.x + (row.toggle > 0 ? 46.0f : 18.0f), track.cy(), 15.0f, Color::rgb(0xffffff));
}

} // namespace

// ---------------------------------------------------------------- View

void View::clamp(const List &list)
{
    index = std::clamp(index, 0, std::max(0, list.size - 1));
}

bool View::move(const List &list, const hui::InputFrame &input, hui::ui::Feedback &feedback)
{
    if (input.nav != hui::Direction::up && input.nav != hui::Direction::down)
        return false;
    const int next = index + (input.nav == hui::Direction::down ? 1 : -1);
    if (next < 0 || next >= list.size)
    {
        refuse(feedback, input);
        return false;
    }
    index = next;
    feedback.play(hui::audio::Cue::focus);
    return true;
}

void View::animate(float dt)
{
    at_.target = static_cast<float>(index);
    at_.update(dt, look::prefs().reduced_motion ? 60.0f : 13.0f);
}

void View::snap()
{
    at_.snap(static_cast<float>(index));
}

bool View::focus_key(const List &list, const std::string &key)
{
    for (int i = 0; i < list.size; ++i)
    {
        Row row;
        list.fill(i, row);
        if (row.key == key)
        {
            index = i;
            snap();
            return true;
        }
    }
    return false;
}

std::string View::focused_key(const List &list) const
{
    if (index < 0 || index >= list.size)
        return {};
    Row row;
    list.fill(index, row);
    return row.key;
}

void View::draw(Context &ctx, hui::gfx::DrawList &out, const List &list) const
{
    if (list.size <= 0)
        return;
    const ui::Type &type = ctx.type;
    const float at = at_.value;
    const int base = std::clamp(static_cast<int>(std::floor(at)), 0, list.size - 1);
    const float t = base + 1 < list.size ? at - static_cast<float>(base) : 0.0f;
    const int first = std::max(0, base - 2);
    const int last = std::min(list.size - 1, base + 9);
    std::vector<Row> rows(static_cast<std::size_t>(last - first + 1));
    for (int i = first; i <= last; ++i)
        list.fill(i, rows[static_cast<std::size_t>(i - first)]);
    const auto row_at = [&](int i) -> const Row & { return rows[static_cast<std::size_t>(i - first)]; };
    // Where row i's top sits while the focus rests on row f.
    const auto top_at = [&](int i, int f) -> float
    {
        if (i == f)
            return kRowTop;
        if (i < f)
            return kAboveTop - 128.0f * static_cast<float>(f - 1 - i);
        float y = kRowTop + focused_block(type, row_at(f)).room + 46.0f;
        for (int k = f + 1; k < i; ++k)
            y += pitch(row_at(k));
        return y;
    };
    for (int i = first; i <= last; ++i)
    {
        const Row &row = row_at(i);
        float y = top_at(i, base);
        if (t > 0.0f)
            y += (top_at(i, base + 1) - y) * t;
        // The design leaves out rows that would start below 1000 px.
        if (y > 1000.0f || y < -200.0f)
            continue;
        // 1 on the focused row, easing as the focus moves.
        const float big = std::clamp(1.0f - std::abs(static_cast<float>(i) - at), 0.0f, 1.0f);
        const float fi = static_cast<float>(i);
        float alpha = 1.0f;
        if (fi < at && big < 0.5f)
            alpha = i == index - 1 ? 0.32f : 0.0f;
        else if (fi > at)
            alpha = std::clamp(1.0f - (fi - at - 1.0f) * 0.15f, 0.38f, 1.0f);
        if (row.off && alpha > 0.0f)
            alpha = 0.45f;
        if (alpha <= 0.01f)
            continue;
        out.push_opacity(alpha);
        const bool on = big > 0.5f;
        const auto lerp = [big](float a, float b) { return a + (b - a) * big; };
        // The row is as tall as its picture or its text, whichever is taller;
        // the other is centred on it.
        const std::string &sub = on ? focused_sub(row) : row.sub;
        const float rest_title = title_size(row, false);
        const float rest_text = rest_title * 1.6f + (row.sub.empty() ? 0.0f : 4.0f + 19.0f * 1.6f) + (row.bar > 0.0f ? 15.0f : 0.0f);
        const Block block = big > 0.0f ? focused_block(type, row) : Block{};
        const float rest_pic = pic_size(row, false);
        const float on_pic = pic_size(row, true);
        const float pic = lerp(rest_pic, on_pic);
        const float pic_top = y + lerp((std::max(rest_pic, rest_text) - rest_pic) * 0.5f,
                                       (std::max(on_pic, block.text) - on_pic) * 0.5f);
        const ui::Rect pic_r{lerp(kRestX, kFocusedX), pic_top, pic, pic};
        if (row.game != nullptr)
        {
            const float radius = 12.0f + 6.0f * big;
            if (big > 0.01f)
                ui::focus_ring(out, pic_r, radius, big);
            draw_cover(ctx, out, *row.game, pic_r, radius);
        }
        else if (row.has_icon || row.mark)
            draw_icon_tile(ctx, out, row, pic_r, big);
        const float tx = pic_r.x + pic + kGap;
        const float width = kListRight - tx;
        const Color white = Color::rgb(0xeceef4);
        const Color sub_ink = row.warn ? kAmber : Color::rgb(0xffffff, 0.68f);
        if (on)
        {
            // The title (with its tag, heart and control), the value, the sub
            // line, progress, more text, then "Actions".
            float top = y + (std::max(on_pic, block.text) - block.text) * 0.5f;
            const float size = title_size(row, true);
            const float b = baseline(top, size);
            const float mid = b - size * 0.3f;
            const float extras = (row.tag.empty() ? 0.0f : type.semibold.measure(row.tag, 16.0f) + 34.0f) +
                                 (row.favourite ? 42.0f : 0.0f) +
                                 (control_width(type, row, true) > 0.0f ? control_width(type, row, true) + 20.0f : 0.0f);
            float after = tx + ui::text_fit(out, bold(type, row.title), row.title, tx, b, size, width - extras, white);
            if (!row.tag.empty())
                after += 14.0f + draw_tag(type, out, row, after + 14.0f, mid, true);
            if (row.favourite)
            {
                icons::draw(out, icons::Icon::heart_fill, after + 27.0f, mid, 30.0f, Color::rgb(0xff9aa8));
                after += 42.0f;
            }
            if (control_width(type, row, true) > 0.0f)
                draw_control(type, out, row, after + 20.0f, mid, true);
            top += size * 1.6f;
            if (!row.value.empty())
            {
                top += 8.0f;
                const float vb = baseline(top, 24.0f);
                const float drawn = ui::text_fit(out, regular(type, row.value), row.value, tx, vb, 24.0f, width - 24.0f, Color::rgb(0xffffff));
                if (row.dot)
                    out.circle(tx + drawn + 16.0f, vb - 8.0f, 6.0f, kAmber);
                top += 24.0f * 1.6f;
            }
            if (!sub.empty())
            {
                top += 6.0f;
                const int n = lines(regular(type, sub), sub, 22.0f, width, 2);
                ui::paragraph(out, regular(type, sub), sub, tx, baseline(top, 22.0f), 22.0f, width, 22.0f * 1.6f,
                              row.meta.empty() ? sub_ink : Color::rgb(0xffffff, 0.68f), 2);
                top += 22.0f * 1.6f * static_cast<float>(n);
            }
            if (row.bar >= 0.0f)
            {
                top += 20.0f;
                ui::progress_bar(out, {tx, top, std::min(520.0f, width), 8.0f}, std::clamp(row.bar, 0.0f, 1.0f), Color::rgb(0xffffff));
                top += 8.0f;
            }
            if (!row.detail.empty())
            {
                top += 12.0f;
                ui::text_fit(out, regular(type, row.detail), row.detail, tx, baseline(top, 19.0f), 19.0f, width,
                             Color::rgb(0xffffff, 0.68f));
                top += 19.0f * 1.6f;
            }
            if (!row.text.empty())
            {
                top += 14.0f;
                const int n = lines(regular(type, row.text), row.text, 20.0f, width, 6);
                ui::paragraph(out, regular(type, row.text), row.text, tx, baseline(top, 20.0f, 1.5f), 20.0f, width, 30.0f,
                              Color::rgb(0xffffff, 0.78f), 6);
                top += 30.0f * static_cast<float>(n);
            }
            if (!row.hint.empty())
            {
                top += 18.0f;
                out.circle(tx + 17.0f, top + 17.0f, 17.0f, Color::rgb(0xffffff, 0.16f));
                icons::draw(out, icons::Icon::next, tx + 17.0f, top + 17.0f, 18.0f, Color::rgb(0xffffff), 2.4f);
                ui::text(out, regular(type, row.hint), row.hint, tx + 46.0f, top + 17.0f + 19.0f * 0.33f, 19.0f,
                         Color::rgb(0xffffff, 0.82f));
            }
        }
        else
        {
            // At rest: the title and sub line, the value or control on the right.
            const float row_h = std::max(rest_pic, rest_text);
            const float mid = y + row_h * 0.5f;
            float right = 0.0f;
            if (control_width(type, row, false) > 0.0f)
            {
                right = control_width(type, row, false);
                draw_control(type, out, row, kListRight - right, mid, false);
            }
            else if (!row.value.empty())
            {
                const float dot = row.dot ? 22.0f : 0.0f;
                right = std::min(340.0f, regular(type, row.value).measure(row.value, 21.0f)) + dot;
                ui::text_fit(out, regular(type, row.value), row.value, kListRight - dot, mid + 21.0f * 0.33f, 21.0f, 340.0f,
                             Color::rgb(0xffffff, 0.72f), ui::Align::right);
                if (row.dot)
                    out.circle(kListRight - 6.0f, mid, 6.0f, kAmber);
            }
            const float room = width - (right > 0.0f ? right + kGap : 0.0f);
            float top = y + (row_h - rest_text) * 0.5f;
            const float size = rest_title;
            const float b = baseline(top, size);
            const float extras = (row.tag.empty() ? 0.0f : type.semibold.measure(row.tag, 14.0f) + 30.0f) + (row.favourite ? 34.0f : 0.0f);
            float after = tx + ui::text_fit(out, regular(type, row.title), row.title, tx, b, size, room - extras, white);
            if (!row.tag.empty())
                after += 14.0f + draw_tag(type, out, row, after + 14.0f, b - size * 0.3f, false);
            if (row.favourite)
                icons::draw(out, icons::Icon::heart_fill, after + 23.0f, b - size * 0.3f, 22.0f, Color::rgb(0xff9aa8));
            top += size * 1.6f;
            if (!row.sub.empty())
            {
                top += 4.0f;
                ui::text_fit(out, regular(type, row.sub), row.sub, tx, baseline(top, 19.0f), 19.0f, room, sub_ink);
                top += 19.0f * 1.6f;
            }
            if (row.bar > 0.0f)
                ui::progress_bar(out, {tx, top + 10.0f, 360.0f, 5.0f}, std::clamp(row.bar, 0.0f, 1.0f), Color::rgb(0xffffff, 0.65f));
        }
        out.pop_opacity();
    }
}

// ---------------------------------------------------------------- the bar

std::vector<BarItem> sections(Context &ctx, bool update_dot)
{
    const Snapshot &state = ctx.store.state();
    int active = 0;
    for (const Job &job : state.jobs)
        if (!job_inactive(job) && job.status != "error")
            ++active;
    static const icons::Icon kIcons[section::count] = {icons::Icon::search, icons::Icon::spark, icons::Icon::grid,
                                                       icons::Icon::laptop, icons::Icon::layers, icons::Icon::books,  icons::Icon::down,
                                                       icons::Icon::refresh, icons::Icon::cog};
    std::vector<BarItem> items;
    for (int s = 0; s < section::count; ++s)
    {
        BarItem item;
        item.icon = kIcons[s];
        item.label = tr(section_title(s));
        if (s == section::downloads)
            item.badge = active;
        if (s == section::updates)
            item.badge = state.game_updates;
        if (s == section::settings)
            item.dot = update_dot;
        items.push_back(std::move(item));
    }
    return items;
}

void draw_bar(Context &ctx, hui::gfx::DrawList &out, const std::vector<BarItem> &items, int current)
{
    const ui::Type &type = ctx.type;
    // Nested ({category, folder}): the category is dimmed and loses its count.
    const bool nested = items.size() == 2 && current == 1;
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
    {
        const BarItem &item = items[static_cast<std::size_t>(i)];
        const bool parent = nested && i == 0;
        const float x = kBarX + static_cast<float>(i - current) * kBarStep;
        if (x < 120.0f || x > 1820.0f)
            continue;
        const bool on = i == current;
        const float size = on ? 96.0f : 62.0f;
        if (on)
            out.glow({x - 110.0f, kBarY - 70.0f, 220.0f, 140.0f}, 70.0f, 40.0f, Color::rgb(0xffffff, 0.07f));
        const Color ink = on ? Color::rgb(0xffffff) : Color::rgb(0xffffff, parent ? 0.55f * 0.45f : 0.55f);
        if (item.game != nullptr)
        {
            const ui::Rect r{x - size * 0.5f, kBarY - size * 0.5f, size, size};
            if (on)
                ui::focus_ring(out, r, 20.0f, 1.0f);
            out.push_opacity(on ? 1.0f : 0.6f);
            draw_cover(ctx, out, *item.game, r, on ? 20.0f : 12.0f);
            out.pop_opacity();
        }
        else
            icons::draw(out, item.icon, x, kBarY, size, ink, on ? 1.4f : 1.6f);
        // The count in a white pill at the icon's top right; an update's amber dot.
        if (item.badge > 0 && !parent)
        {
            const std::string n = i18n::count(item.badge);
            const float w = std::max(30.0f, type.semibold.measure(n, 16.0f) + 16.0f);
            const ui::Rect b{x + size * 0.5f + 16.0f - w, kBarY - size * 0.5f - 8.0f, w, 30.0f};
            out.rounded_rect(b, 15.0f, Color::rgb(0xffffff));
            ui::text(out, type.semibold, n, b.cx(), b.cy() + 5.6f, 16.0f, Color::rgb(0x18183a), ui::Align::center);
        }
        if (item.dot && !parent)
        {
            out.circle(x + size * 0.5f, kBarY - size * 0.5f + 6.0f, 11.0f, Color::rgb(0x141432, 0.6f));
            out.circle(x + size * 0.5f, kBarY - size * 0.5f + 6.0f, 8.0f, kAmber);
        }
        if (on && !item.label.empty())
            ui::text_fit(out, regular(type, item.label), item.label, x, kBarY + 91.0f, 26.0f, 380.0f,
                         Color::rgb(0xffffff), ui::Align::center);
    }
}

void draw_header(Context &ctx, hui::gfx::DrawList &out, bool addons)
{
    // The wordmark, and at the top right the search field, as the browser
    // version and Zero have it (Search is the bar's first section here).
    const ui::Type &type = ctx.type;
    if (ctx.mark != 0)
        out.image(ctx.mark, {100.0f, 46.0f, 34.0f, 34.0f}, hui::gfx::kFullUv, Color::rgb(0xffffff));
    ui::text(out, bold(type, "Orbit"), "Orbit", 148.0f, 71.0f, 24.0f, Color::rgb(0xffffff));
    const ui::Rect field{1480.0f, 38.0f, 340.0f, 50.0f};
    out.bordered_rect(field, 25.0f, Color::rgb(0xffffff, 0.12f), 1.0f, Color::rgb(0xffffff, 0.2f));
    const Color ink = Color::rgb(0xffffff, 0.7f);
    icons::draw(out, icons::Icon::search, field.x + 30.5f, field.cy(), 21.0f, ink, 1.8f);
    const std::string_view hint = tr(addons ? "Search add-ons or base games" : /* i18n */ "Search games or title ID");
    ui::text_fit(out, regular(type, hint), hint, field.x + 53.0f, field.cy() + 19.0f * 0.35f, 19.0f, field.w - 53.0f - 20.0f, ink);
}

// ---------------------------------------------------------------- the right side

void draw_preview(Context &ctx, hui::gfx::DrawList &out, const Game &game, float x)
{
    // The design's preview: the art (16:9), the tagline, the description,
    // then chips for the genre, year, platform and size.
    const ui::Type &type = ctx.type;
    const ui::Rect r{x, 452.0f, kPreviewW, 332.0f};
    out.shadow({r.x, r.y + 30.0f, r.w, r.h}, 22.0f, 60.0f, Color::rgb(0x000000, 0.35f));
    const Artwork *art = ctx.art.get(game.id, api::ArtKind::thumbnail);
    if (art != nullptr)
        ui::image_cover(out, art->texture, art->width, art->height, r, Color::rgb(0xffffff), 22.0f);
    else
        ui::cover_placeholder(out, type, r, game.title, 22.0f);
    // Three lines of text at most, so the chips stay clear of the hints.
    float top = r.y + r.h + 24.0f;
    int room = 3;
    if (!game.tagline.empty())
    {
        const int n = lines(bold(type, game.tagline), game.tagline, 28.0f, kPreviewW, 2);
        ui::paragraph(out, bold(type, game.tagline), game.tagline, x, baseline(top, 28.0f, 1.3f), 28.0f, kPreviewW, 36.4f,
                      Color::rgb(0xffffff), 2);
        top += 36.4f * static_cast<float>(n);
        room -= n;
    }
    if (!game.description.empty())
    {
        top += 10.0f;
        const int most = std::min(2, room);
        const int n = lines(regular(type, game.description), game.description, 19.0f, kPreviewW, most);
        ui::paragraph(out, regular(type, game.description), game.description, x, baseline(top, 19.0f, 1.5f), 19.0f, kPreviewW,
                      28.5f, Color::rgb(0xffffff, 0.72f), most);
        top += 28.5f * static_cast<float>(n);
    }
    std::vector<std::string> chips;
    if (!game.genre.empty())
        chips.push_back(i18n::genre(game.genre));
    if (game.release_date.size() >= 4)
        chips.push_back(game.release_date.substr(0, 4));
    if (!game.releases.empty())
    {
        chips.push_back(game.releases.front().platform == "ps4" ? "PS4" : "PS5");
        if (listed_size(game.releases.front()) > 0.0)
            chips.push_back(format_bytes(listed_size(game.releases.front())));
    }
    top += 18.0f;
    float cx = x;
    for (const std::string &chip : chips)
    {
        const float w = regular(type, chip).measure(chip, 17.0f) + 32.0f;
        if (cx + w > x + kPreviewW)
            break;
        out.rounded_rect({cx, top, w, 43.2f}, 21.6f, Color::rgb(0xffffff, 0.12f));
        ui::text(out, regular(type, chip), chip, cx + 16.0f, top + 21.6f + 17.0f * 0.33f, 17.0f, Color::rgb(0xffffff));
        cx += w + 12.0f;
    }
}

float draw_panel(hui::gfx::DrawList &out, float top, float height, float scroll,
                 const std::function<float(hui::gfx::DrawList &, float, float, float)> &body)
{
    const float x = 1190.0f, w = 630.0f, pad = 32.0f;
    // Measure first, then draw the glass behind.
    hui::gfx::DrawList scratch;
    const float content = body(scratch, x + pad, top + pad, w - 2.0f * pad) - top + pad;
    const float h = height > 0.0f ? height : std::min(content, 1000.0f - top);
    const float room = std::max(0.0f, content - h);
    const ui::Rect r{x, top, w, h};
    out.shadow({r.x, r.y + 30.0f, r.w, r.h}, 28.0f, 60.0f, Color::rgb(0x000000, 0.35f));
    out.bordered_rect(r, 28.0f, kGlass, 1.0f, Color::rgb(0xffffff, 0.14f));
    out.push_clip(r);
    body(out, x + pad, top + pad - std::clamp(scroll, 0.0f, room), w - 2.0f * pad);
    out.pop_clip();
    if (room > 0.0f)
    {
        // A thin bar along the right edge shows how much more there is.
        const float track = h - 2.0f * 28.0f;
        const float bar = std::max(40.0f, track * h / (h + room));
        const float at = (track - bar) * std::clamp(scroll / room, 0.0f, 1.0f);
        out.rounded_rect({x + w - 14.0f, top + 28.0f, 4.0f, track}, 2.0f, Color::rgb(0xffffff, 0.12f));
        out.rounded_rect({x + w - 14.0f, top + 28.0f + at, 4.0f, bar}, 2.0f, Color::rgb(0xffffff, 0.7f));
    }
    return room;
}

float draw_label(Context &ctx, hui::gfx::DrawList &out, float x, float y, std::string_view text)
{
    hui::ui::text(out, ctx.type.semibold, hui::ui::upper(text), x, baseline(y, 15.0f), 15.0f, Color::rgb(0xffffff, 0.6f),
                  ui::Align::left, 2.1f);
    return y + 15.0f * 1.6f;
}

float draw_facts(Context &ctx, hui::gfx::DrawList &out, float x, float y, float width,
                 const std::vector<std::pair<std::string, std::string>> &facts, int columns, const FactBadge &badge)
{
    // The design's grid: 26 px between columns, 20 between rows; a 16 px
    // label over a 19 px value.
    // A value wraps to two lines; its row is as tall as its tallest value.
    const ui::Type &type = ctx.type;
    // With badges each cell gives 50 px to the icon on its left.
    const float inset = badge ? 50.0f : 0.0f;
    const float column = (width - 26.0f * static_cast<float>(columns - 1)) / static_cast<float>(columns) - inset;
    const std::size_t step = static_cast<std::size_t>(columns);
    float top = y;
    for (std::size_t first = 0; first < facts.size(); first += step)
    {
        int tallest = 1;
        for (std::size_t i = first; i < std::min(facts.size(), first + step); ++i)
            tallest = std::max(tallest, lines(regular(type, facts[i].second), facts[i].second, 19.0f, column, 2));
        for (std::size_t i = first; i < std::min(facts.size(), first + step); ++i)
        {
            const float cx = x + inset + static_cast<float>(i - first) * (column + inset + 26.0f);
            if (badge)
                badge(out, i, cx - inset + 20.0f, top + 30.0f, 40.0f);
            ui::text_fit(out, regular(type, facts[i].first), facts[i].first, cx, baseline(top, 16.0f), 16.0f, column,
                         Color::rgb(0xffffff, 0.62f));
            ui::paragraph(out, regular(type, facts[i].second), facts[i].second, cx, baseline(top + 29.6f, 19.0f), 19.0f, column,
                          30.4f, Color::rgb(0xffffff), 2);
        }
        top += 29.6f + 30.4f * static_cast<float>(tallest) + 20.0f;
    }
    return facts.empty() ? y : top - 20.0f;
}

float draw_steps(Context &ctx, hui::gfx::DrawList &out, float x, float y, float width,
                 const std::vector<std::string> &steps)
{
    // Each step's number sits in a pill as tall as its text.
    const ui::Type &type = ctx.type;
    for (std::size_t i = 0; i < steps.size(); ++i)
    {
        std::string_view text = steps[i];
        const std::size_t dot = text.find(". ");
        if (dot != std::string_view::npos && dot <= 2 && std::isdigit(static_cast<unsigned char>(text[0])) != 0)
            text.remove_prefix(dot + 2);
        const std::string number = std::to_string(i + 1);
        const float pill = std::max(14.0f, type.semibold.measure(number, 20.0f) + 2.0f);
        const float tx = x + pill + 14.0f;
        const int n = lines(regular(type, text), text, 20.0f, width - pill - 14.0f, 6);
        const float h = 29.0f * static_cast<float>(std::max(1, n));
        out.rounded_rect({x, y, pill, h}, pill * 0.5f, Color::rgb(0xffffff, 0.16f));
        ui::text(out, type.semibold, number, x + pill * 0.5f, y + h * 0.5f + 20.0f * 0.33f, 20.0f, Color::rgb(0xffffff),
                 ui::Align::center);
        ui::paragraph(out, regular(type, text), text, tx, baseline(y, 20.0f, 1.45f), 20.0f, width - pill - 14.0f, 29.0f,
                      Color::rgb(0xeceef4), 6);
        y += h + (i + 1 < steps.size() ? 14.0f : 0.0f);
    }
    return y;
}

// ---------------------------------------------------------------- the menu

int Menu::update(const hui::InputFrame &input, const std::vector<MenuItem> &items, hui::ui::Feedback &feedback)
{
    if (!open)
        return -1;
    const int count = static_cast<int>(items.size());
    active = std::clamp(active, 0, std::max(0, count - 1));
    if (input.is_pressed(hui::Action::back))
    {
        open = false;
        feedback.play(hui::audio::Cue::back);
        return -2;
    }
    if (input.nav == hui::Direction::up || input.nav == hui::Direction::down)
    {
        const int step = input.nav == hui::Direction::down ? 1 : -1;
        int next = active + step;
        while (next >= 0 && next < count && !items[static_cast<std::size_t>(next)].enabled)
            next += step;
        if (next >= 0 && next < count)
        {
            active = next;
            feedback.play(hui::audio::Cue::focus);
        }
        else
            refuse(feedback, input);
    }
    if (input.is_pressed(hui::Action::confirm) && count > 0)
    {
        if (!items[static_cast<std::size_t>(active)].enabled)
        {
            refuse(feedback, input);
            return -1;
        }
        open = false;
        feedback.play(hui::audio::Cue::select);
        return active;
    }
    return -1;
}

void Menu::animate(float dt)
{
    in.target = open ? 1.0f : 0.0f;
    in.update(dt, 16.0f);
}

void draw_menu(Context &ctx, hui::gfx::DrawList &out, std::string_view title, const std::vector<MenuItem> &items,
               const Menu &menu, const MenuAt &at, const MenuFoot &foot)
{
    if (menu.in.value <= 0.01f)
        return;
    // The design's menu: 16 px of padding, a 16 px title, items of 25 px
    // text with 18 px sub lines, then the foot.
    const ui::Type &type = ctx.type;
    const float x = at.x, w = at.w;
    float top = at.y;
    const float inner = w - 32.0f;
    const auto lead = [](const MenuItem &item) { return item.radio ? 44.0f : item.has_icon ? 46.0f : 0.0f; };
    const auto value_w = [&](const MenuItem &item)
    { return item.value.empty() ? 0.0f : std::min(220.0f, regular(type, item.value).measure(item.value, 19.0f)) + 18.0f; };
    const auto sub_lines = [&](const MenuItem &item)
    { return lines(regular(type, item.sub), item.sub, 18.0f, inner - 40.0f - lead(item) - value_w(item), 3); };
    const auto item_h = [&](const MenuItem &item)
    { return item.sub.empty() ? at.row_height : 72.0f + 4.0f + 25.2f * static_cast<float>(sub_lines(item)); };
    const float note_w = foot.figures.empty() ? inner - 40.0f : inner - 52.0f;
    const int note_lines = lines(regular(type, foot.note), foot.note, 16.0f, note_w, 4);
    float foot_h = 0.0f;
    if (!foot.figures.empty())
        foot_h = 14.0f + 1.0f + 18.0f + 38.4f * static_cast<float>(foot.figures.size()) + (foot.bar >= 0.0f ? 20.0f : 0.0f) +
                 (note_lines > 0 ? 12.0f + 22.4f * static_cast<float>(note_lines) : 0.0f) + 8.0f;
    else if (note_lines > 0)
        foot_h = 12.0f + 25.6f * static_cast<float>(note_lines) + 6.0f;
    float h = 16.0f + 47.6f + foot_h + 16.0f;
    for (const MenuItem &item : items)
        h += item_h(item);
    top = std::min(top, at.bottom - h);
    const ui::Rect r{x, top, w, h};
    out.push_opacity(menu.in.value);
    out.shadow({r.x, r.y + 30.0f, r.w, r.h}, 28.0f, 60.0f, Color::rgb(0x000000, 0.35f));
    out.bordered_rect(r, 28.0f, kMenu, 1.0f, Color::rgb(0xffffff, 0.14f));
    hui::ui::text(out, type.semibold, hui::ui::upper(title), x + 32.0f, top + 42.0f, 16.0f, Color::rgb(0xffffff, 0.6f),
                  ui::Align::left, 2.2f);
    float y = top + 16.0f + 47.6f;
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
    {
        const MenuItem &item = items[static_cast<std::size_t>(i)];
        const ui::Rect ir{x + 16.0f, y, inner, item_h(item)};
        const bool on = i == menu.active;
        if (on)
            out.rounded_rect(ir, 16.0f, Color::rgb(0xffffff, 0.96f));
        const Color ink = on ? (item.danger ? Color::rgb(0xb0102a) : Color::rgb(0x141433))
                          : item.danger ? Color::rgb(0xff9aa8)
                                        : Color::rgb(0xffffff, 0.86f);
        out.push_opacity(item.enabled ? 1.0f : 0.42f);
        const float label_b = ir.y + (item.sub.empty() ? ir.h * 0.5f + 8.0f : 44.0f);
        const float mid = label_b - 8.0f;
        float tx = ir.x + 20.0f;
        if (item.radio && item.box)
        {
            // A box, filled and ticked when chosen: several can be.
            const ui::Rect box{tx + 1.0f, mid - 12.0f, 24.0f, 24.0f};
            if (item.checked)
            {
                out.rounded_rect(box, 6.0f, ink);
                ui::icon_check(out, box.x + 12.0f, mid, 16.0f, on ? Color::rgb(0xffffff) : Color::rgb(0x141433), 2.4f);
            }
            else
                out.bordered_rect(box, 6.0f, Color{0, 0, 0, 0}, 2.0f, on ? ink : Color::rgb(0xffffff, 0.6f));
        }
        else if (item.radio)
        {
            // The design's radio: a 2 px ring, or a 7 px one when chosen.
            if (item.checked)
                out.ring(tx + 13.0f, mid, 13.0f, 7.0f, ink);
            else
                out.ring(tx + 13.0f, mid, 13.0f, 2.0f, on ? ink : Color::rgb(0xffffff, 0.6f));
        }
        else if (item.has_icon)
            icons::draw(out, item.icon, tx + 14.0f, mid, 28.0f, ink, 2.0f);
        tx += lead(item);
        const float right = value_w(item);
        if (right > 0.0f)
            ui::text_fit(out, regular(type, item.value), item.value, ir.x + ir.w - 20.0f, mid + 19.0f * 0.33f, 19.0f, 220.0f,
                         on ? Color::rgb(0x141433, 0.62f) : Color::rgb(0xffffff, 0.6f), ui::Align::right);
        const float label_w = ir.x + ir.w - 20.0f - right - tx;
        ui::text_fit(out, on ? bold(type, item.label) : ui::face(type.sora, type.medium, item.label), item.label, tx, label_b,
                     25.0f, label_w, ink);
        if (!item.sub.empty())
            ui::paragraph(out, regular(type, item.sub), item.sub, tx, ir.y + 60.0f + 12.6f + 5.9f, 18.0f, label_w, 25.2f,
                          on ? Color::rgb(0x141433, 0.62f) : Color::rgb(0xffffff, 0.6f), 3);
        out.pop_opacity();
        y += ir.h;
    }
    if (!foot.figures.empty())
    {
        y += 14.0f;
        out.rounded_rect({x + 24.0f, y, w - 48.0f, 1.0f}, 0.0f, Color::rgb(0xffffff, 0.14f));
        y += 1.0f + 18.0f;
        for (const auto &[label, value] : foot.figures)
        {
            const float b = y + 4.0f + 15.2f + 6.3f;
            const float vw = regular(type, value).measure(value, 19.0f);
            ui::text_fit(out, regular(type, label), label, x + 42.0f, b, 19.0f, w - 84.0f - vw - 16.0f, Color::rgb(0xffffff, 0.78f));
            ui::text(out, regular(type, value), value, x + w - 42.0f, b, 19.0f, Color::rgb(0xffffff), ui::Align::right);
            y += 38.4f;
        }
        if (foot.bar >= 0.0f)
        {
            y += 12.0f;
            ui::progress_bar(out, {x + 42.0f, y, w - 84.0f, 8.0f}, std::clamp(foot.bar, 0.0f, 1.0f), Color::rgb(0xffffff));
            y += 8.0f;
        }
        if (note_lines > 0)
            ui::paragraph(out, regular(type, foot.note), foot.note, x + 42.0f, y + 12.0f + 11.2f + 5.3f, 16.0f, note_w, 22.4f,
                          Color::rgb(0xffffff, 0.55f), 4);
    }
    else if (note_lines > 0)
        ui::paragraph(out, regular(type, foot.note), foot.note, x + 36.0f, y + 12.0f + 12.8f + 5.3f, 16.0f, note_w, 25.6f,
                      Color::rgb(0xffffff, 0.55f), 4);
    out.pop_opacity();
}

void keyboard_look(hui::ui::KeyboardStyle &style, bool crossbar)
{
    const hui::ui::KeyboardStyle store;
    style.theme = keyboard_theme();
    style.panel = crossbar ? false : store.panel;
    style.gap = crossbar ? 10.0f : store.gap;
    style.radius_source = crossbar ? hui::ui::RadiusSource::custom : store.radius_source;
    style.radius = crossbar ? 12.0f : store.radius;
    style.label_size = crossbar ? 24.0f : store.label_size;
    style.wide_label_size = crossbar ? 20.0f : store.wide_label_size;
    style.glyph_size = crossbar ? 21.0f : store.glyph_size;
    style.binding_glyphs = store.binding_glyphs; // Panorama turns them off; switching back restores them
    if (crossbar)
    {
        style.theme.surface_high = Color::rgb(0xffffff, 0.1f);
        style.theme.secondary = Color::rgb(0xffffff, 0.1f);
        style.theme.primary = Color::rgb(0xffffff);
        style.theme.on_primary = Color::rgb(0x161636);
        style.theme.border = 0.0f;
    }
}

void draw_keyboard(Context &ctx, Frame &frame, const hui::ui::Keyboard &keys, std::string_view label, std::string_view value,
                   bool secret, float in)
{
    if (in <= 0.01f)
        return;
    hui::gfx::DrawList &out = frame.overlay;
    out.push_opacity(in);
    const ui::Rect panel{1000.0f, 410.0f, 820.0f, 510.0f};
    out.shadow({panel.x, panel.y + 30.0f, panel.w, panel.h}, 28.0f, 60.0f, Color::rgb(0x000000, 0.35f));
    out.bordered_rect(panel, 28.0f, kMenu, 1.0f, Color::rgb(0xffffff, 0.14f));
    draw_label(ctx, out, panel.x + 28.0f, panel.y + 26.0f, label);
    const ui::Rect field{1024.0f, 472.0f, 772.0f, 66.0f};
    out.bordered_rect(field, 33.0f, Color::rgb(0xffffff, 0.12f), 1.0f, Color::rgb(0xffffff, 0.2f));
    float tx = field.x + 24.0f;
    if (!secret)
    {
        icons::draw(out, icons::Icon::search, tx + 13.0f, field.cy(), 26.0f, Color::rgb(0xffffff), 1.8f);
        tx += 40.0f;
    }
    // Dots for a secret, one per character.
    std::string shown;
    if (secret)
        for (unsigned char c : value)
            if ((c & 0xc0) != 0x80)
                shown += "\xE2\x80\xA2";
    const std::string_view text = secret ? std::string_view(shown) : value;
    const float typed = ui::text_fit(out, regular(ctx.type, text), text, tx, field.cy() + 26.0f * 0.33f, 26.0f,
                                     field.x + field.w - 30.0f - tx, Color::rgb(0xffffff));
    out.rounded_rect({tx + typed + 4.0f, field.cy() - 16.0f, 2.0f, 32.0f}, 0.0f, Color::rgb(0xffffff));
    hui::ui::Canvas canvas{out, ctx.fonts, frame.glass_texture, ctx.clock};
    keys.draw(canvas);
    out.pop_opacity();
}

// ---------------------------------------------------------------- the band

void draw_band(Context &ctx, hui::gfx::DrawList &out, const Band &band, float in)
{
    if (in <= 0.01f)
        return;
    // The design's band: from 290 px down, its content 1080 px wide at 420.
    const ui::Type &type = ctx.type;
    const float x = 420.0f, width = 1080.0f;
    const int body_lines = lines(regular(type, band.body), band.body, 24.0f, 1040.0f, 8);
    const int note_lines = lines(regular(type, band.note), band.note, 20.0f, 1040.0f, 3);
    const int error_lines = lines(regular(type, band.error), band.error, 21.0f, 1040.0f, 4);
    float h = 54.0f + (band.kicker.empty() ? 0.0f : 27.2f + 12.0f) + 73.6f +
              (body_lines > 0 ? 16.0f + 37.2f * static_cast<float>(body_lines) : 0.0f) +
              (note_lines > 0 ? 10.0f + 30.0f * static_cast<float>(note_lines) : 0.0f) +
              (error_lines > 0 ? 12.0f + 30.0f * static_cast<float>(error_lines) : 0.0f) +
              (band.options.empty() ? 0.0f : 28.0f + 84.8f * static_cast<float>(band.options.size()) - 12.0f) +
              (band.controls.empty() ? 0.0f : 22.0f + 52.0f * static_cast<float>((band.controls.size() + 1) / 2)) +
              (band.buttons.empty() ? 0.0f : 36.0f + 66.0f) + 60.0f;
    out.push_opacity(in);
    out.rounded_rect({0, 0, 1920, 1080}, 0.0f, Color::rgb(0x040410, 0.5f));
    const ui::Rect r{0.0f, std::min(290.0f, 1060.0f - h), 1920.0f, h};
    out.shadow({r.x, r.y + 40.0f, r.w, r.h}, 0.0f, 90.0f, Color::rgb(0x000000, 0.45f));
    out.gradient_rect(r, 0.0f, Color::rgb(0x0e102e, 0.9f), Color::rgb(0x0a0c24, 0.95f));
    out.rounded_rect({0.0f, r.y, 1920.0f, 1.0f}, 0.0f, Color::rgb(0xffffff, 0.22f));
    out.rounded_rect({0.0f, r.y + r.h - 1.0f, 1920.0f, 1.0f}, 0.0f, Color::rgb(0xffffff, 0.22f));
    float y = r.y + 54.0f;
    if (!band.kicker.empty())
    {
        hui::ui::text(out, type.semibold, hui::ui::upper(band.kicker), x, baseline(y, 17.0f), 17.0f, Color::rgb(0xffffff, 0.6f),
                      ui::Align::left, 2.4f);
        y += 27.2f + 12.0f;
    }
    float tx = x;
    if (band.busy)
    {
        // The design's spinner: a faint ring with a white arc going round.
        out.ring(x + 20.0f, y + 36.8f, 20.0f, 4.0f, Color::rgb(0xffffff, 0.2f));
        out.arc(x + 20.0f, y + 36.8f, 20.0f, 4.0f, std::fmod(ctx.clock * 5.0f, 6.2831853f), 1.5708f, Color::rgb(0xffffff));
        tx += 62.0f;
    }
    ui::text_fit(out, bold(type, band.title), band.title, tx, baseline(y, 46.0f), 46.0f, width - (tx - x), Color::rgb(0xffffff));
    y += 73.6f;
    if (body_lines > 0)
    {
        ui::paragraph(out, regular(type, band.body), band.body, x, baseline(y + 16.0f, 24.0f, 1.55f), 24.0f, 1040.0f, 37.2f,
                      Color::rgb(0xffffff, 0.82f), 8);
        y += 16.0f + 37.2f * static_cast<float>(body_lines);
    }
    if (note_lines > 0)
    {
        ui::paragraph(out, regular(type, band.note), band.note, x, baseline(y + 10.0f, 20.0f, 1.5f), 20.0f, 1040.0f, 30.0f,
                      Color::rgb(0xffffff, 0.62f), 3);
        y += 10.0f + 30.0f * static_cast<float>(note_lines);
    }
    if (error_lines > 0)
    {
        ui::paragraph(out, regular(type, band.error), band.error, x, baseline(y + 12.0f, 21.0f, 1.43f), 21.0f, 1040.0f, 30.0f,
                      kAmber, 4);
        y += 12.0f + 30.0f * static_cast<float>(error_lines);
    }
    if (!band.controls.empty())
    {
        // Controls: each button and what it does, two columns 60 px apart.
        ui::HintStyle style{0.0f, 23.0f, 34.0f, 16.0f, Color::rgb(0xffffff, 0.85f)};
        y += 22.0f + ui::controls_grid(&out, type, band.controls, x, y + 22.0f, width + 60.0f, 52.0f, style);
    }
    if (!band.options.empty())
    {
        y += 28.0f;
        for (std::size_t i = 0; i < band.options.size(); ++i)
        {
            const MenuItem &option = band.options[i];
            const bool on = static_cast<int>(i) == band.focus;
            const ui::Rect o{x, y, width, 72.8f};
            out.push_opacity(option.enabled ? 1.0f : 0.42f);
            out.rounded_rect(o, 18.0f, on ? Color::rgb(0xffffff, 0.96f) : Color::rgb(0xffffff, 0.08f));
            const Color ink = on ? Color::rgb(0x141433) : Color::rgb(0xffffff);
            const ui::Rect box{o.x + 22.0f, o.cy() - 17.0f, 34.0f, 34.0f};
            if (option.checked)
            {
                out.rounded_rect(box, 8.0f, on ? Color::rgb(0x141433) : Color::rgb(0xffffff));
                icons::draw(out, icons::Icon::check, box.cx(), box.cy(), 22.0f, on ? Color::rgb(0xffffff) : Color::rgb(0x141433), 3.0f);
            }
            else
                out.bordered_rect(box, 8.0f, Color{0, 0, 0, 0}, 2.5f, ink);
            ui::text_fit(out, on ? bold(type, option.label) : regular(type, option.label), option.label, box.x + 52.0f,
                         o.cy() + 23.0f * 0.33f, 23.0f, o.w - 96.0f, ink);
            out.pop_opacity();
            y += 72.8f + 12.0f;
        }
        y -= 12.0f;
    }
    if (!band.buttons.empty())
    {
        y += 36.0f;
        float bx = x;
        for (std::size_t i = 0; i < band.buttons.size(); ++i)
        {
            const std::string &label = band.buttons[i];
            const float w = bold(type, label).measure(label, 23.0f) + 80.0f;
            const ui::Rect b{bx, y, w, 66.0f};
            const bool light = i == 0;
            const bool on = static_cast<int>(i + band.options.size()) == band.focus;
            if (on)
                ui::focus_ring(out, b, 33.0f, light ? 0.6f : 1.0f);
            out.rounded_rect(b, 33.0f, light ? Color::rgb(0xffffff) : Color::rgb(0xffffff, 0.12f));
            if (!light)
                out.bordered_rect(b, 33.0f, Color{0, 0, 0, 0}, 1.0f, Color::rgb(0xffffff, 0.18f));
            ui::text(out, bold(type, label), label, b.cx(), b.cy() + 23.0f * 0.35f, 23.0f,
                     light ? Color::rgb(0x161636) : Color::rgb(0xffffff), ui::Align::center);
            bx += w + 18.0f;
        }
    }
    out.pop_opacity();
}

float draw_toast(Context &ctx, hui::gfx::DrawList &out, float top, icons::Icon icon, Color tone, std::string_view text,
                 std::string_view sub)
{
    // The design's toast: glass, 14 px round a 42 px disc, 22 px text.
    const ui::Type &type = ctx.type;
    const float text_w = std::min(900.0f, std::max(regular(type, text).measure(text, 22.0f),
                                                   sub.empty() ? 0.0f : regular(type, sub).measure(sub, 17.0f)));
    const float h = sub.empty() ? 70.0f : 84.0f;
    const float w = 16.0f + 42.0f + 16.0f + text_w + 28.0f;
    const ui::Rect r{1820.0f - w, top, w, h};
    out.shadow({r.x, r.y + 30.0f, r.w, r.h}, h * 0.5f, 60.0f, Color::rgb(0x000000, 0.35f));
    out.bordered_rect(r, h * 0.5f, kMenu, 1.0f, Color::rgb(0xffffff, 0.14f));
    out.circle(r.x + 16.0f + 21.0f, r.cy(), 21.0f, tone);
    icons::draw(out, icon, r.x + 37.0f, r.cy(), 22.0f, Color::rgb(0x141433), 2.4f);
    const float tx = r.x + 16.0f + 42.0f + 16.0f;
    if (sub.empty())
        ui::text_fit(out, regular(type, text), text, tx, r.cy() + 22.0f * 0.35f, 22.0f, text_w, Color::rgb(0xffffff));
    else
    {
        ui::text_fit(out, regular(type, text), text, tx, r.cy() - 4.0f, 22.0f, text_w, Color::rgb(0xffffff));
        ui::text_fit(out, regular(type, sub), sub, tx, r.cy() + 21.0f, 17.0f, text_w, Color::rgb(0xffffff, 0.66f));
    }
    return h;
}

void draw_status(Context &ctx, hui::gfx::DrawList &out, const Status &status)
{
    // The design's column from 330 px: a 120 px mark (or 96 px spinner),
    // then the title 46 px below it, the body, the small print, the button.
    const ui::Type &type = ctx.type;
    if (ctx.mark != 0)
        out.image(ctx.mark, {100.0f, 46.0f, 34.0f, 34.0f}, hui::gfx::kFullUv, Color::rgb(0xffffff));
    ui::text(out, bold(type, "Orbit"), "Orbit", 148.0f, 71.0f, 24.0f, Color::rgb(0xffffff));
    float y = 330.0f;
    if (status.spin)
    {
        out.ring(960.0f, y + 48.0f, 48.0f, 6.0f, Color::rgb(0xffffff, 0.2f));
        out.arc(960.0f, y + 48.0f, 48.0f, 6.0f, std::fmod(ctx.clock * 5.0f, 6.2831853f), 1.5708f, Color::rgb(0xffffff));
        y += 96.0f;
    }
    else
    {
        out.glow({900.0f, y, 120.0f, 120.0f}, 60.0f, 40.0f, Color::rgb(0xffffff, 0.35f));
        if (ctx.mark != 0)
            out.image(ctx.mark, {900.0f, y, 120.0f, 120.0f}, hui::gfx::kFullUv, Color::rgb(0xffffff));
        y += 120.0f;
    }
    y += 46.0f;
    ui::text(out, bold(type, status.title), status.title, 960.0f, baseline(y, 46.0f), 46.0f, Color::rgb(0xffffff), ui::Align::center);
    y += 46.0f * 1.6f;
    if (!status.body.empty())
    {
        const int n = lines(regular(type, status.body), status.body, 24.0f, 1100.0f, 3);
        ui::paragraph(out, regular(type, status.body), status.body, 960.0f, baseline(y + 16.0f, 24.0f, 1.5f), 24.0f, 1100.0f,
                      36.0f, Color::rgb(0xffffff, 0.82f), 3, ui::Align::center);
        y += 16.0f + 36.0f * static_cast<float>(n);
    }
    if (!status.small.empty())
    {
        const int n = lines(regular(type, status.small), status.small, 18.0f, 1100.0f, 3);
        ui::paragraph(out, regular(type, status.small), status.small, 960.0f, baseline(y + 14.0f, 18.0f, 1.5f), 18.0f, 1100.0f,
                      27.0f, Color::rgb(0xffffff, 0.55f), 3, ui::Align::center);
        y += 14.0f + 27.0f * static_cast<float>(n);
    }
    if (!status.button.empty())
    {
        const float w = bold(type, status.button).measure(status.button, 22.0f) + 88.0f;
        const ui::Rect b{960.0f - w * 0.5f, y + 40.0f, w, 64.0f};
        ui::focus_ring(out, b, 32.0f, 0.6f);
        out.rounded_rect(b, 32.0f, Color::rgb(0xffffff));
        ui::text(out, bold(type, status.button), status.button, b.cx(), b.cy() + 22.0f * 0.35f, 22.0f, Color::rgb(0x161636),
                 ui::Align::center);
    }
}

// ---------------------------------------------------------------- the background

namespace
{
// A game's cover wash, when it's loaded.
const Artwork *wash_of(Context &ctx, const Game *game)
{
    if (game == nullptr)
        return nullptr;
    const Artwork *cover = ctx.art.get(game->id, api::ArtKind::cover);
    return cover != nullptr && cover->ambient != 0 ? cover : nullptr;
}
// The gradient's four stops in a game's colour: deep to light, as the time
// of day's run; without its wash, the time of day's own.
void game_stops(Context &ctx, const Game *game, const Color time[4], Color out[4])
{
    const Artwork *wash = wash_of(ctx, game);
    if (wash == nullptr)
    {
        for (int i = 0; i < 4; ++i)
            out[i] = time[i];
        return;
    }
    const Color tint = Color::rgb(wash->ambient_rgb);
    const Color black = Color::rgb(0x000000);
    out[0] = hui::gfx::mix(black, tint, 0.2f);
    out[1] = hui::gfx::mix(black, tint, 0.42f);
    out[2] = hui::gfx::mix(black, tint, 0.68f);
    out[3] = hui::gfx::mix(black, tint, 0.9f);
}
} // namespace

void art_wash(Context &ctx, hui::gfx::DrawList &out, const Game *now, const Game *before, float in)
{
    const ui::Rect screen = ui::Rect{0.0f, 0.0f, 1920.0f, 1080.0f}.inset(-230.0f);
    const auto lay = [&](const Game *game, float amount)
    {
        const Artwork *wash = wash_of(ctx, game);
        if (wash != nullptr && amount > 0.01f)
            ui::image_cover(out, wash->ambient, 48, 48, screen, Color::rgb(0xffffff, 0.34f * amount));
    };
    if (in < 1.0f)
        lay(before, 1.0f - in);
    lay(now, in);
}

void backdrop(Context &ctx, hui::gfx::BackdropSpec &spec, const Game *now, const Game *before, float in)
{
    Color time[4], from[4], to[4], stops[4];
    look::ribbon_colors(time);
    game_stops(ctx, before, time, from);
    game_stops(ctx, now, time, to);
    for (int i = 0; i < 4; ++i)
        stops[i] = hui::gfx::mix(from[i], to[i], std::clamp(in, 0.0f, 1.0f));
    // The shader lightens under each ribbon toward its colour and draws a
    // thin glowing line on it: colours near the gradient keep that subtle.
    spec.mode = hui::gfx::BackdropMode::waves;
    spec.colors[0] = hui::gfx::mix(stops[0], stops[1], 0.6f);
    spec.colors[1] = hui::gfx::mix(stops[2], stops[3], 0.45f);
    spec.colors[2] = hui::gfx::mix(stops[2], Color::rgb(0xffffff), 0.28f);
    spec.colors[3] = hui::gfx::mix(stops[3], Color::rgb(0xffffff), 0.22f);
    spec.params[0] = 0.0f;
    spec.params[1] = 0.0f;
    spec.params[2] = 0.0f;
    // Reduced motion keeps the ribbons still.
    spec.time = look::prefs().reduced_motion ? 0.0f : ctx.clock;
}

} // namespace orbit::xb
