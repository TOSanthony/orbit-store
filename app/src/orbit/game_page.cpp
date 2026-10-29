// Orbit Store TV app - Artwork-led game hub and controller download sheets.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/screens.hpp"
#include "orbit/i18n.hpp"
#include "orbit/look.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>

namespace orbit
{
namespace
{
constexpr float kBodyTop = 430.0f;
constexpr float kBodyBottom = 978.0f;
constexpr float kLeft = 70.0f;
// ---- the badges of "About this game" ----
enum class FactIcon { platform, platform4, genre, calendar, publisher, download, disk, globe, version, format, source, title_id };

void fact_icon(hui::gfx::DrawList &list, const ui::Type &type, FactIcon icon, float cx, float cy,
               float size, ui::Color ink)
{
    (void)type;
    const float k = size / 40.0f;
    const float t = 2.4f * k;
    const auto line = [&](float x1, float y1, float x2, float y2, float w = 0.0f)
    { list.line(cx + x1 * k, cy + y1 * k, cx + x2 * k, cy + y2 * k, w > 0 ? w * k : t, ink); };
    const auto box = [&](float x, float y, float w, float h, float radius)
    {
        list.bordered_rect({cx + x * k, cy + y * k, w * k, h * k}, radius * k, ui::Color{0, 0, 0, 0}, t, ink);
    };
    const auto dot = [&](float x, float y, float r) { list.circle(cx + x * k, cy + y * k, r * k, ink); };
    // An elliptical arc as a polyline, angles in radians from the right, clockwise.
    const auto arc = [&](float x, float y, float rx, float ry, float from, float to)
    {
        constexpr int kSteps = 20;
        float px = x + rx * std::cos(from), py = y + ry * std::sin(from);
        for (int i = 1; i <= kSteps; ++i)
        {
            const float a = from + (to - from) * static_cast<float>(i) / kSteps;
            const float nx = x + rx * std::cos(a), ny = y + ry * std::sin(a);
            line(px, py, nx, ny);
            px = nx;
            py = ny;
        }
    };
    constexpr float kPi = 3.14159265f;
    switch (icon)
    {
    case FactIcon::platform:
    case FactIcon::platform4:
        // The four PlayStation shapes: triangle, circle, square and cross.
        line(-9, -15, -3, -5);
        line(-3, -5, -15, -5);
        line(-15, -5, -9, -15);
        list.ring(cx + 8 * k, cy - 10 * k, 5.5f * k, t, ink);
        box(-14.5f, 3, 11, 11, 2.5f);
        line(3, 3, 14, 14);
        line(14, 3, 3, 14);
        break;
    case FactIcon::genre:
    {
        // A game controller, outlined; the d-pad and four buttons inside.
        static const float kBody[] = {
            18.0f,5.0f, 17.7f,2.4f, 17.3f,-0.2f, 16.8f,-2.6f, 16.0f,-5.1f, 14.9f,-7.7f, 13.6f,-9.8f, 11.7f,-11.9f, 9.6f,-13.3f, 6.9f,-13.8f, 4.3f,-13.4f, 2.0f,-12.0f, 0.0f,-11.0f, -2.7f,-12.5f, -4.9f,-13.6f, -7.7f,-13.8f, -10.2f,-13.0f, -12.2f,-11.4f, -13.8f,-9.5f, -15.1f,-7.2f, -16.1f,-4.7f, -16.9f,-2.0f, -17.4f,0.5f, -17.8f,3.2f, -18.0f,5.8f, -17.8f,8.5f, -16.6f,10.8f, -14.5f,12.3f, -11.9f,12.6f, -9.6f,11.7f, -7.4f,9.9f, -5.7f,7.9f, -4.1f,5.8f, -2.2f,4.0f, 0.4f,3.3f, 2.6f,4.3f, 4.5f,6.2f, 6.2f,8.5f, 8.0f,10.4f, 10.1f,11.9f, 12.5f,12.7f, 15.0f,12.1f, 16.9f,10.4f, 17.9f,8.0f};
        constexpr int kCount = static_cast<int>(sizeof(kBody) / sizeof(kBody[0])) / 2;
        for (int i = 0; i < kCount; ++i)
        {
            const int j = (i + 1) % kCount;
            line(kBody[i * 2], kBody[i * 2 + 1], kBody[j * 2], kBody[j * 2 + 1]);
        }
        line(-12.2f, -2.3f, -4.6f, -2.3f, 2.6f);
        line(-8.4f, -6.1f, -8.4f, 1.5f, 2.6f);
        dot(8.3f, -5.6f, 1.4f);
        dot(8.3f, 1.7f, 1.4f);
        dot(11.9f, -2.0f, 1.4f);
        dot(4.6f, -2.0f, 1.4f);
        break;
    }
    case FactIcon::calendar:
        box(-14, -12, 28, 26, 5);
        line(-14, -4, 14, -4, 2.2f);
        line(-7, -16, -7, -9);
        line(7, -16, 7, -9);
        for (int i = 0; i < 3; ++i) dot(-7.0f + 7.0f * static_cast<float>(i), 5, 1.4f);
        break;
    case FactIcon::publisher:
    {
        dot(0, -6, 6);
        float xy[34];
        for (int i = 0; i < 17; ++i)
        {
            const float a = kPi + kPi * static_cast<float>(i) / 16.0f;
            xy[i * 2] = cx + 13.0f * k * std::cos(a);
            xy[i * 2 + 1] = cy + 14.0f * k + 13.0f * k * std::sin(a);
        }
        list.polygon(xy, 17, ink);
        break;
    }
    case FactIcon::download:
        line(0, -14, 0, 4, 2.6f);
        line(-7, -3, 0, 4, 2.6f);
        line(7, -3, 0, 4, 2.6f);
        line(-14, 4, -14, 13, 2.6f);
        line(-14, 13, 14, 13, 2.6f);
        line(14, 13, 14, 4, 2.6f);
        break;
    case FactIcon::disk:
        ui::icon_storage(list, cx, cy, size, ink);
        break;
    case FactIcon::globe:
        // The globe of the Language setting: the rim, two meridians that stop short of the
        // poles and two parallels, with finer inner strokes.
        list.ring(cx, cy, 15 * k, t, ink);
        arc(0, 0, 5.4f, 15, -0.5f * kPi + 0.45f, 0.5f * kPi - 0.45f);
        arc(0, 0, 5.4f, 15, 0.5f * kPi + 0.45f, 1.5f * kPi - 0.45f);
        line(-13.6f, -4.5f, 13.6f, -4.5f, 2.0f);
        line(-13.6f, 4.5f, 13.6f, 4.5f, 2.0f);
        break;
    case FactIcon::version:
        line(-14, -3, -3, -14);
        line(-3, -14, 14, -14);
        line(14, -14, 14, 3);
        line(14, 3, 3, 14);
        line(3, 14, -14, -3);
        dot(7, -7, 2);
        break;
    case FactIcon::format:
        line(0, -14, 14, -7);
        line(14, -7, 14, 7);
        line(14, 7, 0, 14);
        line(0, 14, -14, 7);
        line(-14, 7, -14, -7);
        line(-14, -7, 0, -14);
        line(-14, -7, 0, 0, 2.2f);
        line(0, 0, 14, -7, 2.2f);
        line(0, 0, 0, 14, 2.2f);
        break;
    case FactIcon::source:
        // A server: two stacked units with a light and a slot each.
        box(-16, -16, 32, 14, 5);
        box(-16, 2, 32, 14, 5);
        dot(-9, -9, 1.8f);
        dot(-9, 9, 1.8f);
        line(-2, -9, 9, -9, 2.0f);
        line(-2, 9, 9, 9, 2.0f);
        break;
    case FactIcon::title_id:
        box(-16, -11, 32, 22, 5);
        list.ring(cx - 8 * k, cy - 1 * k, 3.4f * k, 2.0f * k, ink);
        line(-13, 7, -3, 7, 2.0f);
        line(1, -4, 11, -4, 2.0f);
        line(1, 2, 9, 2, 2.0f);
        break;
    }
}

// Disk images (exFAT, FFPKG) land on the disk as they are; ZIP and PKG are downloads.
FactIcon size_icon(const Release &release)
{
    std::string upper = release.format;
    for (char &c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const bool disk_image = release.compression.empty() &&
        (upper == "EXFAT" || upper == "FFPKG" || upper == "FFPFSC");
    return disk_image ? FactIcon::disk : FactIcon::download;
}

std::string primary_label(const Release &release, bool again, bool waiting,
                         const CollectionState &collection, const Snapshot &state, bool torbox)
{
    if (!state.paired) return tr("Opening console session\xE2\x80\xA6");
    if (waiting) return tr("Please wait\xE2\x80\xA6");
    if (!again && collection.job != nullptr) return tr("View download");
    if (!again && collection.kind == CollectionState::Kind::library && collection.library != nullptr)
        return tr("View in Library");
    if (torbox) return tr("Download via TorBox");
    if (release.browser_available)
        return state.browser.active ? std::string(tr("View verification")) : release.source_id == "vikingfile"
            ? tr("Download to PS5") : tr("Open {provider} on PS5", {{"provider",release.provider}});
    return collection.job != nullptr || again ? tr("Download again") : tr("Download to PS5");
}
std::string heading(std::string value)
{
    // Preserve UTF-8; uppercase ASCII just as the browser's title treatment.
    for (char &c : value) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    return value;
}
// What to do on a provider's page, as the Store's sheet lists it.
std::vector<std::string> provider_steps(const Release &release)
{
    if (release.source_id == "vikingfile") return {
        tr("First, choose your drive and select {button}.", {{"button", tr("Download to PS5")}}),
        tr("Orbit uses your Vikingfile sign-in when requesting download links. If verification is needed, its page opens."),
        tr("Complete verification if shown, then wait. Orbit will check the generated download link automatically."),
        tr("Orbit returns automatically when the file is ready. Follow progress in Downloads.")};
    return {tr("1. Select Open {provider} on PS5. Your selected game and drive are kept.", {{"provider", release.provider}}),
            release.source_id == "akirabox" || release.source_id == "vikingfile"
                ? std::string(tr("Complete verification if shown, then wait. Orbit will check the generated download link automatically."))
            : release.source_id == "datanodes"
                ? std::string(tr("Scroll to the bottom, select Continue to Download, then Free Download. Wait for the countdown and select Start Download."))
                : tr("2. On {provider}, complete any verification and select Download.", {{"provider", release.provider}}),
            release.source_id == "akirabox" || release.source_id == "vikingfile"
                ? tr("Orbit returns automatically when the file is ready. Follow progress in Downloads.")
                : tr("3. Return to the Orbit Store app. Orbit checks the file and adds it to Downloads.")};
}
}

static std::string download_notes(const Release &release, const std::vector<LibraryGame> &library)
{
    const auto requirements = addon_requirements(release, library);
    return requirements + (requirements.empty() || release.notes.empty() ? "" : "\n") + release.notes;
}
int GamePage::addon_count() const {
    const auto &addons=ctx_.store.state().addons;
    return static_cast<int>(std::count_if(addons.begin(), addons.end(), [this](const Game &g){return g.base_game_id==game_id_;}));
}
std::string GamePage::addon_label() const {return tr("Add-ons ({count})", {{"count", i18n::count(addon_count())}});}

GamePage::GamePage(Context &context) : Screen(context) {}

void GamePage::open(const std::string &game_id)
{
    game_id_ = game_id;
    xb_.index = 0;
    xb_.snap();
    xb_menu_.open = false;
    xb_scroll_ = 0.0f;
    xb_scroll_shown_ = 0.0f;
    xb_idle_ = xb_end_hold_ = 0.0f;
    cn_zone_ = CnZone::buttons;
    cn_view_ = CnView::main;
    cn_sheet_ = false;
    cn_scroll_ = 0.0f;
    for (int &at : cn_at_)
        at = 0;
    pn_.focus = {};
    pn_.show_group(-1);
    pn_view_ = PnView::main;
    pn_space_ = false;
    pn_scroll_ = 0.0f;
    option_ = focus_ = 0;
    sheet_ = Sheet::none;
    download_again_ = waiting_ = torbox_ = all_ = false;
    error_.clear();
    page_scroll_.snap(0);
    sheet_scroll_.snap(0);
    action_serial_ = ctx_.store.action().serial;
    favourite_serial_ = ctx_.store.favourite_action().serial;
    const auto &state = ctx_.store.state();
    const int at = default_drive(state.drives, state.system.preferred_storage);
    drive_id_ = at >= 0 ? state.drives[static_cast<std::size_t>(at)].id : std::string();
}

void GamePage::show_about()
{
    if (look::is(look::Interface::crossbar))
    {
        // Crossbar: focus the About row, whose panel shows the details.
        const std::vector<XbItem> items = crossbar_items();
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].slot == Slot::about)
                xb_.index = static_cast<int>(i);
        xb_.snap();
        return;
    }
    if (look::is(look::Interface::cinema))
    {
        // Cinema: the sheet opens on About.
        cn_view_ = CnView::about;
        return;
    }
    if (look::is(look::Interface::panorama))
    {
        // Panorama: the page slides to About, the about tile focused.
        pn_view_ = PnView::about;
        pn_.focus = {0, 2};
        return;
    }
    show_sheet(Sheet::about, Slot::back);
}

const Drive *GamePage::drive() const
{
    const auto &state = ctx_.store.state();
    for (const auto &d : state.drives) if (d.id == drive_id_) return &d;
    // A disconnected destination must be chosen explicitly, never substituted.
    if (!drive_id_.empty()) return nullptr;
    const int at = default_drive(state.drives, state.system.preferred_storage);
    return at >= 0 ? &state.drives[static_cast<std::size_t>(at)] : nullptr;
}

bool GamePage::choosing(const Game &game) const
{
    return is_pack(game) && game.releases.size() > 1 && ctx_.store.state().paired;
}

GamePage::Offer GamePage::offer(const Release &release) const
{
    const auto &state = ctx_.store.state();
    Offer out;
    out.collection = release_state(release, state.jobs, state.library_games());
    out.show_download = download_again_ || out.collection.kind == CollectionState::Kind::none ||
                        out.collection.kind == CollectionState::Kind::related;
    out.drive = drive();
    if (out.drive != nullptr) out.plan = plan_space(release, *out.drive, state.jobs);
    return out;
}

bool GamePage::primary_enabled(const Release &release, const Offer &now) const
{
    const auto &state = ctx_.store.state();
    if (!state.paired || waiting_) return false;
    if (state.system.zero_enabled && !state.system.zero_error.empty()) return false;
    if (!now.show_download) return true;
    // Viewing/cancelling an existing session must remain possible after its
    // selected drive disconnects. This action never creates another session.
    if (!torbox_ && release.browser_available && state.browser.active) return true;
    if (now.drive == nullptr || !now.plan.enough || state.system.platform == "desktop") return false;
    if (torbox_) return state.debrid.supports(release);
    return !release.browser_available || (state.have_browser && state.browser.available &&
           state.link == Link::online && !ctx_.store.browser_action().busy);
}

std::string GamePage::delivery_note(const Release &release) const
{
    if (torbox_)
        return ctx_.store.state().debrid.supports(release)
            ? tr("Uses your TorBox account. Follow preparation in Downloads.")
            : tr("TorBox is unavailable for this option. Choose the original source or check App settings.");
    return tr("Choose TorBox to use your connected account for this download.");
}

