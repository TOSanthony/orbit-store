// Orbit Store TV app - Panorama's controls, drawn to the design's measurements.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/pano.hpp"

#include "orbit/i18n.hpp"
#include "orbit/look.hpp"
#include "orbit/screens.hpp"
#include "orbit/xb.hpp"

#include <algorithm>
#include <cmath>

namespace orbit::pano
{

using ui::Color;

namespace
{

constexpr Color kClear{0, 0, 0, 0};

int lines(Context &ctx, Face face, std::string_view text, float size, float width, int most)
{
    if (text.empty())
        return 0;
    return std::min(most, static_cast<int>(font(ctx, face, text).font->wrap(text, size, width).size()));
}

// A square outline drawn inside r (the design's inset box-shadows).
void outline(hui::gfx::DrawList &list, const ui::Rect &r, float width, Color ink)
{
    list.bordered_rect(r, 0.0f, kClear, width, ink);
}

} // namespace

// ---------------------------------------------------------------- type

const hui::ui::FontRef &font(Context &ctx, Face face, std::string_view text)
{
    const ui::Type &type = ctx.type;
    switch (face)
    {
    case Face::semibold:
        return type.semibold;
    case Face::light:
        return ui::face(type.tight_light, type.light, text);
    case Face::xlight:
        return ui::face(type.xlight, type.light, text);
    case Face::medium:
        return ui::face(type.tight_medium, type.medium, text);
    default:
        return type.regular;
    }
}

float baseline(float top, float size, float line)
{
    return top + size * line * 0.5f + size * 0.364f;
}

float draw_text(Context &ctx, hui::gfx::DrawList *list, float x, float top, float width, std::string_view text, float size,
                Color ink, Face face, float line, int max_lines)
{
    const int n = lines(ctx, face, text, size, width, max_lines);
    if (n > 0 && list != nullptr)
        ui::paragraph(*list, font(ctx, face, text), text, x, baseline(top, size, line), size, width, size * line, ink, max_lines);
    return size * line * static_cast<float>(n);
}

float caps_width(Context &ctx, std::string_view text, float size)
{
    const std::string up = hui::ui::upper(text);
    std::size_t letters = 0;
    for (unsigned char c : up)
        if ((c & 0xc0) != 0x80)
            ++letters;
    return ctx.type.semibold.measure(up, size) + size * 0.14f * static_cast<float>(letters > 0 ? letters - 1 : 0);
}

std::vector<std::string> wrap_caps(Context &ctx, std::string_view text, float size, float width, int most)
{
    // Word by word, measuring with the spacing.
    std::vector<std::string> out;
    std::string line;
    std::size_t at = 0;
    while (at < text.size())
    {
        std::size_t end = text.find(' ', at);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view word = text.substr(at, end - at);
        const std::string next = line.empty() ? std::string(word) : line + " " + std::string(word);
        if (!line.empty() && caps_width(ctx, next, size) > width)
        {
            out.push_back(line);
            line = std::string(word);
        }
        else
            line = next;
        at = end + 1;
    }
    if (!line.empty())
        out.push_back(line);
    if (static_cast<int>(out.size()) > most)
    {
        out.resize(static_cast<std::size_t>(most));
        std::string &last = out.back();
        while (!last.empty() && caps_width(ctx, last + " \xE2\x80\xA6", size) > width)
        {
            const std::size_t cut = last.find_last_of(' ');
            last.resize(cut == std::string::npos ? 0 : cut);
        }
        last += " \xE2\x80\xA6";
    }
    return out;
}

float draw_caps(Context &ctx, hui::gfx::DrawList *list, float x, float top, std::string_view text, float size, Color ink)
{
    if (list != nullptr && !text.empty())
        hui::ui::text(*list, ctx.type.semibold, hui::ui::upper(text), x, baseline(top, size), size, ink, ui::Align::left,
                      size * 0.14f);
    return text.empty() ? 0.0f : size * 1.6f;
}

// ---------------------------------------------------------------- buttons and switches

Button button(std::string label, bool focus)
{
    Button b;
    b.label = std::move(label);
    b.focus = focus;
    return b;
}

Button button(std::string label, icons::Icon icon, bool focus)
{
    Button b = button(std::move(label), focus);
    b.icon = icon;
    b.has_icon = true;
    return b;
}

float button_height(const Button &b)
{
    return b.small ? 52.0f : 64.0f;
}

float button_width(Context &ctx, const Button &b)
{
    // .pn-btn: 32 px each side (22 small), the icon and a 12 px gap, the label.
    const float h = button_height(b);
    if (b.label.empty())
        return h;
    const float size = b.small ? 19.0f : 23.0f;
    return font(ctx, Face::medium, b.label).measure(b.label, size) + 2.0f * (b.small ? 22.0f : 32.0f) +
           (b.has_icon ? (b.small ? 20.0f : 24.0f) + 12.0f : 0.0f);
}

void draw_button(Context &ctx, hui::gfx::DrawList &list, float x, float y, const Button &b)
{
    const float h = button_height(b);
    const float w = button_width(ctx, b);
    const ui::Rect r{x, y, w, h};
    list.push_opacity(b.enabled ? 1.0f : 0.4f);
    Color ink = kInk;
    if (b.focus)
    {
        list.rounded_rect(r, 0.0f, kInk);
        ink = Color::rgb(0x000000);
    }
    else if (b.primary)
        list.rounded_rect(r, 0.0f, look::accent_color());
    else
        outline(list, r, 3.0f, kInk);
    const float size = b.small ? 19.0f : 23.0f;
    const float icon = b.small ? 20.0f : 24.0f;
    if (b.label.empty())
        icons::draw(list, b.icon, r.cx(), r.cy(), icon, ink, 2.0f);
    else
    {
        float tx = x + (b.small ? 22.0f : 32.0f);
        if (b.has_icon)
        {
            icons::draw(list, b.icon, tx + icon * 0.5f, r.cy(), icon, ink, 2.0f);
            tx += icon + 12.0f;
        }
        ui::text(list, font(ctx, Face::medium, b.label), b.label, tx, r.cy() + size * 0.364f, size, ink);
    }
    list.pop_opacity();
}

float draw_buttons(Context &ctx, hui::gfx::DrawList &list, float x, float y, const std::vector<Button> &buttons)
{
    float bx = x;
    for (const Button &b : buttons)
    {
        draw_button(ctx, list, bx, y, b);
        bx += button_width(ctx, b) + 14.0f;
    }
    return buttons.empty() ? 0.0f : bx - 14.0f - x;
}

float toggle_width(Context &ctx, bool on)
{
    const char *word = on ? tr("On") : tr("Off");
    return ctx.type.regular.measure(word, 20.0f) + 12.0f + 66.0f;
}

void draw_toggle(Context &ctx, hui::gfx::DrawList &list, float right, float cy, bool on)
{
    // .pn-sw: a 66 x 30 outline; on, an accent bar and the white thumb at the right.
    const char *word = on ? tr("On") : tr("Off");
    const ui::Rect sw{right - 66.0f, cy - 15.0f, 66.0f, 30.0f};
    ui::text(list, ctx.type.regular, word, sw.x - 12.0f, cy + 20.0f * 0.364f, 20.0f, kDim, ui::Align::right);
    outline(list, sw, 2.0f, kInk);
    if (on)
    {
        list.rounded_rect({sw.x + 5.0f, sw.y + 5.0f, 66.0f - 5.0f - 20.0f, 20.0f}, 0.0f, look::accent_color());
        list.rounded_rect({sw.x + 66.0f - 15.0f, sw.y, 15.0f, 30.0f}, 0.0f, kInk);
    }
    else
        list.rounded_rect({sw.x, sw.y, 15.0f, 30.0f}, 0.0f, kInk);
}

void draw_box(hui::gfx::DrawList &list, float x, float cy, bool on)
{
    const ui::Rect box{x, cy - 16.0f, 32.0f, 32.0f};
    if (on)
    {
        list.rounded_rect(box, 0.0f, kInk);
        icons::draw(list, icons::Icon::check, box.cx(), box.cy(), 22.0f, Color::rgb(0x000000), 3.0f);
    }
    else
        outline(list, box, 2.0f, kInk);
}

// ---------------------------------------------------------------- lists

namespace
{

// .pn-li: 13 px above and below, 24 px in from the left, 18 px from the right.
struct RowLayout
{
    float text_x = 0.0f;
    float text_w = 0.0f;
    float control = 0.0f;
    int sub_lines = 0;
    float text_h = 0.0f;
    float h = 0.0f;
};

float control_width(Context &ctx, const Item &item)
{
    if (item.toggle >= 0)
        return toggle_width(ctx, item.toggle > 0);
    if (item.check >= 0)
        return 32.0f;
    if (!item.button.empty())
    {
        Button b = button(item.button);
        b.small = true;
        return button_width(ctx, b);
    }
    return 0.0f;
}

float control_height(const Item &item)
{
    if (item.toggle >= 0)
        return 30.0f;
    if (item.check >= 0)
        return 32.0f;
    return item.button.empty() ? 0.0f : 52.0f;
}

RowLayout layout(Context &ctx, float width, const Item &item, float size)
{
    RowLayout l;
    const float ts = item.size > 0.0f ? item.size : size;
    l.control = control_width(ctx, item);
    l.text_x = 24.0f + (item.has_icon ? 30.0f + 22.0f : 0.0f);
    l.text_w = width - 18.0f - (l.control > 0.0f ? l.control + 22.0f : 0.0f) - l.text_x;
    l.sub_lines = lines(ctx, Face::body, item.sub, 18.0f, l.text_w, 6);
    l.text_h = ts * 1.15f * static_cast<float>(std::max(1, lines(ctx, Face::light, item.title, ts, l.text_w, 2))) +
               (item.value.empty() ? 0.0f : 4.0f + 21.0f * 1.6f) +
               (l.sub_lines > 0 ? 5.0f + 18.0f * 1.45f * static_cast<float>(l.sub_lines) : 0.0f);
    l.h = 26.0f + std::max(l.text_h, control_height(item));
    return l;
}

} // namespace

float list_offset(Context &ctx, float width, const std::vector<Item> &items, int index, float size)
{
    float y = 0.0f;
    for (int i = 0; i < index && i < static_cast<int>(items.size()); ++i)
        y += layout(ctx, width, items[static_cast<std::size_t>(i)], size).h;
    return y;
}

float draw_list(Context &ctx, hui::gfx::DrawList *list, float x, float y, float width, const std::vector<Item> &items, int focus,
                float size)
{
    const Color accent = look::accent_color();
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
    {
        const Item &item = items[static_cast<std::size_t>(i)];
        const RowLayout l = layout(ctx, width, item, size);
        const float ts = item.size > 0.0f ? item.size : size;
        if (list != nullptr && y < 1080.0f && y + l.h > 0.0f)
        {
            const bool on = i == focus;
            const ui::Rect r{x, y, width, l.h};
            if (on)
            {
                list->rounded_rect(r, 0.0f, Color::rgb(0xffffff, 0.08f));
                list->rounded_rect({r.x, r.y, 6.0f, r.h}, 0.0f, accent);
            }
            list->push_opacity(item.off ? 0.42f : 1.0f);
            const float top = y + (l.h - l.text_h) * 0.5f;
            if (item.has_icon)
                icons::draw(*list, item.icon, x + 24.0f + 15.0f, y + l.h * 0.5f, 30.0f,
                            item.warn ? kAmber : Color::rgb(0xffffff, 0.92f), 1.6f);
            const float tx = x + l.text_x;
            float ty = top;
            const int title_lines = std::max(1, lines(ctx, Face::light, item.title, ts, l.text_w, 2));
            ui::paragraph(*list, font(ctx, Face::light, item.title), item.title, tx, baseline(ty, ts, 1.15f), ts, l.text_w,
                          ts * 1.15f, item.warn ? kAmber : kInk, 2);
            ty += ts * 1.15f * static_cast<float>(title_lines);
            if (!item.value.empty())
            {
                ty += 4.0f;
                const float vw = ui::text_fit(*list, ctx.type.regular, item.value, tx, baseline(ty, 21.0f), 21.0f,
                                              l.text_w - (item.dot ? 19.0f : 0.0f), item.warn ? kAmber : accent);
                if (item.dot)
                    list->circle(tx + vw + 10.0f + 4.5f, baseline(ty, 21.0f) - 21.0f * 0.364f, 4.5f, accent);
                ty += 21.0f * 1.6f;
            }
            if (l.sub_lines > 0)
            {
                ty += 5.0f;
                ui::paragraph(*list, ctx.type.regular, item.sub, tx, baseline(ty, 18.0f, 1.45f), 18.0f, l.text_w, 18.0f * 1.45f,
                              kDim, l.sub_lines);
            }
            const float right = x + width - 18.0f;
            const float cy = y + l.h * 0.5f;
            if (item.toggle >= 0)
                draw_toggle(ctx, *list, right, cy, item.toggle > 0);
            else if (item.check >= 0)
                draw_box(*list, right - 32.0f, cy, item.check > 0);
            else if (!item.button.empty())
            {
                Button b = button(item.button, on);
                b.small = true;
                draw_button(ctx, *list, right - button_width(ctx, b), cy - 26.0f, b);
            }
            list->pop_opacity();
        }
        y += l.h;
    }
    return y;
}

void draw_pivots(Context &ctx, hui::gfx::DrawList &list, float x, float y, const std::vector<std::string> &items, int current,
                 float size, float shift)
{
    // Starting from the current one, the rest waiting to the right.
    float px = x - shift;
    const int n = static_cast<int>(items.size());
    for (int k = 0; k < n && px < 1920.0f; ++k)
    {
        const std::string &item = items[static_cast<std::size_t>((current + k) % n)];
        const auto &face = font(ctx, Face::light, item);
        px += ui::text(list, face, item, px, baseline(y, size), size, k == 0 ? kInk : Color::rgb(0xffffff, 0.36f)) + 40.0f;
    }
}

// ---------------------------------------------------------------- the picker and the app bar

void draw_picker(Context &ctx, hui::gfx::DrawList &list, const Picker &p, float in)
{
    if (in <= 0.01f)
        return;
    list.push_opacity(in);
    const float left = 100.0f;
    const float width = p.narrow ? 600.0f : 1720.0f;
    if (p.narrow)
        list.rounded_rect({0.0f, 0.0f, 760.0f, 1080.0f}, 0.0f, kPage.with_alpha(0.94f));
    else
        list.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, kPage);
    float y = 64.0f;
    y += draw_caps(ctx, &list, left, y, p.head, 20.0f, kInk);
    if (!p.sub.empty())
    {
        y += 10.0f;
        y += draw_text(ctx, &list, left, y, std::min(1200.0f, width), p.sub, 21.0f, kDim);
    }
    if (p.field)
    {
        // .pn-field: 900 x 60, a light underline, a white ring while typing.
        y += 10.0f;
        const ui::Rect f{left, y, 900.0f, 60.0f};
        list.rounded_rect(f, 0.0f, Color::rgb(0xffffff, 0.1f));
        if (p.field_focus)
            outline(list, f, 3.0f, kInk);
        else
            list.rounded_rect({f.x, f.y + f.h - 2.0f, f.w, 2.0f}, 0.0f, Color::rgb(0xffffff, 0.4f));
        const bool empty = p.field_value.empty();
        ui::text_fit(list, ctx.type.regular, empty ? p.field_placeholder : p.field_value, f.x + 18.0f, f.cy() + 22.0f * 0.364f,
                     22.0f, f.w - 36.0f, empty ? kFaint : kInk);
        y += 60.0f;
    }
    if (!p.note_above.empty())
    {
        y += 26.0f;
        y += draw_caps(ctx, &list, left, y, p.note_above);
        y -= 20.0f;
    }
    y += 34.0f;
    // Each item: 10 px above and below, 24 px in; keep the focused one on screen.
    const auto item_h = [&](const Item &it)
    {
        const float ts = it.size > 0.0f ? it.size : p.size;
        return 20.0f + ts * 1.15f + (it.sub.empty() ? 0.0f : 3.0f + 19.0f * 1.6f) + (it.bar >= 0.0f ? 10.0f + 5.0f : 0.0f);
    };
    float focus_bottom = y;
    for (int i = 0; i <= p.focus && i < static_cast<int>(p.items.size()); ++i)
        focus_bottom += item_h(p.items[static_cast<std::size_t>(i)]);
    const float shift = std::max(0.0f, focus_bottom - (1080.0f - 110.0f));
    list.push_clip({0.0f, y - 4.0f, p.narrow ? 760.0f : 1920.0f, 1080.0f - y + 4.0f});
    float iy = y - shift;
    const Color accent = look::accent_color();
    for (int i = 0; i < static_cast<int>(p.items.size()); ++i)
    {
        const Item &it = p.items[static_cast<std::size_t>(i)];
        const float h = item_h(it);
        const float ts = it.size > 0.0f ? it.size : p.size;
        if (iy + h > 0.0f && iy < 1080.0f)
        {
            const ui::Rect r{left, iy, width, h};
            if (i == p.focus)
            {
                list.rounded_rect(r, 0.0f, Color::rgb(0xffffff, 0.08f));
                list.rounded_rect({r.x, r.y, 6.0f, r.h}, 0.0f, accent);
            }
            list.push_opacity(it.off ? 0.4f : 1.0f);
            float tx = left + 24.0f;
            float ty = iy + 10.0f;
            if (it.has_swatch)
            {
                // A 44 px square of the colour, 22 px before the name.
                list.rounded_rect({tx, ty + ts * 1.15f * 0.5f - 22.0f + 3.0f, 44.0f, 44.0f}, 0.0f, it.swatch);
                tx += 44.0f + 22.0f;
            }
            const float tw = left + width - 24.0f - tx - (it.chosen ? 34.0f + 20.0f : 0.0f);
            ui::text_fit(list, font(ctx, Face::light, it.title), it.title, tx, baseline(ty, ts, 1.15f), ts, tw,
                         it.chosen ? accent : kInk);
            ty += ts * 1.15f;
            if (!it.sub.empty())
            {
                ty += 3.0f;
                ui::text_fit(list, ctx.type.regular, it.sub, left + 24.0f, baseline(ty, 19.0f), 19.0f, tw, kDim);
                ty += 19.0f * 1.6f;
            }
            if (it.bar >= 0.0f)
            {
                ty += 10.0f;
                list.rounded_rect({left + 24.0f, ty, 560.0f, 5.0f}, 0.0f, Color::rgb(0xffffff, 0.18f));
                list.rounded_rect({left + 24.0f, ty, 560.0f * std::clamp(it.bar, 0.0f, 1.0f), 5.0f}, 0.0f,
                                  it.bar_warn ? kAmber : accent);
            }
            if (it.chosen)
                icons::draw(list, icons::Icon::check, left + width - 24.0f - 17.0f, r.cy(), 34.0f, accent, 2.4f);
            list.pop_opacity();
        }
        iy += h;
    }
    list.pop_clip();
    if (!p.note.empty() && iy + 26.0f < 1000.0f)
        draw_text(ctx, &list, left, iy + 26.0f, std::min(1300.0f, p.narrow ? 560.0f : width), p.note, 19.0f, kFaint);
    list.pop_opacity();
}

