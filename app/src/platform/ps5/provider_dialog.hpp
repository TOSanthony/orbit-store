// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "orbit/native_provider.hpp"
#include <cstdint>

namespace hui::ps5 {
// One native-owned dialog. All calls run on the application's frame thread.
class ProviderDialog final : public orbit::ProviderWindow {
public:
    bool initialize();
    bool open(const std::string &url) override;
    int status() override;
    int close() override;
    int error_code() const override { return error_code_; }
private:
    bool ready_ = false, owned_ = false, running_ = false, requested_close_ = false;
    std::int64_t opened_at_ = 0;
    int last_status_ = -1;
    int error_code_ = 0;
};
} // namespace hui::ps5
