// Orbit Store TV app - App settings: sources, storage, pairing, the catalogue and updates.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The browser version's settings (src/Sources.tsx, src/Pairing.tsx,
// src/CatalogueUpdates.tsx, src/Updates.tsx, src/TvApp.tsx) on one page for
// a controller. Each section is laid out by one function that, given a draw
// list, also draws it, so what the focus moves between is what is drawn.

#include "orbit/appearance.hpp"
#include "orbit/screens.hpp"
#include "orbit/i18n.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace orbit
{

namespace
{

constexpr float kTitleBaseline = 176.0f; // as the other page titles
constexpr float kNavTop = 244.0f;
constexpr float kNavWidth = 400.0f;
constexpr float kNavItem = 62.0f;
constexpr float kNavGap = 6.0f;
constexpr float kContentX = 600.0f;
constexpr float kContentTop = 244.0f;
constexpr float kContentWidth = 940.0f;
constexpr float kViewTop = 212.0f;    // content scrolls under the title
constexpr float kViewBottom = 990.0f; // and stops above the hints
constexpr float kCardRadius = 14.0f;
constexpr float kKeyboardTop = 560.0f;
constexpr int kSectionCount = static_cast<int>(SettingsScreen::Section::count);
constexpr const char *kSections[] = {/* i18n */ "Personalisation", /* i18n */ "Language", /* i18n */ "Download sources", /* i18n */ "Storage", /* i18n */ "Pair a device",
                                     /* i18n */ "Game catalogue",   /* i18n */ "Console", /* i18n */ "Updates", "Orbit Zero",
                                     /* i18n */ "More settings"};
constexpr const char *kSavedElf = "/data/orbit-store/orbit_store.elf";
constexpr const char *kNotice =
    /* i18n */ "You are responsible for checking that your downloads are lawful. Third-party downloads are "
    "at your own risk. Only download content you have permission to access and use, following "
    "applicable law and the provider\xE2\x80\x99s terms. Public links do not prove permission, "
    "and Orbit does not guarantee the files\xE2\x80\x99 safety or authenticity.";

// A small line icon for each section, drawn from shapes, centred on (cx, cy).
void ellipse_outline(hui::gfx::DrawList &list, float cx, float cy, float rx, float ry, float turn,
                     float thickness, ui::Color ink)
{
    constexpr int kSteps = 40;
    float last_x = 0.0f, last_y = 0.0f;
    for (int i = 0; i <= kSteps; ++i)
    {
        const float t = static_cast<float>(i) * 6.2831853f / static_cast<float>(kSteps);
        const float ex = std::cos(t) * rx;
        const float ey = std::sin(t) * ry;
        const float x = cx + ex * std::cos(turn) - ey * std::sin(turn);
        const float y = cy + ex * std::sin(turn) + ey * std::cos(turn);
        if (i > 0)
            list.line(last_x, last_y, x, y, thickness, ink);
        last_x = x;
        last_y = y;
    }
}

void section_icon(hui::gfx::DrawList &list, SettingsScreen::Section section, float cx, float cy,
                  float size, ui::Color ink)
{
    using Section = SettingsScreen::Section;
    const float t = std::max(2.0f, size * 0.085f);
    const ui::Color none{0, 0, 0, 0};
    switch (section)
    {
    case Section::appearance:
    {
        // A palette: a round tray with three dabs of colour.
        ellipse_outline(list, cx, cy, size * 0.44f, size * 0.38f, 0.0f, t, ink);
        list.circle(cx - size * 0.17f, cy - size * 0.08f, size * 0.065f, ink);
        list.circle(cx + size * 0.02f, cy - size * 0.18f, size * 0.065f, ink);
        list.circle(cx + size * 0.2f, cy - size * 0.05f, size * 0.065f, ink);
        break;
    }
    case Section::language:
    {
        // A globe: the circle, a meridian and two parallels.
        const float r = size * 0.42f;
        list.ring(cx, cy, r, t, ink);
        ellipse_outline(list, cx, cy, r * 0.45f, r, 0.0f, t * 0.85f, ink);
        list.line(cx - r, cy, cx + r, cy, t * 0.85f, ink);
        list.line(cx - r * 0.86f, cy - r * 0.5f, cx + r * 0.86f, cy - r * 0.5f, t * 0.85f, ink);
        list.line(cx - r * 0.86f, cy + r * 0.5f, cx + r * 0.86f, cy + r * 0.5f, t * 0.85f, ink);
        break;
    }
    case Section::sources:
        ui::icon_layers(list, cx, cy - size * 0.04f, size, ink);
        break;
    case Section::storage:
    {
        // A drive: a flat box, a divider and a small light.
        const ui::Rect body{cx - size * 0.44f, cy - size * 0.2f, size * 0.88f, size * 0.4f};
        list.bordered_rect(body, size * 0.08f, none, t, ink);
        list.line(body.x, cy + size * 0.02f, body.x + body.w, cy + size * 0.02f, t * 0.85f, ink);
        list.circle(cx + size * 0.28f, cy + size * 0.12f, size * 0.04f, ink);
        list.line(cx - size * 0.3f, cy + size * 0.12f, cx - size * 0.1f, cy + size * 0.12f, t, ink);
        break;
    }
    case Section::pairing:
    {
        // A controller: the body, a cross on the left, two buttons on the right.
        const ui::Rect body{cx - size * 0.46f, cy - size * 0.24f, size * 0.92f, size * 0.48f};
        list.bordered_rect(body, size * 0.22f, none, t, ink);
        list.line(cx - size * 0.28f, cy - size * 0.02f, cx - size * 0.1f, cy - size * 0.02f, t, ink);
        list.line(cx - size * 0.19f, cy - size * 0.11f, cx - size * 0.19f, cy + size * 0.07f, t, ink);
        list.circle(cx + size * 0.14f, cy - size * 0.02f, size * 0.045f, ink);
        list.circle(cx + size * 0.28f, cy + size * 0.07f, size * 0.045f, ink);
        break;
    }
    case Section::catalogue:
    {
        // A grid of four tiles.
        const float tile = size * 0.34f;
        const float gap = size * 0.12f;
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 2; ++col)
                list.bordered_rect({cx - tile - gap * 0.5f + static_cast<float>(col) * (tile + gap),
                                    cy - tile - gap * 0.5f + static_cast<float>(row) * (tile + gap),
                                    tile, tile},
                                   size * 0.07f, none, t, ink);
        break;
    }
    case Section::console:
    {
        // The four PlayStation shapes: triangle, circle, square and cross.
        const float k = size / 36.0f;
        const float tt = std::max(2.0f, size * 0.075f);
        list.line(cx - 9.0f * k, cy - 15.0f * k, cx - 3.0f * k, cy - 5.0f * k, tt, ink);
        list.line(cx - 3.0f * k, cy - 5.0f * k, cx - 15.0f * k, cy - 5.0f * k, tt, ink);
        list.line(cx - 15.0f * k, cy - 5.0f * k, cx - 9.0f * k, cy - 15.0f * k, tt, ink);
        list.ring(cx + 8.0f * k, cy - 10.0f * k, 5.5f * k, tt, ink);
        list.bordered_rect({cx - 14.5f * k, cy + 3.0f * k, 11.0f * k, 11.0f * k}, 2.5f * k, none, tt, ink);
        list.line(cx + 3.0f * k, cy + 3.0f * k, cx + 14.0f * k, cy + 14.0f * k, tt, ink);
        list.line(cx + 14.0f * k, cy + 3.0f * k, cx + 3.0f * k, cy + 14.0f * k, tt, ink);
        break;
    }
    case Section::updates:
    {
        // Two arcs chasing each other, each with an arrow head: a refresh.
        const float r = size * 0.36f;
        const float head = size * 0.15f;
        list.arc(cx, cy, r + t * 0.5f, t, 0.5f, 2.3f, ink, false);
        list.arc(cx, cy, r + t * 0.5f, t, 3.64f, 2.3f, ink, false);
        // Heads sit at the clockwise end of each arc.
        const float a1 = 0.5f + 2.3f;
        const float a2 = 3.64f + 2.3f;
        for (const float a : {a1, a2})
        {
            const float px = cx + std::sin(a) * r;
            const float py = cy - std::cos(a) * r;
            const float tx = std::cos(a);
            const float ty = std::sin(a);
            list.line(px, py, px - tx * head - std::sin(a) * head * 0.9f,
                      py - ty * head + std::cos(a) * head * 0.9f, t, ink);
            list.line(px, py, px - tx * head + std::sin(a) * head * 0.9f,
                      py - ty * head - std::cos(a) * head * 0.9f, t, ink);
        }
        break;
    }
    case Section::zero:
    {
        // A planet on its tilted orbit, as Orbit's own mark.
        list.circle(cx, cy, size * 0.17f, ink);
        ellipse_outline(list, cx, cy, size * 0.46f, size * 0.17f, -0.45f, t * 0.9f, ink);
        break;
    }
    default:
    {
        // Three sliders.
        for (int i = 0; i < 3; ++i)
        {
            const float y = cy + (static_cast<float>(i) - 1.0f) * size * 0.28f;
            const float knob = cx + (i == 1 ? 0.16f : i == 0 ? -0.14f : 0.02f) * size;
            list.line(cx - size * 0.42f, y, cx + size * 0.42f, y, t * 0.85f, ink);
            list.circle(knob, y, size * 0.1f, ink);
        }
        break;
    }
    }
}

int seconds_until(double unix_time)
{
    return std::max(0, static_cast<int>(std::ceil(unix_time - unix_seconds())));
}


// The service words the NAS notices in English; a skipped-entries count is part of the sentence.
std::string nas_message(const std::string &error)
{
    unsigned long count = 0;
    if (std::sscanf(error.c_str(), "%lu entr", &count) == 1 &&
        error.find(" skipped: invalid, duplicated or not on your NAS address.") != std::string::npos)
        return trn(static_cast<long>(count),
                   "{count} entry was skipped: invalid, duplicated or not on your NAS address.",
                   "{count} entries were skipped: invalid, duplicated or not on your NAS address.");
    return std::string(tr(std::string_view(error)));
}

} // namespace

