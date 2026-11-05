#include "color_control.hpp"
#include "settings.hpp"
#include <algorithm>
#include <cwctype>
#include <sstream>
namespace og {
const char *color_preset_name(ColorPreset preset) {
    switch (preset) {
    case ColorPreset::Sun:
        return "sun";
    case ColorPreset::Night:
        return "night";
    case ColorPreset::Custom:
        return "custom";
    default:
        return "neutral";
    }
}
bool color_shortcut_keys(const std::string &value, std::vector<WORD> &keys) {
    keys.clear();
    std::string text = value;
    for (const auto &mod : std::vector<std::pair<std::string, WORD>>{
             {"Ctrl+", VK_CONTROL}, {"Alt+", VK_MENU}, {"Shift+", VK_SHIFT}})
        if (text.rfind(mod.first, 0) == 0) {
            keys.push_back(mod.second);
            text.erase(0, mod.first.size());
        }
    if (text.size() < 2 || text.size() > 3 || text[0] != 'F' ||
        !std::all_of(text.begin() + 1, text.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return false;
    int number = std::stoi(text.substr(1));
    if (number < 1 || number > 12 || text != "F" + std::to_string(number))
        return false;
    keys.push_back(WORD(VK_F1 + number - 1));
    return true;
}
void ColorRuntime::cycle() {
    if (config_.enabled)
        preset_ = ColorPreset((uint32_t(preset_) + 1) % 4);
}
void ColorRuntime::frame(uint64_t id, bool focused, bool down) {
    // Never change a snapshot after the first view of a frame.
    if (id == frame_)
        return;
    if (sampled_ && down && !down_ && focused)
        cycle();
    down_ = down;
    sampled_ = true;
    frame_ = id;
    frameColor_ = config_.presets[uint32_t(preset_)];
    frameColor_.enabled = config_.enabled;
    if (preset_ == ColorPreset::Neutral)
        frameColor_ = {config_.enabled, 100, 100, 100, 100};
}
bool consume_color_command(ColorShared &d, uint64_t now) {
    if (d.command == d.ack)
        return false;
    d.ack = d.command;
    d.accepted = d.commandSession == d.session && d.commandEpoch == d.focusEpoch &&
                 d.expires >= now && d.running && d.focused && d.supported && d.enabled &&
                 d.heartbeat <= now && now - d.heartbeat < 1500;
    return d.accepted != 0;
}
static bool alive(DWORD pid) {
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    bool value = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return value;
}
ColorChannel::~ColorChannel() {
    if (ownerSession_ && lock(50)) {
        if (data_->session == ownerSession_) {
            data_->running = data_->focused = data_->effectActive = 0;
            data_->ack = data_->command;
            data_->accepted = 0;
            data_->session = 0;
            data_->pid = 0;
        }
        unlock();
    }
    if (data_)
        UnmapViewOfFile(data_);
    if (mapping_)
        CloseHandle(mapping_);
    if (mutex_)
        CloseHandle(mutex_);
}
bool ColorChannel::open(const std::string &game, bool owner, uint64_t session) {
    if (data_)
        return owner ? ownerSession_ == session : true;
    const auto name = L"Local\\OpenGridXR.Color.v1." + std::wstring(game.begin(), game.end());
    mutex_ = owner
                 ? CreateMutexW(nullptr, FALSE, (name + L".Mutex").c_str())
                 : OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (name + L".Mutex").c_str());
    mapping_ = owner ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                          sizeof(ColorShared), name.c_str())
                     : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (!mutex_ || !mapping_)
        return false;
    data_ = static_cast<ColorShared *>(
        MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ColorShared)));
    if (!data_ || !lock(50))
        return false;
    bool ok = data_->magic == colorMagic && data_->version == colorVersion &&
              data_->size == sizeof(ColorShared);
    if (owner) {
        // A second concurrent session may not take over another live producer.
        if (ok && data_->session && alive(data_->pid))
            ok = false;
        else {
            *data_ = ColorShared{};
            data_->pid = GetCurrentProcessId();
            data_->session = session;
            ownerSession_ = session;
            ok = true;
        }
    }
    unlock();
    return ok;
}
bool ColorChannel::lock(DWORD timeout) {
    if (!data_ || !mutex_)
        return false;
    auto r = WaitForSingleObject(mutex_, timeout);
    return r == WAIT_OBJECT_0 || r == WAIT_ABANDONED;
}
void ColorChannel::unlock() { ReleaseMutex(mutex_); }
void ColorChannel::publish(ColorRuntime &runtime, bool running, bool focused, bool supported,
                           bool enabled, bool effect, uint64_t now, bool serviceCommands) {
    if (!focusKnown_ || focused_ != focused) {
        ++focusEpoch_;
        focused_ = focused;
        focusKnown_ = true;
    }
    if (!ownerSession_ || !lock())
        return;
    auto &d = *data_;
    if (d.session == ownerSession_) {
        d.focusEpoch = focusEpoch_;
        d.running = running;
        d.focused = focused;
        d.supported = supported;
        d.enabled = enabled;
        d.heartbeat = now;
        if (serviceCommands && consume_color_command(d, now)) {
            runtime.cycle();
            effect = false; // The new preset has not reached the GPU yet.
        }
        d.activePreset = uint32_t(runtime.preset());
        d.effectActive = effect && enabled && runtime.preset() != ColorPreset::Neutral;
    }
    unlock();
}
static ColorStatus read_status(const ColorShared &d, const std::string &game, uint64_t now) {
    ColorStatus out;
    if (d.magic != colorMagic || d.version != colorVersion || d.size != sizeof(ColorShared) ||
        !d.session || !d.running || d.heartbeat > now || now - d.heartbeat >= 1500) {
        out.error = "No fresh running OpenGridXR color session.";
        return out;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, d.pid);
    wchar_t image[32768]{};
    DWORD length = 32768;
    bool own = process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT &&
               QueryFullProcessImageNameW(process, 0, image, &length) &&
               game_from_executable(image) == game;
    if (process)
        CloseHandle(process);
    if (!own) {
        out.error = "Color session owner does not match the selected sim.";
        return out;
    }
    out.available = true;
    out.supported = d.supported != 0;
    out.enabled = d.enabled != 0;
    out.activePreset = d.activePreset <= 3 ? ColorPreset(d.activePreset) : ColorPreset::Neutral;
    out.effectActive = d.effectActive != 0;
    if (!out.supported)
        out.error = "Color correction requires supported D3D11 projection images.";
    else if (!out.enabled)
        out.error = "Color correction is disabled for this session.";
    else if (!d.focused)
        out.error = "The selected XR session is not focused.";
    return out;
}
ColorStatus color_control(const std::string &game, bool cycle) {
    ColorStatus out;
    if (game != "iracing" && game != "lmu") {
        out.error = "Unsupported game.";
        return out;
    }
    ColorChannel channel;
    if (!channel.open(game) || !channel.lock(25)) {
        out.error = "No running OpenGridXR color session.";
        return out;
    }
    auto &d = *channel.data();
    out = read_status(d, game, GetTickCount64());
    if (!cycle || !out.error.empty()) {
        channel.unlock();
        return out;
    }
    const uint64_t session = d.session, epoch = d.focusEpoch;
    if (d.command != d.ack && d.expires >= GetTickCount64()) {
        channel.unlock();
        out.error = "Another color command is pending.";
        return out;
    }
    const auto command = ++d.command;
    d.commandSession = session;
    d.commandEpoch = epoch;
    d.expires = GetTickCount64() + 500;
    d.accepted = 0;
    channel.unlock();
    const auto deadline = GetTickCount64() + 600;
    while (GetTickCount64() < deadline) {
        Sleep(5);
        if (!channel.lock(5))
            continue;
        if (d.session != session) {
            channel.unlock();
            out.error = "The color session changed before acknowledgement.";
            return out;
        }
        if (d.ack == command) {
            out = read_status(d, game, GetTickCount64());
            if (!d.accepted)
                out.error =
                    "Color cycle rejected: session lost focus, expired, disabled or unsupported.";
            channel.unlock();
            return out;
        }
        channel.unlock();
    }
    if (channel.lock(25)) {
        if (d.session == session && d.command == command && d.ack != command) {
            d.expires = 0;
            d.ack = command;
            d.accepted = 0;
        }
        channel.unlock();
    }
    out.error = "Color cycle timed out; no command will be replayed.";
    return out;
}
std::string color_status_json(const ColorStatus &s) {
    std::ostringstream out;
    out << "{\"available\":" << (s.available ? "true" : "false")
        << ",\"supported\":" << (s.supported ? "true" : "false")
        << ",\"enabled\":" << (s.enabled ? "true" : "false") << ",\"activePreset\":\""
        << color_preset_name(s.activePreset)
        << "\",\"effectActive\":" << (s.effectActive ? "true" : "false") << ",\"error\":\"";
    for (char c : s.error) {
        if (c == '"' || c == '\\')
            out << '\\';
        if (c == '\n')
            out << "\\n";
        else
            out << c;
    }
    out << "\"}";
    return out.str();
}
} // namespace og