int update_appbar(AppBar &bar, const hui::InputFrame &input, hui::ui::Feedback &feedback)
{
    const int items = static_cast<int>(bar.items.size());
    const int count = bar.count();
    bar.focus = std::clamp(bar.focus, 0, std::max(0, count - 1));
    if (input.is_pressed(hui::Action::back) || input.is_pressed(hui::Action::menu))
    {
        feedback.play(hui::audio::Cue::modal_close);
        return -2;
    }
    const bool on_shortcuts = bar.focus >= items;
    int next = bar.focus;
    if (input.nav == hui::Direction::up)
        next = on_shortcuts ? items - 1 : bar.focus - 1;
    else if (input.nav == hui::Direction::down)
        next = on_shortcuts ? -1 : bar.focus + 1;
    else if (on_shortcuts && (input.nav == hui::Direction::left || input.nav == hui::Direction::right))
    {
        next = bar.focus + (input.nav == hui::Direction::right ? 1 : -1);
        if (next < items)
            next = -1;
    }
    if (next != bar.focus && input.nav != hui::Direction::none)
    {
        // Disabled actions are passed over.
        const int step = next > bar.focus ? 1 : -1;
        while (next >= 0 && next < items && bar.items[static_cast<std::size_t>(next)].off)
            next += step;
        if (next >= 0 && next < count)
        {
            bar.focus = next;
            feedback.play(hui::audio::Cue::focus);
        }
        else
            refuse(feedback, input);
    }
    if (input.is_pressed(hui::Action::confirm) && count > 0)
    {
        const int action = bar.action();
        if (action < 0 || action >= items || bar.items[static_cast<std::size_t>(action)].off)
        {
            refuse(feedback, input);
            return -1;
        }
        feedback.play(hui::audio::Cue::select);
        return action;
    }
    return -1;
}

