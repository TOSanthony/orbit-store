// Orbit Store TV app - Artwork-led game hub and controller download sheets.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/screens.hpp"
#include "orbit/i18n.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace orbit
{
namespace
{
constexpr float kBodyTop = 430.0f;
constexpr float kBodyBottom = 978.0f;
// Every sheet uses the same slightly taller panel.
constexpr float kTallExtra = 70.0f;
constexpr float kLeft = 70.0f;
// Each sentence on its own line ("A. B." becomes "A.\nB.").
std::string one_per_line(std::string text)
{
    for (std::size_t at = text.find(". "); at != std::string::npos; at = text.find(". ", at + 1))
        text.replace(at + 1, 1, "\n");
    return text;
}


// The About button and sheet are named for what the page is: a game, an app, a DLC or an update.
static const char *about_title(const Game &game)
{
    switch (game_category(game))
    {
    case Category::apps:
        return tr("About this app");
    case Category::dlc:
        return tr("About this DLC");
    case Category::updates:
        return tr("About this update");
    case Category::game:
        break;
    }
    return tr("About this game");
}

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
        arc(0, -8, 12, 4.5f, 0.0f, 2.0f * kPi);
        line(-12, -8, -12, 8);
        line(12, -8, 12, 8);
        arc(0, 8, 12, 4.5f, 0.0f, kPi);
        arc(0, 0, 12, 4.5f, 0.0f, kPi);
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
        return state.browser.active ? std::string(tr("View verification")) : tr("Open {provider} on PS5", {{"provider",release.provider}});
    return collection.job != nullptr || again ? tr("Download again") : tr("Download to PS5");
}
std::string heading(std::string value)
{
    // Preserve UTF-8; uppercase ASCII just as the browser's title treatment.
    for (char &c : value) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    return value;
}
}

GamePage::GamePage(Context &context) : Screen(context) {}