// ---- the pane: one pass over a section ----

struct SettingsScreen::Pane
{
    Pane(const SettingsScreen &owner, hui::gfx::DrawList *target)
        : screen(owner), type(owner.ctx_.type), list(target)
    {
    }

    const SettingsScreen &screen;
    const ui::Type &type;
    hui::gfx::DrawList *list; // null while only laying out
    float x = kContentX;
    float width = kContentWidth;
    float y = kContentTop;
    int row = 0;
    std::vector<Control> controls;

    // The focus of the control registered next.
    float amount() const
    {
        const std::size_t i = controls.size();
        return list != nullptr && i < screen.focus_amount_.size() ? screen.focus_amount_[i] : 0.0f;
    }
    void add(Kind kind, int index, const ui::Rect &r, bool enabled)
    {
        controls.push_back({kind, index, row, r, enabled});
    }

    void heading(std::string_view text)
    {
        if (list != nullptr)
            ui::text(*list, type.light, text, x, y + 34.0f, 36.0f, ui::color::text);
        y += 62.0f;
    }
    void subheading(std::string_view text)
    {
        if (list != nullptr)
            ui::text(*list, type.medium, text, x, y + 22.0f, 24.0f, ui::color::text);
        y += 40.0f;
    }
    // Wrapped text; size 21 is body copy, 17 the browser's .fine.
    void body(std::string_view text, float size = 21.0f, ui::Color color = ui::color::text2,
              float indent = 0.0f)
    {
        const float line = std::round(size * 1.5f);
        const float w = width - indent;
        const std::size_t lines = type.regular.font->wrap(text, size, w).size();
        if (list != nullptr)
            ui::paragraph(*list, type.regular, text, x + indent, y + size, size, w, line, color,
                          99);
        y += static_cast<float>(lines) * line + 8.0f;
    }
    void fine(std::string_view text)
    {
        body(text, 17.0f, ui::color::text3);
    }
    void gap(float h)
    {
        y += h;
    }
    // A hairline between parts, as .source-notice's border-top.
    void rule()
    {
        y += 14.0f;
        if (list != nullptr)
            list->rounded_rect({x, y, width, 1.0f}, 0.0f, ui::color::line);
        y += 30.0f;
    }
    // .update-versions: a label on the left, its value on the right.
    void version_row(std::string_view label, std::string_view value,
                     ui::Color ink = ui::color::text)
    {
        if (list != nullptr)
        {
            ui::text(*list, type.regular, label, x, y + 24.0f, 19.0f, ui::color::text2);
            ui::text(*list, type.medium, value, x + width, y + 24.0f, 20.0f, ink, ui::Align::right);
        }
        y += 40.0f;
    }
    // .notice: a blue-tinted box with an optional bold first line.
    void notice(std::string_view title, std::string_view text, bool bad = false)
    {
        const float size = 19.0f;
        const float line = 28.0f;
        const float pad = 22.0f;
        const float w = width - 2.0f * pad;
        const std::size_t lines = text.empty() ? 0 : type.regular.font->wrap(text, size, w).size();
        const float title_h = title.empty() ? 0.0f : 34.0f;
        const float h = 2.0f * pad + title_h + static_cast<float>(lines) * line - 6.0f;
        if (list != nullptr)
        {
            list->rounded_rect({x, y, width, h}, 12.0f,
                               bad ? ui::Color::rgb(0xffffff, 0.14f)
                                   : ui::Color::rgb(0xffffff, 0.16f));
            if (!title.empty())
                ui::text(*list, type.semibold, title, x + pad, y + pad + 20.0f, 20.0f,
                         bad ? ui::Color::rgb(0xffffff) : ui::color::text);
            if (lines > 0)
                ui::paragraph(*list, type.regular, text, x + pad, y + pad + title_h + size, size, w,
                              line, bad ? ui::Color::rgb(0xffffff) : ui::Color::rgb(0xd0d0d0), 99);
        }
        y += h + 18.0f;
    }
    // A card: a quiet panel round what follows, until end_card(). Its size is
    // only known once laid out, so drawing uses the size a layout pass found.
    std::vector<ui::Rect> cards;
    const std::vector<ui::Rect> *laid_out = nullptr;
    std::vector<float> card_tops;
    void begin_card(std::string_view title, std::string_view about)
    {
        const std::size_t i = cards.size(); // cards don't nest
        card_tops.push_back(y);
        if (list != nullptr && laid_out != nullptr && i < laid_out->size())
            list->bordered_rect((*laid_out)[i], 20.0f, ui::Color::rgb(0xffffff, 0.04f), 1.0f,
                                ui::color::line);
        x += 32.0f;
        width -= 64.0f;
        y += 30.0f;
        if (list != nullptr)
            ui::text(*list, type.semibold, title, x, y + 24.0f, 26.0f, ui::color::text);
        y += 42.0f;
        body(about, 18.0f);
        y += 4.0f;
    }
    void end_card()
    {
        x -= 32.0f;
        width += 64.0f;
        y += 10.0f;
        cards.push_back({x, card_tops.back(), width, y - card_tops.back()});
        card_tops.pop_back();
        y += 24.0f;
    }
    void error(std::string_view text)
    {
        if (text.empty())
            return;
        // Orbit's fixed messages translate like any other text.
        body(tr(text), 19.0f, ui::color::bad);
    }
    // .dialog-actions: buttons side by side on one row of the focus. Longer
    // languages can run past the width; the rest wraps onto a row of its own.
    struct Button
    {
        Kind kind;
        std::string label;
        ui::ButtonKind style = ui::ButtonKind::secondary;
        bool enabled = true;
        int index = 0;
    };
    void buttons(const std::vector<Button> &items)
    {
        if (items.empty())
            return;
        float bx = x;
        float by = y + 6.0f;
        for (const Button &item : items)
        {
            const float w = ui::button_width(type, item.label, 20.0f, item.style);
            if (bx > x && bx + w > x + width)
            {
                ++row;
                bx = x;
                by += ui::kButtonHeight + 16.0f;
            }
            const ui::Rect r{bx, by, w, ui::kButtonHeight};
            if (list != nullptr)
                ui::button(*list, type, r, item.label, item.style, amount(), item.enabled);
            add(item.kind, item.index, r, item.enabled);
            bx += w + 16.0f;
        }
        ++row;
        y = by + ui::kButtonHeight + 24.0f;
    }
};

// ---- the screen ----

SettingsScreen::SettingsScreen(Context &context) : Screen(context)
{
    nas_keyboard_.style.theme = keyboard_theme();
    nas_keyboard_.style.bindings = hui::ui::KeyboardBindings::standard();
    nas_keyboard_.style.max_length = 1000;
    nas_keyboard_.style.done_label = tr("Done");
    nas_keyboard_.set_bounds({ui::kGutter + 220.0f, kKeyboardTop + 40.0f, 1280.0f, 400.0f});
}

void SettingsScreen::open(Section section, bool content)
{
    section_ = section;
    in_content_ = false;
    confirm_restart_ = false;
    confirm_replace_ = false;
    error_.clear();
    error_setting_ = Setting::none;
    age_ = 0.0f;
    scroll_.snap(0.0f);
    load_draft();
    setting_serial_ = ctx_.store.setting_result().serial;
    ctx_.store.want_settings(true);
    if (section == Section::pairing)
        ctx_.store.refresh_session();
    if (content)
    {
        const std::vector<Control> all = controls();
        if (!all.empty())
        {
            in_content_ = true;
            focus_kind_ = all.front().kind;
            focus_index_ = all.front().index;
        }
    }
}

void SettingsScreen::close()
{
    ctx_.store.want_settings(false);
}

void SettingsScreen::load_draft()
{
    const Snapshot &state = ctx_.store.state();
    draft_ = state.sources.enabled;
    draft_acknowledged_ = state.sources.acknowledged;
    first_setup_ = !state.sources.acknowledged;
    nas_url_.clear();
    nas_edit_ = false;
}

void SettingsScreen::close_nas_editor()
{
    nas_edit_ = false;
}

bool SettingsScreen::update_available() const
{
    const Snapshot &state = ctx_.store.state();
    const std::string running = app_release_version(ctx_.app_version);
    const bool tv = state.have_tv_app && !state.tv_app.latest_version.empty() && !running.empty() &&
                    starter::compare_versions(state.tv_app.latest_version, running) > 0;
    return tv || (state.have_service_update && state.service.update_available);
}

std::vector<SettingsScreen::Control> SettingsScreen::controls(float *height) const
{
    Pane pane(*this, nullptr);
    build(pane);
    if (height != nullptr)
        *height = pane.y - kContentTop;
    return std::move(pane.controls);
}

void SettingsScreen::build(Pane &pane) const
{
    switch (section_)
    {
    case Section::sources:
        build_sources(pane);
        break;
    case Section::storage:
        build_storage(pane);
        break;
    case Section::pairing:
        build_pairing(pane);
        break;
    case Section::catalogue:
        build_catalogue(pane);
        break;
    case Section::console:
        build_console(pane);
        break;
    case Section::updates:
        build_updates(pane);
        break;
    case Section::zero:
        build_zero(pane);
        break;
    case Section::appearance:
        build_appearance(pane);
        break;
    case Section::language:
        build_language(pane);
        break;
    default:
        build_more(pane);
        break;
    }
}

