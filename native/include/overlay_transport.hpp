#pragma once
#include "channel_open_retry.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <windows.h>
namespace og {
constexpr unsigned overlay_count = 15;
constexpr std::uint32_t overlay_transport_version = 12;
enum class OverlayState : std::uint32_t {
    Waiting,
    Ready,
    Disabled,
    LayerLimit,
    GpuMismatch,
    Error
};
struct OverlayFrame {
    std::uint64_t generation{}, handle{}, frameId{};
    std::uint32_t width{}, height{}, format{};
};
struct OverlayPose {
    std::uint32_t valid{}, pixelWidth{}, pixelHeight{};
    std::uint64_t sourceRevision{}, configRevision{}, sourceGeneration{}, recenter{}, space{};
    float orientation[4]{0, 0, 0, 1}, position[3]{}, width{}, height{};
};
struct DesktopPointer {
    std::uint32_t valid{}, source{}; // 1 head
    std::uint64_t tick{}, session{}, configRevision{}, sourceRevision{}, sourceGeneration{},
        recenter{};
    float u{}, v{}, distance{};
    std::uint64_t focusEpoch{};
};
struct DashboardPlacement {
    float x{}, y{}, z{}, yaw{}, pitch{}, roll{}, widthMeters{};
    uint64_t tick{};
};
struct OverlaySlot {
    std::uint32_t enabled{}, anchor{}, current{};
    float x{}, y{}, z{-1.5f}, yaw{}, pitch{}, roll{}, widthMeters{1}, opacity{1};
    std::uint64_t revision{}, recenter{}, sourceRevision{};
    std::array<OverlayFrame, 2> frames;
    std::uint64_t consumedFrame{};
    OverlayState state{OverlayState::Waiting};
    std::uint32_t showInStream{}, streamCurrent{};
    std::array<OverlayFrame, 2> streamFrames;
    OverlayPose pose;
    std::uint32_t desktopMode{};
    std::int32_t desktopX{}, desktopY{}, desktopWidth{}, desktopHeight{};
    DesktopPointer pointer;
    float precisionScale{1.75f}, precisionU{.5f}, precisionV{.5f};
    std::uint32_t precisionActive{};
    std::uint32_t cockpitOcclusion{}, cockpitDepthActive{};
    std::uint32_t webDashboard{}, steeringFollow{}, placementMode{};
    float pivotX{}, pivotY{}, pivotZ{}, steeringTilt{};
    std::uint64_t placementToken{}, previewUntil{};
    DashboardPlacement placementAhead;
    std::array<char,65> wheelModel{};
    std::uint32_t wheelDevice{}, wheelLights{}, wheelScreen{};
    float wheelAmbientLight{};
    std::uint64_t wheelLightTick{};
    std::array<std::uint8_t,96*3> wheelRgb{};
};
struct OverlayShared {
    std::uint32_t magic{0x564f474f}, version{overlay_transport_version}, bytes{sizeof(OverlayShared)}, producerPid{},
        consumerPid{};
    std::uint32_t adapterLow{};
    std::int32_t adapterHigh{};
    std::uint64_t producerBeat{}, consumerBeat{}, session{};
    std::uint32_t capacity{}, active{};
    std::uint32_t inputFocused{}; // atomically updated on focus loss/session end
    std::uint64_t focusEpoch{};
    std::uint32_t streamConsumerPid{};
    std::uint64_t streamConsumerBeat{};
    // The host invalidates retained VR images before changing publisher or presentation
    // controls under the IPC mutex. Pixel updates to the same source keep this epoch.
    alignas(8) LONG64 presentationEpoch{};
    void invalidatePresentation() { InterlockedIncrement64(&presentationEpoch); }
    LONG64 publishedPresentationEpoch() { return InterlockedCompareExchange64(&presentationEpoch, 0, 0); }
    std::array<OverlaySlot, overlay_count> slots;
};
// A writer can briefly own the IPC mutex while the capture or output thread
// needs to draw. Only reuse controls from the same presentation, for a few frames.
struct OverlayPresentationSnapshot {
    OverlayShared state;
    ULONGLONG expires{};
    void remember(const OverlayShared &live) {
        state = live;
        expires = GetTickCount64() + 200;
    }
    bool usable(OverlayShared &live) const {
        return GetTickCount64() < expires && state.presentationEpoch == live.publishedPresentationEpoch();
    }
};
class OverlayChannel {
    HANDLE mapping_{}, mutex_{};
    OverlayShared *data_{};
    ChannelOpenRetry retry_;

  public:
    ~OverlayChannel();
    bool open(const std::string &game, DWORD timeout=1000);
    bool pending() const { return retry_.pending(); }
    bool ready(ULONGLONG now) {
        return data_ || retry_.poll(now,[this](const auto& game){return open(game,0);});
    }
    bool lock(DWORD timeout = 0);
    void unlock();
    OverlayShared *data() { return data_; }
};
} // namespace og
