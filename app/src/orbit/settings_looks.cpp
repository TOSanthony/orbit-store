#include "orbit/download_settings.hpp"
// Orbit Store TV app - App settings in Crossbar: each section is a folder
// with its value beside it; inside, each setting is a row, and its values
// open in a menu beside the list. Every row acts through Store's own
// activate(), so a setting behaves the same in either interface.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/i18n.hpp"
#include "orbit/look.hpp"
#include "orbit/screens.hpp"
#include "orbit/starter.hpp"
#include "orbit/thread.hpp"

#include <algorithm>
#include <cmath>

namespace orbit
{

namespace
{

constexpr const char *kTitles[] = {/* i18n */ "Download sources", /* i18n */ "Storage", /* i18n */ "Notifications",
                                   /* i18n */ "Pair a device",    /* i18n */ "Game catalogue", /* i18n */ "Updates",
                                   "Orbit Zero",                  /* i18n */ "Appearance",     /* i18n */ "More settings"};
constexpr icons::Icon kIcons[] = {icons::Icon::cloud,   icons::Icon::drive,  icons::Icon::bell,
                                  icons::Icon::link,    icons::Icon::refresh, icons::Icon::update,
                                  icons::Icon::laptop,  icons::Icon::palette, icons::Icon::more};
constexpr const char *kNotice =
    /* i18n */ "You are responsible for checking that your downloads are lawful. Third-party downloads are "
    "at your own risk. Only download content you have permission to access and use, following "
    "applicable law and the provider\xE2\x80\x99s terms. Public links do not prove permission, "
    "and Orbit does not guarantee the files\xE2\x80\x99 safety or authenticity.";
constexpr const char *kSavedElf = "/data/orbit-store/orbit_store.elf";

int wait_for(double unix_time)
{
    return std::max(0, static_cast<int>(std::ceil(unix_time - unix_seconds())));
}

bool browser_source(const std::string &id)
{
    return id == "vikingfile" || id == "akirabox" || id == "datanodes" || id == "1fichier";
}

} // namespace

std::vector<SettingsScreen::XbEntry> SettingsScreen::crossbar_entries() const
{
    std::vector<XbEntry> out;
    if (!xb_open_)
    {
        for (int s = 0; s < static_cast<int>(Section::count); ++s)
            out.push_back({'s', s});
        return out;
    }
    return section_entries();
}

// Rows come and go while a section is open: Update Orbit appears with a release and goes once
// it's installed. The focus stays on the same setting rather than at the same position.
void SettingsScreen::keep_row_focus(bool after)
{
    const bool cinema = look::is(look::Interface::cinema), panorama = look::is(look::Interface::panorama);
    int &row = cinema ? cn_row_ : panorama ? pn_row_ : xb_.index;
    const std::vector<XbEntry> entries = cinema || panorama ? section_entries() : crossbar_entries();
    const int count = static_cast<int>(entries.size());
    const auto same = [](XbEntry a, XbEntry b) { return a.kind == b.kind && a.index == b.index; };
    if (after)
    {
        focus_row_ = row >= 0 && row < count ? entries[row] : XbEntry{0, 0};
        return;
    }
    if (focus_row_.kind == 0 || (row >= 0 && row < count && same(entries[row], focus_row_)))
        return;
    for (int i = 0; i < count; ++i)
        if (same(entries[i], focus_row_))
        {
            row = i;
            if (!cinema && !panorama)
                xb_.snap();
            return;
        }
}

std::vector<SettingsScreen::XbEntry> SettingsScreen::section_entries() const
{
    // The current section's settings, one row each.
    const Snapshot &state = ctx_.store.state();
    std::vector<XbEntry> out;
    switch (section_)
    {
    case Section::sources:
        for (int i = 0; i < static_cast<int>(state.sources.options.size()); ++i)
            out.push_back({'o', i});
        // My NAS: the address of its index.json, typed with the on-screen keyboard.
        if (contains(draft_, "nas"))
            out.push_back({'M', 0});
        out.push_back({'a', 0});
        out.push_back({'v', 0});
        if (state.browser.viking_account_available) { out.push_back({'K', 0}); out.push_back({'J', 0}); }
        // Signing in to Archive.org, on a download service that has it.
        if (state.archive_account.known)
            out.push_back({'O', 0});
        break;
    case Section::storage:
        out.push_back({'d', 0});
        out.push_back({'l', 0});
        if (state.system.has_download_connections)
            out.push_back({'C', 0});
        for (int i = 0; i < 4; ++i) out.push_back({'R', i});
        out.push_back({'k', 0});
        if (state.system.has_delete_packages)
            out.push_back({'P', 0});
        break;
    case Section::notifications:
        // Where to notify, then what about (src/NotificationSetting.tsx).
        if (state.system.has_download_notifications)
            out.push_back({'n', 0});
        if (state.ntfy.known)
            out.push_back({'N', 0});
        if (state.system.has_notification_events)
            for (int i = 0; i < static_cast<int>(state.system.notification_events.size()); ++i)
                out.push_back({'e', i});
        break;
    case Section::pairing:
        out.push_back({'p', 0});
        break;
    case Section::catalogue:
        out.push_back({'c', 0});
        break;
    case Section::updates:
        if (update_orbit().show)
            out.push_back({'U', 0});
        out.push_back({'t', 0});
        out.push_back({'r', 0});
        out.push_back({'u', 0});
        break;
    case Section::zero:
        if (state.system.zero_configured)
        {
            out.push_back({'z', 0});
            out.push_back({'y', 0});
        }
        out.push_back({'h', 0});
        break;
    case Section::appearance:
        // Each interface's options in its design's order, as the browser lists them.
        out.push_back({'i', 0});
        if (look::is(look::Interface::crossbar))
            out.push_back({'b', 0});
        if (look::is(look::Interface::panorama))
            out.push_back({'g', 0});
        out.push_back({'m', 0});
        if (look::is(look::Interface::cinema) || look::is(look::Interface::panorama))
            out.push_back({'A', 0});
        out.push_back({'L', 0});
        out.push_back({'Y', 0});
        out.push_back({'S', 0});
        out.push_back({'Q', 0});
        break;
    default:
        if (ctx_.open_browser)
            out.push_back({'w', 0});
        out.push_back({'V', 0});
        out.push_back({'D', 0});
        out.push_back({'x', 0});
        out.push_back({'x', 1});
        break;
    }
    return out;
}

std::string SettingsScreen::crossbar_value(Section section) const
{
    // What each section is set to, as the browser's Settings list shows it.
    const Snapshot &state = ctx_.store.state();
    switch (section)
    {
    case Section::sources:
    {
        std::string out;
        for (const SourceOption &option : state.sources.options)
            if (contains(state.sources.enabled, option.id))
                out += (out.empty() ? "" : ", ") + option.label;
        return out;
    }
    case Section::storage:
    {
        std::string drive = tr("Choose automatically");
        for (const Drive &d : state.drives)
            if (d.id == state.system.preferred_storage)
                drive = i18n::place(d.label);
        return drive + " \xC2\xB7 " + trn(state.system.concurrent_downloads, "{count} at a time", "{count} at a time");
    }
    case Section::notifications:
    {
        std::string out = state.system.download_notifications ? "PS5" : "";
        if (state.ntfy.enabled)
            out += out.empty() ? "ntfy" : ", ntfy";
        return out.empty() ? std::string(tr("Off")) : out;
    }
    case Section::pairing:
    {
        std::string code = state.pair_code;
        if (code.size() == 6)
            code.insert(3, " ");
        return code;
    }
    case Section::catalogue:
        return state.have_catalogue_status ? tr("Catalogue {revision}", {{"revision", std::to_string(state.catalogue.revision)}})
                                           : std::string();
    case Section::updates:
        return update_available() ? std::string(tr("Update available")) : state.system.version;
    case Section::zero:
        return state.system.zero_enabled ? tr("On") : tr("Off");
    case Section::appearance:
        return tr(look::title(look::prefs().interface));
    default:
        return {};
    }
}

xb::List SettingsScreen::crossbar_list() const
{
    return rows_of(crossbar_entries());
}

xb::List SettingsScreen::rows_of(const std::vector<XbEntry> &entries) const
{
    xb::List list;
    list.size = static_cast<int>(entries.size());
    list.fill = [this, entries](int i, xb::Row &row)
    {
        const Snapshot &state = ctx_.store.state();
        const SettingResult &result = ctx_.store.setting_result();
        const XbEntry entry = entries[static_cast<std::size_t>(i)];
        row.has_icon = true;
        row.key = std::string(1, entry.kind) + std::to_string(entry.index);
        const auto error_for = [&](std::initializer_list<Setting> settings)
        {
            for (Setting s : settings)
                if (error_setting_ == s && !error_.empty())
                    return std::string(tr(std::string_view(error_)));
            return std::string();
        };
        switch (entry.kind)
        {
        case 's':
        {
            // A section, with what it's set to.
            const auto section = static_cast<Section>(entry.index);
            row.icon = kIcons[entry.index];
            row.title = tr(kTitles[entry.index]);
            row.value = crossbar_value(section);
            row.dot = section == Section::updates && update_available();
            row.folder = true;
            switch (section)
            {
            case Section::sources:
                row.text = tr("Select your download sources. You can change them anytime.");
                break;
            case Section::storage:
                row.text = tr("New downloads start with this drive selected under Save to. Choosing another "
                              "drive for a download also makes it the default.");
                break;
            case Section::notifications:
                row.text = tr("Orbit tells you about the events you choose on your PS5, even while you play, and on "
                              "your phone or computer through ntfy.");
                break;
            case Section::pairing:
                row.text = tr("Browse Orbit and queue downloads from another device on the same network as "
                              "this PS5. Downloads still run on this console.");
                break;
            case Section::catalogue:
                row.text = tr("New games arrive without reinstalling Orbit. Your PS5 checks when Orbit starts "
                              "and every six hours, and keeps a saved catalogue for browsing offline.");
                break;
            case Section::updates:
                row.text = tr("Orbit has two parts: this TV app, and the download service that keeps running "
                              "after you close it. Update both at once, or each one below.");
                break;
            case Section::zero:
                row.text = tr("Use your computer to download games and transfer them to this PS5 over Wi-Fi or Ethernet.");
                break;
            case Section::appearance:
                row.text = tr("Choose how Orbit looks and moves on this device. Your queue, favourites and "
                              "settings stay the same.");
                break;
            default:
                row.text = tr("Auto-start and payload managers are in the browser version of Orbit.");
                break;
            }
            break;
        }
        case 'o':
        {
            // A source: on or off until saved.
            const SourceOption &option = state.sources.options[static_cast<std::size_t>(entry.index)];
            row.icon = browser_source(option.id) ? icons::Icon::globe : icons::Icon::cloud;
            row.title = option.label;
            row.sub = trn(option.release_count, "{count} download option", "{count} download options");
            row.toggle = contains(draft_, option.id) ? 1 : 0;
            row.off = result.busy && result.setting == Setting::sources;
            if (option.id == "vikingfile")
                row.text = tr("Complete verification if shown, then wait. Orbit will check the generated download link automatically.");
            else if (option.id == "1fichier")
                row.text = tr("On {provider}, complete any verification and select the site’s {button} button.",
                              {{"provider", option.label}, {"button", tr("Download")}});
            else if (option.id == "akirabox")
                row.text = tr("Complete verification on AkiraBox, then wait for Orbit to check the download link.");
            else if (option.id == "datanodes")
                row.text = tr("Scroll to the bottom, select Continue to Download, then Free Download. Wait for the countdown and select Start Download.");
            if (state.sources.acknowledged)
                row.text += (row.text.empty() ? "" : " ") +
                            std::string(tr("Turning off a source hides its download options and pauses its unfinished "
                                           "downloads. Saved files stay on your drive. Turn the source back on and resume "
                                           "from Downloads when you\xE2\x80\x99re ready."));
            break;
        }
        case 'a':
            // The notice, and the promise that goes with it.
            row.icon = icons::Icon::info;
            row.title = tr("Before you download");
            row.sub = tr("I understand the risks and will only download content I\xE2\x80\x99m "
                         "legally entitled to access and use.");
            row.check = draft_acknowledged_ ? 1 : 0;
            row.text = tr(kNotice);
            row.off = result.busy && result.setting == Setting::sources;
            break;
        case 'v':
        {
            const bool saving = result.busy && result.setting == Setting::sources;
            const bool can_save = !saving && draft_acknowledged_ && (state.sources.acknowledged || !draft_.empty());
            row.icon = icons::Icon::check;
            row.primary = can_save;
            row.title = saving ? tr("Saving\xE2\x80\xA6") : tr("Save sources");
            row.sub = tr("Saved for this console and its paired devices.");
            row.off = !can_save;
            row.detail = error_for({Setting::sources});
            row.warn = !row.detail.empty();
            break;
        }
        case 'J':
            row.icon = icons::Icon::key;
            row.title = tr("Check sign-in");
            row.value = state.browser.viking_state == "signed-in" ? tr("Signed in") :
                state.browser.viking_state == "signed-out" ? tr("Not signed in") : tr("Sign-in could not be confirmed");
            row.off = ctx_.store.browser_action().busy || state.browser.active;
            break;
        case 'K':
            row.icon = icons::Icon::key;
            row.title = "Vikingfile";
            row.value = tr("Open Vikingfile account");
            row.sub = tr("Orbit uses your Vikingfile sign-in when requesting download links. If verification is needed, its page opens.");
            row.text = tr("Enter your Vikingfile account code (hash), complete verification if asked, then select Login. Return to Orbit when finished. Manage or sign out on the same account page.");
            row.off = ctx_.store.browser_action().busy || state.browser.active;
            if (state.browser.account_page) row.detail = ctx_.store.browser_action().error;
            row.warn = !row.detail.empty();
            break;
        case 'O':
        {
            const ArchiveAccount &account = state.archive_account;
            row.icon = icons::Icon::key;
            row.title = "Archive.org";
            row.value = account.signed_in ? tr("Signed in") : tr("Not signed in");
            if (account.signed_in && !account.name.empty())
                row.sub = account.name;
            row.text = tr("Some Archive.org games only download for signed-in accounts. Sign in with your "
                          "Archive.org account to download them.");
            row.off = result.busy && (result.setting == Setting::archive_sign_in ||
                                      result.setting == Setting::archive_sign_out);
            row.detail = error_for({Setting::archive_sign_out});
            row.warn = !row.detail.empty();
            break;
        }
        case 'd':
        {
            const std::string &preferred = state.system.preferred_storage;
            bool known = preferred.empty();
            row.icon = icons::Icon::drive;
            row.title = tr("Default drive");
            row.value = tr("Choose automatically");
            for (const Drive &drive : state.drives)
                if (drive.id == preferred)
                {
                    row.value = i18n::place(drive.label);
                    known = true;
                }
            row.text = tr("New downloads start with this drive selected under Save to. Choosing another "
                          "drive for a download also makes it the default.");
            if (!known)
            {
                row.warn = true;
                row.text += " " + std::string(tr("Your default drive isn\xE2\x80\x99t connected. Until it is, Save to "
                                                 "starts with the first drive available."));
            }
            if (state.drives.empty())
                row.text += " " + std::string(tr("No writable drives found. Connect a USB drive, then come back here."));
            row.detail = error_for({Setting::storage});
            break;
        }
        case 'R':
            row.icon = icons::Icon::layers;
            row.title = tr(download_settings[entry.index].label);
            row.value = std::to_string(state.system.download_recovery[entry.index]);
            row.text = tr(entry.index < 2 ? download_recovery_help : download_speed_help);
            row.detail = error_for({Setting::download_recovery});
            break;
        case 'C':
            row.icon = icons::Icon::layers;
            row.title = tr("Connections per download");
            row.value = std::to_string(state.system.download_connections);
            row.text = tr(connections_help);
            if (result.setting == Setting::download_connections && !result.error.empty())
                row.detail = tr(std::string_view(result.error));
            break;
        case 'l':
            row.icon = icons::Icon::layers;
            row.title = tr("Simultaneous downloads");
            row.value = std::to_string(state.system.concurrent_downloads);
            row.text = tr("Choose 1–8 games. One by default. Lowering the limit lets current downloads finish.");
            if (result.setting == Setting::download_limit && !result.error.empty())
                row.detail = tr(std::string_view(result.error));
            break;
        case 'k':
            row.icon = icons::Icon::archive;
            row.title = tr("Keep archives after extracting");
            row.toggle = state.system.delete_archives ? 0 : 1;
            row.sub = state.system.delete_archives
                          ? tr("ZIP, 7z and RAR downloads are extracted on the drive. The archive is deleted once its files are in place.")
                          : tr("ZIP, 7z and RAR downloads are extracted on the drive. The archive is kept next to its files.");
            row.off = result.busy && result.setting == Setting::archives;
            if (result.setting == Setting::archives && !result.error.empty())
                row.detail = tr(std::string_view(result.error));
            break;
        case 'N':
            // As Archive.org's row: the state, where it sends, and a form behind Cross.
            row.icon = icons::Icon::info;
            row.title = tr("ntfy notifications");
            row.value = state.ntfy.enabled ? tr("On") : tr("Off");
            if (state.ntfy.enabled)
                row.sub = ntfy_address(state.ntfy);
            row.text = tr("Get a push notification on your phone or computer for the events you choose. "
                          "Subscribe to the same topic in the ntfy app.");
            break;
        case 'P':
            row.icon = icons::Icon::info;
            row.title = tr("Delete packages after installing");
            row.toggle = state.system.delete_packages ? 1 : 0;
            row.sub = tr("Orbit can delete a PKG or FPKG once it has installed on your PS5, to free up space. If installation fails, the package is kept so you can try again.");
            row.off = result.busy && result.setting == Setting::packages;
            if (result.setting == Setting::packages && !result.error.empty())
                row.detail = tr(std::string_view(result.error));
            break;
        case 'n':
            row.icon = icons::Icon::bell;
            row.title = tr("On this PS5");
            row.toggle = state.system.download_notifications ? 1 : 0;
            row.sub = tr("Notifications appear on this PS5, even while you play.");
            row.off = result.busy && result.setting == Setting::notifications;
            if (result.setting == Setting::notifications && !result.error.empty())
                row.detail = tr(std::string_view(result.error));
            break;
        case 'e':
        {
            // One of the events, chosen for this PS5 and ntfy alike.
            const auto at = static_cast<std::size_t>(entry.index);
            row.icon = icons::Icon::check;
            row.title = tr(kNotificationEvents[at].label);
            row.toggle = state.system.notification_events[at] ? 1 : 0;
            // The list's one explanation goes on its first row; the updates row says what it costs.
            if (at == 0)
                row.sub = tr("Your choices apply to this PS5 and to ntfy.");
            if (at == 4 && state.system.notification_events[at])
                row.sub = tr("To tell you about updates, Orbit checks for a new release every six hours.");
            row.off = result.busy && result.setting == Setting::notification_event;
            if (result.setting == Setting::notification_event && !result.error.empty())
                row.detail = tr(std::string_view(result.error));
            break;
        }
        case 'p':
            row.icon = icons::Icon::link;
            row.title = tr("Pair a phone or computer");
            row.text = std::string(tr("Browse Orbit and queue downloads from another device on the same network as "
                                      "this PS5. Downloads still run on this console.")) +
                       " " + tr("The code changes when Orbit restarts. Devices you\xE2\x80\x99ve paired stay paired.");
            break;
        case 'c':
        {
            const CatalogueStatus &catalogue = state.catalogue;
            row.icon = icons::Icon::refresh;
            if (state.system.zero_enabled)
            {
                row.title = tr("Game catalogue");
                row.value = std::to_string(state.games.size());
                row.text = "Orbit Zero supplies the catalogue and artwork from your computer. Refresh the catalogue in the desktop app to get new games.";
                row.detail = state.system.zero_error;
                break;
            }
            const bool sending = result.busy && result.setting == Setting::catalogue;
            const int wait = wait_for(catalogue.check_after);
            row.title = sending || catalogue.busy ? tr("Refreshing catalogue\xE2\x80\xA6")
                        : wait > 0                ? tr("Refresh in {seconds} s", {{"seconds", std::to_string(wait)}})
                                                  : tr("Refresh catalogue");
            const bool can = catalogue.available && !catalogue.busy && !sending && wait == 0;
            row.primary = can;
            row.off = !can;
            if (state.have_catalogue_status)
                row.sub = trn(catalogue.game_count, "{count} game", "{count} games") + " \xC2\xB7 " +
                          tr("Catalogue {revision}", {{"revision", std::to_string(catalogue.revision)}}) + " \xC2\xB7 " +
                          (catalogue.checked_at > 0.0 ? tr("Checked {time}", {{"time", format_age(catalogue.checked_at, unix_seconds())}})
                                                      : std::string(tr("Not checked yet")));
            row.text = tr("New games arrive without reinstalling Orbit. Your PS5 checks when Orbit starts "
                          "and every six hours, and keeps a saved catalogue for browsing offline.");
            if (state.have_catalogue_status && !catalogue.available)
                row.text += " " + std::string(tr("Refreshing works with Orbit running on your PS5. This copy uses its bundled "
                                                 "catalogue."));
            row.detail = error_setting_ == Setting::catalogue && !error_.empty() ? std::string(tr(std::string_view(error_)))
                         : !catalogue.error.empty()                              ? std::string(tr(std::string_view(catalogue.error)))
                                                                                 : std::string();
            break;
        }
        case 'U':
        {
            // Both parts in one press.
            const UpdateOrbit one = update_orbit();
            row.icon = icons::Icon::update;
            row.title = tr("Update Orbit");
            row.dot = one.offer;
            row.sub = one.status.empty() ? one.summary : one.status;
            row.text = tr("The TV app and the download service update together. Downloads pause while Orbit restarts, then continue.");
            row.off = !one.offer || !one.enabled;
            row.detail = error_for({Setting::update_all});
            row.warn = !row.detail.empty();
            break;
        }
        case 't':
        {
            // This app: its version, what's new, and what to do about it.
            const TvAppStatus &tv = state.tv_app;
            const std::string running = app_release_version(ctx_.app_version);
            const bool newer = !tv.latest_version.empty() && !running.empty() &&
                               starter::compare_versions(tv.latest_version, running) > 0;
            const bool waiting = tv.installed == "orbit" && !tv.installed_version.empty() && !running.empty() &&
                                 starter::compare_versions(tv.installed_version, running) > 0;
            row.icon = icons::Icon::tv;
            row.title = tr("TV app");
            row.value = running.empty() ? std::string(tr("Unknown")) : running;
            row.dot = newer && !waiting;
            row.text = tr("Orbit Store for TV, the app you\xE2\x80\x99re using now.");
            if (!state.have_tv_app || !tv.available)
                row.sub = state.have_tv_app            ? tr("Updates work with Orbit running on your PS5.")
                          : state.have_service_update ? tr("Checking for TV app updates needs Orbit 0.6.0 or later. Update the "
                                                           "download service below.")
                                                      : tr("Loading\xE2\x80\xA6");
            else if (tv.phase == "downloading")
                row.sub = tr("Downloading the TV app: {received} of {size}",
                             {{"received", format_bytes(tv.received)}, {"size", format_bytes(tv.size)}});
            else if (tv.phase == "verifying")
                row.sub = tr("Checking the download\xE2\x80\xA6");
            else if (tv.phase == "installing")
                row.sub = tr("Installing\xE2\x80\xA6");
            else if (tv.phase == "checked" && !newer && !waiting)
                row.sub = tr("You\xE2\x80\x99re using the latest version.");
            else if (!tv.latest_version.empty() && !check_fresh(tv.checked_at, unix_seconds()) && !tv.busy && newer)
                row.sub = tr("Check for updates again before installing.");
            if (tv.phase == "downloading" && tv.size > 0.0)
                row.bar = static_cast<float>(std::clamp(tv.received / tv.size, 0.0, 1.0));
            if (waiting || (tv.phase == "installed" && newer))
                row.text += " " + tr("Close Orbit Store (press the PS button, then Close Application) and "
                                     "open it again from your Games row. If version {running} still opens, "
                                     "restart your PS5.",
                                     {{"running", running}});
            if (tv.installed == "none" && newer)
                row.text += " " + tr("Orbit updates the TV app in /data/homebrew, and this copy is "
                                     "somewhere else. Download release {version} from Orbit\xE2\x80\x99s "
                                     "GitHub releases and replace your copy yourself.",
                                     {{"version", tv.latest_version}});
            if (tv.installed == "folder")
                row.text += " " + std::string(tr("This app is installed as the folder /data/homebrew/PPSA99177. Keep "
                                                 "updating that copy yourself, or remove the folder so Orbit can "
                                                 "install and update it."));
            if (tv.installed == "blocked")
                row.text += " " + std::string(tr("Something other than the TV app is at "
                                                 "/data/homebrew/PPSA99177.ffpkg. Move it away to install updates "
                                                 "here."));
            row.detail = error_for({Setting::tv_check, Setting::tv_install});
            if (row.detail.empty() && !tv.error.empty())
                row.detail = tr(std::string_view(tv.error));
            row.warn = !row.detail.empty();
            break;
        }
        case 'r':
        {
            // The download service, and a restart that needs you.
            const ServiceUpdate &service = state.service;
            const std::string &running = !service.running_version.empty() ? service.running_version : state.system.version;
            row.icon = icons::Icon::box;
            row.title = tr("Download service");
            row.value = running.empty() ? std::string(tr("Unknown")) : running;
            row.dot = state.have_service_update && service.update_available;
            row.text = tr("Orbit, which downloads your games and keeps running after you close this "
                          "app.");
            const Restart restart = state.restart;
            const int wait = wait_for(service.retry_at);
            if (restart == Restart::manual || restart == Restart::failed)
            {
                row.warn = true;
                row.sub = restart == Restart::manual ? tr("Start Orbit from your payload manager") : tr("Orbit didn\xE2\x80\x99t restart");
                row.text = restart == Restart::manual
                               ? tr("This app can\xE2\x80\x99t start Orbit here. Run {path} from your payload "
                                    "manager. This screen reconnects by itself.",
                                    {{"path", kSavedElf}})
                               : tr("Run {path} from your payload manager, or try again.", {{"path", kSavedElf}});
                if (!state.restart_detail.empty())
                    row.text += " (" + state.restart_detail + ")";
            }
            else if (!state.have_service_update || !service.available)
                row.sub = tr("Updates work with Orbit running on your PS5.");
            // During Update Orbit, its own row shows the progress.
            else if (service.phase == "downloading" && !service.update_all)
                row.sub = tr("Downloading Orbit: {size}", {{"size", format_bytes(service.received)}});
            else if (service.phase == "saving" && !service.update_all)
                row.sub = tr("Saving the verified release\xE2\x80\xA6");
            else if (wait > 0 && !service.busy)
                row.sub = tr("Please wait {seconds} s before another request.", {{"seconds", std::to_string(wait)}});
            else if (service.phase == "checked" && service.latest_version == running)
                row.sub = tr("You\xE2\x80\x99re running the latest version. You can reinstall it if "
                             "needed.");
            else if (!service.latest_version.empty() && !check_fresh(service.checked_at, unix_seconds()) && !service.busy &&
                     !(service.saved_version == service.latest_version && service.restart_required))
                row.sub = tr("Check for updates again before installing.");
            if (service.restart_required)
                row.text += " " + (service.saved_version.empty()
                                       ? std::string(tr("Orbit\xE2\x80\x99s update is saved. It runs the next "
                                                        "time Orbit starts. Restart the download service to "
                                                        "use it now."))
                                       : tr("Orbit {version} is saved. It runs the next time Orbit starts. "
                                            "Restart the download service to use it now.",
                                            {{"version", service.saved_version}}));
            row.detail = error_for({Setting::service_check, Setting::service_install, Setting::service_restart});
            if (row.detail.empty() && !service.error.empty())
                row.detail = tr(std::string_view(service.error));
            row.warn = row.warn || !row.detail.empty();
            break;
        }
        case 'u':
        {
            // Both parts at once: each part's own check, where it's allowed.
            const double checked = std::max(state.tv_app.checked_at, state.service.checked_at);
            row.icon = icons::Icon::refresh;
            row.title = tr("Check for updates");
            if (checked > 0.0)
                row.sub = tr("Checked {time}", {{"time", format_age(checked, unix_seconds())}});
            row.off = !crossbar_can_check();
            break;
        }
        case 'z':
            row.icon = icons::Icon::laptop;
            row.title = state.system.zero_enabled ? tr("Turn off Orbit Zero") : tr("Enable Orbit Zero");
            row.toggle = state.system.zero_enabled ? 1 : 0;
            row.off = result.busy;
            row.text = tr("Use your computer to download games and transfer them to this PS5 over Wi-Fi or Ethernet.");
            if (state.system.zero_enabled)
                row.text += std::string(" ") +
                            (state.system.zero_preference == "prefer-ps5"
                                 ? tr("Your preference uses the PS5 when the selected source is reachable, otherwise your computer. Running downloads keep their route.")
                                 : tr("Downloads run on your computer and transfer to your selected PS5 drive. Keep Orbit Zero open."));
            row.detail = tr(std::string_view(result.setting == Setting::zero && !result.error.empty() ? result.error
                                                                                                       : state.system.zero_error));
            row.warn = !row.detail.empty();
            break;
        case 'y':
            row.icon = icons::Icon::link;
            row.title = tr("Computer");
            row.value = state.system.zero_address;
            break;
        case 'h':
            row.icon = icons::Icon::info;
            row.title = "Orbit Zero";
            row.sub = state.system.zero_configured
                          ? tr("Open Orbit Zero on your computer, connect to this console and enter the PS5 pairing code. Then enable it here.")
                          : tr("No computer paired. Use Console setup in the desktop app to connect.");
            break;
        case 'i':
            row.icon = icons::Icon::palette;
            row.title = tr("Interface");
            row.value = tr(look::title(look::prefs().interface));
            row.text = tr("Choose how Orbit looks and moves on this device. Your queue, favourites and "
                          "settings stay the same.");
            break;
        case 'b':
            row.icon = icons::Icon::spark;
            row.title = tr("Ribbons");
            row.value = tr(look::ribbons_title(look::prefs().ribbons));
            break;
        case 'm':
            row.icon = icons::Icon::eye;
            row.title = tr("Motion");
            row.value = look::prefs().reduced_motion ? tr("Reduced") : tr("Full");
            if (look::is(look::Interface::cinema))
                row.text = tr("Reduced keeps the art still");
            else if (look::is(look::Interface::panorama))
                row.text = tr("Reduced uses fades instead of slides");
            break;
        case 'g':
            row.icon = icons::Icon::spark;
            row.title = tr("Accent colour");
            row.value = tr(look::accent_title(look::prefs().accent));
            break;
        case 'A':
        {
            const bool cinema = look::is(look::Interface::cinema);
            row.icon = icons::Icon::layers;
            row.title = cinema ? tr("Art behind menus") : tr("Art behind pages");
            row.value = !look::prefs().art_behind ? tr("Plain black") : cinema ? tr("Blurred game art") : tr("Faint game art");
            if (cinema)
                row.text = tr("The last game you looked at, blurred");
            break;
        }
        case 'L':
            row.icon = icons::Icon::globe;
            row.title = tr("Language");
            row.value = language_options()[static_cast<std::size_t>(language_index())];
            row.text = tr("Choose the language Orbit uses on this device.");
            break;
        case 'M':
        {
            row.icon = icons::Icon::link;
            row.title = tr("My NAS index address");
            row.value = !nas_url_.empty()          ? nas_url_
                        : state.sources.nas_configured ? std::string(tr("Saved address kept. Select to change it."))
                                                       : std::string("https://nas.example.com/orbit-secret/index.json");
            if (state.sources.nas_configured)
                row.sub = !state.sources.nas_error.empty()
                              ? state.sources.nas_host + ": " + nas_message(state.sources.nas_error)
                              : tr("{host}: {count} games found.",
                                   {{"host", state.sources.nas_host}, {"count", std::to_string(state.sources.nas_count)}});
            else
                row.sub = tr("Enter the https address of the index.json on your NAS.");
            row.off = result.busy && result.setting == Setting::sources;
            break;
        }
        case 'Y':
            row.icon = icons::Icon::list;
            row.title = tr("Keyboard");
            row.value = ctx_.keyboard_choice == "qwerty" ? "QWERTY" : ctx_.keyboard_choice == "azerty" ? "AZERTY" : std::string(tr("Automatic"));
            row.text = tr("The on-screen keyboard\xE2\x80\x99s letters. Automatic uses AZERTY when the app is in French.");
            break;
        case 'S':
            row.icon = icons::Icon::play;
            row.title = tr("Sounds");
            row.toggle = ctx_.sounds_on ? 1 : 0;
            row.text = ctx_.sounds_on ? tr("Menu sounds are on: you hear a sound when you move and select.")
                                      : tr("Menu sounds are off. The app is silent.");
            break;
        case 'Q':
            row.icon = icons::Icon::up;
            row.title = tr("Volume");
            row.value = std::to_string(ctx_.sound_volume) + " %";
            break;
        case 'V':
            row.icon = icons::Icon::info;
            row.title = tr("View diagnostics");
            row.text = tr("Check Orbit\xE2\x80\x99s status and recent download events. Reports exclude pairing codes, "
                          "access tokens, download links, game names and file paths. Nothing is sent automatically.");
            break;
        case 'D':
            row.icon = icons::Icon::bug;
            row.title = tr("Save diagnostic report");
            row.text = tr("Save a report of Orbit\xE2\x80\x99s recent activity to a drive, then attach it to your bug "
                          "report on GitHub. It has no game titles, download links, passwords or tokens.");
            row.sub = state.diagnostics_path.empty() ? std::string()
                                                     : tr("Saved to {path}", {{"path", state.diagnostics_path}});
            row.off = result.busy && result.setting == Setting::diagnostics;
            if (result.setting == Setting::diagnostics && !result.error.empty())
                row.detail = tr(std::string_view(result.error));
            row.warn = !row.detail.empty();
            break;
        case 'w':
            row.icon = icons::Icon::globe;
            row.primary = true;
            row.title = tr("Open browser version");
            row.text = std::string(tr("Auto-start and payload managers are in the browser version of Orbit.")) +
                       " " +
                       tr("On this PS5, the browser version is the Orbit Store icon in your Media area. On a "
                          "phone or computer, pair it under Pair a device.");
            break;
        default:
        {
            // The versions running now.
            const std::string running = app_release_version(ctx_.app_version);
            row.icon = entry.index == 0 ? icons::Icon::tv : icons::Icon::box;
            row.title = entry.index == 0 ? tr("TV app") : tr("Download service");
            const std::string &version = entry.index == 0 ? running : state.system.version;
            row.value = version.empty() ? std::string(tr("Unknown")) : version;
            break;
        }
        }
        if (!row.detail.empty())
            row.warn = true;
    };
    return list;
}

bool SettingsScreen::crossbar_can_check() const
{
    const Snapshot &state = ctx_.store.state();
    const SettingResult &result = ctx_.store.setting_result();
    const TvAppStatus &tv = state.tv_app;
    const ServiceUpdate &service = state.service;
    const bool tv_can = state.have_tv_app && tv.available && !tv.busy && !result.busy &&
                        std::max(wait_for(tv.retry_at), wait_for(tv.check_after)) == 0;
    const bool service_can = state.have_service_update && service.available && !service.busy && !result.busy &&
                             std::max(wait_for(service.retry_at), wait_for(service.check_after)) == 0;
    return tv_can || service_can;
}

std::vector<xb::MenuItem> SettingsScreen::crossbar_menu(xb::MenuFoot *foot) const
{
    // A setting's values, or a part's update actions.
    const Snapshot &state = ctx_.store.state();
    const SettingResult &result = ctx_.store.setting_result();
    const look::Prefs &prefs = look::prefs();
    std::vector<xb::MenuItem> items;
    xb::MenuFoot unused;
    xb::MenuFoot &note = foot != nullptr ? *foot : unused;
    switch (xb_menu_kind_)
    {
    case 0:
        items.push_back(xb::choice(tr("Choose automatically"), state.system.preferred_storage.empty(),
                                   tr("The first USB or external drive, otherwise internal storage"), !result.busy));
        for (const Drive &drive : state.drives)
            items.push_back(xb::choice(i18n::place(drive.label), drive.id == state.system.preferred_storage,
                                       tr("{size} free", {{"size", format_bytes(drive.free_bytes)}}) + " \xC2\xB7 " +
                                           (drive.pending_bytes > 0.0
                                                ? tr("{size} after queued downloads",
                                                     {{"size", format_bytes(std::max(0.0, drive.projected_free_bytes))}})
                                                : tr("of {size}", {{"size", format_bytes(drive.total_bytes)}})),
                                       !result.busy));
        note.note = tr("Downloads save inside the drive\xE2\x80\x99s homebrew folder.");
        break;
    case 1:
        for (int n = 1; n <= 8; ++n)
            items.push_back(xb::choice(trn(n, "{count} game", "{count} games"), n == state.system.concurrent_downloads, {},
                                       !result.busy));
        note.note = tr("Choose 1–8 games. One by default. Lowering the limit lets current downloads finish.");
        break;
    case 2:
    {
        const TvAppStatus &tv = state.tv_app;
        const std::string running = app_release_version(ctx_.app_version);
        const bool newer = !tv.latest_version.empty() && !running.empty() &&
                           starter::compare_versions(tv.latest_version, running) > 0;
        const bool waiting = tv.installed == "orbit" && !tv.installed_version.empty() && !running.empty() &&
                             starter::compare_versions(tv.installed_version, running) > 0;
        const int wait = wait_for(tv.retry_at);
        const int check_wait = std::max(wait, wait_for(tv.check_after));
        const bool disabled = (result.busy && (result.setting == Setting::tv_check || result.setting == Setting::tv_install)) || tv.busy;
        if (newer && !waiting && (tv.installed == "orbit" || tv.installed == "manual"))
        {
            const bool installing = tv.busy && tv.phase != "checking";
            xb::MenuItem install = xb::action(icons::Icon::update,
                                              installing                 ? tr("Installing\xE2\x80\xA6")
                                              : tv.installed == "orbit" ? tr("Update TV app")
                                                                        : tr("Replace with this release"),
                                              !disabled && wait == 0 && check_fresh(tv.checked_at, unix_seconds()));
            install.value = tv.latest_version + " \xC2\xB7 " + format_bytes(tv.size);
            items.push_back(install);
        }
        items.push_back(xb::action(icons::Icon::refresh,
                                   tv.phase == "checking" ? tr("Checking\xE2\x80\xA6")
                                   : check_wait > 0       ? tr("Check again in {seconds} s", {{"seconds", std::to_string(check_wait)}})
                                                          : tr("Check for updates"),
                                   !disabled && check_wait == 0));
        note.figures.push_back({tr("This app"), running.empty() ? std::string(tr("Unknown")) : running});
        if (waiting)
            note.figures.push_back({tr("Installed, opens next time"), tv.installed_version});
        if (!tv.latest_version.empty())
            note.figures.push_back({tr("Latest release"), tv.latest_version + " \xC2\xB7 " + format_bytes(tv.size)});
        break;
    }
    case 3:
    {
        const ServiceUpdate &service = state.service;
        const std::string &running = !service.running_version.empty() ? service.running_version : state.system.version;
        const int wait = wait_for(service.retry_at);
        const int check_wait = std::max(wait, wait_for(service.check_after));
        const bool disabled = (result.busy && (result.setting == Setting::service_check || result.setting == Setting::service_install ||
                                               result.setting == Setting::service_restart)) ||
                              service.busy;
        const bool saved_latest = !service.latest_version.empty() && service.saved_version == service.latest_version &&
                                  service.restart_required;
        if (state.restart == Restart::manual || state.restart == Restart::failed)
        {
            if (ctx_.store.can_start())
                items.push_back(xb::action(icons::Icon::power, tr("Try again")));
        }
        else
        {
            if (!service.latest_version.empty() && !saved_latest &&
                starter::compare_versions(service.latest_version, running) >= 0)
            {
                const bool installing = service.phase == "downloading" || service.phase == "saving";
                xb::MenuItem install = xb::action(icons::Icon::update,
                                                  installing                                  ? tr("Installing\xE2\x80\xA6")
                                                  : service.latest_version == running ? tr("Reinstall release")
                                                                                              : tr("Install update"),
                                                  !disabled && wait == 0 && check_fresh(service.checked_at, unix_seconds()));
                install.value = service.latest_version;
                items.push_back(install);
            }
            items.push_back(xb::action(icons::Icon::power, tr("Restart download service"), !disabled));
            items.push_back(xb::action(icons::Icon::refresh,
                                       service.phase == "checking" ? tr("Checking\xE2\x80\xA6")
                                       : check_wait > 0            ? tr("Check again in {seconds} s", {{"seconds", std::to_string(check_wait)}})
                                                                   : tr("Check for updates"),
                                       !disabled && check_wait == 0));
        }
        note.figures.push_back({tr("Running"), running.empty() ? std::string(tr("Unknown")) : running});
        if (!service.saved_version.empty() && service.saved_version != running)
            note.figures.push_back({tr("Starts next time"), service.saved_version});
        if (!service.latest_version.empty())
            note.figures.push_back({tr("Latest release"), service.latest_version});
        break;
    }
    case 4:
        for (int i = 0; i < static_cast<int>(look::Interface::count); ++i)
        {
            const auto interface = static_cast<look::Interface>(i);
            items.push_back(xb::choice(tr(look::title(interface)), interface == prefs.interface));
        }
        break;
    case 5:
        for (int i = 0; i < static_cast<int>(look::Ribbons::count); ++i)
            items.push_back(xb::choice(tr(look::ribbons_title(static_cast<look::Ribbons>(i))), i == static_cast<int>(prefs.ribbons)));
        break;
    case 7:
        for (int i = 0; i < static_cast<int>(look::Accent::count); ++i)
            items.push_back(xb::choice(tr(look::accent_title(static_cast<look::Accent>(i))), i == static_cast<int>(prefs.accent)));
        note.note = tr("Problems always stay red, whatever the accent.");
        break;
    case 8:
    {
        const bool cinema = look::is(look::Interface::cinema);
        items.push_back(xb::choice(cinema ? tr("Blurred game art") : tr("Faint game art"), prefs.art_behind));
        items.push_back(xb::choice(tr("Plain black"), !prefs.art_behind));
        break;
    }
    case 15:
    {
        const std::vector<std::string> options = language_options();
        const int chosen = language_index();
        for (int i = 0; i < static_cast<int>(options.size()); ++i)
            items.push_back(xb::choice(options[static_cast<std::size_t>(i)], i == chosen));
        break;
    }
    case 16:
    {
        const int chosen = ctx_.keyboard_choice == "qwerty" ? 1 : ctx_.keyboard_choice == "azerty" ? 2 : 0;
        items.push_back(xb::choice(tr("Automatic"), chosen == 0));
        items.push_back(xb::choice("QWERTY", chosen == 1));
        items.push_back(xb::choice("AZERTY", chosen == 2));
        note.note = tr("The on-screen keyboard\xE2\x80\x99s letters. Automatic uses AZERTY when the app is in French.");
        break;
    }
    case 17:
        for (int value = 0; value <= 100; value += 10)
            items.push_back(xb::choice(std::to_string(value), value == (ctx_.sound_volume + 5) / 10 * 10));
        break;
    case 14:
        for (int value : connection_choices(state.system.download_connections))
            items.push_back(xb::choice(std::to_string(value), value == state.system.download_connections, {}, !result.busy));
        note.note = tr(connections_help);
        break;
    case 10: case 11: case 12: case 13:
    {
        const int index = xb_menu_kind_ - 10;
        for (int value : download_choices(index, state.system.download_recovery[index]))
            items.push_back(xb::choice(std::to_string(value), value == state.system.download_recovery[index], {}, !result.busy));
        note.note = tr(index < 2 ? download_recovery_help : download_speed_help);
        break;
    }
    case 9:
    {
        // Signing out asks first, as the Store look does.
        xb::MenuItem out = xb::action(icons::Icon::close, tr("Sign out"));
        out.danger = true;
        items.push_back(out);
        items.push_back(xb::action(icons::Icon::check, tr("Stay signed in")));
        note.note = tr("Sign out of Archive.org? Games that need an account won't download until you sign in again.");
        break;
    }
    default:
        items.push_back(xb::choice(tr("Full"), !prefs.reduced_motion));
        items.push_back(xb::choice(tr("Reduced"), prefs.reduced_motion));
        break;
    }
    return items;
}

std::string SettingsScreen::crossbar_menu_title() const
{
    static const char *const kMenus[] = {/* i18n */ "Default drive", /* i18n */ "Simultaneous downloads", /* i18n */ "TV app",
                                         /* i18n */ "Download service", /* i18n */ "Interface", /* i18n */ "Ribbons",
                                         /* i18n */ "Motion", /* i18n */ "Accent colour"};
    if (xb_menu_kind_ >= 10 && xb_menu_kind_ <= 13) return tr(download_settings[xb_menu_kind_ - 10].label);
    if (xb_menu_kind_ == 14)
        return tr("Connections per download");
    if (xb_menu_kind_ == 15)
        return tr("Language");
    if (xb_menu_kind_ == 16)
        return tr("Keyboard");
    if (xb_menu_kind_ == 17)
        return tr("Volume");
    if (xb_menu_kind_ == 8)
        return look::is(look::Interface::cinema) ? tr("Art behind menus") : tr("Art behind pages");
    if (xb_menu_kind_ == 9)
        return "Archive.org";
    return tr(kMenus[std::clamp(xb_menu_kind_, 0, 7)]);
}

xb::Band SettingsScreen::crossbar_band() const
{
    // Restarting, and the two confirmations Updates asks for.
    const Snapshot &state = ctx_.store.state();
    xb::Band band;
    if (state.restart == Restart::stopping || state.restart == Restart::starting)
    {
        band.busy = true;
        band.title = state.restart == Restart::stopping ? tr("Stopping the download service\xE2\x80\xA6")
                                                        : tr("Starting the download service\xE2\x80\xA6");
        band.body = tr("Downloads are paused. This takes a few seconds.");
    }
    else if (confirm_restart_)
    {
        band.title = tr("Restart the download service?");
        band.body = ctx_.store.state().service.self_restart
                        ? tr("Active downloads pause and continue once Orbit is back.")
                        : tr("Active downloads pause. This app stops Orbit and starts it again, then "
                             "you can resume your downloads from Downloads.");
        band.buttons = {tr("Restart now"), tr("Not now")};
    }
    else if (confirm_replace_)
    {
        band.title = tr("Replace your copy?");
        band.body = tr("The TV app in /data/homebrew is a copy you placed yourself. Orbit "
                       "replaces it with release {version}. This app keeps running until you "
                       "close it.",
                       {{"version", state.tv_app.latest_version}});
        band.buttons = {tr("Replace"), tr("Keep my copy")};
    }
    band.focus = xb_band_;
    return band;
}

bool SettingsScreen::crossbar_band_open() const
{
    const Restart restart = ctx_.store.state().restart;
    return restart == Restart::stopping || restart == Restart::starting || confirm_restart_ || confirm_replace_;
}

void SettingsScreen::act(Kind kind, int index, Intent &intent, Feedback &feedback)
{
    // Store's activate(), as a control of its own kind.
    Control control{kind, index, 0, {}, true};
    activate(control, intent, feedback);
}

void SettingsScreen::band_input(const InputFrame &input, Intent &intent, Feedback &feedback)
{
    // Restarting has no buttons; the two confirmations have two each.
    const int count = static_cast<int>(crossbar_band().buttons.size());
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const int to = xb_band_ + (input.nav == Direction::right ? 1 : -1);
        if (to >= 0 && to < count)
        {
            xb_band_ = to;
            feedback.play(hui::audio::Cue::focus);
        }
        else
            refuse(feedback, input);
    }
    else if (count > 0 && (input.is_pressed(Action::back) || (input.is_pressed(Action::confirm) && xb_band_ == 1)))
    {
        act(confirm_restart_ ? Kind::restart_cancel : Kind::tv_keep, 0, intent, feedback);
        feedback.play(hui::audio::Cue::modal_close);
    }
    else if (count > 0 && input.is_pressed(Action::confirm))
        act(confirm_restart_ ? Kind::restart_confirm : Kind::tv_replace, confirm_restart_ ? 0 : 1, intent, feedback);
    else if (input.is_pressed(Action::back) || input.is_pressed(Action::confirm))
        refuse(feedback, input);
}