// ---- Download sources (src/Sources.tsx) ----

void SettingsScreen::build_sources(Pane &pane) const
{
    const Snapshot &state = ctx_.store.state();
    const ui::Type &type = ctx_.type;
    const SettingResult &result = ctx_.store.setting_result();
    const bool saving = result.busy && result.setting == Setting::sources;
    pane.heading(state.sources.acknowledged ? tr("Download sources") : tr("Choose your sources"));
    pane.body(tr("Select your download sources. You can change them anytime."));
    pane.gap(8.0f);

    // .source-card: a check and the source's name.
    float cx = pane.x;
    const float cy = pane.y;
    bool browser_only_source = false;
    for (int i = 0; i < static_cast<int>(state.sources.options.size()); ++i)
    {
        const SourceOption &option = state.sources.options[static_cast<std::size_t>(i)];
        const bool on = contains(draft_, option.id);
        browser_only_source = browser_only_source || option.id == "vikingfile" || option.id == "akirabox" || option.id == "datanodes";
        const float w = type.medium.measure(option.label, 21.0f) + 104.0f;
        const ui::Rect r{cx, cy, w, 60.0f};
        if (pane.list != nullptr)
        {
            const float f = pane.amount();
            ui::state_fill(*pane.list, r, 30.0f, f, on);
            const float mx = r.x + 22.0f + 13.0f;
            if (on)
            {
                pane.list->circle(mx, r.cy(), 13.0f, f > 0.5f ? ui::color::night1 : ui::color::action);
                ui::icon_check(*pane.list, mx, r.cy(), 15.0f, f > 0.5f ? ui::color::text : ui::color::night1, 2.4f);
            }
            else
            {
                pane.list->ring(mx, r.cy(), 12.0f, 2.0f,
                                f > 0.5f ? ui::Color::rgb(0x080808, 0.6f) : ui::Color::rgb(0xffffff, 0.55f));
            }
            ui::text(*pane.list, type.medium, option.label, mx + 13.0f + 18.0f, r.cy() + 7.5f,
                     21.0f, ui::state_ink(f, on));
        }
        pane.add(Kind::source, i, r, !saving);
        cx += w + 14.0f;
    }
    if (!state.sources.options.empty())
    {
        ++pane.row;
        pane.y = cy + 60.0f + 18.0f;
    }
    if (browser_only_source)
    {
        if (contains(draft_, "vikingfile"))
            pane.fine(tr("On {provider}, complete any verification and select the site’s {button} button.",
                         {{"provider", "Vikingfile"}, {"button", tr("Download")}}));
        if (contains(draft_, "akirabox"))
            pane.fine(tr("Complete verification on AkiraBox, then wait for Orbit to check the download link."));
        if (contains(draft_, "datanodes"))
            pane.fine(tr("Scroll to the bottom, select Continue to Download, then Free Download. Wait for the countdown and select Start Download."));
    }

    // My NAS: the address of the index.json, typed with the on-screen keyboard.
    if (contains(draft_, "nas"))
    {
        pane.subheading(tr("My NAS index address"));
        const ui::Rect field{pane.x, pane.y, pane.width, 64.0f};
        if (pane.list != nullptr)
        {
            ui::DrawList &list = *pane.list;
            ui::focus_ring(list, field, 18.0f, pane.amount());
            list.rounded_rect(field, 18.0f, ui::Color::rgb(0xffffff, 0.08f));
            const bool typed = !nas_url_.empty();
            const std::string shown =
                typed ? nas_url_
                      : state.sources.nas_configured
                            ? std::string(tr("Saved address kept. Select to change it."))
                            : std::string("https://nas.example.com/orbit-secret/index.json");
            ui::text_fit(list, type.regular, shown, field.x + 24.0f, field.cy() + 7.0f, 20.0f,
                         field.w - 48.0f, typed ? ui::color::text : ui::color::text3);
        }
        pane.add(Kind::nas_url, 0, field, !saving);
        ++pane.row;
        pane.y = field.y + field.h + 10.0f;
        if (state.sources.nas_configured)
        {
            if (!state.sources.nas_error.empty())
                pane.fine(state.sources.nas_host + ": " + nas_message(state.sources.nas_error));
            else
                pane.fine(tr("{host}: {count} games found.",
                             {{"host", state.sources.nas_host},
                              {"count", std::to_string(state.sources.nas_count)}}));
        }
        else
        {
            pane.fine(tr("Enter the https address of the index.json on your NAS."));
        }
        pane.gap(6.0f);
    }

    // .source-notice, then .source-acknowledgement.
    pane.rule();
    pane.subheading(tr("Before you download"));
    pane.body(tr(kNotice), 19.0f);
    pane.gap(6.0f);
    const char *agree = tr("I understand the risks and will only download content I\xE2\x80\x99m "
                           "legally entitled to access and use.");
    const float text_w = pane.width - 100.0f;
    const std::size_t lines = type.regular.font->wrap(agree, 19.0f, text_w).size();
    const ui::Rect box{pane.x, pane.y, pane.width, 44.0f + 28.0f * static_cast<float>(lines)};
    if (pane.list != nullptr)
    {
        const float f = pane.amount();
        ui::state_fill(*pane.list, box, 16.0f, f, draft_acknowledged_);
        const float mx = box.x + 26.0f + 16.0f;
        const float my = box.y + 22.0f + 16.0f;
        if (draft_acknowledged_)
        {
            pane.list->circle(mx, my, 16.0f, f > 0.5f ? ui::color::night1 : ui::color::action);
            ui::icon_check(*pane.list, mx, my, 18.0f, f > 0.5f ? ui::color::text : ui::color::night1, 2.6f);
        }
        else
        {
            pane.list->ring(mx, my, 15.0f, 2.0f,
                            f > 0.5f ? ui::Color::rgb(0x080808, 0.6f) : ui::Color::rgb(0xffffff, 0.55f));
        }
        ui::paragraph(*pane.list, type.regular, agree, box.x + 76.0f, box.y + 22.0f + 21.0f, 19.0f,
                      text_w, 28.0f, ui::state_ink(f, draft_acknowledged_), 4);
    }
    pane.add(Kind::acknowledge, 0, box, !saving);
    ++pane.row;
    pane.y = box.y + box.h + 18.0f;
    if (state.sources.acknowledged)
        pane.fine(tr("Turning off a source hides its download options and pauses its unfinished "
                  "downloads. Saved files stay on your drive. Turn the source back on and resume "
                  "from Downloads when you\xE2\x80\x99re ready."));
    if (error_setting_ == Setting::sources)
        pane.error(error_);
    const bool can_save =
        !saving && draft_acknowledged_ && (state.sources.acknowledged || !draft_.empty());
    pane.buttons({{Kind::save_sources, saving ? tr("Saving\xE2\x80\xA6") : tr("Save sources"),
                   ui::ButtonKind::secondary, can_save}});
    pane.fine(tr("Saved for this console and its paired devices."));
}

// ---- Storage: the default drive ----

void SettingsScreen::build_storage(Pane &pane) const
{
    const Snapshot &state = ctx_.store.state();
    const ui::Type &type = ctx_.type;
    const SettingResult &result = ctx_.store.setting_result();
    const bool saving = result.busy && result.setting == Setting::storage;
    pane.heading(tr("Default drive"));
    pane.body(tr("New downloads start with this drive selected under Save to. Choosing another "
              "drive for a download also makes it the default."));
    pane.gap(10.0f);
    const std::string &preferred = state.system.preferred_storage;
    bool known = preferred.empty();
    for (const Drive &drive : state.drives)
        known = known || drive.id == preferred;

    // Option cards, as the game page's download options.
    const auto card = [&](int index, std::string_view title, std::string_view detail,
                          std::string_view right, std::string_view right_detail, bool chosen)
    {
        const ui::Rect r{pane.x, pane.y, pane.width, 92.0f};
        if (pane.list != nullptr)
        {
            ui::DrawList &list = *pane.list;
            const float f = pane.amount();
            ui::state_fill(list, r, kCardRadius, f, chosen);
            const float mx = r.x + 24.0f + 14.0f;
            if (chosen)
            {
                list.circle(mx, r.cy(), 14.0f, f > 0.5f ? ui::color::night1 : ui::color::action);
                ui::icon_check(list, mx, r.cy(), 16.0f, f > 0.5f ? ui::color::text : ui::color::night1, 2.4f);
            }
            else
            {
                list.ring(mx, r.cy(), 13.5f, 1.0f, f > 0.5f ? ui::Color::rgb(0x080808, 0.6f) : ui::color::text3);
            }
            const float tx = mx + 14.0f + 20.0f;
            const float rx = r.x + r.w - 24.0f;
            // Without a right column, the left text may use the whole card.
            const float fit = right.empty() && right_detail.empty() ? rx - tx : 470.0f;
            ui::text_fit(list, type.medium, title, tx, r.cy() - 6.0f, 21.0f, fit,
                         ui::state_ink(f, chosen));
            ui::text_fit(list, type.regular, detail, tx, r.cy() + 22.0f, 16.0f, fit,
                         ui::state_ink_dim(f));
            ui::text(list, type.medium, right, rx, r.cy() - 6.0f, 19.0f, ui::state_ink(f, chosen),
                     ui::Align::right);
            ui::text(list, type.regular, right_detail, rx, r.cy() + 22.0f, 16.0f, ui::state_ink_dim(f),
                     ui::Align::right);
        }
        pane.add(Kind::drive, index, r, !saving);
        ++pane.row;
        pane.y += r.h + 12.0f;
    };
    card(0, tr("Choose automatically"), tr("The first USB or external drive, otherwise internal storage"),
         "", "", preferred.empty());
    for (int i = 0; i < static_cast<int>(state.drives.size()); ++i)
    {
        const Drive &drive = state.drives[static_cast<std::size_t>(i)];
        const std::string after =
            tr("{size} after queued downloads",
               {{"size", format_bytes(std::max(0.0, drive.projected_free_bytes))}});
        card(i + 1, i18n::place(drive.label), drive.path,
             tr("{size} free", {{"size", format_bytes(drive.free_bytes)}}),
             drive.pending_bytes > 0.0 ? after
                                       : tr("of {size}", {{"size", format_bytes(drive.total_bytes)}}),
             drive.id == preferred);
    }
    if (state.drives.empty())
        pane.body(tr("No writable drives found. Connect a USB drive, then come back here."), 19.0f);
    if (!known)
        pane.notice({}, tr("Your default drive isn\xE2\x80\x99t connected. Until it is, Save to "
                        "starts with the first drive available."));
    if (error_setting_ == Setting::storage)
        pane.error(error_);
    pane.fine(tr("Downloads save inside the drive\xE2\x80\x99s homebrew folder."));
    pane.gap(12.0f);
    pane.rule();
    pane.heading(tr("Simultaneous downloads"));
    pane.body(tr("Choose 1–8 games. One by default. Lowering the limit lets current downloads finish."));
    pane.version_row(tr("Simultaneous downloads"), std::to_string(state.system.concurrent_downloads));
    pane.buttons({{Kind::download_limit_less, tr("Decrease download limit"), ui::ButtonKind::secondary,
                   !result.busy && state.system.concurrent_downloads > 1},
                  {Kind::download_limit_more, tr("Increase download limit"), ui::ButtonKind::secondary,
                   !result.busy && state.system.concurrent_downloads < 8}});
    if (result.setting == Setting::download_limit) pane.error(result.error);
    pane.gap(12.0f);
    pane.rule();
    pane.heading(tr("Compressed downloads"));
    pane.body(state.system.delete_archives
                  ? tr("ZIP, 7z and RAR downloads are extracted on the drive. The archive is deleted once its files are in place.")
                  : tr("ZIP, 7z and RAR downloads are extracted on the drive. The archive is kept next to its files."));
    const bool archives_saving = result.busy && result.setting == Setting::archives;
    pane.buttons({{Kind::archives_toggle,
                   state.system.delete_archives ? tr("Keep archives after extracting")
                                                : tr("Delete archives after extracting"),
                   ui::ButtonKind::secondary, !archives_saving}});
    if (result.setting == Setting::archives && !result.error.empty())
        pane.error(result.error);
}

