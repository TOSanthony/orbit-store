// Orbit Store TV app - Cinema's parts, drawn to the design's measurements.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/cn.hpp"

#include "orbit/i18n.hpp"
#include "orbit/screens.hpp"

#include <algorithm>
#include <cmath>

namespace orbit::cn
{

using ui::Color;

namespace
{

// The design's text: 1.6 line height unless set; a line's baseline sits
// 0.35 of its size below the middle of its line.
float baseline(float top, float size, float line = 1.6f)
{
    return top + size * line * 0.5f + size * 0.35f;
}
// Cinema's titles: Inter Tight SemiBold (Inter SemiBold for a letter it lacks).
const hui::ui::FontRef &title_face(const ui::Type &type, std::string_view text)
{
    return ui::face(type.cinema, type.semibold, text);
}
int lines(const hui::ui::FontRef &font, std::string_view text, float size, float width, int most)
{
    return text.empty() ? 0 : std::min(most, static_cast<int>(font.font->wrap(text, size, width).size()));
}
// Capitals spaced .26em, as the design's .caps.
float caps_tracking(float size)
{
    return size * 0.26f;
}
float caps_width(const hui::ui::FontRef &font, std::string_view text, float size)
{
    const std::string up = hui::ui::upper(text);
    std::size_t letters = 0;
    for (unsigned char c : up)
        if ((c & 0xc0) != 0x80)
            ++letters;
    return font.measure(up, size) + caps_tracking(size) * static_cast<float>(letters > 0 ? letters - 1 : 0);
}
void caps(hui::gfx::DrawList &out, const hui::ui::FontRef &font, std::string_view text, float x, float y, float size, Color ink,
          ui::Align align = ui::Align::left)
{
    float left = x;
    if (align != ui::Align::left)
    {
        const float w = caps_width(font, text, size);
        left = align == ui::Align::center ? x - w * 0.5f : x - w;
    }
    hui::ui::text(out, font, hui::ui::upper(text), left, y, size, ink, ui::Align::left, caps_tracking(size));
}

} // namespace

// ---------------------------------------------------------------- the art

void backdrop(Context &ctx, hui::gfx::DrawList &out, const Game *game, Veil veil, float opacity)
{
    out.push_opacity(opacity);
    const ui::Rect screen{0.0f, 0.0f, 1920.0f, 1080.0f};
    if (game == nullptr)
    {
        // radial-gradient(1100px 700px at 70% 30%, #2a2116, #0d0a07 55%, #000).
        out.rounded_rect(screen, 0.0f, Color::rgb(0x000000));
        out.glow({1344.0f - 500.0f, 324.0f - 300.0f, 1000.0f, 600.0f}, 300.0f, 360.0f, Color::rgb(0x2a2116, 0.9f));
        out.pop_opacity();
        return;
    }
    const Artwork *art = ctx.art.get(game->id, api::ArtKind::hero);
    if (art == nullptr)
        art = ctx.art.get(game->id, api::ArtKind::cover);
    if (art != nullptr)
        ui::image_cover(out, art->texture, art->width, art->height, screen, Color::rgb(0xffffff));
    else
        out.rounded_rect(screen, 0.0f, Color::rgb(0x0b0907));
    const Color clear = Color::rgb(0x000000, 0.0f);
    switch (veil)
    {
    case Veil::hero:
        // linear-gradient(0deg, .95 0%, .5 34%, 0 62%) and (90deg, .8 0%, .42 38%, 0 64%).
        out.gradient_rect({0.0f, 1080.0f * 0.38f, 1920.0f, 1080.0f * 0.28f}, 0.0f, clear, Color::rgb(0x000000, 0.5f));
        out.gradient_rect({0.0f, 1080.0f * 0.66f, 1920.0f, 1080.0f * 0.34f}, 0.0f, Color::rgb(0x000000, 0.5f),
                          Color::rgb(0x000000, 0.95f));
        out.gradient_rect_h({0.0f, 0.0f, 1920.0f * 0.38f, 1080.0f}, 0.0f, Color::rgb(0x000000, 0.8f), Color::rgb(0x000000, 0.42f));
        out.gradient_rect_h({1920.0f * 0.38f, 0.0f, 1920.0f * 0.26f, 1080.0f}, 0.0f, Color::rgb(0x000000, 0.42f), clear);
        break;
    case Veil::detail:
        out.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f * 0.4f}, 0.0f, Color::rgb(0x000000, 0.12f));
        out.gradient_rect({0.0f, 1080.0f * 0.4f, 1920.0f, 1080.0f * 0.6f}, 0.0f, Color::rgb(0x000000, 0.12f),
                          Color::rgb(0x000000, 0.6f));
        break;
    case Veil::dim:
        out.gradient_rect(screen, 0.0f, Color::rgb(0x000000, 0.6f), Color::rgb(0x000000, 0.92f));
        break;
    case Veil::dark:
        out.gradient_rect(screen, 0.0f, Color::rgb(0x000000, 0.82f), Color::rgb(0x000000, 0.97f));
        break;
    }
    out.pop_opacity();
}

// ---------------------------------------------------------------- the top

