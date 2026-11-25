// OpenGridXR-EditInput: keyboard and mouse for the in-headset overlay edit mode (RaceLab-style).
//
// stdin, one JSON object per line:
//   {"op":"arm","on":true}    listen for Tab (start editing) while overlays are showing in a game
//   {"op":"edit","on":true}   editing: Tab, Space, M, Esc and Enter come here instead of the game, and the
//                             mouse moves the overlay (the cursor stays put and no clicks reach the game)
//   {"op":"quit"}
// stdout, one JSON object per line:
//   {"op":"ready"}
//   {"op":"key","key":"tab|space|m|escape|enter"}
//   {"op":"mouse","dx":12,"dy":-3,"wheel":1,"right":false,"shift":false}   (at most ~60 a second)
//   {"op":"error","message":"..."}
// Alt/Ctrl/Win combinations (Alt+Tab...) always pass through, and so does input other programs inject.
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {
std::atomic<bool> armed{false}, editing{false}, rightHeld{false};
std::atomic<long> accX{0}, accY{0}, accWheel{0};
std::mutex outMutex;
DWORD hookThread = 0;
POINT anchor{}, restore{};
HHOOK mouseHook = nullptr;
bool keyDown[256] = {};
constexpr UINT WM_EDIT_CHANGED = WM_APP + 1;

void emit(const std::string &line) {
    std::lock_guard lock(outMutex);
    std::cout << line << std::endl;
}

bool held(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

const char *keyName(DWORD vk) {
    switch (vk) {
    case VK_TAB: return "tab";
    case VK_SPACE: return "space";
    case 'M': return "m";
    case VK_ESCAPE: return "escape";
    case VK_RETURN: return "enter";
    }
    return nullptr;
}

// Which keys the helper takes from the game right now.
bool takes(DWORD vk, bool isArmed, bool isEditing, bool modifierHeld) {
    if (!keyName(vk) || modifierHeld) return false;
    return isEditing || (isArmed && vk == VK_TAB);
}

LRESULT CALLBACK keyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        auto *k = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
        DWORD vk = k->vkCode & 0xFF;
        bool up = (k->flags & LLKHF_UP) != 0;
        bool injected = (k->flags & LLKHF_INJECTED) != 0;
        bool modifier = held(VK_MENU) || held(VK_CONTROL) || held(VK_LWIN) || held(VK_RWIN);
        if (!injected && takes(vk, armed, editing, modifier)) {
            if (!up && !keyDown[vk]) emit(std::string("{\"op\":\"key\",\"key\":\"") + keyName(vk) + "\"}");   // first press only, no auto-repeat
            keyDown[vk] = !up;
            return 1;   // the game never sees it
        }
        if (up) keyDown[vk] = false;
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK mouseProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && editing) {
        auto *m = reinterpret_cast<MSLLHOOKSTRUCT *>(lParam);
        if (m->flags & LLMHF_INJECTED) return CallNextHookEx(nullptr, code, wParam, lParam);
        switch (wParam) {
        case WM_MOUSEMOVE:
            // The cursor is held at the anchor, so movement never runs out at a screen edge.
            accX += m->pt.x - anchor.x;
            accY += m->pt.y - anchor.y;
            return 1;
        case WM_MOUSEWHEEL:
            accWheel += GET_WHEEL_DELTA_WPARAM(m->mouseData);
            return 1;
        case WM_RBUTTONDOWN: rightHeld = true; return 1;
        case WM_RBUTTONUP: rightHeld = false; return 1;
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_MOUSEHWHEEL:
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

int selfTest() {
    bool ok = std::string(keyName(VK_TAB)) == "tab" && std::string(keyName('M')) == "m" && keyName('Q') == nullptr &&
              takes(VK_TAB, true, false, false) && !takes(VK_SPACE, true, false, false) && takes(VK_SPACE, false, true, false) &&
              !takes(VK_TAB, true, true, true) && !takes(VK_TAB, false, false, false);
    std::cout << (ok ? "edit input self-test passed" : "edit input self-test FAILED") << std::endl;
    return ok ? 0 : 1;
}
} // namespace

int main(int argc, char **argv) {
    if (argc > 1 && std::string(argv[1]) == "--self-test") return selfTest();
    hookThread = GetCurrentThreadId();
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);   // give this thread a message queue
    HHOOK keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, GetModuleHandleW(nullptr), 0);
    if (!keyboardHook) {
        emit("{\"op\":\"error\",\"message\":\"Keyboard hook unavailable.\"}");
        return 1;
    }
    emit("{\"op\":\"ready\"}");

    std::thread reader([] {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.size() > 512) continue;
            bool on = line.find("\"on\":true") != std::string::npos;
            if (line.find("\"op\":\"quit\"") != std::string::npos) break;
            if (line.find("\"op\":\"arm\"") != std::string::npos) armed = on;
            if (line.find("\"op\":\"edit\"") != std::string::npos && editing.exchange(on) != on)
                PostThreadMessageW(hookThread, WM_EDIT_CHANGED, on ? 1 : 0, 0);
        }
        // stdin closed (the app quit or crashed): stop, so keys are never left captured
        PostThreadMessageW(hookThread, WM_QUIT, 0, 0);
    });

    UINT_PTR timer = SetTimer(nullptr, 0, 16, nullptr);
    long wheelRest = 0;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_EDIT_CHANGED) {
            if (msg.wParam) {
                GetCursorPos(&restore);
                anchor = restore;
                accX = accY = accWheel = 0;
                wheelRest = 0;
                rightHeld = false;
                if (!mouseHook) mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, GetModuleHandleW(nullptr), 0);
                if (!mouseHook) emit("{\"op\":\"error\",\"message\":\"Mouse hook unavailable.\"}");
            } else {
                if (mouseHook) { UnhookWindowsHookEx(mouseHook); mouseHook = nullptr; }
                SetCursorPos(restore.x, restore.y);
            }
            continue;
        }
        if (msg.message == WM_TIMER) {
            long dx = accX.exchange(0), dy = accY.exchange(0);
            wheelRest += accWheel.exchange(0);
            long notches = wheelRest / WHEEL_DELTA;   // keep partial notches from smooth-scrolling mice
            wheelRest -= notches * WHEEL_DELTA;
            if (editing && (dx || dy || notches)) {
                char buf[200];
                std::snprintf(buf, sizeof buf, "{\"op\":\"mouse\",\"dx\":%ld,\"dy\":%ld,\"wheel\":%ld,\"right\":%s,\"shift\":%s}",
                              dx, dy, notches, rightHeld ? "true" : "false", held(VK_SHIFT) ? "true" : "false");
                emit(buf);
            }
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    KillTimer(nullptr, timer);
    if (mouseHook) UnhookWindowsHookEx(mouseHook);
    UnhookWindowsHookEx(keyboardHook);
    reader.detach();
    return 0;
}