// ---- Pair a device (src/Pairing.tsx, on the console) ----

void SettingsScreen::build_pairing(Pane &pane) const
{
    const Snapshot &state = ctx_.store.state();
    const ui::Type &type = ctx_.type;
    pane.heading(tr("Pair a phone or computer"));
    pane.body(tr("Browse Orbit and queue downloads from another device on the same network as "
              "this PS5. Downloads still run on this console."));
    pane.gap(16.0f);

    // Two steps, numbered because they are a sequence.
    const auto step = [&](int number, std::string_view title)
    {
        if (pane.list != nullptr)
        {
            pane.list->circle(pane.x + 20.0f, pane.y + 20.0f, 20.0f,
                              ui::Color::rgb(0xffffff, 0.1f));
            char digit[4];
            std::snprintf(digit, sizeof(digit), "%d", number);
            ui::text(*pane.list, type.semibold, digit, pane.x + 20.0f, pane.y + 27.0f, 19.0f,
                     ui::color::text, ui::Align::center);
            ui::text(*pane.list, type.medium, title, pane.x + 60.0f, pane.y + 28.0f, 23.0f,
                     ui::color::text);
        }
        pane.y += 56.0f;
    };
    step(1, tr("Open Orbit in a web browser"));
    const std::string address =
        !state.address.empty() ? "http://" + state.address + ":34177"
                               : tr("Your PS5\xE2\x80\x99s IP address, port {port}", {{"port", "34177"}});
    if (pane.list != nullptr)
        ui::text_fit(*pane.list, type.medium, address, pane.x + 60.0f, pane.y + 36.0f,
                     state.address.empty() ? 28.0f : 40.0f, pane.width - 60.0f, ui::color::text);
    pane.y += 58.0f;
    if (state.address.empty())
    {
        pane.x += 60.0f;
        pane.width -= 60.0f;
        pane.fine(tr("Find it on the PS5 in Settings \xE2\x86\x92 Network \xE2\x86\x92 Connection "
                  "Status."));
        pane.x -= 60.0f;
        pane.width += 60.0f;
    }
    pane.gap(26.0f);
    step(2, tr("Enter this code"));
    std::string code = state.pair_code;
    if (code.size() == 6)
        code.insert(3, " ");
    if (pane.list != nullptr)
        ui::text(*pane.list, type.light, code.empty() ? "\xE2\x80\x94" : code, pane.x + 60.0f,
                 pane.y + 82.0f, 96.0f, ui::color::text);
    pane.y += 118.0f;
    pane.fine(
        tr("The code changes when Orbit restarts. Devices you\xE2\x80\x99ve paired stay paired."));
}

// ---- Game catalogue (src/CatalogueUpdates.tsx) ----

void SettingsScreen::build_catalogue(Pane &pane) const
{
    const Snapshot &state = ctx_.store.state();
    const CatalogueStatus &catalogue = state.catalogue;
    const SettingResult &result = ctx_.store.setting_result();
    const bool sending = result.busy && result.setting == Setting::catalogue;
    pane.heading(tr("Game catalogue"));
    if(state.system.zero_enabled) {
        pane.body("Orbit Zero supplies the catalogue and artwork from your computer. Refresh the catalogue in the desktop app to get new games.");
        pane.version_row("Games",std::to_string(state.games.size()));
        pane.error(state.system.zero_error);
        return;
    }
    pane.body(tr("New games arrive without reinstalling Orbit. Your PS5 checks when Orbit starts "
              "and every six hours, and keeps a saved catalogue for browsing offline."));
    if (state.have_catalogue_status)
    {
        const std::string checked =
            catalogue.checked_at > 0.0
                ? tr("Checked {time}", {{"time", format_age(catalogue.checked_at, unix_seconds())}})
                : std::string(tr("Not checked yet"));
        const std::string line =
            trn(catalogue.game_count, "{count} game", "{count} games") + " \xC2\xB7 " +
            tr("Catalogue {revision}", {{"revision", std::to_string(catalogue.revision)}}) +
            " \xC2\xB7 " + checked;
        pane.gap(4.0f);
        pane.body(line, 20.0f, ui::color::text);
    }
    const int wait = seconds_until(catalogue.check_after);
    std::string label = tr("Refresh catalogue");
    if (sending || catalogue.busy)
        label = tr("Refreshing catalogue\xE2\x80\xA6");
    else if (wait > 0)
        label = tr("Refresh in {seconds} s", {{"seconds", std::to_string(wait)}});
    pane.buttons({{Kind::refresh_catalogue, label, ui::ButtonKind::secondary,
                   catalogue.available && !catalogue.busy && !sending && wait == 0}});
    if (state.have_catalogue_status && !catalogue.available)
        pane.fine(tr("Refreshing works with Orbit running on your PS5. This copy uses its bundled "
                  "catalogue."));
    if (error_setting_ == Setting::catalogue && !error_.empty())
        pane.notice({}, tr(error_));
    else if (!catalogue.error.empty())
        pane.notice({}, tr(catalogue.error));
}

// ---- Console: what depends on the PlayStation the games are for ----

void SettingsScreen::build_console(Pane &pane) const
{
    const Snapshot &state = ctx_.store.state();
    const SettingResult &result = ctx_.store.setting_result();
    pane.heading(tr("Console"));
    pane.body(state.system.ps4_hidden
                  ? tr("PS4 games are hidden. A game that also comes in a PS5 version stays, with its PS5 options.")
                  : tr("PS4 games are shown next to PS5 games."));
    const bool ps4_saving = result.busy && result.setting == Setting::ps4;
    pane.buttons({{Kind::ps4_toggle, state.system.ps4_hidden ? tr("Show PS4 games") : tr("Hide PS4 games"),
                   ui::ButtonKind::secondary, !ps4_saving}});
    if (result.setting == Setting::ps4 && !result.error.empty())
        pane.error(result.error);
    pane.gap(12.0f);
    pane.rule();
    pane.heading(tr("Installing downloaded packages"));
    pane.body(state.system.install_pkgs
                  ? tr("A downloaded .pkg is installed on your PS5 once Orbit has checked it.")
                  : tr("A downloaded .pkg stays in the homebrew folder. Orbit does not install it."));
    const bool install_saving = result.busy && result.setting == Setting::install_pkgs;
    pane.buttons({{Kind::install_pkgs_toggle,
                   state.system.install_pkgs ? tr("Stop installing packages after download")
                                             : tr("Install packages after download"),
                   ui::ButtonKind::secondary, !install_saving}});
    if (result.setting == Setting::install_pkgs && !result.error.empty())
        pane.error(result.error);
}

// ---- Updates: this app (src/TvApp.tsx) and the download service (src/Updates.tsx) ----