// One measured layout supplies drawing, focus and scroll bounds. No notes are truncated.
GamePage::Layout GamePage::compose(hui::gfx::DrawList *list, Sheet sheet, bool focused) const
{
    Layout out;
    const auto &state = ctx_.store.state();
    const auto &type = ctx_.type;
    const Game *game = state.game(game_id_);
    if (game == nullptr || game->releases.empty()) return out;
    const int selected = std::clamp(option_, 0, static_cast<int>(game->releases.size()) - 1);
    const auto &release = game->releases[static_cast<std::size_t>(selected)];
    const Offer now = offer(release);
    const auto paragraph = [&](const std::string &text, float x, float &y, float width,
                               float size = 22.0f, ui::Color ink = ui::color::text2)
    {
        if (text.empty()) return;
        const auto lines = type.regular.font->wrap(text, size, width);
        if (list) ui::paragraph(*list, type.regular, text, x, y + size, size, width, size * 1.45f,
                               ink, static_cast<int>(lines.size()));
        y += size * 1.45f * static_cast<float>(lines.size());
    };
    const auto control = [&](Slot slot, int index, ui::Rect r, const std::string &label,
                             bool enabled = true, bool fixed = false, bool primary = false)
    {
        const float focus = focused && static_cast<int>(out.controls.size()) == focus_ ? 1.0f : 0.0f;
        out.controls.push_back({slot, index, r, enabled, fixed});
        if (list && !fixed)
        {
            ui::button(*list, type, r, label, primary ? ui::ButtonKind::light : ui::ButtonKind::secondary,
                       focus, enabled, 23);
        }
        return focus;
    };
    const auto card = [&](Slot slot, int index, float x, float y, float width,
                          const std::string &title, const std::string &detail,
                          bool checked, bool enabled = true)
    {
        const float text_width = width - 110;
        const float title_height = 27 * 1.3f * static_cast<float>(type.medium.font->wrap(title, 27, text_width).size());
        const float detail_height = detail.empty() ? 0 : 21 * 1.45f * static_cast<float>(type.regular.font->wrap(detail, 21, text_width).size());
        const float height = std::max(112.0f, 38 + title_height + detail_height);
        const ui::Rect r{x, y, width, height};
        const float focus = focused && static_cast<int>(out.controls.size()) == focus_ ? 1.0f : 0.0f;
        out.controls.push_back({slot, index, r, enabled, false});
        if (list)
        {
            ui::focus_ring(*list, r, 20, focus);
            ui::surface(*list, r, 20, checked ? ui::color::field_open : ui::color::field);
            list->push_opacity(enabled ? 1 : 0.4f);
            if (checked)
            {
                list->circle(x + 34, y + 44, 15, ui::color::text);
                ui::icon_check(*list, x + 34, y + 44, 18, ui::color::night1);
            }
            else list->ring(x + 34, y + 44, 14, 1, ui::color::text3);
            ui::paragraph(*list, type.medium, title, x + 68, y + 47, 27, text_width, 35, ui::color::text, 999);
            if (!detail.empty())
                ui::paragraph(*list, type.regular, detail, x + 68, y + 35 + title_height, 21,
                              text_width, 30.45f, ui::color::text2, 999);
            list->pop_opacity();
        }
        return height;
    };
    const auto budget = [&](float x, float &y, float width)
    {
        if (now.drive == nullptr) return;
        const std::pair<std::string, std::string> cells[] = {
            {tr("Free now"), format_bytes(now.drive->free_bytes)},
            {tr("Unfinished downloads"), format_bytes(now.drive->pending_bytes)},
            {now.plan.planned ? tr("After your queue") : tr("After queue + this download"),
             now.plan.after < 0 ? tr("{size} short", {{"size",format_bytes(-now.plan.after)}}) : format_bytes(now.plan.after)}};
        for (const auto &cell : cells)
        {
            if (list)
            {
                ui::text_fit(*list, type.regular, cell.first, x, y + 22, 20, width - 230, ui::color::text2);
                ui::text(*list, type.medium, cell.second, x + width, y + 22, 21, ui::color::text, ui::Align::right);
            }
            y += 40;
        }
    };
    if (sheet == Sheet::none)
    {
        const std::string title = heading(game->title);
        const float size = game->title.size() > 34 ? 68.0f : 92.0f;
        const float width = 960;
        float y = 185;
        if (!game->addon_id.empty() && !game->base_title.empty())
        {
            paragraph(game->base_title, kLeft, y, width, 26, ui::color::text);
            y += 12;
        }
        const auto lines = type.medium.font->wrap(title, size, width);
        if (list) ui::paragraph(*list, type.medium, title, kLeft, y + size * 0.85f, size,
                               width, size * 1.04f, ui::color::text, static_cast<int>(lines.size()));
        y += static_cast<float>(lines.size()) * size * 1.04f + 25;
        paragraph(game->tagline, kLeft, y, width, 25, ui::color::text);
        if (!game->tagline.empty()) y += 15;
        paragraph(game->description, kLeft, y, std::min(width, type.regular.measure("0", 22) * 54), 22);
        y += 24;
        // Server, format, size and platform as pills, each with its badge.
        {
            const struct { FactIcon icon; std::string text; } pills[] = {
                {FactIcon::source, release.provider},
                {FactIcon::format, format_label(release)},
                {size_icon(release), format_bytes(release.size_bytes)},
                {release.platform == "ps4" ? FactIcon::platform4 : FactIcon::platform, release.platform == "ps4" ? "PS4" : "PS5"}};
            float px = kLeft, py = y + 6;
            for (const auto &pill : pills)
            {
                if (pill.text.empty()) continue;
                const float text_w = type.medium.measure(pill.text, 23);
                const float w = 22 + 32 + 12 + text_w + 28;
                if (px > kLeft && px + w > kLeft + 1000) { px = kLeft; py += 76; }
                if (list)
                {
                    const ui::Rect r{px, py, w, 64};
                    list->rounded_rect(r, 32, ui::Color::rgb(0xffffff, 0.157f));
                    list->bordered_rect(r, 32, ui::Color{0, 0, 0, 0}, 1, ui::color::text.with_alpha(0.35f));
                    fact_icon(*list, type, pill.icon, px + 22 + 16, r.cy(), 36, ui::color::text);
                    ui::text(*list, type.medium, pill.text, px + 22 + 32 + 12, r.cy() + 8, 23, ui::color::text);
                }
                px += w + 12;
            }
            y = py + 64 + 18;
        }
        std::string meta;
        if (!game->genre.empty()) meta = i18n::genre(game->genre);
        if (!game->release_date.empty()) meta += (meta.empty() ? "" : " \xC2\xB7 ") + game->release_date.substr(0, 4);
        paragraph(meta, kLeft, y, width, 22);
        y = std::max(830.0f, y + 60);
        const std::string label = primary_label(release, download_again_, waiting_, now.collection, state, torbox_);
        const float button_width = std::clamp(type.medium.measure(label, 23) + 76, 460.0f, 800.0f);
        control(Slot::primary, 0, {kLeft,y,button_width,88}, label, !waiting_, false, true);
        for (int i=game->addon_id.empty() ? 0 : 1;i<2;++i)
        {
            const ui::Rect r{kLeft + button_width + 24 + (i - (game->addon_id.empty() ? 0 : 1)) * 108.0f,y,88,88};
            const Slot slot = i == 0 ? Slot::favourite : Slot::about;
            const float focus = control(slot,0,r,"",!waiting_);
            if (list)
            {
                list->bordered_rect(r,44,ui::Color{0,0,0,0},1,ui::color::text.with_alpha(0.35f));
                if (i == 0) icons::draw(*list, state.favourite(game->id) ? icons::Icon::heart_fill : icons::Icon::heart, r.cx(),r.cy(),36,ui::color::text);
                else for (int d=-1;d<=1;++d) list->circle(r.cx()+d*12,r.cy(),3,ui::color::text);
                if (focus > 0)
                    ui::text(*list,type.regular,i == 0 ? (state.favourite(game->id) ? tr("Saved to favourites") : tr("Add to favourites")) : tr("About this game"),
                             r.cx(),y+123,18,ui::color::text2,ui::Align::center);
            }
        }
        y += 140;
        if (addon_count() > 0) { control(Slot::addons,0,{kLeft,y,460,70},addon_label()); y += 90; }
        paragraph(addon_requirements(release, ctx_.store.state().library_games()),kLeft,y,1000,21);
        paragraph(error_,kLeft,y,1000,21);
        out.end = y;
        return out;
    }
    // Header/footer controls are fixed; all body content can scroll independently.
    control(Slot::back,0,{116,350,120,50},tr("Back"),true,true);
    control(Slot::close,0,{1730,350,66,50},tr("Close"),true,true);
    float y = 0;
    if (sheet == Sheet::downloads)
    {
        const float width = 554;
        const auto setting = [&](Slot slot,FactIcon icon,float x,const std::string &label,const std::string &value,const std::string &note)
        {
            const ui::Rect r{x,0,width,128};
            const float f = focused && static_cast<int>(out.controls.size()) == focus_ ? 1 : 0;
            out.controls.push_back({slot,0,r,!waiting_,false});
            if (list)
            {
                ui::focus_ring(*list,r,20,f);ui::surface(*list,r,20,ui::color::field);
                // The badge of the setting, as on the download options before.
                list->circle(x+50,64,24,ui::Color::rgb(0xffffff,0.14f));
                fact_icon(*list,type,icon,x+50,64,30,ui::color::text);
                ui::text(*list,type.regular,label,x+92,36,21,ui::color::text2);
                ui::text_fit(*list,type.medium,value,x+92,76,26,width-131,ui::color::text);
                ui::text_fit(*list,type.regular,note,x+92,108,20,width-151,ui::color::text2);
                const float cx = x + width - 30;
                list->line(cx-4,57,cx+3,64,2,ui::color::text2);
                list->line(cx+3,64,cx-4,71,2,ui::color::text2);
            }
        };
        setting(Slot::source,FactIcon::source,116,tr("Source"),release.provider + " \xC2\xB7 " + format_label(release),
                release.title_id + (release.version.empty()?"":" \xC2\xB7 v"+release.version) + " \xC2\xB7 " + format_bytes(release.size_bytes));
        if (now.show_download)
        {
            setting(Slot::delivery,FactIcon::download,684,tr("Download using"),state.system.zero_enabled ? "Orbit Zero" : torbox_ ? "TorBox" : tr("{provider} directly",{{"provider",release.provider}}),
                    state.system.zero_enabled ? "Uses your computer preference" : state.debrid.supports(release) && !torbox_ ? tr("TorBox available") : "");
            setting(Slot::drive,FactIcon::disk,1252,tr("Save to"),now.drive ? i18n::place(now.drive->label) : tr("Select storage"),
                    now.drive ? tr("{size} free",{{"size",format_bytes(now.drive->free_bytes)}}) : "");
        }
        y=162;
        const float width_notes=1040;
        const float notes_top=y;
        if (!firmware_warning(release, state.system.firmware).empty()) {
            paragraph(firmware_warning(release, state.system.firmware),116,y,width_notes,20,ui::color::text); y+=16;
        }
        if (!release.compression.empty()) {
            paragraph(tr("Compressed download. Orbit checks the files and available space before extraction. A password may be required."),116,y,width_notes,20); y+=16;
        }
        if (!download_notes(release, ctx_.store.state().library_games()).empty()) { paragraph(download_notes(release, ctx_.store.state().library_games()),116,y,width_notes,20); y+=16; }
        if (now.show_download && release.browser_available && !torbox_)
        {
            paragraph(tr("{provider}: start here",{{"provider",release.provider}}),116,y,width_notes,25,ui::color::text);y+=16;
            for (const auto &step : provider_steps(release)) { paragraph(step,116,y,width_notes,20); y+=12; }
            y+=4;
            if (!state.have_browser || !state.browser.available)
            {
                paragraph(state.browser_error.empty() ? tr("Provider verification is available on the PS5 only.")
                                                     : std::string(tr(std::string_view(state.browser_error))),116,y,width_notes,20);
                y+=12;
            }
        }
        if(now.show_download && release.browser_available && !torbox_)
            out.controls.push_back({Slot::reading,0,{116,notes_top,width_notes,y-notes_top},true,false});
        if (now.show_download && (torbox_ || (state.debrid.connected && !state.debrid.supports(release))))
        { paragraph(delivery_note(release),116,y,width_notes,21);y+=18; }
        if (now.collection.library)
        {
            paragraph(now.collection.kind == CollectionState::Kind::related
                ? tr("A related copy is in Library. Its format, filename or version does not confirm a match for this option.")
                : tr("In your Library on {location}.",{{"location",i18n::place(now.collection.library->location)}}),116,y,width_notes);
            y+=16;
            if (now.collection.kind == CollectionState::Kind::related)
            { control(Slot::related,0,{116,y,450,58},tr("View related copy"));y+=80; }
        }
        if (!download_again_ && (now.collection.kind == CollectionState::Kind::library ||
            (now.collection.job && now.collection.job->status == "complete")))
        { control(Slot::again,0,{116,y,450,58},tr("Download another copy"));y+=80; }
        if (now.collection.job && now.collection.job->status == "complete")
        { paragraph(tr("Download history records a completed transfer. Find it in Library to check its current location."),116,y,width_notes,20);y+=16; }
        if (now.show_download && (!now.drive || !now.plan.enough))
        {
            paragraph(!now.drive ? tr("Connect a writable drive to your PS5.") :
                tr("Not enough space after unfinished downloads. Free space or cancel an item in Downloads."),116,y,width_notes);
            y+=16;
        }
        paragraph(error_,116,y,width_notes);
        control(Slot::primary,0,{1252,842,554,84},"",primary_enabled(release,now),true,true);
    }
    else if (sheet == Sheet::source)
    {
        paragraph(game->releases.size()>1 ? tr("Choose a source and format for this game.") : tr("Available from your enabled sources."),116,y,1650);y+=22;
        const bool pack=is_pack(*game);
        if (choosing(*game) && now.drive)
        {
            double total=0;
            for (const auto &r:game->releases) total+=r.size_bytes;
            y+=card(Slot::install_all,0,116,y,1690,tr("Install all ({count})",{{"count",std::to_string(game->releases.size())}}),
                    format_bytes(total),all_,!waiting_)+18;
        }
        for (int i=0;i<static_cast<int>(game->releases.size());++i)
        {
            const auto &r=game->releases[static_cast<std::size_t>(i)];
            const auto presence=release_state(r,state.jobs,state.library_games());
            std::string detail=r.title_id + (r.version.empty()?"":" \xC2\xB7 v"+r.version) + " \xC2\xB7 " + format_bytes(r.size_bytes);
            if (!r.region.empty()) detail += " \xC2\xB7 " + r.region;
            if (presence.label) detail += " \xC2\xB7 " + std::string(presence.label);
            if (!firmware_label(r).empty()) detail += " \xC2\xB7 " + firmware_label(r);
            y+=card(Slot::choice,i,116,y,1690,pack && !r.filename.empty() ? r.filename : r.provider+" \xC2\xB7 "+format_label(r),detail,!all_ && i==selected,!waiting_)+18;
        }
    }
    else if (sheet == Sheet::delivery)
    {
        y+=card(Slot::choice,0,116,y,1690,tr("{provider} directly",{{"provider",release.provider}}),"",!torbox_,!waiting_)+18;
        const bool connected=state.debrid.connected || torbox_;
        y+=card(Slot::choice,1,116,y,1690,connected ? "TorBox" : tr("Set up TorBox"),
                connected ? delivery_note(release) : tr("Connect your TorBox account in App settings."),torbox_,
                !waiting_ && (connected ? state.debrid.supports(release) : static_cast<bool>(ctx_.open_download) || static_cast<bool>(ctx_.open_browser)))+18;
    }
    else if (sheet == Sheet::storage)
    {
        for(int i=0;i<static_cast<int>(state.drives.size());++i)
        {
            const auto &d=state.drives[static_cast<std::size_t>(i)];
            y+=card(Slot::choice,i,116,y,1690,i18n::place(d.label),tr("{size} free",{{"size",format_bytes(d.free_bytes)}})+"\n"+
                    tr("Saves to {path}",{{"path",d.path}}),now.drive && d.id==now.drive->id,!waiting_)+18;
        }
        if (state.drives.empty()) paragraph(tr("Connect a writable drive to your PS5."),116,y,1690);
        y+=12;budget(116,y,1690);
        if (now.drive && list)
            ui::progress_bar(*list,{116,y,1690,10},now.drive->total_bytes>0 ? static_cast<float>(1-now.drive->free_bytes/now.drive->total_bytes) : 0,ui::color::text);
        y+=34;
        paragraph(tr("Estimates include paused and failed downloads. Other apps can change free space."),116,y,1690,20);
    }
    else if (sheet == Sheet::about)
    {
        paragraph(game->description,116,y,1650,25);y+=30;
        struct Fact { FactIcon icon; std::string label; std::string value; };
        std::vector<Fact> facts;
        if(!game->publisher.empty())facts.push_back({FactIcon::publisher,tr("Publisher"),game->publisher});
        if(!game->release_date.empty())facts.push_back({FactIcon::calendar,tr("Release date"),format_date(game->release_date)});
        if(!game->genre.empty())facts.push_back({FactIcon::genre,tr("Genre"),i18n::genre(game->genre)});
        facts.push_back({FactIcon::format,tr("Format"),format_label(release)});
        facts.push_back({FactIcon::title_id,tr("Title ID"),release.title_id});
        facts.push_back({FactIcon::source,tr("Source"),release.provider});
        if(!release.version.empty())facts.push_back({FactIcon::version,tr("Version"),release.version});
        if(!release.region.empty())facts.push_back({FactIcon::globe,tr("Region"),release.region});
        facts.push_back({release.compression.empty()?FactIcon::disk:FactIcon::download,release.compression.empty()?tr("Download size"):tr("Compressed"),format_bytes(release.size_bytes)});
        if(!release.compression.empty() && release.extracted_bytes > 0)facts.push_back({FactIcon::disk,tr("Installed"),format_bytes(release.extracted_bytes)});
        for(std::size_t first=0;first<facts.size();first+=3)
        {
            float bottom=y;
            for(std::size_t i=first;i<std::min(first+3,facts.size());++i)
            {
                const float x=116+static_cast<float>(i-first)*568;
                float fy=y;
                if(list)
                {
                    list->circle(x+26,fy+22,24,ui::Color::rgb(0xffffff,0.14f));
                    fact_icon(*list,type,facts[i].icon,x+26,fy+22,30,ui::color::text);
                }
                paragraph(facts[i].label,x+68,fy,452,19,ui::color::text3);fy+=10;
                paragraph(facts[i].value,x+68,fy,452,24,ui::color::text);
                bottom=std::max(bottom,fy+12);
            }
            y=bottom+35;
        }
        paragraph(tr("Saves a file to your console. Installation and launching are separate."),116,y,1690,20);
    }
    out.end=y+20;
    return out;
}