void draw_appbar(Context &ctx, hui::gfx::DrawList &list, const AppBar &bar, float in)
{
    if (in <= 0.01f)
        return;
    list.push_opacity(in);
    list.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, Color::rgb(0x000000, 0.55f));
    // From the bottom: 34 px, the shortcuts, the actions (7 px above and below
    // each, 38 px light lowercase), the heading, 26 px.
    const int items = static_cast<int>(bar.items.size());
    float h = 26.0f + (bar.head.empty() ? 0.0f : 16.0f * 1.6f + 12.0f) + static_cast<float>(items) * (14.0f + 38.0f * 1.6f) +
              (bar.shortcuts.empty() ? 0.0f : 18.0f + 60.0f + 8.0f + 15.0f * 1.6f) + 34.0f;
    const float slide = (1.0f - in) * 80.0f;
    const float top = 1080.0f - h + slide;
    list.rounded_rect({0.0f, top, 1920.0f, h}, 0.0f, kBand);
    float y = top + 26.0f;
    if (!bar.head.empty())
        y += draw_caps(ctx, &list, 100.0f, y, bar.head) + 12.0f;
    const Color accent = look::accent_color();
    for (int i = 0; i < items; ++i)
    {
        const Item &it = bar.items[static_cast<std::size_t>(i)];
        const float rh = 14.0f + 38.0f * 1.6f;
        if (i == bar.focus)
        {
            list.rounded_rect({100.0f, y, 1720.0f, rh}, 0.0f, Color::rgb(0xffffff, 0.08f));
            list.rounded_rect({100.0f, y, 6.0f, rh}, 0.0f, accent);
        }
        list.push_opacity(it.off ? 0.4f : 1.0f);
        const std::string word = lower(it.title);
        const float w = ui::text(list, font(ctx, Face::light, word), word, 124.0f, baseline(y + 7.0f, 38.0f), 38.0f,
                                 it.danger ? Color::rgb(0xff6b57) : kInk);
        if (!it.sub.empty())
            ui::text_fit(list, ctx.type.regular, it.sub, 124.0f + w + 20.0f, y + rh * 0.5f + 19.0f * 0.364f, 19.0f,
                         1720.0f - 48.0f - w - 20.0f, kDim);
        list.pop_opacity();
        y += rh;
    }
    if (!bar.shortcuts.empty())
    {
        // Round buttons, 60 px, 46 px apart, centred, each named under it.
        y += 18.0f;
        std::vector<float> widths;
        float total = 0.0f;
        for (const AppBar::Shortcut &s : bar.shortcuts)
        {
            widths.push_back(std::max(60.0f, ctx.type.regular.measure(s.label, 15.0f)));
            total += widths.back() + 46.0f;
        }
        float x = 960.0f - (total - 46.0f) * 0.5f;
        for (std::size_t k = 0; k < bar.shortcuts.size(); ++k)
        {
            const AppBar::Shortcut &s = bar.shortcuts[k];
            const float cx = x + widths[k] * 0.5f;
            const bool on = bar.focus == items + static_cast<int>(k);
            const bool off = s.item >= 0 && s.item < items && bar.items[static_cast<std::size_t>(s.item)].off;
            list.push_opacity(off ? 0.4f : 1.0f);
            if (on)
                list.circle(cx, y + 30.0f, 30.0f, kInk);
            else
                list.ring(cx, y + 30.0f, 29.0f, 2.0f, kInk);
            icons::draw(list, s.icon, cx, y + 30.0f, 26.0f, on ? Color::rgb(0x000000) : kInk, 2.0f);
            ui::text(list, ctx.type.regular, s.label, cx, baseline(y + 68.0f, 15.0f), 15.0f, kInk, ui::Align::center);
            list.pop_opacity();
            x += widths[k] + 46.0f;
        }
    }
    list.pop_opacity();
}

