// Orbit Store TV app - ntfy notifications (App settings → Storage).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The browser's ntfy panel (src/NtfySetting.tsx) for a controller: rows for
// the switch, server, topic and optional sign-in over the screen. Cross
// switches a row or types into it with the look's keyboard.

#include "orbit/screens.hpp"
#include "orbit/i18n.hpp"
#include "orbit/json.hpp"
#include "orbit/look.hpp"

#include <algorithm>

namespace orbit
{

namespace
{

constexpr float kLeft = 360.0f;
constexpr float kWidth = 1200.0f;
constexpr float kTop = 150.0f;
constexpr float kRow = 66.0f;
constexpr float kValueX = 320.0f; // where a row's value starts, from its left edge

void pop_character(std::string &text)
{
    if (text.empty())
        return;
    std::size_t at = text.size() - 1;
    while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80)
        --at;
    text.resize(at);
}

void wipe(std::string &text)
{
    std::fill(text.begin(), text.end(), '\0');
    text.clear();
}

std::size_t limit(int row)
{
    return row == 1 ? 256 : row == 2 ? 64 : 128; // server, topic, username or password
}

} // namespace

NtfyForm::NtfyForm(Context &context) : ctx_(context)
{
    keyboard_.style.bindings = hui::ui::KeyboardBindings::standard();
}

void NtfyForm::open()
{
    const NtfySettings &saved = ctx_.store.state().ntfy;
    enabled_ = saved.enabled;
    server_ = saved.server;
    topic_ = saved.topic;
    auth_ = saved.has_password;
    username_ = saved.username;
    wipe(password_);
    row_ = 0;
    button_ = 0;
    typing_ = false;
    waiting_ = Setting::none;
    serial_ = ctx_.store.setting_result().serial;
    error_.clear();
    message_.clear();
    open_ = true;
    in_.snap(0.0f);
    in_.target = 1.0f;
}

void NtfyForm::close()
{
    wipe(password_);
    typing_ = false;
    open_ = false;
}

std::vector<NtfyForm::Row> NtfyForm::rows() const
{
    std::vector<Row> out{Row::enabled, Row::server, Row::topic, Row::auth};
    if (auth_)
    {
        out.push_back(Row::username);
        out.push_back(Row::password);
    }
    out.push_back(Row::buttons);
    return out;
}

std::string *NtfyForm::text(Row row)
{
    return row == Row::server     ? &server_
           : row == Row::topic    ? &topic_
           : row == Row::username ? &username_
           : row == Row::password ? &password_
                                  : nullptr;
}

bool NtfyForm::changed() const
{
    const NtfySettings &saved = ctx_.store.state().ntfy;
    return enabled_ != saved.enabled || server_ != saved.server || topic_ != saved.topic ||
           auth_ != saved.has_password || username_ != saved.username || !password_.empty();
}