void GamePage::open(const std::string &game_id)
{
    game_id_ = game_id;
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

const Drive *GamePage::drive() const
{
    const auto &state = ctx_.store.state();
    for (const auto &d : state.drives) if (d.id == drive_id_) return &d;
    // A disconnected destination must be chosen explicitly, never substituted.
    if (!drive_id_.empty()) return nullptr;
    const int at = default_drive(state.drives, state.system.preferred_storage);
    return at >= 0 ? &state.drives[static_cast<std::size_t>(at)] : nullptr;
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

Release GamePage::target(const Game &game, const Release &chosen) const
{
    if (!all_ || !choosing(game)) return chosen;
    // Everything not already queued on this drive, added up: that is what the drive must hold.
    const auto &state = ctx_.store.state();
    const Drive *to = drive();
    Release sum = chosen;
    sum.id.clear();
    sum.size_bytes = 0;
    sum.extracted_bytes = 0;
    sum.version.clear();
    sum.title_id.clear();
    sum.browser_available = false;
    for (const auto &r : game.releases)
    {
        bool taken = false;
        if (to != nullptr)
            for (const auto &job : state.jobs)
                if (job.release_id == r.id && job.storage_id == to->id && job.status != "error" && job.status != "cancelled")
                    taken = true;
        if (taken) continue;
        sum.size_bytes += r.size_bytes;
        sum.extracted_bytes += r.extracted_bytes;
    }
    return sum;
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
    const auto &debrid = ctx_.store.state().debrid;
    if (debrid.connected)
        for (const auto &host : debrid.hosts)
            if (host.source_id == release.source_id && host.available && host.max_file_size > 0 &&
                release.size_bytes > host.max_file_size)
                return tr("TorBox currently allows up to {size} for this source. Choose the original source for this file.",
                          {{"size", format_bytes(host.max_file_size)}});
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
    const Release release = target(*game, game->releases[static_cast<std::size_t>(selected)]);
    Offer now = offer(release);
    if (fresh_layout_) { now.collection = CollectionState{}; now.show_download = true; }
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
            // Chosen option = active (pale); solid white while the cursor is on it.
            ui::state_fill(*list, r, 20, focus, checked);
            list->push_opacity(enabled ? 1 : 0.4f);
            if (checked)
            {
                list->circle(x + 34, y + 44, 15, focus > 0.5f ? ui::color::night1 : ui::color::text);
                ui::icon_check(*list, x + 34, y + 44, 18, focus > 0.5f ? ui::color::text : ui::color::night1);
            }
            else list->ring(x + 34, y + 44, 14, 1, focus > 0.5f ? ui::Color::rgb(0x080808, 0.6f) : ui::color::text3);
            ui::paragraph(*list, type.medium, title, x + 68, y + 47, 27, text_width, 35, ui::state_ink(focus, checked), 999);
            if (!detail.empty())
                ui::paragraph(*list, type.regular, detail, x + 68, y + 35 + title_height, 21,
                              text_width, 30.45f, ui::state_ink_dim(focus), 999);
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
        const auto lines = type.medium.font->wrap(title, size, width);
        if (list) ui::paragraph(*list, type.medium, title, kLeft, y + size * 0.85f, size,
                               width, size * 1.04f, ui::color::text, static_cast<int>(lines.size()));
        y += static_cast<float>(lines.size()) * size * 1.04f + 25;
        paragraph(game->tagline, kLeft, y, width, 25, ui::color::text);
        if (!game->tagline.empty()) y += 15;
        paragraph(game->description, kLeft, y, std::min(width, type.regular.measure("0", 22) * 54), 22);
        y += 24;
        // Server, format, size and platform as pills shaped like the download button.
        {
            const FactIcon size_kind = size_icon(release);
            const struct { FactIcon icon; std::string text; } pills[] = {
                {FactIcon::source,release.provider},
                {FactIcon::format,format_label(release)},
                {size_kind,format_bytes(release.size_bytes)},
                {release.platform == "ps4" ? FactIcon::platform4 : FactIcon::platform,release.platform == "ps4" ? "PS4" : "PS5"}};
            float px = kLeft, py = y + 6;
            for (const auto &pill : pills)
            {
                if (pill.text.empty()) continue;
                const float text_w = type.medium.measure(pill.text, 23);
                const float w = 22 + 32 + 12 + text_w + 28;
                if (px > kLeft && px + w > kLeft + 1000) { px = kLeft; py += 76; }
                if (list)
                {
                    const ui::Rect r{px,py,w,64};
                    list->rounded_rect(r,32,ui::Color::rgb(0xffffff,0.157f));
                    list->bordered_rect(r,32,ui::Color{0,0,0,0},1,ui::color::text.with_alpha(0.35f));
                    fact_icon(*list,type,pill.icon,px+22+16,r.cy(),36,ui::color::text);
                    ui::text(*list,type.medium,pill.text,px+22+32+12,r.cy()+8,23,ui::color::text);
                }
                px += w + 12;
            }
            y = py + 64 + 24;
        }
        {
            std::string meta;
            if (!game->genre.empty()) meta = i18n::genre(game->genre);
            if (!game->release_date.empty()) meta += (meta.empty() ? "" : " \xC2\xB7 ") + game->release_date.substr(0, 4);
            paragraph(meta, kLeft, y, width, 22);
        }
        y = std::max(830.0f, y + 60);
        const std::string label = primary_label(release, download_again_, waiting_, now.collection, state, torbox_);
        const float button_width = std::clamp(type.medium.measure(label, 23) + 76, 460.0f, 800.0f);
        const ui::Rect primary_rect{kLeft,y,button_width,88};
        control(Slot::primary, 0, primary_rect, label, !waiting_, false, false);
        // The same 1 px edge as the favourite and About buttons.
        if (list)
            list->bordered_rect(primary_rect,44,ui::Color{0,0,0,0},1,ui::color::text.with_alpha(0.35f));
        for (int i=0;i<2;++i)
        {
            const ui::Rect r{kLeft + button_width + 24 + i * 108.0f,y,88,88};
            const Slot slot = i == 0 ? Slot::favourite : Slot::about;
            const float focus = control(slot,0,r,"",!waiting_);
            if (list)
            {
                list->bordered_rect(r,44,ui::Color{0,0,0,0},1,ui::color::text.with_alpha(0.35f));
                if (i == 0)
                {
                    if (state.favourite(game->id))
                        ui::icon_heart_filled(*list,r.cx(),r.cy(),36,ui::color::heart);
                    else
                        ui::icon_heart(*list,r.cx(),r.cy(),36,focus > 0.5f ? ui::Color::rgb(0x080808) : ui::color::text);
                }
                else for (int d=-1;d<=1;++d) list->circle(r.cx()+d*12,r.cy(),3,focus > 0.5f ? ui::Color::rgb(0x080808) : ui::color::text);
                if (focus > 0)
                    ui::text(*list,type.regular,i == 0 ? (state.favourite(game->id) ? tr("Saved to favourites") : tr("Add to favourites")) : about_title(*game),
                             r.cx(),y+123,18,ui::color::text2,ui::Align::center);
            }
        }
        y += 140;
        paragraph(error_,kLeft,y,1000,21);
        out.end = y;
        return out;
    }
    // Circle goes back, so the sheets carry no Back or Close buttons; all body content scrolls.
    float y = 0;
    if (sheet == Sheet::downloads)
    {
        // Cover and title on the left; settings, storage and the download button on the right.
        constexpr float nx = 570, nw = 1236;
        // With a drive to save to, the three option rows take the left half of the column and
        // the storage card the right half, as tall as the three rows.
        // A finished download shows the drive it was saved to, in the same card.
        const Drive *installed_drive = nullptr;
        if (!now.show_download && now.collection.job && now.collection.job->status == "complete")
            for (const auto &d : state.drives)
                if (d.id == now.collection.job->storage_id) installed_drive = &d;
        // A game found in Library with no download of ours: the drive it sits on, by its path
        // (the longest drive path that holds it), else by the place the library reports.
        bool in_library = false;
        if (!now.show_download && !now.collection.job &&
            now.collection.kind == CollectionState::Kind::library && now.collection.library != nullptr)
        {
            const auto &found = *now.collection.library;
            std::size_t best = 0;
            for (const auto &d : state.drives)
            {
                const bool holds = !d.path.empty() && found.path.compare(0, d.path.size(), d.path) == 0 &&
                    (found.path.size() == d.path.size() || found.path[d.path.size()] == '/');
                if (holds && d.path.size() >= best) { installed_drive = &d; best = d.path.size(); }
            }
            if (installed_drive == nullptr)
                for (const auto &d : state.drives)
                    if (!found.location.empty() && i18n::place(d.label) == i18n::place(found.location))
                        installed_drive = &d;
            in_library = installed_drive != nullptr;
        }
        const bool installed = installed_drive != nullptr;
        const bool side = (now.show_download && now.drive != nullptr) || installed;
        // An existing download keeps the Source row at the width it has when downloading.
        const float rw = (side || (!now.show_download && now.collection.job)) ? 606.0f : nw;
        constexpr float kRowsHeight = 3 * 76.0f + 2 * 10.0f;
        // The game's title sits above the cover; everything else is lowered to match.
        constexpr float top = 96;
        float ry = top;
        const auto setting = [&](Slot slot,FactIcon icon,const std::string &label,const std::string &value,const std::string &note)
        {
            const ui::Rect r{nx,ry,rw,76};
            const float f = focused && static_cast<int>(out.controls.size()) == focus_ ? 1 : 0;
            out.controls.push_back({slot,0,r,!waiting_,false});
            if (list)
            {
                ui::state_fill(*list,r,20,f);
                const ui::Color ink=ui::state_ink(f),dim=ui::state_ink_dim(f);
                list->circle(nx+50,ry+38,24,f>0.5f?ui::Color::rgb(0x080808,0.1f):ui::Color::rgb(0xffffff,0.14f));
                fact_icon(*list,type,icon,nx+50,ry+38,30,ink);
                ui::text(*list,type.regular,label,nx+92,ry+29,17,dim);
                const float value_w = std::min(type.medium.measure(value,24),rw-160);
                ui::text_fit(*list,type.medium,value,nx+92,ry+60,24,rw-160,ink);
                if (!note.empty() && rw-160-value_w-14 > 120)
                    ui::text_fit(*list,type.regular,note,nx+92+value_w+14,ry+60,17,rw-160-value_w-14,dim);
                const float cx = nx + rw - 34;
                list->line(cx-5,ry+31,cx+3,ry+38,2,dim);
                list->line(cx+3,ry+38,cx-5,ry+45,2,dim);
            }
            ry += 86;
        };
        if (all_ && choosing(*game))
            setting(Slot::source,FactIcon::source,tr("Source"),game_category(*game)==Category::dlc ? tr("All DLC") : tr("All updates"),
                    tr("{count} items",{{"count",std::to_string(game->releases.size())}}) + " \xC2\xB7 " + format_bytes(release.size_bytes));
        else
        setting(Slot::source,FactIcon::source,tr("Source"),release.provider + " \xC2\xB7 " + format_label(release),
                release.title_id + (release.version.empty()?"":" \xC2\xB7 v"+release.version) + " \xC2\xB7 " + format_bytes(release.size_bytes));
        if (now.show_download)
        {
            setting(Slot::delivery,FactIcon::download,tr("Download using"),state.system.zero_enabled ? "Orbit Zero" : torbox_ ? "TorBox" : tr("{provider} directly",{{"provider",release.provider}}),
                    state.system.zero_enabled ? "Uses your computer preference" : state.debrid.supports(release) && !torbox_ ? tr("TorBox available") : "");
            setting(Slot::drive,FactIcon::disk,tr("Save to"),now.drive ? i18n::place(now.drive->label) : tr("Select storage"),
                    now.drive ? tr("{size} free",{{"size",format_bytes(now.drive->free_bytes)}}) : "");
        }
        else if (now.collection.job && now.collection.job->status == "complete")
            // A finished download offers a second copy as a row of its own, under Source.
            setting(Slot::again,FactIcon::download,tr("Downloaded"),tr("Download another copy"),"");
        else if (in_library)
            // A game found in Library gets the same row, with the drive it sits on beside it.
            setting(Slot::again,FactIcon::download,tr("In your Library"),tr("Download another copy"),"");
        ry -= 86;
        ry += 76 + 10;
        if (list)
        {
            // Left column: the cover, the title and a few pills.
            const ui::Rect cover{116,top,400,400};
            list->shadow({cover.x,cover.y+10,cover.w,cover.h},ui::kArtRadius,28,ui::Color::rgb(0x000000,0.45f));
            draw_cover(ctx_,*list,*game,cover,ui::kArtRadius);
            ui::text_fit(*list,type.semibold,game->title,116,top-28,44,1690,ui::color::text);
            struct Pill { FactIcon icon; std::string text; };
            std::vector<Pill> pills{{release.platform=="ps4"?FactIcon::platform4:FactIcon::platform,release.platform=="ps4"?"PS4":"PS5"}};
            if (!release.version.empty()) pills.push_back({FactIcon::version,"v"+release.version});
            pills.push_back({all_ && choosing(*game) ? FactIcon::disk : size_icon(release),format_bytes(release.size_bytes)});
            // The pills are centred under the cover; those that do not fit are left out.
            const auto pill_width = [&](const Pill &pill) { return 12+26+8+type.medium.measure(pill.text,17)+16; };
            std::size_t shown = 0;
            float total = 0;
            for (const auto &pill : pills)
            {
                const float w = pill_width(pill) + (shown > 0 ? 8.0f : 0.0f);
                if (total+w > cover.w) break;
                total += w;++shown;
            }
            float px = cover.cx() - total*0.5f;
            for (std::size_t i = 0; i < shown; ++i)
            {
                const auto &pill = pills[i];
                const float w = pill_width(pill);
                list->rounded_rect({px,top+420,w,40},20,ui::Color::rgb(0xffffff,0.16f));
                fact_icon(*list,type,pill.icon,px+12+13,top+440,26,ui::color::text);
                ui::text(*list,type.medium,pill.text,px+12+26+8,top+447,17,ui::color::text);
                px += w + 8;
            }
        }
        if (((now.show_download && now.drive) || installed) && list)
        {
            const Drive &drive=installed?*installed_drive:*now.drive;
            const ui::Rect card_rect = side ? ui::Rect{nx+rw+24,top,rw,kRowsHeight} : ui::Rect{nx,ry,nw,140};
            ui::surface(*list,card_rect,20,ui::color::field);
            if(drive.total_bytes>0)
            {
                // The drive as a ring: what is used or queued (white), this game
                // (green, red when it does not fit) and what stays free.
                const double total=drive.total_bytes;
                const double game_bytes=release.size_bytes+release.extracted_bytes;
                const double queued=installed?std::max(0.0,total-drive.free_bytes-game_bytes)
                    :std::max(0.0,total-drive.projected_free_bytes-(now.plan.planned?game_bytes:0.0));
                const double room=std::max(0.0,total-queued);
                const double game_shown=std::min(game_bytes,room);
                const bool fits=installed||now.plan.after>=0;
                const double after=installed?drive.free_bytes:std::abs(now.plan.after);
                const float used_part=static_cast<float>(std::clamp(queued/total,0.0,1.0));
                const float game_part=static_cast<float>(std::clamp(game_shown/total,0.0,1.0));
                constexpr float radius=54,thickness=12,tau=6.2831853f;
                const float cy=card_rect.cy(),cx=card_rect.x+28+radius+6,right=card_rect.x+card_rect.w-28;
                const ui::Color game_color=fits?ui::color::fits:ui::color::short_of;
                const struct { std::string label; double bytes; ui::Color dot; } rows[]={
                    {tr("Used and queued"),queued,ui::color::text},
                    {tr("This game"),game_bytes,game_color},
                    {installed?tr("Free now"):fits?tr("Free after install"):tr("Short by"),after,fits?ui::Color::rgb(0xffffff,0.35f):ui::color::short_of}};
                float values_w=0,labels_w=0;
                for(const auto &row:rows)
                {
                    values_w=std::max(values_w,type.medium.measure(format_bytes(row.bytes),20));
                    labels_w=std::max(labels_w,type.regular.measure(row.label,20));
                }
                const float text_x=cx+radius+thickness+(side?30.0f:36.0f);
                const float dot_x=side?text_x+6.0f:right-values_w-40-labels_w-24;
                const float label_x=side?dot_x+24.0f:dot_x+24.0f;
                list->ring(cx,cy,radius,thickness,ui::Color::rgb(0xffffff,0.27f));
                if(used_part>0.0f)list->arc(cx,cy,radius,thickness,0.0f,used_part*tau,ui::color::text,true);
                if(game_part>0.0f)list->arc(cx,cy,radius,thickness,(used_part+0.006f)*tau,game_part*tau,game_color,true);
                {
                    // Share of the drive taken once this game is installed.
                    const int percent=static_cast<int>(std::lround(std::clamp((queued+game_bytes)/total,0.0,1.0)*100.0));
                    const ui::Color ink=fits?ui::color::text:ui::color::short_of;
                    ui::text(*list,type.semibold,std::to_string(percent)+"%",cx,cy+4,24,ink,ui::Align::center);
                    ui::text(*list,type.regular,tr("used"),cx,cy+23,13,ui::color::text2,ui::Align::center);
                }
                const std::string centre=format_bytes(after);
                const float centre_w=side?right-text_x:std::max(60.0f,dot_x-40-text_x);
                const float size=ui::fit_size(type.semibold,centre,30,18,centre_w);
                const float head_y=side?card_rect.y+58:cy+2;
                ui::text(*list,type.semibold,centre,text_x,head_y,size,fits?ui::color::text:ui::color::short_of);
                ui::text(*list,type.regular,installed?tr("Free now"):fits?tr("free after install"):tr("short"),text_x,head_y+28,17,ui::color::text2);
                for(int i=0;i<3;++i)
                {
                    const float y=side?card_rect.y+128+static_cast<float>(i)*38:cy-38+static_cast<float>(i)*38;
                    list->circle(dot_x,y,6,rows[i].dot);
                    ui::text(*list,type.regular,rows[i].label,label_x,y+7,20,ui::color::text2);
                    ui::text(*list,type.medium,format_bytes(rows[i].bytes),right,y+7,20,ui::color::text,ui::Align::right);
                    if(i<2)list->rounded_rect({dot_x-6,y+20,right-dot_x+6,1},0,ui::color::line);
                }
            }
            else
            {
                ui::text_fit(*list,type.regular,now.plan.planned?tr("After your queue"):tr("After queue + this download"),card_rect.x+28,card_rect.cy()+6,22,card_rect.w-200,ui::color::text2);
                ui::text(*list,type.medium,now.plan.after<0?tr("{size} short",{{"size",format_bytes(-now.plan.after)}}):format_bytes(now.plan.after),card_rect.x+card_rect.w-28,card_rect.cy()+6,24,ui::color::text,ui::Align::right);
            }
        }
        if (side) ry = top + kRowsHeight + 30.0f;
        else if (now.show_download && now.drive) ry += 140 + 20;
        // With an existing download, the button keeps the place it has when downloading.
        else if (!now.show_download && now.collection.job) ry = top + kRowsHeight + 30.0f;
        // A game already in Library: the second-copy button sits on the cover's bottom edge, the
        // sentence above it, and the main button is centred in the space left under the source row.
        const bool anchored = !in_library && !now.show_download && !download_again_ && !now.collection.job &&
            now.collection.kind == CollectionState::Kind::library && now.collection.library != nullptr;
        if (anchored) ry = (ry - 12 + top + 295.0f) * 0.5f - 40.0f;
        {
            // The download button with its explanation beside it.
            const std::string label=primary_label(release,download_again_,waiting_,now.collection,state,torbox_);
            const ui::Rect button{nx,ry,520,80};
            const float button_focus=control(Slot::primary,0,button,label,primary_enabled(release,now));
            if (list)
            {
                const float label_w=type.medium.measure(label,23);
                ui::icon_download(*list,button.cx()-label_w*0.5f-28,button.cy(),26,
                                  primary_enabled(release,now)?ui::state_ink(button_focus):ui::color::text.with_alpha(0.4f));
                ui::paragraph(*list,type.regular,one_per_line(tr("Saves a file to your console. Installation and launching are separate.")),
                              nx+520+32,ry+32,17,nw-552,24,ui::color::text3,3);
            }
            ry += 80 + 22;
        }
        if (anchored)
        {
            float py = top + 295;
            paragraph(tr("In your Library on {location}.",{{"location",i18n::place(now.collection.library->location)}}),nx,py,nw);
            control(Slot::again,0,{nx,top+342,450,58},tr("Download another copy"));
            ry = top + 400 + 22;
        }
        out.content=std::max(top+510.0f,ry-22);
        y=ry;
        const float width_notes=1180;
        if (!release.compression.empty()) {
            // The three sentences share one line, shrunk a little when a translation is long.
            const std::string compressed=tr("Compressed download. Orbit checks the files and available space before extraction. A password may be required.");
            paragraph(compressed,nx,y,nw,ui::fit_size(type.regular,compressed,20,14,nw)); y+=16;
        }
        if (!release.notes.empty()) { paragraph(release.notes,nx,y,width_notes,20); y+=16; }
        if (now.show_download && release.browser_available && !torbox_)
        {
            paragraph(tr("{provider}: start here",{{"provider",release.provider}}),nx,y,width_notes,25,ui::color::text);y+=16;
            paragraph(tr("1. Select Open {provider} on PS5. Your selected game and drive are kept.",{{"provider",release.provider}}),nx,y,width_notes,20);y+=12;
            paragraph(release.source_id=="akirabox"
                ? tr("Complete verification on AkiraBox, then wait for Orbit to check the download link.")
                : release.source_id=="datanodes"
                ? tr("Scroll to the bottom, select Continue to Download, then Free Download. Wait for the countdown and select Start Download.")
                : tr("2. On {provider}, complete any verification and select Download.",{{"provider",release.provider}}),nx,y,width_notes,20);y+=12;
            paragraph(tr("3. Return to the Orbit Store app. Orbit checks the file and adds it to Downloads."),nx,y,width_notes,20);y+=16;
            if (!state.have_browser || !state.browser.available)
            {
                paragraph(state.browser_error.empty() ? tr("Provider verification is available on the PS5 only.")
                                                     : std::string(tr(std::string_view(state.browser_error))),nx,y,width_notes,20);
                y+=12;
            }
        }
        if (now.show_download && (torbox_ || (state.debrid.connected && !state.debrid.supports(release))))
        { paragraph(delivery_note(release),nx,y,width_notes,21);y+=18; }
        if (now.collection.library && !anchored)
        {
            paragraph(now.collection.kind == CollectionState::Kind::related
                ? tr("A related copy is in Library. Its format, filename or version does not confirm a match for this option.")
                : tr("In your Library on {location}.",{{"location",i18n::place(now.collection.library->location)}}),nx,y,width_notes);
            y+=16;
            if (now.collection.kind == CollectionState::Kind::related)
            { control(Slot::related,0,{nx,y,450,58},tr("View related copy"));y+=80; }
        }
        if (!anchored && !in_library && !download_again_ && now.collection.kind == CollectionState::Kind::library)
        { control(Slot::again,0,{nx,y,450,58},tr("Download another copy"));y+=80; }
        if (now.collection.job && now.collection.job->status == "complete")
        { paragraph(one_per_line(tr("Download history records a completed transfer. Find it in Library to check its current location.")),nx,y,width_notes,20);y+=16; }
        if (now.show_download && (!now.drive || !now.plan.enough))
        {
            paragraph(!now.drive ? tr("Connect a writable drive to your PS5.") :
                tr("Not enough space after unfinished downloads. Free space or cancel an item in Downloads."),nx,y,width_notes);
            y+=16;
        }
        paragraph(error_,nx,y,width_notes);
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
        struct Fact { FactIcon icon; std::string value; std::string label; };
        std::vector<Fact> facts;
        facts.push_back({release.platform=="ps4"?FactIcon::platform4:FactIcon::platform,release.platform=="ps4"?"PS4":"PS5",tr("Platform")});
        if(!game->genre.empty())facts.push_back({FactIcon::genre,i18n::genre(game->genre),tr("Genre")});
        if(!game->release_date.empty())facts.push_back({FactIcon::calendar,format_date(game->release_date),tr("Release date")});
        if(!game->publisher.empty())facts.push_back({FactIcon::publisher,game->publisher,tr("Publisher")});
        // Without extraction the download is what lands on the disk; archives show both sizes.
        if(release.compression.empty())
            facts.push_back({FactIcon::disk,format_bytes(release.size_bytes),tr("Size on disk")});
        else
        {
            facts.push_back({FactIcon::download,format_bytes(release.size_bytes),tr("Download size")});
            if(release.extracted_bytes > 0)
                facts.push_back({FactIcon::disk,format_bytes(release.extracted_bytes),tr("Size on disk")});
        }
        if(!release.region.empty())facts.push_back({FactIcon::globe,release.region,tr("Region")});
        if(!release.version.empty())facts.push_back({FactIcon::version,release.version,tr("Version")});
        facts.push_back({FactIcon::format,format_label(release),tr("Format")});
        facts.push_back({FactIcon::source,release.provider,tr("Source")});
        if(!release.title_id.empty())facts.push_back({FactIcon::title_id,release.title_id,tr("Title ID")});
        // The cover on the left, the facts in two columns on the right at the cover's height.
        constexpr float kCover=400,kGap=10,kColumnGap=24,kMaxTile=76;
        const float grid_x=570;
        const float tile_w=(1806-grid_x-kColumnGap)*0.5f;
        const int rows=std::max(1,static_cast<int>((facts.size()+1)/2));
        const float tile_h=std::min(kMaxTile,(kCover-kGap*static_cast<float>(rows-1))/static_cast<float>(rows));
        // The game's title sits above the cover, like on the download options.
        constexpr float top=96;
        y=top;
        if(list)
        {
            const ui::Rect cover{116,y,kCover,kCover};
            list->shadow({cover.x,cover.y+10,cover.w,cover.h},ui::kArtRadius,28,ui::Color::rgb(0x000000,0.45f));
            draw_cover(ctx_,*list,*game,cover,ui::kArtRadius);
            ui::text_fit(*list,type.semibold,game->title,116,top-28,44,1690,ui::color::text);
            for(std::size_t i=0;i<facts.size();++i)
            {
                const float x=grid_x+static_cast<float>(i%2)*(tile_w+kColumnGap);
                const float ty=y+static_cast<float>(i/2)*(tile_h+kGap);
                const ui::Rect tile{x,ty,tile_w,tile_h};
                // The same tile as a row of the download options, without the arrow.
                ui::state_fill(*list,tile,20,0);
                list->circle(x+50,tile.cy(),24,ui::Color::rgb(0xffffff,0.14f));
                fact_icon(*list,type,facts[i].icon,x+50,tile.cy(),30,ui::color::text);
                ui::text_fit(*list,type.regular,facts[i].label,x+92,tile.cy()-8,17,tile_w-116,ui::state_ink_dim(0));
                ui::text_fit(*list,type.medium,facts[i].value,x+92,tile.cy()+22,24,tile_w-116,ui::state_ink(0));
            }
        }
        y+=kCover+24;
        paragraph(tr("Saves a file to your console. Installation and launching are separate."),116,y,1690,20,ui::color::text3);
        // Same height as the download options, so the cover sits at the same place in both.
        out.content=std::max(y,top+510.0f);
    }
    out.end=y+20;
    return out;
}

void GamePage::show_sheet(Sheet sheet, Slot preferred, int index)
{
    sheet_=sheet;
    if(sheet==Sheet::none)all_=false;
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
bool GamePage::choosing(const Game &game) const
{
    return is_pack(game) && game.releases.size()>1 && ctx_.store.state().paired;
}
void GamePage::back_sheet(bool /*chosen*/)
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
    case Slot::favourite:
        if(state.paired && !ctx_.store.favourite_action().busy)ctx_.store.set_favourite(game.id,!state.favourite(game.id));
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
        error_.clear();back_sheet(true);break;
    case Slot::again: download_again_=true;error_.clear();show_sheet(Sheet::downloads);break;
    case Slot::related:
        if(now.collection.library)
        {intent.kind=Intent::Kind::show_library;intent.id=now.collection.library->title_id;intent.detail=now.collection.library->source_key;}
        break;
    case Slot::install_all: all_=true;error_.clear();back_sheet(true);break;
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
    if(focused && input.is_pressed(Action::back))
    {
        if(sheet_==Sheet::none)intent.kind=Intent::Kind::close;
        else back_sheet();
        feedback.play(hui::audio::Cue::back);return intent;
    }
    auto where=compose(nullptr,sheet_,false);
    if(where.controls.empty())return intent;
    focus_=std::clamp(focus_,0,static_cast<int>(where.controls.size())-1);
    auto &scroll=sheet_==Sheet::none?page_scroll_:sheet_scroll_;
    const float extra=sheet_!=Sheet::none?kTallExtra:0.0f;
    const float visible=sheet_==Sheet::none?980:kBodyBottom+extra-kBodyTop;
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
        else if(sheet_==Sheet::about && (input.nav==Direction::up || input.nav==Direction::down)) {}
        else if(sheet_==Sheet::none && (input.nav==Direction::up || input.nav==Direction::down))
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
    if(sheet_==Sheet::about || sheet_==Sheet::downloads)scroll.target=0;
    scroll.update(dt,18);
    if(focused && input.is_pressed(Action::confirm))
    {
        const auto &c=where.controls[static_cast<std::size_t>(focus_)];
        if(!c.enabled)refuse(feedback,input);
        else{const Release chosen=target(*game,game->releases[static_cast<std::size_t>(option_)]);intent=activate(c,*game,chosen,offer(chosen),feedback);}
    }
    return intent;
}

// The two read-only sheets never scroll: their content is centred in the window at one fixed
// size, and shrunk further when a long provider note would not fit.
GamePage::Fit GamePage::fit(Sheet sheet) const
{
    if(sheet!=Sheet::about && sheet!=Sheet::downloads)return {1.0f,0.0f};
    // About this game is placed exactly like the download options of the same game.
    // The layout of a game that is not installed is the reference, so the cover and title
    // do not move when the sheet shows an existing download or library copy instead.
    sheet=Sheet::downloads;
    fresh_layout_=true;
    const auto where=compose(nullptr,sheet,false);
    fresh_layout_=false;
    // The block runs from the game's title (about y 36) to the last line; its middle sits at y 340.
    constexpr float kTop=36.0f,kMiddle=321.0f,kMaxHeight=582.0f;
    // Every game, app and update is laid out like one with a provider's "start here" steps
    // (the tallest usual content): the cover, title and rows keep one place and one size
    // whatever a source adds under them. Only a longer note shrinks the block further.
    constexpr float kStandardHeight=619.2f;
    const float bottom=std::max({where.content,where.end-20.0f,kTop+kStandardHeight});
    const float height=bottom-kTop;
    if(height<=0.0f)return {1.0f,0.0f};
    const float scale=std::min(1.0f,kMaxHeight/height);
    return {scale,kMiddle-scale*(kTop+bottom)*0.5f};
}
void GamePage::draw(Frame &frame,bool focused) const
{
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
    // The sheet is laid out for a panel at y 320; it is drawn centred on the screen.
    const float extra=sheet_!=Sheet::none?kTallExtra:0.0f;
    const float kSheetShift=(1080.0f-(710.0f+extra))*0.5f-320.0f;
    const ui::Rect panel{70,320+kSheetShift,1780,710+extra};
    frame.glass=true;
    ui::glass_modal(list,panel,frame.glass_texture,30);
    list.push_transform(1,0,0,0,kSheetShift);
    const Release release=target(*game,game->releases[static_cast<std::size_t>(std::clamp(option_,0,static_cast<int>(game->releases.size())-1))]);
    const auto now=offer(release);
    const char *title=sheet_==Sheet::downloads?tr("Download options"):sheet_==Sheet::source?tr("Source"):
        sheet_==Sheet::delivery?tr("Download using"):sheet_==Sheet::storage?tr("Save to"):about_title(*game);
    ui::text_fit(list,type.medium,title,116,386,34,1380,ui::color::text);
    list.push_clip({100,kBodyTop-8,1720,kBodyBottom+extra-kBodyTop+8});
    const auto placed=fit(sheet_);
    list.push_transform(placed.scale,960,0,0,kBodyTop-sheet_scroll_.value+placed.shift);
    const auto where=compose(&list,sheet_,focused);
    list.pop_transform();list.pop_clip();
    for(std::size_t i=0;i<where.controls.size();++i)
    {
        const auto &c=where.controls[i];if(!c.fixed)continue;
        const float focus=focused && static_cast<int>(i)==focus_?1:0;
        if(c.slot==Slot::primary)
        {
            const std::string label=primary_label(release,download_again_,waiting_,now.collection,state,torbox_);
            const float size=ui::fit_size(type.medium,label,25,18,c.rect.w-45);
            ui::button(list,type,c.rect,label,sheet_==Sheet::downloads?ui::ButtonKind::secondary:ui::ButtonKind::light,focus,c.enabled,size);
        }
    }
    list.pop_transform();
}
std::vector<hui::ui::Hint> GamePage::hints() const
{
    const auto where=compose(nullptr,sheet_,false);
    const bool reading=focus_>=0 && focus_<static_cast<int>(where.controls.size()) && where.controls[static_cast<std::size_t>(focus_)].slot==Slot::reading;
    if(reading)return {{hui::ui::Button::dpad,tr("Scroll")},{hui::ui::Button::circle,tr("Back")}};
    if(sheet_==Sheet::about)return {{hui::ui::Button::circle,tr("Back")}};
    if (sheet_ == Sheet::none)
        return {{hui::ui::Button::cross,tr("Select")},{hui::ui::Button::circle,tr("Back")},
                {hui::ui::Button::triangle,tr("Favourite")},{hui::ui::Button::square,tr("Details")}};
    return {{hui::ui::Button::cross,tr("Select")},{hui::ui::Button::circle,tr("Back")}};
}
} // namespace orbit