void SettingsScreen::build_updates(Pane &pane) const
{
    const Snapshot &state = ctx_.store.state();
    const SettingResult &result = ctx_.store.setting_result();
    const double now = unix_seconds();
    pane.heading(tr("Updates"));
    pane.body(tr("Orbit has two parts, updated separately: this TV app, and the download service "
              "that keeps running after you close it."));
    pane.gap(10.0f);

    // ---- the TV app ----
    const TvAppStatus &tv = state.tv_app;
    const std::string running = app_release_version(ctx_.app_version);
    const bool tv_sending = result.busy && (result.setting == Setting::tv_check ||
                                            result.setting == Setting::tv_install);
    pane.begin_card(tr("TV app"), tr("Orbit Store for TV, the app you\xE2\x80\x99re using now."));
    pane.version_row(tr("This app"), running.empty() ? tr("Unknown") : running);
    const bool tv_newer = !tv.latest_version.empty() && !running.empty() &&
                          starter::compare_versions(tv.latest_version, running) > 0;
    const bool tv_waiting = tv.installed == "orbit" && !tv.installed_version.empty() &&
                            !running.empty() &&
                            starter::compare_versions(tv.installed_version, running) > 0;
    if (tv_waiting)
        pane.version_row(tr("Installed, opens next time"), tv.installed_version, ui::color::ok);
    if (!tv.latest_version.empty())
        pane.version_row(tr("Latest release"),
                         tv.latest_version + " \xC2\xB7 " + format_bytes(tv.size));
    if (!state.have_tv_app || !tv.available)
    {
        // Orbit before 0.6.0 has no TV app installer at all.
        pane.fine(state.have_tv_app ? tr("Updates work with Orbit running on your PS5.")
                  : state.have_service_update
                      ? tr("Checking for TV app updates needs Orbit 0.6.0 or later. Update the "
                        "download service below.")
                      : tr("Loading\xE2\x80\xA6"));
    }
    else
    {
        const int wait = seconds_until(tv.retry_at);
        const int check_wait = std::max(wait, seconds_until(tv.check_after));
        const bool fresh = check_fresh(tv.checked_at, now);
        const bool disabled = tv_sending || tv.busy;
        const bool installable = tv.installed == "orbit";
        std::vector<Pane::Button> row;
        row.push_back({Kind::tv_check,
                       tv.phase == "checking" ? tr("Checking\xE2\x80\xA6")
                       : check_wait > 0 ? tr("Check again in {seconds} s",
                                             {{"seconds", std::to_string(check_wait)}})
                                              : tr("Check for updates"),
                       ui::ButtonKind::secondary, !disabled && check_wait == 0});
        // Orbit updates the copy in /data/homebrew: its own, or one the owner
        // placed (after confirming). With none there, this app runs from
        // somewhere else, and installing would add a second copy.
        if (tv_newer && !tv_waiting && (tv.installed == "orbit" || tv.installed == "manual"))
        {
            const bool installing = tv.busy && tv.phase != "checking";
            const char *label = installing    ? tr("Installing\xE2\x80\xA6")
                                : installable ? tr("Update TV app")
                                              : tr("Replace with this release");
            row.push_back({installable ? Kind::tv_install : Kind::tv_replace, label,
                           ui::ButtonKind::secondary,
                           !disabled && wait == 0 && fresh && !confirm_replace_});
        }
        pane.buttons(row);
        std::string status;
        if (tv.phase == "downloading")
            status = tr("Downloading the TV app: {received} of {size}",
                        {{"received", format_bytes(tv.received)}, {"size", format_bytes(tv.size)}});
        else if (tv.phase == "verifying")
            status = tr("Checking the download\xE2\x80\xA6");
        else if (tv.phase == "installing")
            status = tr("Installing\xE2\x80\xA6");
        else if (tv.phase == "checked" && !tv_newer && !tv_waiting)
            status = tr("You\xE2\x80\x99re using the latest version.");
        else if (!tv.latest_version.empty() && !fresh && !tv.busy && tv_newer)
            status = tr("Check for updates again before installing.");
        if (!status.empty())
            pane.fine(status);
        if (confirm_replace_)
        {
            pane.notice(tr("Replace your copy?"),
                        tr("The TV app in /data/homebrew is a copy you placed yourself. Orbit "
                           "replaces it with release {version}. This app keeps running until you "
                           "close it.",
                           {{"version", tv.latest_version}}));
            pane.buttons({{Kind::tv_replace, tr("Replace"), ui::ButtonKind::secondary,
                           !disabled && wait == 0 && fresh, 1},
                          {Kind::tv_keep, tr("Keep my copy")}});
        }
        if (tv_waiting || (tv.phase == "installed" && tv_newer))
        {
            const std::string version = tv_waiting ? tv.installed_version : tv.latest_version;
            pane.notice(tr("TV app {version} is installed", {{"version", version}}),
                        tr("Close Orbit Store (press the PS button, then Close Application) and "
                           "open it again from your Games row. If version {running} still opens, "
                           "restart your PS5.",
                           {{"running", running}}));
        }
        if (tv.installed == "none" && tv_newer)
            pane.notice({}, tr("Orbit updates the TV app in /data/homebrew, and this copy is "
                               "somewhere else. Download release {version} from Orbit\xE2\x80\x99s "
                               "GitHub releases and replace your copy yourself.",
                               {{"version", tv.latest_version}}));
        if (tv.installed == "folder")
            pane.notice({}, tr("This app is installed as the folder /data/homebrew/PPSA99177. Keep "
                            "updating that copy yourself, or remove the folder so Orbit can "
                            "install and update it."));
        if (tv.installed == "blocked")
            pane.notice({}, tr("Something other than the TV app is at "
                            "/data/homebrew/PPSA99177.ffpkg. Move it away to install updates "
                            "here."));
        if (error_setting_ == Setting::tv_check || error_setting_ == Setting::tv_install)
            pane.error(error_);
        else
            pane.error(tv.error);
    }
    pane.end_card();

    // ---- the download service ----
    const ServiceUpdate &service = state.service;
    const bool service_sending = result.busy && (result.setting == Setting::service_check ||
                                                 result.setting == Setting::service_install ||
                                                 result.setting == Setting::service_restart);
    pane.begin_card(tr("Download service"),
                    tr("Orbit, which downloads your games and keeps running after you close this "
                    "app."));
    const std::string &running_service =
        !service.running_version.empty() ? service.running_version : state.system.version;
    pane.version_row(tr("Running"), running_service.empty() ? tr("Unknown") : running_service);
    if (!service.saved_version.empty() && service.saved_version != running_service)
        pane.version_row(tr("Starts next time"), service.saved_version, ui::color::ok);
    if (!service.latest_version.empty())
        pane.version_row(tr("Latest release"), service.latest_version);

    const Restart restart = state.restart;
    if (restart == Restart::stopping || restart == Restart::starting)
    {
        pane.notice(restart == Restart::stopping ? tr("Stopping the download service\xE2\x80\xA6")
                                                 : tr("Starting the download service\xE2\x80\xA6"),
                    tr("Downloads are paused. This takes a few seconds."));
    }
    else if (restart == Restart::manual || restart == Restart::failed)
    {
        std::string text =
            restart == Restart::manual
                ? tr("This app can\xE2\x80\x99t start Orbit here. Run {path} from your payload "
                     "manager. This screen reconnects by itself.",
                     {{"path", kSavedElf}})
                : tr("Run {path} from your payload manager, or try again.", {{"path", kSavedElf}});
        if (!state.restart_detail.empty())
            text += " (" + state.restart_detail + ")";
        pane.notice(restart == Restart::manual ? tr("Start Orbit from your payload manager")
                                               : tr("Orbit didn\xE2\x80\x99t restart"),
                    text, restart == Restart::failed);
        if (ctx_.store.can_start())
            pane.buttons({{Kind::restart_retry, tr("Try again"), ui::ButtonKind::secondary}});
    }
    else if (!state.have_service_update || !service.available)
    {
        pane.fine(tr("Updates work with Orbit running on your PS5."));
    }
    else
    {
        const int wait = seconds_until(service.retry_at);
        const int check_wait = std::max(wait, seconds_until(service.check_after));
        const bool fresh = check_fresh(service.checked_at, now);
        const bool disabled = service_sending || service.busy;
        std::vector<Pane::Button> row;
        row.push_back({Kind::service_check,
                       service.phase == "checking" ? tr("Checking\xE2\x80\xA6")
                       : check_wait > 0 ? tr("Check again in {seconds} s",
                                             {{"seconds", std::to_string(check_wait)}})
                                        : tr("Check for updates"),
                       ui::ButtonKind::secondary, !disabled && check_wait == 0});
        const bool saved_latest = !service.latest_version.empty() &&
                                  service.saved_version == service.latest_version &&
                                  service.restart_required;
        // Orbit refuses an older release; the same one reinstalls.
        if (!service.latest_version.empty() && !saved_latest &&
            starter::compare_versions(service.latest_version, running_service) >= 0)
        {
            const bool installing = service.phase == "downloading" || service.phase == "saving";
            row.push_back(
                {Kind::service_install,
                 installing                                  ? tr("Installing\xE2\x80\xA6")
                 : service.latest_version == running_service ? tr("Reinstall release")
                                                             : tr("Install update"),
                 ui::ButtonKind::secondary,
                 !disabled && wait == 0 && fresh});
        }
        if (!confirm_restart_)
            row.push_back(
                {Kind::service_restart, tr("Restart download service"),
                 ui::ButtonKind::secondary,
                 !disabled});
        pane.buttons(row);
        std::string status;
        if (service.phase == "downloading")
            status = tr("Downloading Orbit: {size}", {{"size", format_bytes(service.received)}});
        else if (service.phase == "saving")
            status = tr("Saving the verified release\xE2\x80\xA6");
        else if (wait > 0 && !service.busy)
            status = tr("Please wait {seconds} s before another request.",
                        {{"seconds", std::to_string(wait)}});
        else if (service.phase == "checked" && service.latest_version == running_service)
            status = tr("You\xE2\x80\x99re running the latest version. You can reinstall it if "
                     "needed.");
        else if (!service.latest_version.empty() && !fresh && !service.busy && !saved_latest)
            status = tr("Check for updates again before installing.");
        if (!status.empty())
            pane.fine(status);
        if (service.restart_required && !confirm_restart_)
            pane.notice(tr("Restart to finish updating"),
                        service.saved_version.empty()
                            ? std::string(tr("Orbit\xE2\x80\x99s update is saved. It runs the next "
                                             "time Orbit starts. Restart the download service to "
                                             "use it now."))
                            : tr("Orbit {version} is saved. It runs the next time Orbit starts. "
                                 "Restart the download service to use it now.",
                                 {{"version", service.saved_version}}));
        if (confirm_restart_)
        {
            pane.notice(tr("Restart the download service?"),
                        tr("Active downloads pause. This app stops Orbit and starts it again, then "
                        "you can resume your downloads from Downloads."));
            pane.buttons(
                {{Kind::restart_confirm, tr("Restart now"), ui::ButtonKind::secondary, !disabled},
                 {Kind::restart_cancel, tr("Not now")}});
        }
        if (error_setting_ == Setting::service_check ||
            error_setting_ == Setting::service_install ||
            error_setting_ == Setting::service_restart)
            pane.error(error_);
        else
            pane.error(service.error);
    }
    pane.end_card();
}

