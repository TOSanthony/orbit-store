// SPDX-License-Identifier: GPL-3.0-or-later
// Public ABI: SharpProspero's WebBrowserDialog interop, corroborated by
// EVO-PLAYER-PS5's hardware dialog lifecycle notes. No private offsets/input.
#include "platform/ps5/provider_dialog.hpp"
#include "platform/ps5/provider_dialog_abi.hpp"
#include "platform/ps5/system.hpp"
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <sys/stat.h>

extern "C" {
int sceSysmoduleLoadModule(std::uint16_t);
// Resolve through native module imports at load time. On the tested console,
// sceKernelDlsym rejects these dialog exports even after both modules load.
int sceCommonDialogInitialize(void);
int sceWebBrowserDialogInitialize(void);
int sceWebBrowserDialogOpen(hui::ps5::dialog_abi::Param *);
int sceWebBrowserDialogUpdateStatus(void);
int sceWebBrowserDialogGetResult(hui::ps5::dialog_abi::Result *);
int sceWebBrowserDialogClose(void);
int sceUserServiceGetForegroundUser(int *);
int sceUserServiceGetInitialUser(int *);
}
namespace {
using hui::ps5::dialog_abi::Base;
using hui::ps5::dialog_abi::Param;
using hui::ps5::dialog_abi::Result;
alignas(16) Param param;
std::string page;

// Bounded, best-effort lifecycle diagnostics. Never record the page URL,
// signed file links, cookies, credentials, or browser content.
void trace(const char *phase, int result) {
    hui::sys::log("[ORBIT] provider-dialog %s rc=0x%x", phase, static_cast<unsigned>(result));
    // Native titles cannot write the service's /data directory. Use the
    // same persistent title mount as appearance settings and runtime logs.
    (void)::mkdir("/download0/orbit-store", 0755);
    constexpr const char *path = "/download0/orbit-store/native-provider.log";
    std::FILE *file = std::fopen(path, "a+");
    if (!file) return;
    if (std::fseek(file, 0, SEEK_END) == 0 && std::ftell(file) > 16384) {
        std::fclose(file); file = std::fopen(path, "w");
    }
    if (!file) return;
    std::fprintf(file, "%lld %s 0x%x\n", static_cast<long long>(hui::sys::monotonic_us()), phase, static_cast<unsigned>(result));
    std::fclose(file);
}

} // namespace
namespace hui::ps5 {
bool ProviderDialog::initialize() {
    if (ready_) return true;
    const int load = sceSysmoduleLoadModule(0x00ab);
    trace("module", load);
    if (load != 0) return false;
    const int common_rc = sceCommonDialogInitialize();
    trace("common-init", common_rc);
    if (common_rc != 0 && static_cast<std::uint32_t>(common_rc) != 0x80b80002u) {
        return false;
    }
    const int rc = sceWebBrowserDialogInitialize();
    trace("init", rc);
    ready_ = rc == 0;
    // Keep it initialized for this process lifetime, including between opens.
    return ready_;
}
bool ProviderDialog::open(const std::string &url) {
    error_code_ = 0;
    if (!ready_ || owned_ || url.rfind("https://", 0) != 0) return false;
    int user = -1;
    if (sceUserServiceGetForegroundUser(&user) != 0 || user < 0)
        if (sceUserServiceGetInitialUser(&user) != 0 || user < 0) return false;
    page = url;
    param = {};
    param.base.size = sizeof(Base);
    param.base.magic = 0xc0d1a109u + static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&param.base));
    param.size = sizeof(param); param.mode = 1; param.user = user; param.url = page.c_str();
    const int rc = sceWebBrowserDialogOpen(&param);
    if (rc < 0) error_code_ = rc;
    trace("open", rc);
    owned_ = rc == 0; last_status_ = -1; running_ = requested_close_ = false;
    opened_at_ = sys::monotonic_us();
    if (!owned_) page.clear();
    return owned_;
}
int ProviderDialog::status() {
    if (!owned_) return 0;
    int state = sceWebBrowserDialogUpdateStatus();
    if (state < 0) error_code_ = state;
    if (state == 2) running_ = true;
    const bool stopped = state == 1 && (running_ || sys::monotonic_us() - opened_at_ > 5000000);
    if (state != last_status_) trace("status", state);
    last_status_ = state;
    if (state <= 0 || state == 3 || stopped) {
        if (state == 3) {
            Result result{};
            const int rc = sceWebBrowserDialogGetResult(&result);
            trace("finished", rc); trace("result", result.result);
            // A positive result can be a normal user dismissal. Do not invent
            // a network diagnosis from undocumented result values.
            if (rc < 0) error_code_ = rc;
            else if (rc == 0 && result.result < 0) error_code_ = result.result;
        }
        if (!requested_close_) (void)sceWebBrowserDialogClose();
        owned_ = false; page.clear();
    }
    return state;
}
int ProviderDialog::close() {
    if (!owned_) return -1;
    const int rc = sceWebBrowserDialogClose();
    requested_close_ = rc == 0;
    trace("close", rc);
    return rc;
}
} // namespace hui::ps5
