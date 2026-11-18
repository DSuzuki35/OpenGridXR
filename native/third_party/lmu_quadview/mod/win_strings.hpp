#pragma once
// Long-path Win32 string helpers. Results live on the heap: several call sites run on
// LMU's own threads (OpenXR calls, engine hooks), where 64 KiB arrays per call used to
// sit on the game's stack. Every helper is noexcept and returns an empty string when the
// value is missing, longer than the Windows limit (32767 characters) or memory is short.
#include <windows.h>
#include <algorithm>
#include <string>

namespace lmuqv::win {
constexpr DWORD long_path_limit = 32768; // characters including the terminator

inline std::wstring environment(const wchar_t* name) noexcept {
    try {
        const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
        if (!size || size > long_path_limit) return {};
        std::wstring value(size, L'\0');
        const DWORD written = GetEnvironmentVariableW(name, value.data(), size);
        if (!written || written >= size) return {}; // empty, or changed between the calls
        value.resize(written);
        return value;
    } catch (...) {
        return {};
    }
}

inline std::wstring module_path(HMODULE module) noexcept {
    try {
        std::wstring path(MAX_PATH, L'\0');
        for (;;) {
            const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
            if (!length) return {};
            if (length < path.size()) {
                path.resize(length);
                return path;
            }
            if (path.size() >= long_path_limit) return {};
            path.resize((std::min)(path.size() * 2, std::size_t(long_path_limit)));
        }
    } catch (...) {
        return {};
    }
}
} // namespace lmuqv::win
