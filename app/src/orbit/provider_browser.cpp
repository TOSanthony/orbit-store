// Orbit Store TV app - Provider verification without the browser storefront.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/screens.hpp"
#include "orbit/i18n.hpp"
#include "orbit/look.hpp"

namespace orbit
{

bool App::provider_actionable() const
{
    const auto &state = ctx_.store.state();
    const auto &browser = state.browser;
    return state.have_browser && state.link == Link::online && !ctx_.store.browser_action().busy &&
           ((!browser.active && browser.state == "queued" && !browser.job_id.empty()) ||
            (browser.active && browser.state != "cancelled" && browser.state != "queued" && !browser.id.empty()));
}

void App::update_provider(const InputFrame &input, Feedback &feedback)
{
    if (!provider_actionable()) provider_focus_ = 1;
    if (input.is_pressed(Action::back))
    {
        provider_open_ = false; // Going back never cancels or starts a transfer.
        feedback.play(hui::audio::Cue::back);
        return;
    }
    if (input.nav != Direction::none)
    {
        const bool previous = input.nav == Direction::left || input.nav == Direction::up;
        provider_focus_ = previous && provider_actionable() ? 0 : 1;
        feedback.play(hui::audio::Cue::focus);
    }
    if (!input.is_pressed(Action::confirm)) return;
    feedback.play(hui::audio::Cue::select);
    if (provider_focus_ == 1)
    {
        provider_open_ = false;
        return;
    }
    if (!provider_actionable()) return;
    const auto &browser = ctx_.store.state().browser;
    if (browser.active) ctx_.store.cancel_browser(browser.id);
    else
    {
        provider_open_ = false;
        Intent next;
        next.kind = Intent::Kind::show_job;
        next.id = browser.job_id;
        handle_intent(next, feedback);
    }
}

void App::draw_provider(Frame &frame) const
{
    auto &list = frame.overlay;
    const auto &state = ctx_.store.state();
    const auto &browser = state.browser;
    const auto &action = ctx_.store.browser_action();
    const auto &type = ctx_.type;
    if (!look::is(look::Interface::store))
    {
        // The other interfaces ask as they ask everything: Crossbar and
        // Panorama in a band, Cinema in a title card. Same words, same buttons.
        const char *heading = action.busy ? tr("Please wait\xE2\x80\xA6")
            : browser.state == "queued" ? tr("Added to Downloads")
            : !state.have_browser || browser.state == "failed" || browser.state == "idle"
                ? tr("Verification couldn't finish")
            : browser.state == "cancelled" ? tr("Verification cancelled")
            : browser.state == "validating" ? tr("Checking your file")
            : browser.state == "waiting-link" ? tr("Waiting for the download link")
            : browser.state == "opening" ? tr("Opening the download page on your PS5…")
            : tr("Provider verification");
        const std::string message = !state.have_browser ? state.browser_error
            : browser.state == "idle" && !action.busy
                ? tr("This verification session is no longer active.") : browser.message;
        const bool busy = action.busy || browser.state == "validating" || browser.state == "waiting-link" ||
                          browser.state == "opening";
        const bool queued = browser.state == "queued" && !browser.active;
        const char *label = queued ? tr("View download")
            : action.busy || browser.state == "cancelled" ? tr("Cancelling…") : tr("Cancel verification");
        const bool second = browser.active || queued;
        const std::string kicker = std::string(tr("Provider verification")) +
                                   (browser.title.empty() ? std::string() : " \xC2\xB7 " + browser.title);
        const int focus = second ? provider_focus_ : 0;
        if (look::is(look::Interface::panorama))
        {
            pano::Band band;
            band.kicker = kicker;
            band.title = heading;
            band.body = tr(std::string_view(message));
            band.error = tr(std::string_view(action.error));
            band.busy = busy;
            if (second)
            {
                pano::Button cancel = pano::button(label);
                cancel.enabled = provider_actionable();
                band.buttons.push_back(cancel);
            }
            band.buttons.push_back(pano::button(tr("Back")));
            band.focus = focus;
            pano::draw_band(ctx_, list, band, 1.0f);
        }
        else if (look::is(look::Interface::cinema))
        {
            cn::Title card;
            card.kicker = kicker;
            card.title = heading;
            card.body = tr(std::string_view(message));
            card.small = tr(std::string_view(action.error));
            card.spin = busy;
            if (second)
            {
                cn::Button cancel = cn::button(label, queued ? icons::Icon::down : icons::Icon::close);
                cancel.has_icon = false;
                cancel.enabled = provider_actionable();
                cancel.focus = focus == 0;
                card.buttons.push_back(cancel);
            }
            cn::Button back;
            back.label = tr("Back");
            back.primary = true;
            back.focus = focus == (second ? 1 : 0);
            card.buttons.push_back(back);
            cn::draw_title(ctx_, list, card, 1.0f);
        }
        else
        {
            xb::Band band;
            band.kicker = kicker;
            band.title = heading;
            band.body = tr(std::string_view(message));
            band.error = tr(std::string_view(action.error));
            band.busy = busy;
            if (second)
                band.buttons.push_back(label);
            band.buttons.push_back(tr("Back"));
            band.focus = focus;
            xb::draw_band(ctx_, list, band, 1.0f);
        }
        return;
    }
    list.rounded_rect({0,0,1920,1080},0,ui::Color::rgb(0x000000,0.72f));
    const ui::Rect panel{280,220,1360,660};
    list.shadow(panel,30,40,ui::Color::rgb(0x000000,0.4f));
    ui::surface(list,panel,30,ui::color::night2);
    ui::text(list,type.medium,tr("Provider verification"),330,286,30,ui::color::text2);
    ui::text_fit(list,type.medium,browser.title.empty() ? tr("Provider verification") : browser.title,
                 330,345,36,1260,ui::color::text);
    const char *heading = action.busy ? tr("Please wait\xE2\x80\xA6")
        : browser.state == "queued" ? tr("Added to Downloads")
        : !state.have_browser || browser.state == "failed" || browser.state == "idle"
            ? tr("Verification couldn't finish")
        : browser.state == "cancelled" ? tr("Verification cancelled")
        : browser.state == "validating" ? tr("Checking your file")
        : browser.state == "waiting-link" ? tr("Waiting for the download link")
        : browser.state == "opening" ? tr("Opening the download page on your PS5…")
        : tr("Provider verification");
    ui::paragraph(list,type.medium,heading,330,411,26,1260,35,ui::color::text,3);
    const std::string message = !state.have_browser ? state.browser_error
        : browser.state == "idle" && !action.busy
            ? tr("This verification session is no longer active.") : browser.message;
    ui::paragraph(list,type.regular,std::string(tr(std::string_view(message))),330,517,22,1260,31,
                  ui::color::text2,4);
    if (!action.error.empty())
        ui::paragraph(list,type.regular,std::string(tr(std::string_view(action.error))),330,665,21,1260,29,
                      ui::color::bad,3);
    const bool queued = browser.state == "queued" && !browser.active;
    const char *label = queued ? tr("View download")
        : action.busy || browser.state == "cancelled" ? tr("Cancelling…") : tr("Cancel verification");
    if (browser.active || queued)
        ui::button(list,type,{330,773,605,65},label,ui::ButtonKind::secondary,
                   provider_focus_ == 0 ? 1 : 0,provider_actionable(),23);
    ui::button(list,type,{965,773,625,65},tr("Back"),ui::ButtonKind::light,
               provider_focus_ == 1 ? 1 : 0,true,23);
}

} // namespace orbit
