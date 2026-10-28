// Download recovery controls shared by the native settings interfaces.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <vector>
namespace orbit {
struct DownloadSetting { const char *key; const char *label; int min, max, initial; std::vector<int> choices; };
inline const std::array<DownloadSetting, 4> download_settings = {{
    {"downloadRetries", /* i18n */ "Automatic retries", 0, 10, 3, {0,1,2,3,4,5,6,7,8,9,10}},
    {"downloadRetryDelay", /* i18n */ "Wait between retries (seconds)", 1, 300, 10, {1,5,10,20,30,60,120,300}},
    {"downloadStallTimeout", /* i18n */ "Wait before reconnecting (seconds)", 15, 600, 60, {15,30,60,90,120,180,300,600}},
    {"downloadMinSpeed", /* i18n */ "Reconnect below this speed (KiB/s)", 1, 1024, 1, {1,16,32,64,128,256,512,1024}},
}};
inline std::vector<int> download_choices(int index, int current) {
    auto choices = download_settings[index].choices;
    if (std::find(choices.begin(), choices.end(), current) == choices.end()) choices.push_back(current);
    std::sort(choices.begin(), choices.end());
    return choices;
}
// Connections per download (1–64): steps for a controller, and any value set elsewhere.
inline std::vector<int> connection_choices(int current) {
    std::vector<int> choices{1, 2, 4, 8, 12, 16, 24, 32, 48, 64};
    if (current >= 1 && current <= 64 && std::find(choices.begin(), choices.end(), current) == choices.end())
        choices.push_back(current);
    std::sort(choices.begin(), choices.end());
    return choices;
}
inline constexpr const char *connections_help =
    "Choose 1–64. Eight by default. Downloads running at once share up to 64, and one already started keeps its split. "
    "More connections can speed up slow sources that allow them.";
inline constexpr const char *download_recovery_help =
    "Orbit can try again if a download fails or gets stuck. Your progress is kept. Set retries to 0 to turn this off.";
inline constexpr const char *download_speed_help =
    "Orbit reconnects if the speed stays below your chosen limit for the time you set. Changes apply on the next try. Some websites may require a longer wait.";
}
