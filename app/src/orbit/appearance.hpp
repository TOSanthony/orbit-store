// Orbit Store TV app - The app background the owner chose in Settings.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gfx/backdrop_spec.hpp"

#include <cstddef>
#include <string>

namespace orbit::appearance
{

struct Preset
{
    const char *id;      // stored on disk, never translated
    const char *name;    // English, passed through tr()
    const char *about;   // English, passed through tr()
    bool animated;
};

// The presets in menu order; the first is the default (the original black page).
std::size_t count();
const Preset &preset(std::size_t index);
// Index of an id, 0 when unknown.
std::size_t find(const std::string &id);
// The backdrop of a preset at a time (seconds).
hui::gfx::BackdropSpec backdrop(std::size_t index, float time);

// Where the choice is kept: $ORBIT_APPEARANCE_FILE, else /data/orbit-store/appearance.cfg.
std::string path();
// The saved preset index (0 when nothing is saved or the file is unreadable).
std::size_t load();
// Saves a preset; false when the file cannot be written.
bool save(std::size_t index);

} // namespace orbit::appearance