// ---------------------------------------------------------------- notices, dots, bands

float draw_notice(Context &ctx, hui::gfx::DrawList &list, float y, icons::Icon icon, Color tone, std::string_view text,
                  std::string_view sub)
{
    // A strip across the top, at least 84 px, 12 px above and below, 100 px in.
    const ui::Type &type = ctx.type;
    // On amber the text is black.
    const bool dark = std::fabs(tone.r - kAmber.r) < 0.01f && std::fabs(tone.g - kAmber.g) < 0.01f &&
                      std::fabs(tone.b - kAmber.b) < 0.01f;
    const Color ink = dark ? Color::rgb(0x000000) : kInk;
    const float tx = 100.0f + 30.0f + 18.0f;
    const float tw = 1820.0f - tx;
    const float text_w = type.semibold.measure(text, 23.0f);
    const float sub_w = sub.empty() ? 0.0f : type.regular.measure(sub, 20.0f) + 14.0f;
    const bool one_line = text_w + sub_w <= tw;
    const float lh = 23.0f * 1.35f;
    const float content = one_line || sub.empty() ? lh : lh + 20.0f * 1.35f;
    const float h = std::max(84.0f, 24.0f + content);
    list.rounded_rect({0.0f, y, 1920.0f, h}, 0.0f, tone);
    icons::draw(list, icon, 100.0f + 15.0f, y + h * 0.5f, 30.0f, ink, 2.0f);
    const float top = y + (h - content) * 0.5f;
    ui::text_fit(list, type.semibold, text, tx, baseline(top, 23.0f, 1.35f), 23.0f, tw, ink);
    if (!sub.empty())
    {
        if (one_line)
            ui::text(list, type.regular, sub, tx + text_w + 14.0f, baseline(top, 23.0f, 1.35f), 20.0f, ink.with_alpha(0.86f));
        else
            ui::text_fit(list, type.regular, sub, tx, baseline(top + lh, 20.0f, 1.35f), 20.0f, tw, ink.with_alpha(0.86f));
    }
    return h;
}

