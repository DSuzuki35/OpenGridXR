#define DIRECTINPUT_VERSION 0x0800
#include "input_edges.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <dinput.h>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace {
std::string utf8(const wchar_t *value) {
    std::wstring clipped(value);
    if (clipped.size() > 128)
        clipped.resize(128);
    int n = WideCharToMultiByte(CP_UTF8, 0, clipped.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1)
        return "Controller";
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, clipped.c_str(), -1, result.data(), n, nullptr, nullptr);
    result.pop_back();
    return result;
}
std::string quoted(const std::string &value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c < 32)
            out += ' ';
        else
            out += char(c);
    }
    return out + '"';
}
std::string id(const GUID &guid) {
    wchar_t value[40]{};
    StringFromGUID2(guid, value, 40);
    auto text = utf8(value);
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return "dinput:" + text;
}
struct Device {
    ComPtr<IDirectInputDevice8W> api;
    std::string name;
    unsigned buttons{}, povs{};
    og::input::Edges edges;
    bool acquired{};
    ULONGLONG retry{};
    ~Device() {
        if (api)
            api->Unacquire();
    }
};
struct Candidate {
    GUID guid;
    std::string name;
};
BOOL CALLBACK enumerate(const DIDEVICEINSTANCEW *instance, void *pointer) {
    auto &found = *static_cast<std::map<std::string, Candidate> *>(pointer);
    if (found.size() >= 64)
        return DIENUM_STOP;
    found.emplace(id(instance->guidInstance),
                  Candidate{instance->guidInstance, utf8(instance->tszInstanceName)});
    return DIENUM_CONTINUE;
}
void press(const std::string &device, const og::input::Press &event) {
    std::cout << "{\"op\":\"press\",\"deviceId\":" << quoted(device) << ",\"kind\":\""
              << (event.pov ? "pov" : "button") << "\",\"index\":" << event.index;
    if (event.pov)
        std::cout << ",\"direction\":" << event.direction;
    std::cout << "}\n" << std::flush;
}
// Electron supplies a pipe. Checking it here keeps both input and output bounded,
// avoids a blocked reader thread, and notices parent EOF within one polling interval.
bool commands(bool fixture, std::string &line) {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD available{};
    if (GetFileType(input) == FILE_TYPE_PIPE) {
        if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr))
            return false;
        if (!available)
            return true;
    } else if (GetFileType(input) == FILE_TYPE_DISK)
        available = 4096;
    else
        return false;
    char data[4096];
    DWORD read{};
    if (!ReadFile(input, data, std::min<DWORD>(available, sizeof(data)), &read, nullptr) || !read)
        return false;
    for (DWORD i = 0; i < read; ++i) {
        char c = data[i];
        if (c == '\n') {
            line.erase(
                std::remove_if(line.begin(), line.end(),
                               [](unsigned char x) { return x == ' ' || x == '\r' || x == '\t'; }),
                line.end());
            if (line == "{\"op\":\"quit\"}" || line == "quit")
                return false;
            if (fixture && line == "{\"op\":\"fixture-press\"}")
                press("dinput:{00000000-0000-0000-0000-000000000001}", {false, 127, -1});
            line.clear();
        } else {
            if (line.size() >= 256)
                return false;
            line += c;
        }
    }
    return true;
}
int run(bool fixture) {
    if (fixture) {
        std::cout
            << "{\"op\":\"devices\",\"devices\":[{\"id\":\"dinput:{00000000-0000-0000-0000-"
               "000000000001}\",\"name\":\"Synthetic controller\",\"buttons\":128,\"povs\":4}]}\n"
            << std::flush;
        std::string line;
        while (commands(true, line) && std::cout)
            std::this_thread::sleep_for(std::chrono::milliseconds(17));
        return 0;
    }
    auto module = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = module;
    windowClass.lpszClassName = L"OpenGridXRInputWindow";
    if (!RegisterClassW(&windowClass))
        return 2;
    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"OpenGrid XR input", WS_OVERLAPPED,
                                  0, 0, 0, 0, nullptr, nullptr, module, nullptr);
    if (!window)
        return 2;
    ComPtr<IDirectInput8W> input;
    if (FAILED(DirectInput8Create(module, DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                  reinterpret_cast<void **>(input.GetAddressOf()), nullptr))) {
        DestroyWindow(window);
        return 2;
    }
    std::map<std::string, std::unique_ptr<Device>> devices;
    std::string previous, line;
    ULONGLONG nextEnumeration{};
    auto nextPoll = std::chrono::steady_clock::now();
    while (commands(false, line) && std::cout) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        const auto now = GetTickCount64();
        if (now >= nextEnumeration) {
            nextEnumeration = now + 1000;
            std::map<std::string, Candidate> found;
            if (SUCCEEDED(input->EnumDevices(DI8DEVCLASS_GAMECTRL, enumerate, &found,
                                             DIEDFL_ATTACHEDONLY))) {
                for (auto it = devices.begin(); it != devices.end();)
                    if (!found.count(it->first))
                        it = devices.erase(it);
                    else
                        ++it;
                for (const auto &[key, candidate] : found)
                    if (!devices.count(key)) {
                        auto device = std::make_unique<Device>();
                        device->name = candidate.name;
                        DIDEVCAPS caps{};
                        caps.dwSize = sizeof(caps);
                        if (SUCCEEDED(input->CreateDevice(candidate.guid, &device->api, nullptr)) &&
                            SUCCEEDED(device->api->SetDataFormat(&c_dfDIJoystick2)) &&
                            SUCCEEDED(device->api->SetCooperativeLevel(
                                window, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)) &&
                            SUCCEEDED(device->api->GetCapabilities(&caps))) {
                            device->buttons = std::min(caps.dwButtons, DWORD(128));
                            device->povs = std::min(caps.dwPOVs, DWORD(4));
                            devices.emplace(key, std::move(device));
                        }
                    }
                std::string record = "{\"op\":\"devices\",\"devices\":[";
                bool first = true;
                for (const auto &[key, device] : devices) {
                    if (!first)
                        record += ',';
                    first = false;
                    record += "{\"id\":" + quoted(key) + ",\"name\":" + quoted(device->name) +
                              ",\"buttons\":" + std::to_string(device->buttons) +
                              ",\"povs\":" + std::to_string(device->povs) + "}";
                }
                record += "]}";
                if (record != previous) {
                    std::cout << record << '\n' << std::flush;
                    previous = record;
                }
            }
        }
        for (auto &[key, device] : devices) {
            if (!device->acquired) {
                if (now < device->retry)
                    continue;
                device->retry = now + 1000;
                if (FAILED(device->api->Acquire()))
                    continue;
                device->acquired = true;
                device->edges.reset();
            }
            DIJOYSTATE2 state{};
            std::fill(std::begin(state.rgdwPOV), std::end(state.rgdwPOV), DWORD(-1));
            if (FAILED(device->api->Poll()) ||
                FAILED(device->api->GetDeviceState(sizeof(state), &state))) {
                device->acquired = false;
                device->edges.reset();
                device->api->Unacquire();
                device->retry = now + 250;
                continue;
            }
            og::input::Sample sample;
            for (unsigned i = 0; i < device->buttons; ++i)
                sample.buttons[i] = (state.rgbButtons[i] & 0x80) != 0;
            for (unsigned i = 0; i < device->povs; ++i)
                sample.povs[i] = og::input::pov_direction(state.rgdwPOV[i]);
            for (const auto &event : device->edges.sample(sample, device->buttons, device->povs))
                press(key, event);
        }
        nextPoll =
            std::max(nextPoll + std::chrono::microseconds(16667), std::chrono::steady_clock::now());
        std::this_thread::sleep_until(nextPoll);
    }
    devices.clear();
    input.Reset();
    DestroyWindow(window);
    return 0;
}
} // namespace
int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--self-test")
        return og::input::self_test();
    if (argc > 2 || (argc == 2 && std::string(argv[1]) != "--fixture"))
        return 2;
    return run(argc == 2);
}