// ---- More: what stays in the browser version ----

void SettingsScreen::build_zero(Pane &pane) const
{
    const auto &system=ctx_.store.state().system;
    pane.heading("Orbit Zero");
    pane.body("Use your computer to download games and transfer them to this PS5 over Wi-Fi or Ethernet.");
    pane.fine("Open Orbit Zero on your computer, connect to this console and enter the PS5 pairing code. Then enable it here.");
    if(system.zero_configured) {
        pane.version_row("Computer",system.zero_address);
        pane.version_row("Orbit Zero",system.zero_enabled ? "Enabled" : "Off");
        if(system.zero_enabled) pane.body(system.zero_preference=="prefer-ps5" ?
            "Your preference uses the PS5 when the selected source is reachable, otherwise your computer. Running downloads keep their route." :
            "Downloads run on your computer and transfer to your selected PS5 drive. Keep Orbit Zero open.");
        pane.error(system.zero_error);
        pane.buttons({{Kind::zero_toggle,system.zero_enabled?"Turn off Orbit Zero":"Enable Orbit Zero",ui::ButtonKind::secondary,!ctx_.store.setting_result().busy}});
        if(ctx_.store.setting_result().setting==Setting::zero) pane.error(ctx_.store.setting_result().error);
    } else pane.body("No computer paired. Use Console setup in the desktop app to connect.");
}

void SettingsScreen::build_appearance(Pane &pane) const
{
    const ui::Type &type = ctx_.type;
    pane.heading(tr("Personalisation"));
    pane.subheading(tr("Sounds"));
    pane.body(ctx_.sounds_on ? tr("Menu sounds are on: you hear a sound when you move and select.")
                             : tr("Menu sounds are off. The app is silent."));
    pane.buttons({{Kind::sounds_toggle, ctx_.sounds_on ? tr("Turn off sounds") : tr("Turn on sounds"),
                   ui::ButtonKind::secondary}});
    if (ctx_.store.setting_result().setting == Setting::sounds)
        pane.error(ctx_.store.setting_result().error);
    pane.gap(12.0f);
    pane.rule();
    pane.subheading(tr("Background"));
    pane.body(tr("Choose the background of the app. Game artwork still shows over it."));
    pane.gap(10.0f);
    for (std::size_t i = 0; i < appearance::count(); ++i)
    {
        const appearance::Preset &preset = appearance::preset(i);
        const bool chosen = i == ctx_.appearance;
        const ui::Rect r{pane.x, pane.y, pane.width, 92.0f};
        if (pane.list != nullptr)
        {
            ui::DrawList &list = *pane.list;
            const float f = pane.amount();
            ui::state_fill(list, r, kCardRadius, f, chosen);
            const float mx = r.x + 24.0f + 14.0f;
            if (chosen)
            {
                list.circle(mx, r.cy(), 14.0f, f > 0.5f ? ui::color::night1 : ui::color::action);
                ui::icon_check(list, mx, r.cy(), 16.0f, f > 0.5f ? ui::color::text : ui::color::night1, 2.4f);
            }
            else
                list.ring(mx, r.cy(), 13.5f, 1.0f, f > 0.5f ? ui::Color::rgb(0x080808, 0.6f) : ui::color::text3);
            const float tx = mx + 14.0f + 20.0f;
            const float rx = r.x + r.w - 24.0f;
            ui::text_fit(list, type.medium, tr(preset.name), tx, r.cy() - 6.0f, 21.0f, 470.0f, ui::state_ink(f, chosen));
            ui::text_fit(list, type.regular, tr(preset.about), tx, r.cy() + 22.0f, 16.0f, 470.0f,
                         ui::state_ink_dim(f));
            if (preset.animated)
                ui::text(list, type.medium, tr("Animated"), rx, r.cy() + 7.0f, 19.0f, ui::state_ink_dim(f),
                         ui::Align::right);
        }
        pane.add(Kind::appearance, static_cast<int>(i), r, true);
        ++pane.row;
        pane.y += r.h + 12.0f;
    }
    if (ctx_.store.setting_result().setting == Setting::appearance)
        pane.error(ctx_.store.setting_result().error);
    pane.fine(tr("Saved on this console. Animated backgrounds use a little more graphics power."));
}

void SettingsScreen::build_language(Pane &pane) const
{
    const ui::Type &type = ctx_.type;
    pane.heading(tr("Language"));
    pane.subheading(tr("Keyboard"));
    pane.body(tr("The on-screen keyboard\xE2\x80\x99s letters. Automatic uses AZERTY when the app is in French."));
    pane.gap(10.0f);
    {
        const char *names[] = {tr("Automatic"), "QWERTY", "AZERTY"};
        constexpr float kGap = 12.0f;
        const float w = (pane.width - 2.0f * kGap) / 3.0f;
        const std::string &kb = ctx_.keyboard_choice;
        for (int i = 0; i < 3; ++i)
        {
            const bool chosen = i == 0 ? kb != "qwerty" && kb != "azerty" : kb == (i == 1 ? "qwerty" : "azerty");
            const ui::Rect r{pane.x + static_cast<float>(i) * (w + kGap), pane.y, w, 76.0f};
            if (pane.list != nullptr)
            {
                ui::DrawList &list = *pane.list;
                const float f = pane.amount();
                ui::state_fill(list, r, kCardRadius, f, chosen);
                const float mx = r.x + 24.0f + 14.0f;
                if (chosen)
                {
                    list.circle(mx, r.cy(), 14.0f, f > 0.5f ? ui::color::night1 : ui::color::action);
                    ui::icon_check(list, mx, r.cy(), 16.0f, f > 0.5f ? ui::color::text : ui::color::night1, 2.4f);
                }
                else
                    list.ring(mx, r.cy(), 13.5f, 1.0f, f > 0.5f ? ui::Color::rgb(0x080808, 0.6f) : ui::color::text3);
                ui::text_fit(list, type.medium, names[i], mx + 34.0f, r.cy() + 7.0f, 21.0f, w - 90.0f,
                             ui::state_ink(f, chosen));
            }
            pane.add(Kind::keyboard, i, r, true);
        }
        ++pane.row;
        pane.y += 76.0f + 10.0f;
    }
    pane.gap(12.0f);
    pane.rule();
    pane.subheading(tr("App language"));
    pane.body(tr("Choose the language of the app. Automatic follows the console\xE2\x80\x99s language."));
    pane.gap(10.0f);
    const std::string_view system = i18n::resolve({}, ctx_.system_language);
    const bool automatic = ctx_.language_choice.empty() || ctx_.language_choice == "auto" ||
                           !i18n::supported(ctx_.language_choice);
    for (std::size_t i = 0; i <= i18n::choice_count(); ++i)
    {
        const bool chosen = i == 0 ? automatic : !automatic && ctx_.language_choice == i18n::choice(i - 1).code;
        const ui::Rect r{pane.x, pane.y, pane.width, 76.0f};
        if (pane.list != nullptr)
        {
            ui::DrawList &list = *pane.list;
            const float f = pane.amount();
            ui::state_fill(list, r, kCardRadius, f, chosen);
            const float mx = r.x + 24.0f + 14.0f;
            if (chosen)
            {
                list.circle(mx, r.cy(), 14.0f, f > 0.5f ? ui::color::night1 : ui::color::action);
                ui::icon_check(list, mx, r.cy(), 16.0f, f > 0.5f ? ui::color::text : ui::color::night1, 2.4f);
            }
            else
                list.ring(mx, r.cy(), 13.5f, 1.0f, f > 0.5f ? ui::Color::rgb(0x080808, 0.6f) : ui::color::text3);
            const float tx = mx + 14.0f + 20.0f;
            if (i == 0)
            {
                std::string now;
                for (std::size_t k = 0; k < i18n::choice_count(); ++k)
                    if (i18n::choice(k).code == system)
                        now = i18n::choice(k).name;
                ui::text_fit(list, type.medium, tr("Automatic"), tx, r.cy() - 6.0f, 21.0f, 700.0f,
                             ui::state_ink(f, chosen));
                ui::text_fit(list, type.regular, now, tx, r.cy() + 20.0f, 16.0f, 700.0f, ui::state_ink_dim(f));
            }
            else
            {
                ui::text_fit(list, type.medium, i18n::choice(i - 1).name, tx, r.cy() + 7.0f, 21.0f, 700.0f,
                             ui::state_ink(f, chosen));
            }
        }
        pane.add(Kind::language, static_cast<int>(i), r, true);
        ++pane.row;
        pane.y += r.h + 10.0f;
    }
    if (ctx_.store.setting_result().setting == Setting::language || ctx_.store.setting_result().setting == Setting::keyboard)
        pane.error(ctx_.store.setting_result().error);
}