void chapters(Context &ctx, hui::gfx::DrawList &out, int current, bool update_dot, bool capsule, bool focused)
{
    // A row at y 52, 54 px tall: the wordmark (a 30 px mark and 34 px title face),
    // then the chapters in 17 px capitals, 40 px apart.
    const ui::Type &type = ctx.type;
    const float mid = 52.0f + 34.0f * 1.6f * 0.5f;
    if (ctx.mark != 0)
        out.image(ctx.mark, {100.0f, mid - 15.0f, 30.0f, 30.0f}, hui::gfx::kFullUv, kInk);
    float x = 142.0f + ui::text(out, title_face(type, "Orbit"), "Orbit", 142.0f, mid + 34.0f * 0.35f, 34.0f, kInk) + 14.0f + 40.0f;
    for (int s = section::discover; s < section::count; ++s)
    {
        const std::string label = tr(section_title(s));
        const float w = caps_width(type.semibold, label, 17.0f);
        const float top = mid - (17.0f * 1.6f + 8.0f) * 0.5f;
        if (focused && s == current)
            out.bordered_rect({x - 12.0f, top - 5.0f, w + 24.0f, 44.0f}, 8.0f, Color::rgb(0xf7f3ec, 0.08f), 2.0f, kInk);
        caps(out, type.semibold, label, x, baseline(top, 17.0f), 17.0f, s == current ? kInk : Color::rgb(0xf7f3ec, 0.5f));
        if (s == current)
            out.rounded_rect({x, top + 17.0f * 1.6f + 6.0f, w, 2.0f}, 0.0f, kGold);
        if (s == section::settings && update_dot)
            out.circle(x + w + 10.0f - 4.5f, top - 2.0f + 4.5f, 4.5f, kGold);
        x += w + 40.0f;
    }
    if (!capsule)
        return;
    // The capsule: what's downloading, with a ring of the overall progress.
    const Snapshot &state = ctx.store.state();
    int active = 0;
    double speed = 0.0, received = 0.0, total = 0.0;
    for (const Job &job : state.jobs)
        if (job.status == "downloading")
        {
            ++active;
            speed += job.speed;
            received += job.received;
            total += job.total;
        }
    if (active == 0)
        return;
    const std::string count = trn(active, "{count} downloading", "{count} downloading");
    const std::string rate = speed > 0.0 ? " \xC2\xB7 " + format_bytes(speed) + "/s" : std::string();
    const float cw = type.semibold.measure(count, 19.0f);
    const float rw = rate.empty() ? 0.0f : type.regular.measure(rate, 19.0f);
    const float w = 8.0f + 40.0f + 12.0f + cw + rw + 22.0f;
    const ui::Rect r{1820.0f - w, 38.0f, w, 54.0f};
    out.bordered_rect(r, 27.0f, Color::rgb(0x000000, 0.5f), 1.0f, Color::rgb(0xffffff, 0.14f));
    const float pct = total > 0.0 ? static_cast<float>(std::clamp(received / total, 0.0, 1.0)) : 0.0f;
    out.ring(r.x + 8.0f + 20.0f, r.cy(), 20.0f, 4.5f, Color::rgb(0xf7f3ec, 0.22f));
    if (pct > 0.0f)
        out.arc(r.x + 8.0f + 20.0f, r.cy(), 20.0f, 4.5f, 0.0f, pct * 6.2831853f, kInk);
    const float tx = r.x + 8.0f + 40.0f + 12.0f;
    ui::text(out, type.semibold, count, tx, r.cy() + 19.0f * 0.35f, 19.0f, kInk);
    if (!rate.empty())
        ui::text(out, type.regular, rate, tx + cw, r.cy() + 19.0f * 0.35f, 19.0f, kDim);
}

void collection(Context &ctx, hui::gfx::DrawList &out, std::string_view name, int index, int count, float y)
{
    // ‹ NAME › and a dot per collection, the current one long.
    const ui::Type &type = ctx.type;
    const float mid = y + 18.0f * 1.6f * 0.5f;
    float x = 100.0f;
    icons::draw(out, icons::Icon::back, x + 11.0f, mid, 22.0f, kDim, 2.0f);
    x += 22.0f + 18.0f;
    caps(out, type.semibold, name, x, mid + 18.0f * 0.35f, 18.0f, kInk);
    x += caps_width(type.semibold, name, 18.0f) + 18.0f;
    icons::draw(out, icons::Icon::next, x + 11.0f, mid, 22.0f, kDim, 2.0f);
    x += 22.0f + 18.0f + 8.0f;
    for (int i = 0; i < count; ++i)
    {
        const float w = i == index ? 26.0f : 8.0f;
        out.rounded_rect({x, mid - 4.0f, w, 8.0f}, 4.0f, i == index ? kInk : Color::rgb(0xf7f3ec, 0.35f));
        x += w + 8.0f;
    }
}

float kick(Context &ctx, hui::gfx::DrawList &out, float x, float y, std::string_view text, float size, Color ink)
{
    caps(out, ctx.type.semibold, text, x, baseline(y, size), size, ink);
    return size * 1.6f;
}

// ---------------------------------------------------------------- buttons

Button button(std::string label, icons::Icon icon, bool primary)
{
    Button b;
    b.label = std::move(label);
    b.icon = icon;
    b.has_icon = true;
    b.primary = primary;
    return b;
}

float button_width(Context &ctx, const Button &b)
{
    const float h = b.small ? 52.0f : 66.0f;
    if (b.label.empty())
        return h;
    const float size = b.small ? 19.0f : 23.0f;
    return ctx.type.semibold.measure(b.label, size) + 2.0f * (b.small ? 26.0f : 38.0f) +
           (b.has_icon ? (b.small ? 20.0f : 25.0f) + 12.0f : 0.0f);
}