void SettingsScreen::menu_choose(int chosen, Intent &intent, Feedback &feedback)
{
    // A value from a setting's menu (or sheet).
    const Snapshot &state = ctx_.store.state();
    switch (xb_menu_kind_)
    {
    case 0:
        act(Kind::drive, chosen, intent, feedback);
        break;
    case 1:
        if (chosen + 1 != state.system.concurrent_downloads && !ctx_.store.setting_result().busy)
            ctx_.store.set_download_limit(chosen + 1);
        break;
    case 2:
    {
        // The install (when offered) comes first, then the check.
        const std::vector<xb::MenuItem> items = crossbar_menu();
        if (chosen == static_cast<int>(items.size()) - 1)
            act(Kind::tv_check, 0, intent, feedback);
        else if (state.tv_app.installed == "orbit")
            act(Kind::tv_install, 0, intent, feedback);
        else
        {
            act(Kind::tv_replace, 0, intent, feedback);
            xb_band_ = 1; // Keep my copy, as Store focuses it
        }
        break;
    }
    case 3:
    {
        const std::vector<xb::MenuItem> items = crossbar_menu();
        const std::string &label = items[static_cast<std::size_t>(chosen)].label;
        if (label == tr("Try again"))
            act(Kind::restart_retry, 0, intent, feedback);
        else if (label == tr("Restart download service"))
        {
            act(Kind::service_restart, 0, intent, feedback);
            xb_band_ = 1; // Not now, as Store focuses it
        }
        else if (chosen == static_cast<int>(items.size()) - 1)
            act(Kind::service_check, 0, intent, feedback);
        else
            act(Kind::service_install, 0, intent, feedback);
        break;
    }
    case 4:
        act(Kind::interface_card, chosen, intent, feedback);
        break;
    case 5:
        act(Kind::ribbons, chosen, intent, feedback);
        break;
    case 7:
        act(Kind::accent, chosen, intent, feedback);
        break;
    case 8:
        act(Kind::art_behind, chosen, intent, feedback);
        break;
    case 15:
        act(Kind::language, chosen, intent, feedback);
        break;
    case 16:
        act(Kind::keyboard, chosen, intent, feedback);
        break;
    case 17:
        if (chosen >= 0 && chosen <= 10 && chosen * 10 != ctx_.sound_volume)
        {
            ctx_.sound_volume = chosen * 10;
            ctx_.store.set_sound_volume(ctx_.sound_volume);
            feedback.play(hui::audio::Cue::slider); // the new level, heard at once
        }
        break;
    case 14:
    {
        const auto choices = connection_choices(state.system.download_connections);
        if (!ctx_.store.setting_result().busy && chosen >= 0 && chosen < static_cast<int>(choices.size()) &&
            choices[static_cast<std::size_t>(chosen)] != state.system.download_connections)
            ctx_.store.set_download_connections(choices[static_cast<std::size_t>(chosen)]);
        break;
    }
    case 10: case 11: case 12: case 13:
    {
        int index = xb_menu_kind_ - 10;
        const auto choices = download_choices(index, state.system.download_recovery[index]);
        if (!ctx_.store.setting_result().busy && chosen >= 0 && chosen < static_cast<int>(choices.size()))
            ctx_.store.set_download_recovery(index, choices[chosen]);
        break;
    }
    case 9:
        if (chosen == 0)
            act(Kind::archive_sign_out_confirm, 0, intent, feedback);
        break;
    default:
        act(Kind::motion, chosen, intent, feedback);
        break;
    }
}

