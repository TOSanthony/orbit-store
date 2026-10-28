// Orbit Store TV app - Which interface the app draws, and how it looks.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Store is the default. Crossbar, Cinema and Panorama are other layouts over
// the same screens, actions and settings. The choice is saved on this console
// only (a small file in the title's writable storage); it never reaches Orbit's service.

#pragma once

#include "gfx/draw_list.hpp"

#include <cstdint>
#include <string>

namespace orbit::look
{
// Native titles cannot write the service's /data directory. /download0 is
// the title's persistent writable mount, also used by its runtime logs.
inline constexpr const char *kPrefsPath = "/download0/orbit-store/appearance.json";

enum class Interface : std::uint8_t
{
    store,
    crossbar,
    cinema,
    panorama,
    count,
};

enum class Accent : std::uint8_t
{
    blue,
    magenta,
    green,
    orange,
    count,
};

// Crossbar's background: the time of day, or one of its colours.
enum class Ribbons : std::uint8_t
{
    follow_time,
    dawn,
    day,
    dusk,
    night,
    count,
};

struct Prefs
{
    Interface interface = Interface::store;
    bool reduced_motion = false;
    Accent accent = Accent::blue;  // Panorama
    Ribbons ribbons = Ribbons::follow_time; // Crossbar's background
    bool art_behind = true; // Cinema and Panorama: the last game's art behind pages
    bool notice_seen = false; // Store's one-time "choose how Orbit looks" notice
    std::string language; // a code from i18n::languages(), or empty to follow the console
};

// Reads the saved choice; an empty path keeps everything in memory (previews, tests).
void load(const std::string &path);
const Prefs &prefs();
inline Interface current()
{
    return prefs().interface;
}
inline bool is(Interface interface)
{
    return current() == interface;
}
// Changes a setting other than the interface and saves it.
void set(const Prefs &next);

// After switching, the previous interface comes back unless the change is kept.
void choose(Interface interface, float now);
bool pending();
Interface previous();
float seconds_left(float now);
void keep();
void revert();
// Reverts once the 15 seconds have passed.
void tick(float now);

const char *title(Interface interface);   // the untranslated name, for tr()
const char *accent_title(Accent accent);
const char *ribbons_title(Ribbons ribbons);
// Crossbar's gradient for its setting (and the console's clock): top-left to bottom-right.
void ribbon_colors(hui::gfx::Color out[4]);
hui::gfx::Color accent_color();
// An accent's own colour (Panorama's swatches).
hui::gfx::Color accent_color(Accent accent);
// The console's time zone and summer time, in minutes from UTC (the C library has no localtime_r).
void set_utc_offset(int minutes);
int utc_offset();
// Previews and tests: draw an interface without saving it.
void preview(Interface interface);

} // namespace orbit::look