void NtfyForm::update(const InputFrame &input, float dt, Feedback &feedback)
{
    in_.update(dt);
    if (!open_)
        return;
    // The keyboard of the look the form is in.
    if (look::is(look::Interface::cinema))
    {
        cn::keyboard_look(keyboard_.style);
        keyboard_.set_azerty(ctx_.azerty);
        keyboard_.set_bounds(cn::keyboard_keys());
    }
    else if (look::is(look::Interface::panorama))
    {
        pano::keyboard_look(keyboard_.style);
        keyboard_.set_azerty(ctx_.azerty);
        keyboard_.set_bounds(pano::keyboard_keys());
    }
    else
    {
        const bool crossbar = look::is(look::Interface::crossbar);
        xb::keyboard_look(keyboard_.style, crossbar);
        keyboard_.set_azerty(ctx_.azerty);
        keyboard_.set_bounds(crossbar ? xb::keyboard_keys() : ui::Rect{320.0f, 590.0f, 1280.0f, 400.0f});
    }
    const std::size_t max = limit(static_cast<int>(typing_row_));
    keyboard_.style.bindings = hui::ui::KeyboardBindings::standard();
    keyboard_.style.max_length = static_cast<int>(max);
    keyboard_.style.done_label = tr("Done");
    keyboard_.set_active(typing_);
    keyboard_.update(dt);

    // What the store has finished: a save or a test notification.
    const SettingResult &result = ctx_.store.setting_result();
    if (waiting_ != Setting::none && result.serial != serial_ && !result.busy)
    {
        serial_ = result.serial;
        const Setting done = waiting_;
        waiting_ = Setting::none;
        if (!result.error.empty())
            error_ = tr(std::string_view(result.error));
        else if (done == Setting::ntfy_save)
        {
            // As the service saved them (an address loses its trailing slash,
            // the username goes with authentication), so Send test is ready.
            const NtfySettings &saved = ctx_.store.state().ntfy;
            enabled_ = saved.enabled;
            server_ = saved.server;
            topic_ = saved.topic;
            auth_ = saved.has_password;
            username_ = saved.username;
            wipe(password_);
            message_ = tr("ntfy settings saved.");
        }
        else
            message_ = tr("Test notification sent. Check the ntfy app on your phone or computer.");
        feedback.play(result.error.empty() ? hui::audio::Cue::saved : hui::audio::Cue::error);
    }

    if (typing_)
    {
        const hui::ui::Event event = keyboard_.handle(input, feedback);
        std::string &value = *text(typing_row_);
        for (int i = 0; i < keyboard_.erased(); ++i)
            pop_character(value);
        std::string typed = keyboard_.typed();
        // Addresses and topics never hold spaces; passwords may.
        if (typing_row_ != Row::password)
            typed.erase(std::remove(typed.begin(), typed.end(), ' '), typed.end());
        if (value.size() + typed.size() <= max)
            value += typed;
        wipe(typed);
        keyboard_.set_length(static_cast<int>(value.size()));
        if (event == hui::ui::Event::activated || event == hui::ui::Event::cancelled)
        {
            typing_ = false;
            feedback.play(hui::audio::Cue::modal_close);
        }
        return;
    }

    const std::vector<Row> list = rows();
    row_ = std::min(row_, static_cast<int>(list.size()) - 1);
    const Row row = list[static_cast<std::size_t>(row_)];
    const NtfySettings &saved = ctx_.store.state().ntfy;
    const bool busy = waiting_ != Setting::none || result.busy;
    if (input.is_pressed(Action::back))
    {
        if (waiting_ != Setting::none)
            refuse(feedback, input);
        else
        {
            close();
            feedback.play(hui::audio::Cue::back);
        }
    }
    else if (input.nav == Direction::up || input.nav == Direction::down)
    {
        const int to = row_ + (input.nav == Direction::down ? 1 : -1);
        if (to >= 0 && to < static_cast<int>(list.size()))
        {
            row_ = to;
            feedback.play(hui::audio::Cue::focus);
        }
        else
            refuse(feedback, input);
    }
    else if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const int to = button_ + (input.nav == Direction::right ? 1 : -1);
        if (row == Row::buttons && to >= 0 && to < 3)
        {
            button_ = to;
            feedback.play(hui::audio::Cue::focus);
        }
        else
            refuse(feedback, input);
    }
    else if (input.is_pressed(Action::confirm))
    {
        if (busy && !(row == Row::buttons && button_ == 2))
            refuse(feedback, input);
        else if (row == Row::enabled || row == Row::auth)
        {
            bool &flag = row == Row::enabled ? enabled_ : auth_;
            flag = !flag;
            message_.clear();
            feedback.play(hui::audio::Cue::toggle);
        }
        else if (row != Row::buttons)
        {
            typing_ = true;
            typing_row_ = row;
            message_.clear();
            keyboard_.enter();
            keyboard_.set_length(static_cast<int>(text(row)->size()));
            feedback.play(hui::audio::Cue::modal_open);
        }
        else if (button_ == 2)
        {
            if (waiting_ != Setting::none)
                refuse(feedback, input);
            else
            {
                close();
                feedback.play(hui::audio::Cue::back);
            }
        }
        else if (button_ == 0 ? !changed() : changed() || saved.server.empty() || saved.topic.empty())
            refuse(feedback, input);
        else
        {
            error_.clear();
            message_.clear();
            serial_ = result.serial;
            if (button_ == 0)
            {
                waiting_ = Setting::ntfy_save;
                // Room for every field up front, so the password is never left in a freed copy.
                std::string body, secret = json::quote(auth_ ? password_ : std::string());
                body.reserve(1024 + secret.size());
                body += std::string("{\"enabled\":") + (enabled_ ? "true" : "false");
                body += ",\"server\":" + json::quote(server_) + ",\"topic\":" + json::quote(topic_);
                body += std::string(",\"auth\":") + (auth_ ? "true" : "false");
                body += ",\"username\":" + json::quote(auth_ ? username_ : std::string());
                body += ",\"password\":";
                body += secret;
                body += "}";
                wipe(secret);
                ctx_.store.save_ntfy(std::move(body));
            }
            else
            {
                waiting_ = Setting::ntfy_test;
                ctx_.store.test_ntfy();
            }
            feedback.play(hui::audio::Cue::select);
        }
    }
}

