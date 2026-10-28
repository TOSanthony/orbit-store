// Orbit Store TV app - Which interface the app draws, and how it looks.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/look.hpp"

#include "core/save_file.hpp"
#include "orbit/i18n.hpp"
#include "orbit/json.hpp"

#include <ctime>
#include <cstdio>
#include <string_view>

namespace orbit::look
{

namespace
{

constexpr float kKeepSeconds = 15.0f;

struct State
{
    Prefs prefs;
    std::string path;
    bool pending = false;
    Interface previous = Interface::store;
    float deadline = 0.0f;
    int utc_offset = 0; // minutes, for the time of day
};

State &state()
{
    static State instance;
    return instance;
}

constexpr const char *kNames[] = {"store", "crossbar", "cinema", "panorama"};
constexpr const char *kAccents[] = {"blue", "magenta", "green", "orange"};
constexpr const char *kRibbons[] = {"time", "dawn", "day", "dusk", "night"};

template <typename E, std::size_t N>
E parse(std::string_view value, const char *const (&names)[N], E fallback)
{
    for (std::size_t i = 0; i < N; ++i)
        if (value == names[i])
            return static_cast<E>(i);
    return fallback;
}

void save()
{
    const State &s = state();
    if (s.path.empty())
        return;
    const Prefs &p = s.prefs;
    std::string text = "{\"interface\":";
    // Closing during the preview must restore the last confirmed interface.
    text += json::quote(kNames[static_cast<int>(s.pending ? s.previous : p.interface)]);
    text += ",\"motion\":";
    text += json::quote(p.reduced_motion ? "reduced" : "full");
    text += ",\"accent\":";
    text += json::quote(kAccents[static_cast<int>(p.accent)]);
    text += ",\"ribbons\":";
    text += json::quote(kRibbons[static_cast<int>(p.ribbons)]);
    text += ",\"artBehind\":";
    text += p.art_behind ? "true" : "false";
    text += ",\"noticeSeen\":";
    text += p.notice_seen ? "true" : "false";
    text += ",\"language\":";
    text += json::quote(p.language);
    text += "}\n";
    // A failed save keeps the choice for this session and leaves a diagnostic.
    const std::size_t slash = s.path.rfind('/');
    if (slash != std::string::npos && slash > 0)
        (void)hui::save::ensure_directory(s.path.substr(0, slash));
    const std::string error = hui::save::write_atomic(s.path, text);
    if (!error.empty())
        std::fprintf(stderr, "[ORBIT] appearance save failed: %s\n", error.c_str());
}

} // namespace

void load(const std::string &path)
{
    State &s = state();
    s.path = path;
    s.prefs = {};
    // On the console (a saved file exists or can be written) Orbit opens as Crossbar until the player picks another look.
    if (!path.empty())
        s.prefs.interface = Interface::crossbar;
    s.pending = false;
    std::string text;
    if (path.empty() || !hui::save::read_file(path, &text, 16u << 10))
        return;
    json::Value doc;
    if (!json::parse(text, &doc) || !doc.is_object())
        return;
    s.prefs.interface = parse(doc.text("interface"), kNames, Interface::crossbar);
    s.prefs.reduced_motion = doc.text("motion") == "reduced";
    s.prefs.accent = parse(doc.text("accent"), kAccents, Accent::blue);
    s.prefs.ribbons = parse(doc.text("ribbons"), kRibbons, Ribbons::follow_time);
    s.prefs.art_behind = doc.flag("artBehind", true);
    s.prefs.notice_seen = doc.flag("noticeSeen", false);
    if (i18n::supported(doc.text("language")))
        s.prefs.language = doc.text("language");
}

const Prefs &prefs()
{
    return state().prefs;
}

void set(const Prefs &next)
{
    State &s = state();
    const Interface interface = s.prefs.interface;
    s.prefs = next;
    s.prefs.interface = interface;
    save();
}

void choose(Interface interface, float now)
{
    State &s = state();
    if (interface == s.prefs.interface || interface >= Interface::count)
        return;
    s.previous = s.pending ? s.previous : s.prefs.interface;
    s.prefs.interface = interface;
    s.pending = true;
    s.deadline = now + kKeepSeconds;
}

bool pending()
{
    return state().pending;
}

Interface previous()
{
    return state().previous;
}

float seconds_left(float now)
{
    const State &s = state();
    return s.pending ? (s.deadline - now > 0.0f ? s.deadline - now : 0.0f) : 0.0f;
}

void keep()
{
    state().pending = false;
    save();
}

void revert()
{
    State &s = state();
    if (!s.pending)
        return;
    s.prefs.interface = s.previous;
    s.pending = false;
    save();
}

void tick(float now)
{
    if (state().pending && now >= state().deadline)
        revert();
}

const char *title(Interface interface)
{
    switch (interface)
    {
    case Interface::crossbar:
        return /* i18n */ "Crossbar";
    case Interface::cinema:
        return /* i18n */ "Cinema";
    case Interface::panorama:
        return /* i18n */ "Panorama";
    default:
        return /* i18n */ "Store";
    }
}

const char *accent_title(Accent accent)
{
    switch (accent)
    {
    case Accent::magenta:
        return /* i18n */ "Magenta";
    case Accent::green:
        return /* i18n */ "Green";
    case Accent::orange:
        return /* i18n */ "Orange";
    default:
        return /* i18n */ "Blue";
    }
}

const char *ribbons_title(Ribbons ribbons)
{
    switch (ribbons)
    {
    case Ribbons::dawn:
        return /* i18n */ "Dawn";
    case Ribbons::day:
        return /* i18n */ "Day";
    case Ribbons::dusk:
        return /* i18n */ "Dusk";
    case Ribbons::night:
        return /* i18n */ "Night";
    default:
        return /* i18n */ "Follow the time of day";
    }
}

void ribbon_colors(hui::gfx::Color out[4])
{
    using hui::gfx::Color;
    // The design's gradients, at 0, 40, 72 and 100 %.
    static const std::uint32_t kStops[4][4] = {
        {0x1b1838, 0x4a3470, 0xb2607c, 0xf0a27a}, // dawn
        {0x0a2a4a, 0x145a86, 0x2f8fb2, 0x7cc8d6}, // day
        {0x0b1633, 0x1c2257, 0x45286b, 0x8a3a68}, // dusk
        {0x02040b, 0x070d22, 0x10173a, 0x1c1d48}, // night
    };
    int which = 2;
    const Ribbons chosen = prefs().ribbons;
    if (chosen == Ribbons::follow_time)
    {
        const long long local = static_cast<long long>(std::time(nullptr)) + state().utc_offset * 60LL;
        const int hour = static_cast<int>(((local / 3600) % 24 + 24) % 24);
        which = hour >= 5 && hour < 9 ? 0 : hour >= 9 && hour < 17 ? 1 : hour >= 17 && hour < 21 ? 2 : 3;
    }
    else
    {
        which = static_cast<int>(chosen) - 1;
    }
    for (int i = 0; i < 4; ++i)
        out[i] = Color::rgb(kStops[which][i]);
}

hui::gfx::Color accent_color()
{
    return accent_color(prefs().accent);
}

hui::gfx::Color accent_color(Accent accent)
{
    switch (accent)
    {
    case Accent::magenta:
        return hui::gfx::Color::rgb(0xd80073);
    case Accent::green:
        return hui::gfx::Color::rgb(0x339933);
    case Accent::orange:
        return hui::gfx::Color::rgb(0xf09609);
    default:
        return hui::gfx::Color::rgb(0x1ba1e2);
    }
}

void set_utc_offset(int minutes)
{
    state().utc_offset = minutes;
}
int utc_offset()
{
    return state().utc_offset;
}

void preview(Interface interface)
{
    state().prefs.interface = interface;
    state().pending = false;
}

} // namespace orbit::look
