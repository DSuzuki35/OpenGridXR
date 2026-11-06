#include <windows.h>
#include "color_control.hpp"
#include <algorithm>
#include <cwctype>
#include <iostream>
#include <string>
#include <vector>

// Same grammar as the color shortcut (Ctrl+/Alt+/Shift+ prefixes, F1-F12).
static bool ParseShortcut(const std::string& key, std::vector<WORD>& keys) { return og::color_shortcut_keys(key, keys); }
// Many sims read the keyboard through DirectInput/raw input, which uses scan
// codes. Virtual-key-only SendInput events carry scan code 0 and are ignored there.
static INPUT KeyInput(WORD key, bool up) {
    INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = key;
    input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(key, MAPVK_VK_TO_VSC));
    input.ki.dwFlags = (input.ki.wScan ? KEYEVENTF_SCANCODE : 0) | (up ? KEYEVENTF_KEYUP : 0);
    return input;
}
static bool IsGame(const std::wstring& basename, const std::string& game) {
    if (game == "iracing") return basename == L"iracingsim64dx11.exe" || basename == L"iracingsim64dx11_test.exe";
    return game == "lmu" && (basename == L"le mans ultimate.exe" || basename == L"lemansultimate.exe");
}
static std::wstring ForegroundExecutable(HWND window) {
    DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    wchar_t buffer[32768]; DWORD length = 32768;
    const bool ok = QueryFullProcessImageNameW(process, 0, buffer, &length) != FALSE;
    CloseHandle(process); if (!ok) return {};
    std::wstring result(buffer, length); const auto slash = result.find_last_of(L"\\/");
    if (slash != std::wstring::npos) result.erase(0, slash + 1);
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return result;
}
static int Fail(const char* message) { std::cerr << message << '\n'; return 1; }
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
        std::vector<WORD> keys;
        if (!ParseShortcut("Ctrl+Alt+Shift+F12", keys) || keys.size() != 4 || keys.back() != VK_F12 ||
            ParseShortcut("F0", keys) || ParseShortcut("F13", keys) || ParseShortcut("F01", keys) || ParseShortcut("Alt+Ctrl+F1", keys) ||
            !IsGame(L"iracingsim64dx11.exe", "iracing") || IsGame(L"notepad.exe", "iracing")) return 1;
        for (WORD key : {WORD(VK_CONTROL), WORD(VK_MENU), WORD(VK_SHIFT), WORD(VK_F1), WORD(VK_F12)}) {
            const auto down = KeyInput(key, false), up = KeyInput(key, true);
            if (down.type != INPUT_KEYBOARD || !down.ki.wScan || down.ki.dwFlags != KEYEVENTF_SCANCODE ||
                up.ki.wScan != down.ki.wScan || up.ki.dwFlags != (KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP)) return 1;
        }
        std::cout << "Control validation passed; no input sent.\n"; return 0;
    }
    if (argc >= 2 && (std::string(argv[1]) == "--color-status" || std::string(argv[1]) == "--color-cycle")) {
        og::ColorStatus status;
        const bool cycle = std::string(argv[1]) == "--color-cycle";
        if (argc != 4 || std::string(argv[2]) != "--game") status.error = "Invalid color command.";
        else status = og::color_control(argv[3], cycle);
        std::cout << og::color_status_json(status) << '\n';
        return cycle && !status.error.empty() ? 1 : 0;
    }
    if (argc != 7 || std::string(argv[1]) != "--game" || std::string(argv[3]) != "--key" || std::string(argv[5]) != "--delay-ms") return Fail("Invalid centering command.");
    const std::string game = argv[2], shortcut = argv[4], delayText = argv[6];
    if (game != "iracing" && game != "lmu") return Fail("Unsupported game.");
    std::vector<WORD> keys; if (!ParseShortcut(shortcut, keys)) return Fail("Assign a supported VR recenter key in the game first.");
    if (delayText != "1000" && delayText != "2000" && delayText != "3000" && delayText != "4000" && delayText != "5000") return Fail("Invalid delay.");
    Sleep(static_cast<DWORD>(std::stoi(delayText)));
    const HWND foreground = GetForegroundWindow();
    if (!foreground || !IsGame(ForegroundExecutable(foreground), game)) return Fail("Bring the selected sim to the foreground, then retry. No key was sent.");
    for (int key : {VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN})
        if (GetAsyncKeyState(key) & 0x8000) return Fail("Release modifier keys before centering. No key was sent.");
    if (GetAsyncKeyState(keys.back()) & 0x8000) return Fail("Release the recenter key before retrying.");
    std::vector<INPUT> inputs;
    for (WORD key : keys) inputs.push_back(KeyInput(key, false));
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) inputs.push_back(KeyInput(*it, true));
    if (GetForegroundWindow() != foreground) return Fail("Foreground window changed. No key was sent.");
    if (SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) != inputs.size()) {
        // Release any modifier that a partial insertion may have pressed.
        const auto firstRelease = inputs.begin() + keys.size();
        std::vector<INPUT> releases(firstRelease, inputs.end());
        SendInput(static_cast<UINT>(releases.size()), releases.data(), sizeof(INPUT));
        return Fail("Windows could not send the complete shortcut.");
    }
    std::cout << "Configured recenter shortcut sent to the foreground sim.\n";
    return 0;
}
