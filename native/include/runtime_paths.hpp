#pragma once
#include <filesystem>
#include "../third_party/lmu_quadview/mod/win_strings.hpp"
#include <windows.h>

namespace og {
// Machine-installed executable assets are read-only to ordinary users. Only
// settings, artwork and status use the current user's writable runtime folder.
inline std::filesystem::path runtime_data_directory(const std::filesystem::path& moduleDirectory) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(moduleDirectory / L"OpenGridXR.machine", error)) return moduleDirectory;
    const auto local = lmuqv::win::environment(L"LOCALAPPDATA");
    if (local.empty()) return moduleDirectory; // no writable fallback
    return std::filesystem::path(local) / L"OpenGridXR" / L"runtime";
}
}