void SettingsScreen::press(const std::string &key, bool off, const InputFrame &input, Intent &intent, Feedback &feedback)
{
    // Cross on a setting's row: toggle it, act, or open its values.
    const Snapshot &state = ctx_.store.state();
    const char kind = key.empty() ? ' ' : key[0];
    const int index = key.size() > 1 ? std::atoi(key.c_str() + 1) : 0;
    const auto menu = [&](int which)
    {
        xb_menu_kind_ = which;
        int at = 0;
        const std::vector<xb::MenuItem> items = crossbar_menu();
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
            if (items[static_cast<std::size_t>(i)].checked)
                at = i;
        xb_menu_.show(at);
        feedback.play(hui::audio::Cue::modal_open);
    };
    if (off)
    {
        refuse(feedback, input);
        return;
    }
    switch (kind)
    {
    case 'o':
        act(Kind::source, index, intent, feedback);
        break;
    case 'a':
        act(Kind::acknowledge, 0, intent, feedback);
        break;
    case 'v':
        act(Kind::save_sources, 0, intent, feedback);
        break;
    case 'd':
        menu(0);
        break;
    case 'R':
        menu(10 + index);
        break;
    case 'l':
        menu(1);
        break;
    case 'C':
        menu(14);
        break;
    case 'k':
        act(Kind::archives_toggle, 0, intent, feedback);
        break;
    case 'n':
        act(Kind::notifications_toggle, 0, intent, feedback);
        break;
    case 'e':
        act(Kind::notification_event, index, intent, feedback);
        break;
    case 'P':
        act(Kind::packages_toggle, 0, intent, feedback);
        break;
    case 'N':
        act(Kind::ntfy_open, 0, intent, feedback);
        break;
    case 'U':
        act(Kind::update_all, 0, intent, feedback);
        break;
    case 'c':
        if (state.system.zero_enabled)
            refuse(feedback, input);
        else
            act(Kind::refresh_catalogue, 0, intent, feedback);
        break;
    case 't':
        if (state.have_tv_app && state.tv_app.available)
            menu(2);
        else
            refuse(feedback, input);
        break;
    case 'r':
        if ((state.have_service_update && state.service.available) || state.restart == Restart::manual ||
            state.restart == Restart::failed)
            menu(3);
        else
            refuse(feedback, input);
        break;
    case 'u':
    {
        // Each part checks for itself, where it can.
        const SettingResult &result = ctx_.store.setting_result();
        if (state.have_tv_app && state.tv_app.available && !state.tv_app.busy && !result.busy &&
            std::max(wait_for(state.tv_app.retry_at), wait_for(state.tv_app.check_after)) == 0)
            act(Kind::tv_check, 0, intent, feedback);
        if (state.have_service_update && state.service.available && !state.service.busy &&
            std::max(wait_for(state.service.retry_at), wait_for(state.service.check_after)) == 0)
            act(Kind::service_check, 0, intent, feedback);
        break;
    }
    case 'z':
        act(Kind::zero_toggle, 0, intent, feedback);
        break;
    case 'i':
        menu(4);
        break;
    case 'b':
        menu(5);
        break;
    case 'm':
        menu(6);
        break;
    case 'g':
        menu(7);
        break;
    case 'A':
        menu(8);
        break;
    case 'L':
        menu(15);
        break;
    case 'Y':
        menu(16);
        break;
    case 'Q':
        menu(17);
        break;
    case 'S':
        act(Kind::sounds_toggle, 0, intent, feedback);
        break;
    case 'M':
        act(Kind::nas_url, 0, intent, feedback);
        break;
    case 'w':
        act(Kind::open_browser, 0, intent, feedback);
        break;
    case 'V':
        act(Kind::view_diagnostics, 0, intent, feedback);
        break;
    case 'D':
        act(Kind::save_diagnostics, 0, intent, feedback);
        break;
    case 'J':
        act(Kind::viking_check, 0, intent, feedback);
        break;
    case 'K':
        act(Kind::viking_account, 0, intent, feedback);
        break;
    case 'O':
        if (!state.archive_account.signed_in)
            act(Kind::archive_sign_in, 0, intent, feedback);
        else
        {
            // On to the question, focused on Stay signed in.
            menu(9);
            xb_menu_.show(1);
        }
        break;
    default:
        refuse(feedback, input);
        break;
    }
}

