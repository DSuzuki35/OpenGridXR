#include "desktop_interaction.hpp"
#include "overlay_protocol.hpp"
#include "overlay_transport.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <d3d11_4.h>
#include <deque>
#include <dxgi1_2.h>
#include <iostream>
#include <mutex>
#include <string_view>
#include <thread>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace {
std::mutex outputMutex;
std::atomic<uint64_t> physicalMouseEpoch{};
LRESULT CALLBACK mouseHook(int code, WPARAM message, LPARAM data) {
    if (code >= 0 && !(reinterpret_cast<MSLLHOOKSTRUCT *>(data)->flags & LLMHF_INJECTED))
        ++physicalMouseEpoch;
    return CallNextHookEx(nullptr, code, message, data);
}
struct Buffer {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<IDXGIKeyedMutex> key;
    HANDLE handle{};
    uint32_t width{}, height{}, format{};
    uint64_t generation{};
    ~Buffer() {
        if (handle)
            CloseHandle(handle);
    }
};
struct Pending {
    ComPtr<ID3D11Texture2D> source;
    ComPtr<ID3D11Query> done;
    uint64_t frame{}, started{};
    unsigned buffer{};
    bool cancelled{};
};
struct StreamPending {
    ComPtr<ID3D11Texture2D> source;
    ComPtr<IDXGIKeyedMutex> sourceKey;
    ComPtr<ID3D11Query> done;
    uint64_t frame{}, sourceGeneration{}, sourceRevision{};
    unsigned buffer{}, sourceReturnKey{};
    bool cancelled{};
};
struct Host {
    og::OverlayChannel channel;
    std::string game;
    DWORD parent{};
    bool testNamespace{};
    bool retryCommand{};
    HANDLE parentProcess{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Device1> device1;
    LUID adapter{};
    std::array<std::array<std::unique_ptr<Buffer>, 2>, og::overlay_count> buffers;
    std::array<std::array<std::unique_ptr<Buffer>, 2>, og::overlay_count> streamBuffers;
    std::array<std::unique_ptr<Pending>, og::overlay_count> pending;
    std::array<std::unique_ptr<StreamPending>, og::overlay_count> streamPending;
    std::array<bool, og::overlay_count> retire{};
    std::array<bool, og::overlay_count> retireStream{};
    std::array<uint64_t, og::overlay_count> streamSourceGeneration{};
    uint64_t generation{GetTickCount64() << 16}, lastStatus{};
    std::array<og::DesktopInputOwnership, og::overlay_count> ownership;
    std::array<std::string, og::overlay_count> inputErrors;
    std::array<og::DesktopPrecision, og::overlay_count> precision;
    std::array<og::DesktopShortcut, og::overlay_count> shortcuts;
    std::array<uint64_t, og::overlay_count> precisionTransition{};
    bool mouseHookAvailable{}, buttonsPending{}, syntheticInputActive{}, internalUiActive{};
    std::array<std::string, og::overlay_count> uiTokens;
    struct UiHover {
        og::DesktopPointer pointer;
        std::string token;
        uint64_t sent{};
        bool active{};
    };
    std::array<UiHover, og::overlay_count> uiHover;
    uint64_t uiSequence{};
    uint64_t lastMove{}, precisionFocus{}, precisionSession{};
    std::string error;
    bool emitUi(unsigned slot, const char *event, const og::DesktopPointer &p,
                const std::string &token) {
        std::lock_guard outputLock(outputMutex);
        // Focus changes deliberately bypass the metadata mutex. Recheck the atomic
        // gate after waiting for stdout as well as at source/command validation.
        if (std::string_view(event) != "leave") {
            const auto &d = *channel.data();
            if (!og::pointer_target_fresh(d, d.slots[slot], GetTickCount64()) ||
                !InterlockedCompareExchange(reinterpret_cast<volatile LONG *>(&channel.data()->inputFocused), 0, 0) ||
                uint64_t(InterlockedCompareExchange64(reinterpret_cast<volatile LONG64 *>(&channel.data()->focusEpoch), 0, 0)) != p.focusEpoch)
                return false;
        }
        std::cout << "{\"op\":\"ui-pointer\",\"slot\":" << slot << ",\"event\":\"" << event
                  << "\",\"u\":" << p.u << ",\"v\":" << p.v << ",\"uiToken\":\"" << token
                  << "\",\"session\":\"" << p.session << "\",\"focusEpoch\":\"" << p.focusEpoch
                  << "\",\"configRevision\":\"" << p.configRevision << "\",\"sourceRevision\":\"" << p.sourceRevision
                  << "\",\"sourceGeneration\":\"" << p.sourceGeneration << "\",\"recenter\":\"" << p.recenter
                  << "\",\"tickMs\":\"" << GetTickCount64() << "\",\"sequence\":\"" << ++uiSequence << "\"}" << std::endl;
        return true;
    }
    void leaveUi(unsigned slot) {
        auto &hover = uiHover[slot];
        if (hover.active) emitUi(slot, "leave", hover.pointer, hover.token);
        hover = {};
    }
    void captureClick(og::OverlayCommand &c) {
        if (c.text("op") != "click" || c.text("uiToken").empty()) return;
        c.receivedTarget = false;
        c.receivedTick = GetTickCount64();
        const auto slot = c.number("slot", -1);
        // Never retry an input receipt without its original focused source snapshot.
        if (slot < 0 || slot >= og::overlay_count || slot != std::floor(slot) || !channel.lock()) return;
        const auto &d = *channel.data();
        const auto &s = d.slots[unsigned(slot)];
        if (s.desktopMode == 2 && c.text("session") == std::to_string(d.session) &&
            c.text("focusEpoch") == std::to_string(d.focusEpoch) &&
            c.text("configRevision") == std::to_string(s.revision) &&
            c.text("sourceRevision") == std::to_string(s.sourceRevision) &&
            c.text("recenter") == std::to_string(s.recenter) &&
            og::pointer_target_fresh(d, s, c.receivedTick) &&
            og::desktop_target_slot(d, c.receivedTick) == int(slot)) {
            c.clickTarget = s.pointer;
            c.receivedTarget = true;
        }
        channel.unlock();
    }
    bool clickUi(const og::OverlayCommand &c, unsigned slot) {
        const auto now = GetTickCount64();
        if (c.text("button") != "left" || !c.receivedTarget || now < c.receivedTick ||
            now - c.receivedTick > 200 || c.text("uiToken").empty() ||
            c.text("uiToken") != uiTokens[slot] || !channel.lock()) return false;
        const auto &d = *channel.data();
        const auto &s = d.slots[slot];
        const bool valid = s.desktopMode == 2 && og::pointer_target_fresh(d, s, now) &&
            c.text("session") == std::to_string(d.session) &&
            c.text("focusEpoch") == std::to_string(d.focusEpoch) &&
            c.text("configRevision") == std::to_string(s.revision) &&
            c.text("sourceRevision") == std::to_string(s.sourceRevision) &&
            c.text("recenter") == std::to_string(s.recenter) &&
            og::pointer_identity_equal(s.pointer, c.clickTarget) &&
            og::desktop_target_slot(d, now) == int(slot);
        const bool emitted = valid && emitUi(slot, "click", s.pointer, uiTokens[slot]);
        channel.unlock();
        return emitted;
    }
    void updateUiInput() {
        if (!internalUiActive) return;
        if (!channel.lock()) return;
        const auto &d = *channel.data();
        const auto now = GetTickCount64();
        const auto target = og::desktop_target_slot(d, now);
        for (unsigned i = 0; i < og::overlay_count; ++i) {
            const auto &s = d.slots[i];
            auto &hover = uiHover[i];
            if (target != int(i) || s.desktopMode != 2 || uiTokens[i].empty()) {
                leaveUi(i);
                continue;
            }
            const auto &p = s.pointer;
            const bool changed = !hover.active || hover.token != uiTokens[i] ||
                !og::pointer_identity_equal(hover.pointer, p) || hover.pointer.u != p.u || hover.pointer.v != p.v;
            if (changed && (!hover.active || now - hover.sent >= 33)) {
                if (emitUi(i, "move", p, uiTokens[i]))
                    hover = {p, uiTokens[i], now, true};
            }
        }
        channel.unlock();
    }
    void syncPrecisionContext(og::OverlayShared &d) {
        if (precisionFocus != d.focusEpoch || precisionSession != d.session) {
            for (unsigned i = 0; i < og::overlay_count; ++i) {
                precision[i].cancel();
                setPrecision(d.slots[i], i, false, GetTickCount64());
            }
            precisionFocus = d.focusEpoch; precisionSession = d.session;
        }
    }
    bool precisionEligible(const og::OverlayShared &d, unsigned slot, uint64_t now) {
        const auto &s = d.slots[slot];
        if (testNamespace || !mouseHookAvailable || ownership[slot].paused() || !d.inputFocused || !s.enabled || s.desktopMode != 1 || s.desktopWidth <= 0 || s.desktopHeight <= 0) return false;
        auto candidate = d;
        // A transition increments the configuration revision, invalidating input until
        // the renderer publishes the enlarged quad's new hit. Keep only the hold alive
        // for that bounded handoff; clicks still require the exact current revision.
        if (s.precisionActive && now >= precisionTransition[slot] && now - precisionTransition[slot] <= 200 &&
            s.pointer.configRevision + 1 == s.revision)
            candidate.slots[slot].pointer.configRevision = s.revision;
        return og::desktop_target_fresh(candidate, candidate.slots[slot], now) &&
               og::desktop_target_slot(candidate, now) == int(slot);
    }
    bool keyboardHeld(unsigned slot) const {
        if (testNamespace || !shortcuts[slot].key) return false;
        const auto down = [](unsigned key) { return (GetAsyncKeyState(int(key)) & 0x8000) != 0; };
        unsigned modifiers = (down(VK_CONTROL) ? 1 : 0) | (down(VK_MENU) ? 2 : 0) | (down(VK_SHIFT) ? 4 : 0);
        return down(shortcuts[slot].key) && modifiers == shortcuts[slot].modifiers;
    }
    void setPrecision(og::OverlaySlot &s, unsigned slot, bool active, uint64_t now) {
        if (bool(s.precisionActive) == active) return;
        if (active) { s.precisionU = s.pointer.u; s.precisionV = s.pointer.v; }
        s.precisionActive = active;
        ++s.revision;
        s.pose.valid = 0;
        precisionTransition[slot] = now;
    }
    ~Host() {
        releaseButtons();
        if (parentProcess)
            CloseHandle(parentProcess);
    }
    void releaseButtons() {
        if (!buttonsPending || testNamespace)
            return;
        INPUT release[2]{};
        for (auto &i : release)
            i.type = INPUT_MOUSE;
        release[0].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        release[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
        if (SendInput(2, release, sizeof(INPUT)) == 2)
            buttonsPending = false;
    }
    bool inject(const og::OverlayShared &d, unsigned slot, const std::string &button = {}) {
        auto &s = d.slots[slot];
        const auto now = GetTickCount64();
        if (s.desktopMode != 1 || !ownership[slot].observe(physicalMouseEpoch.load()) || !mouseHookAvailable ||
            testNamespace ||
            (!og::desktop_target_fresh(d, s, now) || og::desktop_target_slot(d, now) != int(slot)))
            return false;
        int x{}, y{};
        if (!og::desktop_pixel(s.pointer.u, s.pointer.v, s.desktopX, s.desktopY, s.desktopWidth,
                               s.desktopHeight, x, y))
            return false;
        int left = GetSystemMetrics(SM_XVIRTUALSCREEN), top = GetSystemMetrics(SM_YVIRTUALSCREEN),
            width = GetSystemMetrics(SM_CXVIRTUALSCREEN),
            height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        std::array<INPUT, 3> events{};
        const unsigned count =
            og::desktop_input_events(x, y, left, top, width, height, button.c_str(), events);
        if (!channel.lock())
            return false;
        const auto &live = *channel.data();
        const auto &current = live.slots[slot];
        const bool same = live.session == d.session && live.focusEpoch == d.focusEpoch &&
            current.revision == s.revision && current.sourceRevision == s.sourceRevision &&
            current.recenter == s.recenter && current.desktopMode == s.desktopMode &&
            current.pointer.source == s.pointer.source &&
            current.frames[0].generation == s.frames[0].generation &&
            current.frames[1].generation == s.frames[1].generation &&
            og::desktop_target_fresh(live, current, GetTickCount64()) &&
            og::desktop_target_slot(live, GetTickCount64()) == int(slot);
        channel.unlock();
        if (!same)
            return false;
        if (!count ||
            !InterlockedCompareExchange(
                reinterpret_cast<volatile LONG *>(&channel.data()->inputFocused), 0, 0) ||
            uint64_t(InterlockedCompareExchange64(
                reinterpret_cast<volatile LONG64 *>(&channel.data()->focusEpoch), 0, 0)) !=
                s.pointer.focusEpoch)
            return false;
        releaseButtons();
        if (buttonsPending)
            return false;
        buttonsPending = count > 1;
        if (SendInput(count, events.data(), sizeof(INPUT)) != count) {
            inputErrors[slot] = "Windows rejected desktop input";
            releaseButtons();
            return false;
        }
        buttonsPending = false;
        inputErrors[slot].clear();
        return true;
    }
    void updateInput(const og::OverlayShared &d) {
        updateUiInput();
        releaseButtons();
        for (auto &o : ownership)
            o.observe(physicalMouseEpoch.load());
        const auto now = GetTickCount64();
        if (channel.lock()) {
            auto &live = *channel.data();
            syncPrecisionContext(live);
            for (unsigned i = 0; i < og::overlay_count; ++i) {
                const bool active = precision[i].update(now, keyboardHeld(i), precisionEligible(live, i, now));
                setPrecision(live.slots[i], i, active, now);
            }
            channel.unlock();
        }
        if (now - lastMove < 16)
            return;
        // Later composed panels are topmost. Only one explicit source moves the cursor.
        const int slot = og::desktop_target_slot(d, now);
        if (slot >= 0 && !ownership[slot].paused()) {
            inject(d, unsigned(slot));
            lastMove = now;
        }
    }
    void ack(unsigned slot, uint64_t frame, const char *state, const char *reason = "") {
        std::lock_guard outputLock(outputMutex);
        std::cout << "{\"op\":\"ack\",\"slot\":" << slot << ",\"frameId\":" << frame
                  << ",\"status\":\"" << state << "\",\"reason\":\"" << reason << "\"}"
                  << std::endl;
    }
    bool initialize() {
        if (!channel.open(testNamespace ? "test" : game) || !channel.lock(1000))
            return false;
        auto *d = channel.data();
        if (d->producerPid && d->producerPid != GetCurrentProcessId() &&
            GetTickCount64() - d->producerBeat < 3000) {
            channel.unlock();
            return false;
        }
        d->producerPid = GetCurrentProcessId();
        d->producerBeat = GetTickCount64();
        for (auto &slot : d->slots) {
            slot.frames = {};
            slot.current = 0;
            slot.consumedFrame = 0;
            slot.streamFrames = {};
            slot.streamCurrent = 0;
            slot.pose = {};
            slot.pointer = {};
            slot.precisionActive = 0;
            ++slot.sourceRevision;
            slot.state = og::OverlayState::Waiting;
        }
        channel.unlock();
        parentProcess = OpenProcess(PROCESS_DUP_HANDLE | SYNCHRONIZE, FALSE, parent);
        return parentProcess != nullptr;
    }
    bool connect() {
        if (!channel.lock())
            return false;
        auto d = *channel.data();
        channel.unlock();
        if (!d.consumerPid || GetTickCount64() - d.consumerBeat > 3000) {
            error = "No active headset overlay receiver";
            return false;
        }
        if (device && adapter.LowPart == d.adapterLow && adapter.HighPart == d.adapterHigh)
            return true;
        if (std::any_of(pending.begin(), pending.end(), [](auto &p) { return bool(p); }))
            return false;
        if (std::any_of(streamPending.begin(), streamPending.end(),
                        [](auto &p) { return bool(p); }))
            return false;
        if (!channel.lock())
            return false;
        for (auto &slot : channel.data()->slots) {
            slot.frames = {};
            slot.current = 0;
            slot.consumedFrame = 0;
            slot.streamFrames = {};
            slot.streamCurrent = 0;
            slot.pose = {};
            ++slot.sourceRevision;
        }
        channel.unlock();
        buffers = {};
        streamBuffers = {};
        streamSourceGeneration = {};
        device1.Reset();
        context.Reset();
        device.Reset();
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
            return false;
        ComPtr<IDXGIAdapter1> selected;
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> a;
            if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND)
                break;
            DXGI_ADAPTER_DESC1 desc{};
            a->GetDesc1(&desc);
            if (desc.AdapterLuid.LowPart == d.adapterLow &&
                desc.AdapterLuid.HighPart == d.adapterHigh) {
                selected = a;
                adapter = desc.AdapterLuid;
                break;
            }
        }
        if (!selected ||
            FAILED(D3D11CreateDevice(selected.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                     D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                     D3D11_SDK_VERSION, &device, nullptr, &context)) ||
            FAILED(device.As(&device1))) {
            error = "Headset GPU unavailable";
            return false;
        }
        error.clear();
        return true;
    }
    Buffer *buffer(unsigned slot, unsigned index, uint32_t width, uint32_t height, uint32_t format,
                   bool forStream = false) {
        auto &ptr = forStream ? streamBuffers[slot][index] : buffers[slot][index];
        if (ptr && (ptr->width != width || ptr->height != height || ptr->format != format))
            ptr.reset();
        if (!ptr) {
            auto b = std::make_unique<Buffer>();
            D3D11_TEXTURE2D_DESC d{};
            d.Width = width;
            d.Height = height;
            d.ArraySize = d.MipLevels = d.SampleDesc.Count = 1;
            d.Format = format == DXGI_FORMAT_B8G8R8A8_UNORM ? DXGI_FORMAT_B8G8R8A8_TYPELESS
                                                            : DXGI_FORMAT_R8G8B8A8_TYPELESS;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            d.MiscFlags =
                D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
            ComPtr<IDXGIResource1> shared;
            if (FAILED(device->CreateTexture2D(&d, nullptr, &b->texture)) ||
                FAILED(b->texture.As(&shared)) || FAILED(b->texture.As(&b->key)) ||
                FAILED(shared->CreateSharedHandle(
                    nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                    &b->handle)))
                return nullptr;
            b->width = width;
            b->height = height;
            b->format = format;
            b->generation = ++generation;
            ptr = std::move(b);
        }
        return ptr.get();
    }
    void command(const og::OverlayCommand &c) {
        retryCommand = false;
        auto op = c.text("op");
        auto rawSlot = c.number("slot", -2);
        if (rawSlot != std::floor(rawSlot) || rawSlot < ((op == "recenter") ? -1 : 0) ||
            rawSlot >= og::overlay_count) {
            error = "Invalid overlay slot";
            return;
        }
        if (op == "recenter") {
            if (channel.lock()) {
                for (unsigned i = 0; i < og::overlay_count; ++i)
                    if (rawSlot < 0 || i == unsigned(rawSlot)) {
                        leaveUi(i);
                        precision[i].cancel();
                        setPrecision(channel.data()->slots[i], i, false, GetTickCount64());
                        ++channel.data()->slots[i].recenter;
                    }
                channel.unlock();
            } else
                retryCommand = true;
            return;
        }
        unsigned slot = unsigned(rawSlot);
        if (op == "precision") {
            const auto now = GetTickCount64();
            if (!channel.lock()) { retryCommand = true; return; }
            auto &d = *channel.data();
            syncPrecisionContext(d);
            ownership[slot].observe(physicalMouseEpoch.load());
            precision[slot].request(now, c.boolean("held"), uint64_t(c.number("pressId", 0)), precisionEligible(d, slot, now));
            setPrecision(d.slots[slot], slot, precision[slot].update(now, keyboardHeld(slot), precisionEligible(d, slot, now)), now);
            channel.unlock(); return;
        }
        if (op == "click" || op == "resume") {
            // A token-bearing command can only reach the local UI path, including
            // after a slot is reconfigured as an OS desktop source.
            if (op == "click" && !c.text("uiToken").empty()) {
                clickUi(c, slot);
                return;
            }
            if (op == "click" && c.text("button") != "left" && c.text("button") != "right") {
                error = "Invalid desktop click button";
                return;
            }
            if (!channel.lock()) {
                retryCommand = true;
                return;
            }
            const auto d = *channel.data();
            if (op == "resume") {
                precision[slot].cancel();
                setPrecision(channel.data()->slots[slot], slot, false, GetTickCount64());
            }
            channel.unlock();
            if (op == "resume") {
                if (d.slots[slot].enabled && d.slots[slot].desktopMode == 1 &&
                    d.slots[slot].desktopWidth > 0)
                    ownership[slot].resume(physicalMouseEpoch.load());
            } else if (!inject(d, slot, c.text("button")))
                inputErrors[slot] = "Desktop target unavailable, stale or input paused";
            return;
        }
        if (op == "configure" || op == "clear") {
            if (!channel.lock()) {
                retryCommand = true;
                return;
            }
            auto &s = channel.data()->slots[slot];
            leaveUi(slot);
            uiTokens[slot].clear();
            const bool wasEnabled = s.enabled != 0;
            precision[slot].cancel();
            s.precisionActive = 0;
            s.pointer = {};
            releaseButtons();
            ownership[slot].resume(physicalMouseEpoch.load());
            inputErrors[slot].clear();
            if (op == "clear" || !c.boolean("enabled")) {
                s.enabled = 0;
                s.state = og::OverlayState::Disabled;
                s.frames = {};
                s.current = 0;
                s.consumedFrame = 0;
                s.streamFrames = {};
                s.streamCurrent = 0;
                s.pose = {};
                ++s.sourceRevision;
                if (pending[slot])
                    pending[slot]->cancelled = true;
                retire[slot] = true;
                if (streamPending[slot])
                    streamPending[slot]->cancelled = true;
                retireStream[slot] = true;
            } else {
                auto anchor = c.text("anchor", "head");
                if (anchor != "head" && anchor != "local") {
                    channel.unlock();
                    error = "Invalid anchor";
                    return;
                }
                s.enabled = c.boolean("enabled");
                s.anchor = anchor == "local";
                s.x = float(std::clamp(c.number("x", 0), -5., 5.));
                s.y = float(std::clamp(c.number("y", 0), -5., 5.));
                s.z = float(std::clamp(c.number("z", -1.5), -10., -.1));
                s.yaw = float(std::clamp(c.number("yaw", 0), -180., 180.));
                s.pitch = float(std::clamp(c.number("pitch", 0), -180., 180.));
                s.roll = float(std::clamp(c.number("roll", 0), -180., 180.));
                s.widthMeters = float(std::clamp(c.number("widthMeters", 1), .1, 3.));
                s.opacity = float(std::clamp(c.number("opacity", 1), 0., 1.));
                s.desktopMode = c.number("desktopMode", 0) == 1 ? 1 :
                    c.number("desktopMode", 0) == 2 && !c.text("uiToken").empty() ? 2 : 0;
                if (s.desktopMode == 2) uiTokens[slot] = c.text("uiToken");
                if (s.desktopMode == 2) s.anchor = 1;
                s.precisionScale = float(std::clamp(c.number("precisionScale", 1.75), 1., 3.));
                if (!og::desktop_shortcut(c.text("precisionShortcut"), shortcuts[slot])) {
                    shortcuts[slot] = {};
                    error = "Invalid precision shortcut";
                }
                s.desktopX = int32_t(std::clamp(c.number("desktopX", 0), -131072., 131072.));
                s.desktopY = int32_t(std::clamp(c.number("desktopY", 0), -131072., 131072.));
                s.desktopWidth = int32_t(std::clamp(c.number("desktopWidth", 0), 0., 131072.));
                s.desktopHeight = int32_t(std::clamp(c.number("desktopHeight", 0), 0., 131072.));
                if (!wasEnabled && s.desktopWidth > 0 && s.desktopHeight > 0)
                    ++s.recenter;
                s.showInStream = s.desktopMode != 2 && c.boolean("showInStream");
                if (!s.showInStream) {
                    s.streamFrames = {};
                    s.streamCurrent = 0;
                    if (streamPending[slot])
                        streamPending[slot]->cancelled = true;
                    retireStream[slot] = true;
                }
            }
            precision[slot].update(GetTickCount64(), keyboardHeld(slot), false);
            ++s.revision;
            const auto schedule = og::desktop_host_schedule(*channel.data(), false);
            syntheticInputActive = schedule.monitorMouse;
            internalUiActive = schedule.pollUi;
            channel.unlock();
            if (retire[slot] && !pending[slot]) {
                buffers[slot] = {};
                retire[slot] = false;
            }
            if (retireStream[slot] && !streamPending[slot]) {
                streamBuffers[slot] = {};
                streamSourceGeneration[slot] = 0;
                retireStream[slot] = false;
            }
            return;
        }
        auto frameNumber = c.number("frameId", -1);
        if (frameNumber < 0 || frameNumber > 9007199254740991. ||
            frameNumber != std::floor(frameNumber)) {
            error = "Invalid frame id";
            return;
        }
        uint64_t frame = uint64_t(frameNumber);
        if (pending[slot]) {
            ack(slot, frame, "dropped", "busy");
            return;
        }
        auto w = c.number("width", 0), h = c.number("height", 0);
        uint64_t handle{};
        auto format = c.text("format");
        if (w < 1 || w > 1920 || h < 1 || h > 1080 || w != std::floor(w) || h != std::floor(h) ||
            (format != "bgra" && format != "rgba") ||
            !og::overlay_handle(c.text("handle"), handle)) {
            ack(slot, frame, "error", "invalid-frame");
            return;
        }
        if (!connect()) {
            ack(slot, frame, "dropped", "receiver-unavailable");
            return;
        }
        HANDLE imported{};
        if (!DuplicateHandle(parentProcess, reinterpret_cast<HANDLE>(handle), GetCurrentProcess(),
                             &imported, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            ack(slot, frame, "error", "handle-duplicate");
            return;
        }
        auto p = std::make_unique<Pending>();
        auto hr = device1->OpenSharedResource1(imported, IID_PPV_ARGS(&p->source));
        CloseHandle(imported);
        if (FAILED(hr)) {
            error = "Source texture GPU differs from headset GPU or handle unavailable";
            ack(slot, frame, "error", "gpu-import");
            return;
        }
        D3D11_TEXTURE2D_DESC desc{};
        p->source->GetDesc(&desc);
        auto typed = format == "bgra" ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
        if (desc.Width != w || desc.Height != h || desc.ArraySize != 1 || desc.MipLevels != 1 ||
            desc.SampleDesc.Count != 1 ||
            (desc.Format != typed &&
             desc.Format != (format == "bgra" ? DXGI_FORMAT_B8G8R8A8_TYPELESS
                                              : DXGI_FORMAT_R8G8B8A8_TYPELESS))) {
            ack(slot, frame, "error", "texture-description");
            return;
        }
        if (!channel.lock()) {
            ack(slot, frame, "dropped", "metadata-busy");
            return;
        }
        auto &s = channel.data()->slots[slot];
        if (!s.enabled) {
            channel.unlock();
            ack(slot, frame, "dropped", "disabled");
            return;
        }
        auto index = 1 - s.current;
        auto *b = buffer(slot, index, uint32_t(w), uint32_t(h), typed);
        if (!b) {
            channel.unlock();
            ack(slot, frame, "error", "allocation");
            return;
        }
        // This is the older buffer. Reclaim only under the same short metadata lock used by the
        // receiver.
        auto acquired = b->key->AcquireSync(0, 0);
        if (acquired == WAIT_TIMEOUT && b->key->AcquireSync(1, 0) == S_OK) {
            b->key->ReleaseSync(0);
            acquired = b->key->AcquireSync(0, 0);
        }
        if (acquired != S_OK) {
            channel.unlock();
            ack(slot, frame, "dropped", "gpu-busy");
            return;
        }
        s.frames[index] = {};
        channel.unlock();
        D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0};
        if (FAILED(device->CreateQuery(&query, &p->done))) {
            b->key->ReleaseSync(0);
            ack(slot, frame, "error", "gpu-query");
            return;
        }
        context->CopyResource(b->texture.Get(), p->source.Get());
        context->End(p->done.Get());
        context->Flush();
        p->frame = frame;
        p->buffer = index;
        p->started = GetTickCount64();
        pending[slot] = std::move(p);
    }
    bool streamLive() {
        auto *d = channel.data();
        return d->streamConsumerPid && GetTickCount64() - d->streamConsumerBeat < 3000;
    }
    void updateStream(unsigned index) {
        if (streamPending[index]) {
            auto &p = *streamPending[index];
            auto result =
                context->GetData(p.done.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (result == S_FALSE || !channel.lock())
                return;
            auto &slot = channel.data()->slots[index];
            auto &target = *streamBuffers[index][p.buffer];
            p.sourceKey->ReleaseSync(p.sourceReturnKey);
            if (SUCCEEDED(result) && !p.cancelled && slot.enabled && slot.showInStream &&
                slot.sourceRevision == p.sourceRevision && streamLive()) {
                target.key->ReleaseSync(1);
                slot.streamFrames[p.buffer] = {
                    target.generation, reinterpret_cast<uint64_t>(target.handle),
                    p.frame,           target.width,
                    target.height,     target.format};
                slot.streamCurrent = p.buffer;
                streamSourceGeneration[index] = p.sourceGeneration;
            } else
                target.key->ReleaseSync(0);
            channel.unlock();
            streamPending[index].reset();
        }
        if (!channel.lock())
            return;
        auto &slot = channel.data()->slots[index];
        if (!slot.enabled || !slot.showInStream || !streamLive() || retireStream[index]) {
            slot.streamFrames = {};
            slot.streamCurrent = 0;
            channel.unlock();
            streamBuffers[index] = {};
            streamSourceGeneration[index] = 0;
            retireStream[index] = false;
            return;
        }
        const auto current = slot.current < 2 ? slot.current : 0;
        const auto frame = slot.frames[current];
        const auto streamCurrent = slot.streamCurrent < 2 ? slot.streamCurrent : 0;
        if (!frame.generation || !buffers[index][current] ||
            (slot.streamFrames[streamCurrent].frameId == frame.frameId &&
             streamSourceGeneration[index] == frame.generation)) {
            channel.unlock();
            return;
        }
        auto &source = *buffers[index][current];
        const unsigned targetIndex = 1 - streamCurrent;
        auto *target = buffer(index, targetIndex, frame.width, frame.height, frame.format, true);
        if (!target) {
            channel.unlock();
            return;
        }
        // Retained helper-owned pixels make static pages available on selection without a
        // second Chromium paint. Restore the original key so VR ownership is unchanged.
        unsigned sourceKey = 0;
        auto acquired = source.key->AcquireSync(0, 0);
        if (acquired == WAIT_TIMEOUT) {
            sourceKey = 1;
            acquired = source.key->AcquireSync(1, 0);
        }
        if (acquired != S_OK) {
            channel.unlock();
            return;
        }
        auto targetAcquired = target->key->AcquireSync(0, 0);
        if (targetAcquired == WAIT_TIMEOUT && target->key->AcquireSync(1, 0) == S_OK) {
            target->key->ReleaseSync(0);
            targetAcquired = target->key->AcquireSync(0, 0);
        }
        if (targetAcquired != S_OK) {
            source.key->ReleaseSync(sourceKey);
            channel.unlock();
            return;
        }
        auto copy = std::make_unique<StreamPending>();
        D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0};
        if (FAILED(device->CreateQuery(&query, &copy->done))) {
            source.key->ReleaseSync(sourceKey);
            target->key->ReleaseSync(0);
            channel.unlock();
            return;
        }
        copy->source = source.texture;
        copy->sourceKey = source.key;
        copy->sourceReturnKey = sourceKey;
        copy->sourceGeneration = frame.generation;
        copy->sourceRevision = slot.sourceRevision;
        copy->frame = frame.frameId;
        copy->buffer = targetIndex;
        slot.streamFrames[targetIndex] = {};
        channel.unlock();
        context->CopyResource(target->texture.Get(), copy->source.Get());
        context->End(copy->done.Get());
        context->Flush();
        streamPending[index] = std::move(copy);
    }
    void tick() {
        for (unsigned slot = 0; slot < og::overlay_count; ++slot)
            if (pending[slot]) {
                auto &p = *pending[slot];
                auto result =
                    context->GetData(p.done.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
                if (result == S_FALSE) {
                    if (GetTickCount64() - p.started > 2000)
                        error = "GPU copy pending; source retained";
                    continue;
                }
                if (!channel.lock())
                    continue;
                auto &b = *buffers[slot][p.buffer];
                auto &s = channel.data()->slots[slot];
                if (SUCCEEDED(result) && !p.cancelled) {
                    b.key->ReleaseSync(1);
                    s.frames[p.buffer] = {b.generation, reinterpret_cast<uint64_t>(b.handle),
                                          p.frame,      b.width,
                                          b.height,     b.format};
                    s.current = p.buffer;
                } else
                    b.key->ReleaseSync(0);
                channel.unlock();
                ack(slot, p.frame,
                    FAILED(result) ? "error"
                    : p.cancelled  ? "dropped"
                                   : "copied",
                    FAILED(result) ? "gpu-copy" : "");
                pending[slot].reset();
                if (retire[slot]) {
                    buffers[slot] = {};
                    retire[slot] = false;
                }
            }
        for (unsigned slot = 0; slot < og::overlay_count; ++slot)
            updateStream(slot);
        if (channel.lock()) {
            channel.data()->producerBeat = GetTickCount64();
            auto d = *channel.data();
            channel.unlock();
            updateInput(d);
            if (GetTickCount64() - lastStatus >= 500) {
                lastStatus = GetTickCount64();
                bool ready = d.consumerPid && lastStatus - d.consumerBeat < 3000;
                std::lock_guard outputLock(outputMutex);
                std::cout << "{\"op\":\"status\",\"game\":\"" << game
                          << "\",\"available\":" << (ready ? "true" : "false")
                          << ",\"capacity\":" << d.capacity << ",\"streamActive\":"
                          << ((d.streamConsumerPid && lastStatus - d.streamConsumerBeat < 3000)
                                  ? "true"
                                  : "false")
                          << ",\"activeOverlayCount\":" << d.active << ",\"error\":\"" << error
                          << "\",\"slots\":[";
                for (unsigned i = 0; i < og::overlay_count; ++i) {
                    if (i)
                        std::cout << ',';
                    auto &s = d.slots[i];
                    std::cout << "{\"slot\":" << i << ",\"state\":" << int(s.state)
                              << ",\"frameId\":" << s.consumedFrame << ",\"pointerMode\":\""
                              << (s.desktopMode == 2 ? "internalUI" : s.desktopMode == 1 ? "head" : "mouse")
                              << "\",\"inputAvailable\":"
                              << (s.enabled && s.desktopWidth > 0 && s.desktopHeight > 0 &&
                                          (s.desktopMode == 0 ||
                                           (s.desktopMode == 2 && !uiTokens[i].empty() &&
                                            og::pointer_target_fresh(d, s, lastStatus) &&
                                            og::desktop_target_slot(d, lastStatus) == int(i)) ||
                                           (mouseHookAvailable && !testNamespace &&
                                            !ownership[i].paused() &&
                                            og::desktop_target_fresh(d, s, lastStatus) &&
                                            og::desktop_target_slot(d, lastStatus) == int(i)))
                                      ? "true"
                                      : "false")
                              << ",\"inputPaused\":" << (s.desktopMode == 1 && ownership[i].paused() ? "true" : "false")
                              << ",\"inputError\":\""
                              << (s.desktopMode == 0 ? ""
                                  : s.desktopMode == 2 ?
                                      (og::pointer_target_fresh(d, s, lastStatus) &&
                                       og::desktop_target_slot(d, lastStatus) == int(i) ? "" : "No fresh rendered menu target")
                                  : ownership[i].paused()
                                      ? "Physical mouse active; resume desktop input"
                                  : !mouseHookAvailable ? "Mouse takeover monitor unavailable"
                                  : !og::desktop_target_fresh(d, s, lastStatus)
                                      ? "No fresh rendered desktop target"
                                      : inputErrors[i])
                              << "\",\"precisionActive\":" << (s.precisionActive ? "true" : "false") << "}";
                }
                std::cout << "]}" << std::endl;
            }
        }
    }
};
} // namespace
int main(int argc, char **argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Host host;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        if (key == "--test-namespace")
            host.testNamespace = true;
        else if (key == "--game" && i + 1 < argc)
            host.game = argv[++i];
        else if (key == "--parent-pid" && i + 1 < argc)
            host.parent = DWORD(strtoul(argv[++i], nullptr, 10));
        else
            return 2;
    }
    if (!host.parent || !host.initialize()) {
        std::cout << "{\"op\":\"status\",\"available\":false,\"error\":\"Overlay host "
                     "initialization failed\"}"
                  << std::endl;
        return 1;
    }
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<og::OverlayCommand> commands;
    std::atomic<bool> stop{};
    std::thread worker([&] {
        HHOOK hook{};
        bool monitorRequested{};
        auto updateMouseMonitor = [&] {
            if (monitorRequested == host.syntheticInputActive)
                return;
            monitorRequested = host.syntheticInputActive;
            if (hook) {
                UnhookWindowsHookEx(hook);
                hook = nullptr;
            }
            if (monitorRequested && !host.testNamespace)
                hook = SetWindowsHookExW(WH_MOUSE_LL, mouseHook, GetModuleHandleW(nullptr), 0);
            host.mouseHookAvailable = hook != nullptr;
        };
        std::deque<og::OverlayCommand> batch;
        while (!stop) {
            updateMouseMonitor();
            MSG message;
            while (hook && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            {
                std::unique_lock lock(mutex);
                wake.wait_for(
                    lock,
                    std::chrono::milliseconds(og::desktop_host_wait(
                        host.syntheticInputActive,
                        !batch.empty() ||
                            std::any_of(host.pending.begin(), host.pending.end(),
                                        [](auto &p) { return bool(p); }) ||
                            std::any_of(host.streamPending.begin(), host.streamPending.end(),
                                        [](auto &p) { return bool(p); }), host.internalUiActive)),
                    [&] { return stop || (batch.empty() && !commands.empty()); });
                if (batch.empty())
                    batch.swap(commands);
            }
            while (!batch.empty()) {
                host.command(batch.front());
                // Preserve ordering across clear/configure/recenter while the game owns
                // the metadata lock. Only this helper retries; the XR thread never waits.
                if (host.retryCommand)
                    break;
                batch.pop_front();
            }
            updateMouseMonitor();
            host.tick();
            if (WaitForSingleObject(host.parentProcess, 0) == WAIT_OBJECT_0)
                stop = true;
        }
        host.releaseButtons();
        if (hook)
            UnhookWindowsHookEx(hook);
    });
    std::string line;
    char ch{};
    bool oversize = false;
    while (!stop && std::cin.get(ch)) {
        if (ch != '\n') {
            if (line.size() < 4096 && !oversize)
                line += ch;
            else
                oversize = true;
            continue;
        }
        og::OverlayCommand c;
        std::string error;
        if (!oversize && og::parse_overlay_command(line, c, error)) {
            if (c.text("op") == "shutdown")
                break;
            host.captureClick(c);
            std::lock_guard lock(mutex);
            auto drop = [&](const og::OverlayCommand &old) {
                if (old.text("op") == "frame")
                    host.ack(unsigned(old.number("slot", 0)), uint64_t(old.number("frameId", 0)),
                             "dropped", "command-queue-full");
            };
            if (c.text("op") != "frame" && c.text("op") != "click") {
                // Keep the latest state/recenter command per slot. Control cannot be starved by
                // frames.
                for (auto it = commands.begin(); it != commands.end();) {
                    bool sameSlot = it->number("slot", -2) == c.number("slot", -3);
                    bool sameClass = c.text("op") == it->text("op") ||
                                     (c.text("op") == "clear" && it->text("op") == "configure");
                    if (sameSlot && sameClass && it->text("op") != "frame")
                        it = commands.erase(it);
                    else
                        ++it;
                }
            }
            if (commands.size() >= 64) {
                if (c.text("op") == "frame")
                    drop(c);
                else {
                    auto it =
                        std::find_if(commands.begin(), commands.end(), [](const auto &queued) {
                            return queued.text("op") == "frame";
                        });
                    if (it != commands.end()) {
                        drop(*it);
                        commands.erase(it);
                        commands.push_back(std::move(c));
                    }
                }
            } else
                commands.push_back(std::move(c));
        }
        line.clear();
        oversize = false;
        wake.notify_one();
    }
    stop = true;
    wake.notify_one();
    worker.join();
    if (host.channel.lock()) {
        host.channel.data()->producerBeat = 0;
        host.channel.data()->producerPid = 0;
        host.channel.unlock();
    }
    return 0;
}
