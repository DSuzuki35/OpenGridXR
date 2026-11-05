#include <windows.h>
static unsigned injectedEvents{};
UINT WINAPI testSendInput(UINT count, LPINPUT, int) { injectedEvents += count; return count; }
#define SendInput testSendInput
#define main overlay_host_entry_for_test
#include "../src/overlay_host.cpp"
#undef main
#undef SendInput
#include <sstream>
#include <stdexcept>
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
int main() {
    std::ostringstream output;
    auto *original = std::cout.rdbuf(output.rdbuf());
    try {
        Host host;
        host.testNamespace = true;
        check(host.channel.open("test") && host.channel.lock(1000), "open isolated test mapping");
        *host.channel.data() = {};
        host.channel.unlock();
        auto command = [&](const std::string &json) {
            og::OverlayCommand c; std::string error;
            check(og::parse_overlay_command(json, c, error), "parse fixture command");
            return c;
        };
        const std::string token = "12345678-abcd-1234-abcd-123456789012";
        auto configure = command("{\"op\":\"configure\",\"slot\":0,\"enabled\":true,\"anchor\":\"local\",\"desktopMode\":2,\"desktopWidth\":1024,\"desktopHeight\":768,\"uiToken\":\"" + token + "\"}");
        host.command(configure);
        check(host.channel.data()->slots[0].desktopMode == 2, "host configures internal UI mode");
        check(!host.syntheticInputActive, "menu never activates physical mouse hook");
        auto publish = [&] {
            check(host.channel.lock(1000), "publish target lock");
            auto &d = *host.channel.data(); auto &s = d.slots[0];
            d.consumerPid = GetCurrentProcessId(); d.consumerBeat = GetTickCount64();
            d.session = 9007199254740993ULL; d.inputFocused = 1;
            s.frames[0].generation = 9007199254740995ULL; s.current = 0;
            s.pointer = {1, 1, GetTickCount64(), d.session, s.revision, s.sourceRevision,
                         s.frames[0].generation, s.recenter, .25f, .75f, 1, d.focusEpoch};
            host.channel.unlock();
        };
        auto click = [&] {
            const auto &d = *host.channel.data();
            auto c = command("{\"op\":\"click\",\"slot\":0,\"button\":\"left\",\"uiToken\":\"" + token +
                "\",\"session\":\"" + std::to_string(d.session) + "\",\"focusEpoch\":\"" + std::to_string(d.focusEpoch) +
                "\",\"configRevision\":\"" + std::to_string(d.slots[0].revision) +
                "\",\"sourceRevision\":\"" + std::to_string(d.slots[0].sourceRevision) +
                "\",\"recenter\":\"" + std::to_string(d.slots[0].recenter) + "\"}");
            host.captureClick(c);
            return c;
        };
        auto clearOutput = [&] { output.str(""); output.clear(); };
        publish();
        host.updateInput(*host.channel.data());
        check(output.str().find("\"event\":\"move\"") != std::string::npos, "fresh menu emits move");
        check(output.str().find("\"session\":\"9007199254740993\"") != std::string::npos, "64 bit identity is exact decimal string");
        clearOutput();
        host.updateInput(*host.channel.data());
        check(output.str().empty(), "unchanged hover does not repeat");
        host.uiHover[0].sent = GetTickCount64();
        host.channel.data()->slots[0].pointer.u = .3f;
        host.updateInput(*host.channel.data());
        check(output.str().empty(), "changed hover is bounded to 33ms cadence");
        host.uiHover[0].sent -= 33;
        host.updateInput(*host.channel.data());
        check(output.str().find("\"event\":\"move\"") != std::string::npos, "latest changed hover is emitted after cadence");
        clearOutput();
        host.ownership[0].resume(0);
        physicalMouseEpoch = 1;
        auto c = click(); host.command(c);
        check(output.str().find("\"event\":\"click\"") != std::string::npos, "valid source bound click ignores physical mouse ownership and emits local event");
        clearOutput();
        c = click(); c.values["uiToken"] = "87654321-abcd-1234-abcd-123456789012"; host.command(c);
        check(output.str().empty(), "wrong token rejects click");
        c = click(); c.values["button"] = "right"; host.command(c);
        check(output.str().empty(), "menu has no right click");
        c = click(); c.values["session"] = "42"; host.captureClick(c); host.command(c);
        check(output.str().empty(), "pre-receipt select from old session cannot use current pointer");
        c = click(); c.values["focusEpoch"] = "42"; host.captureClick(c); host.command(c);
        check(output.str().empty(), "pre-receipt select from old focus cannot use current pointer");
        c = click(); ++host.channel.data()->slots[0].recenter; publish(); host.captureClick(c); host.command(c);
        check(output.str().empty(), "pre-receipt select from old recenter cannot use current pointer");
        c = click(); ++host.channel.data()->slots[0].revision; publish(); host.captureClick(c); host.command(c);
        check(output.str().empty(), "pre-receipt select from old configuration cannot use current pointer");
        c = click(); ++host.channel.data()->slots[0].sourceRevision; publish(); host.captureClick(c); host.command(c);
        check(output.str().empty(), "pre-receipt select from old source cannot use current pointer");
        c = click();
        ++host.channel.data()->slots[0].frames[0].generation;
        ++host.channel.data()->slots[0].pointer.sourceGeneration;
        host.captureClick(c); host.command(c);
        check(output.str().find("\"event\":\"click\"") != std::string::npos,
              "ordinary buffer flip before receipt preserves prepared select context");
        clearOutput(); publish();
        c = click();
        outputMutex.lock();
        std::thread delayedClick([&] { host.command(c); });
        bool acquired = false;
        for (unsigned i = 0; i < 200 && !acquired; ++i) {
            if (!host.channel.lock()) acquired = true;
            else host.channel.unlock();
            Sleep(1);
        }
        InterlockedExchange(reinterpret_cast<volatile LONG *>(&host.channel.data()->inputFocused), 0);
        outputMutex.unlock();
        delayedClick.join();
        check(acquired && output.str().empty(), "focus loss while output is queued rejects local click");
        publish();
        c = click(); c.receivedTick -= 201; host.command(c);
        check(output.str().empty(), "queued click expires");
        c = click(); ++host.channel.data()->focusEpoch; publish(); host.command(c);
        check(output.str().empty(), "queued pre-focus click cannot affect a new focused target");
        c = click(); ++host.channel.data()->slots[0].recenter; publish(); host.command(c);
        check(output.str().empty(), "recenter rejects queued click");
        c = click(); ++host.channel.data()->slots[0].sourceRevision; publish(); host.command(c);
        check(output.str().empty(), "source replacement rejects queued click");
        c = click(); ++host.channel.data()->slots[0].revision; publish(); host.command(c);
        check(output.str().empty(), "configure revision rejects queued click");
        c = click(); ++host.channel.data()->session; host.command(c);
        check(output.str().empty(), "session replacement rejects queued click");
        publish(); c = click(); ++host.channel.data()->slots[0].frames[0].generation; host.command(c);
        check(output.str().empty(), "texture generation replacement rejects queued click");
        publish(); host.channel.data()->inputFocused = 0;
        host.updateInput(*host.channel.data());
        check(output.str().find("\"event\":\"leave\"") != std::string::npos, "focus loss clears cursor");
        clearOutput(); host.updateInput(*host.channel.data());
        check(output.str().empty(), "leave emitted only once");
        publish(); c = click(); host.channel.data()->slots[0].pointer.tick -= 201;
        host.command(c); check(output.str().empty(), "stale rendered target rejects click");
        publish();
        host.testNamespace = false; host.mouseHookAvailable = true;
        check(!host.inject(*host.channel.data(), 0, "left"), "internal mode cannot reach OS injection even with mouse monitor available");
        host.channel.data()->slots[1] = host.channel.data()->slots[0];
        host.channel.data()->slots[0].desktopMode = 1;
        host.ownership[0].resume(physicalMouseEpoch.load());
        check(!host.inject(*host.channel.data(), 0, "left"), "top menu blocks OS click to lower desktop");
        check(!host.inject(*host.channel.data(), 0), "top menu blocks OS cursor movement to lower desktop");
        check(!host.inject(*host.channel.data(), 1, "left") && injectedEvents == 0, "mixed source menu never calls SendInput");
        host.channel.data()->slots[0].desktopMode = 2;
        host.testNamespace = true;
        host.channel.data()->slots[1] = host.channel.data()->slots[0];
        host.channel.data()->slots[1].desktopMode = 1;
        c = click(); host.command(c);
        check(output.str().empty(), "top desktop occludes menu in mixed source set");
        host.channel.data()->slots[1].enabled = 0;
        c = click(); host.command(command("{\"op\":\"clear\",\"slot\":0}"));
        host.command(c); check(output.str().empty(), "clear invalidates queued click");
        check(!host.syntheticInputActive && !host.internalUiActive, "hidden menu stops all pointer polling and does not hook OS input");
        std::cout.rdbuf(original);
        std::cout << "Internal UI native authorization, event and OS isolation tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cout.rdbuf(original); std::cerr << e.what() << '\n'; return 1;
    }
}