void SettingsScreen::open_section(Section section)
{
    section_ = section;
    confirm_restart_ = false;
    confirm_replace_ = false;
    error_.clear();
    error_setting_ = Setting::none;
    load_draft();
    if (section_ == Section::pairing)
        ctx_.store.refresh_session();
}

Intent SettingsScreen::update_crossbar(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    Intent intent;
    const xb::List list = crossbar_list();
    xb_.clamp(list);
    xb::Row row;
    if (list.size > 0)
        list.fill(xb_.index, row);
    if (focused && crossbar_band_open())
        band_input(input, intent, feedback);
    else if (focused && xb_menu_.open)
    {
        const int chosen = xb_menu_.update(input, crossbar_menu(), feedback);
        if (chosen >= 0)
            menu_choose(chosen, intent, feedback);
    }
    else if (focused)
    {
        if (input.nav == Direction::up || input.nav == Direction::down)
            xb_.move(list, input, feedback);
        else if (input.is_pressed(Action::back))
        {
            if (xb_open_)
            {
                // Out of a section, back to its row.
                xb_open_ = false;
                open_section(section_);
                xb_.index = static_cast<int>(section_);
                xb_.snap();
                feedback.play(hui::audio::Cue::back);
            }
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::confirm) && list.size > 0)
        {
            if (!row.key.empty() && row.key[0] == 's')
            {
                open_section(static_cast<Section>(std::atoi(row.key.c_str() + 1)));
                xb_open_ = true;
                xb_.index = 0;
                xb_.snap();
                feedback.play(hui::audio::Cue::open);
            }
            else
                press(row.key, row.off, input, intent, feedback);
        }
    }
    xb_band_in_.target = crossbar_band_open() ? 1.0f : 0.0f;
    xb_band_in_.update(dt, 16.0f);
    xb_.animate(dt);
    xb_menu_.animate(dt);
    return intent;
}

