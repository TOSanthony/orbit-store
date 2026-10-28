// Temporary native entry point. Exercises the existing supported dialog ABI.
// Never starts Orbit, changes its preferences, reads accounts or downloads files.
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/provider_dialog.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/system.hpp"
#include "orbit/http.hpp"
#include <GL/glcorearb.h>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <sys/stat.h>
#include "session_probe_config.hpp"

int main() {
    hui::ps5::Display display;
    if (!display.open(1920, 1080)) hui::sys::park();
    // Regular Orbit initializes the user service through its controller before
    // opening a dialog. The probe must follow the same initialization order.
    hui::ps5::Pad pad;
    const bool user_ready = pad.open();
    hui::ps5::ProviderDialog dialog;
    orbit::http::SocketTransport http(PROBE_HOST, PROBE_PORT, 1500);
    // Status reports run off the display thread. They contain phases and return
    // codes only, never page content or cookies.
    auto report = [&](const char *phase, int rc) {
        orbit::http::Request q;
        q.path = std::string("/") + PROBE_RUN + "/event/native?phase=" + phase + "&rc=" + std::to_string(rc);
        q.max_bytes = 64;
        http.send(q);
    };
    const bool ready = user_ready && dialog.initialize();
    report("initialize", ready ? 0 : -1);
    if (!ready) hui::sys::park();
    std::mutex report_mutex;
    std::deque<std::pair<int, int>> reports;
    auto emit = [&](int phase, int rc) {
        std::lock_guard<std::mutex> lock(report_mutex);
        reports.emplace_back(phase, rc);
    };
    std::thread reporter([&] {
        const char *names[] = {"", "open-seed", "open-check", "open-clear", "close", "finished"};
        for (;;) {
            std::pair<int, int> item{0, 0};
            {
                std::lock_guard<std::mutex> lock(report_mutex);
                if (!reports.empty()) { item = reports.front(); reports.pop_front(); }
            }
            if (item.first > 0 && item.first < 6) report(names[item.first], item.second);
            hui::sys::sleep_us(100000);
        }
    });
    reporter.detach();
    const char *steps[] = {"seed", "check", "clear"};
    int step = 0;
    bool opened = false, closing = false, finished = false, failed = false;
    bool first_frame = true;
    auto next = hui::sys::monotonic_us() + 2000000;
    std::int64_t deadline = 0;
    for (;;) {
        auto now = hui::sys::monotonic_us();
        if (!finished && !opened && now >= next) {
            const std::string url = std::string(PROBE_ORIGIN) + "/" + PROBE_RUN + "/" + steps[step] + "/native";
            bool ok = dialog.open(url);
            const int error = dialog.error_code() < 0 ? dialog.error_code() : -1;
            emit(step + 1, ok ? 0 : error);
            if (!ok) { finished = failed = true; emit(5, error); }
            else { opened = true; closing = false; deadline = now + 12000000; }
        }
        if (opened) {
            int status = dialog.status();
            if (status <= 0 || status == 3 || (status == 1 && now > deadline - 7000000)) {
                opened = false;
                if (!closing) { finished = failed = true; emit(5, -2); }
                else if (++step == 3) { finished = true; emit(5, 0); }
                else next = now + 2500000;
            } else if (!closing && status == 2 && now >= deadline) {
                int rc = dialog.close();
                emit(4, rc);
                closing = true;
            } else if (now > deadline + 15000000) {
                (void)dialog.close();
                opened = false; finished = failed = true; emit(5, -3);
            }
        }
        glClearColor(failed ? 0.35f : 0.02f, finished && !failed ? 0.2f : 0.04f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!display.swap()) hui::sys::park();
        if (first_frame) { (void)hui::sys::hide_splash_screen(); first_frame = false; }
    }
}