float draw_dots(Context &ctx, hui::gfx::DrawList &list, float x, float cy, float size, Color tone)
{
    // The design's five dots fade up to the last; the brightest runs along them.
    static constexpr float kAlpha[] = {0.25f, 0.45f, 0.7f, 0.9f, 1.0f};
    const Color ink = tone.a > 0.0f ? tone : look::accent_color();
    const float gap = std::round(size * 1.4f);
    const int lead = look::prefs().reduced_motion ? 4 : static_cast<int>(std::fmod(ctx.clock * 6.0f, 5.0f));
    for (int i = 0; i < 5; ++i)
    {
        const float a = kAlpha[(i - lead + 4 + 5) % 5];
        list.circle(x + size * 0.5f + static_cast<float>(i) * (size + gap), cy, size * 0.5f, ink.with_alpha(a));
    }
    return 5.0f * size + 4.0f * gap;
}

void draw_band(Context &ctx, hui::gfx::DrawList &list, const Band &band, float in)
{
    if (in <= 0.01f)
        return;
    const ui::Type &type = ctx.type;
    const float width = 1300.0f;
    // Measure first: the band is centred on the screen.
    const int body_lines = lines(ctx, Face::body, band.body, 24.0f, width, 6);
    const int error_lines = lines(ctx, Face::body, band.error, 20.0f, width, 3);
    const float title_w = width - (band.busy ? 5.0f * 9.0f + 4.0f * 13.0f + 22.0f : 0.0f);
    const int title_lines = std::max(1, lines(ctx, Face::light, band.title, 48.0f, title_w, 2));
    float content = (band.kicker.empty() ? 0.0f : 16.0f * 1.6f + 12.0f) + 48.0f * 1.1f * static_cast<float>(title_lines) +
                    (body_lines > 0 ? 16.0f + 36.0f * static_cast<float>(body_lines) : 0.0f) +
                    (error_lines > 0 ? 14.0f + 30.0f * static_cast<float>(error_lines) : 0.0f) +
                    (band.options.empty() ? 0.0f : 24.0f + static_cast<float>(band.options.size()) * (23.0f * 1.6f + 12.0f) - 12.0f) +
                    (band.controls.empty() ? 0.0f : 30.0f + 52.0f * static_cast<float>((band.controls.size() + 1) / 2)) +
                    (band.tiles.empty() ? 0.0f : 22.0f + 16.0f * 1.6f + 12.0f + 160.0f) + (band.buttons.empty() ? 0.0f : 34.0f + 64.0f);
    const float h = 52.0f + content + 50.0f;
    list.push_opacity(in);
    list.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, Color::rgb(0x000000, 0.74f));
    const float top = std::round(540.0f - h * 0.5f);
    list.rounded_rect({0.0f, top, 1920.0f, h}, 0.0f, kBand);
    float y = top + 52.0f;
    if (!band.kicker.empty())
        y += draw_caps(ctx, &list, 100.0f, y, band.kicker) + 12.0f;
    float tx = 100.0f;
    if (band.busy)
        tx += draw_dots(ctx, list, tx, y + 48.0f * 1.1f * 0.5f, 9.0f) + 22.0f;
    ui::paragraph(list, font(ctx, Face::light, band.title), band.title, tx, baseline(y, 48.0f, 1.1f), 48.0f, title_w, 48.0f * 1.1f,
                  kInk, 2);
    y += 48.0f * 1.1f * static_cast<float>(title_lines);
    if (body_lines > 0)
    {
        y += 16.0f;
        ui::paragraph(list, type.regular, band.body, 100.0f, baseline(y, 24.0f, 1.5f), 24.0f, width, 36.0f,
                      Color::rgb(0xffffff, 0.86f), body_lines);
        y += 36.0f * static_cast<float>(body_lines);
    }
    if (error_lines > 0)
    {
        y += 14.0f;
        ui::paragraph(list, type.regular, band.error, 100.0f, baseline(y, 20.0f, 1.5f), 20.0f, width, 30.0f, kAmber, error_lines);
        y += 30.0f * static_cast<float>(error_lines);
    }
    if (!band.controls.empty())
    {
        // Controls: each button and what it does, two columns 70 px apart.
        ui::HintStyle style{0.0f, 23.0f, 34.0f, 16.0f, Color::rgb(0xffffff, 0.86f)};
        y += 30.0f + ui::controls_grid(&list, type, band.controls, 100.0f, y + 30.0f, width + 70.0f, 52.0f, style);
    }
    int at = 0;
    if (!band.options.empty())
    {
        // Check rows: the box and the line, 16 px apart; the focused one lit.
        y += 24.0f;
        for (const Item &o : band.options)
        {
            const float rh = 23.0f * 1.6f;
            const bool on = band.focus == at++;
            list.push_opacity(o.off ? 0.4f : 1.0f);
            if (on)
            {
                list.rounded_rect({88.0f, y - 6.0f, width + 24.0f, rh + 12.0f}, 0.0f, Color::rgb(0xffffff, 0.08f));
                list.rounded_rect({88.0f, y - 6.0f, 6.0f, rh + 12.0f}, 0.0f, look::accent_color());
            }
            draw_box(list, 100.0f, y + rh * 0.5f, o.check > 0);
            ui::text_fit(list, type.regular, o.title, 100.0f + 32.0f + 16.0f, baseline(y, 23.0f), 23.0f, width - 48.0f, kInk);
            list.pop_opacity();
            y += rh + 12.0f;
        }
        y -= 12.0f;
    }
    if (!band.tiles.empty())
    {
        y += 22.0f;
        y += draw_caps(ctx, &list, 100.0f, y, band.tiles_label) + 12.0f;
        float x = 100.0f;
        for (const Tile &t : band.tiles)
        {
            const bool on = band.focus == at++;
            draw_tile(ctx, list, t, {x, y, 404.0f, 160.0f}, on ? 1.0f : 0.0f);
            x += 404.0f + 12.0f;
        }
        y += 160.0f;
    }
    if (!band.buttons.empty())
    {
        y += 34.0f;
        std::vector<Button> buttons = band.buttons;
        for (Button &b : buttons)
            b.focus = band.focus == at++;
        draw_buttons(ctx, list, 100.0f, y, buttons);
    }
    list.pop_opacity();
}

