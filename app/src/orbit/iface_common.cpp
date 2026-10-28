// Orbit Store TV app - Drawing the interfaces share: their thumbnails.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/look.hpp"
#include "orbit/screens.hpp"

namespace orbit
{

const char *section_title(int section)
{
    switch (section)
    {
    case section::search:
        return /* i18n */ "Search";
    case section::browse:
        return /* i18n */ "Browse";
    case section::addons:
        return /* i18n */ "Add-ons";
    case section::library:
        return /* i18n */ "Library";
    case section::downloads:
        return /* i18n */ "Downloads";
    case section::updates:
        return /* i18n */ "Updates";
    case section::dlc:
        return /* i18n */ "DLC";
    case section::settings:
        return /* i18n */ "Settings";
    default:
        return /* i18n */ "Discover";
    }
}

void draw_interface_thumb(Context &ctx, hui::gfx::DrawList &list, int interface, const ui::Rect &r)
{
    using ui::Color;
    const ui::Type &type = ctx.type;
    list.push_clip(r);
    switch (static_cast<look::Interface>(interface))
    {
    case look::Interface::crossbar:
    {
        // Ribbons on dusk, a row of category marks, a cover and a line of text.
        list.gradient_rect(r, 12.0f, Color::rgb(0x1c2257), Color::rgb(0x45286b));
        list.gradient_rect_h({r.x, r.y + r.h * 0.55f, r.w, r.h * 0.45f}, 0.0f,
                             Color::rgb(0x45286b, 0.0f), Color::rgb(0x8a3a68, 0.8f));
        for (int i = 0; i < 4; ++i)
        {
            const float y = r.y + r.h * (0.5f + 0.06f * static_cast<float>(i));
            list.line(r.x, y, r.x + r.w, y - r.h * 0.08f, 1.5f, Color::rgb(0xffffff, 0.08f));
        }
        for (int i = 0; i < 5; ++i)
        {
            const float size = i == 1 ? r.w * 0.05f : r.w * 0.032f;
            list.ring(r.x + r.w * (0.16f + 0.12f * static_cast<float>(i)), r.y + r.h * 0.3f, size,
                      1.6f, Color::rgb(0xffffff, 0.9f));
        }
        list.rounded_rect({r.x + r.w * 0.18f, r.y + r.h * 0.52f, r.w * 0.18f, r.w * 0.18f}, 8.0f,
                          Color::rgb(0xffffff, 0.85f));
        list.rounded_rect({r.x + r.w * 0.42f, r.y + r.h * 0.58f, r.w * 0.34f, r.h * 0.07f}, 4.0f,
                          Color::rgb(0xffffff, 0.7f));
        break;
    }
    case look::Interface::cinema:
    {
        // Full-screen art and a bold title.
        list.gradient_rect(r, 12.0f, Color::rgb(0x2a2015), Color::rgb(0x000000));
        list.glow({r.x + r.w * 0.55f, r.y + r.h * 0.12f, r.w * 0.2f, r.w * 0.2f}, r.w * 0.1f,
                  r.w * 0.12f, Color::rgb(0xe8dcc4, 0.35f));
        list.circle(r.x + r.w * 0.65f, r.y + r.h * 0.34f, r.w * 0.11f, Color::rgb(0xf3ece0));
        list.gradient_rect({r.x, r.y + r.h * 0.4f, r.w, r.h * 0.6f}, 0.0f, Color::rgb(0x000000, 0.0f),
                           Color::rgb(0x000000, 0.85f));
        ui::text(list, ui::face(type.cinema, type.semibold, "Tidewater"), "Tidewater", r.x + r.w * 0.08f,
                 r.y + r.h * 0.74f, r.h * 0.16f, Color::rgb(0xf7f3ec));
        for (int i = 0; i < 4; ++i)
            list.rounded_rect({r.x + r.w * (0.58f + 0.1f * static_cast<float>(i)), r.y + r.h * 0.82f,
                               r.w * 0.085f, r.h * 0.1f},
                              2.0f, Color::rgb(0xf7f3ec, i == 0 ? 0.8f : 0.4f));
        break;
    }
    case look::Interface::panorama:
    {
        // A huge lowercase word over a few live tiles.
        list.rounded_rect(r, 12.0f, Color::rgb(0x0c0d10));
        ui::text(list, ui::face(type.xlight, type.light, "featured"), "featured", r.x + r.w * 0.05f,
                 r.y + r.h * 0.3f, r.h * 0.26f, Color::rgb(0xffffff));
        const float u = r.h * 0.2f;
        const float top = r.y + r.h * 0.42f;
        list.rounded_rect({r.x + r.w * 0.06f, top, 2.0f * u + 3.0f, 2.0f * u + 3.0f}, 0.0f,
                          Color::rgb(0x2c4f78));
        list.circle(r.x + r.w * 0.06f + u, top + u, u * 0.55f, Color::rgb(0xdfe8f2));
        const Color tiles[] = {Color::rgb(0x1ba1e2), Color::rgb(0xd80073), Color::rgb(0x3a3c42),
                               Color::rgb(0x25272c)};
        for (int i = 0; i < 4; ++i)
            list.rounded_rect({r.x + r.w * 0.06f + 2.0f * u + 6.0f + static_cast<float>(i / 2) * (u + 3.0f),
                               top + static_cast<float>(i % 2) * (u + 3.0f), u, u},
                              0.0f, tiles[i]);
        break;
    }
    default:
    {
        // Store: art behind a title, the hero button and a row of covers.
        list.gradient_rect_h(r, 12.0f, Color::rgb(0x080808), Color::rgb(0x5a5a2a));
        list.circle(r.x + r.w * 0.62f, r.y + r.h * 0.42f, r.w * 0.16f, Color::rgb(0xd8d8d0, 0.85f));
        list.rounded_rect({r.x + r.w * 0.06f, r.y + r.h * 0.16f, r.w * 0.36f, r.h * 0.09f}, 3.0f,
                          Color::rgb(0xffffff, 0.9f));
        list.rounded_rect({r.x + r.w * 0.06f, r.y + r.h * 0.32f, r.w * 0.16f, r.h * 0.07f}, 6.0f,
                          Color::rgb(0xffffff, 0.85f));
        const Color covers[] = {Color::rgb(0xc9925a), Color::rgb(0x3aa5a0), Color::rgb(0x4a86c8),
                                Color::rgb(0xb05ad0), Color::rgb(0x3aa58a), Color::rgb(0xc98a5a)};
        const float w = (r.w * 0.88f - 5.0f * 5.0f) / 6.0f;
        for (int i = 0; i < 6; ++i)
            list.rounded_rect({r.x + r.w * 0.06f + static_cast<float>(i) * (w + 5.0f), r.y + r.h * 0.6f, w, w},
                              4.0f, covers[i]);
        break;
    }
    }
    list.pop_clip();
}

} // namespace orbit
