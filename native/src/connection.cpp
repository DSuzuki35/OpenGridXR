// Mutations install only this executable's adjacent OpenGrid assets in the
// protected Program Files runtime. No caller-selected mutation path/key/command.
// Read-only --status accepts an owned cache manifest to inspect legacy entries.
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "../layers/layer_registry.h"
#include "../include/runtime_paths.hpp"

namespace opengrid::connection {
constexpr wchar_t registry_key[] = L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit";
std::filesystem::path machine_directory() {
    PWSTR value = nullptr;
    const auto result = SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &value);
    if (FAILED(result)) throw std::runtime_error("Program Files unavailable");
    const auto folder = std::filesystem::path(value) / L"OpenGridXR Runtime";
    CoTaskMemFree(value);
    return folder;
}
std::string utf8(const std::wstring& text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
struct Key {
    HKEY value = nullptr;
    void close() { if (value) { RegCloseKey(value); value = nullptr; } }
    ~Key() { close(); }
};
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
DWORD read_registration(HKEY root, const wchar_t* subkey, const std::wstring& manifest, bool& present, bool& enabled) {
    present = enabled = false;
    Key key;
    auto error = RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key.value);
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return ERROR_SUCCESS;
    if (error) return error;
    DWORD bytes = 0, type = 0;
    error = RegQueryValueExW(key.value, manifest.c_str(), nullptr, &type, nullptr, &bytes);
    if (error == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
    if (error) return error;
    present = true;
    if (type == REG_DWORD && bytes == sizeof(DWORD)) {
        DWORD value = 1;
        error = RegQueryValueExW(key.value, manifest.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(&value), &bytes);
        if (error) return error;
        enabled = type == REG_DWORD && bytes == sizeof(DWORD) && value == 0;
    }
    return ERROR_SUCCESS;
}
DWORD print_status(const std::wstring& localManifest) {
    if (!layers::valid_manifest(localManifest) || _wcsicmp(std::filesystem::path(localManifest).filename().c_str(), L"OpenGridXR.json")) return ERROR_INVALID_PARAMETER;
    const auto folder = machine_directory();
    std::string json = "{\"machineDirectory\":" + layers::json_string(utf8(folder.wstring()));
    for (const auto& scope : {std::string("machine"), std::string("legacyMachine"), std::string("user")}) {
        bool present = false, enabled = false;
        const auto error = read_registration(scope == "user" ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE, registry_key,
            scope == "machine" ? (folder / L"OpenGridXR.json").wstring() : localManifest, present, enabled);
        if (error) return error; // permission/read failures never mean "absent"
        json += ",\"" + scope + "\":{\"present\":" + (present ? "true" : "false") + ",\"enabled\":" + (enabled ? "true" : "false") + "}";
    }
    std::cout << json << "}\n";
    return ERROR_SUCCESS;
}

DWORD install_assets(const std::filesystem::path& source, const std::filesystem::path& destination) {
    std::error_code error;
    std::filesystem::create_directories(destination, error);
    if (error) return DWORD(error.value());
    // Refuse redirected destinations instead of writing through a junction.
    for (const auto& relative : {L"", L"providers", L"providers\\qvfr"}) {
        const auto dir = relative[0] ? destination / relative : destination;
        const auto attributes = GetFileAttributesW(dir.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return ERROR_ACCESS_DENIED;
    }
    for (const auto& relative : {L"OpenGridXR.dll", L"OpenGridXR-Enhancements.dll", L"OpenGridXR.json", L"providers\\qvfr\\OpenGridXR-QVFR.dll"}) {
        const auto from = source / relative, to = destination / relative;
        if (std::wstring(relative).find(L"providers") == 0 && !std::filesystem::exists(from)) continue;
        std::filesystem::create_directories(to.parent_path(), error);
        if (error) return DWORD(error.value());
        const auto staged = to.wstring() + L".new";
        if (!CopyFileW(from.c_str(), staged.c_str(), FALSE)) return GetLastError();
        if (!MoveFileExW(staged.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return GetLastError();
    }
    std::ofstream marker(destination / L"OpenGridXR.machine", std::ios::binary | std::ios::trunc);
    marker << "User data: LOCALAPPDATA/OpenGridXR/runtime\n";
    marker.close();
    return marker ? ERROR_SUCCESS : ERROR_WRITE_FAULT;
}

DWORD ensure_idle() {
    Handle snapshot;
    snapshot.value = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot.value == INVALID_HANDLE_VALUE) return GetLastError();
    PROCESSENTRY32W process{}; process.dwSize = sizeof(process);
    if (!Process32FirstW(snapshot.value, &process)) return GetLastError();
    do { if (layers::supported_process(process.szExeFile)) return ERROR_BUSY; }
    while (Process32NextW(snapshot.value, &process));
    const auto error = GetLastError();
    return error == ERROR_NO_MORE_FILES ? ERROR_SUCCESS : error;
}

// Root/key injection is internal, used only by the private-HKCU test fixture.
DWORD change_registration(HKEY root, const wchar_t* subkey, const std::wstring& manifest, bool install) {
    if (!layers::valid_manifest(manifest) ||
        _wcsicmp(std::filesystem::path(manifest).filename().c_str(), L"OpenGridXR.json") != 0)
        return ERROR_INVALID_PARAMETER;
    Key key;
    DWORD error;
    if (install) {
        error = RegCreateKeyExW(root, subkey, 0, nullptr, 0,
            KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr,
            &key.value, nullptr);
    } else {
        error = RegOpenKeyExW(root, subkey, 0,
            KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, &key.value);
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return ERROR_SUCCESS;
    }
    if (error) return error;
    // Exactly one owned value changes. Each Set/Delete is atomic; no multi-value
    // transaction is needed (KTM may be unavailable on otherwise valid systems).
    const DWORD enabled = 0;
    error = install ? RegSetValueExW(key.value, manifest.c_str(), 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&enabled), sizeof(enabled)) : RegDeleteValueW(key.value, manifest.c_str());
    if (error && (install || error != ERROR_FILE_NOT_FOUND)) return error;
    key.close();
    error = RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key.value);
    if (error) return error;
    DWORD type = 0, bytes = sizeof(DWORD), value = 1;
    error = RegQueryValueExW(key.value, manifest.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(&value), &bytes);
    if (!install) return error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : ERROR_IO_DEVICE;
    return !error && type == REG_DWORD && bytes == sizeof(DWORD) && value == 0 ? ERROR_SUCCESS : ERROR_IO_DEVICE;
}

DWORD execute(const std::vector<std::wstring>& args) {
    if (args.size() == 2 && args[0] == L"--status") return print_status(args[1]);
    if (args.size() != 1) return ERROR_INVALID_PARAMETER;
    const bool install = args[0] == L"--install" || args[0] == L"--commit-install";
    const bool remove = args[0] == L"--remove" || args[0] == L"--commit-remove";
    if (!install && !remove) return ERROR_INVALID_PARAMETER;
    auto error = ensure_idle();
    if (error) return error;
    std::vector<wchar_t> buffer(32768);
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length) return GetLastError();
    if (length >= buffer.size()) return ERROR_INSUFFICIENT_BUFFER;
    if (args[0] == L"--install" || args[0] == L"--remove") {
        SHELLEXECUTEINFOW info{}; info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        info.lpVerb = L"runas"; info.lpFile = buffer.data();
        info.lpParameters = install ? L"--commit-install" : L"--commit-remove";
        info.nShow = SW_HIDE;
        if (!ShellExecuteExW(&info)) return GetLastError();
        Handle child; child.value = info.hProcess;
        if (!child.value) return ERROR_INVALID_HANDLE;
        if (WaitForSingleObject(child.value, INFINITE) != WAIT_OBJECT_0) return GetLastError();
        DWORD result = ERROR_GEN_FAILURE;
        return GetExitCodeProcess(child.value, &result) ? result : GetLastError();
    }
    const auto folder = std::filesystem::path(buffer.data()).parent_path();
    const auto manifest = folder / L"OpenGridXR.json";
    if (install) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(manifest, ec) ||
            !std::filesystem::is_regular_file(folder / L"OpenGridXR.dll", ec)) return ERROR_FILE_NOT_FOUND;
    }
    const auto machine = machine_directory();
    if (install) {
        error = install_assets(folder, machine);
        if (error) return error;
    }
    error = change_registration(HKEY_LOCAL_MACHINE, registry_key, (machine / L"OpenGridXR.json").wstring(), install);
    if (error) return error;
    // Migrate/remove the previous HKLM entry pointing into the user cache only
    // after the protected installation has been confirmed. No foreign entries.
    return change_registration(HKEY_LOCAL_MACHINE, registry_key, manifest.wstring(), false);
}
}

#ifndef OPENGRID_CONNECTION_TEST
int wmain(int argc, wchar_t** argv) {
    try {
        const auto error = opengrid::connection::execute(std::vector<std::wstring>(argv + 1, argv + argc));
        if (error) std::cerr << "OpenGrid connection failed: Win32 " << error << '\n';
        return static_cast<int>(error);
    } catch (...) { return ERROR_NOT_ENOUGH_MEMORY; }
}
#endif
