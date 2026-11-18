#include "overlay_transport.hpp"
#include "owner_security.hpp"
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
bool OverlayChannel::open(const std::string &game, DWORD timeout) {
    retry_.clear();
    if (game != "iracing" && game != "lmu" && game != "test")
        return false;
    std::wstring name = L"Local\\OpenGridXR.Overlays." + std::wstring(game.begin(), game.end());
    if (data_)
        return true; // already open: never replace (and leak) the handles of a live channel
    OwnerSecurity security;
    if (!security.get())
        return false;
    mutex_ = CreateMutexW(security.get(), FALSE, (name + L".Lock").c_str());
    // Create and initialise the section under the channel lock. The game layer, the overlay
    // host and Clean Output open this channel at about the same time: without the lock a
    // second process could map the still zero-filled section between CreateFileMapping and
    // the placement-new below, fail the magic check and stay disconnected.
    if (!mutex_)
        return false;
    if (!lock(timeout)) {
        retry_.defer(game);
        CloseHandle(mutex_);
        mutex_ = nullptr;
        return false;
    }
    bool valid = false;
    SetLastError(ERROR_SUCCESS);
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, security.get(), PAGE_READWRITE, 0,
                                  sizeof(OverlayShared), name.c_str());
    const bool fresh = mapping_ && GetLastError() != ERROR_ALREADY_EXISTS;
    auto *view = mapping_ ? static_cast<OverlayShared *>(MapViewOfFile(
                                mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(OverlayShared)))
                          : nullptr;
    if (view) {
        if (fresh)
            new (view) OverlayShared;
        valid = view->magic == 0x564f474f && view->version == overlay_transport_version &&
                view->bytes == sizeof(OverlayShared);
        if (valid)
            data_ = view;
        else
            UnmapViewOfFile(view);
    }
    unlock();
    if (!valid) {
        if (mapping_)
            CloseHandle(mapping_);
        CloseHandle(mutex_);
        mapping_ = mutex_ = nullptr;
    }
    return valid;
}
bool OverlayChannel::lock(DWORD timeout) {
    if (!mutex_) return false;
    auto r = WaitForSingleObject(mutex_, timeout);
    return r == WAIT_OBJECT_0 || r == WAIT_ABANDONED;
}
void OverlayChannel::unlock() { ReleaseMutex(mutex_); }
} // namespace og