// ---------------------------------------------------------------- whole screens

void draw_splash(Context &ctx, hui::gfx::DrawList &list, std::string_view title, std::string_view body, std::string_view small,
                 const std::vector<Button> &buttons, bool busy)
{
    list.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, kPage);
    // The mark (96 px) and "orbit" in 170 px extra-light, a line box of 170.
    float y = 230.0f;
    if (ctx.mark != 0)
        list.image(ctx.mark, {100.0f, y + 85.0f - 48.0f, 96.0f, 96.0f}, hui::gfx::kFullUv, kInk);
    hui::ui::text(list, font(ctx, Face::xlight, "orbit"), "orbit", 100.0f + 96.0f + 26.0f, baseline(y, 170.0f, 1.0f), 170.0f, kInk,
                  ui::Align::left, -0.05f * 170.0f);
    y += 170.0f;
    if (busy)
    {
        // The dots sit in a line of the page's 15 px text (24 px tall).
        y += 34.0f;
        draw_dots(ctx, list, 100.0f, y + 12.0f, 11.0f);
        y += 24.0f;
    }
    y += 42.0f;
    y += draw_text(ctx, &list, 100.0f, y, 1720.0f, title, 50.0f, kInk, Face::light, 1.6f, 2);
    if (!body.empty())
        y += 14.0f + draw_text(ctx, &list, 100.0f, y + 14.0f, 1300.0f, body, 24.0f, Color::rgb(0xffffff, 0.86f), Face::body, 1.5f, 4);
    if (!small.empty())
        y += 12.0f + draw_text(ctx, &list, 100.0f, y + 12.0f, 1300.0f, small, 18.0f, kFaint, Face::body, 1.5f, 5);
    if (!buttons.empty())
        draw_buttons(ctx, list, 100.0f, y + 36.0f, buttons);
}