void SettingsScreen::build_more(Pane &pane) const
{
    const Snapshot &state = ctx_.store.state();
    pane.heading(tr("More settings"));
    pane.body(tr("Auto-start, payload managers and diagnostics are in the browser version of "
              "Orbit."));
    pane.fine(tr("On this PS5, the browser version is the Orbit Store icon in your Media area. On a "
              "phone or computer, pair it under Pair a device."));
    if (ctx_.open_browser)
        pane.buttons({{Kind::open_browser, tr("Open browser version"), ui::ButtonKind::secondary}});
    pane.gap(12.0f);
    pane.rule();
    const std::string running = app_release_version(ctx_.app_version);
    pane.version_row(tr("TV app"), running.empty() ? tr("Unknown") : running);
    pane.version_row(tr("Download service"),
                     state.system.version.empty() ? tr("Unknown") : state.system.version);
}

// ---- input ----

void SettingsScreen::activate(const Control &control, Intent &intent, Feedback &feedback)
{
    const Snapshot &state = ctx_.store.state();
    feedback.play(hui::audio::Cue::select);
    switch (control.kind)
    {
    case Kind::nas_url:
        nas_edit_ = true;
        if (nas_url_.empty())
            nas_url_ = "https://";
        nas_keyboard_.enter();
        nas_keyboard_.set_length(static_cast<int>(nas_url_.size()));
        feedback.play(hui::audio::Cue::modal_open);
        break;
    case Kind::sounds_toggle:
        ctx_.sounds_on = !ctx_.sounds_on; // at once; the service keeps it for next time
        ctx_.store.set_sounds_off(!ctx_.sounds_on);
        break;
    case Kind::appearance:
        // Shown at once, kept by the download service (the app itself cannot write to /data).
        ctx_.appearance = static_cast<std::size_t>(control.index);
        error_.clear();
        appearance::save(ctx_.appearance);
        ctx_.store.set_appearance(appearance::preset(ctx_.appearance).id);
        break;
    case Kind::keyboard:
        ctx_.keyboard_choice = control.index == 1 ? "qwerty" : control.index == 2 ? "azerty" : "auto";
        error_.clear();
        ctx_.store.set_keyboard(ctx_.keyboard_choice);
        break;
    case Kind::language:
    {
        // Shown at once, kept by the download service (the app itself cannot write to /data).
        const std::string saved = control.index == 0 ? std::string("auto")
                                                     : std::string(i18n::choice(static_cast<std::size_t>(control.index) - 1).code);
        ctx_.language_choice = saved;
        error_.clear();
        if (i18n::apply(ctx_.language_dir, saved, ctx_.system_language))
            ctx_.store.language_changed();
        ctx_.store.set_language(saved);
        break;
    }
    case Kind::zero_toggle:
        error_.clear();ctx_.store.set_zero(!state.system.zero_enabled);break;
    case Kind::download_limit_less:
    case Kind::download_limit_more:
        if (!ctx_.store.setting_result().busy) {
            const int limit = state.system.concurrent_downloads + (control.kind == Kind::download_limit_more ? 1 : -1);
            if (limit >= 1 && limit <= 8) ctx_.store.set_download_limit(limit);
        }
        break;
    case Kind::ps4_toggle:
        error_.clear();
        ctx_.store.set_ps4_hidden(!state.system.ps4_hidden);
        break;
    case Kind::install_pkgs_toggle:
        error_.clear();
        ctx_.store.set_install_pkgs(!state.system.install_pkgs);
        break;
    case Kind::archives_toggle:
        error_.clear();
        ctx_.store.set_delete_archives(!state.system.delete_archives);
        break;
    case Kind::source:
    {
        if (control.index >= static_cast<int>(state.sources.options.size()))
            break;
        const std::string &id = state.sources.options[static_cast<std::size_t>(control.index)].id;
        const auto at = std::find(draft_.begin(), draft_.end(), id);
        if (at != draft_.end())
            draft_.erase(at);
        else
            draft_.push_back(id);
        break;
    }
    case Kind::acknowledge:
        draft_acknowledged_ = !draft_acknowledged_;
        break;
    case Kind::save_sources:
    {
        error_.clear();
        // Only a typed address is sent; "https://" alone is the untouched prefix.
        std::string address = nas_url_;
        while (!address.empty() && address.back() == ' ')
            address.pop_back();
        std::optional<std::string> nas;
        if (!address.empty() && address != "https://")
            nas = address;
        ctx_.store.save_sources(draft_, state.sources.notice_version, nas);
        break;
    }
        break;
    case Kind::drive:
    {
        const std::string id =
            control.index > 0 && control.index <= static_cast<int>(state.drives.size())
                ? state.drives[static_cast<std::size_t>(control.index - 1)].id
                : std::string();
        if (id != state.system.preferred_storage)
        {
            error_.clear();
            ctx_.store.set_preferred_storage(id);
        }
        break;
    }
    case Kind::refresh_catalogue:
        error_.clear();
        ctx_.store.refresh_catalogue();
        break;
    case Kind::tv_check:
        error_.clear();
        ctx_.store.check_tv_app();
        break;
    case Kind::tv_install:
        error_.clear();
        ctx_.store.install_tv_app(state.tv_app.latest_version, state.tv_app.checksum, false);
        break;
    case Kind::tv_replace:
        if (control.index == 0)
        {
            confirm_replace_ = true;
            focus_kind_ = Kind::tv_replace;
            focus_index_ = 1;
            feedback.play(hui::audio::Cue::modal_open);
        }
        else
        {
            error_.clear();
            ctx_.store.install_tv_app(state.tv_app.latest_version, state.tv_app.checksum, true);
        }
        break;
    case Kind::tv_keep:
        confirm_replace_ = false;
        focus_kind_ = Kind::tv_check;
        focus_index_ = 0;
        break;
    case Kind::service_check:
        error_.clear();
        ctx_.store.check_service_update();
        break;
    case Kind::service_install:
        error_.clear();
        ctx_.store.install_service_update(state.service.latest_version, state.service.checksum);
        break;
    case Kind::service_restart:
        confirm_restart_ = true;
        focus_kind_ = Kind::restart_cancel;
        focus_index_ = 0;
        feedback.play(hui::audio::Cue::modal_open);
        break;
    case Kind::restart_confirm:
        error_.clear();
        ctx_.store.restart_service();
        break;
    case Kind::restart_cancel:
        confirm_restart_ = false;
        focus_kind_ = Kind::service_restart;
        focus_index_ = 0;
        break;
    case Kind::restart_retry:
        ctx_.store.start_orbit();
        break;
    case Kind::open_browser:
        feedback.play(hui::audio::Cue::launch);
        if (!ctx_.open_browser || !ctx_.open_browser())
            intent.toast = tr("The browser could not be opened.");
        break;
    }
}