void SettingsScreen::draw_crossbar(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const Snapshot &state = ctx_.store.state();
    const ui::Type &type = ctx_.type;
    std::vector<xb::BarItem> bar = xb::sections(ctx_, update_available());
    int current = section::settings;
    if (xb_open_)
    {
        xb::BarItem here;
        here.icon = kIcons[static_cast<int>(section_)];
        here.label = tr(kTitles[static_cast<int>(section_)]);
        bar = {bar[section::settings], here};
        current = 1;
    }
    xb::draw_bar(ctx_, list, bar, current);
    const xb::List rows = crossbar_list();
    xb_.draw(ctx_, list, rows);
    if (xb_open_ && section_ == Section::pairing)
    {
        // The address and the code, large, while this is open.
        const float x = 1080.0f, w = 740.0f;
        const ui::Rect panel{x, 430.0f, w, 380.0f};
        list.shadow({panel.x, panel.y + 30.0f, panel.w, panel.h}, 28.0f, 60.0f, ui::Color::rgb(0x000000, 0.35f));
        list.bordered_rect(panel, 28.0f, ui::Color::rgb(0x0a0c2c, 0.62f), 1.0f, ui::Color::rgb(0xffffff, 0.14f));
        const auto step = [&](float y, int number, std::string_view label)
        {
            list.rounded_rect({x + 32.0f, y, 34.0f, 34.0f}, 17.0f, ui::Color::rgb(0xffffff, 0.12f));
            ui::text(list, type.semibold, std::to_string(number), x + 49.0f, y + 17.0f + 6.5f, 19.0f, ui::Color::rgb(0xffffff),
                     ui::Align::center);
            ui::text(list, type.regular, label, x + 80.0f, y + 17.0f + 6.6f, 20.0f, ui::Color::rgb(0xffffff, 0.75f));
        };
        step(panel.y + 32.0f, 1, tr("Open Orbit in a web browser"));
        float y = panel.y + 32.0f + 34.0f + 12.0f;
        if (!state.address.empty())
        {
            const std::string address = "http://" + state.address + ":34177";
            ui::text_fit(list, type.medium, address, x + 80.0f, y + 44.0f, 40.0f, w - 112.0f, ui::Color::rgb(0xffffff));
            y += 64.0f + 30.0f;
        }
        else
        {
            const std::string address = tr("Your PS5\xE2\x80\x99s IP address, port {port}", {{"port", "34177"}});
            ui::text_fit(list, type.medium, address, x + 80.0f, y + 34.0f, 30.0f, w - 112.0f, ui::Color::rgb(0xffffff));
            y = ui::paragraph(list, type.regular,
                              tr("Find it on the PS5 in Settings \xE2\x86\x92 Network \xE2\x86\x92 Connection "
                                 "Status."),
                              x + 80.0f, y + 74.0f, 19.0f, w - 112.0f, 27.0f, ui::Color::rgb(0xffffff, 0.62f), 2) +
                10.0f;
        }
        step(y, 2, tr("Enter this code"));
        std::string code = state.pair_code;
        if (code.size() == 6)
            code.insert(3, " ");
        ui::text(list, ui::face(type.sora_semibold, type.semibold, code), code.empty() ? "\xE2\x80\x94" : code, x + 76.0f,
                 y + 34.0f + 6.0f + 82.0f, 96.0f, ui::Color::rgb(0xffffff));
    }
    if (xb_menu_.in.value > 0.01f && xb_menu_kind_ == 4 && xb_menu_.active != static_cast<int>(look::prefs().interface))
    {
        // Focusing another interface previews it behind the menu.
        hui::gfx::DrawList &out = frame.overlay;
        out.push_opacity(xb_menu_.in.value);
        draw_interface_thumb(ctx_, out, xb_menu_.active, {0.0f, 0.0f, 1920.0f, 1080.0f});
        out.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, ui::Color::rgb(0x000000, 0.35f));
        out.pop_opacity();
    }
    if (xb_menu_.in.value > 0.01f)
    {
        xb::MenuFoot foot;
        const std::vector<xb::MenuItem> items = crossbar_menu(&foot);
        xb::MenuAt at;
        if (xb_menu_kind_ == 1 || xb_menu_kind_ >= 10)
            at.y = 180.0f;
        if (xb_menu_kind_ >= 10)
        {
            at.row_height = 60.0f;
            at.bottom = 990.0f;
        }
        xb::draw_menu(ctx_, frame.overlay, crossbar_menu_title(), items, xb_menu_, at, foot);
    }
    xb::draw_band(ctx_, frame.overlay, crossbar_band(), xb_band_in_.value);
}

