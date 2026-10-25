#include "depthxr/config_path.h"

#include <cstdlib>
#include <cwctype>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace depthxr {
std::filesystem::path LocalAppDataPath() {
#if defined(_WIN32)
    return EnvironmentPath(L"LOCALAPPDATA");
#else
    return {};
#endif
}

std::filesystem::path EnvironmentPath(const wchar_t *name) {
#if defined(_WIN32)
    if (const wchar_t *value = _wgetenv(name); value && *value != L'\0') {
        return std::filesystem::path(value);
    }
#else
    const std::wstring wide(name);
    if (const char *value = std::getenv(std::string(wide.begin(), wide.end()).c_str());
        value && *value != '\0') {
        return std::filesystem::path(value);
    }
#endif
    return {};
}

std::string PathToUtf8(const std::filesystem::path &path) {
#if defined(_WIN32)
    const std::wstring &wide = path.native();
    if (wide.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), size,
                        nullptr, nullptr);
    return result;
#else
    return path.string();
#endif
}

std::filesystem::path EngineModuleDirectory() {
#if defined(_WIN32)
    HMODULE module{};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&EngineModuleDirectory), &module)) {
        std::wstring path(32768, L'\0');
        DWORD n = GetModuleFileNameW(module, path.data(), DWORD(path.size()));
        if (n && n < path.size()) {
            path.resize(n);
            return std::filesystem::path(path).parent_path();
        }
    }
#endif
    return std::filesystem::current_path();
}
std::filesystem::path ResolveConfigPath() {
    if (auto path = EnvironmentPath(L"OPENGRIDXR_ENHANCEMENTS_CONFIG"); !path.empty())
        return path;
    std::wstring exe(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size()));
    exe.resize(n);
    exe = std::filesystem::path(exe).filename().wstring();
    for (auto &c : exe)
        c = std::towlower(c);
    const char *game =
        (exe == L"iracingsim64dx11.exe" || exe == L"iracingsim64dx11_test.exe") ? "iracing"
        : (exe == L"le mans ultimate.exe" || exe == L"lemansultimate.exe")      ? "lmu"
                                                                                : "disabled";
    return EngineModuleDirectory() / (std::string("OpenGridXR-enhancements-") + game + ".json");
}
std::filesystem::path ResolveSeenAppsPath() {
    if (auto path = EnvironmentPath(L"OPENGRIDXR_ENHANCEMENTS_SEEN_APPS_PATH"); !path.empty())
        return path;
    return EngineModuleDirectory() / "enhancements-data" / "seen-apps.json";
}
std::filesystem::path ResolveLogPath() {
    if (auto path = EnvironmentPath(L"OPENGRIDXR_ENHANCEMENTS_LOG_PATH"); !path.empty())
        return path;
    return EngineModuleDirectory() / "enhancements-data" / "OpenGridXR-enhancements.log";
}
} // namespace depthxr
