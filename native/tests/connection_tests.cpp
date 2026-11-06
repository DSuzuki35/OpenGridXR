#define OPENGRID_CONNECTION_TEST
#include "../src/connection.cpp"
#include <stdexcept>

void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    using namespace opengrid::connection;
    const auto subkey = L"SOFTWARE\\OpenGridXR-Connection-Test-" + std::to_wstring(GetCurrentProcessId());
    const std::wstring own = L"C:\\Users\\test's space\\runtime\\OpenGridXR.json";
    const std::wstring foreign = L"C:\\Foreign\\layer.json";
    const auto files = std::filesystem::temp_directory_path() / (L"opengrid-connection-test-" + std::to_wstring(GetCurrentProcessId()));
    try {
        check(execute({}) == ERROR_INVALID_PARAMETER, "No implicit operation");
        check(execute({L"--install", L"C:\\foreign"}) == ERROR_INVALID_PARAMETER, "No caller-supplied path");
        check(execute({L"--unknown"}) == ERROR_INVALID_PARAMETER, "No unknown operation");
        check(change_registration(HKEY_CURRENT_USER, subkey.c_str(), foreign, true) == ERROR_INVALID_PARAMETER, "Reject foreign identity");
        const auto first = change_registration(HKEY_CURRENT_USER, subkey.c_str(), own, false);
        if (first) throw std::runtime_error("Remove missing key: Win32 " + std::to_string(first));
        check(change_registration(HKEY_CURRENT_USER, subkey.c_str(), own, true) == 0, "Install into empty key");
        bool present = false, enabled = false;
        check(read_registration(HKEY_CURRENT_USER, subkey.c_str(), own, present, enabled) == 0 && present && enabled, "Structured enabled status");
        check(read_registration(HKEY_CURRENT_USER, subkey.c_str(), foreign, present, enabled) == 0 && !present && !enabled, "Structured missing status");
        check(read_registration(reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(0x1234)), subkey.c_str(), own, present, enabled) != 0, "Read errors are not absence");
        Key key;
        check(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_ALL_ACCESS, &key.value) == 0, "Open fixture");
        const wchar_t untouched[] = L"unrelated string value";
        check(RegSetValueExW(key.value, foreign.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(untouched), sizeof(untouched)) == 0, "Seed foreign value");
        const DWORD disabled = 1;
        check(RegSetValueExW(key.value, own.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&disabled), sizeof(disabled)) == 0, "Disable owned value");
        check(change_registration(HKEY_CURRENT_USER, subkey.c_str(), own, true) == 0, "Repair disabled registration");
        check(change_registration(HKEY_CURRENT_USER, subkey.c_str(), own, true) == 0, "Idempotent install");
        check(change_registration(HKEY_CURRENT_USER, subkey.c_str(), own, false) == 0, "Remove own value");
        check(change_registration(HKEY_CURRENT_USER, subkey.c_str(), own, false) == 0, "Idempotent remove");
        wchar_t actual[64]{}; DWORD size = sizeof(actual), type = 0;
        check(RegQueryValueExW(key.value, foreign.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(actual), &size) == 0 && type == REG_SZ && size == sizeof(untouched) && wcscmp(actual, untouched) == 0, "Foreign value preserved byte-for-byte");
        key.close();
        const auto source = files / L"source", machine = files / L"machine";
        std::filesystem::create_directories(source / L"providers" / L"qvfr");
        for (const auto name : {L"OpenGridXR.dll", L"OpenGridXR-Enhancements.dll", L"OpenGridXR.json", L"OpenGridXR.ini", L"providers\\qvfr\\OpenGridXR-QVFR.dll"})
            std::ofstream(source / name, std::ios::binary) << "fixture";
        check(og::runtime_data_directory(source) == source, "Uninstalled fixtures keep adjacent config");
        check(install_assets(source, machine) == 0, "Install executable assets");
        check(std::filesystem::exists(machine / L"OpenGridXR.dll") && std::filesystem::exists(machine / L"providers" / L"qvfr" / L"OpenGridXR-QVFR.dll"), "Install base and optional provider");
        check(!std::filesystem::exists(machine / L"OpenGridXR.ini"), "Keep mutable settings out of machine files");
        std::filesystem::remove(source / L"OpenGridXR.dll");
        std::filesystem::create_directory(source / L"OpenGridXR.dll");
        check(install_assets(source, machine) == ERROR_ACCESS_DENIED, "Reject a directory masquerading as a source DLL");
        std::filesystem::remove(source / L"OpenGridXR.dll");
        if (CreateSymbolicLinkW((source / L"OpenGridXR.dll").c_str(), (source / L"OpenGridXR-Enhancements.dll").c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
            check(install_assets(source, machine) == ERROR_ACCESS_DENIED, "Reject a redirected source DLL");
            std::filesystem::remove(source / L"OpenGridXR.dll");
        } else {
            check(GetLastError() == ERROR_PRIVILEGE_NOT_HELD, "Unexpected symbolic link setup failure");
            std::cout << "Source symlink fixture unavailable without Developer Mode or symlink privilege\n";
        }
        wchar_t local[32768]{};
        check(GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768) != 0, "User data environment available");
        check(og::runtime_data_directory(machine) == std::filesystem::path(local) / L"OpenGridXR" / L"runtime", "Machine binaries use current user config/artwork/status");
        RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
        std::filesystem::remove_all(files);
        std::cout << "Connection registry tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
        std::error_code ignored; std::filesystem::remove_all(files, ignored);
        std::cerr << error.what() << '\n'; return 1;
    }
}
