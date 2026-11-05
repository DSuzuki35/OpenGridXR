// Fixed-purpose helper: never opens a manifest, starts a shell, or accepts a registry key.
#include "layer_registry.h"
#include <windows.h>
#include <ktmw32.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <iostream>
#include <sstream>
#include <utility>

namespace {
using namespace opengrid::layers;
struct Key {
    HKEY value = nullptr;
    ~Key() { close(); }
    void close() { if (value) { RegCloseKey(value); value = nullptr; } }
};
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
HKEY root(Scope scope) { return scope == Scope::user ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE; }
REGSAM view_flag(View view) { return view == View::x64 ? KEY_WOW64_64KEY : KEY_WOW64_32KEY; }

uint32_t read_value(HKEY key, const std::wstring& name, Value& value) {
    DWORD type = 0, size = 0, data = 0;
    auto error = RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &size);
    if (error) return error;
    value = {type, size, 0};
    if (type != REG_DWORD || size != sizeof(DWORD)) return ERROR_SUCCESS;
    error = RegQueryValueExW(key, name.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(&data), &size);
    if (error) return error;
    value = {type, size, data};
    return ERROR_SUCCESS;
}

uint32_t ensure_processes_idle() {
    Handle snapshot;
    snapshot.value = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot.value == INVALID_HANDLE_VALUE) return GetLastError();
    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    if (!Process32FirstW(snapshot.value, &process)) return GetLastError();
    do {
        if (supported_process(process.szExeFile)) return ERROR_BUSY;
    } while (Process32NextW(snapshot.value, &process));
    const DWORD error = GetLastError();
    return error == ERROR_NO_MORE_FILES ? ERROR_SUCCESS : error;
}

