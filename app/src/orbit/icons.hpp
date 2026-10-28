// Orbit Store TV app - Line icons for Crossbar, Cinema and Panorama.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interface designs' icon set (24 px grid, round strokes), flattened to
// polylines by tools/icons/generate.sh.

#pragma once

#include "gfx/draw_list.hpp"

#include <cstdint>

namespace orbit::icons
{

enum class Icon : std::uint8_t
{
    search,
    gear,
    heart,
    heart_fill,
    down,
    grid,
    books,
    spark,
    laptop,
    back,
    next,
    more,
    pause,
    play,
    close,
    check,
    drive,
    ps,
    up,
    arrow_down,
    info,
    warn,
    send,
    list,
    chev_down,
    sort,
    filter,
    folder,
    layers,
    link,
    refresh,
    update,
    palette,
    wrench,
    key,
    globe,
    lock,
    clip,
    archive,
    eject,
    copy,
    move,
    scan,
    stop,
    trash,
    history,
    terminal,
    upload,
    cloud,
    wifi,
    star,
    bolt,
    bug,
    tv,
    power,
    eye,
    box,
    clock,
    phone,
    bell,
    pencil,
    cog, // drawn in code: a toothed wheel with a hole (Settings in the Crossbar bar)
    count,
};

// Draws icon centred on (cx, cy), size px square; stroke is in grid units (24 = size).
void draw(hui::gfx::DrawList &list, Icon icon, float cx, float cy, float size, hui::gfx::Color ink,
          float stroke = 1.8f);

} // namespace orbit::icons