std::vector<hui::ui::Hint> SettingsScreen::crossbar_hints() const
{
    if (crossbar_band_open())
    {
        if (crossbar_band().buttons.empty())
            return {};
        return {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Back")}};
    }
    if (xb_menu_.open)
        return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Back")}};
    if (!xb_open_)
        return {{hui::ui::Button::cross, tr("Open")}, {hui::ui::Button::l1, tr("Categories"), hui::ui::Button::r1}};
    const xb::List rows = crossbar_list();
    xb::Row row;
    if (rows.size > 0)
        rows.fill(std::clamp(xb_.index, 0, rows.size - 1), row);
    // What Cross does on this row, as the design names it.
    std::vector<hui::ui::Hint> hints;
    const char kind = row.key.empty() ? ' ' : row.key[0];
    if (row.toggle >= 0)
        hints.push_back({hui::ui::Button::cross, row.toggle > 0 ? tr("Turn off") : tr("Turn on")});
    else if (kind == 'd' || kind == 'l' || kind == 'C' || kind == 'i' || kind == 'b' || kind == 'm' || kind == 'L' || kind == 'Y' || kind == 'Q' || kind == 'M')
        hints.push_back({hui::ui::Button::cross, tr("Change")});
    else if (kind == 't' || kind == 'r' || kind == 'w' || kind == 'N' || kind == 'V')
        hints.push_back({hui::ui::Button::cross, tr("Open")});
    else if (kind == 'O' && !row.off)
        hints.push_back({hui::ui::Button::cross, ctx_.store.state().archive_account.signed_in ? tr("Sign out") : tr("Sign in")});
    else if ((kind == 'c' || kind == 'u') && !row.off)
        hints.push_back({hui::ui::Button::cross, tr("Refresh")});
    else if (kind == 'U' && !row.off)
        hints.push_back({hui::ui::Button::cross, tr("Update Orbit")});
    else if (kind == 'D' && !row.off)
        hints.push_back({hui::ui::Button::cross, tr("Save")});
    else if (kind == 'a' || (kind == 'v' && !row.off))
        hints.push_back({hui::ui::Button::cross, tr("Select")});
    hints.push_back({hui::ui::Button::circle, tr("Back")});
    return hints;
}

// ---------------------------------------------------------------- Cinema

namespace
{
// A Crossbar row as a Cinema row: the line under the title, or its text.
cn::Row cinema_row(const xb::Row &row)
{
    cn::Row out;
    out.icon = row.icon;
    out.has_icon = row.has_icon;
    out.title = row.title;
    out.sub = !row.sub.empty() ? row.sub : row.text;
    if (!row.detail.empty())
        out.sub += (out.sub.empty() ? "" : " ") + row.detail;
    out.value = row.value;
    out.toggle = row.toggle;
    out.check = row.check;
    out.dot = row.dot;
    out.warn = row.warn;
    out.off = row.off;
    return out;
}
} // namespace

Intent SettingsScreen::update_cinema(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    Intent intent;
    const xb::List rows = rows_of(section_entries());
    cn_row_ = std::clamp(cn_row_, 0, std::max(0, rows.size - 1));
    if (focused && crossbar_band_open())
        band_input(input, intent, feedback);
    else if (focused && xb_menu_.open)
    {
        // A setting's values as cards across the sheet.
        const std::vector<xb::MenuItem> items = crossbar_menu();
        const int columns = std::min(4, static_cast<int>(items.size()));
        int to = xb_menu_.active;
        if (input.nav == Direction::left)
            to -= 1;
        else if (input.nav == Direction::right)
            to += 1;
        else if (input.nav == Direction::up)
            to -= columns;
        else if (input.nav == Direction::down)
            to += columns;
        if (input.nav != Direction::none)
        {
            if (to >= 0 && to < static_cast<int>(items.size()))
            {
                xb_menu_.active = to;
                feedback.play(hui::audio::Cue::focus);
            }
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::back))
        {
            xb_menu_.open = false;
            feedback.play(hui::audio::Cue::modal_close);
        }
        else if (input.is_pressed(Action::confirm))
        {
            if (!items[static_cast<std::size_t>(xb_menu_.active)].enabled)
                refuse(feedback, input);
            else
            {
                xb_menu_.open = false;
                menu_choose(xb_menu_.active, intent, feedback);
            }
        }
    }
    else if (focused && !cn_rows_)
    {
        // The table of contents: up and down change the section.
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int to = static_cast<int>(section_) + (input.nav == Direction::down ? 1 : -1);
            if (to >= 0 && to < static_cast<int>(Section::count))
            {
                open_section(static_cast<Section>(to));
                cn_row_ = 0;
                feedback.play(hui::audio::Cue::focus);
            }
            else if (input.nav == Direction::up)
                intent.kind = Intent::Kind::top_bar;
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::confirm) || input.nav == Direction::right)
        {
            if (section_ == Section::pairing || rows.size == 0)
                refuse(feedback, input);
            else
            {
                cn_rows_ = true;
                cn_row_ = 0;
                feedback.play(hui::audio::Cue::focus);
            }
        }
    }
    else if (focused)
    {
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int to = cn_row_ + (input.nav == Direction::down ? 1 : -1);
            if (to >= 0 && to < rows.size)
            {
                cn_row_ = to;
                feedback.play(hui::audio::Cue::focus);
            }
            else if (input.nav == Direction::up)
                intent.kind = Intent::Kind::top_bar;
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::back) || input.nav == Direction::left)
        {
            cn_rows_ = false;
            feedback.play(hui::audio::Cue::back);
        }
        else if (input.is_pressed(Action::confirm) && rows.size > 0)
        {
            xb::Row row;
            rows.fill(cn_row_, row);
            press(row.key, row.off, input, intent, feedback);
        }
    }
    xb_band_in_.target = crossbar_band_open() ? 1.0f : 0.0f;
    xb_band_in_.update(dt, 16.0f);
    xb_menu_.animate(dt);
    return intent;
}

