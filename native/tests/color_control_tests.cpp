#include "color_control.hpp"
#include "settings.hpp"
#include <atomic>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <windows.h>
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        const auto path = std::filesystem::temp_directory_path() /
                          (L"og-color-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        auto set = [&](const wchar_t *key, const wchar_t *value) {
            WritePrivateProfileStringW(L"iracing", key, value, path.c_str());
        };
        auto defaults = og::read_config(path.wstring(), "iracing");
        check(!defaults.color.enabled && defaults.color.preset == og::ColorPreset::Neutral,
              "new profiles are disabled and neutral");
        set(L"ColorCorrectionEnabled", L"1");
        set(L"ColorCorrectionPreset", L"night");
        set(L"ColorNightGamma", L"999");
        set(L"ColorSunBrightness", L"NaN");
        set(L"ColorCorrectionShortcut", L"Ctrl+Alt+F6");
        set(L"HelmetVisorEnabled", L"1");
        set(L"HelmetVisorBrightness", L"60");
        auto config = og::read_config(path.wstring(), "iracing");
        check(config.color.enabled && config.color.preset == og::ColorPreset::Night,
              "independent color configuration loads");
        check(config.color.presets[2].gamma == 200 && config.color.presets[1].brightness == 75,
              "preset bounds clamp and invalid numbers use preset defaults");
        check(config.color.presets[2].brightness == 110 && config.color.presets[2].contrast == 95,
              "night starts brighter with preserved contrast");
        set(L"ColorCorrectionPreset", L"arbitrary");
        set(L"ColorCorrectionShortcut", L"Ctrl+Q");
        config = og::read_config(path.wstring(), "iracing");
        check(config.color.preset == og::ColorPreset::Neutral &&
                  config.color.shortcut == "Ctrl+Alt+F6",
              "invalid preset and shortcut fail to safe defaults");
        set(L"ColorCorrectionShortcut", L"");
        config = og::read_config(path.wstring(), "iracing");
        check(config.color.shortcut.empty(),
              "explicit unbound shortcut stays unbound instead of restoring the default key");
        set(L"ColorCorrectionEnabled", L"0");
        config = og::read_config(path.wstring(), "iracing");
        check(config.color.configured && !config.color.enabled,
              "an explicit independent OFF is distinguishable from a legacy-only INI");
        std::filesystem::remove(path);
        og::ColorConfig settings;
        settings.enabled = true;
        og::ColorRuntime runtime(settings);
        runtime.frame(1, true, false);
        check(!og::scene_color_active(runtime.frameColor()), "neutral frame is an exact bypass");
        runtime.frame(2, false, true);
        runtime.frame(3, true, true);
        check(runtime.preset() == og::ColorPreset::Neutral,
              "held shortcut does not fire on focus return");
        runtime.frame(4, true, false);
        runtime.frame(5, true, true);
        check(runtime.preset() == og::ColorPreset::Sun && runtime.frameColor().brightness == 75,
              "focused rising edge cycles live from neutral");
        runtime.cycle();
        runtime.frame(5, true, false);
        check(runtime.frameColor().brightness == 75,
              "both eyes retain one frame snapshot despite a midframe cycle");
        runtime.frame(6, true, false);
        check(runtime.frameColor().gamma == 135, "next frame adopts night gamma");
        runtime.cycle();
        runtime.cycle();
        check(runtime.preset() == og::ColorPreset::Neutral, "four presets form a bounded cycle");
        og::ColorShared data{};
        data.session = 17;
        data.focusEpoch = 4;
        data.focused = 1;
        data.enabled = 1;
        data.supported = 1;
        data.running = 1;
        data.heartbeat = 100;
        data.command = 1;
        data.commandSession = 16;
        data.commandEpoch = 4;
        data.expires = 500;
        check(!og::consume_color_command(data, 100) && data.ack == 1,
              "old-session commands are rejected and acknowledged");
        data.command = 2;
        data.commandSession = 17;
        data.commandEpoch = 3;
        check(!og::consume_color_command(data, 100), "old-focus commands never replay");
        data.command = 3;
        data.commandEpoch = 4;
        data.expires = 99;
        check(!og::consume_color_command(data, 100), "expired commands never replay");
        data.command = 4;
        data.expires = 500;
        data.focused = 0;
        check(!og::consume_color_command(data, 100), "unfocused sessions reject commands");
        data.command = 5;
        data.focused = 1;
        data.supported = 0;
        check(!og::consume_color_command(data, 100),
              "unsupported graphics rejects simulated effects");
        data.command = 6;
        data.supported = 1;
        check(og::consume_color_command(data, 100) && data.ack == 6 && data.accepted == 1,
              "fresh focused own-session command is accepted once");
        check(!og::consume_color_command(data, 100), "acknowledged cycle cannot replay");
        const auto channelName = "color-test-" + std::to_string(GetCurrentProcessId());
        og::ColorChannel observer;
        {
            og::ColorChannel owner, contender;
            check(owner.open(channelName, true, 1001), "isolated producer channel opens");
            check(!contender.open(channelName, true, 1002),
                  "another session cannot steal a producer during startup before first heartbeat");
            check(observer.open(channelName), "client opens existing isolated producer");
            og::ColorRuntime live(settings);
            owner.publish(live, true, true, true, true, false, GetTickCount64());
            check(observer.lock(), "client lock");
            auto &shared = *observer.data();
            shared.command = 1;
            shared.commandSession = 1001;
            shared.commandEpoch = shared.focusEpoch;
            shared.expires = GetTickCount64() + 500;
            observer.unlock();
            owner.publish(live, true, true, true, true, false, GetTickCount64());
            check(live.preset() == og::ColorPreset::Neutral,
                  "status publication cannot consume a command midframe");
            owner.publish(live, true, true, true, true, true, GetTickCount64(), true);
            check(observer.data()->effectActive == 0,
                  "newly acknowledged preset waits for a real GPU pass before reporting an effect");
            live.frame(1, true, false);
            check(live.preset() == og::ColorPreset::Sun && observer.data()->ack == 1,
                  "real shared channel acknowledges exactly one cycle at frame start");
            check(observer.lock(), "second command lock");
            shared.command = 2;
            shared.commandSession = 1001;
            shared.commandEpoch = shared.focusEpoch;
            shared.expires = GetTickCount64() + 500;
            observer.unlock();
            owner.publish(live, true, false, true, true, false, GetTickCount64());
            owner.publish(live, true, true, true, true, false, GetTickCount64(), true);
            check(live.preset() == og::ColorPreset::Sun && observer.data()->ack == 2 &&
                      !observer.data()->accepted,
                  "focus loss invalidates pending commands across a rapid focus return");
            check(observer.lock(), "contended focus command lock");
            shared.command = 3;
            shared.commandSession = 1001;
            shared.commandEpoch = shared.focusEpoch;
            shared.expires = GetTickCount64() + 500;
            observer.unlock();
            std::atomic<bool> held{false}, releaseMutex{false};
            std::thread holder([&] {
                if (observer.lock()) {
                    held = true;
                    while (!releaseMutex.load())
                        Sleep(1);
                    observer.unlock();
                }
            });
            while (!held.load())
                Sleep(1);
            owner.publish(live, true, false, true, true, false, GetTickCount64());
            releaseMutex = true;
            holder.join();
            owner.publish(live, true, true, true, true, false, GetTickCount64(), true);
            check(live.preset() == og::ColorPreset::Sun && observer.data()->ack == 3 &&
                      !observer.data()->accepted,
                  "focus loss invalidates pending commands even when another thread holds the "
                  "shared mutex during the focus edge");
        }
        check(!observer.data()->running,
              "producer teardown invalidates its session while clients still map the channel");
        og::ColorChannel replacement;
        check(replacement.open(channelName, true, 1003),
              "new session may take a relinquished channel without inheriting commands");
        check(observer.data()->command == 0 && observer.data()->ack == 0,
              "new session has no stale commands");
        // The production helper must verify its producer process as well as the
        // shared protocol. This fixture takes a lease only when no sim owns it.
        og::ColorChannel host;
        if (host.open("iracing", true, 2001)) {
            og::ColorRuntime live(settings);
            host.publish(live, true, true, true, true, false, GetTickCount64());
            auto status = og::color_control("iracing", false);
            check(status.available && status.supported && status.enabled && !status.effectActive,
                  "helper verifies and reports the recognized current producer");
            std::atomic<bool> stop{false}, service{true};
            std::thread render([&] {
                uint64_t frame = 1;
                while (!stop.load()) {
                    host.publish(live, true, true, true, true, false, GetTickCount64(),
                                 service.load());
                    live.frame(frame++, true, false);
                    Sleep(5);
                }
            });
            auto cycled = og::color_control("iracing", true);
            stop = true;
            render.join();
            check(cycled.error.empty() && cycled.activePreset == og::ColorPreset::Sun,
                  "helper returns the actually acknowledged running preset");
            host.publish(live, true, false, true, true, false, GetTickCount64());
            auto unfocused = og::color_control("iracing", true);
            check(!unfocused.error.empty() && live.preset() == og::ColorPreset::Sun,
                  "helper rejects unfocused session without queuing work");
            host.publish(live, true, true, true, true, false, GetTickCount64());
            auto timedOut = og::color_control("iracing", true);
            check(timedOut.error.find("timed out") != std::string::npos,
                  "helper fails within its acknowledgement deadline when render stops");
            host.publish(live, true, true, true, true, false, GetTickCount64(), true);
            check(live.preset() == og::ColorPreset::Sun,
                  "timed out helper command cannot replay when rendering resumes");
            host.publish(live, true, true, true, true, false, GetTickCount64() - 2000);
            auto stale = og::color_control("iracing", false);
            check(!stale.available,
                  "helper rejects a stale heartbeat despite a live producer process");
        } else {
            std::cout << "Existing sim owns iracing; live helper fixture skipped safely\n";
        }
        std::cout << "Color config, focus, stale command and per-frame tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
