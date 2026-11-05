#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <windows.h>
namespace og {
constexpr uint32_t frameMagic = 0x4f475852, frameVersion = 2;
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
};
class Channel {
    HANDLE mapping_{}, mutex_{};
    SharedFrame *data_{};
    bool locked_{};

  public:
    ~Channel() {
        if (locked_)
            unlock();
        if (data_)
            UnmapViewOfFile(data_);
        if (mapping_)
            CloseHandle(mapping_);
        if (mutex_)
            CloseHandle(mutex_);
    }
    bool open(const std::string &game) {
        if (data_)
            return true;
        std::wstring name = L"Local\\OpenGridXR.Frame." + std::wstring(game.begin(), game.end());
        mutex_ = CreateMutexW(nullptr, FALSE, (name + L".Mutex").c_str());
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                      sizeof(SharedFrame), name.c_str());
        if (!mutex_ || !mapping_)
            return false;
        data_ = static_cast<SharedFrame *>(
            MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedFrame)));
        if (!data_)
            return false;
        if (lock()) {
            if (data_->magic != frameMagic || data_->version != frameVersion ||
                data_->size != sizeof(SharedFrame)) {
                *data_ = {};
                data_->magic = frameMagic;
                data_->version = frameVersion;
                data_->size = sizeof(SharedFrame);
            }
            unlock();
        }
        return true;
    }
    bool lock() {
        if (!data_ || !mutex_)
            return false;
        DWORD r = WaitForSingleObject(mutex_, 0);
        return locked_ = (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED);
    }
    void unlock() {
        if (locked_) {
            locked_ = false;
            ReleaseMutex(mutex_);
        }
    }
    SharedFrame *data() { return data_; }
};
} // namespace og