void GamePage::show_sheet(Sheet sheet, Slot preferred, int index)
{
    sheet_=sheet;
    sheet_scroll_.snap(0);
    const auto where=compose(nullptr,sheet,false);
    focus_=0;
    for(std::size_t i=0;i<where.controls.size();++i)
        if(where.controls[i].slot==preferred && where.controls[i].index==index && where.controls[i].enabled)
        {focus_=static_cast<int>(i);return;}
    for(std::size_t i=0;i<where.controls.size();++i)
        if(where.controls[i].enabled && where.controls[i].slot!=Slot::back && where.controls[i].slot!=Slot::close)
        {focus_=static_cast<int>(i);return;}
}
void GamePage::back_sheet()
{
    const auto previous=sheet_;
    if(previous==Sheet::source)show_sheet(Sheet::downloads,Slot::source);
    else if(previous==Sheet::delivery)show_sheet(Sheet::downloads,Slot::delivery);
    else if(previous==Sheet::storage)show_sheet(Sheet::downloads,Slot::drive);
    else show_sheet(Sheet::none,previous==Sheet::about?Slot::about:Slot::primary);
}

Intent GamePage::activate(const Control &control,const Game &game,const Release &release,
                          const Offer &now,Feedback &feedback)
{
    Intent intent;
    const auto &state=ctx_.store.state();
    if(!control.enabled)return intent;
    feedback.play(hui::audio::Cue::select);
    switch(control.slot)
    {
    case Slot::reading: break;
    case Slot::back: back_sheet();break;
    case Slot::close: show_sheet(Sheet::none,sheet_==Sheet::about?Slot::about:Slot::primary);break;
    case Slot::addons: intent.kind=Intent::Kind::open_addons; intent.id=game.id; break;
    case Slot::favourite:
        if(game.addon_id.empty() && state.paired && !ctx_.store.favourite_action().busy)ctx_.store.set_favourite(game.id,!state.favourite(game.id));
        break;
    case Slot::about: show_sheet(Sheet::about,Slot::back);break;
    case Slot::source: show_sheet(Sheet::source,Slot::choice,option_);break;
    case Slot::delivery:
        if(state.system.zero_enabled) intent.toast="Change the download preference in Orbit Zero on your computer.";
        else show_sheet(Sheet::delivery,Slot::choice,torbox_?1:0);break;
    case Slot::drive:
    {
        int at=0;
        for(std::size_t i=0;i<state.drives.size();++i)if(now.drive && now.drive->id==state.drives[i].id)at=static_cast<int>(i);
        show_sheet(Sheet::storage,Slot::choice,at);break;
    }
    case Slot::choice:
        if(sheet_==Sheet::source)
        {
            if(all_||option_!=control.index){option_=control.index;all_=false;torbox_=false;download_again_=false;}
        }
        else if(sheet_==Sheet::storage)drive_id_=state.drives[static_cast<std::size_t>(control.index)].id;
        else if(sheet_==Sheet::delivery)
        {
            if(control.index==1 && !state.debrid.connected && !torbox_)
            {
                // Keep this view and all selections while the user connects in the browser.
                if(now.drive && ctx_.open_download)ctx_.open_download({game.id,release.id,now.drive->id});
                else if(ctx_.open_browser)ctx_.open_browser();
                return intent;
            }
            torbox_=control.index==1;
        }
        error_.clear();back_sheet();break;
    case Slot::install_all: all_=true;error_.clear();back_sheet();break;
    case Slot::again: download_again_=true;error_.clear();show_sheet(Sheet::downloads);break;
    case Slot::related:
        if(now.collection.library)
        {intent.kind=Intent::Kind::show_library;intent.id=now.collection.library->title_id;intent.detail=now.collection.library->source_key;}
        break;
    case Slot::primary:
        if(sheet_==Sheet::none){show_sheet(Sheet::downloads);break;}
        // Opening/reviewing a sheet never executes these actions.
        if(!primary_enabled(release,now))break;
        if(all_ && choosing(game))
        {
            error_.clear();
            int queued=0;
            for(const auto &r:game.releases)
            {
                bool taken=false;
                for(const auto &job:state.jobs)
                    if(job.release_id==r.id && job.storage_id==now.drive->id && job.status!="error" && job.status!="cancelled")taken=true;
                if(taken)continue;
                if(!queued){waiting_=true;action_serial_=ctx_.store.action().serial;}
                ctx_.store.download(r.id,now.drive->id,false);
                ++queued;
            }
            if(!queued)intent.toast=tr("Added to your downloads");
            break;
        }
        if(!download_again_ && now.collection.job)
        {intent.kind=Intent::Kind::show_job;intent.id=now.collection.job->id;}
        else if(!download_again_ && now.collection.kind==CollectionState::Kind::library && now.collection.library)
        {intent.kind=Intent::Kind::show_library;intent.id=now.collection.library->title_id;intent.detail=now.collection.library->source_key;}
        else if(release.browser_available && !torbox_)
        {
            error_.clear();
            if (!state.browser.active) ctx_.store.start_browser(release.id,now.drive->id);
            intent.kind=Intent::Kind::provider_browser;
        }
        else
        {
            error_.clear();waiting_=true;action_serial_=ctx_.store.action().serial;
            ctx_.store.download(release.id,now.drive->id,torbox_);
        }
        break;
    }
    return intent;
}

Intent GamePage::update(const InputFrame &input,float dt,Feedback &feedback,bool focused)
{
    if (look::is(look::Interface::crossbar))
        return update_crossbar(input, dt, feedback, focused);
    if (look::is(look::Interface::cinema))
        return update_cinema(input, dt, feedback, focused);
    if (look::is(look::Interface::panorama))
        return update_panorama(input, dt, feedback, focused);
    Intent intent;
    const auto &state=ctx_.store.state();
    const Game *game=state.game(game_id_);
    if(!game || game->releases.empty())
    {
        if(focused && (input.is_pressed(Action::back)||input.is_pressed(Action::confirm)))intent.kind=Intent::Kind::close;
        return intent;
    }
    option_=std::clamp(option_,0,static_cast<int>(game->releases.size())-1);
    if(waiting_ && ctx_.store.action().serial!=action_serial_ && !ctx_.store.action().busy)
    {
        waiting_=false;action_serial_=ctx_.store.action().serial;
        if(ctx_.store.action().error.empty())
        {
            if(ctx_.store.action().created_job=="zero-provider") {
                intent.toast="Continue in the provider window on your computer. Your download will appear here when it starts.";
                show_sheet(Sheet::none);return intent;
            }
            intent.kind=Intent::Kind::show_job;intent.id=ctx_.store.action().created_job;
            intent.toast=tr("Added to your downloads");return intent;
        }
        error_=ctx_.store.action().error;
    }
    if(ctx_.store.favourite_action().serial!=favourite_serial_ && !ctx_.store.favourite_action().busy)
    {
        favourite_serial_=ctx_.store.favourite_action().serial;
        if(!ctx_.store.favourite_action().error.empty())error_=ctx_.store.favourite_action().error;
    }
    if (focused && sheet_ == Sheet::none && !waiting_)
    {
        if (input.is_pressed(Action::north) || input.is_pressed(Action::west))
        {
            const auto &release = game->releases[static_cast<std::size_t>(option_)];
            const Slot slot = input.is_pressed(Action::north) ? Slot::favourite : Slot::about;
            return activate({slot, 0, {}, true, false}, *game, release, offer(release), feedback);
        }
    }
    auto where=compose(nullptr,sheet_,false);
    if(where.controls.empty())return intent;
    focus_=std::clamp(focus_,0,static_cast<int>(where.controls.size())-1);
    if(focused && input.is_pressed(Action::back))
    {
        if(sheet_==Sheet::none)intent.kind=Intent::Kind::close;
        else back_sheet();
        feedback.play(hui::audio::Cue::back);return intent;
    }
    auto &scroll=sheet_==Sheet::none?page_scroll_:sheet_scroll_;
    const float visible=sheet_==Sheet::none?980:kBodyBottom-kBodyTop;
    bool moved=false;
    bool read_scroll=false;
    if(focused && input.nav!=Direction::none)
    {
        const auto &current_control=where.controls[static_cast<std::size_t>(focus_)];
        if(current_control.slot==Slot::reading && (input.nav==Direction::up || input.nav==Direction::down))
        {
            const float first=std::max(0.0f,current_control.rect.y-14);
            const float last=std::max(first,current_control.rect.y+current_control.rect.h-visible+14);
            const float next=std::clamp(scroll.target+(input.nav==Direction::down?130.0f:-130.0f),first,last);
            read_scroll=next!=scroll.target;
            scroll.target=next;
        }
        if(read_scroll) {}
        else if((sheet_==Sheet::none || sheet_==Sheet::about) && (input.nav==Direction::up || input.nav==Direction::down))
            scroll.target=std::clamp(scroll.target+(input.nav==Direction::down?150.0f:-150.0f),0.0f,std::max(0.0f,where.end-visible));
        else
        {
            const auto screen_rect=[&](const Control &c){auto r=c.rect;if(!c.fixed)r.y+=(sheet_==Sheet::none?0:kBodyTop)-scroll.target;return r;};
            const auto current=screen_rect(where.controls[static_cast<std::size_t>(focus_)]);
            float best=std::numeric_limits<float>::max();int next=-1;
            for(std::size_t i=0;i<where.controls.size();++i)
            {
                if(static_cast<int>(i)==focus_ || !where.controls[i].enabled)continue;
                const auto r=screen_rect(where.controls[i]);
                float dx=r.cx()-current.cx(),dy=r.cy()-current.cy();
                const bool vertical=input.nav==Direction::up || input.nav==Direction::down;
                const float along=vertical?dy:dx,cross=vertical?dx:dy;
                if((input.nav==Direction::up || input.nav==Direction::left)?along>=-1:along<=1)continue;
                const float score=std::abs(along)+std::abs(cross)*3;
                if(score<best){best=score;next=static_cast<int>(i);}
            }
            if(next>=0){focus_=next;moved=true;feedback.play(hui::audio::Cue::focus);}
            else scroll.target=std::clamp(scroll.target+(input.nav==Direction::down?130.0f:input.nav==Direction::up?-130.0f:0),0.0f,std::max(0.0f,where.end-visible));
        }
    }
    if(moved)
    {
        const auto &c=where.controls[static_cast<std::size_t>(focus_)];
        if(!c.fixed)
        {
            if(c.rect.y-scroll.target<14)scroll.target=std::max(0.0f,c.rect.y-14);
            if(c.rect.y+c.rect.h-scroll.target>visible-14)scroll.target=c.rect.y+c.rect.h-visible+14;
        }
    }
    scroll.target=std::clamp(scroll.target,0.0f,std::max(0.0f,where.end-visible));
    scroll.update(dt,18);
    if(focused && input.is_pressed(Action::confirm))
    {
        const auto &c=where.controls[static_cast<std::size_t>(focus_)];
        if(!c.enabled)refuse(feedback,input);
        else intent=activate(c,*game,game->releases[static_cast<std::size_t>(option_)],offer(game->releases[static_cast<std::size_t>(option_)]),feedback);
    }
    return intent;
}