void draw_button(Context &ctx, hui::gfx::DrawList &out, float x, float y, const Button &b)
{
    // .cn-btn: a pill; the primary light, the rest outlined; focus rings it.
    const float h = b.small ? 52.0f : 66.0f;
    const float w = button_width(ctx, b);
    const ui::Rect r{x, y, w, h};
    out.push_opacity(b.enabled ? 1.0f : 0.42f);
    const bool solid = b.primary || b.danger;
    if (b.focus)
    {
        if (solid)
        {
            out.glow(r, h * 0.5f, 44.0f, Color::rgb(0xf7f3ec, 0.32f));
            out.bordered_rect(r.inset(-4.0f), h * 0.5f + 4.0f, Color{0, 0, 0, 0}, 4.0f, Color::rgb(0xf7f3ec, 0.32f));
        }
        else
            out.bordered_rect(r.inset(-4.0f), h * 0.5f + 4.0f, Color{0, 0, 0, 0}, 4.0f, Color::rgb(0xf7f3ec, 0.28f));
    }
    if (b.danger)
        out.rounded_rect(r, h * 0.5f, Color::rgb(0xe8a08c));
    else if (b.primary)
        out.rounded_rect(r, h * 0.5f, kInk);
    else
        out.bordered_rect(r, h * 0.5f, Color{0, 0, 0, 0}, 2.0f, b.focus ? kInk : Color::rgb(0xf7f3ec, 0.42f));
    const Color ink = b.danger ? Color::rgb(0x2a0b05) : b.primary ? kDark : kInk;
    const float size = b.small ? 19.0f : 23.0f;
    const float icon = b.small ? 20.0f : 25.0f;
    if (b.label.empty())
        icons::draw(out, b.icon, r.cx(), r.cy(), icon, ink, 2.1f);
    else
    {
        float tx = x + (b.small ? 26.0f : 38.0f);
        if (b.has_icon)
        {
            icons::draw(out, b.icon, tx + icon * 0.5f, r.cy(), icon, ink, 2.1f);
            tx += icon + 12.0f;
        }
        ui::text(out, ctx.type.semibold, b.label, tx, r.cy() + size * 0.35f, size, ink);
    }
    out.pop_opacity();
}

float draw_buttons(Context &ctx, hui::gfx::DrawList &out, float x, float y, const std::vector<Button> &buttons, bool centred)
{
    float total = 0.0f;
    for (const Button &b : buttons)
        total += button_width(ctx, b) + 16.0f;
    total = buttons.empty() ? 0.0f : total - 16.0f;
    float bx = centred ? x - total * 0.5f : x;
    for (const Button &b : buttons)
    {
        draw_button(ctx, out, bx, y, b);
        bx += button_width(ctx, b) + 16.0f;
    }
    return total;
}

// ---------------------------------------------------------------- the stage

float draw_hero(Context &ctx, hui::gfx::DrawList &out, const Hero &hero)
{
    const ui::Type &type = ctx.type;
    // The title: a long one takes two lines rather than more than 45%
    // of the screen over the art (smaller only if two lines aren't enough).
    // The stage keeps its foot, so a second line moves the text up.
    const auto &font = title_face(type, hero.title);
    const float title_w = std::min(hero.width, 1920.0f * 0.45f);
    float size = hero.size;
    while (size > hero.size * 0.64f && lines(font, hero.title, size, title_w, 3) > 2)
        size -= 2.0f;
    const int n = lines(font, hero.title, size, title_w, 2);
    float y = hero.y - size * 0.98f * static_cast<float>(std::max(1, n) - 1);
    if (!hero.kicker.empty())
    {
        caps(out, type.semibold, hero.kicker, hero.x, baseline(y, 20.0f), 20.0f, hero.warn ? kWarn : kGold);
        y += 20.0f * 1.6f;
    }
    y += 16.0f;
    ui::paragraph(out, font, hero.title, hero.x, y + size * 0.8f, size, title_w, size * 0.98f, kInk, 2);
    y += size * 0.98f * static_cast<float>(std::max(1, n));
    if (!hero.tag.empty())
    {
        y += 20.0f;
        const int t = lines(type.regular, hero.tag, 30.0f, hero.width, 2);
        ui::paragraph(out, type.regular, hero.tag, hero.x, baseline(y, 30.0f, 1.4f), 30.0f, hero.width, 42.0f,
                      Color::rgb(0xf7f3ec, 0.9f), 2);
        y += 42.0f * static_cast<float>(t);
    }
    if (!hero.body.empty())
    {
        y += 12.0f;
        const float w = std::min(1000.0f, hero.width);
        const int t = lines(type.regular, hero.body, 23.0f, w, 4);
        ui::paragraph(out, type.regular, hero.body, hero.x, baseline(y, 23.0f, 1.55f), 23.0f, w, 23.0f * 1.55f, kDim, 4);
        y += 23.0f * 1.55f * static_cast<float>(t);
    }
    if (!hero.buttons.empty())
    {
        y += 34.0f;
        draw_buttons(ctx, out, hero.x, y, hero.buttons);
    }
    return y;
}

float stage_height(float size)
{
    return 20.0f * 1.6f + 10.0f + size * 1.04f + 14.0f + 30.0f * 1.4f;
}

float draw_stage(Context &ctx, hui::gfx::DrawList &out, const Stage &stage)
{
    const ui::Type &type = ctx.type;
    float y = stage.y;
    if (!stage.kicker.empty())
        caps(out, type.semibold, stage.kicker, 100.0f, baseline(y, 20.0f), 20.0f, kGold);
    y += 20.0f * 1.6f + 10.0f;
    // One line over the left 54% of the screen, as the browser version sets it.
    const auto &font = title_face(type, stage.title);
    const float width = 1920.0f * 0.54f, box = stage.size * 1.04f;
    float size = stage.size;
    while (size > stage.size * 0.5f && font.measure(stage.title, size) > width)
        size -= 2.0f;
    if (font.measure(stage.title, size) <= width)
        ui::text(out, font, stage.title, 100.0f, y + (box - size * 0.98f) * 0.5f + size * 0.8f, size, kInk);
    else
    {
        size = stage.size * 0.5f;
        ui::paragraph(out, font, stage.title, 100.0f, y + (box - size * 2.04f) * 0.5f + size * 0.8f, size, width,
                      size * 1.02f, kInk, 2);
    }
    y += box + 14.0f;
    if (!stage.tag.empty())
        ui::text_fit(out, type.regular, stage.tag, 100.0f, baseline(y, 30.0f, 1.4f), 30.0f, 1920.0f * 0.6f,
                     Color::rgb(0xf7f3ec, 0.9f));
    return y + 30.0f * 1.4f;
}

