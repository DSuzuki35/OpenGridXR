#include "overlay_transport.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <windows.h>

void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    PROCESS_INFORMATION child{};
    HANDLE readPipe{}, writePipe{};
    og::OverlayChannel channel;
    try {
        check(channel.open("test") && channel.lock(1000), "test channel");
        auto *data = channel.data();
        data->producerPid = 0;
        data->producerBeat = 0;
        data->slots = {};
        data->slots[0].enabled = 1;
        data->slots[0].frames[0].generation = 91;
        data->slots[0].frames[0].frameId = 34;
        channel.unlock();
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        check(CreatePipe(&readPipe, &writePipe, &security, 0), "stdin pipe");
        check(SetHandleInformation(writePipe, HANDLE_FLAG_INHERIT, 0), "private pipe writer");
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        auto host = std::filesystem::path(exe).parent_path() / L"OpenGridXR-OverlayHost.exe";
        std::wstring command = L"\"" + host.wstring() +
                               L"\" --game iracing --test-namespace --parent-pid " +
                               std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = readPipe;
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        check(CreateProcessW(host.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                             nullptr, nullptr, &startup, &child),
              "host starts");
        CloseHandle(readPipe);
        readPipe = nullptr;
        bool started = false;
        for (unsigned i = 0; i < 200 && !started; ++i) {
            if (channel.lock(10)) {
                started = channel.data()->producerPid == child.dwProcessId;
                channel.unlock();
            }
            Sleep(5);
        }
        check(started, "host owns test mapping");
        check(channel.lock(1000), "hold XR metadata lock");
        check(data->slots[0].frames[0].generation == 0,
              "producer restart invalidates old process handles");
        data->slots[0].frames[0].generation = 91;
        std::string commands = "{\"op\":\"clear\",\"slot\":0}\n"
                               "{\"op\":\"configure\",\"slot\":0,\"enabled\":true,\"anchor\":"
                               "\"local\",\"x\":1.25,\"desktopMode\":1,\"desktopX\":-1920,"
                               "\"desktopY\":-200,\"desktopWidth\":1920,\"desktopHeight\":1080}\n"
                               "{\"op\":\"recenter\",\"slot\":0}\n";
        DWORD written{};
        check(WriteFile(writePipe, commands.data(), DWORD(commands.size()), &written, nullptr),
              "send ordered controls while runtime holds lock");
        Sleep(150);
        check(data->slots[0].frames[0].generation == 91 && data->slots[0].recenter == 0,
              "contention fixture keeps commands pending");
        channel.unlock();
        bool applied = false;
        for (unsigned i = 0; i < 200 && !applied; ++i) {
            if (channel.lock(10)) {
                const auto &slot = data->slots[0];
                applied = slot.enabled && slot.anchor == 1 && slot.x == 1.25f &&
                          slot.recenter == 2 && slot.frames[0].generation == 0;
                channel.unlock();
            }
            Sleep(5);
        }
        check(applied, "clear, configure and recenter eventually apply in order after contention");
        check(channel.lock(1000), "desktop metadata lock");
        const auto desktop = data->slots[0];
        channel.unlock();
        check(desktop.desktopMode == 1 && desktop.desktopX == -1920 && desktop.desktopY == -200 &&
                  desktop.desktopWidth == 1920 && desktop.desktopHeight == 1080,
              "desktop physical monitor rectangle survives configure");
        commands="{\"op\":\"clear\",\"slot\":0}\n"
                 "{\"op\":\"configure\",\"slot\":0,\"enabled\":true,\"anchor\":\"local\",\"desktopMode\":1,\"desktopWidth\":1920,\"desktopHeight\":1080}\n"
                 "{\"op\":\"configure\",\"slot\":1,\"enabled\":true,\"anchor\":\"local\"}\n";
        check(WriteFile(writePipe,commands.data(),DWORD(commands.size()),&written,nullptr),
              "show head desktop and ordinary panel");
        applied=false;
        for(unsigned i=0;i<200 && !applied;++i) {
            if(channel.lock(10)) {
                applied=data->slots[0].enabled && data->slots[0].recenter==3 &&
                        data->slots[1].enabled && data->slots[1].recenter==0;
                channel.unlock();
            }
            Sleep(5);
        }
        check(applied,"each desktop reshow recenters while URL panel pose is unchanged");
        const char shutdown[] = "{\"op\":\"shutdown\"}\n";
        WriteFile(writePipe, shutdown, sizeof(shutdown) - 1, &written, nullptr);
        CloseHandle(writePipe);
        writePipe = nullptr;
        check(WaitForSingleObject(child.hProcess, 3000) == WAIT_OBJECT_0, "host exits");
        DWORD code{};
        GetExitCodeProcess(child.hProcess, &code);
        check(code == 0, "host exit success");
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
        std::cout << "Overlay helper ordered control contention regression passed\n";
        return 0;
    } catch (const std::exception &e) {
        if (child.hProcess) {
            TerminateProcess(child.hProcess, 1);
            CloseHandle(child.hThread);
            CloseHandle(child.hProcess);
        }
        if (readPipe)
            CloseHandle(readPipe);
        if (writePipe)
            CloseHandle(writePipe);
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