void GamePage::draw(Frame &frame,bool focused) const
{
    if (look::is(look::Interface::crossbar))
    {
        draw_crossbar(frame);
        return;
    }
    if (look::is(look::Interface::cinema))
    {
        draw_cinema(frame);
        return;
    }
    if (look::is(look::Interface::panorama))
    {
        draw_panorama(frame);
        return;
    }
    const auto &state=ctx_.store.state();
    const auto &type=ctx_.type;
    const Game *game=state.game(game_id_);
    auto &scene=frame.scene;
    if(!game || game->releases.empty())
    {
        ui::text(scene,type.light,tr("This game is no longer available"),70,420,52,ui::color::text);
        ui::text(scene,type.regular,tr("Its download source was turned off. Press Circle to go back."),70,470,22,ui::color::text2);return;
    }
    scene.push_clip({0,132,1920,865});
    scene.push_transform(1,0,0,0,-page_scroll_.value);
    compose(&scene,Sheet::none,focused && sheet_==Sheet::none);
    scene.pop_transform();scene.pop_clip();
    if(sheet_==Sheet::none)return;
    auto &list=frame.overlay;
    list.rounded_rect({0,0,1920,1080},0,ui::Color::rgb(0x000000,0.35f));
    const ui::Rect panel{70,320,1780,710};
    list.shadow(panel,30,50,ui::Color::rgb(0x000000,0.5f));
    list.gradient_rect(panel,30,ui::Color::rgb(0x242424,0.98f),ui::Color::rgb(0x101010,0.99f));
    list.bordered_rect(panel,30,ui::Color{0,0,0,0},1,ui::color::line);
    const auto &release=game->releases[static_cast<std::size_t>(std::clamp(option_,0,static_cast<int>(game->releases.size())-1))];
    const auto now=offer(release);
    const char *title=sheet_==Sheet::downloads?tr("Download options"):sheet_==Sheet::source?tr("Source"):
        sheet_==Sheet::delivery?tr("Download using"):sheet_==Sheet::storage?tr("Save to"):tr("About this game");
    ui::text_fit(list,type.medium,title,270,386,34,1380,ui::color::text);
    list.push_clip({100,kBodyTop-8,1720,kBodyBottom-kBodyTop+8});
    list.push_transform(1,0,0,0,kBodyTop-sheet_scroll_.value);
    const auto where=compose(&list,sheet_,focused);
    list.pop_transform();list.pop_clip();
    if(sheet_==Sheet::downloads)
    {
        list.rounded_rect({1226,738,596,253},22,ui::Color::rgb(0x111111,0.98f));
        if(now.show_download && now.drive)
        {
            list.rounded_rect({1252,765,554,1},0,ui::color::line);
            ui::text_fit(list,type.regular,now.plan.planned?tr("After your queue"):tr("After queue + this download"),1252,808,21,370,ui::color::text2);
            ui::text(list,type.medium,now.plan.after<0?tr("{size} short",{{"size",format_bytes(-now.plan.after)}}):format_bytes(now.plan.after),1806,808,22,ui::color::text,ui::Align::right);
        }
        ui::paragraph(list,type.regular,tr("Saves a file to your console. Installation and launching are separate."),1252,958,17,554,24,ui::color::text3,3);
    }
    for(std::size_t i=0;i<where.controls.size();++i)
    {
        const auto &c=where.controls[i];if(!c.fixed)continue;
        const float focus=focused && static_cast<int>(i)==focus_?1:0;
        if(c.slot==Slot::primary)
        {
            const std::string label=primary_label(release,download_again_,waiting_,now.collection,state,torbox_);
            const float size=ui::fit_size(type.medium,label,25,18,c.rect.w-45);
            ui::button(list,type,c.rect,label,ui::ButtonKind::light,focus,c.enabled,size);
        }
        else if(c.slot==Slot::back)ui::button(list,type,c.rect,tr("Back"),ui::ButtonKind::secondary,focus,true,22);
        else
        {
            ui::focus_ring(list,c.rect,c.rect.h/2,focus);
            const float x=c.rect.cx(),y=c.rect.cy();
            list.line(x-9,y-9,x+9,y+9,2,ui::color::text);list.line(x-9,y+9,x+9,y-9,2,ui::color::text);
        }
    }
}
std::vector<hui::ui::Hint> GamePage::hints() const
{
    if (look::is(look::Interface::cinema))
        return cinema_hints();
    if (look::is(look::Interface::panorama))
        return panorama_hints();
    const Game *game = ctx_.store.state().game(game_id_);
    const bool addon = game && !game->addon_id.empty();
    if (look::is(look::Interface::crossbar))
    {
        if (xb_menu_.open)
            return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Back")}};
        if (crossbar_panel() != Slot::back)
            return {{hui::ui::Button::options, tr("Scroll")}, {hui::ui::Button::circle, tr("Back")}};
        if (addon) return {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Back")}};
        return {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Back")},
                {hui::ui::Button::triangle, tr("Favourite")}};
    }
    const auto where=compose(nullptr,sheet_,false);
    const bool reading=focus_>=0 && focus_<static_cast<int>(where.controls.size()) && where.controls[static_cast<std::size_t>(focus_)].slot==Slot::reading;
    if(sheet_==Sheet::about || reading)return {{hui::ui::Button::dpad,tr("Scroll")},{hui::ui::Button::circle,tr("Back")}};
    if (sheet_ == Sheet::none && addon)
        return {{hui::ui::Button::cross,tr("Select")},{hui::ui::Button::circle,tr("Back")},{hui::ui::Button::square,tr("Details")}};
    if (sheet_ == Sheet::none)
        return {{hui::ui::Button::cross,tr("Select")},{hui::ui::Button::circle,tr("Back")},
                {hui::ui::Button::triangle,tr("Favourite")},{hui::ui::Button::square,tr("Details")}};
    return {{hui::ui::Button::cross,tr("Select")},{hui::ui::Button::circle,tr("Back")}};
}

// ---------------------------------------------------------------- Crossbar

std::vector<std::string> GamePage::situation(const Release &release, const Offer &now) const
{
    // What the page says about this option: its format, the delivery, where
    // a copy already is, the drive, and an error.
    const auto &state = ctx_.store.state();
    std::vector<std::string> notes;
    if (const std::string warning = firmware_warning(release, state.system.firmware); !warning.empty())
        notes.push_back(warning);
    if (!release.compression.empty())
        notes.push_back(tr("Compressed download. Orbit checks the files and available space before extraction. A password may be required."));
    if (now.show_download && (torbox_ || (state.debrid.connected && !state.debrid.supports(release))))
        notes.push_back(delivery_note(release));
    if (now.collection.library != nullptr)
        notes.push_back(now.collection.kind == CollectionState::Kind::related
                            ? tr("A related copy is in Library. Its format, filename or version does not confirm a match for this option.")
                            : tr("In your Library on {location}.", {{"location", i18n::place(now.collection.library->location)}}));
    if (now.collection.job != nullptr && now.collection.job->status == "complete")
        notes.push_back(tr("Download history records a completed transfer. Find it in Library to check its current location."));
    if (now.show_download && (now.drive == nullptr || !now.plan.enough))
        notes.push_back(now.drive == nullptr ? tr("Connect a writable drive to your PS5.")
                                             : tr("Not enough space after unfinished downloads. Free space or cancel an item in Downloads."));
    if (!error_.empty())
        notes.push_back(error_);
    return notes;
}

bool GamePage::follow(Intent &intent)
{
    // A download that was asked for, and a favourite: their results.
    if (waiting_ && ctx_.store.action().serial != action_serial_ && !ctx_.store.action().busy)
    {
        waiting_ = false;
        action_serial_ = ctx_.store.action().serial;
        if (ctx_.store.action().error.empty())
        {
            if (ctx_.store.action().created_job == "zero-provider")
            {
                intent.toast = "Continue in the provider window on your computer. Your download will appear here when it starts.";
                return true;
            }
            intent.kind = Intent::Kind::show_job;
            intent.id = ctx_.store.action().created_job;
            intent.toast = tr("Added to your downloads");
            return true;
        }
        error_ = ctx_.store.action().error;
    }
    if (ctx_.store.favourite_action().serial != favourite_serial_ && !ctx_.store.favourite_action().busy)
    {
        favourite_serial_ = ctx_.store.favourite_action().serial;
        if (!ctx_.store.favourite_action().error.empty())
            error_ = ctx_.store.favourite_action().error;
    }
    return false;
}

std::vector<GamePage::XbItem> GamePage::crossbar_items() const
{
    const auto item = [](Slot slot, icons::Icon icon, std::string title, std::string value = {}, bool enabled = true)
    {
        XbItem out;
        out.slot = slot;
        out.icon = icon;
        out.title = std::move(title);
        out.value = std::move(value);
        out.enabled = enabled;
        return out;
    };
    std::vector<XbItem> items;
    const auto &state = ctx_.store.state();
    const Game *game = state.game(game_id_);
    if (game == nullptr || game->releases.empty())
        return items;
    const auto &release = game->releases[static_cast<std::size_t>(std::clamp(option_, 0, static_cast<int>(game->releases.size()) - 1))];
    const Offer now = offer(release);
    // The download, with what the sheet says around it.
    XbItem main;
    main.slot = Slot::primary;
    main.primary = true;
    main.title = primary_label(release, download_again_, waiting_, now.collection, state, torbox_);
    main.icon = !download_again_ && now.collection.kind == CollectionState::Kind::library ? icons::Icon::books : icons::Icon::down;
    main.sub = now.show_download && now.drive != nullptr
                   ? tr("{size} \xC2\xB7 to {drive}", {{"size", format_bytes(release.size_bytes)}, {"drive", i18n::place(now.drive->label)}})
                   : format_bytes(release.size_bytes);
    if (now.show_download && now.drive != nullptr && now.plan.after < 0)
        main.sub += " \xC2\xB7 " + tr("{size} short", {{"size", format_bytes(-now.plan.after)}});
    main.enabled = primary_enabled(release, now) || !now.show_download;
    std::vector<std::string> notes;
    if (now.show_download && now.drive != nullptr && now.plan.after >= 0)
        notes.push_back(now.plan.planned
                            ? tr("After your queue, {size} stays free on this drive.", {{"size", format_bytes(now.plan.after)}})
                            : tr("After your queue and this download, {size} stays free on this drive.",
                                 {{"size", format_bytes(now.plan.after)}}));
    const bool steps = now.show_download && release.browser_available && !torbox_;
    if (steps)
    {
        // A provider's page: the first row's text is the three steps.
        notes.clear();
        for (const std::string &step : provider_steps(release))
            notes.push_back(step);
    }
    for (std::string &note : situation(release, now))
        notes.push_back(std::move(note));
    if (!steps)
        notes.push_back(tr("Saves a file to your console. Installation and launching are separate."));
    for (const std::string &note : notes)
        main.text += (main.text.empty() ? "" : " ") + note;
    items.push_back(main);
    items.push_back(item(Slot::source, icons::Icon::layers, tr("Version"), release.provider + " \xC2\xB7 " + format_label(release),
                         !waiting_));
    if (now.show_download)
    {
        items.push_back(item(Slot::delivery, icons::Icon::cloud, tr("Download using"),
                             state.system.zero_enabled ? std::string("Orbit Zero")
                             : torbox_                  ? std::string("TorBox")
                                                        : tr("{provider} directly", {{"provider", release.provider}}),
                             !waiting_));
        items.push_back(item(Slot::drive, icons::Icon::drive, tr("Save to"),
                             now.drive != nullptr ? i18n::place(now.drive->label) : std::string(tr("Select storage")), !waiting_));
    }
    if (now.collection.kind == CollectionState::Kind::related && now.collection.library != nullptr)
        items.push_back(item(Slot::related, icons::Icon::books, tr("View related copy")));
    if (!download_again_ && (now.collection.kind == CollectionState::Kind::library ||
                             (now.collection.job != nullptr && now.collection.job->status == "complete")))
        items.push_back(item(Slot::again, icons::Icon::copy, tr("Download another copy")));
    if (game->addon_id.empty()) items.push_back(item(Slot::favourite, state.favourite(game->id) ? icons::Icon::heart_fill : icons::Icon::heart,
                         state.favourite(game->id) ? tr("Saved to favourites") : tr("Add to favourites"), {}, !waiting_));
    if (steps || !download_notes(release, ctx_.store.state().library_games()).empty())
        items.push_back(item(Slot::reading, icons::Icon::list, tr("Source notes")));
    if (addon_count()>0) items.push_back(item(Slot::addons, icons::Icon::grid, addon_label()));
    items.push_back(item(Slot::about, icons::Icon::info, tr("About this game")));
    return items;
}

std::vector<xb::MenuItem> GamePage::crossbar_choices(Slot slot) const
{
    std::vector<xb::MenuItem> items;
    const auto &state = ctx_.store.state();
    const Game *game = state.game(game_id_);
    if (game == nullptr || game->releases.empty())
        return items;
    const int selected = std::clamp(option_, 0, static_cast<int>(game->releases.size()) - 1);
    const auto &release = game->releases[static_cast<std::size_t>(selected)];
    const Offer now = offer(release);
    if (slot == Slot::source)
        for (int i = 0; i < static_cast<int>(game->releases.size()); ++i)
        {
            const auto &r = game->releases[static_cast<std::size_t>(i)];
            const auto presence = release_state(r, state.jobs, state.library_games());
            std::string detail = r.title_id + (r.version.empty() ? "" : " \xC2\xB7 v" + r.version) + " \xC2\xB7 " + format_bytes(r.size_bytes);
            if (!r.region.empty())
                detail += " \xC2\xB7 " + r.region;
            if (presence.label)
                detail += " \xC2\xB7 " + std::string(tr(presence.label));
            if (!firmware_label(r).empty())
                detail += " \xC2\xB7 " + firmware_label(r);
            items.push_back(xb::choice(r.provider + " \xC2\xB7 " + format_label(r), i == selected, detail, !waiting_));
        }
    else if (slot == Slot::delivery)
    {
        const bool connected = state.debrid.connected || torbox_;
        items.push_back(xb::choice(tr("{provider} directly", {{"provider", release.provider}}), !torbox_, {}, !waiting_));
        items.push_back(xb::choice(connected ? std::string("TorBox") : std::string(tr("Set up TorBox")), torbox_,
                                   connected ? delivery_note(release) : std::string(tr("Connect your TorBox account in App settings.")),
                                   !waiting_ && (connected ? state.debrid.supports(release)
                                                           : static_cast<bool>(ctx_.open_download) || static_cast<bool>(ctx_.open_browser))));
    }
    else if (slot == Slot::drive)
        for (const Drive &d : state.drives)
        {
            items.push_back(xb::choice(i18n::place(d.label), now.drive != nullptr && d.id == now.drive->id,
                                       tr("{size} free", {{"size", format_bytes(d.free_bytes)}}) + " \xC2\xB7 " +
                                           tr("Saves to {path}", {{"path", d.path}}),
                                       !waiting_));
        }
    return items;
}

GamePage::Slot GamePage::crossbar_panel() const
{
    // About and Source notes show their panel while focused (Slot::back: neither).
    const std::vector<XbItem> items = crossbar_items();
    if (xb_.index < 0 || xb_.index >= static_cast<int>(items.size()))
        return Slot::back;
    const Slot slot = items[static_cast<std::size_t>(xb_.index)].slot;
    return slot == Slot::about || slot == Slot::reading ? slot : Slot::back;
}

Intent GamePage::update_crossbar(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    Intent intent;
    const auto &state = ctx_.store.state();
    const Game *game = state.game(game_id_);
    if (game == nullptr || game->releases.empty())
    {
        if (focused && (input.is_pressed(Action::back) || input.is_pressed(Action::confirm)))
            intent.kind = Intent::Kind::close;
        return intent;
    }
    option_ = std::clamp(option_, 0, static_cast<int>(game->releases.size()) - 1);
    if (follow(intent))
        return intent;
    const std::vector<XbItem> items = crossbar_items();
    xb::List list;
    list.size = static_cast<int>(items.size());
    list.fill = [](int, xb::Row &) {};
    xb_.clamp(list);
    const auto &release = game->releases[static_cast<std::size_t>(option_)];
    const auto run = [&](Slot slot, int index, Sheet context)
    {
        // The page's own actions: a choice applies in its sheet's context.
        sheet_ = context;
        intent = activate({slot, index, {}, true, false}, *game, release, offer(release), feedback);
        sheet_ = Sheet::none;
    };
    if (focused && xb_menu_.open)
    {
        const int chosen = xb_menu_.update(input, crossbar_choices(xb_choosing_), feedback);
        if (chosen >= 0)
            run(Slot::choice, chosen,
                xb_choosing_ == Slot::source ? Sheet::source : xb_choosing_ == Slot::delivery ? Sheet::delivery : Sheet::storage);
    }
    else if (focused)
    {
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            if (xb_.move(list, input, feedback))
                xb_scroll_ = xb_scroll_shown_ = xb_idle_ = xb_end_hold_ = 0.0f;
        }
        else if (input.is_pressed(Action::back))
        {
            intent.kind = Intent::Kind::close;
            feedback.play(hui::audio::Cue::back);
            return intent;
        }
        else if (input.is_pressed(Action::menu) && crossbar_panel() != Slot::back)
        {
            // Options scrolls the panel a page at a time, then back to the top.
            xb_scroll_ = xb_scroll_ >= xb_scroll_end_ ? 0.0f : std::min(xb_scroll_ + 340.0f, xb_scroll_end_);
            xb_idle_ = -3.0f; // the reader has taken over for a while
            xb_end_hold_ = 0.0f;
            feedback.play(hui::audio::Cue::focus);
        }
        else if (input.is_pressed(Action::north) && !waiting_)
            run(Slot::favourite, 0, Sheet::none);
        else if (input.is_pressed(Action::confirm) && !items.empty())
        {
            const XbItem &item = items[static_cast<std::size_t>(xb_.index)];
            if (!item.enabled)
                refuse(feedback, input);
            else if (item.slot == Slot::source || item.slot == Slot::drive ||
                     (item.slot == Slot::delivery && !state.system.zero_enabled))
            {
                xb_choosing_ = item.slot;
                int at = 0;
                const auto choices = crossbar_choices(item.slot);
                for (int i = 0; i < static_cast<int>(choices.size()); ++i)
                    if (choices[static_cast<std::size_t>(i)].checked)
                        at = i;
                xb_menu_.show(at);
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (item.slot == Slot::about || item.slot == Slot::reading)
                refuse(feedback, input);
            else if (item.slot == Slot::primary)
            {
                // The rows are the download options: Cross downloads at once.
                const Offer now = offer(release);
                if (!now.show_download || primary_enabled(release, now))
                    run(Slot::primary, 0, Sheet::downloads);
                else
                    refuse(feedback, input);
            }
            else
                run(item.slot, item.index, Sheet::downloads);
        }
    }
    xb_.animate(dt);
    xb_menu_.animate(dt);
    // A long text scrolls by itself: a pause at the top, a slow read down, a pause at the end,
    // then back to the top. Options takes over for a few seconds.
    if (!xb_menu_.open && crossbar_panel() != Slot::back && xb_scroll_end_ > 0.0f)
    {
        constexpr float kStart = 2.5f, kSpeed = 45.0f, kRest = 2.5f;
        xb_idle_ += dt;
        if (xb_idle_ > kStart)
        {
            if (xb_scroll_ < xb_scroll_end_ - 0.5f)
            {
                xb_scroll_ = std::min(xb_scroll_end_, xb_scroll_ + kSpeed * dt);
                xb_scroll_shown_ = xb_scroll_;
            }
            else if ((xb_end_hold_ += dt) > kRest)
            {
                xb_scroll_ = 0.0f;
                xb_idle_ = xb_end_hold_ = 0.0f;
            }
        }
    }
    // The panel's text glides to where Options sent it.
    xb_scroll_shown_ += (xb_scroll_ - xb_scroll_shown_) * std::min(1.0f, dt * 14.0f);
    if (std::abs(xb_scroll_ - xb_scroll_shown_) < 0.5f)
        xb_scroll_shown_ = xb_scroll_;
    return intent;
}

void GamePage::draw_crossbar(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const auto &state = ctx_.store.state();
    const auto &type = ctx_.type;
    const Game *game = state.game(game_id_);
    if (game == nullptr || game->releases.empty())
    {
        ui::text(list, type.sora_semibold, tr("This game is no longer available"), 330, 520, 44, ui::color::text);
        ui::text(list, type.sora, tr("Its download source was turned off. Press Circle to go back."), 330, 570, 22,
                 ui::color::text2);
        return;
    }
    // The bar: the section it came from, then the game in the category's place.
    std::vector<xb::BarItem> sections = xb::sections(ctx_, false);
    xb::BarItem here;
    here.game = game;
    here.label = (!game->addon_id.empty() && !game->base_title.empty() ? game->base_title + " · " : "") + game->title;
    xb::draw_bar(ctx_, list, {sections[static_cast<std::size_t>(std::clamp(section_, 0, section::count - 1))], here}, 1);
    const std::vector<XbItem> items = crossbar_items();
    xb::List rows;
    rows.size = static_cast<int>(items.size());
    rows.fill = [&items](int i, xb::Row &row)
    {
        const XbItem &item = items[static_cast<std::size_t>(i)];
        row.key = std::to_string(static_cast<int>(item.slot)) + ":" + std::to_string(item.index);
        row.has_icon = true;
        row.icon = item.icon;
        row.primary = item.primary;
        row.title = item.title;
        row.value = item.primary ? std::string() : item.value;
        row.sub = item.primary ? item.sub : std::string();
        row.text = item.text;
        row.off = !item.enabled;
    };
    xb_.draw(ctx_, list, rows);
    const auto &release = game->releases[static_cast<std::size_t>(std::clamp(option_, 0, static_cast<int>(game->releases.size()) - 1))];
    const Offer now = offer(release);
    const float hide = xb_menu_.in.value;
    list.push_opacity(1.0f - hide);
    const Slot panel = crossbar_panel();
    if (panel == Slot::about)
    {
        std::vector<std::pair<std::string, std::string>> facts;
        if (!game->publisher.empty()) facts.emplace_back(tr("Publisher"), game->publisher);
        if (!game->release_date.empty()) facts.emplace_back(tr("Release date"), format_date(game->release_date));
        if (!game->genre.empty()) facts.emplace_back(tr("Genre"), i18n::genre(game->genre));
        facts.emplace_back(tr("Format"), format_label(release));
        facts.emplace_back(tr("Title ID"), release.title_id);
        facts.emplace_back(tr("Source"), release.provider);
        if (!release.region.empty()) facts.emplace_back(tr("Region"), release.region);
        if (!release.version.empty()) facts.emplace_back(tr("Version"), release.version);
        facts.emplace_back(release.compression.empty() ? tr("Download size") : tr("Compressed"), format_bytes(release.size_bytes));
        if (!release.compression.empty() && release.extracted_bytes > 0)
            facts.emplace_back(tr("Installed"), format_bytes(release.extracted_bytes));
        // The facts and the note stay in view; only the description scrolls, in the room they leave.
        float description_room = 0.0f;
        xb::draw_panel(list, 400.0f, 0.0f, 0.0f, [&](hui::gfx::DrawList &out, float x, float y, float w)
        {
            y = xb::draw_label(ctx_, out, x, y, tr("About this game"));
            const std::string note = tr("Saves a file to your console. Installation and launching are separate.");
            const int note_lines = std::min(3, static_cast<int>(type.sora.font->wrap(note, 16.0f, w).size()));
            hui::gfx::DrawList scratch;
            const float facts_h = xb::draw_facts(ctx_, scratch, 0.0f, 0.0f, w, facts);
            const float note_h = 26.0f + 25.6f * static_cast<float>(note_lines);
            if (!game->description.empty())
            {
                const int n = static_cast<int>(type.sora.font->wrap(game->description, 22.0f, w - 14.0f).size());
                const float full = 14.0f + 34.1f * static_cast<float>(n);
                // The panel is at most 600 px tall, 32 px of padding top and bottom, the label above.
                const float room = 600.0f - 64.0f - 24.0f - 8.0f - 26.0f - facts_h - note_h;
                const float shown = std::min(full, std::max(room, 34.1f * 3.0f));
                description_room = std::max(0.0f, full - shown);
                const float offset = std::clamp(xb_scroll_shown_, 0.0f, description_room);
                y += 8.0f; // a little air under the label before the text slides under it
                out.push_clip({x - 4.0f, y, w + 8.0f, shown});
                ui::paragraph(out, type.sora, game->description, x, y - offset + 14.0f + 17.05f + 7.26f, 22.0f, w - 14.0f,
                              34.1f, ui::Color::rgb(0xeceef4), 400);
                out.pop_clip();
                if (description_room > 0.0f)
                {
                    // A thin bar beside the text shows how far along the description is.
                    const float bar = std::max(36.0f, shown * shown / full);
                    out.rounded_rect({x + w - 4.0f, y, 4.0f, shown}, 2.0f, ui::Color::rgb(0xffffff, 0.12f));
                    out.rounded_rect({x + w - 4.0f, y + (shown - bar) * offset / description_room, 4.0f, bar}, 2.0f,
                                     ui::Color::rgb(0xffffff, 0.7f));
                }
                y += shown + 26.0f;
            }
            else
                y += 20.0f;
            y = xb::draw_facts(ctx_, out, x, y, w, facts);
            ui::paragraph(out, type.sora, note, x, y + 26.0f + 12.8f + 5.3f, 16.0f, w, 25.6f, ui::Color::rgb(0xffffff, 0.55f), 3);
            return y + note_h;
        });
        xb_scroll_end_ = description_room;
    }
    else if (panel == Slot::reading)
    {
        const bool steps = now.show_download && release.browser_available && !torbox_;
        xb_scroll_end_ = xb::draw_panel(list, 400.0f, 560.0f, xb_scroll_shown_, [&](hui::gfx::DrawList &out, float x, float y, float w)
        {
            if (steps)
            {
                y = xb::draw_label(ctx_, out, x, y, tr("{provider}: start here", {{"provider", release.provider}}));
                y = xb::draw_steps(ctx_, out, x, y + 18.0f, w, provider_steps(release));
                if (!state.have_browser || !state.browser.available)
                {
                    const std::string why = state.browser_error.empty() ? std::string(tr("Provider verification is available on the PS5 only."))
                                                                        : std::string(tr(std::string_view(state.browser_error)));
                    y = ui::paragraph(out, type.sora, why, x, y + 14.0f + 15.2f + 6.3f, 19.0f, w, 30.4f, ui::Color::rgb(0xffd27a), 3) -
                        15.2f - 6.3f + 30.4f;
                }
                if (download_notes(release, ctx_.store.state().library_games()).empty())
                    return y;
                out.rounded_rect({x, y + 26.0f, w, 1.0f}, 0.0f, ui::Color::rgb(0xffffff, 0.14f));
                y += 53.0f;
            }
            y = xb::draw_label(ctx_, out, x, y, tr("Notes for this option"));
            const int n = static_cast<int>(type.sora.font->wrap(download_notes(release, ctx_.store.state().library_games()), 19.0f, w).size());
            ui::paragraph(out, type.sora, download_notes(release, ctx_.store.state().library_games()), x, y + 12.0f + 15.2f + 6.3f, 19.0f, w, 30.4f, ui::Color::rgb(0xffffff, 0.82f), 40);
            return y + 12.0f + 30.4f * static_cast<float>(std::min(n, 40));
        });
    }
    else
        xb::draw_preview(ctx_, list, *game);
    list.pop_opacity();
    if (xb_menu_.in.value > 0.01f)
    {
        // Save to: the drive's figures under the drives, as the Store's sheet shows them.
        xb::MenuFoot foot;
        if (xb_choosing_ == Slot::source)
            foot.note = tr("Choose a source and format for this game.");
        else if (xb_choosing_ == Slot::drive && now.drive != nullptr)
        {
            foot.figures = {{tr("Free now"), format_bytes(now.drive->free_bytes)},
                            {tr("Unfinished downloads"), format_bytes(now.drive->pending_bytes)},
                            {now.plan.planned ? tr("After your queue") : tr("After queue + this download"),
                             now.plan.after < 0 ? tr("{size} short", {{"size", format_bytes(-now.plan.after)}}) : format_bytes(now.plan.after)}};
            foot.bar = now.drive->total_bytes > 0 ? static_cast<float>(1.0 - now.drive->free_bytes / now.drive->total_bytes) : 0.0f;
            foot.note = tr("Estimates include paused and failed downloads. Other apps can change free space.");
        }
        else if (xb_choosing_ == Slot::drive)
            foot.note = tr("Connect a writable drive to your PS5.");
        xb::draw_menu(ctx_, frame.overlay,
                      xb_choosing_ == Slot::source ? tr("Version") : xb_choosing_ == Slot::delivery ? tr("Download using") : tr("Save to"),
                      crossbar_choices(xb_choosing_), xb_menu_, {.y = xb_choosing_ == Slot::drive ? 380.0f : 430.0f}, foot);
    }
}

// ---------------------------------------------------------------- Cinema

std::vector<GamePage::CnButton> GamePage::cinema_buttons(bool extras) const
{
    // The page's buttons: the download, the heart, About and the provider's
    // notes; under them, what else this game offers. In the download's
    // sheet, only the button that starts it.
    std::vector<CnButton> out;
    const auto &state = ctx_.store.state();
    const Game *game = state.game(game_id_);
    if (game == nullptr || game->releases.empty())
        return out;
    const auto &release = game->releases[static_cast<std::size_t>(std::clamp(option_, 0, static_cast<int>(game->releases.size()) - 1))];
    const Offer now = offer(release);
    const bool steps = now.show_download && release.browser_available && !torbox_;
    if (extras && cn_sheet_)
        return out;
    if (extras)
    {
        if (now.collection.kind == CollectionState::Kind::related && now.collection.library != nullptr)
            out.push_back({Slot::related, cn::button(tr("View related copy"), icons::Icon::books)});
        if (!download_again_ && (now.collection.kind == CollectionState::Kind::library ||
                                 (now.collection.job != nullptr && now.collection.job->status == "complete")))
            out.push_back({Slot::again, cn::button(tr("Download another copy"), icons::Icon::copy)});
        for (CnButton &b : out)
            b.look.small = true;
        return out;
    }
    cn::Button primary = cn::button(primary_label(release, download_again_, waiting_, now.collection, state, torbox_),
                                    steps ? icons::Icon::send
                                    : !download_again_ && now.collection.kind == CollectionState::Kind::library ? icons::Icon::books
                                                                                                                : icons::Icon::down,
                                    true);
    primary.enabled = !waiting_ && (primary_enabled(release, now) || !now.show_download);
    if (!cn_sheet_ && now.show_download && state.paired)
    {
        // On the page, Download · size raises the sheet with its choices.
        primary = cn::button(release.size_bytes > 0.0 ? std::string(tr("Download")) + " \xC2\xB7 " + format_bytes(release.size_bytes)
                                                      : std::string(tr("Download")),
                             icons::Icon::down, true);
        primary.enabled = !waiting_;
    }
    out.push_back({Slot::primary, primary});
    if (cn_sheet_)
        return out;
    cn::Button heart;
    heart.has_icon = true;
    heart.icon = state.favourite(game->id) ? icons::Icon::heart_fill : icons::Icon::heart;
    heart.enabled = !waiting_ && state.paired;
    if (game->addon_id.empty()) out.push_back({Slot::favourite, heart});
    if (addon_count()>0) out.push_back({Slot::addons, cn::button(addon_label(), icons::Icon::grid)});
    out.push_back({Slot::about, cn::button(tr("About"), icons::Icon::info)});
    if (steps || !download_notes(release, ctx_.store.state().library_games()).empty())
        out.push_back({Slot::reading, cn::button(tr("Source notes"), icons::Icon::list)});
    return out;
}

int GamePage::cinema_count(CnZone zone) const
{
    const auto &state = ctx_.store.state();
    const Game *game = state.game(game_id_);
    if (game == nullptr || game->releases.empty())
        return 0;
    const auto &release = game->releases[static_cast<std::size_t>(std::clamp(option_, 0, static_cast<int>(game->releases.size()) - 1))];
    const Offer now = offer(release);
    // The cards are in the download's sheet only.
    if (!cn_sheet_ && zone != CnZone::buttons && zone != CnZone::extras)
        return 0;
    switch (zone)
    {
    case CnZone::buttons:
        return static_cast<int>(cinema_buttons(false).size());
    case CnZone::extras:
        return static_cast<int>(cinema_buttons(true).size());
    case CnZone::versions:
        return static_cast<int>(game->releases.size());
    case CnZone::delivery:
        // As the design: the choice shows once TorBox is connected or chosen.
        return now.show_download && !state.system.zero_enabled && (state.debrid.connected || torbox_) ? 2 : 0;
    default:
        return now.show_download ? static_cast<int>(state.drives.size()) : 0;
    }
}

Intent GamePage::update_cinema(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    (void)dt;
    Intent intent;
    const auto &state = ctx_.store.state();
    const Game *game = state.game(game_id_);
    if (game == nullptr || game->releases.empty())
    {
        if (focused && (input.is_pressed(Action::back) || input.is_pressed(Action::confirm)))
            intent.kind = Intent::Kind::close;
        return intent;
    }
    option_ = std::clamp(option_, 0, static_cast<int>(game->releases.size()) - 1);
    if (follow(intent) || !focused)
        return intent;
    const auto &release = game->releases[static_cast<std::size_t>(option_)];
    const auto run = [&](Slot slot, int index, Sheet context)
    {
        // A choice applies in its sheet's context, as the Store's sheets do.
        sheet_ = context;
        intent = activate({slot, index, {}, true, false}, *game, release, offer(release), feedback);
        sheet_ = Sheet::none;
    };
    if (cn_view_ != CnView::main)
    {
        if (input.is_pressed(Action::back))
        {
            cn_view_ = CnView::main;
            feedback.play(hui::audio::Cue::back);
        }
        else if (input.is_pressed(Action::menu))
        {
            cn_scroll_ = cn_scroll_ >= cn_scroll_end_ ? 0.0f : std::min(cn_scroll_ + 260.0f, cn_scroll_end_);
            feedback.play(hui::audio::Cue::focus);
        }
        return intent;
    }
    // Zones without anything to focus are skipped.
    if (cinema_count(cn_zone_) == 0)
        cn_zone_ = CnZone::buttons;
    int &at = cn_at_[static_cast<int>(cn_zone_)];
    at = std::clamp(at, 0, std::max(0, cinema_count(cn_zone_) - 1));
    const auto enter = [&](CnZone zone)
    {
        cn_zone_ = zone;
        // A row of choices starts on the chosen one.
        if (zone == CnZone::versions)
            cn_at_[static_cast<int>(zone)] = option_;
        else if (zone == CnZone::delivery)
            cn_at_[static_cast<int>(zone)] = torbox_ ? 1 : 0;
        else if (zone == CnZone::drives)
        {
            const Offer now = offer(release);
            for (int i = 0; i < static_cast<int>(state.drives.size()); ++i)
                if (now.drive != nullptr && state.drives[static_cast<std::size_t>(i)].id == now.drive->id)
                    cn_at_[static_cast<int>(zone)] = i;
        }
        feedback.play(hui::audio::Cue::focus);
    };
    const bool cards = cn_zone_ == CnZone::versions || cn_zone_ == CnZone::delivery || cn_zone_ == CnZone::drives;
    if (cn_zone_ == CnZone::versions && input.nav != Direction::none &&
        ((input.nav == Direction::left && at % 2 == 1) || (input.nav == Direction::right && at % 2 == 0) ||
         (input.nav == Direction::up && at >= 2) || (input.nav == Direction::down && at + 2 < cinema_count(CnZone::versions))))
    {
        // The versions are two to a row.
        const int to = at + (input.nav == Direction::left ? -1 : input.nav == Direction::right ? 1 : input.nav == Direction::up ? -2 : 2);
        if (to >= 0 && to < cinema_count(CnZone::versions))
        {
            at = to;
            feedback.play(hui::audio::Cue::focus);
        }
        else
            refuse(feedback, input);
    }
    else if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const int to = at + (input.nav == Direction::right ? 1 : -1);
        if (to >= 0 && to < cinema_count(cn_zone_))
        {
            at = to;
            feedback.play(hui::audio::Cue::focus);
        }
        else if (input.nav == Direction::right && !cards && cinema_count(CnZone::versions) > 0)
            enter(CnZone::versions);
        else if (input.nav == Direction::left && cards)
            enter(CnZone::buttons);
        else
            refuse(feedback, input);
    }
    else if (input.nav == Direction::up || input.nav == Direction::down)
    {
        const bool down = input.nav == Direction::down;
        CnZone to = cn_zone_;
        if (cn_zone_ == CnZone::buttons)
            to = down && cinema_count(CnZone::extras) > 0 ? CnZone::extras : cn_zone_;
        else if (cn_zone_ == CnZone::extras)
            to = down ? cn_zone_ : CnZone::buttons;
        else
        {
            // The card rows, top to bottom: versions, delivery, drives.
            static constexpr CnZone kRows[] = {CnZone::versions, CnZone::delivery, CnZone::drives};
            int row = cn_zone_ == CnZone::versions ? 0 : cn_zone_ == CnZone::delivery ? 1 : 2;
            for (int r = row + (down ? 1 : -1); r >= 0 && r < 3; r += down ? 1 : -1)
                if (cinema_count(kRows[r]) > 0)
                {
                    to = kRows[r];
                    break;
                }
        }
        if (to != cn_zone_)
            enter(to);
        else
            refuse(feedback, input);
    }
    else if (input.is_pressed(Action::back))
    {
        // Circle closes the download's sheet, then the page.
        if (cn_sheet_)
        {
            cn_sheet_ = false;
            cn_zone_ = CnZone::buttons;
            cn_at_[0] = 0;
        }
        else
            intent.kind = Intent::Kind::close;
        feedback.play(hui::audio::Cue::back);
    }
    else if (input.is_pressed(Action::north) && !waiting_)
        run(Slot::favourite, 0, Sheet::none);
    else if (input.is_pressed(Action::confirm))
    {
        if (cn_zone_ == CnZone::buttons || cn_zone_ == CnZone::extras)
        {
            const std::vector<CnButton> buttons = cinema_buttons(cn_zone_ == CnZone::extras);
            const CnButton &b = buttons[static_cast<std::size_t>(at)];
            if (!b.look.enabled)
                refuse(feedback, input);
            else if (b.slot == Slot::primary && !cn_sheet_ && offer(release).show_download && state.paired)
            {
                // Download raises the sheet on its start button, the chosen
                // version, delivery and drive beside it.
                cn_sheet_ = true;
                cn_zone_ = CnZone::buttons;
                cn_at_[0] = 0;
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (b.slot == Slot::about || b.slot == Slot::reading)
            {
                cn_view_ = b.slot == Slot::about ? CnView::about : CnView::notes;
                cn_scroll_ = 0.0f;
                feedback.play(hui::audio::Cue::select);
            }
            else
                run(b.slot, 0, b.slot == Slot::favourite ? Sheet::none : Sheet::downloads);
        }
        else if (waiting_)
            refuse(feedback, input);
        else
            run(Slot::choice, at,
                cn_zone_ == CnZone::versions ? Sheet::source : cn_zone_ == CnZone::delivery ? Sheet::delivery : Sheet::storage);
    }
    return intent;
}

void GamePage::draw_cinema(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const auto &state = ctx_.store.state();
    const auto &type = ctx_.type;
    const Game *game = state.game(game_id_);
    if (game == nullptr || game->releases.empty())
    {
        cn::backdrop(ctx_, list, game, cn::Veil::dim);
        cn::Hero gone;
        gone.title = tr("This game is no longer available");
        gone.body = tr("Its download source was turned off. Press Circle to go back.");
        gone.size = 110.0f;
        gone.y = 400.0f;
        cn::draw_hero(ctx_, list, gone);
        return;
    }
    const auto &release = game->releases[static_cast<std::size_t>(std::clamp(option_, 0, static_cast<int>(game->releases.size()) - 1))];
    const Offer now = offer(release);
    const bool steps = now.show_download && release.browser_available && !torbox_;
    cn::backdrop(ctx_, list, game, cn::Veil::detail);
    // Over the art: the game's genre, year, platform and source, and its title.
    std::string kicker = game->addon_id.empty() ? std::string() : game->base_title;
    if (!game->genre.empty())
        kicker += (kicker.empty() ? "" : " · ") + i18n::genre(game->genre);
    if (game->release_date.size() >= 4)
        kicker += (kicker.empty() ? "" : " \xC2\xB7 ") + game->release_date.substr(0, 4);
    kicker += (kicker.empty() ? "" : " \xC2\xB7 ") + std::string(release.platform == "ps4" ? "PS4" : "PS5");
    if (!release.compression.empty())
        kicker += " \xC2\xB7 " + format_label(release);
    else if (release.browser_available)
        kicker += " \xC2\xB7 " + release.provider;
    cn::kick(ctx_, list, 100.0f, 150.0f, kicker, 20.0f);
    const auto &title_font = ui::face(type.cinema, type.semibold, game->title);
    const float title_size = ui::fit_size(title_font, game->title, 112.0f, 72.0f, 1720.0f);
    ui::text(list, title_font, game->title, 100.0f, 150.0f + 32.0f + 12.0f + title_size * 0.82f, title_size, cn::kInk);
    const auto notes_card = [&](hui::gfx::DrawList &out, float x, float y, float width)
    {
        // What matters about this option now, in one card, as the web's.
        std::string notes;
        for (const std::string &note : situation(release, now))
            notes += (notes.empty() ? "" : " ") + note;
        if (notes.empty())
            return y;
        const int n = std::min(3, static_cast<int>(type.regular.font->wrap(notes, 19.0f, width - 44.0f).size()));
        const ui::Rect card{x, y + 22.0f, width, 36.0f + 28.5f * static_cast<float>(n)};
        out.rounded_rect(card, 20.0f, ui::Color::rgb(0xffffff, 0.06f));
        out.bordered_rect(card, 20.0f, ui::Color{0, 0, 0, 0}, 1.0f, ui::Color::rgb(0xffffff, 0.1f));
        const bool warn = now.show_download && (now.drive == nullptr || !now.plan.enough);
        ui::paragraph(out, type.regular, notes, card.x + 22.0f, card.y + 18.0f + 14.25f + 6.65f, 19.0f, width - 44.0f, 28.5f,
                      warn ? cn::kWarn : cn::kInk, 3);
        return card.y + card.h;
    };
    const auto button_row = [&](hui::gfx::DrawList &out, float x, float y, bool extras)
    {
        const std::vector<CnButton> buttons = cinema_buttons(extras);
        std::vector<cn::Button> looks;
        for (int i = 0; i < static_cast<int>(buttons.size()); ++i)
        {
            cn::Button b = buttons[static_cast<std::size_t>(i)].look;
            b.focus = cn_zone_ == (extras ? CnZone::extras : CnZone::buttons) && i == cn_at_[extras ? 1 : 0];
            looks.push_back(b);
        }
        if (!looks.empty())
            cn::draw_buttons(ctx_, out, x, y, looks);
        return !looks.empty();
    };
    const bool sheet = cn_sheet_ || cn_view_ != CnView::main;
    if (cn_view_ == CnView::main)
    {
        // The page under the title: the line, a few lines about the game, what
        // matters now and the buttons (dimmed under the download's sheet).
        if (sheet)
            list.push_opacity(0.3f);
        const float page_w = 1000.0f;
        float y = 150.0f + 32.0f + 12.0f + title_size + 30.0f;
        if (!game->tagline.empty())
        {
            const int n = std::min(2, static_cast<int>(type.medium.font->wrap(game->tagline, 32.0f, page_w).size()));
            ui::paragraph(list, type.medium, game->tagline, 100.0f, y + 21.6f + 11.2f, 32.0f, page_w, 43.2f, cn::kInk, 2);
            y += 43.2f * static_cast<float>(n);
        }
        if (!game->description.empty())
        {
            const int n = std::min(3, static_cast<int>(type.regular.font->wrap(game->description, 21.0f, page_w).size()));
            ui::paragraph(list, type.regular, game->description, 100.0f, y + 16.0f + 16.8f + 7.35f, 21.0f, page_w, 33.6f, cn::kDim, n);
            y += 16.0f + 33.6f * static_cast<float>(n);
        }
        if (!cn_sheet_)
        {
            y = notes_card(list, 100.0f, y, page_w);
            if (button_row(list, 100.0f, y + 30.0f, false))
                y += 30.0f + 66.0f;
            button_row(list, 100.0f, y + 16.0f, true);
        }
        if (sheet)
            list.pop_opacity();
    }
    if (!sheet)
        return;
    // The sheet: the download's choices, About or the provider's notes.
    const float top = cn::draw_sheet(list, 690.0f) + 56.0f;
    const float left = 100.0f, left_w = 560.0f;
    const float right = left + left_w + 70.0f, right_w = 1820.0f - right;
    const auto kv = [&](hui::gfx::DrawList &out, float x, float y, float width, const std::vector<std::pair<std::string, std::string>> &pairs)
    {
        // Gold labels over values, three to a row.
        const float column = (width - 60.0f) / 3.0f;
        for (std::size_t i = 0; i < pairs.size(); ++i)
        {
            const float cx = x + static_cast<float>(i % 3) * (column + 30.0f);
            const float cy = y + static_cast<float>(i / 3) * (20.8f + 6.0f + 33.6f + 18.0f);
            cn::kick(ctx_, out, cx, cy, pairs[i].first, 13.0f);
            ui::text_fit(out, type.regular, pairs[i].second, cx, cy + 20.8f + 6.0f + 21.0f * 0.8f + 4.0f, 21.0f, column, cn::kInk);
        }
        return y + static_cast<float>((pairs.size() + 2) / 3) * (20.8f + 6.0f + 33.6f + 18.0f) - 18.0f;
    };
    const auto step_list = [&](hui::gfx::DrawList &out, float x, float y, float width)
    {
        // The provider's steps, numbered in gold italics.
        const std::vector<std::string> list_of = provider_steps(release);
        for (std::size_t i = 0; i < list_of.size(); ++i)
        {
            std::string_view text = list_of[i];
            const std::size_t dot = text.find(". ");
            if (dot != std::string_view::npos && dot <= 2)
                text.remove_prefix(dot + 2);
            ui::text(out, ui::face(type.cinema, type.semibold, "1"), std::to_string(i + 1), x, y + 30.0f * 0.85f, 30.0f, cn::kGold);
            const int n = static_cast<int>(type.regular.font->wrap(text, 21.0f, width - 44.0f).size());
            ui::paragraph(out, type.regular, text, x + 44.0f, y + 15.75f + 7.35f, 21.0f, width - 44.0f, 31.5f, cn::kInk, 4);
            y += 31.5f * static_cast<float>(std::max(1, n)) + 14.0f;
        }
        return y - 14.0f;
    };
    if (cn_view_ == CnView::notes)
    {
        // The provider's steps, and the notes for this option.
        list.push_clip({0.0f, top - 56.0f, 1920.0f, 690.0f});
        float y = top - cn_scroll_;
        float bottom = y;
        if (steps)
        {
            cn::kick(ctx_, list, left, y, tr("{provider}: start here", {{"provider", release.provider}}));
            bottom = step_list(list, left, y + 25.6f + 14.0f, 700.0f);
        }
        if (!download_notes(release, ctx_.store.state().library_games()).empty())
        {
            const float x = steps ? left + 770.0f : left;
            const float w = steps ? 1820.0f - x : 1720.0f;
            cn::kick(ctx_, list, x, y, tr("Notes for this option"));
            bottom = std::max(bottom, ui::paragraph(list, type.regular, download_notes(release, ctx_.store.state().library_games()), x, y + 25.6f + 14.0f + 17.6f + 7.7f, 22.0f, w, 35.2f,
                                                    cn::kInk, 40));
        }
        list.pop_clip();
        cn_scroll_end_ = std::max(0.0f, bottom + cn_scroll_ - (1080.0f - 60.0f));
        return;
    }
    // Moving onto the versions or the drives turns the sheet to them.
    const auto version_card = [&](int i)
    {
        const Release &r = game->releases[static_cast<std::size_t>(i)];
        cn::Card card;
        card.kicker = r.provider;
        card.title = format_label(r);
        card.sub = format_bytes(r.size_bytes) + (r.version.empty() ? "" : " \xC2\xB7 v" + r.version) + (r.region.empty() ? "" : " \xC2\xB7 " + r.region);
        const auto presence = release_state(r, state.jobs, state.library_games());
        if (presence.label != nullptr && presence.label[0] != '\0')
            card.foot = tr(presence.label);
        if (!firmware_label(r).empty())
            card.foot += (card.foot.empty() ? "" : " \xC2\xB7 ") + firmware_label(r);
        card.chosen = i == option_;
        card.focus = cn_zone_ == CnZone::versions && i == cn_at_[2];
        card.enabled = !waiting_;
        return card;
    };
    const auto drive_cards = [&]
    {
        std::vector<cn::Card> cards;
        for (int i = 0; i < static_cast<int>(state.drives.size()); ++i)
        {
            const Drive &d = state.drives[static_cast<std::size_t>(i)];
            const SpacePlan plan = plan_space(release, d, state.jobs);
            cn::Card card;
            card.title = i18n::place(d.label);
            card.sub = tr("{size} free", {{"size", format_bytes(d.free_bytes)}}) + " \xC2\xB7 " +
                       (plan.after < 0 ? tr("{size} short", {{"size", format_bytes(-plan.after)}})
                                       : tr("{size} after queue", {{"size", format_bytes(plan.after)}}));
            card.bar = d.total_bytes > 0.0 ? static_cast<float>(1.0 - d.free_bytes / d.total_bytes) : 0.0f;
            card.foot = d.path;
            card.chosen = now.drive != nullptr && d.id == now.drive->id;
            card.focus = cn_zone_ == CnZone::drives && i == cn_at_[4];
            card.enabled = !waiting_;
            cards.push_back(std::move(card));
        }
        if (cards.empty())
        {
            cn::Card none;
            none.title = tr("Connect a writable drive to your PS5.");
            none.enabled = false;
            cards.push_back(none);
        }
        return cards;
    };
    if (cn_view_ == CnView::main && cn_zone_ == CnZone::versions)
    {
        // Every version, two to a row, three rows at a time.
        const int count = static_cast<int>(game->releases.size());
        const int row = cn_at_[2] / 2;
        const int first_row = std::clamp(row - 1, 0, std::max(0, (count + 1) / 2 - 3));
        std::vector<cn::Card> cards;
        for (int i = first_row * 2; i < std::min(count, first_row * 2 + 6); ++i)
            cards.push_back(version_card(i));
        cn::kick(ctx_, list, left, top, tr("Version"));
        const float end = cn::draw_cards(ctx_, &list, left, top + 25.6f + 16.0f, 1720.0f, cards, 2);
        ui::text(list, type.regular, tr("Choose a source and format for this game."), left, end + 20.0f + 18.0f, 18.0f, cn::kFaint);
        return;
    }
    if (cn_view_ == CnView::main && cn_zone_ == CnZone::drives)
    {
        // The drives, and the chosen one's space before and after the queue.
        cn::kick(ctx_, list, left, top, tr("Save to"));
        float y = cn::draw_cards(ctx_, &list, left, top + 25.6f + 16.0f, 1720.0f, drive_cards(), 2);
        if (now.drive != nullptr)
            y = kv(list, left, y + 16.0f, 1720.0f,
                   {{tr("Free now"), format_bytes(now.drive->free_bytes)},
                    {tr("Unfinished downloads"), format_bytes(now.drive->pending_bytes)},
                    {now.plan.planned ? tr("After your queue") : tr("After queue + this download"),
                     now.plan.after < 0 ? tr("{size} short", {{"size", format_bytes(-now.plan.after)}}) : format_bytes(now.plan.after)}});
        ui::text(list, type.regular, tr("Estimates include paused and failed downloads. Other apps can change free space."), left,
                 y + 20.0f + 17.0f, 17.0f, cn::kFaint);
        return;
    }
    // The left column. About: the line and the description; the download:
    // the provider's steps, what matters now and the button that starts it.
    float y = top;
    if (cn_view_ == CnView::about)
    {
        if (!game->tagline.empty())
        {
            const int n = static_cast<int>(type.medium.font->wrap(game->tagline, 32.0f, left_w).size());
            ui::paragraph(list, type.medium, game->tagline, left, y + 21.6f + 11.2f, 32.0f, left_w, 43.2f, cn::kInk, 2);
            y += 43.2f * static_cast<float>(std::min(2, n));
        }
        if (!game->description.empty())
        {
            const int n = std::min(8, static_cast<int>(type.regular.font->wrap(game->description, 21.0f, left_w).size()));
            ui::paragraph(list, type.regular, game->description, left, y + 16.0f + 16.8f + 7.35f, 21.0f, left_w, 33.6f, cn::kDim, n);
        }
    }
    else
    {
        if (steps)
        {
            cn::kick(ctx_, list, left, y, tr("{provider}: start here", {{"provider", release.provider}}));
            y = step_list(list, left, y + 25.6f + 14.0f, left_w);
        }
        y = notes_card(list, left, y, left_w);
        button_row(list, left, y + 30.0f, false);
        ui::text_fit(list, type.regular, tr("Saves a file to your console. Installation and launching are separate."), left,
                     y + 30.0f + 66.0f + 24.0f + 17.0f, 17.0f, left_w, cn::kFaint);
    }
    // The right column: About; or the versions (but for choosing the
    // delivery), the delivery and the drives, as cards.
    if (cn_view_ == CnView::about)
    {
        std::vector<std::pair<std::string, std::string>> facts;
        if (!game->publisher.empty()) facts.emplace_back(tr("Publisher"), game->publisher);
        if (!game->release_date.empty()) facts.emplace_back(tr("Release date"), format_date(game->release_date));
        if (!game->genre.empty()) facts.emplace_back(tr("Genre"), i18n::genre(game->genre));
        facts.emplace_back(tr("Format"), format_label(release));
        facts.emplace_back(tr("Title ID"), release.title_id);
        facts.emplace_back(tr("Source"), release.provider);
        if (!release.region.empty()) facts.emplace_back(tr("Region"), release.region);
        if (!release.version.empty()) facts.emplace_back(tr("Version"), release.version);
        facts.emplace_back(release.compression.empty() ? tr("Download size") : tr("Compressed"), format_bytes(release.size_bytes));
        if (!release.compression.empty() && release.extracted_bytes > 0)
            facts.emplace_back(tr("Installed"), format_bytes(release.extracted_bytes));
        cn::kick(ctx_, list, right, top, tr("About this game"));
        const float end = kv(list, right, top + 25.6f + 16.0f, right_w, facts);
        ui::text_fit(list, type.regular, tr("Saves a file to your console. Installation and launching are separate."), right,
                     end + 24.0f + 17.0f, 17.0f, right_w, cn::kFaint);
        cn_scroll_end_ = 0.0f;
        return;
    }
    float ry = top;
    if (cn_zone_ != CnZone::delivery)
    {
        // Three versions, the chosen one among them.
        const int count = static_cast<int>(game->releases.size());
        const int first = std::clamp(option_ - 1, 0, std::max(0, count - 3));
        std::vector<cn::Card> cards;
        for (int i = first; i < std::min(count, first + 3); ++i)
            cards.push_back(version_card(i));
        cn::kick(ctx_, list, right, ry, tr("Version"));
        ry = cn::draw_cards(ctx_, &list, right, ry + 25.6f + 14.0f, right_w, cards, 3);
        ry += 26.0f;
    }
    if (cinema_count(CnZone::delivery) > 0)
    {
        cn::kick(ctx_, list, right, ry, tr("Download using"));
        std::vector<cn::Card> cards(2);
        cards[0].title = tr("{provider} directly", {{"provider", release.provider}});
        cards[0].chosen = !torbox_;
        cards[1].title = "TorBox";
        cards[1].sub = delivery_note(release);
        cards[1].chosen = torbox_;
        cards[1].enabled = state.debrid.supports(release);
        for (int i = 0; i < 2; ++i)
            cards[static_cast<std::size_t>(i)].focus = cn_zone_ == CnZone::delivery && i == cn_at_[3];
        ry = cn::draw_cards(ctx_, &list, right, ry + 25.6f + 14.0f, right_w, cards, 2) + 26.0f;
    }
    if (now.show_download)
    {
        cn::kick(ctx_, list, right, ry, tr("Save to"));
        const std::vector<cn::Card> cards = drive_cards();
        cn::draw_cards(ctx_, &list, right, ry + 25.6f + 14.0f, right_w, cards, std::max(2, std::min(3, static_cast<int>(cards.size()))));
    }
}

std::vector<hui::ui::Hint> GamePage::cinema_hints() const
{
    if (cn_view_ != CnView::main)
        return {{hui::ui::Button::options, tr("Scroll")}, {hui::ui::Button::circle, tr("Back")}};
    return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Close")}};
}

// ---------------------------------------------------------------- Panorama

namespace
{
// A group of text: no tiles, the content drawn from its top-left corner.
pano::Group text_group(std::string title, int cols, std::function<void(hui::gfx::DrawList &, float, float, float)> body)
{
    pano::Group g;
    g.title = std::move(title);
    g.cols = cols;
    g.below_rows = pano::kRows;
    g.below = std::move(body);
    return g;
}
} // namespace

pano::Page GamePage::panorama_page() const
{
    pano::Page page;
    const auto &state = ctx_.store.state();
    const Game *game = state.game(game_id_);
    if (game == nullptr || game->releases.empty())
        return page;
    const int selected = std::clamp(option_, 0, static_cast<int>(game->releases.size()) - 1);
    const Release &release = game->releases[static_cast<std::size_t>(selected)];
    const Offer now = offer(release);
    const bool steps = now.show_download && release.browser_available && !torbox_;
    const bool reading = pn_view_ != PnView::main;
    const std::string id = game_id_;

    // The download: the art, the main action, favourite and about; the
    // tagline and description under them, then a note or the facts.
    pano::Group download;
    download.title = pano::lower(tr("Download"));
    download.pattern = pano::Pattern::custom;
    download.places = {{2, 0, 2, 1}, {2, 1, 1, 1}, {3, 1, 1, 1}};
    const bool is_addon = !game->addon_id.empty();
    if (is_addon) download.places = {{2, 0, 2, 1}, {2, 1, 2, 1}};
    download.size = is_addon ? 2 : addon_count()>0 ? 4 : 3;
    if (addon_count()>0) download.places.push_back({2,2,2,1});
    download.cols = 4;
    const bool enabled = primary_enabled(release, now) || !now.show_download;
    std::string primary_sub;
    if (waiting_ || !state.paired)
        primary_sub.clear();
    else if (!download_again_ && now.collection.job != nullptr)
    {
        // Where the download is, as measured.
        const Job &job = *now.collection.job;
        primary_sub = now.collection.label != nullptr ? std::string(tr(now.collection.label)) : std::string();
        if (job.status == "downloading" && job.total > 0.0)
            primary_sub += " \xC2\xB7 " + i18n::whole_percent(std::clamp(job.received / job.total, 0.0, 1.0) * 100.0);
    }
    else if (!download_again_ && now.collection.kind == CollectionState::Kind::library && now.collection.library != nullptr)
        primary_sub = tr("In your Library on {location}.", {{"location", i18n::place(now.collection.library->location)}});
    else if (torbox_)
        primary_sub = delivery_note(release);
    else if (now.drive != nullptr)
    {
        primary_sub = tr("{size} \xC2\xB7 to {drive}", {{"size", format_bytes(release.size_bytes)}, {"drive", i18n::place(now.drive->label)}});
        if (now.plan.after < 0)
            primary_sub += " \xC2\xB7 " + tr("{size} short", {{"size", format_bytes(-now.plan.after)}});
    }
    else
        primary_sub = format_bytes(release.size_bytes);
    const std::string label = primary_label(release, download_again_, waiting_, now.collection, state, torbox_);
    const bool favourite = state.favourite(game->id);
    const icons::Icon primary_icon = steps ? icons::Icon::send
                                     : !download_again_ && now.collection.kind == CollectionState::Kind::library ? icons::Icon::books
                                                                                                                  : icons::Icon::down;
    download.fill = [is_addon, addons_label=addon_label(), label, primary_sub, enabled, favourite, primary_icon, waiting = waiting_](int i, pano::Tile &tile)
    {
        tile.has_icon = true;
        if (i == 0)
        {
            tile.key = "primary";
            tile.icon = primary_icon;
            tile.label = label;
            tile.sub = primary_sub;
            tile.has_bg = true;
            tile.bg = enabled && !waiting ? look::accent_color() : ui::Color::rgb(0x3a3c42);
            return;
        }
        if (is_addon && i == 1) i = 2;
        if (i==3) { tile.key="addons"; tile.icon=icons::Icon::grid; tile.label=addons_label; return; }
        tile.key = i == 1 ? "favourite" : "about";
        tile.icon = i == 1 ? (favourite ? icons::Icon::heart_fill : icons::Icon::heart) : icons::Icon::info;
        tile.label = pano::lower(i == 1 ? tr("Favourite") : tr("About"));
        tile.off = i == 1 && waiting;
    };
    std::string note;
    for (const std::string &n : situation(release, now))
        note += (note.empty() ? "" : " ") + n;
    const bool warn = !error_.empty() || (now.show_download && (now.drive == nullptr || !now.plan.enough));
    std::string meta = game->genre.empty() ? std::string() : i18n::genre(game->genre);
    if (game->release_date.size() >= 4)
        meta += (meta.empty() ? "" : " \xC2\xB7 ") + game->release_date.substr(0, 4);
    meta += (meta.empty() ? "" : " \xC2\xB7 ") + std::string(release.platform == "ps4" ? "PS4" : "PS5");
    meta += " \xC2\xB7 " + (release.compression.empty() ? release.provider : format_label(release));
    download.below_rows = pano::kRows;
    download.below = [this, id, note, warn, meta](hui::gfx::DrawList &list, float x, float top, float width)
    {
        const Game *g = ctx_.store.state().game(id);
        if (g == nullptr)
            return;
        pano::Tile art;
        art.game = g;
        pano::draw_tile(ctx_, list, art, {x, top, 2.0f * pano::kStep - pano::kGap, 2.0f * pano::kStep - pano::kGap}, 0.0f);
        float y = top + 2.0f * pano::kStep;
        y += pano::draw_text(ctx_, &list, x, y, width, g->tagline, 30.0f, pano::kInk, pano::Face::light, 1.3f, 1);
        if (!g->description.empty())
            y += 10.0f + pano::draw_text(ctx_, &list, x, y + 10.0f, width, g->description, 19.0f, pano::kDim, pano::Face::body, 1.55f, 3);
        if (!note.empty())
            pano::draw_text(ctx_, &list, x, y + 12.0f, width, note, 19.0f, warn ? pano::kAmber : pano::kInk, pano::Face::body, 1.5f, 2);
        else
            pano::draw_caps(ctx_, &list, x, y + 14.0f, meta, 14.0f, ui::Color::rgb(0xffffff, 0.55f));
    };
    page.groups.push_back(std::move(download));

    if (!reading && now.show_download)
    {
        // Download using: directly, or TorBox (or Orbit Zero when it's on).
        pano::Group using_group;
        using_group.title = pano::lower(tr("Download using"));
        using_group.pattern = pano::Pattern::wide;
        const bool zero = state.system.zero_enabled;
        const bool connected = state.debrid.connected || torbox_;
        const bool torbox_ok = connected ? state.debrid.supports(release)
                                         : static_cast<bool>(ctx_.open_download) || static_cast<bool>(ctx_.open_browser);
        const std::string direct = tr("{provider} directly", {{"provider", release.provider}});
        const std::string torbox_note = connected ? delivery_note(release) : std::string(tr("Connect your TorBox account in App settings."));
        using_group.size = zero ? 1 : 2;
        using_group.fill = [zero, connected, torbox_ok, direct, torbox_note, chosen = torbox_, waiting = waiting_,
                            available = state.debrid.supports(release)](int i, pano::Tile &tile)
        {
            tile.has_icon = true;
            if (zero)
            {
                tile.key = "use:zero";
                tile.icon = icons::Icon::laptop;
                tile.label = "Orbit Zero";
                tile.check = tile.selected = true;
                return;
            }
            tile.key = i == 0 ? "use:0" : "use:1";
            tile.icon = i == 0 ? icons::Icon::cloud : icons::Icon::link;
            tile.label = i == 0 ? direct : connected ? std::string("TorBox") : std::string(tr("Set up TorBox"));
            if (i == 1)
            {
                tile.sub = torbox_note;
                if (connected && available && !chosen)
                    tile.kicker = tr("TorBox available");
            }
            tile.check = tile.selected = (i == 1) == chosen;
            tile.off = waiting || (i == 1 && !torbox_ok);
        };
        page.groups.push_back(std::move(using_group));
    }

    if (now.show_download)
    {
        // Save to: every drive with its bar and what stays free after the queue.
        pano::Group drives;
        drives.title = pano::lower(tr("Save to"));
        drives.pattern = pano::Pattern::wide;
        drives.size = std::max(1, static_cast<int>(state.drives.size()));
        const std::string chosen = now.drive != nullptr ? now.drive->id : std::string();
        drives.fill = [this, chosen, &release](int i, pano::Tile &tile)
        {
            const auto &now_state = ctx_.store.state();
            tile.has_icon = true;
            tile.icon = icons::Icon::drive;
            if (now_state.drives.empty())
            {
                tile.key = "drive:none";
                tile.icon = icons::Icon::warn;
                tile.label = tr("Connect a writable drive to your PS5.");
                tile.off = true;
                return;
            }
            const Drive &d = now_state.drives[static_cast<std::size_t>(i)];
            const SpacePlan plan = plan_space(release, d, now_state.jobs);
            tile.key = "drive:" + std::to_string(i);
            tile.label = i18n::place(d.label);
            tile.sub = tr("{size} free", {{"size", format_bytes(d.free_bytes)}}) + " \xC2\xB7 " +
                       (plan.after < 0 ? tr("{size} short", {{"size", format_bytes(-plan.after)}})
                                       : tr("{size} after queue", {{"size", format_bytes(plan.after)}}));
            if (d.total_bytes > 0.0)
                tile.bar = static_cast<float>(std::clamp(1.0 - d.free_bytes / d.total_bytes, 0.0, 1.0));
            tile.bar_warn = plan.after < 0;
            tile.check = tile.selected = d.id == chosen;
            tile.off = waiting_;
        };
        page.groups.push_back(std::move(drives));
    }

    if (!reading)
    {
        // Versions: three, then the rest under More versions.
        const int count = static_cast<int>(game->releases.size());
        const auto versions = [this, id, selected](int first, int size, std::string title)
        {
            pano::Group g;
            g.title = std::move(title);
            g.pattern = pano::Pattern::wide;
            g.size = size;
            g.fill = [this, id, selected, first](int i, pano::Tile &tile)
            {
                const auto &now_state = ctx_.store.state();
                const Game *game = now_state.game(id);
                if (game == nullptr)
                    return;
                const int at = first + i;
                const Release &r = game->releases[static_cast<std::size_t>(at)];
                tile.key = "ver:" + std::to_string(at);
                tile.kicker = r.provider;
                tile.label = format_label(r) + " \xC2\xB7 " + format_bytes(r.size_bytes);
                std::string sub = r.version.empty() ? std::string() : "v" + r.version;
                if (!r.region.empty())
                    sub += (sub.empty() ? "" : " \xC2\xB7 ") + r.region;
                const CollectionState presence = release_state(r, now_state.jobs, now_state.library_games());
                if (presence.label != nullptr)
                    sub += (sub.empty() ? "" : " \xC2\xB7 ") + std::string(tr(presence.label));
                if (!firmware_label(r).empty())
                    sub += (sub.empty() ? "" : " \xC2\xB7 ") + firmware_label(r);
                tile.sub = sub;
                tile.check = tile.selected = at == selected;
                tile.off = waiting_;
            };
            return g;
        };
        page.groups.push_back(versions(0, std::min(count, 3), pano::lower(tr("Versions"))));
        if (count > 3)
        {
            pano::Group more = versions(3, count - 3, pano::lower(tr("More versions")));
            if (count - 3 < pano::kRows)
            {
                // Under the last of them, when there's room.
                more.below_rows = pano::kRows - (count - 3);
                more.below = [this](hui::gfx::DrawList &list, float x, float top, float width)
                { pano::draw_text(ctx_, &list, x, top, width, tr("Choose a source and format for this game."), 18.0f, pano::kFaint); };
            }
            page.groups.push_back(std::move(more));
        }
        // What else this game offers: another copy, the related one.
        std::vector<std::pair<std::string, std::string>> extras;
        if (now.collection.kind == CollectionState::Kind::related && now.collection.library != nullptr)
            extras.emplace_back("related", tr("View related copy"));
        if (!download_again_ && (now.collection.kind == CollectionState::Kind::library ||
                                 (now.collection.job != nullptr && now.collection.job->status == "complete")))
            extras.emplace_back("again", tr("Download another copy"));
        if (!extras.empty())
        {
            pano::Group or_group;
            or_group.title = pano::lower(tr("Or"));
            or_group.pattern = pano::Pattern::wide;
            or_group.size = static_cast<int>(extras.size());
            or_group.fill = [extras](int i, pano::Tile &tile)
            {
                tile.key = extras[static_cast<std::size_t>(i)].first;
                tile.has_icon = true;
                tile.icon = tile.key == "again" ? icons::Icon::copy : icons::Icon::books;
                tile.label = extras[static_cast<std::size_t>(i)].second;
            };
            page.groups.push_back(std::move(or_group));
        }
        if (now.show_download && now.drive != nullptr && pn_space_)
        {
            // Space, while a drive is focused: the chosen drive now, its
            // unfinished downloads and after the queue.
            const std::vector<std::pair<std::string, std::string>> pairs = {
                {tr("Free now"), format_bytes(now.drive->free_bytes)},
                {tr("Unfinished downloads"), format_bytes(now.drive->pending_bytes)},
                {now.plan.planned ? tr("After your queue") : tr("After queue + this download"),
                 now.plan.after < 0 ? tr("{size} short", {{"size", format_bytes(-now.plan.after)}}) : format_bytes(now.plan.after)}};
            page.groups.push_back(text_group(pano::lower(tr("Space")), 2,
                                             [this, pairs](hui::gfx::DrawList &list, float x, float top, float width)
                                             {
                                                 const float h = pano::draw_pairs(ctx_, &list, x, top - 16.0f, width, pairs, 1);
                                                 pano::draw_text(ctx_, &list, x, top - 16.0f + h + 18.0f, width,
                                                                 tr("Estimates include paused and failed downloads. Other apps can change free space."),
                                                                 17.0f, pano::kFaint, pano::Face::body, 1.5f, 4);
                                             }));
        }
    }

    // Source notes under the provider's steps (or under the start of a
    // direct download's notes): it slides the page to the notes.
    const float text_w = 3.0f * pano::kStep - pano::kGap;
    const auto with_notes_button = [this](pano::Group &g, float y)
    {
        pano::Button b = pano::button(tr("Source notes"), icons::Icon::list);
        g.pattern = pano::Pattern::pixels;
        g.rects = {{0.0f, y, pano::button_width(ctx_, b), pano::button_height(b)}};
        g.size = 1;
        g.fill = [](int, pano::Tile &tile)
        {
            tile.key = "notes";
            tile.button = true;
            tile.has_icon = true;
            tile.icon = icons::Icon::list;
            tile.label = tr("Source notes");
        };
    };
    if (steps && pn_view_ != PnView::about)
    {
        const std::vector<std::string> list_steps = provider_steps(release);
        const float h = pano::draw_steps(ctx_, nullptr, 0.0f, 0.0f, text_w, list_steps);
        pano::Group g = text_group(pano::lower(tr("{provider}: start here", {{"provider", release.provider}})), 3,
                                   [this, list_steps](hui::gfx::DrawList &list, float x, float top, float w)
                                   { pano::draw_steps(ctx_, &list, x, top - 16.0f, w, list_steps); });
        if (pn_view_ == PnView::main)
            with_notes_button(g, h - 16.0f + 22.0f);
        page.groups.push_back(std::move(g));
    }
    else if (!steps && !download_notes(release, ctx_.store.state().library_games()).empty() && pn_view_ == PnView::main)
    {
        const std::string notes = download_notes(release, ctx_.store.state().library_games());
        const float h = pano::draw_text(ctx_, nullptr, 0.0f, 0.0f, text_w, notes, 28.0f, pano::kInk, pano::Face::light, 1.45f, 4);
        pano::Group g = text_group(pano::lower(tr("Notes for this option")), 3,
                                   [this, notes](hui::gfx::DrawList &list, float x, float top, float w)
                                   { pano::draw_text(ctx_, &list, x, top - 10.0f, w, notes, 28.0f, pano::kInk, pano::Face::light, 1.45f, 4); });
        with_notes_button(g, h - 10.0f + 22.0f);
        page.groups.push_back(std::move(g));
    }
    // Reading: the notes for this option, or About, scrolled by Options.
    const auto scrolled = [this](std::function<float(hui::gfx::DrawList *, float, float, float)> body)
    {
        return [this, body](hui::gfx::DrawList &list, float x, float top, float width)
        {
            const float room = 1000.0f - top;
            const float h = body(nullptr, x, top, width);
            pn_scroll_end_ = std::max(0.0f, h - room);
            list.push_clip({x - 8.0f, top - 4.0f, width + 16.0f, room + 4.0f});
            body(&list, x, top - std::min(pn_scroll_, pn_scroll_end_), width);
            list.pop_clip();
        };
    };
    if (pn_view_ == PnView::notes && !download_notes(release, ctx_.store.state().library_games()).empty())
    {
        const std::string notes = download_notes(release, ctx_.store.state().library_games());
        page.groups.push_back(text_group(pano::lower(tr("Notes for this option")), 3,
                                         scrolled([this, notes](hui::gfx::DrawList *list, float x, float top, float width)
                                                  { return pano::draw_text(ctx_, list, x, top - 10.0f, width, notes, 28.0f, pano::kInk,
                                                                           pano::Face::light, 1.45f, 40); })));
    }
    if (pn_view_ == PnView::about)
    {
        std::vector<std::pair<std::string, std::string>> facts;
        if (!game->publisher.empty())
            facts.emplace_back(tr("Publisher"), game->publisher);
        if (!game->release_date.empty())
            facts.emplace_back(tr("Release date"), format_date(game->release_date));
        if (!game->genre.empty())
            facts.emplace_back(tr("Genre"), i18n::genre(game->genre));
        facts.emplace_back(tr("Format"), format_label(release));
        facts.emplace_back(tr("Title ID"), release.title_id);
        facts.emplace_back(tr("Source"), release.provider);
        if (!release.region.empty())
            facts.emplace_back(tr("Region"), release.region);
        if (!release.version.empty())
            facts.emplace_back(tr("Version"), release.version);
        facts.emplace_back(release.compression.empty() ? tr("Download size") : tr("Compressed"), format_bytes(release.size_bytes));
        if (!release.compression.empty() && release.extracted_bytes > 0)
            facts.emplace_back(tr("Installed"), format_bytes(release.extracted_bytes));
        page.groups.push_back(text_group(pano::lower(tr("About this game")), 4,
                                         scrolled([this, facts](hui::gfx::DrawList *list, float x, float top, float width)
                                                  {
                                                      const float h = pano::draw_pairs(ctx_, list, x, top - 16.0f, width, facts, 3);
                                                      return h - 16.0f + 24.0f +
                                                             pano::draw_text(ctx_, list, x, top - 16.0f + h + 24.0f, width,
                                                                             tr("Saves a file to your console. Installation and launching are separate."),
                                                                             17.0f, pano::kFaint);
                                                  })));
    }
    return page;
}

Intent GamePage::update_panorama(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    Intent intent;
    const auto &state = ctx_.store.state();
    const Game *game = state.game(game_id_);
    if (game == nullptr || game->releases.empty())
    {
        if (focused && (input.is_pressed(Action::back) || input.is_pressed(Action::confirm)))
            intent.kind = Intent::Kind::close;
        return intent;
    }
    option_ = std::clamp(option_, 0, static_cast<int>(game->releases.size()) - 1);
    if (follow(intent))
        return intent;
    const auto &release = game->releases[static_cast<std::size_t>(option_)];
    const auto run = [&](Slot slot, int index, Sheet context)
    {
        // A choice applies in its sheet's context, as the Store's sheets do.
        sheet_ = context;
        intent = activate({slot, index, {}, true, false}, *game, release, offer(release), feedback);
        sheet_ = Sheet::none;
    };
    pano::Page page = panorama_page();
    pn_.clamp(page);
    const std::string key = pn_.focused_key(page);
    if (focused && pn_view_ != PnView::main)
    {
        // About or the notes: Options scrolls them, Circle goes back.
        if (input.is_pressed(Action::back))
        {
            // Back on the tile that opened it.
            const std::string opener = pn_view_ == PnView::about ? "about" : "notes";
            pn_view_ = PnView::main;
            pn_.focus_key(panorama_page(), opener);
            feedback.play(hui::audio::Cue::back);
            return intent;
        }
        else if (input.is_pressed(Action::menu))
        {
            pn_scroll_ = pn_scroll_ >= pn_scroll_end_ ? 0.0f : std::min(pn_scroll_ + 300.0f, pn_scroll_end_);
            feedback.play(hui::audio::Cue::focus);
        }
        else if (input.nav != Direction::none || input.is_pressed(Action::confirm))
            refuse(feedback, input);
    }
    else if (focused)
    {
        if (input.nav != Direction::none)
            pn_.move(page, input, feedback);
        else if (input.is_pressed(Action::page_prev) || input.is_pressed(Action::page_next))
        {
            if (!pn_.jump(page, input.is_pressed(Action::page_next) ? 1 : -1, feedback))
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::back))
        {
            intent.kind = Intent::Kind::close;
            feedback.play(hui::audio::Cue::back);
            return intent;
        }
        else if (input.is_pressed(Action::north) && !waiting_)
            run(Slot::favourite, 0, Sheet::none);
        else if (input.is_pressed(Action::confirm))
        {
            const Offer now = offer(release);
            const auto at = [&](std::size_t prefix) { return std::atoi(key.c_str() + prefix); };
            if (key == "primary")
            {
                if (!waiting_ && (!now.show_download || primary_enabled(release, now)))
                    run(Slot::primary, 0, Sheet::downloads);
                else
                    refuse(feedback, input);
            }
            else if (key == "favourite" && !waiting_)
                run(Slot::favourite, 0, Sheet::none);
            else if (key == "addons") run(Slot::addons, 0, Sheet::none);
            else if (key == "about" || key == "notes")
            {
                pn_view_ = key == "about" ? PnView::about : PnView::notes;
                pn_scroll_ = 0.0f;
                feedback.play(hui::audio::Cue::select);
            }
            else if (waiting_)
                refuse(feedback, input);
            else if (key == "use:zero")
                run(Slot::delivery, 0, Sheet::downloads);
            else if (key.rfind("use:", 0) == 0)
            {
                const bool connected = state.debrid.connected || torbox_;
                const bool ok = at(4) == 0 || (connected ? state.debrid.supports(release)
                                                         : static_cast<bool>(ctx_.open_download) || static_cast<bool>(ctx_.open_browser));
                if (ok)
                    run(Slot::choice, at(4), Sheet::delivery);
                else
                    refuse(feedback, input);
            }
            else if (key.rfind("drive:", 0) == 0 && key != "drive:none")
                run(Slot::choice, at(6), Sheet::storage);
            else if (key.rfind("ver:", 0) == 0)
                run(Slot::choice, at(4), Sheet::source);
            else if (key == "again")
                run(Slot::again, 0, Sheet::downloads);
            else if (key == "related")
                run(Slot::related, 0, Sheet::downloads);
            else
                refuse(feedback, input);
        }
    }
    // A choice can add or remove groups: keep the focus on the same tile.
    const std::string now_key = pn_.focused_key(page);
    pn_space_ = pn_view_ == PnView::main && now_key.rfind("drive:", 0) == 0;
    page = panorama_page();
    if (!now_key.empty() && pn_.focused_key(page) != now_key)
        pn_.focus_key(page, now_key);
    pn_.show_group(pn_view_ != PnView::main ? static_cast<int>(page.groups.size()) - 1 : -1);
    pn_.animate(page, dt);
    return intent;
}