// ---------------------------------------------------------------- the strip

namespace
{
// A frame's picture (or, with none, its icon and title), its badge and its bar.
void draw_still(Context &ctx, hui::gfx::DrawList &out, const Still &f, const ui::Rect &r)
{
    const ui::Type &type = ctx.type;
    out.push_clip(r);
    if (f.game != nullptr)
    {
        // Keep one stable picture per tile. Full-screen heroes have a much
        // smaller cache and must not evict the pictures in scrolling rows.
        const Artwork *art = ctx.art.get(f.game->id, api::ArtKind::thumbnail);
        if (art != nullptr)
            ui::image_cover(out, art->texture, art->width, art->height, r, Color::rgb(0xffffff), 10.0f);
        else
            ui::cover_placeholder(out, type, r, f.game->title, 10.0f);
    }
    else
    {
        // No art: a dark card with its icon and title.
        out.gradient_rect(r, 10.0f, Color::rgb(0x1d1710), Color::rgb(0x0a0806));
        icons::draw(out, f.icon, r.x + 16.0f + 11.0f, r.y + 14.0f + 11.0f, 22.0f, kGold, 1.7f);
        ui::text_fit(out, title_face(type, f.title), f.title, r.x + 16.0f, r.y + 14.0f + 22.0f + 6.0f + 22.0f, 22.0f, r.w - 32.0f,
                     kInk);
        if (!f.sub.empty())
            ui::text_fit(out, type.regular, f.sub, r.x + 16.0f, r.y + 14.0f + 22.0f + 6.0f + 22.0f + 4.0f + 18.0f, 15.0f,
                         r.w - 32.0f, kDim);
    }
    if (f.game != nullptr && !f.game->addon_id.empty())
    {
        out.gradient_rect({r.x, r.y + r.h - 83.0f, r.w, 83.0f}, 0.0f,
                          Color::rgb(0x000000, 0.1f), Color::rgb(0x000000, 0.95f));
        ui::text_fit(out, type.medium, f.game->title, r.x + 12, r.y + r.h - 48, 17, r.w - 24, kInk);
        ui::text_fit(out, type.regular, f.game->base_title, r.x + 12, r.y + r.h - 29, 14, r.w - 24, kInk);
        if (!f.game->addon_type.empty())
            ui::text_fit(out, type.regular, addon_type_label(f.game->addon_type), r.x + 12, r.y + r.h - 11, 13, r.w - 24, kDim);
    }
    else if (!f.badge.empty())
    {
        // A small dark label, its capitals spaced .14em.
        const std::string label = hui::ui::upper(f.badge);
        const float tw = type.semibold.measure(label, 12.0f) + 12.0f * 0.14f * static_cast<float>(label.size() - 1);
        const ui::Rect b{r.x + 10.0f, r.y + r.h - 12.0f - 27.0f, tw + 16.0f, 27.0f};
        out.rounded_rect(b, 6.0f, Color::rgb(0x000000, 0.6f));
        hui::ui::text(out, type.semibold, label, b.x + 8.0f, b.cy() + 12.0f * 0.35f, 12.0f, kInk, ui::Align::left, 12.0f * 0.14f);
    }
    if (f.game != nullptr && f.game->addon_id.empty() && ctx.store.state().favourite(f.game->id))
    {
        out.circle(r.x + r.w - 25.0f, r.y + 25.0f, 15.0f, Color::rgb(0x141414, 0.87f));
        icons::draw(out, icons::Icon::heart_fill, r.x + r.w - 25.0f, r.y + 25.0f, 18.0f, kInk);
    }
    if (f.progress >= 0.0f)
    {
        out.rounded_rect({r.x, r.y + r.h - 5.0f, r.w, 5.0f}, 0.0f, Color::rgb(0x000000, 0.5f));
        out.rounded_rect({r.x, r.y + r.h - 5.0f, r.w * std::clamp(f.progress, 0.0f, 1.0f), 5.0f}, 0.0f, kGold);
    }
    out.pop_clip();
}
} // namespace

void draw_strip(Context &ctx, hui::gfx::DrawList &out, const std::vector<Still> &frames, int focused, const Strip &strip)
{
    const ui::Type &type = ctx.type;
    const float big_w = std::round(strip.frame_w * 1.18f), big_h = std::round(strip.frame_h * 1.18f);
    // Start late enough that the focused frame is on screen.
    int start = 0;
    const auto reach = [&]
    {
        float r = strip.x;
        for (int i = start; i <= focused; ++i)
            r += (i == focused ? big_w : strip.frame_w) + strip.gap;
        return r - strip.gap;
    };
    while (start < focused && reach() > 1820.0f)
        ++start;
    const float label_y = strip.y - strip.frame_h * 0.18f - 34.0f;
    if (!strip.label.empty())
        caps(out, type.semibold, strip.label, strip.x, baseline(label_y, 15.0f), 15.0f, kFaint);
    if (!strip.right.empty())
        caps(out, type.semibold, strip.right, 1820.0f, baseline(label_y, 15.0f), 15.0f, kFaint, ui::Align::right);
    float x = strip.x;
    for (int i = start; i < static_cast<int>(frames.size()) && x < 1920.0f; ++i)
    {
        const Still &f = frames[static_cast<std::size_t>(i)];
        const bool on = i == focused;
        const float w = on ? big_w : strip.frame_w, h = on ? big_h : strip.frame_h;
        const ui::Rect r{x, strip.y + strip.frame_h - h, w, h};
        out.push_opacity(f.dim ? 0.45f : on ? 1.0f : 0.78f);
        if (on && strip.focused)
        {
            out.shadow({r.x, r.y + 24.0f, r.w, r.h}, 10.0f, 48.0f, Color::rgb(0x000000, 0.6f));
            out.bordered_rect(r.inset(-3.0f), 13.0f, Color{0, 0, 0, 0}, 3.0f, kInk);
        }
        draw_still(ctx, out, f, r);
        out.pop_opacity();
        x += w + strip.gap;
    }
}