void NtfyForm::draw(Frame &frame) const
{
    // Only while open: update() stops when it closes, so a fade-out would never finish.
    if (!open_)
        return;
    const ui::Type &type = ctx_.type;
    hui::gfx::DrawList &overlay = frame.overlay;
    overlay.push_opacity(in_.value);
    overlay.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, ui::color::night0.with_alpha(0.92f));
    const std::string masked(password_.size(), '*');
    const auto label = [](Row row)
    {
        return row == Row::enabled    ? tr("Send ntfy notifications")
               : row == Row::server   ? tr("Server address")
               : row == Row::topic    ? tr("Topic")
               : row == Row::auth     ? tr("Use authentication")
               : row == Row::username ? tr("Username")
                                      : tr("Password");
    };
    if (typing_)
    {
        // Typing: the row on its own above the keys, as the archive password asks.
        ui::text_fit(overlay, type.light, label(typing_row_), 320.0f, 395.0f, 46.0f, 1280.0f, ui::color::text);
        const std::string &value = typing_row_ == Row::password  ? masked
                                   : typing_row_ == Row::server  ? server_
                                   : typing_row_ == Row::topic   ? topic_
                                                                 : username_;
        ui::text_fit(overlay, type.regular, value.empty() ? std::string("\xE2\x80\x94") : value, 320.0f, 520.0f, 30.0f,
                     1280.0f, value.empty() ? ui::color::text3 : ui::color::text);
        hui::ui::Canvas canvas{overlay, ctx_.fonts, frame.glass_texture, ctx_.clock};
        keyboard_.draw(canvas);
        overlay.pop_opacity();
        return;
    }
    ui::text_fit(overlay, type.light, tr("ntfy notifications"), kLeft, kTop + 44.0f, 46.0f, kWidth, ui::color::text);
    ui::paragraph(overlay, type.regular,
                  tr("Get a push notification on your phone or computer for the events you choose. "
                     "Subscribe to the same topic in the ntfy app."),
                  kLeft, kTop + 100.0f, 21.0f, kWidth, 32.0f, ui::color::text2, 2);
    const NtfySettings &saved = ctx_.store.state().ntfy;
    const std::vector<Row> list = rows();
    float y = kTop + 166.0f;
    for (std::size_t i = 0; i + 1 < list.size(); ++i)
    {
        const Row row = list[i];
        const ui::Rect r{kLeft, y, kWidth, kRow - 12.0f};
        const bool focus = static_cast<int>(i) == row_;
        ui::focus_ring(overlay, r, 14.0f, focus ? 1.0f : 0.0f);
        ui::surface(overlay, r, 14.0f, focus ? ui::color::field_open : ui::color::field);
        ui::text_fit(overlay, type.regular, label(row), r.x + 24.0f, r.cy() + 8.0f, 20.0f, kValueX - 40.0f,
                     ui::color::text2);
        std::string value, hint;
        if (row == Row::enabled || row == Row::auth)
            value = (row == Row::enabled ? enabled_ : auth_) ? tr("On") : tr("Off");
        else if (row == Row::password)
        {
            value = masked;
            if (value.empty() && saved.has_password && username_ == saved.username)
                hint = tr("Saved. Type a new one to change it.");
        }
        else
        {
            value = row == Row::server ? server_ : row == Row::topic ? topic_ : username_;
            if (value.empty() && row == Row::server)
                hint = "https://ntfy.sh";
        }
        ui::text_fit(overlay, type.medium, value.empty() ? hint : value, r.x + kValueX, r.cy() + 8.0f, 22.0f,
                     r.w - kValueX - 24.0f, value.empty() ? ui::color::text3 : ui::color::text);
        y += kRow;
    }
    y += 12.0f;
    const SettingResult &result = ctx_.store.setting_result();
    const bool busy = waiting_ != Setting::none || result.busy;
    const std::string labels[] = {waiting_ == Setting::ntfy_save ? tr("Saving\xE2\x80\xA6") : tr("Save"),
                                  waiting_ == Setting::ntfy_test ? tr("Sending\xE2\x80\xA6") : tr("Send test notification"),
                                  tr("Close")};
    const bool enabled[] = {!busy && changed(), !busy && !changed() && !saved.server.empty() && !saved.topic.empty(),
                            waiting_ == Setting::none};
    float x = kLeft;
    for (int b = 0; b < 3; ++b)
    {
        const ui::ButtonKind kind = b == 0 ? ui::ButtonKind::primary : ui::ButtonKind::secondary;
        const float w = ui::button_width(type, labels[b], 21.0f, kind);
        ui::button(overlay, type, {x, y, w, ui::kButtonHeight}, labels[b], kind,
                   row_ == static_cast<int>(list.size()) - 1 && b == button_ ? 1.0f : 0.0f, enabled[b], 21.0f);
        x += w + 16.0f;
    }
    y += ui::kButtonHeight + 22.0f;
    if (!error_.empty() || !message_.empty())
    {
        ui::text_fit(overlay, type.regular, error_.empty() ? message_ : error_, kLeft, y + 14.0f, 21.0f, kWidth,
                     error_.empty() ? ui::color::text : ui::color::bad);
        y += 44.0f;
    }
    ui::text_fit(overlay, type.regular,
                 tr("Anyone who knows a topic on a public server can read it, so choose one that’s hard to guess."),
                 kLeft, y + 10.0f, 17.0f, kWidth, ui::color::text3);
    if (auth_)
        ui::text_fit(overlay, type.regular,
                     tr("For an access token, leave the username empty and enter the token as the password."), kLeft,
                     y + 40.0f, 17.0f, kWidth, ui::color::text3);
    overlay.pop_opacity();
}

std::vector<hui::ui::Hint> NtfyForm::hints() const
{
    if (typing_)
        return {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Back")}};
    return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Close")}};
}

} // namespace orbit
