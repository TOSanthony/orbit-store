// Orbit Store TV app - The app background the owner chose in Settings.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/appearance.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace orbit::appearance
{
namespace
{

using hui::gfx::BackdropMode;

struct Entry
{
    Preset info;
    BackdropMode mode;
    std::uint32_t colour[4];
    float param[3];
};

// Colours stay dark so the white text and the cards remain readable.
const Entry kEntries[] = {
    {{"black", /* i18n */ "Black", /* i18n */ "The original near-black page.", false},
     BackdropMode::gradient, {0x080808, 0x000000, 0x101010, 0}, {0.08f, 0.0f, 1.0f}},
    {{"midnight", /* i18n */ "Midnight blue", /* i18n */ "Deep navy, lit from the top left.", false},
     BackdropMode::gradient, {0x0b1530, 0x02050f, 0x133070, 0}, {0.08f, 0.0f, 0.8f}},
    {{"violet", /* i18n */ "Violet", /* i18n */ "Dark purple with a soft glow.", false},
     BackdropMode::gradient, {0x1a0f30, 0x06030f, 0x4a2a8a, 0}, {0.5f, 0.0f, 0.6f}},
    {{"forest", /* i18n */ "Forest green", /* i18n */ "Dark green fading to black.", false},
     BackdropMode::gradient, {0x0a2418, 0x020805, 0x1d6b43, 0}, {0.9f, 0.0f, 0.5f}},
    {{"crimson", /* i18n */ "Crimson", /* i18n */ "Deep red fading to black.", false},
     BackdropMode::gradient, {0x2a0a10, 0x080203, 0x8a1c2e, 0}, {0.1f, 0.0f, 0.55f}},
    {{"slate", /* i18n */ "Slate grey", /* i18n */ "Neutral dark grey.", false},
     BackdropMode::gradient, {0x20242b, 0x0b0d10, 0x3a414d, 0}, {0.5f, 0.0f, 0.4f}},
    {{"aurora", /* i18n */ "Aurora", /* i18n */ "Slow drifting northern lights.", true},
     BackdropMode::aurora, {0x0b1026, 0x1b1f4a, 0x3a5bd9, 0xc04bd6}, {0, 0, 0}},
    {{"stars", /* i18n */ "Starfield", /* i18n */ "Twinkling stars in deep space.", true},
     BackdropMode::stars, {0x02030a, 0x0a1030, 0x2a3a8a, 0x6a2a8a}, {0.0f, 0.0f, 0.0f}},
    {{"waves", /* i18n */ "Waves", /* i18n */ "Glowing ribbons rolling across the screen.", true},
     BackdropMode::waves, {0x050a1c, 0x0a1a3a, 0x2f7fe0, 0x20d0c0}, {0, 0, 0}},
    {{"bokeh", /* i18n */ "Bokeh", /* i18n */ "Soft lights floating upwards.", true},
     BackdropMode::bokeh, {0x0a0a18, 0x14102a, 0x6a5ae0, 0xe05aa0}, {0, 0, 0}},
    {{"grid", /* i18n */ "Neon grid", /* i18n */ "A retro sunset over a moving grid.", true},
     BackdropMode::grid, {0x0a0420, 0x3a0a4a, 0xe02aa0, 0xffb030}, {0, 0, 0}},
    {{"dots", /* i18n */ "Pulsing dots", /* i18n */ "A grid of dots with a gentle pulse.", true},
     BackdropMode::dots, {0x0a2747, 0x0c3157, 0x2f6fa6, 0}, {0, 0, 0}},
    {{"vista", /* i18n */ "Mountains", /* i18n */ "Layered ridges at sunset.", true},
     BackdropMode::vista, {0x2a1a48, 0xe07a50, 0x1a1030, 0x4a2a5a}, {1.0f, 0.0f, 0.0f}},
};

} // namespace

std::size_t count()
{
    return sizeof(kEntries) / sizeof(kEntries[0]);
}

const Preset &preset(std::size_t index)
{
    return kEntries[index < count() ? index : 0].info;
}

std::size_t find(const std::string &id)
{
    for (std::size_t i = 0; i < count(); ++i)
        if (id == kEntries[i].info.id)
            return i;
    return 0;
}

hui::gfx::BackdropSpec backdrop(std::size_t index, float time)
{
    const Entry &entry = kEntries[index < count() ? index : 0];
    hui::gfx::BackdropSpec spec;
    spec.mode = entry.mode;
    for (int i = 0; i < 4; ++i)
        spec.colors[i] = hui::gfx::Color::rgb(entry.colour[i]);
    for (int i = 0; i < 3; ++i)
        spec.params[i] = entry.param[i];
    spec.time = time;
    return spec;
}

std::string path()
{
    const char *over = std::getenv("ORBIT_APPEARANCE_FILE");
    return over != nullptr && *over != '\0' ? over : "/data/orbit-store/appearance.cfg";
}

std::size_t load()
{
    std::ifstream file(path());
    std::string id;
    if (!file || !std::getline(file, id))
        return 0;
    while (!id.empty() && (id.back() == '\r' || id.back() == ' '))
        id.pop_back();
    return find(id);
}

bool save(std::size_t index)
{
    const std::filesystem::path where = path();
    std::error_code ignored;
    if (where.has_parent_path())
        std::filesystem::create_directories(where.parent_path(), ignored);
    std::ofstream file(where, std::ios::trunc);
    file << preset(index).id << '\n';
    return static_cast<bool>(file);
}

} // namespace orbit::appearance
