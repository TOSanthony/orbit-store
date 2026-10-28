// Native provider sessions use the same authenticated service workflow.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include "orbit/api.hpp"
#include "orbit/store.hpp"
#include "support.hpp"

namespace orbit
{
using test::json_reply;
using test::ScriptedTransport;

TEST(ProviderBrowser, StartRequestsNativeReturnWithSelectedIdsAndTheSessionToken)
{
    ScriptedTransport transport;
    transport.reply=[](const http::Request &) {
        return json_reply(202,R"({"available":true,"active":true,"id":"s1","state":"waiting","title":"Test game","jobId":"","message":"Waiting"})");
    };
    api::Client client(transport);client.set_token("local-token");
    EXPECT_TRUE(client.start_browser("r1","m2").ok());
    EXPECT_EQ(transport.seen.back().body,R"({"action":"start","releaseId":"r1","storageId":"m2","returnTarget":"native"})");
    EXPECT_EQ(transport.seen.back().bearer,"local-token");
    EXPECT_TRUE(client.cancel_browser("s1").ok());
    EXPECT_EQ(transport.seen.back().body,R"({"action":"cancel","id":"s1"})");
}

TEST(ProviderBrowser, RejectsMalformedStatusAndPreservesRefusalReason)
{
    ScriptedTransport transport;api::Client client(transport);
    transport.reply=[](const http::Request &) { return json_reply(200,R"({"available":true})"); };
    EXPECT_FALSE(client.browser_session().ok());
    transport.reply=[](const http::Request &) { return json_reply(200,R"({"state":"waiting","active":true})"); };
    EXPECT_FALSE(client.browser_session().ok());
    transport.reply=[](const http::Request &) { return json_reply(409,R"({"error":"Finish or cancel the current verification first."})"); };
    EXPECT_EQ(client.start_browser("r","usb0").error,"Finish or cancel the current verification first.");
}

TEST(ProviderBrowser, AcceptsAkiraReadyLinkWaitAndAutomaticQueueCompletion)
{
    ScriptedTransport transport;
    api::Client client(transport);
    transport.reply=[](const http::Request &) {
        return json_reply(202,R"({"available":true,"active":true,"id":"akira1","state":"waiting-link","title":"Test game","message":"Complete verification on AkiraBox, then wait for Orbit to check the download link."})");
    };
    const auto waiting = client.start_browser("akira-release", "internal");
    ASSERT_TRUE(waiting.ok());
    EXPECT_EQ(waiting.value.state, "waiting-link");
    EXPECT_TRUE(waiting.value.active);
    EXPECT_EQ(waiting.value.message, "Complete verification on AkiraBox, then wait for Orbit to check the download link.");
    transport.reply=[](const http::Request &) {
        return json_reply(200,R"({"available":true,"active":false,"id":"akira1","state":"queued","title":"Test game","jobId":"download1","message":"Your verified file has been added to Downloads."})");
    };
    const auto queued = client.browser_session();
    ASSERT_TRUE(queued.ok());
    EXPECT_FALSE(queued.value.active);
    EXPECT_EQ(queued.value.job_id, "download1");
    EXPECT_EQ(transport.seen.size(), 2u);
    EXPECT_EQ(transport.seen.back().method, "GET");
}

TEST(ProviderBrowser, LostStartReplyIsReconciledWithoutReplayingOrQueueing)
{
    host::Fixture fixture(host::FixtureMode::normal);
    ScriptedTransport transport;
    int posts=0;
    transport.reply=[&](const http::Request &request) {
        auto response=fixture.send(request);
        if(request.path=="/api/v1/browser" && request.method=="POST") {
            ++posts;response={};response.error="connection interrupted";
        }
        return response;
    };
    Store store(transport);store.poll_once();store.tick();
    store.start_browser("ironclad-skies-viking","usb0");
    store.start_browser("ironclad-skies-viking","usb0"); // repeated X
    ASSERT_TRUE(store.run_next_action());store.tick();
    EXPECT_FALSE(store.run_next_action());
    EXPECT_TRUE(store.state().browser.active);
    EXPECT_EQ(store.browser_action().error,"connection interrupted");
    store.poll_once();store.tick();
    EXPECT_EQ(posts,1);
    EXPECT_EQ(store.state().jobs.size(),6u);
    EXPECT_FALSE(store.action().busy);
}

TEST(ProviderBrowser, OlderBackendAndReadFailureDisableNewStartsWithoutFakeCompletion)
{
    host::Fixture fixture(host::FixtureMode::normal);
    ScriptedTransport transport;
    transport.reply=[&](const http::Request &request) {
        return request.path=="/api/v1/browser" ? json_reply(404,R"({"error":"Not found"})") : fixture.send(request);
    };
    Store store(transport);store.poll_once();store.tick();
    EXPECT_FALSE(store.state().have_browser);
    EXPECT_FALSE(store.state().browser.available);
    EXPECT_EQ(store.state().link,Link::online);
    EXPECT_EQ(store.state().browser_error,"Update Orbit to open provider pages.");
}
} // namespace orbit

