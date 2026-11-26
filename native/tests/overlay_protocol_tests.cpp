#include "overlay_protocol.hpp"
#include <vector>
#include <thread>
#include <atomic>
#include <array>
#include "overlay_transport.hpp"
#include "../wheels/wheel_resources.hpp"
#include <aclapi.h>
#include <iostream>
#include <stdexcept>
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
void wheelResourceLifetimes() {
    using namespace std::chrono_literals;
    ovrlay::WheelMeshCache<int> cache;
    std::promise<std::shared_ptr<int>> loaded;
    unsigned loads=0;
    auto start=[&]{++loads;return loaded.get_future();};
    check(!cache.select(0,L"same",start) && !cache.select(1,L"same",start),"two slots join the pending model");
    check(!cache.hasAssets(),"pending model does not allocate driver render targets");
    cache.forget(0);
    loaded.set_value(std::make_shared<int>(7));
    const auto* first=cache.select(1,L"same",start);
    const auto* second=cache.select(0,L"same",start);
    check(first && first==second && *first==7 && loads==1,"pending load survives its first slot and shares one asset");
    cache.forget(0);
    check(cache.hasAssets(),"shared driver targets survive while another slot needs them");
    cache.forget(1);
    check(!cache.hasAssets(),"last driver release permits reclaiming its render targets");

    using Cache=ovrlay::WheelMeshCache<int>;
    ovrlay::WheelResources<Cache> work;
    std::promise<void> allowCompile;
    work.pipeline=std::async(std::launch::async,[&]{allowCompile.get_future().wait();return std::make_unique<Cache>();});
    auto discard=std::async(std::launch::async,[&]{work.release();});
    auto returned=discard.wait_for(500ms)==std::future_status::ready;
    allowCompile.set_value();
    discard.get();
    check(returned && work.retiring,"in-flight shader compile is retired without waiting on the XR thread");
    if(work.pipeline.valid())work.pipeline.wait();
    work.release();
    check(!work.retiring && !work.pipeline.valid(),"finished shader worker is reclaimed");
    work.renderer=std::make_unique<Cache>();
    std::promise<void> allowRead;
    work.renderer->select(0,L"wheel",[&]{return std::async(std::launch::async,[&]{allowRead.get_future().wait();return std::make_shared<int>(1);});});
    discard=std::async(std::launch::async,[&]{work.release();});
    returned=discard.wait_for(500ms)==std::future_status::ready;
    if(!returned) allowRead.set_value();
    discard.get();
    check(returned && work.retiring && work.renderer,"hot-disable keeps ownership of a running model worker");
    std::promise<void> entered;
    auto ended=std::async(std::launch::async,[&]{entered.set_value();work.release(true);});
    entered.get_future().wait();
    bool joined=ended.wait_for(15ms)==std::future_status::timeout;
    allowRead.set_value();ended.get();
    check(joined && !work.renderer,"session teardown joins model work before DLL unload");
}
int main() {
    try {
        wheelResourceLifetimes();
        og::OverlayCommand c;
        std::string error;
        std::uint64_t handle{};
        check(
            og::parse_overlay_command(
                R"({"op":"frame","slot":14,"frameId":42,"handle":"18446744073709551615","width":1024,"height":576,"format":"bgra"})",
                c, error),
            "bounded frame parses");
        check(og::overlay_handle(c.text("handle"), handle) && handle == UINT64_MAX,
              "64-bit handles retain precision");
        for (auto s : {R"({"op":"frame","slot":NaN})", R"({"op":"frame","op":"clear"})",
                       R"({"op":"frame",})", R"({"op":"frame","bad":{}})", R"({"op":"frame"}x)"})
            check(!og::parse_overlay_command(s, c, error), "invalid command rejected");
        check(!og::parse_overlay_command(std::string(4097, 'x'), c, error), "oversize rejected");
        check(og::parse_overlay_command(R"({"op":"click","slot":0,"button":"left"})", c, error),
              "desktop click operation parses");
        check(!og::parse_overlay_command(R"({"op":"configure","slot":0,"desktopMode":2,"enabled":true})", c, error),
              "internal UI configure requires a UUID token");
        check(!og::parse_overlay_command(R"({"op":"click","slot":0,"button":"left","uiToken":"arbitrary"})", c, error),
              "malformed UI token rejected");
        check(og::parse_overlay_command(R"({"op":"configure","slot":0,"desktopMode":2,"enabled":true,"uiToken":"12345678-abcd-1234-abcd-123456789012"})", c, error),
              "internal UI UUID configure parses");
        check(!og::parse_overlay_command(R"({"op":"click","slot":0,"button":"left","uiToken":"12345678-abcd-1234-abcd-123456789012"})", c, error),
              "internal UI click requires originating focus and session");
        check(!og::parse_overlay_command(R"({"op":"click","slot":0,"button":"left","uiToken":"12345678-abcd-1234-abcd-123456789012","session":"9007199254740993","focusEpoch":"0"})", c, error),
              "internal UI click requires originating configuration, source and recenter");
        check(og::parse_overlay_command(R"({"op":"click","slot":0,"button":"left","uiToken":"12345678-abcd-1234-abcd-123456789012","session":"9007199254740993","focusEpoch":"0","configRevision":"2","sourceRevision":"3","recenter":"4"})", c, error),
              "internal UI click keeps exact decimal session and focus epoch");
        for (auto identity : {"9007199254740993", "\"1.0\"", "\"-1\"", "\"01\"", "\"18446744073709551616\""}) {
            auto json = std::string(R"({"op":"click","slot":0,"button":"left","uiToken":"12345678-abcd-1234-abcd-123456789012","session":)") + identity + R"(,"focusEpoch":"1","configRevision":"2","sourceRevision":"3","recenter":"4"})";
            check(!og::parse_overlay_command(json, c, error), "UI click identity must be exact unsigned decimal string");
        }
        check(og::parse_overlay_command(R"({"op":"resume","slot":0})", c, error),
              "explicit pointer resume parses");
        check(og::parse_overlay_command(R"({"op":"placementPreview","slot":14})", c, error),
              "last dashboard slot accepts a preview");
        std::string lights=R"({"op":"wheelLights","slot":0,"wheelModel":"hyper-p1","wheelDevice":1,"rgb":")";
        check(og::parse_overlay_command(lights+std::string(88*6,'f')+R"("})",c,error),
              "complete physical LED array accepted");
        for(const auto& value:{std::string(87*6,'a'),std::string(88*6,'g'),std::string(97*6,'0')})
            check(!og::parse_overlay_command(lights+value+R"("})",c,error),"invalid LED payload rejected");
        const std::string arturaLights=R"({"op":"wheelLights","slot":0,"wheelModel":"ascher-artura","wheelDevice":6,"rgb":")";
        check(og::parse_overlay_command(arturaLights+std::string(63*6,'a')+R"("})",c,error),
              "Artura accepts all 63 physical mapper channels including unused slots");
        for(unsigned count:{35u,62u,64u})
            check(!og::parse_overlay_command(arturaLights+std::string(count*6,'a')+R"("})",c,error),
                  "Artura rejects logical-only and incomplete physical arrays");
        check(!og::parse_overlay_command(R"({"op":"configure","slot":0,"wheelModel":"ascher-artura","wheelDevice":7})",c,error),
              "unknown wheel device remains rejected");
        check(!og::parse_overlay_command(R"({"op":"configure","slot":0,"wheelModel":"../bad","wheelDevice":1})",c,error),
              "model key cannot escape the cache");
        for (const auto value : {"-1", "100.01", "\"50\"", "true"})
            check(!og::parse_overlay_command(std::string(R"({"op":"configure","slot":0,"wheelModel":"hyper-p1","wheelDevice":1,"wheelAmbientLight":)") + value + "}", c, error),
                  "wheel lighting requires a percentage between zero and one hundred");
        check(og::parse_overlay_command(R"({"op":"recenterShortcut","shortcut":"Ctrl+F7"})", c, error) &&
              og::parse_overlay_command(R"({"op":"recenterShortcut","shortcut":"Ctrl+R"})", c, error) &&
              og::parse_overlay_command(R"({"op":"recenterShortcut","shortcut":"Space"})", c, error) &&
              og::parse_overlay_command(R"({"op":"recenterShortcut","shortcut":""})", c, error),
              "passive recenter shortcut can be configured and cleared");
        for (auto invalid : {R"({"op":"recenterShortcut"})", R"({"op":"recenterShortcut","shortcut":3})",
                             R"({"op":"recenterShortcut","shortcut":"Ctrl+Missing"})"})
            check(!og::parse_overlay_command(invalid, c, error), "invalid recenter shortcut rejected");
        for (const auto mode : {"start", "freeze", "cancel", "reset"})
            check(og::parse_overlay_command(std::string(R"({"op":"place","slot":0,"mode":")") + mode + "\"}", c, error),
                  "dashboard placement lifecycle parses");
        for (const auto line : {R"({"op":"place","slot":0,"mode":"unknown"})", R"({"op":"place","slot":0})",
                 R"({"op":"placementPreview","slot":"0"})", R"({"op":"placementPreview","slot":15})",
                 R"({"op":"place","slot":0.5,"mode":"start"})", R"({"op":"place","slot":0,"mode":"reset","requestId":"42"})",
                 R"({"op":"place","slot":0,"mode":"reset","requestId":-1})"})
            check(!og::parse_overlay_command(line, c, error), "invalid dashboard gesture rejected");
        check(og::parse_overlay_command(
            R"({"op":"configure","slot":4,"enabled":true,"anchor":"local","x":0,"y":-0.3,"z":-0.6,"yaw":0,"pitch":0,"roll":0,"widthMeters":0.4,"opacity":1,"sourceKind":"url","cockpitOcclusion":true,"steeringFollow":true,"pivotX":0,"pivotY":-0.12,"pivotZ":0,"steeringTilt":8,"showInStream":false,"desktopMode":0,"desktopX":0,"desktopY":0,"desktopWidth":0,"desktopHeight":0,"precisionScale":1.75,"precisionShortcut":"","wheelModel":"ascher-artura","wheelDevice":6,"wheelLights":true,"wheelScreen":true,"wheelAmbientLight":72.5,"driverEnabled":true,"driverGloveTheme":0,"driverSuitTheme":4,"driverTorso":false,"driverLegs":true,"driverSeatHeight":-0.1,"driverSeatDistance":0.04,"driverShoulderWidth":1.1,"driverTorsoLean":10,"driverLegLength":0.85,"driverKneeHeight":0.02})",
            c,error) && c.number("wheelAmbientLight", -1) == 72.5 && c.boolean("driverEnabled") &&
            c.number("driverSuitTheme", -1) == 4 && !c.boolean("driverTorso") && c.boolean("driverLegs"),
              "complete wheel and driver configuration fits the IPC key bound");
        const std::string driverConfig = R"({"op":"configure","sourceKind":"url","wheelModel":"fpe2","wheelDevice":5,)";
        for (auto token : {"0", "1", "4294967295"})
            check(og::parse_overlay_command(driverConfig + "\"steeringInputToken\":" + token + "}", c, error), "steering token accepts uint32");
        for (auto token : {"-1", "4294967296", "0.5", "true", "\"1\"", "1e999"})
            check(!og::parse_overlay_command(driverConfig + "\"steeringInputToken\":" + token + "}", c, error), "invalid steering token rejected");
        check(og::parse_overlay_command(driverConfig + R"("driverEnabled":false})", c, error), "FPE2 accepts driver configuration");
        struct DriverFitRange { const char* key; double low, high; };
        for (const auto bound : {DriverFitRange{"driverSeatHeight", -.3, .3}, DriverFitRange{"driverSeatDistance", -.3, .4},
             DriverFitRange{"driverShoulderWidth", .75, 1.25}, DriverFitRange{"driverTorsoLean", -20, 30},
             DriverFitRange{"driverLegLength", .7, 1.15}, DriverFitRange{"driverKneeHeight", -.15, .15}}) {
            const auto command = driverConfig + "\"" + bound.key + "\":";
            for (double value : {bound.low, bound.high})
                check(og::parse_overlay_command(command + std::to_string(value) + "}", c, error), "seating limits accepted");
            for (const auto value : {std::to_string(bound.low - .01), std::to_string(bound.high + .01),
                     std::string("\"0\""), std::string("true"), std::string("1e999")})
                check(!og::parse_overlay_command(command + value + "}", c, error), "invalid seating value rejected");
        }
        for (const auto model : {"hyper-p1", "hyper-sl", "gxl", "gt-max", "fpe2", "ascher-artura"})
            for (const auto enabled : {"true", "false"})
                check(og::parse_overlay_command(std::string(R"({"op":"configure","sourceKind":"url","wheelModel":")") +
                    model + R"(","wheelDevice":0,"driverEnabled":)" + enabled +
                    R"(,"driverGloveTheme":2,"driverSuitTheme":4,"driverTorso":false,"driverLegs":true})", c, error),
                    "every built-in wheel accepts enabled and disabled driver configuration");
        for (const auto invalid : {R"("driverEnabled":"true"})", R"("driverTorso":1})", R"("driverLegs":0})",
                 R"("driverGloveTheme":5})", R"("driverGloveTheme":-1})", R"("driverGloveTheme":1.5})",
                 R"("driverSuitTheme":"2"})", R"("driverSuitTheme":false})"})
            check(!og::parse_overlay_command(driverConfig + invalid, c, error), "invalid driver setting rejected");
        for (const auto invalid : {R"({"op":"configure","driverEnabled":true})",
                 R"({"op":"configure","sourceKind":"url","wheelModel":"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff","wheelDevice":0,"driverEnabled":true})",
                 R"({"op":"configure","sourceKind":"desktop","wheelModel":"fpe2","wheelDevice":5,"driverEnabled":true})"})
            check(!og::parse_overlay_command(invalid, c, error), "ineligible driver source rejected");
        check(og::parse_overlay_command(R"({"op":"precision","slot":0,"held":true,"pressId":1})", c, error), "precision hold parses");
        check(og::parse_overlay_command(R"({"op":"precision","slot":14,"held":false,"pressId":9007199254740991})", c, error), "release and maximum safe press id parse");
        for (auto invalid : {R"({"op":"precision","slot":0,"held":1,"pressId":1})", R"({"op":"precision","slot":0,"held":"true","pressId":1})", R"({"op":"precision","slot":0,"held":true,"pressId":0})", R"({"op":"precision","slot":0,"held":false,"pressId":1.5})", R"({"op":"precision","slot":0,"held":true,"pressId":9007199254740992})", R"({"op":"precision","slot":15,"held":true,"pressId":1})"})
            check(!og::parse_overlay_command(invalid, c, error), "invalid precision lease rejected");
        // Several processes open the channel at the same time (game layer, overlay host,
        // Clean Output). Whoever loses the creation race must still see an initialised section.
        for (int round = 0; round < 200; ++round) {
            std::array<og::OverlayChannel, 4> channels;
            std::array<bool, 4> opened{};
            std::atomic<bool> go{false};
            std::vector<std::thread> threads;
            for (size_t i = 0; i < channels.size(); ++i)
                threads.emplace_back([&, i] {
                    while (!go.load(std::memory_order_acquire)) {}
                    opened[i] = channels[i].open("test");
                });
            go.store(true, std::memory_order_release);
            for (auto &thread : threads) thread.join();
            for (bool ok : opened) check(ok, "concurrent first open sees an initialised channel");
        }
        og::OverlayChannel a, b;
        check(a.open("test") && b.open("test"), "mapping opens");
        check(a.open("test") && a.data(), "reopening a live channel keeps it");
        {
            // The freshly created section and its lock grant access to this user and SYSTEM
            // only: a protected DACL with exactly those two entries.
            for (const wchar_t *name : {L"Local\\OpenGridXR.Overlays.test", L"Local\\OpenGridXR.Overlays.test.Lock"}) {
                const bool section = !wcsstr(name, L".Lock");
                HANDLE object = section ? OpenFileMappingW(READ_CONTROL | FILE_MAP_READ, FALSE, name)
                                        : OpenMutexW(READ_CONTROL | SYNCHRONIZE, FALSE, name);
                check(object != nullptr, "channel object opens for its owner");
                PACL dacl{};
                PSECURITY_DESCRIPTOR descriptor{};
                check(GetSecurityInfo(object, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                      &dacl, nullptr, &descriptor) == ERROR_SUCCESS && dacl,
                      "channel object has a DACL");
                SECURITY_DESCRIPTOR_CONTROL control{};
                DWORD revision{};
                check(GetSecurityDescriptorControl(descriptor, &control, &revision) &&
                          (control & SE_DACL_PROTECTED) && dacl->AceCount == 2,
                      "channel object is restricted to its owner and SYSTEM");
                LocalFree(descriptor);
                CloseHandle(object);
            }
        }
        check(a.lock(), "producer lock");
        a.data()->slots[14].revision = 42;
        a.unlock();
        check(b.lock() && b.data()->slots[14].revision == 42, "fixed ABI shared");
        b.unlock();
        check(a.lock(), "ABI compatibility lock");
        a.data()->version = 8;
        a.unlock();
        {
            og::OverlayChannel incompatible;
            check(!incompatible.open("test"),
                  "the previous slot layout is rejected");
        }
        check(a.lock(), "restore ABI version");
        a.data()->version = og::overlay_transport_version;
        a.unlock();
        std::cout << "Overlay protocol and ABI tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
