#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <windows.h>
namespace og {
constexpr unsigned overlay_count = 15;
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
};
struct OverlayShared {
    std::uint32_t magic{0x564f474f}, version{4}, bytes{sizeof(OverlayShared)}, producerPid{},
        consumerPid{};
    std::uint32_t adapterLow{};
    std::int32_t adapterHigh{};
    std::uint64_t producerBeat{}, consumerBeat{}, session{};
    std::uint32_t capacity{}, active{};
    std::uint32_t streamConsumerPid{};
    std::uint64_t streamConsumerBeat{};
    std::array<OverlaySlot, overlay_count> slots;
};
class OverlayChannel {
    HANDLE mapping_{}, mutex_{};
    OverlayShared *data_{};

  public:
    ~OverlayChannel();
    bool open(const std::string &game);
    bool lock(DWORD timeout = 0);
    void unlock();
    OverlayShared *data() { return data_; }
};
} // namespace og