Intent SettingsScreen::update(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    Intent intent;
    age_ += dt;
    const SettingResult &result = ctx_.store.setting_result();
    if (result.serial != setting_serial_ && !result.busy)
    {
        setting_serial_ = result.serial;
        error_ = result.error;
        error_setting_ = result.setting;
        if (error_.empty())
        {
            switch (result.setting)
            {
            case Setting::sources:
                intent.toast = tr("Sources saved");
                if (first_setup_)
                {
                    // Setting up is done: on to the games.
                    intent.kind = Intent::Kind::show_tab;
                    intent.tab = tab::discover;
                }
                load_draft();
                break;
            case Setting::storage:
                intent.toast = tr("Default drive saved");
                break;
            case Setting::service_restart:
                confirm_restart_ = false;
                break;
            case Setting::tv_install:
                confirm_replace_ = false;
                break;
            default:
                break;
            }
        }
    }

    // The My NAS address: the keyboard takes every button while it is open.
    nas_keyboard_.set_azerty(ctx_.azerty);
    nas_keyboard_.style.space_label = tr("Space");
    nas_keyboard_.style.done_label = tr("Done");
    nas_keyboard_.set_active(nas_edit_);
    nas_keyboard_.update(dt);
    nas_keyboard_in_.target = nas_edit_ ? 1.0f : 0.0f;
    nas_keyboard_in_.update(dt, 14.0f);
    if (focused && nas_edit_)
    {
        const hui::ui::Event event = nas_keyboard_.handle(input, feedback);
        for (int i = 0; i < nas_keyboard_.erased() && !nas_url_.empty(); ++i)
        {
            std::size_t pos = nas_url_.size() - 1;
            while (pos > 0 && (static_cast<unsigned char>(nas_url_[pos]) & 0xc0) == 0x80)
                --pos;
            nas_url_.resize(pos);
        }
        if (nas_url_.size() + nas_keyboard_.typed().size() <= 1000)
            nas_url_ += nas_keyboard_.typed();
        nas_keyboard_.set_length(static_cast<int>(nas_url_.size()));
        if (event == hui::ui::Event::activated || event == hui::ui::Event::cancelled)
        {
            close_nas_editor();
            feedback.play(hui::audio::Cue::modal_close);
        }
        return intent;
    }

    float height = 0.0f;
    const std::vector<Control> all = controls(&height);
    if (all.empty())
        in_content_ = false;
    int at = -1;
    for (int i = 0; i < static_cast<int>(all.size()); ++i)
    {
        if (all[static_cast<std::size_t>(i)].kind == focus_kind_ &&
            all[static_cast<std::size_t>(i)].index == focus_index_)
            at = i;
    }
    if (at < 0)
    {
        // The focused control went away (a button whose job is done): the
        // nearest one, on the same row if there is one.
        at = 0;
        float best = 1e9f;
        for (int i = 0; i < static_cast<int>(all.size()); ++i)
        {
            const Control &c = all[static_cast<std::size_t>(i)];
            const float d = 10000.0f * static_cast<float>(std::abs(c.row - last_row_)) +
                            std::fabs(c.rect.cx() - last_x_);
            if (d < best)
            {
                best = d;
                at = i;
            }
        }
    }

    if (focused && intent.kind == Intent::Kind::none)
    {
        if (!in_content_)
        {
            if (input.is_pressed(Action::back))
            {
                intent.kind = Intent::Kind::close;
                feedback.play(hui::audio::Cue::modal_close);
                return intent;
            }
            if (input.nav == Direction::up || input.nav == Direction::down)
            {
                const int next =
                    static_cast<int>(section_) + (input.nav == Direction::down ? 1 : -1);
                if (next >= 0 && next < kSectionCount)
                {
                    section_ = static_cast<Section>(next);
                    confirm_restart_ = false;
                    confirm_replace_ = false;
                    error_.clear();
                    error_setting_ = Setting::none;
                    load_draft();
                    if (section_ == Section::pairing)
                        ctx_.store.refresh_session();
                    scroll_.target = 0.0f;
                    feedback.play(hui::audio::Cue::focus);
                    return intent;
                }
                refuse(feedback, input);
            }
            else if (input.nav == Direction::right || input.is_pressed(Action::confirm))
            {
                if (all.empty())
                {
                    refuse(feedback, input);
                }
                else
                {
                    in_content_ = true;
                    at = 0;
                    feedback.play(hui::audio::Cue::focus);
                }
            }
            else if (input.nav == Direction::left)
            {
                refuse(feedback, input);
            }
        }
        else
        {
            const Control &here = all[static_cast<std::size_t>(at)];
            int next = at;
            if (input.is_pressed(Action::back))
            {
                if (confirm_restart_ || confirm_replace_)
                {
                    focus_kind_ = confirm_restart_ ? Kind::service_restart : Kind::tv_check;
                    focus_index_ = 0;
                    confirm_restart_ = false;
                    confirm_replace_ = false;
                }
                else
                {
                    in_content_ = false;
                }
                feedback.play(hui::audio::Cue::back);
                return intent;
            }
            if (input.nav == Direction::left || input.nav == Direction::right)
            {
                next = at + (input.nav == Direction::right ? 1 : -1);
                const bool same_row = next >= 0 && next < static_cast<int>(all.size()) &&
                                      all[static_cast<std::size_t>(next)].row == here.row;
                if (!same_row)
                {
                    next = -1;
                    if (input.nav == Direction::left)
                    {
                        // Off the left edge: back to the sections.
                        in_content_ = false;
                        feedback.play(hui::audio::Cue::focus);
                        return intent;
                    }
                }
            }
            else if (input.nav == Direction::up || input.nav == Direction::down)
            {
                // The nearest control, by centre, on the neighbouring row.
                const int row = here.row + (input.nav == Direction::down ? 1 : -1);
                next = -1;
                float best = 1e9f;
                for (int i = 0; i < static_cast<int>(all.size()); ++i)
                {
                    const Control &c = all[static_cast<std::size_t>(i)];
                    if (c.row != row)
                        continue;
                    const float d = std::fabs(c.rect.cx() - here.rect.cx());
                    if (d < best)
                    {
                        best = d;
                        next = i;
                    }
                }
            }
            if (input.nav != Direction::none)
            {
                if (next >= 0)
                {
                    at = next;
                    feedback.play(hui::audio::Cue::focus);
                }
                else
                {
                    refuse(feedback, input);
                }
            }
            else if (input.is_pressed(Action::confirm))
            {
                if (here.enabled)
                {
                    focus_kind_ = here.kind;
                    focus_index_ = here.index;
                    activate(here, intent, feedback);
                    return intent;
                }
                refuse(feedback, input);
            }
        }
    }
    if (!all.empty())
    {
        const Control &here = all[static_cast<std::size_t>(at)];
        focus_kind_ = here.kind;
        focus_index_ = here.index;
        last_row_ = here.row;
        last_x_ = here.rect.cx();
    }

    // ---- animation and scrolling ----
    focus_amount_.resize(all.size(), 0.0f);
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        const float target = focused && in_content_ && static_cast<int>(i) == at ? 1.0f : 0.0f;
        focus_amount_[i] += (target - focus_amount_[i]) * std::min(1.0f, dt * 16.0f);
    }
    for (int i = 0; i < kSectionCount; ++i)
    {
        const float target =
            focused && !in_content_ && i == static_cast<int>(section_) ? 1.0f : 0.0f;
        nav_focus_[i] += (target - nav_focus_[i]) * std::min(1.0f, dt * 16.0f);
    }
    const float limit = std::max(0.0f, kContentTop + height - (kViewBottom - 24.0f));
    float target = scroll_.target;
    if (in_content_ && !all.empty())
    {
        const Control &here = all[static_cast<std::size_t>(at)];
        if (here.row == all.front().row && all.front().row != all.back().row)
            target = 0.0f; // the first row shows the heading and text above it too
        else if (here.row == all.back().row)
            target = limit; // the last row shows what follows it too
        else if (here.rect.y - target < kViewTop + 60.0f)
            target = here.rect.y - (kViewTop + 60.0f);
        else if (here.rect.y + here.rect.h - target > kViewBottom - 40.0f)
            target = here.rect.y + here.rect.h - (kViewBottom - 40.0f);
    }
    else if (!in_content_)
    {
        target = 0.0f;
    }
    scroll_.target = std::clamp(target, 0.0f, limit);
    scroll_.update(dt, 12.0f);
    return intent;
}

// ---- drawing ----

void SettingsScreen::draw(Frame &frame, bool focused) const
{
    (void)focused;
    hui::gfx::DrawList &list = frame.scene;
    const ui::Type &type = ctx_.type;
    const float in = std::clamp(age_ / 0.3f, 0.0f, 1.0f);
    list.push_opacity(in);
    ui::text(list, type.semibold, tr("App settings"), ui::kGutter, kTitleBaseline, 60.0f, ui::color::text);

    // The sections, as pills: the current one pale, as the tabs are.
    for (int i = 0; i < kSectionCount; ++i)
    {
        const ui::Rect r{ui::kGutter, kNavTop + static_cast<float>(i) * (kNavItem + kNavGap),
                         kNavWidth, kNavItem};
        const bool current = i == static_cast<int>(section_);
        const float focus = nav_focus_[i];
        ui::state_fill(list, r, r.h * 0.5f, focus, current, true);
        const ui::Color ink = ui::state_ink(focus, current, true);
        section_icon(list, static_cast<Section>(i), r.x + 44.0f, r.cy(), 28.0f, ink);
        ui::text(list, type.medium, tr(kSections[i]), r.x + 78.0f, r.cy() + 7.5f, 21.0f, ink);
        if (static_cast<Section>(i) == Section::updates && update_available())
            list.circle(r.x + r.w - 32.0f, r.cy(), 7.0f, ui::color::action_hi);
    }

    // The section: laid out once to size its cards, then drawn.
    Pane layout(*this, nullptr);
    build(layout);
    Pane pane(*this, &list);
    pane.laid_out = &layout.cards;
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, -scroll_.value);
    list.push_clip({kContentX - 44.0f, kViewTop + scroll_.value, 1920.0f - kContentX + 44.0f,
                    kViewBottom - kViewTop});
    build(pane);
    list.pop_clip();
    list.pop_transform();
    list.pop_opacity();

    // ---- the My NAS address keyboard ----
    if (nas_keyboard_in_.value > 0.01f)
    {
        hui::gfx::DrawList &overlay = frame.overlay;
        frame.glass = true;
        const float shown = nas_keyboard_in_.value;
        overlay.push_opacity(shown);
        overlay.push_transform(1.0f, 0.0f, 0.0f, 0.0f, 60.0f * (1.0f - shown));
        const ui::Rect panel{0.0f, kKeyboardTop - 120.0f, 1920.0f, 1080.0f - kKeyboardTop + 120.0f};
        overlay.glass(frame.glass_texture, panel, 0.0f, ui::Color::rgb(0xffffff));
        overlay.rounded_rect(panel, 0.0f, ui::color::night1.with_alpha(0.78f));
        overlay.rounded_rect({0.0f, panel.y, 1920.0f, 1.5f}, 0.0f, ui::color::line);
        ui::text(overlay, type.regular, tr("My NAS index address"), ui::kGutter + 220.0f,
                 panel.y + 50.0f, 20.0f, ui::color::text2);
        const ui::Rect field{ui::kGutter + 220.0f, panel.y + 66.0f, 1280.0f, 56.0f};
        overlay.rounded_rect(field, 16.0f, ui::Color::rgb(0xffffff, 0.1f));
        ui::text_fit(overlay, type.regular, nas_url_, field.x + 20.0f, field.cy() + 7.0f, 20.0f,
                     field.w - 40.0f, ui::color::text);
        hui::ui::Canvas canvas{overlay, ctx_.fonts, frame.glass_texture, ctx_.clock};
        nas_keyboard_.draw(canvas);
        overlay.pop_transform();
        overlay.pop_opacity();
    }
}

std::vector<hui::ui::Hint> SettingsScreen::hints() const
{
    if (nas_edit_)
        return {{hui::ui::Button::square, tr("Delete")},
                {hui::ui::Button::triangle, tr("Space")},
                {hui::ui::Button::circle, tr("Close")}};
    if (in_content_)
        return {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Back")}};
    return {{hui::ui::Button::dpad, tr("Sections")},
            {hui::ui::Button::cross, tr("Select")},
            {hui::ui::Button::circle, tr("Close")}};
}

} // namespace orbit