namespace orbit {
TEST(ProviderBrowser, NativeDialogContractCarriesOwnerAndAcknowledgesTheExactSession)
{
    test::ScriptedTransport transport;
    transport.reply=[](const http::Request &) {
        return test::json_reply(202,R"({"available":true,"nativeDialogAvailable":true,"active":true,"id":"s1","clientId":"owner","pageUrl":"https://vikingfile.com/f/example","dialogOpened":false,"state":"opening"})");
    };
    api::Client client(transport);
    const auto result=client.start_browser("r1","internal","owner");
    ASSERT_TRUE(result.ok());EXPECT_TRUE(result.value.native_dialog_available);
    EXPECT_EQ(result.value.client_id,"owner");EXPECT_EQ(result.value.page_url,"https://vikingfile.com/f/example");
    EXPECT_EQ(transport.seen.back().body,R"({"action":"start","releaseId":"r1","storageId":"internal","returnTarget":"native","nativeDialog":true,"clientId":"owner"})");
    EXPECT_TRUE(client.browser_opened("s1","owner").ok());
    EXPECT_EQ(transport.seen.back().body,R"({"action":"opened","id":"s1","clientId":"owner"})");
    EXPECT_TRUE(client.cancel_browser("s1","owner","browser",-6).ok());
    EXPECT_EQ(transport.seen.back().body,R"({"action":"cancel","id":"s1","clientId":"owner","nativeFailure":"browser","errorCode":-6})");
}
}

namespace orbit {
TEST(ProviderBrowser, StoreReconcilesLostStartThenClosesOnlyAfterCommittedJob)
{
    struct Window : ProviderWindow {
        int opens=0,closes=0,current=2;
        bool open(const std::string &url) override { EXPECT_EQ(url,"https://vikingfile.com/f/example");++opens;return true; }
        int close() override { ++closes;return 0; }
        int status() override { return current; }
    } window;
    host::Fixture fixture(host::FixtureMode::normal);
    test::ScriptedTransport transport;
    std::string client,state="idle";
    bool acknowledged=false;
    int starts=0,acks=0;
    transport.reply=[&](const http::Request &request) {
        if(request.path!="/api/v1/browser")return fixture.send(request);
        if(request.method=="POST") {
            json::Value input;EXPECT_TRUE(json::parse(request.body,&input));
            if(input.text("action")=="start") {
                ++starts;client=input.text("clientId");EXPECT_FALSE(client.empty());
                EXPECT_TRUE(input.flag("nativeDialog"));state="opening";
                http::Response lost;lost.error="connection interrupted";return lost;
            }
            EXPECT_EQ(input.text("action"),"opened");EXPECT_EQ(input.text("id"),"s1");
            EXPECT_EQ(input.text("clientId"),client);acknowledged=true;++acks;state="waiting-link";
        }
        const bool idle=state=="idle",queued=state=="queued";
        return test::json_reply(200,"{\"available\":true,\"nativeDialogAvailable\":true,\"active\":"+
            std::string(!idle&&!queued?"true":"false")+",\"id\":"+json::quote(idle?"":"s1")+
            ",\"state\":"+json::quote(state)+",\"clientId\":"+json::quote(client)+
            ",\"dialogOpened\":"+(acknowledged?"true":"false")+
            ",\"pageUrl\":"+json::quote(!idle&&!acknowledged?"https://vikingfile.com/f/example":"")+
            ",\"jobId\":"+json::quote(queued?"job":"")+"}");
    };
    Store store(transport);store.set_provider_window(&window);store.poll_once();store.tick();
    store.start_browser("ironclad-skies-viking","usb0");ASSERT_TRUE(store.run_next_action());store.tick();
    EXPECT_EQ(window.opens,1);EXPECT_TRUE(store.provider_dialog_visible());
    ASSERT_TRUE(store.run_next_action());store.tick();EXPECT_EQ(starts,1);EXPECT_EQ(acks,1);
    EXPECT_EQ(window.closes,0);EXPECT_FALSE(store.run_next_action());
    transport.seen.clear();store.poll_once();store.tick();
    ASSERT_EQ(transport.seen.size(),1u);
    EXPECT_EQ(transport.seen.front().path,"/api/v1/browser");
    transport.seen.clear();store.start();
    const double deadline=now_seconds()+1.5;
    while(transport.count("/api/v1/browser")<4 && now_seconds()<deadline) {
        store.tick();timespec pause{0,5000000};nanosleep(&pause,nullptr);
    }
    store.stop();store.tick();
    EXPECT_GE(transport.count("/api/v1/browser"),4);
    EXPECT_EQ(transport.count("/api/v1/system"),0);
    EXPECT_EQ(transport.count("/api/v1/storage"),0);
    state="queued";store.poll_once();store.tick();EXPECT_EQ(window.closes,1);
    EXPECT_TRUE(store.provider_dialog_visible());window.current=3;store.tick();
    EXPECT_FALSE(store.provider_dialog_visible());EXPECT_FALSE(store.run_next_action());
    transport.seen.clear();store.poll_once();store.tick();
    EXPECT_GT(transport.count("/api/v1/downloads"),0);
    EXPECT_GT(transport.count("/api/v1/system"),0);
}
TEST(ProviderBrowser, BrowserFailureRacingWithCommittedDownloadDoesNotLeaveAnError)
{
    struct Window : ProviderWindow {
        int current=2;
        bool open(const std::string &) override { return true; }
        int close() override { return 0; }
        int status() override { return current; }
        int error_code() const override { return current==3 ? -6 : 0; }
    } window;
    host::Fixture fixture(host::FixtureMode::normal);
    test::ScriptedTransport transport;
    std::string client,state="idle";
    bool acknowledged=false;
    int failures=0;
    transport.reply=[&](const http::Request &request) {
        if(request.path!="/api/v1/browser")return fixture.send(request);
        if(request.method=="POST") {
            json::Value input;EXPECT_TRUE(json::parse(request.body,&input));
            if(input.text("action")=="start") { client=input.text("clientId");state="opening"; }
            else if(input.text("action")=="opened") { acknowledged=true;state="waiting-link"; }
            else {
                ++failures;EXPECT_EQ(input.text("nativeFailure"),"browser");
                EXPECT_EQ(input.text("clientId"),client);EXPECT_EQ(input.text("id"),"s1");
                state="queued";
                return test::json_reply(409,R"({"error":"The file is already in Downloads. Manage it there."})");
            }
        }
        const bool idle=state=="idle",queued=state=="queued";
        return test::json_reply(200,"{\"available\":true,\"nativeDialogAvailable\":true,\"active\":"+
            std::string(!idle&&!queued?"true":"false")+",\"id\":"+json::quote(idle?"":"s1")+
            ",\"state\":"+json::quote(state)+",\"clientId\":"+json::quote(client)+
            ",\"dialogOpened\":"+(acknowledged?"true":"false")+
            ",\"pageUrl\":"+json::quote(!idle&&!acknowledged?"https://vikingfile.com/f/example":"")+
            ",\"jobId\":"+json::quote(queued?"job":"")+"}");
    };
    Store store(transport);store.set_provider_window(&window);store.poll_once();store.tick();
    store.start_browser("ironclad-skies-viking","usb0");ASSERT_TRUE(store.run_next_action());store.tick();
    ASSERT_TRUE(store.run_next_action());store.tick();
    window.current=3;store.tick();ASSERT_TRUE(store.run_next_action());store.tick();
    EXPECT_EQ(failures,1);EXPECT_EQ(store.state().browser.state,"queued");
    EXPECT_TRUE(store.browser_action().error.empty());EXPECT_FALSE(store.provider_dialog_visible());
}
}
