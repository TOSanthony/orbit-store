// Native ownership of one user-requested provider dialog.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "orbit/model.hpp"
#include <string>

namespace orbit {
class ProviderWindow {
public:
    virtual ~ProviderWindow() = default;
    virtual bool open(const std::string &url) = 0;
    // 0 none, 1 initialized, 2 running, 3 finished; negative means an API error.
    virtual int status() = 0;
    virtual int close() = 0;
    // Negative platform error from the latest open/status/result, if available.
    virtual int error_code() const { return 0; }
};
class NativeProvider {
public:
    struct Event {
        enum Kind { none, opened, cancel } kind = none;
        std::string id, client_id, error;
        std::string failure;
        int error_code = 0;
    };
    void set_window(ProviderWindow *window) { window_ = window; }
    bool available() const { return window_ != nullptr; }
    bool busy() const { return !client_.empty(); }
    bool visible() const { return launched_; }
    void begin(std::string client, double now);
    void abandon() { if (!launched_) reset(); }
    Event tick(const BrowserSession &session, bool fresh, double now);
private:
    void reset();
    ProviderWindow *window_ = nullptr;
    std::string client_, id_;
    double began_ = 0, last_seen_ = 0, next_close_ = 0, opened_at_ = 0;
    bool launched_ = false, closing_ = false, cancelled_ = false, running_ = false;
    unsigned close_attempts_ = 0;
};
} // namespace orbit
