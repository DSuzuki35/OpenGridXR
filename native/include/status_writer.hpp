#pragma once
#include <windows.h>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>

namespace og {
// One serialized writer for all sessions, including sessions sharing a game.
// Queued records own their data and never acquire the OpenXR layer lock.
class StatusWriter {
    std::mutex mutex;
    std::condition_variable idle;
    std::map<std::filesystem::path, std::string> pending;
    bool running{};
    HMODULE module{};

    static void write(const std::filesystem::path& path, const std::string& json) noexcept {
        try {
            auto temp = path;
            temp += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) return;
            out << json;
            out.close();
            if (!out || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
                DeleteFileW(temp.c_str());
        } catch (...) {} // A diagnostic failure must not affect frame submission.
    }

    static void CALLBACK drain(PTP_CALLBACK_INSTANCE callback, void* context) noexcept {
        auto& self = *static_cast<StatusWriter*>(context);
        std::unique_lock lock(self.mutex);
        FreeLibraryWhenCallbackReturns(callback, self.module);
        while (!self.pending.empty()) {
            auto record = self.pending.extract(self.pending.begin());
            lock.unlock();
            write(record.key(), record.mapped());
            lock.lock();
        }
        self.running = false;
        self.idle.notify_all();
    }

public:
    void submit(std::filesystem::path path, std::string json, bool terminal) {
        std::unique_lock lock(mutex);
        pending.insert_or_assign(path, std::move(json));
        if (!running) {
            // Keep the DLL loaded through the callback epilogue even if the
            // loader releases its reference immediately after session teardown.
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                   reinterpret_cast<LPCWSTR>(&drain), &module)) {
                running = TrySubmitThreadpoolCallback(drain, this, nullptr) != FALSE;
                if (!running) FreeLibrary(module);
            }
            if (!running) {
                auto record = pending.extract(path);
                if (terminal) write(record.key(), record.mapped());
                return;
            }
        }
        if (terminal) idle.wait(lock, [&] { return !running; });
    }

    void flush() {
        std::unique_lock lock(mutex);
        idle.wait(lock, [&] { return !running; });
    }
};
} // namespace og
