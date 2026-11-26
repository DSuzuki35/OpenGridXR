#define DIRECTINPUT_VERSION 0x0800
#include "input_edges.hpp"
#include "../include/steering_input.hpp"
#include "../include/artura_dials.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <dinput.h>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
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
    og::artura::Positions dials;
    ULONGLONG retry{}, nextHeld{};
    struct Axis { DWORD offset{}; LONG minimum{}, maximum{}; bool available{}; };
    std::array<Axis,8> axes{};
    bool axesEnumerated{};
    ULONGLONG nextAxes{},nextAxisCheck{};
    ~Device() {
        if (api)
            api->Unacquire();
    }
};
std::int64_t sampled_at();
constexpr const char *axisNames[]={"x","y","z","rx","ry","rz","slider0","slider1"};
const DWORD axisOffsets[]={DIJOFS_X,DIJOFS_Y,DIJOFS_Z,DIJOFS_RX,DIJOFS_RY,DIJOFS_RZ,DIJOFS_SLIDER(0),DIJOFS_SLIDER(1)};
struct Steering {
    bool configured{},enabled{},discover{};
    unsigned revision{};
    std::string deviceId;
    unsigned axis{};
    double lockDegrees{900};
    bool invert{};
    std::uint32_t token{};
    ULONGLONG discoveryUntil{};
    std::unique_ptr<og::steering::Writer> writer;
    bool discovery() const { return discover && GetTickCount64()<discoveryUntil; }
    bool interested(const std::string &key) const { return discovery() || (enabled && deviceId==key); }
    void invalidate() {if(writer) writer->publish(0,token,false);}
};
BOOL CALLBACK enumerate_axes(const DIDEVICEOBJECTINSTANCEW *object,void *pointer) {
    if(static_cast<std::vector<DWORD> *>(pointer)->size()>=128) return DIENUM_STOP;
    if((DIDFT_GETTYPE(object->dwType)&DIDFT_ABSAXIS)!=0)
        static_cast<std::vector<DWORD> *>(pointer)->push_back(object->dwType);
    return DIENUM_CONTINUE;
}
const std::string fixtureId="dinput:{00000000-0000-0000-0000-000000000001}";
void fixture_devices(const Steering &steering,bool connected=true) {
    std::cout<<"{\"op\":\"devices\",\"devices\":[";
    if(connected) {
        std::cout<<"{\"id\":"<<quoted(fixtureId)<<",\"name\":\"Synthetic controller\",\"buttons\":128,\"povs\":4";
        if(steering.configured) std::cout<<",\"axes\":[\"x\",\"rz\",\"slider0\"]";
        std::cout<<'}';
    }
    std::cout<<"]}\n"<<std::flush;
}
bool configure_steering(const std::string &line,Steering &s,bool fixture) {
    // Parent sends this fixed, bounded JSON schema. Reject reordered/extra fields instead
    // of interpreting arbitrary JSON in the device process.
    static const std::regex pattern(R"cfg(^\{"op":"steering","enabled":(true|false),"discover":(true|false),"deviceId":"((?:dinput:\{[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\})?)","axis":"(x|y|z|rx|ry|rz|slider0|slider1)","lockDegrees":([0-9]{2,4}),"invert":(true|false),"token":([0-9]{1,10})\}$)cfg");
    std::smatch match;
    if(!std::regex_match(line,match,pattern)) {s.invalidate();return false;}
    const bool enabled=match[1]=="true",discover=match[2]=="true",invert=match[6]=="true";
    const auto degrees=std::stoul(match[5].str());
    const auto token=std::stoull(match[7].str());
    const std::string device=match[3].str(),axis=match[4].str();
    if(degrees<90 || degrees>2520 || token>0xffffffffull || (enabled && (device.empty() || !token))) {s.invalidate();return false;}
    unsigned axisIndex=0;while(axisIndex<8 && axis!=axisNames[axisIndex]) ++axisIndex;
    const bool changed=s.enabled!=enabled || s.deviceId!=device || s.axis!=axisIndex ||
        s.lockDegrees!=degrees || s.invert!=invert || s.token!=token;
    if(!s.configured || changed || s.discover!=discover) ++s.revision;
    if(changed) s.invalidate();
    s.configured=true;s.enabled=enabled;s.discover=discover;s.deviceId=device;s.axis=axisIndex;
    s.lockDegrees=degrees;s.invert=invert;s.token=static_cast<std::uint32_t>(token);
    s.discoveryUntil=discover?GetTickCount64()+30000:0;
    if(enabled && !s.writer) {
        const std::wstring name=fixture ? L"Local\\OpenGridXR.SteeringInput.Fixture."+std::to_wstring(GetCurrentProcessId()) : og::steering::MappingName;
        s.writer=std::make_unique<og::steering::Writer>(name.c_str());
        if(!s.writer->available()) {std::cout<<"{\"op\":\"error\"}\n"<<std::flush;return false;}
    }
    if(changed) s.invalidate();
    return true;
}
void read_axis_capabilities(Device &device) {
    std::vector<DWORD> objects;
    device.axes={};
    device.axesEnumerated=true;
    if(FAILED(device.api->EnumObjects(enumerate_axes,&objects,DIDFT_AXIS))) return;
    for(unsigned i=0;i<8;++i) {
        DIDEVICEOBJECTINSTANCEW object{};object.dwSize=sizeof(object);
        if(FAILED(device.api->GetObjectInfo(&object,axisOffsets[i],DIPH_BYOFFSET)) ||
           std::find(objects.begin(),objects.end(),object.dwType)==objects.end()) continue;
        DIPROPRANGE range{};range.diph.dwSize=sizeof(range);range.diph.dwHeaderSize=sizeof(DIPROPHEADER);
        range.diph.dwHow=DIPH_BYID;range.diph.dwObj=object.dwType;
        // Read the client's documented range. Never write calibration/range or FF properties.
        if(SUCCEEDED(device.api->GetProperty(DIPROP_RANGE,&range.diph)) &&
           og::steering::normalize_axis(range.lMin,range.lMin,range.lMax))
            device.axes[i]={axisOffsets[i],range.lMin,range.lMax,true};
    }
    device.axesEnumerated=true;
}
std::string axis_list(const Device &device) {
    std::string result="[";bool first=true;
    for(unsigned i=0;i<8;++i) if(device.axes[i].available) {
        if(!first) result+=',';first=false;result+=quoted(axisNames[i]);
    }
    return result+"]";
}
std::optional<double> read_axis(const DIJOYSTATE2 &state,const Device::Axis &axis) {
    if(!axis.available) return std::nullopt;
    LONG value{};
    std::memcpy(&value,reinterpret_cast<const char *>(&state)+axis.offset,sizeof(value));
    return og::steering::normalize_axis(value,axis.minimum,axis.maximum);
}
void axis_sample(const std::string &key,const Device *device,const DIJOYSTATE2 *state,const Steering &steering) {
    std::cout<<"{\"op\":\"axes\",\"sampledAt\":"<<sampled_at()<<",\"deviceId\":"<<quoted(key)<<",\"axes\":{";
    bool first=true;
    if(device && state) for(unsigned i=0;i<8;++i) {
        if(!steering.discovery() && i!=steering.axis) continue;
        const auto value=read_axis(*state,device->axes[i]);if(!value) continue;
        if(!first) std::cout<<',';first=false;std::cout<<quoted(axisNames[i])<<':'<<*value;
    }
    std::cout<<"}}\n"<<std::flush;
}
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
std::int64_t sampled_at() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
void press(const std::string &device, const og::input::Press &event) {
    std::cout << "{\"op\":\"" << (event.held ? "press" : "release") << "\",\"sampledAt\":" << sampled_at() << ",\"deviceId\":" << quoted(device) << ",\"kind\":\""
              << (event.pov ? "pov" : "button") << "\",\"index\":" << event.index;
    if (event.pov)
        std::cout << ",\"direction\":" << event.direction;
    std::cout << "}\n" << std::flush;
}
void held(const std::string &device, const og::input::Sample &sample, unsigned buttons, unsigned povs) {
    std::cout << "{\"op\":\"held\",\"sampledAt\":" << sampled_at() << ",\"deviceId\":" << quoted(device) << ",\"inputs\":[";
    bool first=true;
    for(unsigned i=0;i<buttons;++i) if(sample.buttons[i]) {
        if(!first) std::cout << ','; first=false;
        std::cout << "{\"kind\":\"button\",\"index\":" << i << '}';
    }
    for(unsigned i=0;i<povs;++i) if(sample.povs[i]>=0 && sample.povs[i]<8) {
        if(!first) std::cout << ','; first=false;
        std::cout << "{\"kind\":\"pov\",\"index\":" << i << ",\"direction\":" << sample.povs[i] << '}';
    }
    std::cout << "]}\n" << std::flush;
}
// Electron supplies a pipe. Checking it here keeps both input and output bounded,
// avoids a blocked reader thread, and notices parent EOF within one polling interval.
bool commands(bool fixture, std::string &line,Steering &steering,bool &fixtureConnected) {
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
            if(line.rfind("{\"op\":\"steering\"",0)==0) {
                if(!configure_steering(line,steering,fixture)) return false;
                if(fixture) fixture_devices(steering,fixtureConnected);
            }
            if(fixture && line=="{\"op\":\"fixture-disconnect\"}") {fixtureConnected=false;steering.invalidate();fixture_devices(steering,false);}
            if(fixture && line=="{\"op\":\"fixture-reconnect\"}") {fixtureConnected=true;fixture_devices(steering);}
            if(fixture && line.rfind("{\"op\":\"fixture-axis\",\"value\":",0)==0 && steering.configured) {
                static const std::regex axisPattern(R"cfg(^\{"op":"fixture-axis","value":(-?(?:0(?:\.[0-9]{1,6})?|1(?:\.0{1,6})?))\}$)cfg");
                std::smatch match;
                if(!std::regex_match(line,match,axisPattern)) return false;
                double value=std::stod(match[1].str());
                if(fixtureConnected && steering.interested(fixtureId))
                    std::cout<<"{\"op\":\"axes\",\"sampledAt\":"<<sampled_at()<<",\"deviceId\":"<<quoted(fixtureId)<<",\"axes\":{\"x\":"<<value<<"}}\n"<<std::flush;
                if(fixtureConnected && steering.enabled && steering.deviceId==fixtureId && steering.axis==0 && steering.writer)
                    steering.writer->publish(og::steering::radians(value,steering.lockDegrees,steering.invert),steering.token,true);
            }
            if (fixture && line == "{\"op\":\"fixture-press\"}") {
                press("dinput:{00000000-0000-0000-0000-000000000001}", {false, 127, -1});
                og::input::Sample sample;sample.buttons[127]=true;
                held("dinput:{00000000-0000-0000-0000-000000000001}", sample, 128, 4);
                press("dinput:{00000000-0000-0000-0000-000000000001}", {false, 127, -1, false});
            }
            line.clear();
        } else {
            if (line.size() >= 512)
                return false;
            line += c;
        }
    }
    return true;
}
int run(bool fixture) {
    Steering steering;
    og::artura::Writer dialWriter;
    bool fixtureConnected=true;
    if (fixture) {
        fixture_devices(steering);
        std::string line;
        while (commands(true, line,steering,fixtureConnected) && std::cout)
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
    unsigned previousRevision{};
    bool previousDiscovery{};
    auto nextPoll = std::chrono::steady_clock::now();
    while (commands(false, line,steering,fixtureConnected) && std::cout) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        const auto now = GetTickCount64();
        bool reportDevices=now>=nextEnumeration || previousRevision!=steering.revision || previousDiscovery!=steering.discovery();
        previousRevision=steering.revision;previousDiscovery=steering.discovery();
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
            }
        }
        // Capability reads happen on demand, including a late discovery request.
        for(auto &[key,device]:devices)
            if(steering.interested(key) && (!device->axesEnumerated || now>=device->nextAxisCheck)) {
                read_axis_capabilities(*device);device->nextAxisCheck=now+1000;reportDevices=true;
            }
        if(reportDevices) {
                std::string record = "{\"op\":\"devices\",\"devices\":[";
                bool first = true;
                for (const auto &[key, device] : devices) {
                    if (!first)
                        record += ',';
                    first = false;
                    record += "{\"id\":" + quoted(key) + ",\"name\":" + quoted(device->name) +
                              ",\"buttons\":" + std::to_string(device->buttons) +
                              ",\"povs\":" + std::to_string(device->povs);
                    if(steering.configured) record+=",\"axes\":"+(steering.interested(key)?axis_list(*device):"[]");
                    record+="}";
                }
                record += "]}";
                if (record != previous) {
                    std::cout << record << '\n' << std::flush;
                    previous = record;
                }
        }
        bool published=false;
        unsigned arturas=0;for(const auto& [key,device]:devices)if(og::artura::device_name(device->name))++arturas;
        if(arturas!=1)for(auto& [key,device]:devices)device->dials.reset();
        og::artura::Positions dialPositions;
        bool dialSampled=false;
        for (auto &[key, device] : devices) {
            const bool artura=arturas==1 && og::artura::device_name(device->name);
            if(steering.configured && !steering.interested(key) && !artura) {
                if(device->acquired) {device->api->Unacquire();device->acquired=false;device->retry=0;}
                continue;
            }
            if (!device->acquired) {
                if (now < device->retry)
                    continue;
                device->retry = now + 1000;
                if (FAILED(device->api->Acquire()))
                    continue;
                device->acquired = true;
                device->edges.reset();
                device->dials.reset();
            }
            DIJOYSTATE2 state{};
            std::fill(std::begin(state.rgdwPOV), std::end(state.rgdwPOV), DWORD(-1));
            if (FAILED(device->api->Poll()) ||
                FAILED(device->api->GetDeviceState(sizeof(state), &state))) {
                if(steering.configured) axis_sample(key,nullptr,nullptr,steering);
                else held(key, {}, device->buttons, device->povs);
                device->acquired = false;
                device->edges.reset();
                device->dials.reset();
                device->api->Unacquire();
                device->retry = now + 250;
                continue;
            }
            if(artura) {
                std::array<bool,128> buttons{};
                for(unsigned i=0;i<device->buttons;++i)buttons[i]=(state.rgbButtons[i]&0x80)!=0;
                device->dials.sample(buttons);dialPositions=device->dials;dialSampled=true;
            }
            if(steering.configured) {
                if(steering.interested(key) && now>=device->nextAxes) {
                    axis_sample(key,device.get(),&state,steering);
                    device->nextAxes=now+(steering.discovery()?100:50);
                }
                if(steering.enabled && key==steering.deviceId) {
                    const auto value=read_axis(state,device->axes[steering.axis]);
                    if(value && steering.writer) {
                        steering.writer->publish(og::steering::radians(*value,steering.lockDegrees,steering.invert),steering.token,true,now);
                        published=true;
                    }
                }
                continue;
            }
            og::input::Sample sample;
            for (unsigned i = 0; i < device->buttons; ++i)
                sample.buttons[i] = (state.rgbButtons[i] & 0x80) != 0;
            for (unsigned i = 0; i < device->povs; ++i)
                sample.povs[i] = og::input::pov_direction(state.rgdwPOV[i]);
            const auto events=device->edges.sample(sample, device->buttons, device->povs);
            for (const auto &event : events)
                press(key, event);
            if(now >= device->nextHeld || !events.empty()) {
                held(key, sample, device->buttons, device->povs);
                device->nextHeld=now+100;
            }
        }
        if(steering.writer && !published) steering.invalidate();
        if(arturas==1 && dialWriter.acquire(now))dialWriter.publish(dialSampled?dialPositions:og::artura::Positions{},now);
        else if(arturas!=1)dialWriter.close();
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