void GamePage::draw_panorama(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const auto &state = ctx_.store.state();
    const Game *game = state.game(game_id_);
    if (game == nullptr || game->releases.empty())
    {
        pano::draw_empty(ctx_, list, {}, tr("This game is no longer available"),
                         tr("Its download source was turned off. Press Circle to go back."), {});
        return;
    }
    // About and the notes are read, not chosen from: nothing is lit.
    const std::string title = (!game->addon_id.empty() && !game->base_title.empty() ? game->base_title + " · " : "") + game->title;
    pano::draw_header(ctx_, list, pano::lower(title), pn_.scroll() * 0.35f, 200.0f, 26.0f);
    pn_.draw(ctx_, list, panorama_page(), pn_view_ == PnView::main);
}

std::vector<hui::ui::Hint> GamePage::panorama_hints() const
{
    const Game *game = ctx_.store.state().game(game_id_);
    if (game == nullptr || game->releases.empty())
        return {{hui::ui::Button::circle, tr("Back")}};
    if (pn_view_ != PnView::main)
        return {{hui::ui::Button::options, tr("Scroll")}, {hui::ui::Button::circle, tr("Back")}};
    return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Back")},
            {hui::ui::Button::l1, tr("Groups"), hui::ui::Button::r1}};
}

} // namespace orbit
