// Orbit Store TV app - Diagnostics on screen (App settings → More settings).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The browser's diagnostics (src/Diagnostics.tsx) for a controller: the download
// service's version and health, its download errors and recent events, over the
// screen. Cross reads them again, Up and Down scroll, Circle closes. Saving the
// report to a drive stays in More settings.

#include "orbit/screens.hpp"
#include "orbit/i18n.hpp"
#include "orbit/thread.hpp"

#include <algorithm>

namespace orbit
{

namespace
{

constexpr float kLeft = 360.0f;
constexpr float kWidth = 1200.0f;
constexpr float kTop = 150.0f;
constexpr float kListTop = 330.0f;
constexpr float kListBottom = 960.0f;

// Phases, statuses and reasons stay in English, as in the browser: they are codes for support.
std::string readable(std::string text)
{
    std::replace(text.begin(), text.end(), '-', ' ');
    return text;
}

} // namespace

struct DiagnosticsView::Item
{
    enum class Kind : std::uint8_t
    {
        heading,
        fact,
        event,
        note,
    } kind;
    std::string title, detail;
    float height() const
    {
        return kind == Kind::heading ? 64.0f : kind == Kind::event ? 74.0f : 44.0f;
    }
};

DiagnosticsView::DiagnosticsView(Context &context) : ctx_(context)
{
}

void DiagnosticsView::open()
{
    open_ = true;
    first_ = 0;
    ctx_.store.load_diagnostics();
}

std::vector<DiagnosticsView::Item> DiagnosticsView::items() const
{
    const DiagnosticsReport &report = ctx_.store.state().diagnostics;
    std::vector<Item> out;
    if (!report.known)
        return out;
    out.push_back({Item::Kind::fact, tr("Running version"), report.version});
    out.push_back({Item::Kind::fact, tr("Platform"), report.platform == "ps5" ? "PS5" : tr("Desktop preview")});
    out.push_back({Item::Kind::fact, tr("Queue storage"), report.state_healthy ? tr("Healthy") : tr("Needs attention")});
    out.push_back({Item::Kind::fact, tr("Home-screen icon"), readable(report.launcher_status)});
    out.push_back({Item::Kind::heading, tr("Download errors"), {}});
    bool failed = false;
    for (const DiagnosticEvent &d : report.downloads)
    {
        if (d.reason == "none")
            continue;
        failed = true;
        out.push_back({Item::Kind::event,
                       tr("Download {id}", {{"id", std::to_string(d.download)}}) + " \xC2\xB7 " + d.provider,
                       readable(d.reason) + " \xC2\xB7 " + readable(d.status)});
    }
    if (!failed)
        out.push_back({Item::Kind::note, tr("No errors in the current queue."), {}});
    out.push_back({Item::Kind::heading, tr("Recent events"), {}});
    const double now = unix_seconds();
    const int from = std::max(0, static_cast<int>(report.events.size()) - 8);
    for (int i = static_cast<int>(report.events.size()) - 1; i >= from; --i)
    {
        const DiagnosticEvent &e = report.events[static_cast<std::size_t>(i)];
        std::string title = e.download ? tr("Download {id}", {{"id", std::to_string(e.download)}}) + " \xC2\xB7 " : std::string();
        title += readable(e.phase.empty() ? e.status : e.phase);
        std::string detail = format_age(e.at, now);
        if (e.download)
            detail += " \xC2\xB7 " + format_bytes(e.received) + " / " + format_bytes(e.total);
        if (e.http_status)
            detail += " \xC2\xB7 HTTP " + std::to_string(e.http_status);
        if (!e.download && e.status != "none")
            detail += " \xC2\xB7 " + readable(e.status);
        if (e.result)
            detail += " \xC2\xB7 " + tr("code {code}", {{"code", std::to_string(e.result)}});
        if (e.reason != "none" && !e.reason.empty())
            detail += " \xC2\xB7 " + readable(e.reason);
        out.push_back({Item::Kind::event, std::move(title), std::move(detail)});
    }
    return out;
}

int DiagnosticsView::last_first() const
{
    // The first item that still lets the last one show.
    const std::vector<Item> all = items();
    float height = 0.0f;
    for (int i = static_cast<int>(all.size()) - 1; i >= 0; --i)
    {
        height += all[static_cast<std::size_t>(i)].height();
        if (height > kListBottom - kListTop)
            return i + 1;
    }
    return 0;
}

void DiagnosticsView::update(const InputFrame &input, float dt, Feedback &feedback)
{
    (void)dt;
    if (!open_)
        return;
    const bool busy = ctx_.store.setting_result().busy && ctx_.store.setting_result().setting == Setting::diagnostics_report;
    if (input.is_pressed(Action::back))
    {
        open_ = false;
        feedback.play(hui::audio::Cue::modal_close);
    }
    else if (input.nav == Direction::up || input.nav == Direction::down)
    {
        const int next = first_ + (input.nav == Direction::down ? 1 : -1);
        if (next >= 0 && next <= last_first())
        {
            first_ = next;
            feedback.play(hui::audio::Cue::focus);
        }
        else
            refuse(feedback, input);
    }
    else if (input.is_pressed(Action::confirm) && !busy)
    {
        ctx_.store.load_diagnostics();
        feedback.play(hui::audio::Cue::select);
    }
}

void DiagnosticsView::draw(Frame &frame) const
{
    if (!open_)
        return;
    const ui::Type &type = ctx_.type;
    hui::gfx::DrawList &overlay = frame.overlay;
    overlay.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, ui::color::night0.with_alpha(0.94f));
    ui::text_fit(overlay, type.light, tr("Diagnostics"), kLeft, kTop + 44.0f, 46.0f, kWidth, ui::color::text);
    ui::paragraph(overlay, type.regular,
                  tr("Check Orbit\xE2\x80\x99s status and recent download events. Reports exclude pairing codes, access "
                     "tokens, download links, game names and file paths. Nothing is sent automatically."),
                  kLeft, kTop + 100.0f, 21.0f, kWidth, 32.0f, ui::color::text2, 3);
    const SettingResult &result = ctx_.store.setting_result();
    const bool loading = result.busy && result.setting == Setting::diagnostics_report;
    const std::vector<Item> all = items();
    if (all.empty())
    {
        const std::string message = loading ? std::string(tr("Loading report\xE2\x80\xA6"))
                                    : result.setting == Setting::diagnostics_report && !result.error.empty()
                                        ? std::string(tr(std::string_view(result.error)))
                                        : std::string();
        ui::text(overlay, type.regular, message, kLeft, kListTop + 30.0f, 22.0f,
                 result.error.empty() ? ui::color::text2 : ui::color::bad);
        return;
    }
    overlay.push_clip({kLeft - 20.0f, kListTop - 10.0f, kWidth + 40.0f, kListBottom - kListTop + 10.0f});
    float y = kListTop;
    for (int i = std::clamp(first_, 0, static_cast<int>(all.size()) - 1); i < static_cast<int>(all.size()); ++i)
    {
        const Item &item = all[static_cast<std::size_t>(i)];
        if (y + item.height() > kListBottom + 1.0f)
            break;
        switch (item.kind)
        {
        case Item::Kind::heading:
            ui::text(overlay, type.medium, item.title, kLeft, y + 44.0f, 26.0f, ui::color::text);
            break;
        case Item::Kind::fact:
            ui::text(overlay, type.regular, item.title, kLeft, y + 30.0f, 21.0f, ui::color::text2);
            ui::text_fit(overlay, type.medium, item.detail, kLeft + 420.0f, y + 30.0f, 21.0f, kWidth - 420.0f,
                         ui::color::text);
            break;
        case Item::Kind::event:
            ui::text_fit(overlay, type.medium, item.title, kLeft, y + 28.0f, 21.0f, kWidth, ui::color::text);
            ui::text_fit(overlay, type.regular, item.detail, kLeft, y + 58.0f, 18.0f, kWidth, ui::color::text2);
            break;
        case Item::Kind::note:
            ui::text(overlay, type.regular, item.title, kLeft, y + 30.0f, 20.0f, ui::color::text3);
            break;
        }
        y += item.height();
    }
    overlay.pop_clip();
    if (loading)
        ui::text(overlay, type.regular, tr("Loading report\xE2\x80\xA6"), kLeft + kWidth, kTop + 44.0f, 20.0f,
                 ui::color::text2, ui::Align::right);
}

std::vector<hui::ui::Hint> DiagnosticsView::hints() const
{
    return {{hui::ui::Button::dpad, tr("Scroll")},
            {hui::ui::Button::cross, tr("Refresh report")},
            {hui::ui::Button::circle, tr("Close")}};
}

} // namespace orbit