void draw_row_label(Context &ctx, hui::gfx::DrawList &out, std::string_view label, float y)
{
    caps(out, ctx.type.semibold, label, 100.0f, baseline(y, 15.0f), 15.0f, kFaint);
}

void draw_row_frames(Context &ctx, hui::gfx::DrawList &out, const std::vector<Still> &frames, int focused, float y)
{
    float x = 100.0f;
    for (int i = 0; i < static_cast<int>(frames.size()) && x < 1920.0f; ++i)
    {
        const Still &f = frames[static_cast<std::size_t>(i)];
        const bool on = i == focused;
        const ui::Rect r{x, y, kRowFrameW, kRowFrameH};
        out.push_opacity(f.dim ? 0.45f : on ? 1.0f : 0.74f);
        if (on)
        {
            out.shadow({r.x, r.y + 14.0f, r.w, r.h}, 10.0f, 32.0f, Color::rgb(0x000000, 0.55f));
            out.bordered_rect(r.inset(-3.0f), 13.0f, Color{0, 0, 0, 0}, 3.0f, kInk);
        }
        draw_still(ctx, out, f, r);
        out.pop_opacity();
        x += kRowFrameW + kRowGap;
    }
}

// ---------------------------------------------------------------- sheets, cards, rows

void glass(hui::gfx::DrawList &out, const ui::Rect &r, float radius)
{
    // .cn-glass: dark warm glass with a light top edge and a faint ring.
    out.rounded_rect(r, radius, Color::rgb(0x0e0c0a, 0.9f));
    out.bordered_rect(r, radius, Color{0, 0, 0, 0}, 1.0f, Color::rgb(0xffffff, 0.06f));
    out.rounded_rect({r.x + radius, r.y, r.w - 2.0f * radius, 1.0f}, 0.0f, Color::rgb(0xffffff, 0.12f));
}

float draw_sheet(hui::gfx::DrawList &out, float h)
{
    const float top = 1080.0f - h;
    glass(out, {0.0f, top, 1920.0f, h + 40.0f}, 36.0f);
    return top;
}