void SettingsScreen::draw_cinema(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const Snapshot &state = ctx_.store.state();
    const ui::Type &type = ctx_.type;
    cn::backdrop(ctx_, list, nullptr, cn::Veil::dark);
    // The table of contents: the current section large, its neighbours brighter.
    const int current = static_cast<int>(section_);
    float y = 190.0f;
    for (int i = 0; i < static_cast<int>(Section::count); ++i)
    {
        const std::string title = tr(kTitles[i]);
        const auto &title_font = ui::face(type.cinema, type.semibold, title);
        if (i == current)
        {
            y += 10.0f;
            list.rounded_rect({100.0f, y + 27.0f, 34.0f, 2.0f}, 0.0f, cn::kGold);
            ui::text_fit(list, title_font, title, 152.0f, y + 52.0f * 0.85f, 52.0f, 548.0f, cn::kInk);
            if (!cn_rows_ && !xb_menu_.open)
                list.rounded_rect({152.0f, y + 58.0f, title_font.measure(title, 52.0f), 2.0f}, 0.0f, cn::kInk.with_alpha(0.0f));
            y += 54.6f + 14.0f;
        }
        else
        {
            const float alpha = std::abs(i - current) == 1 ? 0.62f : 0.4f;
            const float w = ui::text(list, title_font, title, 152.0f, y + 52.5f * 0.5f + 30.0f * 0.35f, 30.0f, ui::Color::rgb(0xf7f3ec, alpha));
            if (static_cast<Section>(i) == Section::updates && update_available())
                list.circle(152.0f + w + 12.0f + 5.0f, y + 52.5f * 0.5f - 6.0f, 5.0f, cn::kGold);
            y += 52.5f;
        }
    }
    // The section's panel.
    const ui::Rect panel{760.0f, 190.0f, 1060.0f, 800.0f};
    cn::glass(list, panel, 30.0f);
    list.push_clip(panel);
    const float x = panel.x + 44.0f, w = panel.w - 88.0f;
    float top = panel.y + 40.0f;
    cn::kick(ctx_, list, x, top, tr(kTitles[current]));
    top += 25.6f;
    if (section_ == Section::pairing)
    {
        // The address and the code, large.
        top = ui::paragraph(list, type.regular,
                            tr("Browse Orbit and queue downloads from another device on the same network as "
                               "this PS5. Downloads still run on this console."),
                            x, top + 12.0f + 16.8f + 7.4f, 21.0f, w, 32.6f, cn::kDim, 3) + 10.0f;
        cn::kick(ctx_, list, x, top + 40.0f, "1 \xC2\xB7 " + std::string(tr("Open Orbit in a web browser")), 15.0f);
        top += 40.0f + 24.0f;
        if (!state.address.empty())
        {
            ui::text_fit(list, type.medium, "http://" + state.address + ":34177", x, top + 12.0f + 46.0f, 46.0f, w, cn::kInk);
            top += 12.0f + 74.0f;
        }
        else
        {
            const std::string address = tr("Your PS5\xE2\x80\x99s IP address, port {port}", {{"port", "34177"}});
            ui::text_fit(list, ui::face(type.cinema, type.semibold, address), address, x, top + 10.0f + 44.0f, 44.0f, w, cn::kInk);
            top = ui::paragraph(list, type.regular, tr("Find it on the PS5 in Settings \xE2\x86\x92 Network \xE2\x86\x92 Connection Status."), x,
                                top + 10.0f + 70.0f + 8.0f + 20.0f, 20.0f, w, 30.0f, cn::kDim, 2);
        }
        cn::kick(ctx_, list, x, top + 40.0f, "2 \xC2\xB7 " + std::string(tr("Enter this code")), 15.0f);
        std::string code = state.pair_code;
        if (code.size() == 6)
            code.insert(3, " ");
        ui::text(list, ui::face(type.cinema, type.semibold, code), code.empty() ? "\xE2\x80\x94" : code, x, top + 40.0f + 24.0f + 150.0f * 0.9f,
                 150.0f, cn::kInk);
        ui::text_fit(list, type.regular, tr("The code changes when Orbit restarts. Devices you\xE2\x80\x99ve paired stay paired."), x,
                     top + 40.0f + 24.0f + 165.0f + 6.0f + 18.0f, 18.0f, w, cn::kFaint);
        list.pop_clip();
    }
    else
    {
        // The section's intro, then its rows; a long section follows the focus.
        xb::Row intro;
        rows_of({{'s', current}}).fill(0, intro);
        if (!intro.text.empty())
            top = ui::paragraph(list, type.regular, intro.text, x, top + 12.0f + 16.8f + 7.4f, 21.0f, std::min(940.0f, w), 32.6f, cn::kDim, 3) -
                  16.8f - 7.4f + 32.6f - 12.0f + 12.0f;
        const xb::List rows = rows_of(section_entries());
        std::vector<cn::Row> shown;
        for (int i = 0; i < rows.size; ++i)
        {
            xb::Row row;
            rows.fill(i, row);
            cn::Row out = cinema_row(row);
            // Save sources is a pill once it can save, as the design has it.
            if (row.key[0] == 'v' && !row.off)
                out.button = tr("Save sources");
            shown.push_back(std::move(out));
        }
        const int focus = cn_rows_ ? cn_row_ : -1;
        float shift = 0.0f;
        if (focus >= 0)
        {
            // Measure the rows above the focus and the focused one.
            const std::vector<cn::Row> before(shown.begin(), shown.begin() + focus + 1);
            const float bottom = cn::draw_rows(ctx_, nullptr, x, top + 16.0f, w, before, -1);
            shift = std::max(0.0f, bottom - (panel.y + panel.h - 30.0f));
        }
        cn::draw_rows(ctx_, &list, x, top + 16.0f - shift, w, shown, focus);
        list.pop_clip();
    }
    // A setting's values: a sheet of cards; the interface's, with that interface behind.
    if (xb_menu_.in.value > 0.01f)
    {
        hui::gfx::DrawList &over = frame.overlay;
        over.push_opacity(xb_menu_.in.value);
        if (xb_menu_kind_ == 4 && xb_menu_.active != static_cast<int>(look::prefs().interface))
            draw_interface_thumb(ctx_, over, xb_menu_.active, {0.0f, 0.0f, 1920.0f, 1080.0f});
        xb::MenuFoot foot;
        const std::vector<xb::MenuItem> items = crossbar_menu(&foot);
        std::string kicker = crossbar_menu_title();
        for (const auto &[label, value] : foot.figures)
            kicker += " \xC2\xB7 " + label + " " + value;
        std::vector<cn::Card> cards;
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
        {
            const xb::MenuItem &item = items[static_cast<std::size_t>(i)];
            cn::Card card;
            card.title = item.label;
            card.sub = !item.sub.empty() ? item.sub : item.value;
            card.chosen = item.checked;
            card.focus = i == xb_menu_.active;
            card.enabled = item.enabled;
            card.big = xb_menu_kind_ == 1 || (xb_menu_kind_ >= 10 && xb_menu_kind_ <= 14); // numbers
            cards.push_back(std::move(card));
        }
        const int columns = std::min(4, static_cast<int>(cards.size()));
        const float h = cn::draw_cards(ctx_, nullptr, 100.0f, 0.0f, 1720.0f, cards, columns) + 56.0f * 2.0f + 25.6f + 18.0f +
                        (foot.note.empty() ? 0.0f : 20.0f + 28.8f);
        const float sheet_top = cn::draw_sheet(over, std::max(360.0f, h)) + 56.0f;
        cn::kick(ctx_, over, 100.0f, sheet_top, kicker);
        const float end = cn::draw_cards(ctx_, &over, 100.0f, sheet_top + 25.6f + 18.0f, 1720.0f, cards, columns);
        if (!foot.note.empty())
            ui::text_fit(over, type.regular, foot.note, 100.0f, end + 20.0f + 18.0f, 18.0f, 1720.0f, cn::kFaint);
        over.pop_opacity();
    }
    // Restarting and the confirmations, as title cards.
    if (xb_band_in_.value > 0.01f)
    {
        const xb::Band band = crossbar_band();
        cn::Title question;
        question.title = band.title;
        question.body = band.body;
        question.spin = band.busy;
        for (int i = 0; i < static_cast<int>(band.buttons.size()); ++i)
        {
            cn::Button b;
            b.label = band.buttons[static_cast<std::size_t>(i)];
            b.primary = i == 0;
            b.focus = i == xb_band_;
            question.buttons.push_back(std::move(b));
        }
        cn::draw_title(ctx_, frame.overlay, question, xb_band_in_.value);
    }
}

std::vector<hui::ui::Hint> SettingsScreen::cinema_hints() const
{
    if (crossbar_band_open())
        return crossbar_band().buttons.empty()
                   ? std::vector<hui::ui::Hint>{}
                   : std::vector<hui::ui::Hint>{{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Back")}};
    if (xb_menu_.open)
        return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Back")}};
    if (!cn_rows_)
        return {{hui::ui::Button::cross, tr("Open")}, {hui::ui::Button::l1, tr("Categories"), hui::ui::Button::r1}};
    const xb::List rows = rows_of(section_entries());
    xb::Row row;
    if (rows.size > 0)
        rows.fill(std::clamp(cn_row_, 0, rows.size - 1), row);
    std::vector<hui::ui::Hint> hints;
    const char kind = row.key.empty() ? ' ' : row.key[0];
    if (row.toggle >= 0)
        hints.push_back({hui::ui::Button::cross, row.toggle > 0 ? tr("Turn off") : tr("Turn on")});
    else if (kind == 'd' || kind == 'l' || kind == 'C' || kind == 'i' || kind == 'b' || kind == 'm' || kind == 'L' || kind == 'Y' || kind == 'Q' || kind == 'M')
        hints.push_back({hui::ui::Button::cross, tr("Change")});
    else if (kind == 't' || kind == 'r' || kind == 'w' || kind == 'N' || kind == 'V')
        hints.push_back({hui::ui::Button::cross, tr("Open")});
    else if (kind == 'O' && !row.off)
        hints.push_back({hui::ui::Button::cross, ctx_.store.state().archive_account.signed_in ? tr("Sign out") : tr("Sign in")});
    else if ((kind == 'c' || kind == 'u') && !row.off)
        hints.push_back({hui::ui::Button::cross, tr("Refresh")});
    else if (kind == 'U' && !row.off)
        hints.push_back({hui::ui::Button::cross, tr("Update Orbit")});
    else if (kind == 'D' && !row.off)
        hints.push_back({hui::ui::Button::cross, tr("Save")});
    else if (kind == 'a' || (kind == 'v' && !row.off))
        hints.push_back({hui::ui::Button::cross, tr("Select")});
    hints.push_back({hui::ui::Button::circle, tr("Back")});
    return hints;
}

// ---------------------------------------------------------------- Panorama

std::vector<pano::Item> SettingsScreen::panorama_items() const
{
    // The section's rows as a Metro list: the value under the name in the
    // accent, the line under it, and a switch, box or button on the right.
    const xb::List rows = rows_of(section_entries());
    std::vector<pano::Item> items;
    for (int i = 0; i < rows.size; ++i)
    {
        xb::Row row;
        rows.fill(i, row);
        pano::Item item;
        item.icon = row.icon;
        item.has_icon = row.has_icon;
        item.title = row.title;
        item.value = row.value;
        item.sub = !row.sub.empty() ? row.sub : row.text;
        if (!row.detail.empty())
            item.sub += (item.sub.empty() ? "" : " ") + row.detail;
        item.toggle = row.toggle;
        item.check = row.check;
        item.dot = row.dot;
        item.warn = row.warn;
        item.off = row.off;
        // The rows that act at once carry their button, as the design has them.
        const char kind = row.key.empty() ? ' ' : row.key[0];
        if (kind == 'v' && !row.off)
            item.button = tr("Save sources");
        else if (kind == 'c' && !row.off)
            item.button = tr("Refresh");
        else if (kind == 'u' && !row.off)
            item.button = tr("Check");
        else if (kind == 'w')
            item.button = tr("Open");
        items.push_back(std::move(item));
    }
    return items;
}

bool SettingsScreen::page(int step)
{
    // Panorama: L1 and R1 go through the pivots, then on to the next section.
    if (!look::is(look::Interface::panorama) || modal())
        return false;
    const int next = static_cast<int>(section_) + step;
    if (next < 0 || next >= static_cast<int>(Section::count))
        return false;
    open_section(static_cast<Section>(next));
    pn_row_ = 0;
    if (section_ == Section::pairing || rows_of(section_entries()).size == 0)
        pn_rows_ = false;
    return true;
}

Intent SettingsScreen::update_panorama(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    Intent intent;
    const xb::List rows = rows_of(section_entries());
    pn_row_ = std::clamp(pn_row_, 0, std::max(0, rows.size - 1));
    if (focused && crossbar_band_open())
        band_input(input, intent, feedback);
    else if (focused && xb_menu_.open)
    {
        // A setting's values: a full-screen list, up and down.
        const std::vector<xb::MenuItem> items = crossbar_menu();
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int to = xb_menu_.active + (input.nav == Direction::down ? 1 : -1);
            if (to >= 0 && to < static_cast<int>(items.size()))
            {
                xb_menu_.active = to;
                feedback.play(hui::audio::Cue::focus);
            }
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::back))
        {
            xb_menu_.open = false;
            feedback.play(hui::audio::Cue::modal_close);
        }
        else if (input.is_pressed(Action::confirm))
        {
            if (!items[static_cast<std::size_t>(xb_menu_.active)].enabled)
                refuse(feedback, input);
            else
            {
                xb_menu_.open = false;
                menu_choose(xb_menu_.active, intent, feedback);
            }
        }
    }
    else if (focused && !pn_rows_)
    {
        // On the pivots: left and right change the section, Cross goes into its list.
        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            const int to = static_cast<int>(section_) + (input.nav == Direction::right ? 1 : -1);
            if (to >= 0 && to < static_cast<int>(Section::count))
            {
                open_section(static_cast<Section>(to));
                pn_row_ = 0;
                feedback.play(hui::audio::Cue::focus);
            }
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::confirm) || input.nav == Direction::down)
        {
            if (section_ == Section::pairing || rows.size == 0)
                refuse(feedback, input);
            else
            {
                pn_rows_ = true;
                pn_row_ = 0;
                feedback.play(hui::audio::Cue::focus);
            }
        }
        else if (input.is_pressed(Action::back))
        {
            intent.kind = Intent::Kind::close;
            feedback.play(hui::audio::Cue::back);
        }
    }
    else if (focused)
    {
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int to = pn_row_ + (input.nav == Direction::down ? 1 : -1);
            if (to >= 0 && to < rows.size)
            {
                pn_row_ = to;
                feedback.play(hui::audio::Cue::focus);
            }
            else if (to < 0)
            {
                // Up from the first row: back on the pivots.
                pn_rows_ = false;
                feedback.play(hui::audio::Cue::focus);
            }
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::back))
        {
            pn_rows_ = false;
            feedback.play(hui::audio::Cue::back);
        }
        else if (input.is_pressed(Action::confirm) && rows.size > 0)
        {
            xb::Row row;
            rows.fill(pn_row_, row);
            press(row.key, row.off, input, intent, feedback);
        }
    }
    xb_band_in_.target = crossbar_band_open() ? 1.0f : 0.0f;
    xb_band_in_.update(dt, 16.0f);
    xb_menu_.animate(dt);
    return intent;
}

