// Orbit Store TV app - Signing in to Archive.org (App settings → Download sources).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The browser's Archive.org panel (src/ArchiveAccount.tsx) for a controller:
// an email row and a password row over the screen. Cross types into a row with
// the look's keyboard; Sign in and Cancel sit under them.

#include "orbit/screens.hpp"
#include "orbit/i18n.hpp"
#include "orbit/look.hpp"

#include <algorithm>

namespace orbit
{

namespace
{

constexpr float kLeft = 360.0f;
constexpr float kWidth = 1200.0f;
constexpr float kTop = 220.0f;
constexpr float kRow = 72.0f;
constexpr float kValueX = 300.0f; // where a row's value starts, from its left edge

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

} // namespace

ArchiveSignIn::ArchiveSignIn(Context &context) : ctx_(context)
{
    keyboard_.style.bindings = hui::ui::KeyboardBindings::standard();
}

void ArchiveSignIn::open()
{
    wipe(email_);
    wipe(password_);
    row_ = 0;
    button_ = 0;
    typing_ = false;
    waiting_ = false;
    serial_ = ctx_.store.setting_result().serial;
    error_.clear();
    open_ = true;
    in_.snap(0.0f);
    in_.target = 1.0f;
}

void ArchiveSignIn::close()
{
    wipe(email_);
    wipe(password_);
    typing_ = false;
    open_ = false;
}

ArchiveSignIn::Result ArchiveSignIn::update(const InputFrame &input, float dt, Feedback &feedback)
{
    in_.update(dt);
    if (!open_)
        return Result::none;
    // The keyboard of the look the form is in.
    if (look::is(look::Interface::cinema))
    {
        cn::keyboard_look(keyboard_.style);
        keyboard_.set_bounds(cn::keyboard_keys());
    }
    else if (look::is(look::Interface::panorama))
    {
        pano::keyboard_look(keyboard_.style);
        keyboard_.set_bounds(pano::keyboard_keys());
    }
    else
    {
        const bool crossbar = look::is(look::Interface::crossbar);
        xb::keyboard_look(keyboard_.style, crossbar);
        keyboard_.set_bounds(crossbar ? xb::keyboard_keys() : ui::Rect{320.0f, 590.0f, 1280.0f, 400.0f});
    }
    const std::size_t limit = typing_field_ == Field::email ? 254 : 256;
    keyboard_.style.bindings = hui::ui::KeyboardBindings::standard();
    keyboard_.style.max_length = static_cast<int>(limit);
    keyboard_.style.done_label = tr("Done");
    keyboard_.set_active(typing_);
    keyboard_.update(dt);

    // The sign-in the store has finished.
    const SettingResult &result = ctx_.store.setting_result();
    if (waiting_ && result.serial != serial_ && !result.busy)
    {
        serial_ = result.serial;
        waiting_ = false;
        wipe(password_);
        if (result.error.empty())
        {
            close();
            feedback.play(hui::audio::Cue::modal_close);
            return Result::signed_in;
        }
        error_ = tr(std::string_view(result.error));
    }

    if (typing_)
    {
        const hui::ui::Event event = keyboard_.handle(input, feedback);
        std::string &text = typing_field_ == Field::email ? email_ : password_;
        for (int i = 0; i < keyboard_.erased(); ++i)
            pop_character(text);
        std::string typed = keyboard_.typed();
        if (typing_field_ == Field::email)
            typed.erase(std::remove(typed.begin(), typed.end(), ' '), typed.end());
        if (text.size() + typed.size() <= limit)
            text += typed;
        wipe(typed);
        keyboard_.set_length(static_cast<int>(text.size()));
        if (event == hui::ui::Event::activated || event == hui::ui::Event::cancelled)
        {
            typing_ = false;
            // Done on the email goes on to the password.
            if (event == hui::ui::Event::activated && typing_field_ == Field::email)
                row_ = 1;
            feedback.play(hui::audio::Cue::modal_close);
        }
        return Result::none;
    }

    const bool ready = email_.find('@') != std::string::npos && !password_.empty();
    if (input.is_pressed(Action::back))
    {
        if (waiting_)
            refuse(feedback, input);
        else
        {
            close();
            feedback.play(hui::audio::Cue::back);
            return Result::closed;
        }
    }
    else if (input.nav == Direction::up || input.nav == Direction::down)
    {
        const int to = row_ + (input.nav == Direction::down ? 1 : -1);
        if (to >= 0 && to <= 2)
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
        if (row_ == 2 && to >= 0 && to < 2)
        {
            button_ = to;
            feedback.play(hui::audio::Cue::focus);
        }
        else
            refuse(feedback, input);
    }
    else if (input.is_pressed(Action::confirm))
    {
        if (row_ == 2 && button_ == 1 && !waiting_)
        {
            close();
            feedback.play(hui::audio::Cue::back);
            return Result::closed;
        }
        if (row_ == 2)
        {
            if (waiting_ || result.busy || !ready)
                refuse(feedback, input);
            else
            {
                waiting_ = true;
                serial_ = result.serial;
                error_.clear();
                ctx_.store.sign_in_archive(email_, password_);
                feedback.play(hui::audio::Cue::select);
            }
        }
        else if (waiting_)
            refuse(feedback, input);
        else
        {
            typing_ = true;
            typing_field_ = row_ == 0 ? Field::email : Field::password;
            keyboard_.enter();
            keyboard_.set_length(static_cast<int>((row_ == 0 ? email_ : password_).size()));
            feedback.play(hui::audio::Cue::modal_open);
        }
    }
    return Result::none;
}

void ArchiveSignIn::draw(Frame &frame) const
{
    // Only while open: update() stops when it closes, so a fade-out would never finish.
    if (!open_)
        return;
    const ui::Type &type = ctx_.type;
    hui::gfx::DrawList &overlay = frame.overlay;
    overlay.push_opacity(in_.value);
    overlay.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, ui::color::night0.with_alpha(0.92f));
    const std::string masked(password_.size(), '*');
    if (typing_)
    {
        // Typing: the row on its own above the keys, as the archive password asks.
        const bool email = typing_field_ == Field::email;
        ui::text_fit(overlay, type.light, email ? tr("Email") : tr("Password"), 320.0f, 395.0f, 46.0f, 1280.0f,
                     ui::color::text);
        const std::string &value = email ? email_ : masked;
        ui::text_fit(overlay, type.regular, value.empty() ? std::string("\xE2\x80\x94") : value, 320.0f, 520.0f, 30.0f,
                     1280.0f, value.empty() ? ui::color::text3 : ui::color::text);
        hui::ui::Canvas canvas{overlay, ctx_.fonts, frame.glass_texture, ctx_.clock};
        keyboard_.draw(canvas);
        overlay.pop_opacity();
        return;
    }
    ui::text_fit(overlay, type.light, tr("Sign in to Archive.org"), kLeft, kTop + 44.0f, 46.0f, kWidth, ui::color::text);
    ui::paragraph(overlay, type.regular,
                  tr("Some Archive.org games only download for signed-in accounts. Sign in with your Archive.org "
                     "account to download them."),
                  kLeft, kTop + 100.0f, 21.0f, kWidth, 32.0f, ui::color::text2, 2);
    const SettingResult &result = ctx_.store.setting_result();
    const bool ready = email_.find('@') != std::string::npos && !password_.empty();
    float y = kTop + 166.0f;
    for (int i = 0; i < 2; ++i)
    {
        const ui::Rect r{kLeft, y, kWidth, kRow - 12.0f};
        ui::focus_ring(overlay, r, 14.0f, i == row_ ? 1.0f : 0.0f);
        ui::surface(overlay, r, 14.0f, i == row_ ? ui::color::field_open : ui::color::field);
        ui::text_fit(overlay, type.regular, i == 0 ? tr("Email") : tr("Password"), r.x + 24.0f, r.cy() + 8.0f, 20.0f,
                     kValueX - 40.0f, ui::color::text2);
        const std::string &value = i == 0 ? email_ : masked;
        ui::text_fit(overlay, type.medium, value, r.x + kValueX, r.cy() + 8.0f, 22.0f, r.w - kValueX - 24.0f,
                     ui::color::text);
        y += kRow;
    }
    y += 12.0f;
    const std::string labels[] = {waiting_ ? tr("Signing in\xE2\x80\xA6") : tr("Sign in"), tr("Cancel")};
    float x = kLeft;
    for (int b = 0; b < 2; ++b)
    {
        const ui::ButtonKind kind = b == 0 ? ui::ButtonKind::primary : ui::ButtonKind::secondary;
        const float w = ui::button_width(type, labels[b], 21.0f, kind);
        const bool enabled = b == 1 ? !waiting_ : !waiting_ && !result.busy && ready;
        ui::button(overlay, type, {x, y, w, ui::kButtonHeight}, labels[b], kind,
                   row_ == 2 && b == button_ ? 1.0f : 0.0f, enabled, 21.0f);
        x += w + 16.0f;
    }
    y += ui::kButtonHeight + 22.0f;
    if (!error_.empty())
    {
        ui::text_fit(overlay, type.regular, error_, kLeft, y + 14.0f, 21.0f, kWidth, ui::color::bad);
        y += 44.0f;
    }
    ui::text_fit(overlay, type.regular,
                 tr("Orbit sends these to Archive.org once and keeps only the sign-in on your PS5, never your password."),
                 kLeft, y + 10.0f, 17.0f, kWidth, ui::color::text3);
    overlay.pop_opacity();
}

std::vector<hui::ui::Hint> ArchiveSignIn::hints() const
{
    if (typing_)
        return {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Back")}};
    return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Cancel")}};
}

} // namespace orbit
