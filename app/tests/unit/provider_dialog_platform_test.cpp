// Verify the real platform adapter against a stubbed public dialog API.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/ps5/provider_dialog.hpp"
#include "platform/ps5/provider_dialog_abi.hpp"
#include <gtest/gtest.h>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {
int module_rc, dialog_state=1, open_count, close_count, results;
int common_init_rc, dialog_init_rc;
int open_rc, result_rc, result_value;
std::vector<std::string> calls;
const unsigned char *saved_param;
int initialize_dialog_stub() { calls.emplace_back("browser-init"); return dialog_init_rc; }
int initialize_common_stub() { calls.emplace_back("common-init"); return common_init_rc; }
int open_stub(hui::ps5::dialog_abi::Param *p) {
    saved_param=reinterpret_cast<unsigned char *>(p);++open_count;
    std::uint64_t size,base_size;std::uint32_t magic;std::int32_t mode,user;
    std::memcpy(&base_size,saved_param,8);std::memcpy(&magic,saved_param+44,4);
    std::memcpy(&size,saved_param+48,8);std::memcpy(&mode,saved_param+56,4);std::memcpy(&user,saved_param+60,4);
    EXPECT_EQ(base_size,48u);EXPECT_EQ(size,328u);EXPECT_EQ(mode,1);EXPECT_EQ(user,42);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p)%16,0u);
    EXPECT_EQ(magic,0xc0d1a109u+static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)));
    dialog_state=1;return open_rc;
}
int update_stub() { return dialog_state; }
int result_stub(hui::ps5::dialog_abi::Result *r) { ++results;r->result=result_value;return result_rc; }
int close_stub() { ++close_count;return 0; }
void reset() {
    module_rc=common_init_rc=dialog_init_rc=0; calls.clear();
    open_rc=result_rc=result_value=0;
    dialog_state=1;open_count=close_count=results=0;saved_param=nullptr;
}
}
extern "C" {
int sceSysmoduleLoadModule(std::uint16_t module) { EXPECT_EQ(module,0xabu);calls.emplace_back("module");return module_rc; }
int sceCommonDialogInitialize(void) { return initialize_common_stub(); }
int sceWebBrowserDialogInitialize(void) { return initialize_dialog_stub(); }
int sceWebBrowserDialogOpen(hui::ps5::dialog_abi::Param *p) { return open_stub(p); }
int sceWebBrowserDialogUpdateStatus(void) { return update_stub(); }
int sceWebBrowserDialogGetResult(hui::ps5::dialog_abi::Result *r) { return result_stub(r); }
int sceWebBrowserDialogClose(void) { return close_stub(); }
int sceUserServiceGetForegroundUser(int *user) { *user=42;return 0; }
int sceUserServiceGetInitialUser(int *user) { *user=42;return 0; }
}
TEST(ProviderDialogPlatform, StableAbiParametersAndAsynchronousCloseThenReopen) {
    reset();hui::ps5::ProviderDialog d;ASSERT_TRUE(d.initialize());
    ASSERT_TRUE(d.open("https://vikingfile.com/f/example"));
    EXPECT_FALSE(d.open("https://vikingfile.com/f/another"));
    const char *page;std::memcpy(&page,saved_param+64,sizeof page);
    EXPECT_STREQ(page,"https://vikingfile.com/f/example");
    EXPECT_EQ(d.status(),1);dialog_state=2;EXPECT_EQ(d.status(),2);
    EXPECT_EQ(d.close(),0);EXPECT_EQ(d.status(),2);EXPECT_EQ(close_count,1);
    dialog_state=3;EXPECT_EQ(d.status(),3);EXPECT_EQ(results,1);EXPECT_EQ(close_count,1);
    EXPECT_EQ(d.status(),0);EXPECT_EQ(d.close(),-1);
    EXPECT_TRUE(d.open("https://akirabox.to/example/file"));EXPECT_EQ(open_count,2);
    dialog_state=3;d.status();
}
TEST(ProviderDialogPlatform, FailedInitializationNeverOpensOrClosesAnything) {
    reset();module_rc=-5;hui::ps5::ProviderDialog d;EXPECT_FALSE(d.initialize());
    EXPECT_FALSE(d.open("https://vikingfile.com/f/example"));EXPECT_EQ(d.close(),-1);EXPECT_EQ(open_count,0);EXPECT_EQ(close_count,0);
}
TEST(ProviderDialogPlatform, ManualFinishReleasesTheOwnedDialogForReuse) {
    reset();hui::ps5::ProviderDialog d;ASSERT_TRUE(d.initialize());ASSERT_TRUE(d.open("https://vikingfile.com/f/example"));
    dialog_state=3;EXPECT_EQ(d.status(),3);EXPECT_EQ(close_count,1);
    EXPECT_TRUE(d.open("https://vikingfile.com/f/second"));dialog_state=-1;EXPECT_EQ(d.status(),-1);
    EXPECT_EQ(d.status(),0);EXPECT_EQ(close_count,2);
}
TEST(ProviderDialogPlatform, InitializesCommonBeforeBrowserAndOnlyOnce) {
    reset();hui::ps5::ProviderDialog d;ASSERT_TRUE(d.initialize());
    EXPECT_EQ(calls,(std::vector<std::string>{"module","common-init","browser-init"}));
    ASSERT_TRUE(d.initialize());EXPECT_EQ(calls.size(),3u);
}
TEST(ProviderDialogPlatform, AcceptsAnInitializedCommonDialog) {
    reset();common_init_rc=static_cast<int>(0x80b80002u);
    hui::ps5::ProviderDialog d;ASSERT_TRUE(d.initialize());
    ASSERT_TRUE(d.open("https://vikingfile.com/f/example"));dialog_state=3;d.status();
}
TEST(ProviderDialogPlatform, InitializationFailuresNeverOpenOrCloseADialog) {
    reset();common_init_rc=-1;hui::ps5::ProviderDialog common;
    EXPECT_FALSE(common.initialize());EXPECT_EQ(calls.size(),2u);
    reset();dialog_init_rc=-1;hui::ps5::ProviderDialog browser;
    EXPECT_FALSE(browser.initialize());
    EXPECT_FALSE(browser.open("https://vikingfile.com/f/example"));
    EXPECT_EQ(open_count,0);EXPECT_EQ(close_count,0);
}
TEST(ProviderDialogPlatform, ReportsNegativeResultAndClearsItOnTheNextOpen) {
    reset();hui::ps5::ProviderDialog d;ASSERT_TRUE(d.initialize());
    ASSERT_TRUE(d.open("https://vikingfile.com/f/example"));
    dialog_state=3;result_value=static_cast<int>(0x80000006u);
    EXPECT_EQ(d.status(),3);EXPECT_EQ(d.error_code(),result_value);
    ASSERT_TRUE(d.open("https://vikingfile.com/f/again"));EXPECT_EQ(d.error_code(),0);
    dialog_state=3;result_value=1;d.status();EXPECT_EQ(d.error_code(),0);
}
TEST(ProviderDialogPlatform, ReportsOpenStatusAndGetResultErrors) {
    reset();hui::ps5::ProviderDialog d;ASSERT_TRUE(d.initialize());
    open_rc=-8;EXPECT_FALSE(d.open("https://vikingfile.com/f/example"));EXPECT_EQ(d.error_code(),-8);
    open_rc=0;ASSERT_TRUE(d.open("https://vikingfile.com/f/example"));
    dialog_state=-9;EXPECT_EQ(d.status(),-9);EXPECT_EQ(d.error_code(),-9);
    ASSERT_TRUE(d.open("https://vikingfile.com/f/example"));
    dialog_state=3;result_rc=-10;d.status();EXPECT_EQ(d.error_code(),-10);
}