void draw_empty(Context &ctx, hui::gfx::DrawList &list, std::string_view kicker, std::string_view title, std::string_view body,
                const std::vector<Button> &buttons, bool busy)
{
    float y = 330.0f;
    if (!kicker.empty())
        y += draw_caps(ctx, &list, 100.0f, y, kicker);
    y += 14.0f;
    const auto &face = font(ctx, Face::xlight, title);
    const float size = ui::fit_size(face, title, 96.0f, 60.0f, 1720.0f);
    hui::ui::text(list, face, title, 100.0f, baseline(y, size, 1.0f), size, kInk, ui::Align::left, -0.03f * size);
    y += 96.0f;
    if (!body.empty())
        y += 24.0f + draw_text(ctx, &list, 100.0f, y + 24.0f, 1100.0f, body, 26.0f, Color::rgb(0xffffff, 0.84f), Face::body, 1.5f, 5);
    if (busy)
    {
        y += 26.0f;
        draw_dots(ctx, list, 100.0f, y + 12.0f, 11.0f);
        y += 24.0f;
    }
    if (!buttons.empty())
        draw_buttons(ctx, list, 100.0f, y + 40.0f, buttons);
}

float draw_pairs(Context &ctx, hui::gfx::DrawList *list, float x, float top, float width,
                 const std::vector<std::pair<std::string, std::string>> &pairs, int columns)
{
    // A grid, 30 px between columns and 20 px between rows: a 13 px caps key, a 25 px light value.
    columns = std::max(1, columns);
    const float cw = (width - 30.0f * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    float y = top + 16.0f;
    for (std::size_t first = 0; first < pairs.size(); first += static_cast<std::size_t>(columns))
    {
        float row_h = 0.0f;
        for (std::size_t i = first; i < std::min(pairs.size(), first + static_cast<std::size_t>(columns)); ++i)
        {
            const float cx = x + static_cast<float>(i - first) * (cw + 30.0f);
            float h = draw_caps(ctx, list, cx, y, pairs[i].first, 13.0f, Color::rgb(0xffffff, 0.55f)) + 6.0f;
            h += draw_text(ctx, list, cx, y + h, cw, pairs[i].second, 25.0f, kInk, Face::light, 1.6f, 3);
            row_h = std::max(row_h, h);
        }
        y += row_h + 20.0f;
    }
    return pairs.empty() ? 0.0f : y - 20.0f - top;
}

float draw_steps(Context &ctx, hui::gfx::DrawList *list, float x, float top, float width, const std::vector<std::string> &steps)
{
    // A 44 px extra-light number in the accent, then the step in 21 px.
    float y = top + 16.0f;
    for (std::size_t i = 0; i < steps.size(); ++i)
    {
        std::string_view text = steps[i];
        if (text.size() > 3 && text[0] >= '1' && text[0] <= '9' && text[1] == '.' && text[2] == ' ')
            text.remove_prefix(3);
        const std::string number = i18n::count(static_cast<long>(i + 1));
        if (list != nullptr)
            ui::text(*list, font(ctx, Face::xlight, number), number, x, y + 44.0f * 0.9f * 0.5f + 44.0f * 0.364f, 44.0f,
                     look::accent_color());
        const float h = draw_text(ctx, list, x + 32.0f + 18.0f, y, width - 50.0f, text, 21.0f, kInk, Face::body, 1.5f, 4);
        y += std::max(h, 44.0f * 0.9f) + 16.0f;
    }
    return steps.empty() ? 0.0f : y - 16.0f - top;
}

void keyboard_look(hui::ui::KeyboardStyle &style)
{
    // .pn-key: flat #34363c caps 6 px apart, the focused one in the accent.
    xb::keyboard_look(style, true);
    style.gap = 6.0f;
    style.radius = 0.0f;
    style.label_size = 26.0f;
    style.wide_label_size = 22.0f;
    style.theme.surface_high = kKey;
    style.theme.secondary = kKey;
    style.theme.primary = look::accent_color();
    style.theme.on_primary = kInk;
    style.theme.text = kInk;
    style.binding_glyphs = false; // the hints name the buttons
}

void draw_keyboard(Context &ctx, orbit::Frame &frame, const hui::ui::Keyboard &keys, std::string_view label, std::string_view value,
                   std::string_view placeholder, bool secret)
{
    // The label at the top, the text as a 132 px extra-light title with an
    // accent caret; the keys on the band along the bottom.
    hui::gfx::DrawList &out = frame.overlay;
    draw_caps(ctx, &out, 100.0f, 64.0f, label);
    std::string shown;
    if (secret)
        for (unsigned char c : value)
            if ((c & 0xc0) != 0x80)
                shown += "\xE2\x80\xA2";
    const std::string_view text = secret ? std::string_view(shown) : value;
    const float top = 64.0f + 16.0f * 1.6f + 12.0f;
    const float base = baseline(top, 132.0f, 1.0f);
    const auto &face = font(ctx, Face::xlight, text.empty() ? placeholder : text);
    float w = 0.0f;
    if (text.empty())
        w = hui::ui::text(out, face, placeholder, 100.0f, base, 132.0f, Color::rgb(0xffffff, 0.3f), ui::Align::left, -0.04f * 132.0f);
    else
    {
        const float size = ui::fit_size(face, text, 132.0f, 72.0f, 1640.0f);
        w = hui::ui::text(out, face, text, 100.0f, base, size, kInk, ui::Align::left, -0.04f * size);
    }
    // The caret: 5 x 110, 8 px after the text, its foot 8 px under the baseline.
    out.rounded_rect({100.0f + w + 8.0f, base + 8.0f - 110.0f, 5.0f, 110.0f}, 0.0f, look::accent_color());
    const ui::Rect k = keyboard_keys();
    out.rounded_rect({0.0f, k.y - 26.0f, 1920.0f, 1080.0f - (k.y - 26.0f)}, 0.0f, kBand);
    hui::ui::Canvas canvas{out, ctx.fonts, frame.glass_texture, ctx.clock};
    keys.draw(canvas);
}

ui::HintStyle hint_style(float bottom)
{
    ui::HintStyle style;
    style.y = 1080.0f - bottom - 15.0f;
    style.label = 19.0f;
    style.glyph = 30.0f;
    style.gap = 12.0f;
    style.ink = Color::rgb(0xffffff, 0.72f);
    return style;
}

} // namespace orbit::pano
