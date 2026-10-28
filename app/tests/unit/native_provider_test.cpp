// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/native_provider.hpp"
#include <gtest/gtest.h>

namespace orbit { namespace {
struct Window : ProviderWindow {
    int opens=0, closes=0, state=2, close_rc=0, failure_code=0;
    bool accepted=true;
    std::string url;
    bool open(const std::string &page) override { ++opens;url=page;return accepted; }
    int status() override { return state; }
    int close() override { ++closes;return close_rc; }
    int error_code() const override { return failure_code; }
};
BrowserSession prepared() {
    BrowserSession s;s.id="session";s.client_id="owner";s.state="opening";s.active=true;
    s.page_url="https://vikingfile.com/f/example";return s;
}
TEST(NativeProvider, VerifiedSessionDownloadQueuesWithoutOpeningAnyWindow) {
    Window w; NativeProvider p; p.set_window(&w); p.begin("owner",0);
    auto s=prepared(); s.state="validating"; s.page_url.clear();
    EXPECT_EQ(p.tick(s,true,1).kind,NativeProvider::Event::none);
    EXPECT_EQ(w.opens,0); EXPECT_TRUE(p.busy());
    s.state="queued"; s.job_id="job"; s.active=false;
    EXPECT_EQ(p.tick(s,true,2).kind,NativeProvider::Event::none);
    EXPECT_EQ(w.opens,0); EXPECT_EQ(w.closes,0); EXPECT_FALSE(p.busy());
}
TEST(NativeProvider, AccountCheckEndsWithoutOpeningOrClosingAWindow) {
    Window w; NativeProvider p; p.set_window(&w); p.begin("owner",0);
    auto s=prepared(); s.account_page=true; s.state="validating"; s.page_url.clear();
    p.tick(s,true,1); EXPECT_TRUE(p.busy()); EXPECT_EQ(w.opens,0);
    s.active=false; s.state="idle"; s.viking_state="signed-in";
    p.tick(s,true,2); EXPECT_FALSE(p.busy()); EXPECT_EQ(w.opens,0); EXPECT_EQ(w.closes,0);
}
TEST(NativeProvider, AccountPageWaitsForUserAndNeverPretendsADownloadWasQueued) {
    Window w; NativeProvider p; p.set_window(&w); p.begin("owner",0);
    auto s=prepared(); s.account_page=true; s.page_url="https://vikingfile.com/login";
    EXPECT_EQ(p.tick(s,true,1).kind,NativeProvider::Event::opened);
    EXPECT_EQ(w.url,"https://vikingfile.com/login");
    s.dialog_opened=true; s.state="waiting"; p.tick(s,true,30);
    EXPECT_EQ(w.closes,0);
    w.state=3; auto event=p.tick(s,true,31);
    EXPECT_EQ(event.kind,NativeProvider::Event::cancel); EXPECT_TRUE(event.error.empty());
    EXPECT_FALSE(p.busy()); EXPECT_TRUE(s.job_id.empty());
}
TEST(NativeProvider, AccountExpiryClosesOnlyItsOwnedWindow) {
    Window w; NativeProvider p; p.set_window(&w); p.begin("owner",0);
    auto s=prepared(); s.account_page=true; s.page_url="https://vikingfile.com/login";
    p.tick(s,true,1);s.state="idle";s.active=false;
    EXPECT_EQ(p.tick(s,true,2).kind,NativeProvider::Event::none);EXPECT_EQ(w.closes,1);
}
TEST(NativeProvider, OpensOnlyItsOwnExplicitRequestAndAcknowledgesOnce) {
    Window w;NativeProvider p;p.set_window(&w);auto s=prepared();
    EXPECT_EQ(p.tick(s,true,0).kind,NativeProvider::Event::none);
    p.begin("owner",1);s.client_id="other";p.tick(s,true,2);EXPECT_EQ(w.opens,0);
    s.client_id="owner";auto event=p.tick(s,true,3);
    EXPECT_EQ(event.kind,NativeProvider::Event::opened);EXPECT_EQ(event.id,"session");
    EXPECT_EQ(event.client_id,"owner");EXPECT_EQ(w.opens,1);EXPECT_TRUE(p.visible());
    p.tick(s,true,4);EXPECT_EQ(w.opens,1);EXPECT_EQ(w.closes,0);
}
TEST(NativeProvider, ClosesAfterCommittedQueueAndWaitsForFinished) {
    Window w;NativeProvider p;p.set_window(&w);p.begin("owner",0);auto s=prepared();p.tick(s,true,1);
    s.state="validating";s.dialog_opened=true;s.page_url.clear();p.tick(s,true,2);EXPECT_EQ(w.closes,0);
    s.state="queued";p.tick(s,true,3);EXPECT_EQ(w.closes,0); // no committed job yet
    s.job_id="job";p.tick(s,true,4);EXPECT_EQ(w.closes,1);EXPECT_TRUE(p.visible());
    p.tick(s,true,5);EXPECT_EQ(w.closes,1);
    w.state=3;EXPECT_EQ(p.tick(s,true,6).kind,NativeProvider::Event::none);EXPECT_FALSE(p.busy());
}
TEST(NativeProvider, ManualBackCancelsVerificationAndNeverReopens) {
    Window w;NativeProvider p;p.set_window(&w);p.begin("owner",0);auto s=prepared();p.tick(s,true,1);
    w.state=3;auto event=p.tick(s,true,2);EXPECT_EQ(event.kind,NativeProvider::Event::cancel);EXPECT_EQ(event.id,"session");
    EXPECT_TRUE(event.failure.empty());EXPECT_TRUE(event.error.empty());
    p.tick(s,true,3);EXPECT_EQ(w.opens,1);EXPECT_FALSE(p.busy());
}
TEST(NativeProvider, BackAfterQueueNeverCancelsDownload) {
    Window w;NativeProvider p;p.set_window(&w);p.begin("owner",0);auto s=prepared();p.tick(s,true,1);
    s.state="queued";s.job_id="job";w.state=3;
    EXPECT_EQ(p.tick(s,true,2).kind,NativeProvider::Event::none);EXPECT_FALSE(p.busy());
}
TEST(NativeProvider, FailedValidationClosesOwnedWindow) {
    Window w;NativeProvider p;p.set_window(&w);p.begin("owner",0);auto s=prepared();p.tick(s,true,1);
    s.state="failed";s.active=false;EXPECT_EQ(p.tick(s,true,2).kind,NativeProvider::Event::none);
    EXPECT_EQ(w.closes,1);EXPECT_TRUE(p.visible());
}
TEST(NativeProvider, FailedOpenCancelsOnlyItsSession) {
    Window w;w.accepted=false;NativeProvider p;p.set_window(&w);p.begin("owner",0);
    auto event=p.tick(prepared(),true,1);EXPECT_EQ(event.kind,NativeProvider::Event::cancel);
    EXPECT_FALSE(event.error.empty());EXPECT_FALSE(p.busy());EXPECT_EQ(w.closes,0);
}
TEST(NativeProvider, OldBackendOrNonHttpsPageCannotOpenADialog) {
    Window w;NativeProvider p;p.set_window(&w);p.begin("owner",0);auto s=prepared();s.client_id.clear();
    p.tick(s,true,1);EXPECT_EQ(w.opens,0);p.tick(s,true,36);EXPECT_FALSE(p.busy());
    p.begin("owner",40);s=prepared();s.page_url="http://127.0.0.1/action";
    EXPECT_EQ(p.tick(s,true,41).kind,NativeProvider::Event::cancel);EXPECT_EQ(w.opens,0);
}
TEST(NativeProvider, ConnectionLossClosesAndRequestsCancellationOnce) {
    Window w;NativeProvider p;p.set_window(&w);p.begin("owner",0);auto s=prepared();p.tick(s,true,1);
    p.tick(s,false,10);EXPECT_EQ(w.closes,0);
    auto event=p.tick(s,false,14);EXPECT_EQ(event.kind,NativeProvider::Event::cancel);
    EXPECT_EQ(event.failure,"connection");
    EXPECT_FALSE(event.error.empty());EXPECT_EQ(w.closes,1);
    EXPECT_EQ(p.tick(s,false,15).kind,NativeProvider::Event::none);
}
TEST(NativeProvider, NewSessionNeverGetsCancelledByOldDialog) {
    Window w;NativeProvider p;p.set_window(&w);p.begin("owner",0);auto s=prepared();p.tick(s,true,1);
    s.client_id="other";s.id="other-session";auto event=p.tick(s,true,2);
    EXPECT_EQ(event.kind,NativeProvider::Event::cancel);EXPECT_EQ(event.id,"session");EXPECT_EQ(w.closes,1);
}
TEST(NativeProvider, CloseFailureRetriesWithoutClaimingWindowFinished) {
    Window w;w.close_rc=-1;NativeProvider p;p.set_window(&w);p.begin("owner",0);auto s=prepared();p.tick(s,true,1);
    s.state="queued";s.job_id="job";p.tick(s,true,2);p.tick(s,true,3);EXPECT_EQ(w.closes,1);
    p.tick(s,true,4);p.tick(s,true,6);p.tick(s,true,8);EXPECT_EQ(w.closes,3);EXPECT_TRUE(p.visible());
    w.state=3;p.tick(s,true,9);EXPECT_FALSE(p.visible());
}
TEST(NativeProvider, InitializationMayBeAsynchronousButCannotHangForever) {
    Window w;w.state=1;NativeProvider p;p.set_window(&w);p.begin("owner",0);auto s=prepared();p.tick(s,true,1);
    p.tick(s,true,2);EXPECT_TRUE(p.visible());
    EXPECT_EQ(p.tick(s,true,7).kind,NativeProvider::Event::cancel);EXPECT_FALSE(p.busy());
}
TEST(NativeProvider, NewDialogDoesNotInheritPreviousCloseRetryDelay) {
    Window w;NativeProvider p;p.set_window(&w);auto s=prepared();
    p.begin("owner",0);p.tick(s,true,1);
    s.state="queued";s.job_id="job";p.tick(s,true,2);EXPECT_EQ(w.closes,1);
    w.state=3;p.tick(s,true,2.1);EXPECT_FALSE(p.busy());
    w.state=2;s=prepared();p.begin("owner",2.2);p.tick(s,true,2.3);
    s.state="queued";s.job_id="next-job";p.tick(s,true,2.4);
    EXPECT_EQ(w.closes,2); // do not wait until the old session's retry at t=4
}
TEST(NativeProvider, FinishedBrowserErrorIsNotAManualCancellationAndCanRetry) {
    Window w;NativeProvider p;p.set_window(&w);p.begin("owner",0);auto s=prepared();p.tick(s,true,1);
    w.state=3;w.failure_code=static_cast<int>(0x80000006u);
    const auto event=p.tick(s,true,2);
    EXPECT_EQ(event.failure,"browser");EXPECT_EQ(event.error_code,w.failure_code);
    EXPECT_NE(event.error.find("0x80000006"),std::string::npos);EXPECT_FALSE(p.busy());
    w.state=2;w.failure_code=0;p.begin("owner",3);s.id="new-session";
    EXPECT_EQ(p.tick(s,true,4).kind,NativeProvider::Event::opened);
    w.state=3;EXPECT_TRUE(p.tick(s,true,5).failure.empty());
}
TEST(NativeProvider, BrowserFailureAfterQueueDoesNotCancelTheJob) {
    Window w;NativeProvider p;p.set_window(&w);p.begin("owner",0);auto s=prepared();p.tick(s,true,1);
    w.state=3;w.failure_code=-6;s.state="queued";s.job_id="job";
    EXPECT_EQ(p.tick(s,true,2).kind,NativeProvider::Event::none);
}
TEST(NativeProvider, VerificationTimeoutWithFreshStatusIsNotConnectionLoss) {
    Window w;NativeProvider p;p.set_window(&w);p.begin("owner",0);auto s=prepared();p.tick(s,true,1);
    auto event=p.tick(s,true,241);
    EXPECT_EQ(event.failure,"timeout");EXPECT_EQ(w.closes,1);
    EXPECT_NE(event.error.find("Verification timed out"),std::string::npos);
    EXPECT_EQ(event.error.find("Connection"),std::string::npos);
    EXPECT_EQ(p.tick(s,true,242).kind,NativeProvider::Event::none);
}
TEST(NativeProvider, FailedOpenPreservesPlatformCodeAndOwner) {
    Window w;w.accepted=false;w.failure_code=-7;NativeProvider p;p.set_window(&w);p.begin("owner",0);
    const auto event=p.tick(prepared(),true,1);
    EXPECT_EQ(event.failure,"open");EXPECT_EQ(event.error_code,-7);EXPECT_EQ(event.client_id,"owner");
}
} } // namespace orbit