void SettingsScreen::draw_panorama(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const Snapshot &state = ctx_.store.state();
    const ui::Type &type = ctx_.type;
    pano::draw_header(ctx_, list, pano::lower(tr("Settings")), 0.0f);
    // The sections in lowercase, the current one first and bright.
    std::vector<std::string> pivots;
    for (int i = 0; i < static_cast<int>(Section::count); ++i)
        pivots.push_back(pano::lower(tr(kTitles[i])));
    pano::draw_pivots(ctx_, list, pano::kLeft, 282.0f, pivots, static_cast<int>(section_));
    const float x = pano::kLeft, w = 1300.0f;
    float top = 376.0f;
    if (section_ == Section::pairing)
    {
        // The address and the code, large.
        top += pano::draw_caps(ctx_, &list, x, top, tr("Pair a phone or computer"));
        top += 10.0f + pano::draw_text(ctx_, &list, x, top + 10.0f, 1100.0f,
                                       tr("Browse Orbit and queue downloads from another device on the same network as this PS5. "
                                          "Downloads still run on this console."),
                                       22.0f, pano::kDim, pano::Face::body, 1.5f, 3);
        top += 34.0f + pano::draw_caps(ctx_, &list, x, top + 34.0f, "1 \xC2\xB7 " + std::string(tr("Open Orbit in a web browser")));
        if (!state.address.empty())
        {
            ui::text_fit(list, type.medium, "http://" + state.address + ":34177", x, pano::baseline(top + 10.0f, 46.0f, 1.3f), 46.0f, w, pano::kInk);
            top += 10.0f + 46.0f * 1.3f;
        }
        else
        {
            const std::string address = tr("Your PS5\xE2\x80\x99s IP address, port {port}", {{"port", "34177"}});
            top += 8.0f + pano::draw_text(ctx_, &list, x, top + 8.0f, w, address, 46.0f, pano::kInk, pano::Face::light, 1.3f, 1);
            top += 6.0f + pano::draw_text(ctx_, &list, x, top + 6.0f, w,
                                          tr("Find it on the PS5 in Settings \xE2\x86\x92 Network \xE2\x86\x92 Connection Status."), 20.0f, pano::kDim);
        }
        top += 34.0f + pano::draw_caps(ctx_, &list, x, top + 34.0f, "2 \xC2\xB7 " + std::string(tr("Enter this code")));
        std::string code = state.pair_code;
        if (code.size() == 6)
            code.insert(3, " ");
        hui::ui::text(list, pano::font(ctx_, pano::Face::xlight, code), code.empty() ? "\xE2\x80\x94" : code, x, pano::baseline(top, 170.0f, 1.0f),
                      170.0f, pano::kInk, ui::Align::left, 0.02f * 170.0f);
        top += 170.0f + 8.0f;
        pano::draw_text(ctx_, &list, x, top, w, tr("The code changes when Orbit restarts. Devices you\xE2\x80\x99ve paired stay paired."), 19.0f,
                        pano::kFaint);
    }
    else
    {
        // The section's intro, then its list; a long list follows the focus.
        xb::Row intro;
        rows_of({{'s', static_cast<int>(section_)}}).fill(0, intro);
        if (!intro.text.empty())
            top += 14.0f + pano::draw_text(ctx_, &list, x, top, 1200.0f, intro.text, 22.0f, pano::kDim, pano::Face::body, 1.5f, 3);
        const std::vector<pano::Item> items = panorama_items();
        const int focus = pn_rows_ && !xb_menu_.open ? pn_row_ : -1;
        float shift = 0.0f;
        if (pn_rows_)
        {
            const float bottom = top + pano::list_offset(ctx_, w, items, pn_row_ + 1);
            shift = std::max(0.0f, bottom - 990.0f);
        }
        list.push_clip({0.0f, top - 4.0f, 1920.0f, 1000.0f - top + 4.0f});
        pano::draw_list(ctx_, &list, x, top - shift, w, items, focus);
        list.pop_clip();
    }
    hui::gfx::DrawList &over = frame.overlay;
    if (xb_menu_.in.value > 0.01f)
    {
        // A setting's values, full screen; the interfaces show the focused one
        // beside a narrow list until Cross switches.
        xb::MenuFoot foot;
        const std::vector<xb::MenuItem> values = crossbar_menu(&foot);
        pano::Picker picker;
        picker.head = crossbar_menu_title();
        for (const auto &[label, value] : foot.figures)
            picker.head += " \xC2\xB7 " + label + " " + value;
        picker.note = foot.note;
        for (int i = 0; i < static_cast<int>(values.size()); ++i)
        {
            const xb::MenuItem &v = values[static_cast<std::size_t>(i)];
            pano::Item item;
            item.title = v.label;
            item.sub = !v.sub.empty() ? v.sub : v.value;
            item.chosen = v.checked;
            item.off = !v.enabled;
            if (xb_menu_kind_ == 7)
            {
                item.has_swatch = true;
                item.swatch = look::accent_color(static_cast<look::Accent>(i));
            }
            if (xb_menu_kind_ == 0 && i > 0 && i - 1 < static_cast<int>(state.drives.size()))
            {
                // A drive: its bar, as full as the drive is.
                const Drive &d = state.drives[static_cast<std::size_t>(i - 1)];
                if (d.total_bytes > 0.0)
                    item.bar = static_cast<float>(std::clamp(1.0 - d.free_bytes / d.total_bytes, 0.0, 1.0));
            }
            picker.items.push_back(std::move(item));
        }
        picker.focus = xb_menu_.active;
        picker.size = (xb_menu_kind_ == 1 || xb_menu_kind_ >= 10) ? 44.0f : 50.0f;
        if (xb_menu_kind_ == 4)
        {
            if (xb_menu_.active != static_cast<int>(look::prefs().interface))
            {
                // Another interface focused: it shows beside a narrow list.
                draw_interface_thumb(ctx_, over, xb_menu_.active, {0.0f, 0.0f, 1920.0f, 1080.0f});
                picker.narrow = true;
                picker.size = 46.0f;
                picker.head += " \xC2\xB7 " + tr("Previewing {name}", {{"name", tr(look::title(static_cast<look::Interface>(xb_menu_.active)))}});
                picker.note = tr("Nothing changes until you press Cross.");
            }
        }
        pano::draw_picker(ctx_, over, picker, xb_menu_.in.value);
    }
    if (xb_band_in_.value > 0.01f)
    {
        // Restarting and the confirmations, as message bands.
        const xb::Band question = crossbar_band();
        pano::Band band;
        band.title = question.title;
        band.body = question.body;
        band.busy = question.busy;
        for (const std::string &b : question.buttons)
            band.buttons.push_back(pano::button(b));
        band.focus = question.buttons.empty() ? -1 : xb_band_;
        pano::draw_band(ctx_, over, band, xb_band_in_.value);
    }
}

std::vector<hui::ui::Hint> SettingsScreen::panorama_hints() const
{
    if (crossbar_band_open())
        return {};
    if (xb_menu_.open)
        return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Back")}};
    if (!pn_rows_)
    {
        if (section_ == Section::pairing)
            return {{hui::ui::Button::circle, tr("Back")}, {hui::ui::Button::l1, tr("Pivots"), hui::ui::Button::r1}};
        return {{hui::ui::Button::cross, tr("Open")}, {hui::ui::Button::l1, tr("Pivots"), hui::ui::Button::r1}};
    }
    const xb::List rows = rows_of(section_entries());
    xb::Row row;
    if (rows.size > 0)
        rows.fill(std::clamp(pn_row_, 0, rows.size - 1), row);
    std::vector<hui::ui::Hint> hints;
    const char kind = row.key.empty() || row.off ? ' ' : row.key[0]; // an unavailable row has no Cross
    if (row.toggle >= 0 && !row.off)
        hints.push_back({hui::ui::Button::cross, row.toggle > 0 ? tr("Turn off") : tr("Turn on")});
    else if (kind == 'd' || kind == 'l' || kind == 'C' || kind == 'i' || kind == 'b' || kind == 'm' || kind == 'g' || kind == 'A' ||
             kind == 'L' || kind == 'Y' || kind == 'Q' || kind == 'M')
        hints.push_back({hui::ui::Button::cross, tr("Change")});
    else if (kind == 't' || kind == 'r' || kind == 'w' || kind == 'N' || kind == 'V')
        hints.push_back({hui::ui::Button::cross, tr("Open")});
    else if (kind == 'O' && !row.off)
        hints.push_back({hui::ui::Button::cross, ctx_.store.state().archive_account.signed_in ? tr("Sign out") : tr("Sign in")});
    else if (kind == 'c' || kind == 'u')
        hints.push_back({hui::ui::Button::cross, tr("Refresh")});
    else if (kind == 'U')
        hints.push_back({hui::ui::Button::cross, tr("Update Orbit")});
    else if (kind == 'D')
        hints.push_back({hui::ui::Button::cross, tr("Save")});
    else if (kind == 'v')
        hints.push_back({hui::ui::Button::cross, tr("Save sources")});
    else if (kind == 'a')
        hints.push_back({hui::ui::Button::cross, tr("Select")});
    hints.push_back({hui::ui::Button::circle, tr("Back")});
    hints.push_back({hui::ui::Button::l1, tr("Pivots"), hui::ui::Button::r1});
    return hints;
}

} // namespace orbit