class WindowsRegistry final : public Registry {
    Target target_;
    Handle transaction_;
    Key key_;
public:
    uint32_t ensure_idle() override { return ensure_processes_idle(); }
    uint32_t begin(const Target& target) override {
        target_ = target;
        // No fallback to nontransactional writes if KTM is unavailable.
        transaction_.value = CreateTransaction(nullptr, nullptr, 0, 0, 0, 10000, nullptr);
        if (transaction_.value == INVALID_HANDLE_VALUE) return GetLastError();
        return RegOpenKeyTransactedW(root(target.scope), registry_key(target.kind).c_str(), 0,
            KEY_QUERY_VALUE | KEY_SET_VALUE | view_flag(target.view), &key_.value, transaction_.value, nullptr);
    }
    uint32_t read(Value& value) override { return read_value(key_.value, target_.manifest, value); }
    uint32_t write(uint32_t value) override {
        return RegSetValueExW(key_.value, target_.manifest.c_str(), 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&value), sizeof(value));
    }
    uint32_t commit() override {
        key_.close();
        return CommitTransaction(transaction_.value) ? ERROR_SUCCESS : GetLastError();
    }
    uint32_t read_committed(Value& value) override {
        Key fresh;
        const auto error = RegOpenKeyExW(root(target_.scope), registry_key(target_.kind).c_str(), 0,
            KEY_QUERY_VALUE | view_flag(target_.view), &fresh.value);
        return error ? error : read_value(fresh.value, target_.manifest, value);
    }
};

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
    return out;
}
const char* type_name(DWORD type) {
    switch (type) {
        case REG_DWORD: return "DWORD";
        case REG_SZ: return "REG_SZ";
        case REG_EXPAND_SZ: return "REG_EXPAND_SZ";
        case REG_MULTI_SZ: return "REG_MULTI_SZ";
        case REG_BINARY: return "REG_BINARY";
        case REG_QWORD: return "REG_QWORD";
        case REG_NONE: return "REG_NONE";
        case REG_LINK: return "REG_LINK";
        default: return "OTHER";
    }
}
void inventory() {
    std::ostringstream entries;
    std::vector<std::string> errors;
    bool first = true;
    for (Scope scope : {Scope::user, Scope::machine}) {
        for (View view : {View::x64, View::x86}) {
            for (Kind kind : {Kind::implicit, Kind::explicit_layer}) {
                const std::string scope_text = scope == Scope::user ? "HKCU" : "HKLM";
                const std::string view_text = view == View::x64 ? "64" : "32";
                const std::string kind_text = kind == Kind::implicit ? "implicit" : "explicit";
                auto report = [&](DWORD error) {
                    errors.push_back(scope_text + "/" + view_text + "/" + kind_text + ": Win32 " + std::to_string(error));
                };
                Key key;
                auto error = RegOpenKeyExW(root(scope), registry_key(kind).c_str(), 0, KEY_QUERY_VALUE | view_flag(view), &key.value);
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) continue;
                if (error) { report(error); continue; }
                // Registry value names cannot exceed 16,383 UTF-16 characters.
                std::vector<wchar_t> name(16384);
                for (DWORD index = 0; ; ++index) {
                    DWORD name_size = static_cast<DWORD>(name.size()), type = 0, size = sizeof(DWORD), data = 0;
                    error = RegEnumValueW(key.value, index, name.data(), &name_size, nullptr, &type,
                        reinterpret_cast<BYTE*>(&data), &size);
                    if (error == ERROR_NO_MORE_ITEMS) break;
                    if ((error != ERROR_SUCCESS && error != ERROR_MORE_DATA) || name_size >= name.size()) {
                        report(error ? error : ERROR_MORE_DATA); break;
                    }
                    if (!first) entries << ',';
                    first = false;
                    entries << "{\"scope\":" << json_string(scope_text) << ",\"view\":" << json_string(view_text)
                        << ",\"kind\":" << json_string(kind_text) << ",\"manifest\":" << json_string(utf8(std::wstring(name.data(), name_size)))
                        << ",\"type\":" << json_string(type_name(type)) << ",\"value\":";
                    if (error == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(DWORD)) entries << data;
                    else entries << "null";
                    entries << '}';
                }
            }
        }
    }
    std::cout << "{\"entries\":[" << entries.str() << "],\"errors\":[";
    for (size_t i = 0; i < errors.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << json_string(errors[i]);
    }
    std::cout << "]}\n";
}

uint32_t elevate(const Command& command) {
    const auto parameters = elevated_parameters(command);
    if (parameters.empty()) return ERROR_INVALID_PARAMETER;
    const auto idle = ensure_processes_idle();
    if (idle) return idle;
    std::vector<wchar_t> executable(32768);
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length) return GetLastError();
    if (length >= executable.size()) return ERROR_INSUFFICIENT_BUFFER;
    // Resolve only this running executable, never caller-selected programs or shell text.
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"runas";
    info.lpFile = executable.data();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) return GetLastError();
    Handle child;
    child.value = info.hProcess;
    if (!child.value) return ERROR_INVALID_HANDLE;
    if (WaitForSingleObject(child.value, INFINITE) != WAIT_OBJECT_0) return GetLastError();
    DWORD result = ERROR_GEN_FAILURE;
    if (!GetExitCodeProcess(child.value, &result)) return GetLastError();
    return result;
}
}

int wmain(int argc, wchar_t** argv) {
    using namespace opengrid::layers;
    try {
        Command command;
        uint32_t error = parse_command(std::vector<std::wstring>(argv + 1, argv + argc), command);
        if (!error) {
            if (command.operation == Operation::inventory) { inventory(); return 0; }
            if (command.operation == Operation::elevate_set) error = elevate(command);
            else { WindowsRegistry registry; error = set_existing(command, registry); }
        }
        if (error) std::cerr << "OpenXR layer operation failed: Win32 " << error << '\n';
        return static_cast<int>(error);
    } catch (...) {
        std::cerr << "OpenXR layer operation failed: Win32 8\n";
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}