float draw_cards(Context &ctx, hui::gfx::DrawList *out, float x, float y, float width, const std::vector<Card> &cards,
                 int columns, float min_height)
{
    const ui::Type &type = ctx.type;
    columns = std::max(1, columns);
    const float cw = (width - 16.0f * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    const float inner = cw - 48.0f;
    const auto height = [&](const Card &c)
    {
        float h = 44.0f + (c.big ? 48.0f : 38.4f);
        if (!c.kicker.empty())
            h += 25.6f + 6.0f;
        if (!c.sub.empty())
            h += 6.0f + 28.8f * static_cast<float>(lines(type.regular, c.sub, 18.0f, inner, 3));
        if (c.bar >= 0.0f)
            h += 14.0f + 6.0f;
        if (!c.foot.empty())
            h += 10.0f + 24.0f;
        return std::max(min_height, h);
    };
    float top = y;
    for (std::size_t first = 0; first < cards.size(); first += static_cast<std::size_t>(columns))
    {
        float row_h = 0.0f;
        for (std::size_t i = first; i < std::min(cards.size(), first + static_cast<std::size_t>(columns)); ++i)
            row_h = std::max(row_h, height(cards[i]));
        for (std::size_t i = first; out != nullptr && i < std::min(cards.size(), first + static_cast<std::size_t>(columns)); ++i)
        {
            const Card &c = cards[i];
            const ui::Rect r{x + static_cast<float>(i - first) * (cw + 16.0f), top, cw, row_h};
            out->push_opacity(c.enabled ? 1.0f : 0.42f);
            if (c.focus)
            {
                out->bordered_rect(r.inset(-4.0f), 24.0f, Color{0, 0, 0, 0}, 4.0f, Color::rgb(0xf7f3ec, 0.3f));
                out->rounded_rect(r, 20.0f, Color::rgb(0xf7f3ec, 0.96f));
            }
            else
            {
                out->rounded_rect(r, 20.0f, Color::rgb(0xffffff, 0.06f));
                out->bordered_rect(r, 20.0f, Color{0, 0, 0, 0}, c.chosen ? 2.0f : 1.0f,
                                   c.chosen ? Color::rgb(0xf7f3ec, 0.75f) : Color::rgb(0xffffff, 0.1f));
            }
            const Color ink = c.focus ? kDark : kInk;
            const Color dim = c.focus ? Color::rgb(0x0b0907, 0.62f) : kDim;
            const Color faint = c.focus ? Color::rgb(0x0b0907, 0.62f) : kFaint;
            float cy = r.y + 22.0f;
            if (!c.kicker.empty())
            {
                ui::text_fit(*out, type.regular, c.kicker, r.x + 24.0f, baseline(cy, 16.0f), 16.0f, inner, dim);
                cy += 25.6f + 6.0f;
            }
            const float ts = c.big ? 30.0f : 24.0f;
            ui::text_fit(*out, type.semibold, c.title, r.x + 24.0f, baseline(cy, ts), ts, inner - (c.chosen ? 34.0f : 0.0f), ink);
            if (c.chosen)
                icons::draw(*out, icons::Icon::check, r.x + r.w - 24.0f - 12.0f, cy + ts * 0.8f, 24.0f, ink, 2.6f);
            cy += ts * 1.6f;
            if (!c.sub.empty())
            {
                cy += 6.0f;
                const int n = lines(type.regular, c.sub, 18.0f, inner, 3);
                ui::paragraph(*out, type.regular, c.sub, r.x + 24.0f, baseline(cy, 18.0f), 18.0f, inner, 28.8f, dim, 3);
                cy += 28.8f * static_cast<float>(n);
            }
            if (c.bar >= 0.0f)
            {
                cy += 14.0f;
                out->rounded_rect({r.x + 24.0f, cy, inner, 6.0f}, 3.0f, Color::rgb(0xf7f3ec, 0.18f));
                out->rounded_rect({r.x + 24.0f, cy, inner * std::clamp(c.bar, 0.0f, 1.0f), 6.0f}, 3.0f, c.focus ? kDarkGold : kGold);
                cy += 6.0f;
            }
            if (!c.foot.empty())
            {
                cy += 10.0f;
                ui::text_fit(*out, type.regular, c.foot, r.x + 24.0f, baseline(cy, 15.0f), 15.0f, inner, faint);
            }
            out->pop_opacity();
        }
        top += row_h + 16.0f;
    }
    return cards.empty() ? y : top - 16.0f;
}

float draw_rows(Context &ctx, hui::gfx::DrawList *out, float x, float y, float width, const std::vector<Row> &rows, int focused)
{
    const ui::Type &type = ctx.type;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
    {
        const Row &row = rows[static_cast<std::size_t>(i)];
        const bool on = i == focused;
        const float pad = row.sub.empty() ? 20.0f : 16.0f;
        // The control on the right, and the room it leaves the text.
        float control = 0.0f;
        if (row.toggle >= 0)
            control = 58.0f;
        else if (row.check >= 0)
            control = 32.0f;
        else if (!row.value.empty())
            control = std::min(420.0f, type.regular.measure(row.value, 22.0f)) + (row.dot ? 20.0f : 0.0f);
        else if (!row.button.empty())
        {
            Button b;
            b.label = row.button;
            b.small = true;
            control = button_width(ctx, b);
        }
        const float tx = x + 20.0f + (row.has_icon ? 26.0f + 20.0f : 0.0f);
        const float text_w = x + width - 20.0f - (control > 0.0f ? control + 20.0f : 0.0f) - tx;
        const int n = lines(type.regular, row.sub, 18.0f, text_w, 4);
        const float text_h = 40.0f + (n > 0 ? 4.0f + 26.1f * static_cast<float>(n) : 0.0f);
        const float h = pad * 2.0f + std::max(text_h, control > 0.0f ? 32.0f : 0.0f);
        if (out != nullptr)
        {
            out->push_opacity(row.off ? 0.45f : 1.0f);
            const ui::Rect r{x, y, width, h};
            if (on)
                out->rounded_rect(r, 16.0f, Color::rgb(0xf7f3ec, 0.95f));
            else
                out->rounded_rect({x, y + h - 1.0f, width, 1.0f}, 0.0f, Color::rgb(0xf7f3ec, 0.1f));
            const Color ink = on ? kDark : row.warn ? Color::rgb(0xe8c27a) : kInk;
            const Color dim = on ? Color::rgb(0x0b0907, 0.6f) : kDim;
            const float top = y + (h - text_h) * 0.5f;
            if (row.has_icon)
                icons::draw(*out, row.icon, x + 20.0f + 13.0f, top + 20.0f, 26.0f, on ? kDarkGold : row.warn ? kWarn : kGold, 1.7f);
            ui::text_fit(*out, on ? type.semibold : type.medium, row.title, tx, baseline(top, 25.0f), 25.0f, text_w, ink);
            if (n > 0)
                ui::paragraph(*out, type.regular, row.sub, tx, baseline(top + 44.0f, 18.0f, 1.45f), 18.0f, text_w, 26.1f, dim, 4);
            const float cx = x + width - 20.0f - control;
            const float cy = y + h * 0.5f;
            if (row.toggle >= 0)
            {
                const Color track = on ? (row.toggle > 0 ? kDarkGold : Color::rgb(0x0b0907, 0.2f))
                                       : (row.toggle > 0 ? kGold : Color::rgb(0xf7f3ec, 0.24f));
                out->rounded_rect({cx, cy - 16.0f, 58.0f, 32.0f}, 16.0f, track);
                out->circle(cx + (row.toggle > 0 ? 29.0f + 13.0f : 3.0f + 13.0f), cy, 13.0f, kInk);
            }
            else if (row.check >= 0)
            {
                const ui::Rect box{cx, cy - 16.0f, 32.0f, 32.0f};
                if (row.check > 0)
                {
                    out->rounded_rect(box, 8.0f, on ? kDark : kInk);
                    icons::draw(*out, icons::Icon::check, box.cx(), box.cy(), 20.0f, on ? kInk : kDark, 3.0f);
                }
                else
                    out->bordered_rect(box, 8.0f, Color{0, 0, 0, 0}, 2.0f, ink);
            }
            else if (!row.value.empty())
            {
                ui::text_fit(*out, type.regular, row.value, x + width - 20.0f - (row.dot ? 20.0f : 0.0f), cy + 22.0f * 0.35f, 22.0f,
                             420.0f, on ? kDark : kDim, ui::Align::right);
                if (row.dot)
                    out->circle(x + width - 20.0f - 5.0f, cy - 2.0f, 5.0f, kGold);
            }
            else if (!row.button.empty())
            {
                Button b;
                b.label = row.button;
                b.small = true;
                b.primary = on;
                draw_button(ctx, *out, cx, cy - 26.0f, b);
            }
            out->pop_opacity();
        }
        y += h;
    }
    return y;
}

// ---------------------------------------------------------------- title cards, notices

void spinner(Context &ctx, hui::gfx::DrawList &out, float cx, float cy, float size, float width)
{
    out.ring(cx, cy, size * 0.5f, width, Color::rgb(0xf7f3ec, 0.2f));
    out.arc(cx, cy, size * 0.5f, width, std::fmod(ctx.clock * 5.0f, 6.2831853f), 1.5708f, kInk);
}

void draw_title(Context &ctx, hui::gfx::DrawList &out, const Title &t, float in)
{
    if (in <= 0.01f)
        return;
    // The design's intertitle, centred on the screen over a dark veil.
    const ui::Type &type = ctx.type;
    const int body_lines = lines(type.regular, t.body, 26.0f, 1100.0f, 6);
    const int small_lines = lines(type.regular, t.small, 18.0f, 1100.0f, 4);
    float h = (t.kicker.empty() ? 0.0f : 18.0f * 1.6f) + 18.0f + 1.0f + 26.0f + 78.0f * 1.05f +
              (body_lines > 0 ? 22.0f + 40.3f * static_cast<float>(body_lines) : 0.0f) +
              (small_lines > 0 ? 14.0f + 27.0f * static_cast<float>(small_lines) : 0.0f) +
              (t.options.empty() ? 0.0f : 28.0f + 76.0f * static_cast<float>(t.options.size()) - 12.0f) +
              (t.controls.empty() ? 0.0f : 30.0f + 52.0f * static_cast<float>((t.controls.size() + 1) / 2)) +
              (t.buttons.empty() ? 0.0f : 40.0f + 66.0f);
    out.push_opacity(in);
    // The design blurs what's behind; without a blur, a deeper veil keeps it quiet.
    out.rounded_rect({0, 0, 1920, 1080}, 0.0f, Color::rgb(0x000000, 0.86f));
    float y = 540.0f - h * 0.5f;
    if (!t.kicker.empty())
    {
        caps(out, type.semibold, t.kicker, 960.0f, baseline(y, 18.0f), 18.0f, kGold, ui::Align::center);
        y += 18.0f * 1.6f;
    }
    y += 18.0f;
    out.rounded_rect({930.0f, y, 60.0f, 1.0f}, 0.0f, kGold);
    y += 1.0f + 26.0f;
    const auto &font = title_face(type, t.title);
    const float size = ui::fit_size(font, t.title, 78.0f, 52.0f, 1600.0f);
    const float tw = font.measure(t.title, size) + (t.spin ? 44.0f + 22.0f : 0.0f);
    if (t.spin)
        spinner(ctx, out, 960.0f - tw * 0.5f + 22.0f, y + 78.0f * 1.05f * 0.5f, 44.0f, 4.0f);
    ui::text(out, font, t.title, 960.0f - tw * 0.5f + (t.spin ? 66.0f : 0.0f), y + 78.0f * 0.8f, size, kInk);
    y += 78.0f * 1.05f;
    if (body_lines > 0)
    {
        ui::paragraph(out, type.regular, t.body, 960.0f, baseline(y + 22.0f, 26.0f, 1.55f), 26.0f, 1100.0f, 40.3f,
                      Color::rgb(0xf7f3ec, 0.84f), 6, ui::Align::center);
        y += 22.0f + 40.3f * static_cast<float>(body_lines);
    }
    if (small_lines > 0)
    {
        ui::paragraph(out, type.regular, t.small, 960.0f, baseline(y + 14.0f, 18.0f, 1.5f), 18.0f, 1100.0f, 27.0f, kFaint, 4,
                      ui::Align::center);
        y += 14.0f + 27.0f * static_cast<float>(small_lines);
    }
    if (!t.controls.empty())
    {
        // Controls: each button and what it does, in two left-aligned columns about the middle.
        ui::HintStyle style{0.0f, 23.0f, 34.0f, 16.0f, Color::rgb(0xf7f3ec, 0.85f)};
        y += 30.0f + ui::controls_grid(&out, type, t.controls, 960.0f - 435.0f, y + 30.0f, 940.0f, 52.0f, style);
    }
    if (!t.options.empty())
    {
        y += 28.0f;
        for (const Option &o : t.options)
        {
            const ui::Rect r{550.0f, y, 820.0f, 64.0f};
            out.push_opacity(o.enabled ? 1.0f : 0.42f);
            out.rounded_rect(r, 16.0f, o.focus ? Color::rgb(0xf7f3ec, 0.95f) : Color::rgb(0xf7f3ec, 0.06f));
            const Color ink = o.focus ? kDark : kInk;
            const ui::Rect box{r.x + 20.0f, r.cy() - 15.0f, 30.0f, 30.0f};
            if (o.checked)
            {
                out.rounded_rect(box, 8.0f, o.focus ? kDark : kInk);
                icons::draw(out, icons::Icon::check, box.cx(), box.cy(), 18.0f, o.focus ? kInk : kDark, 3.0f);
            }
            else
                out.bordered_rect(box, 8.0f, Color{0, 0, 0, 0}, 2.0f, ink);
            ui::text_fit(out, o.focus ? type.semibold : type.regular, o.label, box.x + 46.0f, r.cy() + 22.0f * 0.35f, 22.0f,
                         r.w - 96.0f, ink);
            out.pop_opacity();
            y += 64.0f + 12.0f;
        }
        y -= 12.0f;
    }
    if (!t.buttons.empty())
        draw_buttons(ctx, out, 960.0f, y + 40.0f, t.buttons, true);
    out.pop_opacity();
}

float draw_notice(Context &ctx, hui::gfx::DrawList &out, float top, icons::Icon icon, Color tone, std::string_view text,
                  std::string_view sub)
{
    // A glass note at the top right: a toned disc, the line, the small print.
    const ui::Type &type = ctx.type;
    const float text_w = std::min(600.0f - 14.0f - 42.0f - 16.0f - 26.0f,
                                  std::max(type.semibold.measure(text, 22.0f), sub.empty() ? 0.0f : type.regular.measure(sub, 17.0f)));
    const int n = lines(type.regular, sub, 17.0f, text_w, 3);
    const float h = 14.0f + std::max(42.0f, 6.0f + 35.2f + (n > 0 ? 4.0f + 24.65f * static_cast<float>(n) : 0.0f)) + 14.0f;
    const float w = 14.0f + 42.0f + 16.0f + text_w + 26.0f;
    const ui::Rect r{1820.0f - w, top, w, h};
    glass(out, r, 28.0f);
    out.circle(r.x + 14.0f + 21.0f, r.y + 14.0f + 21.0f, 21.0f, tone);
    icons::draw(out, icon, r.x + 14.0f + 21.0f, r.y + 14.0f + 21.0f, 22.0f, kDark, 2.4f);
    const float tx = r.x + 14.0f + 42.0f + 16.0f;
    ui::text_fit(out, type.semibold, text, tx, baseline(r.y + 14.0f + 6.0f, 22.0f), 22.0f, text_w, kInk);
    if (n > 0)
        ui::paragraph(out, type.regular, sub, tx, baseline(r.y + 14.0f + 6.0f + 35.2f + 4.0f, 17.0f, 1.45f), 17.0f, text_w, 24.65f,
                      kDim, 3);
    return h;
}

void draw_status(Context &ctx, hui::gfx::DrawList &out, std::string_view title, std::string_view body, std::string_view small,
                 const std::vector<Button> &buttons, bool spin)
{
    // On black: the wordmark (or a spinner), a gold rule, the title, the
    // lines under it and buttons, a little above the middle.
    const ui::Type &type = ctx.type;
    const int body_lines = lines(type.regular, body, 24.0f, 1100.0f, 4);
    const int small_lines = lines(type.regular, small, 18.0f, 1100.0f, 4);
    const float top_h = spin ? 70.0f : 64.0f * 1.6f;
    const float h = top_h + 30.0f + 1.0f + 30.0f + 58.0f * 1.6f + (body_lines > 0 ? 16.0f + 36.0f * static_cast<float>(body_lines) : 0.0f) +
                    (small_lines > 0 ? 12.0f + 27.0f * static_cast<float>(small_lines) : 0.0f) + (buttons.empty() ? 0.0f : 36.0f + 66.0f);
    out.rounded_rect({0, 0, 1920, 1080}, 0.0f, Color::rgb(0x000000));
    float y = 540.0f - h * 0.55f;
    if (spin)
        spinner(ctx, out, 960.0f, y + 35.0f, 70.0f, 5.0f);
    else
    {
        const auto &font = title_face(type, "Orbit");
        const float w = 58.0f + 16.0f + font.measure("Orbit", 64.0f);
        if (ctx.mark != 0)
            out.image(ctx.mark, {960.0f - w * 0.5f, y + top_h * 0.5f - 29.0f, 58.0f, 58.0f}, hui::gfx::kFullUv, kInk);
        ui::text(out, font, "Orbit", 960.0f - w * 0.5f + 74.0f, y + top_h * 0.5f + 64.0f * 0.35f, 64.0f, kInk);
    }
    y += top_h + 30.0f;
    out.rounded_rect({930.0f, y, 60.0f, 1.0f}, 0.0f, kGold);
    y += 1.0f + 30.0f;
    ui::text(out, title_face(type, title), title, 960.0f, baseline(y, 58.0f), 58.0f, kInk, ui::Align::center);
    y += 58.0f * 1.6f;
    if (body_lines > 0)
    {
        ui::paragraph(out, type.regular, body, 960.0f, baseline(y + 16.0f, 24.0f, 1.5f), 24.0f, 1100.0f, 36.0f,
                      Color::rgb(0xf7f3ec, 0.84f), 4, ui::Align::center);
        y += 16.0f + 36.0f * static_cast<float>(body_lines);
    }
    if (small_lines > 0)
    {
        ui::paragraph(out, type.regular, small, 960.0f, baseline(y + 12.0f, 18.0f, 1.5f), 18.0f, 1100.0f, 27.0f, kFaint, 4,
                      ui::Align::center);
        y += 12.0f + 27.0f * static_cast<float>(small_lines);
    }
    if (!buttons.empty())
        draw_buttons(ctx, out, 960.0f, y + 36.0f, buttons, true);
}

void keyboard_look(hui::ui::KeyboardStyle &style)
{
    xb::keyboard_look(style, true);
    style.radius = 14.0f;
    style.theme.surface_high = Color::rgb(0xf7f3ec, 0.08f);
    style.theme.secondary = Color::rgb(0xf7f3ec, 0.08f);
    style.theme.primary = kInk;
    style.theme.on_primary = kDark;
    style.theme.text = kInk;
}

void draw_keyboard(Context &ctx, orbit::Frame &frame, const hui::ui::Keyboard &keys, std::string_view label, std::string_view value,
                   std::string_view placeholder, bool secret)
{
    // The label, the text in large italics and a gold caret; the keys in a sheet.
    const ui::Type &type = ctx.type;
    hui::gfx::DrawList &out = frame.overlay;
    kick(ctx, out, 100.0f, 150.0f, label);
    std::string shown;
    if (secret)
        for (unsigned char c : value)
            if ((c & 0xc0) != 0x80)
                shown += "\xE2\x80\xA2";
    const std::string_view text = secret ? std::string_view(shown) : value;
    const float base = 150.0f + 25.6f + 14.0f + 110.0f * 0.82f;
    float w = 0.0f;
    if (text.empty())
        w = ui::text_fit(out, title_face(type, placeholder), placeholder, 100.0f, base, 110.0f, 1600.0f, kFaint);
    else
        w = ui::text_fit(out, title_face(type, text), text, 100.0f, base, 110.0f, 1600.0f, kInk);
    out.rounded_rect({100.0f + w + 8.0f, base - 86.0f, 4.0f, 96.0f}, 0.0f, kGold);
    draw_sheet(out, 430.0f);
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
    style.ink = Color::rgb(0xf7f3ec, 0.72f);
    return style;
}

} // namespace orbit::cn
