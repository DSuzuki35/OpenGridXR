#include "overlay_transport.hpp"
#include <new>
namespace og {
OverlayChannel::~OverlayChannel() {
    if (data_)
        UnmapViewOfFile(data_);
    if (mapping_)
        CloseHandle(mapping_);
    if (mutex_)
        CloseHandle(mutex_);
}
bool OverlayChannel::open(const std::string &game) {
    if (game != "iracing" && game != "lmu" && game != "test")
        return false;
    std::wstring name = L"Local\\OpenGridXR.Overlays." + std::wstring(game.begin(), game.end());
    mutex_ = CreateMutexW(nullptr, FALSE, (name + L".Lock").c_str());
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  sizeof(OverlayShared), name.c_str());
    const bool fresh = GetLastError() != ERROR_ALREADY_EXISTS;
    if (!mapping_ || !mutex_)
        return false;
    data_ = static_cast<OverlayShared *>(
        MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(OverlayShared)));
    if (!data_ || !lock(1000))
        return false;
    if (fresh)
        new (data_) OverlayShared;
    const bool valid =
        data_->magic == 0x564f474f && data_->version == 4 && data_->bytes == sizeof(OverlayShared);
    unlock();
    return valid;
}
bool OverlayChannel::lock(DWORD timeout) {
    auto r = WaitForSingleObject(mutex_, timeout);
    return r == WAIT_OBJECT_0 || r == WAIT_ABANDONED;
}
void OverlayChannel::unlock() { ReleaseMutex(mutex_); }
} // namespace og
