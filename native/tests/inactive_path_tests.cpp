#include <windows.h>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
uint64_t clockMs = 10000;
uint64_t mutexProbes{};
DWORD countedWait(HANDLE handle, DWORD timeout) {
    ++mutexProbes;
    return WaitForSingleObject(handle, timeout);
}
ULONGLONG testClock() { return clockMs; }
}
#define WaitForSingleObject countedWait
#define GetTickCount64 testClock
#include "../src/layer.cpp"
#undef GetTickCount64
#undef WaitForSingleObject

void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
bool originalViewer(Session &s) {
    if (!s.channel.lock()) return false;
    auto *f = s.channel.data();
    const bool active = f->consumer && clockMs - f->consumerBeat < 3000;
    s.channel.unlock();
    return active;
}
template<class Probe> void benchmark(const char *name, Session &s, Probe probe) {
    constexpr unsigned calls = 1000000;
    mutexProbes = 0;
    const auto start = std::chrono::steady_clock::now();
    unsigned active{};
    for (unsigned n = 0; n < calls; ++n) {
        clockMs = 10000 + uint64_t(n) * 1000 / 270; // Three probes per frame, 90 fps.
        active += probe(s);
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << name << ": calls=" << calls << " mutexProbes=" << mutexProbes
              << " ns/call=" << double(ns) / calls << " active=" << active << '\n';
}
int main(int argc, char **argv) {
    try {
        Instance instance;
        instance.game = "inactive-test-" + std::to_string(GetCurrentProcessId());
        Session session;
        session.instance = &instance;
        require(session.channel.open(instance.game), "isolated capture channel");
        if (argc > 1 && std::string(argv[1]) == "--benchmark") {
            benchmark("original inactive viewer", session, originalViewer);
            clockMs = 10000;
            benchmark("optimized inactive viewer", session, viewer);
            return 0;
        }
        mutexProbes = 0;
        for (unsigned frame = 0; frame < 90; ++frame) {
            clockMs = 10000 + frame * 1000 / 90;
            for (unsigned probe = 0; probe < 3; ++probe)
                require(!viewer(session), "absent viewer stays inactive");
        }
        require(mutexProbes == 4, "one second at 90 fps probes absent viewer only four times");
        require(session.channel.lock(), "attach output");
        session.channel.data()->consumer = GetCurrentProcessId();
        session.channel.data()->consumerBeat = clockMs;
        session.channel.unlock();
        clockMs += 250;
        require(viewer(session), "output attached within 250 ms without session restart");
        require(session.channel.lock(), "detach output");
        session.channel.data()->consumer = 0;
        session.channel.unlock();
        require(!viewer(session), "active output detachment checked immediately");

        auto chain = std::make_unique<Swapchain>();
        chain->session = &session;
        chain->info.width = chain->info.height = 64;
        chain->info.arraySize = 1;
        chain->info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        const auto handle = reinterpret_cast<XrSwapchain>(43);
        chains.emplace(handle, std::move(chain));
        XrCompositionLayerProjectionView views[2]{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                                {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
        for (auto &view : views) view.subImage = {handle, {{0, 0}, {64, 64}}, 0};
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        projection.viewCount = 2;
        projection.views = views;
        const auto base = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&projection);
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
        end.layerCount = 1;
        end.layers = &base;
        remember_projection_regions(session, &end);
        require(chains.at(handle)->projectionRegions.empty(),
                "disabled image processing does not collect projection regions");
        require(chains.at(handle)->projectionRegions.capacity() == 0,
                "disabled image processing allocates no region vector storage");
        chains.clear();
        std::cout << "Inactive paths: 270 viewer checks -> 4 mutex probes; zero region allocations\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
