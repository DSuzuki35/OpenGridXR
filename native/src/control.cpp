#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <iostream>
#include <string>
#include <vector>

static bool ParseShortcut(std::string key, std::vector<WORD>& keys) {
    keys.clear();
    for (const auto& modifier : std::vector<std::pair<std::string, WORD>>{{"Ctrl+", VK_CONTROL}, {"Alt+", VK_MENU}, {"Shift+", VK_SHIFT}}) {
        if (key.rfind(modifier.first, 0) == 0) { keys.push_back(modifier.second); key.erase(0, modifier.first.size()); }
    }
    if (key.size() < 2 || key.size() > 3 || key[0] != 'F') return false;
    if (!std::all_of(key.begin() + 1, key.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
    const int number = std::stoi(key.substr(1));
    if (number < 1 || number > 12 || key != "F" + std::to_string(number)) return false;
    keys.push_back(static_cast<WORD>(VK_F1 + number - 1)); return true;
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
        std::cout << "Control validation passed; no input sent.\n"; return 0;
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
    for (WORD key : keys) { INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = key; inputs.push_back(input); }
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) { INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = *it; input.ki.dwFlags = KEYEVENTF_KEYUP; inputs.push_back(input); }
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
