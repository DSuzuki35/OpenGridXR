#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <windows.h>
#include "owner_security.hpp"
#include "image_processing.hpp"
#include "channel_open_retry.hpp"
namespace og {
constexpr uint32_t frameMagic = 0x4f475852, frameVersion = 5;
// Single-sample depth from the selected eye's subimage at this frame's display time.
struct CapturedDepth {
    uint64_t texture{}, generation{};
    uint32_t width{}, height{};
    float fov[4]{}; // left, right, up, down tangents
    float minDepth{}, maxDepth{}, invNear{}, invFar{};
};
struct CapturedOverlay {
    uint32_t valid{};
    uint32_t pixelWidth{}, pixelHeight{};
    uint64_t sourceRevision{};
    float corners[4][3]{}; // TL, TR, BL, BR in the raw captured eye's camera space (-Z forward).
};
struct SharedFrame {
    uint32_t magic{}, version{}, size{}, ready{}, producer{}, consumer{}, eye{}, fps{60};
    uint64_t consumerBeat{}, published{}, sequence{}, texture{}, session{}, generation{},
        reference{};
    uint32_t width{}, height{}, format{};
    LUID adapter{};
    float orientation[4]{0, 0, 0, 1}, fov[4]{-1, 1, 1, -1};
    float position[3]{};
    int64_t displayTime{};
    uint32_t captureEye{};
    uint64_t overlaySession{};
    uint32_t overlayConsumerPid{}, overlayProducerPid{};
    std::array<CapturedOverlay, 15> overlays;
    CapturedDepth depth;
    SceneColorSettings color{};
    int32_t visorPosition{-1}; // Headset state for this capture, or use saved stream position.
    uint64_t sourceBeat{}; // XR eye activity even when the output is displaying a monitor.
};
class Channel {
    HANDLE mapping_{}, mutex_{}, frameEvent_{};
    SharedFrame *data_{};
    bool locked_{};
    ChannelOpenRetry retry_;

    void close() {
        if (locked_)
            unlock();
        if (data_)
            UnmapViewOfFile(data_);
        if (mapping_)
            CloseHandle(mapping_);
        if (mutex_)
            CloseHandle(mutex_);
        if (frameEvent_)
            CloseHandle(frameEvent_);
        data_ = nullptr;
        mapping_ = mutex_ = frameEvent_ = nullptr;
    }

  public:
    ~Channel() { close(); }
    bool open(const std::string &game, DWORD timeout=1000) {
        if (data_)
            return true;
        retry_.clear();
        std::wstring name = L"Local\\OpenGridXR.Frame." + std::to_wstring(frameVersion) +
                            L"." + std::wstring(game.begin(), game.end());
        OwnerSecurity security;
        if (!security.get())
            return false;
        mutex_ = CreateMutexW(security.get(), FALSE, (name + L".Mutex").c_str());
        if (mutex_)
            mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, security.get(), PAGE_READWRITE, 0,
                                          sizeof(SharedFrame), name.c_str());
        if (mapping_)
            data_ = static_cast<SharedFrame *>(
                MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedFrame)));
        if (!data_ || !lock(timeout)) {
            if(data_) retry_.defer(game);
            close();
            return false;
        }
        if (data_->magic != frameMagic || data_->version != frameVersion ||
            data_->size != sizeof(SharedFrame)) {
            *data_ = {};
            data_->magic = frameMagic;
            data_->version = frameVersion;
            data_->size = sizeof(SharedFrame);
        }
        // Optional so older peers and unavailable event handles still work by polling.
        frameEvent_ = CreateEventW(security.get(), FALSE, FALSE, (name + L".Ready").c_str());
        unlock();
        return true;
    }
    bool lock(DWORD timeout = 0) {
        if (!data_ || !mutex_)
            return false;
        DWORD r = WaitForSingleObject(mutex_, timeout);
        return locked_ = (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED);
    }
    void unlock() {
        if (locked_) {
            locked_ = false;
            ReleaseMutex(mutex_);
        }
    }
    SharedFrame *data() { return data_; }
    HANDLE frameEvent() const { return frameEvent_; }
    void notifyFrame() const { if (frameEvent_) SetEvent(frameEvent_); }
    bool pending() const { return retry_.pending(); }
    bool ready(ULONGLONG now) {
        return data_ || retry_.poll(now,[this](const auto& game){return open(game,0);});
    }
};
} // namespace og
