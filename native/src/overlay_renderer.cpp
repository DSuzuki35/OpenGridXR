#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#ifndef XR_USE_GRAPHICS_API_D3D12
#define XR_USE_GRAPHICS_API_D3D12
#endif
#include "overlay_renderer.hpp"
#include "cockpit_depth.hpp"
#include "dashboard_placement.hpp"
#include "desktop_interaction.hpp"
#include "overlay_transport.hpp"
#include "steering_input.hpp"
#include "session_d3d11on12.hpp"
#include "wheel_model.hpp"
#include "../wheels/mesh_renderer.h"
#include "windows_memory.hpp"
#include "wheel_projection.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <d3d11_4.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <mutex>
#ifdef XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#endif
#include <openxr/openxr_platform.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace og {
namespace {
std::atomic<uint64_t> nextOverlaySession{GetTickCount64() << 16};
XrQuaternionf mul(XrQuaternionf a, XrQuaternionf b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
XrQuaternionf rotation(float yaw, float pitch, float roll) {
    constexpr float d = .00872664626f;
    return mul(mul({0, std::sin(yaw * d), 0, std::cos(yaw * d)},
                   {std::sin(pitch * d), 0, 0, std::cos(pitch * d)}),
               {0, 0, std::sin(roll * d), std::cos(roll * d)});
}
XrPosef compose(XrPosef a, XrPosef b) {
    auto q = mul(mul(a.orientation, {b.position.x, b.position.y, b.position.z, 0}),
                 {-a.orientation.x, -a.orientation.y, -a.orientation.z, a.orientation.w});
    return {mul(a.orientation, b.orientation),
            {a.position.x + q.x, a.position.y + q.y, a.position.z + q.z}};
}
XrPosef relativeTo(const XrPosef &space, XrPosef pose) {
    pose.position.x -= space.position.x;
    pose.position.y -= space.position.y;
    pose.position.z -= space.position.z;
    const auto &q = space.orientation;
    return compose({{-q.x, -q.y, -q.z, q.w}, {}}, pose);
}
constexpr char shader[] = R"(
Texture2D image:register(t0);SamplerState sampler0:register(s0);cbuffer Settings:register(b0){float opacity;float3 pad;};
struct V{float4 position:SV_Position;float2 uv:TEXCOORD0;};
V vs(uint id:SV_VertexID){V o;float2 uv=float2((id<<1)&2,id&2);o.position=float4(uv*float2(2,-2)+float2(-1,1),0,1);o.uv=uv;return o;}
float4 ps(V i):SV_Target{return image.Sample(sampler0,i.uv)*opacity;}
)";
constexpr char maskShader[] = R"(
Texture2D image : register(t0);
Texture2DArray<float> depthImage : register(t1);
SamplerState imageSampler : register(s0);
cbuffer Eye : register(b0) {
    float4 centerOpacity, panelRight, panelUp, fov, depthRect, range;
};
float4 ps(float4 pixel : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 eyePosition = centerOpacity.xyz + (uv.x-.5)*panelRight.xyz + (.5-uv.y)*panelUp.xyz;
    float forward = -eyePosition.z;
    if (forward > .002) {
        float2 tangent = eyePosition.xy / forward;
        float2 coord = float2((tangent.x-fov.x)*fov.z, (fov.y-tangent.y)*fov.w);
        if (all(coord >= 0) && all(coord < 1)) {
            int2 texel = int2(depthRect.xy + floor(coord*depthRect.zw));
            float stored = depthImage.Load(int4(texel, int(panelRight.w), 0));
            if (stored >= range.x && stored <= range.y) {
                float invZ = lerp(range.z, range.w, (stored-range.x)/(range.y-range.x));
                if (invZ * (forward-.002) > 1) return 0;
            }
        }
    }
    return image.SampleLevel(imageSampler, uv, 0) * centerOpacity.w;
}
)";
struct MaskSettings {
    float centerOpacity[4], right[4], up[4], fov[4], rect[4], range[4];
};
struct ImmediateContextScope {
    ID3D11Multithread *guard;
    explicit ImmediateContextScope(ID3D11Multithread *value) : guard(value) { guard->Enter(); }
    ~ImmediateContextScope() { guard->Leave(); }
};
bool usablePose(const XrPosef &p) {
    const auto &q = p.orientation;
    const auto &v = p.position;
    return std::abs(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w - 1.f) < .001f &&
           std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool maskSettings(const CockpitDepthEye &eye, const XrPosef &panel, XrExtent2Df size, float opacity,
                  MaskSettings &out) {
    if (!eye.view || !usablePose(eye.pose) || !usablePose(panel) || eye.rect.offset.x < 0 ||
        eye.rect.offset.y < 0 || eye.rect.extent.width <= 0 || eye.rect.extent.height <= 0 ||
        !(0 <= eye.minDepth && eye.minDepth < eye.maxDepth && eye.maxDepth <= 1) ||
        !(eye.invNear >= 0 && eye.invFar >= 0 && eye.invNear != eye.invFar) ||
        !std::isfinite(eye.invNear) || !std::isfinite(eye.invFar))
        return false;
    const auto &f = eye.fov;
    constexpr float halfPi = 1.570796327f;
    if (!(f.angleLeft > -halfPi && f.angleLeft < f.angleRight && f.angleRight < halfPi &&
          f.angleDown > -halfPi && f.angleDown < f.angleUp && f.angleUp < halfPi))
        return false;
    const float left = std::tan(f.angleLeft), up = std::tan(f.angleUp);
    auto inEye = relativeTo(eye.pose, panel);
    auto center = inEye.position;
    inEye.position = {};
    auto x = compose(inEye, {{0, 0, 0, 1}, {size.width, 0, 0}}).position;
    auto y = compose(inEye, {{0, 0, 0, 1}, {0, size.height, 0}}).position;
    out = {{center.x, center.y, center.z, opacity},
           {x.x, x.y, x.z, float(eye.slice)},
           {y.x, y.y, y.z, 0},
           {left, up, 1.f / (std::tan(f.angleRight) - left), 1.f / (up - std::tan(f.angleDown))},
           {float(eye.rect.offset.x), float(eye.rect.offset.y), float(eye.rect.extent.width),
            float(eye.rect.extent.height)},
           {eye.minDepth, eye.maxDepth, eye.invNear, eye.invFar}};
    return true;
}
struct RuntimeSlot {
    XrSwapchain swap{};
    uint32_t width{}, height{};
    int64_t format{};
    std::vector<ComPtr<ID3D11Texture2D>> images;
    std::vector<ComPtr<ID3D11RenderTargetView>> targets;
    std::array<ComPtr<ID3D11Texture2D>, 2> sources;
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> views;
    std::array<ComPtr<IDXGIKeyedMutex>, 2> keys;
    std::array<uint64_t, 2> generations{};
    uint64_t frame{}, lastGeneration{}, sourceRevision{}, recenter{UINT64_MAX};
    float opacity{-1};
    XrPosef anchor{{0, 0, 0, 1}, {}};
    PlacementAdjustment adjustment;
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    bool ready{}, waited{};
    uint64_t retainUntil{};
    LONG64 presentationEpoch{};
    bool retainedOcclusion{}, retainedWheel{};
    uint32_t acquired{UINT32_MAX};
};
struct MaskSlot {
    RuntimeSlot surface, rightWheel;
    std::array<XrCompositionLayerQuad, 2> eyes{};
    std::array<XrCompositionLayerProjectionView, 2> wheelViews{};
    XrCompositionLayerProjection wheelProjection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    bool wheelUsesProjection{};
    ComPtr<ID3D11Texture2D> page;
    ComPtr<ID3D11ShaderResourceView> pageView;
    uint64_t frame{}, generation{}, retryAt{};
    DXGI_FORMAT sourceFormat{};
};
} // namespace
struct OverlayRenderer::Impl {
    std::mutex mutex;
    std::shared_ptr<OverlayChannel> channel;
#if defined(__cpp_lib_atomic_shared_ptr)
    std::atomic<std::shared_ptr<OverlayChannel>> inputChannel;
#else
    std::shared_ptr<OverlayChannel> inputChannel;
#endif
    std::mutex focusPublication;
    bool channelAttached{};
    uint64_t nextAttach{};
    std::atomic<bool> inputFocused{false};
    std::atomic<uint64_t> focusRevision{};
    OverlayDispatch api;
    XrSession session{};
    XrSpace view{}, local{};
    uint32_t maxLayers{};
    LUID adapterLuid{};
    uint64_t sessionId{};
    uint64_t nextIdleUpdate{};
    ComPtr<ID3D11Device> source11;
    ComPtr<ID3D12Device> source12;
    ComPtr<ID3D12CommandQueue> sourceQueue;
    std::shared_ptr<SessionD3D11On12> bridge;
    std::shared_ptr<D3D11On12Binding> binding11on12;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Device1> device1;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11DeviceContext1> context1;
    ComPtr<ID3D11On12Device> on12;
    ComPtr<ID3DDeviceContextState> drawState;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Buffer> constants;
    std::array<RuntimeSlot, overlay_count> slots;
    std::array<MaskSlot, overlay_count> masks;
    ovrlay::WheelResources<ovrlay::MeshRenderer> wheelWork;
    uint64_t lastWheelRequest{};
    std::wstring wheelDirectory;
    struct WheelFile {
        std::array<char,65> id{};
        std::wstring path,driverPath;
        unsigned kind{},driverState{};
        bool driverFitLimited{};
    };
    std::array<WheelFile,overlay_count> wheelFiles;
    bool wheelPipelineFailed{};
    ComPtr<ID3D11PixelShader> maskPs;
    ComPtr<ID3D11Buffer> maskConstants;
    ComPtr<ID3D11Multithread> maskContextGuard;
    bool maskShaderFailed{}, lmuGame{};
    std::atomic<bool> depthDemand{};
    uint64_t streamDepthUntil{};
    HMODULE lmuModule{};
    LmuCockpitDepthFn lmuDepth{};
    uint64_t lmuRetryAt{};
    telemetry::SteeringInput steering;
    og::steering::Reader assignedSteering;
    bool steeringActive{};
    void stopSteering() {
        assignedSteering.reset();
        if (steeringActive)
            steering.reset();
        steeringActive = false;
    }
    uint32_t producer{};
    HANDLE producerProcess{};
    bool supported{};
    void setAnchor(RuntimeSlot &slot, uint64_t revision, const XrPosef &head) {
        auto forward = spatial_rotate(head.orientation, {0, 0, -1});
        float yaw = std::atan2(-forward.x, -forward.z);
        slot.anchor = {{0, std::sin(yaw / 2), 0, std::cos(yaw / 2)}, head.position};
        slot.recenter = revision;
    }
    bool resolvePose(RuntimeSlot &slot, const OverlaySlot &config, XrTime time, XrPosef &pose) {
        pose = {rotation(config.yaw, config.pitch, config.roll), {config.x, config.y, config.z}};
        if (!config.anchor)
            return true;
        if (slot.recenter != config.recenter) {
            XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
            if (XR_FAILED(api.locate(view, local, time, &location)) ||
                (location.locationFlags & (XR_SPACE_LOCATION_POSITION_VALID_BIT |
                                           XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) !=
                    (XR_SPACE_LOCATION_POSITION_VALID_BIT |
                     XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
                return false;
            setAnchor(slot, config.recenter, location.pose);
        }
        pose = compose(slot.anchor, pose);
        return true;
    }
    DashboardPlacement placement(RuntimeSlot &slot, const OverlaySlot &config, const XrPosef &pose,
                                 XrTime time, uint64_t now) {
        if (config.placementMode == 3) {
            auto fixed = config.placementAhead;
            if (fixed.tick)
                fixed.tick = now;
            return fixed;
        }
        XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
        constexpr auto flags =
            XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if (api.locate(view, local, time, &head) != XR_SUCCESS ||
            (head.locationFlags & flags) != flags || !usablePose(head.pose))
            return {};
        if (slot.recenter != config.recenter)
            setAnchor(slot, config.recenter, head.pose);
        if (!config.placementMode)
            return dashboard_ahead(head.pose, slot.anchor, std::clamp(-config.z, .1f, 10.f),
                                   0, 0, config.widthMeters, now);
        if (config.placementMode == 4) {
            auto reset = dashboard_ahead(head.pose, slot.anchor, 1.f, 0, 0, config.widthMeters, now);
            // Looking behind the cockpit can put the gaze target outside the editable volume.
            if (!reset.tick)
                reset = {0, 0, -1, 0, 0, 0, config.widthMeters, now};
            return reset;
        }
        auto &a = slot.adjustment;
        if (a.token != config.placementToken) {
            a.start(config.placementToken, config.z, config.widthMeters);
            a.panelInHead = config.anchor ? relativeTo(head.pose, pose) : pose;
        }
        if (config.placementMode == 1 && inputFocused.load()) {
            DWORD pid{};
            GetWindowThreadProcessId(GetForegroundWindow(), &pid);
            if (pid == GetCurrentProcessId()) {
                unsigned keys = 0;
                constexpr int virtualKeys[]{VK_LEFT,    VK_RIGHT, VK_UP,   VK_DOWN,
                                            VK_CONTROL, VK_MENU,  VK_SHIFT};
                for (unsigned i = 0; i < 7; ++i)
                    if (GetAsyncKeyState(virtualKeys[i]) & 0x8000)
                        keys |= 1u << i;
                a.nudge(keys, now);
            }
        }
        auto offset = a.panelInHead;
        offset.position.z -= a.distance - a.startingDistance;
        auto panel = relativeTo(slot.anchor, compose(head.pose, offset));
        panel.position.x += a.x;
        panel.position.y += a.y;
        return a.orient(dashboard_placement(panel, a.width, now));
    }
    ~Impl() { reset(); }
    bool clear(RuntimeSlot &s, bool preserveAnchor = false) {
        auto anchor = s.anchor;
        auto recenter = s.recenter;
        auto adjustment = s.adjustment;
        auto sourceRevision = s.sourceRevision;
        if (s.acquired != UINT32_MAX && s.swap) {
            if (!s.waited) {
                XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                wait.timeout = 0;
                auto result = api.wait(s.swap, &wait);
                if (result == XR_TIMEOUT_EXPIRED)
                    return false;
                s.waited = XR_SUCCEEDED(result);
            }
            if (s.waited) {
                XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                api.release(s.swap, &release);
            }
            s.acquired = UINT32_MAX;
            s.waited = false;
        }
        if (s.swap && api.destroy)
            api.destroy(s.swap);
        s = RuntimeSlot{};
        if (preserveAnchor) {
            s.anchor = anchor;
            s.recenter = recenter;
            s.adjustment = adjustment;
            s.sourceRevision = sourceRevision;
        }
        return true;
    }
    bool clearMask(MaskSlot &m) {
        m.pageView.Reset();
        m.page.Reset();
        m.frame = m.generation = m.retryAt = 0;
        const bool left=!m.surface.swap || clear(m.surface);
        const bool right=!m.rightWheel.swap || clear(m.rightWheel);
        return left && right;
    }
    void stopDepth() {
        streamDepthUntil = 0;
        if (depthDemand.exchange(false) && lmuDepth)
            lmuDepth(source11.Get(), session, 0, false, nullptr);
    }
    void releaseGraphics(bool sessionEnding=false) {
        wheelWork.release(sessionEnding);
        wheelPipelineFailed=false;
        if (view && api.destroySpace)
            api.destroySpace(view);
        if (local && api.destroySpace)
            api.destroySpace(local);
        view = local = XR_NULL_HANDLE;
        constants.Reset();
        maskConstants.Reset();
        maskPs.Reset();
        maskContextGuard.Reset();
        maskShaderFailed = false;
        sampler.Reset();
        ps.Reset();
        vs.Reset();
        drawState.Reset();
        context1.Reset();
        context.Reset();
        device1.Reset();
        device.Reset();
        on12.Reset();
        binding11on12.reset();
    }
    void reset() {
        stopDepth();
        lmuDepth = nullptr;
        if (lmuModule)
            FreeLibrary(lmuModule);
        lmuModule = nullptr;
        lmuRetryAt = 0;
        lmuGame = false;
        stopSteering();
        {
            std::lock_guard focusLock(focusPublication);
            inputFocused.store(false);
            ++focusRevision;
#if defined(__cpp_lib_atomic_shared_ptr)
            auto held = inputChannel.exchange({});
#else
            auto held = std::atomic_exchange(&inputChannel, std::shared_ptr<OverlayChannel>{});
#endif
            if (held)
                InterlockedExchange(reinterpret_cast<volatile LONG *>(&held->data()->inputFocused), 0);
        }
        for (auto &s : slots) {
            clear(s);
            s = RuntimeSlot{};
        }
        for (auto &m : masks) {
            clearMask(m);
            m = {};
        }
        releaseGraphics(true);
        bridge.reset();
        source11.Reset();
        source12.Reset();
        sourceQueue.Reset();
        nextIdleUpdate = 0;
        nextAttach = 0;
        channelAttached = false;
        if (channel && channel->lock()) {
            auto *d = channel->data();
            if (d->consumerPid == GetCurrentProcessId()) {
                d->consumerPid = 0;
                d->consumerBeat = 0;
                d->active = 0;
                for (auto &config : d->slots) {
                    config.placementMode = config.cockpitDepthActive = 0;
                    config.placementAhead = {};
                    config.pointer = {};
                    if (config.precisionActive) {
                        config.precisionActive = 0;
                        ++config.revision;
                        config.pose.valid = 0;
                    }
                }
            }
            channel->unlock();
        }
        channel.reset();
        if (producerProcess)
            CloseHandle(producerProcess);
        producerProcess = nullptr;
        producer = 0;
        session = XR_NULL_HANDLE;
        supported = false;
    }
    bool attachChannel() {
        // Publish the current focus epoch and gate together when a late attach
        // meets a focus transition. Neither the frame nor input waits for IPC.
        std::unique_lock focusLock(focusPublication, std::try_to_lock);
        if (!focusLock.owns_lock() || !channel->lock()) return false;
        auto* d=channel->data();
        d->consumerPid=GetCurrentProcessId();
        d->consumerBeat=GetTickCount64();
        d->adapterLow=adapterLuid.LowPart;
        d->adapterHigh=adapterLuid.HighPart;
        d->session=sessionId;
        d->active=0;
        for (auto& config : d->slots) {
            config.placementMode = 0;
            config.placementAhead = {};
            config.pointer.valid=0;
            if (config.precisionActive) {
                config.precisionActive=0;
                ++config.revision;
                config.pose.valid=0;
            }
        }
        d->capacity=maxLayers>0 ? std::min(overlay_count,maxLayers-1) : 0;
        InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&d->focusEpoch), LONG64(focusRevision.load()));
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&d->inputFocused), inputFocused.load()?1:0);
#if defined(__cpp_lib_atomic_shared_ptr)
        inputChannel.store(channel);
#else
        std::atomic_store(&inputChannel,channel);
#endif
        channelAttached=true;
        channel->unlock();
        return true;
    }
    bool retainBinding(const void *chain) {
        for (auto *n = static_cast<const XrBaseInStructure *>(chain); n; n = n->next) {
            if (n->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
                source11 = reinterpret_cast<const XrGraphicsBindingD3D11KHR *>(n)->device;
                ComPtr<IDXGIDevice> dx;
                ComPtr<IDXGIAdapter> adapter;
                DXGI_ADAPTER_DESC desc{};
                if (!source11 || FAILED(source11.As(&dx)) || FAILED(dx->GetAdapter(&adapter)) ||
                    FAILED(adapter->GetDesc(&desc)))
                    return false;
                adapterLuid = desc.AdapterLuid;
                return true;
            }
            if (n->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR) {
                auto *b = reinterpret_cast<const XrGraphicsBindingD3D12KHR *>(n);
                if (!b->device || !b->queue)
                    return false;
                source12 = b->device;
                sourceQueue = b->queue;
                adapterLuid = source12->GetAdapterLuid();
                return true;
            }
        }
        return false;
    }
    bool graphics() {
        if (constants && view && local)
            return true;
        releaseGraphics();
        if (source11) {
            device = source11;
            device->GetImmediateContext(&context);
        } else if (source12 && sourceQueue) {
            if (!bridge) bridge = std::make_shared<SessionD3D11On12>();
            binding11on12 = bridge->acquire(source12.Get(), sourceQueue.Get());
            if (!binding11on12)
                return false;
            device = binding11on12->device;
            context = binding11on12->context;
            on12 = binding11on12->on12;
        }
        if (!device || FAILED(device.As(&device1)) || FAILED(context.As(&context1)))
            return false;
        auto level = device->GetFeatureLevel();
        UINT flags = (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED)
                         ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                         : 0;
        if (FAILED(device1->CreateDeviceContextState(flags, &level, 1, D3D11_SDK_VERSION,
                                                     __uuidof(ID3D11Device), nullptr, &drawState)))
            return false;
        ComPtr<ID3DBlob> v, p;
        if (FAILED(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "vs", "vs_5_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &v, nullptr)) ||
            FAILED(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "ps", "ps_5_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &p, nullptr)))
            return false;
        if (FAILED(device->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), nullptr,
                                              &vs)) ||
            FAILED(
                device->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), nullptr, &ps)))
            return false;
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 16;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateSamplerState(&sd, &sampler)) ||
            FAILED(device->CreateBuffer(&bd, nullptr, &constants)))
            return false;
        XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        space.poseInReferenceSpace.orientation.w = 1;
        space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        if (XR_FAILED(api.createSpace(session, &space, &view)))
            return false;
        space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        return XR_SUCCEEDED(api.createSpace(session, &space, &local));
    }
    bool create(RuntimeSlot &s, uint32_t width, uint32_t height) {
        if (!clear(s, true))
            return false;
        uint32_t count{};
        if (XR_FAILED(api.formats(session, 0, &count, nullptr)) || count > 256)
            return false;
        std::vector<int64_t> formats(count);
        if (XR_FAILED(api.formats(session, count, &count, formats.data())))
            return false;
        for (auto f : {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                       DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM})
            if (std::find(formats.begin(), formats.end(), f) != formats.end()) {
                s.format = f;
                break;
            }
        if (!s.format)
            return false;
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags =
            XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        info.format = s.format;
        info.width = width;
        info.height = height;
        info.arraySize = info.faceCount = info.mipCount = info.sampleCount = 1;
        if (XR_FAILED(api.create(session, &info, &s.swap)) ||
            XR_FAILED(api.images(s.swap, 0, &count, nullptr)) || !count || count > 16) {
            clear(s);
            return false;
        }
        s.images.resize(count);
        s.targets.resize(count);
        if (on12) {
            std::vector<XrSwapchainImageD3D12KHR> images(count,
                                                         {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
            if (XR_FAILED(
                    api.images(s.swap, count, &count,
                               reinterpret_cast<XrSwapchainImageBaseHeader *>(images.data())))) {
                clear(s);
                return false;
            }
            D3D11_RESOURCE_FLAGS flags{};
            flags.BindFlags = D3D11_BIND_RENDER_TARGET;
            for (unsigned i = 0; i < count; ++i)
                if (FAILED(on12->CreateWrappedResource(
                        images[i].texture, &flags, D3D12_RESOURCE_STATE_RENDER_TARGET,
                        D3D12_RESOURCE_STATE_RENDER_TARGET, IID_PPV_ARGS(&s.images[i])))) {
                    clear(s);
                    return false;
                }
        } else {
            std::vector<XrSwapchainImageD3D11KHR> images(count,
                                                         {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
            if (XR_FAILED(
                    api.images(s.swap, count, &count,
                               reinterpret_cast<XrSwapchainImageBaseHeader *>(images.data())))) {
                clear(s);
                return false;
            }
            for (unsigned i = 0; i < count; ++i)
                s.images[i] = images[i].texture;
        }
        D3D11_RENDER_TARGET_VIEW_DESC rtv{};
        rtv.Format = DXGI_FORMAT(s.format);
        rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        for (unsigned i = 0; i < count; ++i)
            if (FAILED(device->CreateRenderTargetView(s.images[i].Get(), &rtv, &s.targets[i]))) {
                clear(s);
                return false;
            }
        s.width = width;
        s.height = height;
        s.quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        s.quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        s.quad.subImage.swapchain = s.swap;
        s.quad.subImage.imageRect = {{0, 0}, {int32_t(width), int32_t(height)}};
        return true;
    }
    bool source(RuntimeSlot &s, const OverlayFrame &frame, unsigned buffer) {
        if (s.generations[buffer] == frame.generation && s.sources[buffer])
            return true;
        s.sources[buffer].Reset();
        s.views[buffer].Reset();
        s.keys[buffer].Reset();
        HANDLE handle{};
        if (!producerProcess ||
            !DuplicateHandle(producerProcess, reinterpret_cast<HANDLE>(frame.handle),
                             GetCurrentProcess(), &handle, 0, FALSE, DUPLICATE_SAME_ACCESS))
            return false;
        auto result = device1->OpenSharedResource1(handle, IID_PPV_ARGS(&s.sources[buffer]));
        CloseHandle(handle);
        if (FAILED(result) || FAILED(s.sources[buffer].As(&s.keys[buffer])))
            return false;
        D3D11_TEXTURE2D_DESC desc{};
        s.sources[buffer]->GetDesc(&desc);
        if (desc.Width != frame.width || desc.Height != frame.height || desc.ArraySize != 1 ||
            desc.MipLevels != 1 || desc.SampleDesc.Count != 1)
            return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = frame.format == DXGI_FORMAT_B8G8R8A8_UNORM ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
                                                                : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        if (FAILED(
                device->CreateShaderResourceView(s.sources[buffer].Get(), &srv, &s.views[buffer])))
            return false;
        s.generations[buffer] = frame.generation;
        return true;
    }
    bool acquireTarget(RuntimeSlot &s) {
        if (s.acquired == UINT32_MAX) {
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            if (XR_FAILED(api.acquire(s.swap, &acquire, &s.acquired)))
                return false;
            s.waited = false;
        }
        if (!s.waited) {
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wait.timeout = 0;
            auto r = api.wait(s.swap, &wait);
            if (r == XR_TIMEOUT_EXPIRED)
                return false;
            if (XR_FAILED(r) || s.acquired >= s.images.size())
                return false;
            s.waited = true;
        }
        return true;
    }
    bool releaseTarget(RuntimeSlot &s) {
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if (XR_FAILED(api.release(s.swap, &release)))
            return false;
        s.acquired = UINT32_MAX;
        s.waited = false;
        return true;
    }
    bool upload(RuntimeSlot &s, unsigned buffer, float opacity) {
        if (!acquireTarget(s))
            return false;
        ComPtr<ID3DDeviceContextState> previous;
        context1->SwapDeviceContextState(drawState.Get(), &previous);
        ID3D11Resource *target = s.images[s.acquired].Get();
        if (on12)
            on12->AcquireWrappedResources(&target, 1);
        float settings[]{opacity, 0, 0, 0};
        context->UpdateSubresource(constants.Get(), 0, nullptr, settings, 0, 0);
        auto *rt = s.targets[s.acquired].Get();
        context->OMSetRenderTargets(1, &rt, nullptr);
        D3D11_VIEWPORT viewport{0, 0, float(s.width), float(s.height), 0, 1};
        context->RSSetViewports(1, &viewport);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(), nullptr, 0);
        context->PSSetShader(ps.Get(), nullptr, 0);
        auto *view = s.views[buffer].Get();
        auto *sample = sampler.Get();
        auto *cb = constants.Get();
        context->PSSetShaderResources(0, 1, &view);
        context->PSSetSamplers(0, 1, &sample);
        context->PSSetConstantBuffers(0, 1, &cb);
        context->Draw(3, 0);
        ID3D11ShaderResourceView *empty{};
        context->PSSetShaderResources(0, 1, &empty);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        if (on12) {
            on12->ReleaseWrappedResources(&target, 1);
            context->Flush();
        }
        context1->SwapDeviceContextState(previous.Get(), nullptr);
        return releaseTarget(s);
    }
    bool maskPrograms() {
        if (maskShaderFailed || maskPs)
            return maskPs != nullptr;
        ComPtr<ID3DBlob> code;
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = sizeof(MaskSettings);
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        maskShaderFailed =
            FAILED(context.As(&maskContextGuard)) ||
            FAILED(D3DCompile(maskShader, sizeof(maskShader), nullptr, nullptr, nullptr, "ps",
                              "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, nullptr)) ||
            FAILED(device->CreateBuffer(&cb, nullptr, &maskConstants)) ||
            FAILED(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(),
                                             nullptr, &maskPs));
        return !maskShaderFailed;
    }
    // The keyed producer texture may be rewritten on its next paint. Keep a private
    // copy only for dashboards being masked, and update it only on a new web frame.
    bool cachePage(MaskSlot &m, RuntimeSlot &s, unsigned b, const OverlayFrame &frame) {
        if (!maskPrograms())
            return false;
        if (m.sourceFormat != DXGI_FORMAT(frame.format)) {
            m.pageView.Reset();
            m.page.Reset();
            m.sourceFormat = DXGI_FORMAT(frame.format);
        }
        if (!m.page) {
            D3D11_TEXTURE2D_DESC desc{};
            s.sources[b]->GetDesc(&desc);
            desc.MiscFlags = desc.CPUAccessFlags = 0;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
            s.views[b]->GetDesc(&viewDesc);
            if (FAILED(device->CreateTexture2D(&desc, nullptr, &m.page)) ||
                FAILED(device->CreateShaderResourceView(m.page.Get(), &viewDesc, &m.pageView))) {
                m.page.Reset();
                m.retryAt = GetTickCount64() + 1000;
                return false;
            }
        }
        // CopyResource honors the game's current predicate. Use our clean context state.
        ImmediateContextScope lock(maskContextGuard.Get());
        ComPtr<ID3DDeviceContextState> previous;
        context1->SwapDeviceContextState(drawState.Get(), &previous);
        context->CopyResource(m.page.Get(), s.sources[b].Get());
        context1->SwapDeviceContextState(previous.Get(), nullptr);
        m.frame = frame.frameId;
        m.generation = frame.generation;
        return true;
    }
    bool masked(MaskSlot &m, const RuntimeSlot &plain, const CockpitDepthFrame &depth,
                const XrPosef &panel, float opacity) {
        if (!m.pageView || m.frame != plain.frame || m.generation != plain.lastGeneration ||
            GetTickCount64() < m.retryAt || !maskPrograms())
            return false;
        MaskSettings settings[2]{};
        for (unsigned eye = 0; eye < 2; ++eye)
            if (!maskSettings(depth.eyes[eye], panel, plain.quad.size, opacity, settings[eye]))
                return false;
        auto &s = m.surface;
        // Pack both eye images in one runtime surface to acquire/wait/release only once.
        if (!s.swap && !create(s, plain.width * 2, plain.height)) {
            m.retryAt = GetTickCount64() + 1000;
            return false;
        }
        if (!acquireTarget(s))
            return false;
        {
            ImmediateContextScope lock(maskContextGuard.Get());
            ComPtr<ID3DDeviceContextState> previous;
            context1->SwapDeviceContextState(drawState.Get(), &previous);
            auto *target = s.targets[s.acquired].Get();
            context->OMSetRenderTargets(1, &target, nullptr);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(vs.Get(), nullptr, 0);
            context->PSSetShader(maskPs.Get(), nullptr, 0);
            context->PSSetSamplers(0, 1, sampler.GetAddressOf());
            context->PSSetConstantBuffers(0, 1, maskConstants.GetAddressOf());
            for (unsigned eye = 0; eye < 2; ++eye) {
                D3D11_VIEWPORT viewport{float(eye * plain.width), 0, float(plain.width),
                                        float(plain.height),      0, 1};
                context->RSSetViewports(1, &viewport);
                context->UpdateSubresource(maskConstants.Get(), 0, nullptr, &settings[eye], 0, 0);
                ID3D11ShaderResourceView *inputs[]{m.pageView.Get(), depth.eyes[eye].view};
                context->PSSetShaderResources(0, 2, inputs);
                context->Draw(3, 0);
                auto &quad = m.eyes[eye];
                quad = plain.quad;
                quad.eyeVisibility = eye ? XR_EYE_VISIBILITY_RIGHT : XR_EYE_VISIBILITY_LEFT;
                quad.subImage.swapchain = s.swap;
                quad.subImage.imageRect.offset.x = int32_t(eye * plain.width);
            }
            ID3D11ShaderResourceView *empty[2]{};
            context->PSSetShaderResources(0, 2, empty);
            context->OMSetRenderTargets(0, nullptr, nullptr);
            context1->SwapDeviceContextState(previous.Get(), nullptr);
        }
        return releaseTarget(s);
    }
    bool wheel(unsigned slotIndex,const OverlaySlot& config,const XrFrameEndInfo& info,
               const CockpitDepthFrame* depth, ID3D11ShaderResourceView* sceneProbe,float steeringRadians) {
        if(config.wheelDevice>6) return false;
        auto& file=wheelFiles[slotIndex];
        if(file.id!=config.wheelModel) {
            const auto end=std::find(config.wheelModel.begin(),config.wheelModel.end(),'\0');
            const std::string_view id(config.wheelModel.data(),end-config.wheelModel.begin());
            if(!wheel_model_id(id)) return false;
            if(wheelDirectory.empty()) {
                wchar_t localData[32768];
                auto size=GetEnvironmentVariableW(L"LOCALAPPDATA",localData,32768);
                if(!size || size>=32768) return false;
                wheelDirectory=std::wstring(localData)+L"\\OpenGridXR\\wheels\\";
            }
            file.path=wheelDirectory;
            file.path.append(id.begin(),id.end());
            const auto driverModel=wheel_driver_model(id);
            file.driverPath=driverModel.empty()?L"":wheelDirectory+std::wstring(driverModel.begin(),driverModel.end())+L".agd";
            file.path+=L".agm";
            file.kind=wheel_kind(id);
            file.id=config.wheelModel;
        }
        // Shader compilation and immutable model uploads stay off the XR frame thread.
        if(wheelWork.retiring)return false;
        auto& wheels=wheelWork.renderer;
        auto& wheelPipeline=wheelWork.pipeline;
        if(!wheels) {
            if(wheelPipelineFailed) return false;
            try {
                if(!wheelPipeline.valid()) {
                    if(device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) {
                        wheelPipelineFailed=true;return false;
                    }
                    wheelPipeline=std::async(std::launch::async,[retained=device] {
                        auto pipeline=std::make_unique<ovrlay::MeshRenderer>();
                        if(!pipeline->initializePipeline(retained.Get())) pipeline.reset();
                        return pipeline;
                    });
                    return false;
                }
                if(wheelPipeline.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return false;
                wheels=wheelPipeline.get();
                wheelPipelineFailed=!wheels;
            } catch(...) {wheelPipelineFailed=true;}
            if(!wheels)return false;
        }
        if(!wheels->selectModel(device.Get(),slotIndex,file.path,file.kind,config.wheelDevice)) return false;

        const auto* projection=wheel_projection_source(info);
        if(!projection && !depth) return false;
        auto& plain=slots[slotIndex].quad;
        auto pose=plain.pose;
        auto eyeSpace=depth?depth->space:projection->space;
        if(eyeSpace!=plain.space) {
            XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
            constexpr auto valid=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
            if(XR_FAILED(api.locate(plain.space,eyeSpace,info.displayTime,&location)) ||
               (location.locationFlags&valid)!=valid || !usablePose(location.pose)) return false;
            pose=compose(location.pose,pose);
        }
        auto inverse=[](XrPosef transform) {
            auto q=transform.orientation;
            transform.orientation={-q.x,-q.y,-q.z,q.w};
            auto v=transform.position;
            transform.position=spatial_rotate(transform.orientation,{-v.x,-v.y,-v.z});
            return transform;
        };
        const bool driverRequested=config.driverEnabled && !file.driverPath.empty() &&
            config.pivotX*config.pivotX+config.pivotY*config.pivotY+config.pivotZ*config.pivotZ<=.00250001f &&
            std::abs(config.steeringTilt)<=10.f;
        file.driverState=0;
        file.driverFitLimited=false;
        if(driverRequested) {
            const XrPosef identity{{0,0,0,1},{0,0,0}};
            const auto back=inverse(steered_dashboard(identity,{config.pivotX,config.pivotY,config.pivotZ},
                                                       config.steeringTilt,steeringRadians));
            const auto x=spatial_rotate(back.orientation,{1,0,0}),y=spatial_rotate(back.orientation,{0,1,0}),
                       z=spatial_rotate(back.orientation,{0,0,1});
            const float rows[]={x.x,y.x,z.x,back.position.x,x.y,y.y,z.y,back.position.y,x.z,y.z,z.z,back.position.z};
            ovrlay::DriverFit fit;
            fit.seatHeight=config.driverSeatHeight;fit.seatDistance=config.driverSeatDistance;
            fit.shoulderWidth=config.driverShoulderWidth;fit.torsoLean=config.driverTorsoLean;
            fit.legLength=config.driverLegLength;fit.kneeHeight=config.driverKneeHeight;
            const bool ready=wheels->selectDriver(device.Get(),slotIndex,file.driverPath,steeringRadians,rows,config.driverGloveTheme,
                                 config.driverSuitTheme,config.driverTorso!=0,config.driverLegs!=0,fit,config.widthMeters,
                                 {config.pivotX,config.pivotY,config.pivotZ,config.steeringTilt});
            file.driverFitLimited=wheels->driverFitLimited();
            file.driverState=ready?2u:file.driverFitLimited?4u:wheels->driverFailed(slotIndex)?3u:1u;
        } else wheels->forgetDriver(slotIndex);
        ovrlay::MeshEyeParameters params[2]{};
        auto& mask=masks[slotIndex];
        // Stereo projection layers carry one explicit subimage per eye, avoiding
        // runtimes that ignore eyeVisibility on the two wheel quad layers.
        // Keep the existing path when the session uses a four-view projection.
        mask.wheelUsesProjection = projection && projection->viewCount == 2;
        // Hands and arms cover more of the eye than the wheel alone. Keep a
        // bounded texel budget without shrinking the controls to a few pixels.
        const bool driverReady=file.driverState==2u;
        const unsigned pixels=driverReady?1536u:std::clamp(slots[slotIndex].width,256u,1024u);
        for(unsigned eye=0;eye<2;eye++) {
            const auto& eyePose=depth?depth->eyes[eye].pose:projection->views[eye].pose;
            if(!usablePose(eyePose))return false;
            auto modelToEye=compose(inverse(eyePose),pose);
            auto eyeLocal=inverse(modelToEye).position;
            auto& p=params[eye];
            p.eyeLocal[0]=eyeLocal.x;p.eyeLocal[1]=eyeLocal.y;p.eyeLocal[2]=eyeLocal.z;
            p.model[0]=plain.size.width;p.model[1]=config.opacity;
            p.clip[3]=config.wheelScreen && mask.pageView?1.f:0.f;
            if(depth) {
                MaskSettings settings{};
                if(!maskSettings(depth->eyes[eye],pose,plain.size,config.opacity,settings))return false;
                auto& e=depth->eyes[eye];
                p.clip[2]=1;p.model[2]=.002f;p.model[3]=float(e.slice);
                p.fov[0]=std::tan(e.fov.angleLeft);p.fov[1]=std::tan(e.fov.angleRight);
                p.fov[2]=std::tan(e.fov.angleUp);p.fov[3]=std::tan(e.fov.angleDown);
                std::copy_n(settings.rect,4,p.rectangle);std::copy_n(settings.range,4,p.depth);
                const auto q=modelToEye.orientation;
                const auto x=spatial_rotate(q,{1,0,0}),y=spatial_rotate(q,{0,1,0}),z=spatial_rotate(q,{0,0,1});
                float rows[]={x.x,y.x,z.x,modelToEye.position.x,x.y,y.y,z.y,modelToEye.position.y,
                              x.z,y.z,z.z,modelToEye.position.z};
                std::copy_n(rows,12,p.modelToEye);
            }
            float visiblePlane[]={-4,-4,4,4};
            if(driverReady) {
                const auto& fov=depth?depth->eyes[eye].fov:projection->views[eye].fov;
                const auto rotation=inverse(modelToEye).orientation;
                float lo[]={4.f,4.f},hi[]={-4.f,-4.f};bool bounded=true;
                for(float x:{std::tan(fov.angleLeft),std::tan(fov.angleRight)})
                    for(float y:{std::tan(fov.angleDown),std::tan(fov.angleUp)}) {
                        const auto ray=spatial_rotate(rotation,{x,y,-1});
                        if(ray.z>=-.001f){bounded=false;continue;}
                        const float t=-eyeLocal.z/ray.z;
                        const float q[]={eyeLocal.x+t*ray.x,eyeLocal.y+t*ray.y};
                        for(unsigned a=0;a<2;a++) {
                            lo[a]=std::min(lo[a],q[a]);hi[a]=std::max(hi[a],q[a]);
                        }
                    }
                if(bounded)for(unsigned a=0;a<2;a++){
                    visiblePlane[a]=std::clamp(lo[a],-4.f,4.f);
                    visiblePlane[a+2]=std::clamp(hi[a],-4.f,4.f);
                }
            }
            if(!wheels->aperture(p,visiblePlane)) return false;
            auto& surface=eye?mask.rightWheel:mask.surface;
            if((!surface.swap || surface.width!=pixels || surface.height!=pixels) && !create(surface,pixels,pixels))return false;
        }
        ovrlay::ModelLightsFrame colors{};
        colors.count=96;colors.sampled_tick_ms=config.wheelLightTick;
        for(unsigned j=0;j<96;j++)std::copy_n(config.wheelRgb.begin()+3*j,3,colors.rgba[j]);
        wheels->setLights(config.wheelLights!=0,colors,GetTickCount64());
        for(unsigned eye=0;eye<2;eye++) {
            auto& surface=eye?mask.rightWheel:mask.surface;
            if(!acquireTarget(surface))return false;
            auto* texture=surface.images[surface.acquired].Get();
            ID3D11Resource* target=texture;
            if(on12)on12->AcquireWrappedResources(&target,1);
            bool drawn=wheels->render(device.Get(),context.Get(),texture,DXGI_FORMAT(surface.format),
                depth?depth->eyes[eye].view:nullptr,params[eye],surface.targets[surface.acquired].Get(),
                mask.pageView.Get(),eye,sceneProbe,config.wheelAmbientLight);
            if(on12){on12->ReleaseWrappedResources(&target,1);context->Flush();}
            if(!releaseTarget(surface) || !drawn)return false;
            auto& quad=mask.eyes[eye];
            quad=surface.quad;quad.space=plain.space;
            quad.layerFlags|=XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
            quad.eyeVisibility=eye?XR_EYE_VISIBILITY_RIGHT:XR_EYE_VISIBILITY_LEFT;
            const auto& a=params[eye].aperture;
            quad.pose=compose(plain.pose,{{0,0,0,1},{a[0],a[1],0}});
            quad.size={a[2],a[3]};
            if (mask.wheelUsesProjection) {
                const auto &p = params[eye];
                const XrVector3f eyeLocal{p.eyeLocal[0], p.eyeLocal[1], p.eyeLocal[2]};
                mask.wheelViews[eye] = wheel_projection_view(
                    compose(plain.pose, {{0, 0, 0, 1}, eyeLocal}), eyeLocal,
                    p.aperture, quad.subImage);
            }
        }
        mask.wheelProjection.space = plain.space;
        mask.wheelProjection.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                                         XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
        mask.wheelProjection.viewCount = uint32_t(mask.wheelViews.size());
        mask.wheelProjection.views = mask.wheelViews.data();
        return true;
    }
    bool maskDashboard(unsigned i, const CockpitDepthFrame &depth, XrTime time) {
        auto &s = slots[i];
        auto pose = s.quad.pose;
        if (s.quad.space != depth.space) {
            XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
            constexpr auto valid =
                XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            if (XR_FAILED(api.locate(s.quad.space, depth.space, time, &location)) ||
                (location.locationFlags & valid) != valid || !usablePose(location.pose))
                return false;
            pose = compose(location.pose, pose);
        }
        return masked(masks[i], s, depth, pose, s.opacity);
    }
};
OverlayRenderer::OverlayRenderer() : impl_(std::make_unique<Impl>()) {}
OverlayRenderer::~OverlayRenderer() = default;
bool OverlayRenderer::initialize(XrSession session, const void *binding, uint32_t maxLayers,
                                 const OverlayDispatch &api, const std::string &game,
                                 std::shared_ptr<SessionD3D11On12> bridge) {
    auto &p = *impl_;
    std::lock_guard lock(p.mutex);
    p.reset();
    p.bridge = std::move(bridge);
    p.api = api;
    p.session = session;
    p.lmuGame = game == "lmu";
    p.maxLayers = maxLayers;
    if (!p.retainBinding(binding))
        return false;
    p.channel = std::make_shared<OverlayChannel>();
    if (!p.channel->open(game, 0) && !p.channel->pending())
        return false;
    p.sessionId = ++nextOverlaySession;
    if (p.channel->data()) p.attachChannel();
    p.supported = true;
    return true;
}
void OverlayRenderer::setInputFocused(bool focused) {
    auto &p = *impl_;
    std::lock_guard focusLock(p.focusPublication);
#if defined(__cpp_lib_atomic_shared_ptr)
    auto channel = p.inputChannel.load();
#else
    auto channel = std::atomic_load(&p.inputChannel);
#endif
    // This path must also work while append owns its internal or IPC mutex.
    // Clear the global gate first; returning focus requires a newly published hit.
    if (channel) {
        if (!focused)
            channel->data()->invalidatePresentation();
        InterlockedExchange(reinterpret_cast<volatile LONG *>(&channel->data()->inputFocused), 0);
    }
    p.inputFocused.store(false);
    const auto epoch = ++p.focusRevision;
    if (channel)
        InterlockedExchange64(reinterpret_cast<volatile LONG64 *>(&channel->data()->focusEpoch),
                              LONG64(epoch));
    if (!focused && channel)
        for (auto &s : channel->data()->slots) {
            InterlockedExchange(reinterpret_cast<volatile LONG *>(&s.placementMode), 0);
            if (InterlockedExchange(reinterpret_cast<volatile LONG *>(&s.precisionActive), 0)) {
                InterlockedIncrement64(reinterpret_cast<volatile LONG64 *>(&s.revision));
                InterlockedExchange(reinterpret_cast<volatile LONG *>(&s.pose.valid), 0);
            }
        }
    p.inputFocused.store(focused);
    if (channel)
        InterlockedExchange(reinterpret_cast<volatile LONG *>(&channel->data()->inputFocused),
                            focused ? 1 : 0);
}
void OverlayRenderer::reset() {
    auto &p = *impl_;
    std::lock_guard lock(p.mutex);
    p.reset();
}
bool OverlayRenderer::wantsCockpitDepth() const {
    return impl_->depthDemand.load(std::memory_order_relaxed);
}
void OverlayRenderer::append(const XrFrameEndInfo &info, bool enabled, bool shouldRender,
                             std::vector<const XrCompositionLayerBaseHeader *> &layers,
                             const CockpitDepthFrame *depth, DashboardLayerOrder order,
                             ID3D11ShaderResourceView *sceneProbe) {
    auto &p = *impl_;
    std::unique_lock lock(p.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return;
    if(p.wheelWork.retiring) p.wheelWork.release();
    if (!p.supported || !p.channel)
        return;
    if (!p.channelAttached) {
        if (!enabled) return;
        const auto now=GetTickCount64();
        if (now<p.nextAttach) return;
        p.nextAttach=now+250;
        if (!p.channel->ready(now) || !p.attachChannel()) return;
    }
    const auto focusRevision = p.focusRevision.load();
    if (!enabled || !shouldRender || !info.layerCount) {
        p.stopDepth();
        p.stopSteering();
        for (auto &s : p.slots)
            s.retainUntil = 0;
    }
    // Keep IPC discoverable for hot enable without a per-frame kernel mutex
    // operation when the module is disabled and owns no graphics resources.
    if (!enabled && !p.device && GetTickCount64() < p.nextIdleUpdate)
        return;
    auto *d = p.channel->data();
    size_t visibleSlots = 0;
    const auto *projection = wheel_projection_source(info);
    const bool stereoWheels = projection && projection->viewCount == 2;
    bool requested = false, wheelRequested=false, projectionDepthRequested=false;
    const bool liveMetadata = p.channel->lock();
    auto canRetain = [&](const RuntimeSlot &s) {
        return s.ready && s.retainUntil > GetTickCount64() &&
               s.presentationEpoch == d->publishedPresentationEpoch();
    };
    if (!liveMetadata) {
        if (!enabled || !shouldRender || !info.layerCount)
            return;
        for (const auto &s : p.slots) {
            if (canRetain(s)) {
                ++visibleSlots;
                requested |= s.retainedOcclusion;
                projectionDepthRequested |= s.retainedOcclusion && s.retainedWheel && stereoWheels;
            }
        }
        if (!visibleSlots)
            return;
    } else {
        d->consumerPid = GetCurrentProcessId();
        d->adapterLow = p.adapterLuid.LowPart;
        d->adapterHigh = p.adapterLuid.HighPart;
        d->session = p.sessionId;
        d->consumerBeat = GetTickCount64();
        d->capacity = p.maxLayers > layers.size()
                          ? uint32_t(std::min<size_t>(overlay_count, p.maxLayers - layers.size()))
                          : 0;
        d->active = 0;
        const auto now = GetTickCount64();
        const bool streamActive = d->streamConsumerPid && now - d->streamConsumerBeat < 3000;
        p.streamDepthUntil = 0;
        for (auto &config : d->slots) {
            if (config.enabled && config.frames[config.current < 2 ? config.current : 0].generation) {
                ++visibleSlots;
                wheelRequested|=config.wheelModel[0]!=0;
                requested |= config.cockpitOcclusion && config.opacity > 0;
                projectionDepthRequested |= config.cockpitOcclusion && config.opacity > 0 &&
                                            config.wheelModel[0] && stereoWheels;
                if (streamActive && config.webDashboard && config.showInStream &&
                    config.cockpitOcclusion && config.opacity > 0)
                    p.streamDepthUntil = now + 200;
            }
            if (!enabled || !shouldRender || !info.layerCount || !config.enabled ||
                !config.cockpitOcclusion || !d->producerPid ||
                GetTickCount64() - d->producerBeat > 3000)
                config.cockpitDepthActive = 0;
            // The last presented hit remains usable while this frame is prepared.
            // Invalidating it here would make the worker cancel a precision hold
            // merely because it polled between frame preparation and publication.
            if (!enabled || !shouldRender || !info.layerCount || !d->producerPid ||
                GetTickCount64() - d->producerBeat > 3000)
                config.pointer.valid = 0;
            if (config.precisionActive && (!enabled || !shouldRender || !info.layerCount)) {
                config.precisionActive = 0;
                ++config.revision;
                config.pose.valid = 0;
            }
        }
        if (!enabled || !d->producerPid || GetTickCount64() - d->producerBeat > 3000) {
            for (auto &config : d->slots)
                config.pose.valid = 0;
            p.channel->unlock();
            p.stopDepth();
            p.stopSteering();
            bool cleared = true;
            for (auto &m : p.masks)
                cleared = p.clearMask(m) && cleared;
            for (auto &slot : p.slots)
                if (slot.swap || slot.ready)
                    cleared = p.clear(slot, true) && cleared;
            if (cleared) {
                p.releaseGraphics();
                if (!enabled)
                    p.nextIdleUpdate = GetTickCount64() + 250;
            }
            return;
        }
        // A runtime can temporarily suspend rendering. Keep the consumed texture and its
        // key ownership so a static page resumes without requiring another Chromium paint.
        if (!shouldRender || !info.layerCount) {
            const auto configs = d->slots;
            p.channel->unlock();
            for (unsigned i = 0; i < overlay_count; ++i) {
                auto &slot = p.slots[i];
                if (!configs[i].enabled || slot.sourceRevision != configs[i].sourceRevision) {
                    if (p.clear(slot, true))
                        slot.sourceRevision = configs[i].sourceRevision;
                }
            }
            return;
        }
        if (p.producer != d->producerPid) {
            if (p.producerProcess)
                CloseHandle(p.producerProcess);
            p.producer = d->producerPid;
            p.producerProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, p.producer);
            for (auto &s : p.slots) {
                s.generations = {};
                s.adjustment = {};
                s.lastGeneration = 0;
                s.ready = false;
                s.retainUntil = 0;
            }
            for (auto &m : p.masks)
                m.generation = 0;
        }
        p.channel->unlock();
    }
    // A brief visibility toggle can reuse the pipeline and immutable mesh.
    // Disconnect and device teardown still release everything immediately.
    if(wheelRequested) p.lastWheelRequest=GetTickCount64();
    const bool keepHiddenWheel = p.lastWheelRequest && GetTickCount64()-p.lastWheelRequest < 10000;
    if(liveMetadata && !wheelRequested && !keepHiddenWheel) {
        p.wheelWork.release();
        p.wheelPipelineFailed=false;
    }
    // Preserve the layer budget for ordinary dashboards before spending a second
    // composition layer on either eye. Stereo projection wheels need only one layer.
    size_t spare =
        p.maxLayers > layers.size() + visibleSlots ? p.maxLayers - layers.size() - visibleSlots : 0;
    // Stream composition has its own render target and consumes no XR layers.
    const bool demand = ((requested && spare) ||
                         (projectionDepthRequested && layers.size() < p.maxLayers) ||
                         GetTickCount64() < p.streamDepthUntil) && p.source11;
    CockpitDepthFrame nativeDepth{};
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> nativeRefs;
    if (demand) {
        p.depthDemand = true;
        if (p.lmuGame) {
            if (!p.lmuDepth && GetTickCount64() >= p.lmuRetryAt) {
                p.lmuRetryAt = GetTickCount64() + 2000;
                if (!p.lmuModule)
                    GetModuleHandleExW(0, L"OpenGridXR.dll", &p.lmuModule);
                if (p.lmuModule)
                    p.lmuDepth = reinterpret_cast<LmuCockpitDepthFn>(
                        GetProcAddress(p.lmuModule, "OpenGridLmuCockpitDepthV1"));
            }
            if (p.lmuDepth &&
                p.lmuDepth(p.source11.Get(), p.session, info.displayTime, true, &nativeDepth)) {
                for (unsigned i = 0; i < 2; ++i)
                    nativeRefs[i].Attach(nativeDepth.eyes[i].view);
                depth = &nativeDepth;
            }
        }
    } else {
        p.stopDepth();
    }
    if (!demand || !depth || !depth->space || depth->time != info.displayTime ||
        !depth->eyes[0].view || !depth->eyes[1].view)
        depth = nullptr;
    bool created = false;
    std::array<bool, overlay_count> admitted{};
    std::array<DesktopPointer, overlay_count> pointers{};
    std::array<XrPosef, overlay_count> rays{};
    std::array<float, overlay_count> opacities{};
    std::optional<double> telemetryAngle;
    bool readWheel = false, readAssignedWheel = false;
    XrSpaceLocation inputHead{XR_TYPE_SPACE_LOCATION};
    bool inputHeadSampled{}, inputHeadLocated{}, interactive{}, retainedDashboard{};
    for (unsigned i = 0; i < overlay_count; ++i) {
        std::optional<double> wheelAngle;
        auto &slot = p.slots[i];
        auto &mask = p.masks[i];
        auto submit = [&](bool dashboard, bool stereo, bool wheel) {
            if (stereo) {
                if (wheel && mask.wheelUsesProjection)
                    order.insert(layers, dashboard,
                        reinterpret_cast<const XrCompositionLayerBaseHeader *>(&mask.wheelProjection));
                else {
                    for (auto &eye : mask.eyes)
                        order.insert(layers, dashboard, eye);
                    --spare;
                }
            } else {
                order.insert(layers, dashboard, slot.quad);
            }
        };
        // A paint, stream copy or status poll can hold the host mutex. A recently
        // presented web image is private to our swapchain. Only the control epoch
        // is read without the mutex, and depth is recomputed for this display time.
        auto lockForSlot = [&](std::optional<bool> preparedMask = {}) {
            if (liveMetadata && p.channel->lock())
                return true;
            if (!order.helmetAtLayerLimit && layers.size() < p.maxLayers && canRetain(slot)) {
                if(slot.retainedWheel && (mask.wheelUsesProjection != stereoWheels ||
                   (!mask.wheelUsesProjection && (!spare || layers.size()+1>=p.maxLayers)))) return false;
                const bool stereo = slot.retainedWheel || (preparedMask ? *preparedMask
                    : depth && spare && layers.size() + 1 < p.maxLayers &&
                          slot.retainedOcclusion && p.maskDashboard(i, *depth, info.displayTime));
                if (canRetain(slot)) {
                    submit(true, stereo, slot.retainedWheel);
                    retainedDashboard = true;
                    admitted[i] = true;
                    opacities[i] = slot.opacity;
                }
            }
            return false;
        };
        if (!lockForSlot())
            continue;
        auto &sdata = d->slots[i];
        auto config = sdata;
        const auto configEpoch = d->publishedPresentationEpoch();
        const bool isWheel=config.wheelModel[0]!=0;
        if(p.wheelWork.renderer && (wheelRequested || !keepHiddenWheel) && (!isWheel || !config.enabled))p.wheelWork.renderer->forgetModel(i);
        if (((!config.cockpitOcclusion && !isWheel) || !config.enabled || config.opacity <= 0 ||
             slot.sourceRevision != config.sourceRevision) &&
            (mask.page || mask.surface.swap)) {
            p.channel->unlock();
            if (!p.clearMask(mask) || !lockForSlot())
                continue;
        }
        if (slot.sourceRevision != config.sourceRevision) {
            p.channel->unlock();
            if (!p.clear(slot, true))
                continue;
            slot.sourceRevision = config.sourceRevision;
            if (!lockForSlot())
                continue;
        }
        if (!config.enabled) {
            sdata.state = OverlayState::Disabled;
            p.channel->unlock();
            if (slot.swap || slot.ready)
                p.clear(slot, true);
            continue;
        }
        auto frame = config.frames[config.current < 2 ? config.current : 0];
        unsigned buffer = config.current < 2 ? config.current : 0;
        if (!frame.generation || frame.width < 1 || frame.width > 1920 || frame.height < 1 ||
            frame.height > 1080) {
            sdata.state = OverlayState::Waiting;
            p.channel->unlock();
            if (slot.swap || slot.ready)
                p.clear(slot, true);
            continue;
        }
        // Publish the exact resolved VR placement. Base raw-frame capture transforms this
        // pose at that image's display time, so generated-frame camera poses cannot leak in.
        p.channel->unlock();
        if (!p.graphics()) {
            p.stopDepth();
            p.releaseGraphics();
            p.supported = false;
            return;
        }
        XrPosef pose{};
        if (!p.resolvePose(slot, config, info.displayTime, pose))
            continue;
        XrSpace panelSpace = config.anchor ? p.local : p.view;
        const auto now = GetTickCount64();
        DashboardPlacement ahead{};
        if (config.webDashboard && (config.placementMode || config.previewUntil > now))
            ahead = p.placement(slot, config, pose, info.displayTime, now);
        if (ahead.tick && config.placementMode) {
            pose = compose(slot.anchor, {rotation(ahead.yaw, ahead.pitch, ahead.roll),
                                         {ahead.x, ahead.y, ahead.z}});
            panelSpace = p.local;
        }
        // Persist the wheel's neutral mounting pose and apply the live steering
        // transform to the displayed dashboard throughout the grab and release.
        if (config.webDashboard && config.steeringFollow) {
            if (config.steeringInputToken) {
                readAssignedWheel = true;
                wheelAngle = p.assignedSteering.read(config.steeringInputToken);
            } else {
              if (!readWheel) {
                readWheel = true;
                p.steeringActive = true;
                telemetryAngle = p.steering.read(
                    p.lmuGame ? telemetry::Game::Lmu : telemetry::Game::IRacing, now);
              }
              wheelAngle = telemetryAngle;
            }
            if (wheelAngle)
                pose = steered_dashboard(pose, {config.pivotX, config.pivotY, config.pivotZ},
                                         config.steeringTilt, float(*wheelAngle));
        }
        if (!lockForSlot())
            continue;
        // Chromium may have published the other buffer while resolving the pose.
        // Its pixels share this geometry when the dimensions and controls still match.
        const unsigned currentBuffer = sdata.current < 2 ? sdata.current : 0;
        const auto &latest = sdata.frames[currentBuffer];
        if (config.webDashboard && latest.width == frame.width && latest.height == frame.height) {
            buffer = currentBuffer;
            frame = latest;
        }
        if (d->producerPid != p.producer || !sdata.enabled || !frame.generation ||
            sdata.sourceRevision != config.sourceRevision ||
            sdata.revision != config.revision || sdata.placementToken != config.placementToken ||
            sdata.placementMode != config.placementMode ||
            latest.generation != frame.generation) {
            p.channel->unlock();
            continue;
        }
        sdata.placementAhead = ahead;
        if ((config.placementMode == 2 || config.placementMode == 4) && ahead.tick &&
            p.inputFocused.load() && focusRevision == p.focusRevision.load())
            InterlockedCompareExchange(reinterpret_cast<volatile LONG *>(&sdata.placementMode), 3,
                                       config.placementMode);
        float resolvedWidth =
            ahead.tick && config.placementMode ? ahead.widthMeters : config.widthMeters;
        float resolvedHeight = resolvedWidth * float(frame.height) / frame.width;
        if (config.precisionActive && config.desktopMode == 1 && p.inputFocused.load() && p.api.focused && p.api.focused(p.api.focusContext))
            desktop_precision_geometry(pose, resolvedWidth, resolvedHeight, config.precisionScale, config.precisionU, config.precisionV);
        sdata.pose.valid = 1;
        sdata.pose.sourceRevision = config.sourceRevision;
        sdata.pose.configRevision = config.revision;
        sdata.pose.sourceGeneration = frame.generation;
        sdata.pose.recenter = config.recenter;
        sdata.pose.space = reinterpret_cast<uint64_t>(panelSpace);
        sdata.pose.orientation[0] = pose.orientation.x;
        sdata.pose.orientation[1] = pose.orientation.y;
        sdata.pose.orientation[2] = pose.orientation.z;
        sdata.pose.orientation[3] = pose.orientation.w;
        sdata.pose.position[0] = pose.position.x;
        sdata.pose.position[1] = pose.position.y;
        sdata.pose.position[2] = pose.position.z;
        sdata.pose.width = resolvedWidth;
        sdata.pose.height = resolvedHeight;
        sdata.pose.pixelWidth = frame.width;
        sdata.pose.pixelHeight = frame.height;
        if ((isWheel && !stereoWheels && (!spare || layers.size()+1>=p.maxLayers)) || layers.size() >= p.maxLayers || (config.webDashboard && order.helmetAtLayerLimit)) {
            sdata.state = OverlayState::LayerLimit;
            p.channel->unlock();
            continue;
        }
        if (!slot.swap || slot.width != frame.width || slot.height != frame.height) {
            p.channel->unlock();
            if (!p.clearMask(mask))
                continue;
            if (created)
                continue;
            created = true;
            if (!p.create(slot, frame.width, frame.height)) {
                if (p.channel->lock()) {
                    p.channel->data()->slots[i].state = OverlayState::Error;
                    p.channel->unlock();
                }
                continue;
            }
            if (!lockForSlot())
                continue;
        }
        if (d->producerPid != p.producer || !sdata.enabled || sdata.sourceRevision != config.sourceRevision) {
            p.channel->unlock();
            continue;
        }
        bool updated = slot.frame == frame.frameId && slot.lastGeneration == frame.generation &&
                       slot.opacity == config.opacity;
        const bool depthFits = isWheel && stereoWheels ? layers.size() < p.maxLayers
                                                      : spare && layers.size() + 1 < p.maxLayers;
        const bool applyDepth = depth && depthFits && (isWheel || !p.maskShaderFailed) &&
                                config.opacity > 0 && config.cockpitOcclusion;
        const bool cachePage = (isWheel ? config.wheelScreen != 0 : applyDepth) && GetTickCount64() >= mask.retryAt &&
                               (mask.frame != frame.frameId || mask.generation != frame.generation);
        const auto &published = sdata.frames[buffer];
        if ((!updated || cachePage) && buffer == (sdata.current < 2 ? sdata.current : 0) &&
            published.generation == frame.generation && published.frameId == frame.frameId) {
            if (!p.source(slot, frame, buffer)) {
                p.channel->data()->slots[i].state = OverlayState::GpuMismatch;
                p.channel->unlock();
                continue;
            }
            unsigned acquiredKey = 1;
            auto acquired = slot.keys[buffer]->AcquireSync(1, 0);
            if (acquired == WAIT_TIMEOUT && slot.frame == frame.frameId &&
                slot.lastGeneration == frame.generation) {
                acquiredKey = 0;
                acquired = slot.keys[buffer]->AcquireSync(0, 0);
            }
            p.channel->unlock();
            if (acquired == S_OK) {
                if (!updated)
                    updated = p.upload(slot, buffer, config.opacity);
                if (updated && cachePage)
                    p.cachePage(mask, slot, buffer, frame);
                slot.keys[buffer]->ReleaseSync(updated ? 0 : acquiredKey);
                if (updated) {
                    slot.frame = frame.frameId;
                    slot.lastGeneration = frame.generation;
                    slot.opacity = config.opacity;
                    slot.ready = true;
                }
            }
        } else
            p.channel->unlock();
        if (!slot.ready)
            continue;
        slot.quad.space = panelSpace;
        slot.quad.pose = pose;
        slot.quad.size = {resolvedWidth, resolvedHeight};
        const bool masked = isWheel ? p.wheel(i,config,info,applyDepth?depth:nullptr,sceneProbe,
            config.steeringFollow?float(wheelAngle.value_or(0)):0.f)
                                    : applyDepth && p.maskDashboard(i, *depth, info.displayTime);
        if(isWheel && !masked) {
            slot.retainUntil=0;
            if(p.channel->lock()){
                p.channel->data()->slots[i].state=p.wheelPipelineFailed || (p.wheelWork.renderer && p.wheelWork.renderer->failed(i))
                    ? OverlayState::Error : OverlayState::Waiting;
                p.channel->unlock();
            }
            continue;
        }
        XrPosef pointerRay{{0, 0, 0, 1}, {}};
        DesktopHit hit;
        bool hitValid = false;
        uint32_t pointerSource = 1;
        if ((config.desktopMode == 1 || config.desktopMode == 2) && config.desktopWidth > 0 && config.desktopHeight > 0 &&
            config.opacity > 0 && p.inputFocused.load() && p.api.focused &&
            p.api.focused(p.api.focusContext)) {
            XrPosef ray{{0, 0, 0, 1}, {}};
            auto &head = inputHead;
            constexpr auto valid =
                XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            if (!inputHeadSampled) {
                inputHeadSampled = true;
                inputHeadLocated =
                    p.api.locate &&
                    p.api.locate(p.view, p.local, info.displayTime, &head) == XR_SUCCESS &&
                    (head.locationFlags & valid) == valid;
            }
            const bool tracked = inputHeadLocated;
            ray = config.anchor ? head.pose : XrPosef{{0, 0, 0, 1}, {}};
            if (tracked)
                hitValid = desktop_hit(pose, slot.quad.size.width, slot.quad.size.height, ray, hit);
            pointerRay = ray;
        }
        if (lockForSlot(masked)) {
            auto &state = p.channel->data()->slots[i];
            const auto &current = state.frames[state.current < 2 ? state.current : 0];
            // A web source owns two independently generated producer textures. The
            // released XR image remains valid across their handoff, including a busy key.
            const bool presentable = config.webDashboard
                ? current.generation && current.width == slot.width && current.height == slot.height &&
                      slot.opacity == config.opacity
                : current.generation == slot.lastGeneration;
            if (presentable && d->producerPid == p.producer && state.enabled &&
                state.sourceRevision == config.sourceRevision && state.revision == config.revision &&
                state.recenter == config.recenter && state.placementToken == config.placementToken) {
                submit(config.webDashboard, masked, isWheel);
                slot.retainUntil = config.webDashboard && config.opacity > 0
                    ? std::min<uint64_t>(GetTickCount64() + 100, d->producerBeat + 3000) : 0;
                slot.presentationEpoch = configEpoch;
                slot.retainedWheel = isWheel;
                slot.retainedOcclusion = config.cockpitOcclusion;
                state.cockpitDepthActive = masked && applyDepth ? 1u : 0u;
                state.driverState=isWheel?p.wheelFiles[i].driverState:0u;
                state.driverFitLimited=isWheel && p.wheelFiles[i].driverFitLimited;
                admitted[i] = true;
                rays[i] = pointerRay;
                opacities[i] = config.opacity;
                interactive = interactive || ((config.desktopMode == 1 || config.desktopMode == 2) && config.desktopWidth > 0 &&
                                              config.desktopHeight > 0);
                state.state = OverlayState::Ready;
                state.consumedFrame = slot.frame;
                pointers[i] = {uint32_t(hitValid),
                               pointerSource,
                               GetTickCount64(),
                               p.sessionId,
                               config.revision,
                               config.sourceRevision,
                               slot.lastGeneration,
                               config.recenter,
                               hit.u,
                               hit.v,
                               hit.distance,
                               focusRevision};
                ++p.channel->data()->active;
            }
            p.channel->unlock();
        }
    }
    if (!readWheel && !readAssignedWheel && !retainedDashboard)
        p.stopSteering();
    else if (!readWheel && p.steeringActive) {
        p.steering.reset(); p.steeringActive = false;
    }
    if (!interactive) {
        if (p.channel->lock()) {
            for (unsigned i = 0; i < overlay_count; ++i) {
                auto &state = p.channel->data()->slots[i];
                state.pointer.valid = 0;
                if (!admitted[i])
                    state.cockpitDepthActive = 0;
            }
            p.channel->unlock();
        }
        return;
    }
    // Composition order wins over distance. Conservatively treat the visible
    // rectangles as opaque for input, including regular noninteractive panels.
    const auto &viewInLocal = inputHead;
    const bool located = inputHeadLocated;
    if (p.channel->lock()) {
        for (unsigned i = 0; i < overlay_count; ++i) {
            auto &pointer = pointers[i];
            if (!pointer.valid)
                continue;
            if (!located) {
                pointer.valid = 0;
                continue;
            }
            auto ray =
                p.slots[i].quad.space == p.view ? compose(viewInLocal.pose, rays[i]) : rays[i];
            std::array<XrPosef, overlay_count> poses;
            std::array<float, overlay_count> widths{}, heights{};
            unsigned count = 0;
            for (unsigned j = i + 1; j < overlay_count; ++j)
                if (admitted[j] && opacities[j] > 0) {
                    const auto &q = p.slots[j].quad;
                    poses[count] = q.space == p.view ? compose(viewInLocal.pose, q.pose) : q.pose;
                    widths[count] = q.size.width;
                    heights[count++] = q.size.height;
                }
            if (desktop_occluded(ray, poses.data(), widths.data(), heights.data(), count))
                pointer.valid = 0;
        }
        for (unsigned i = 0; i < overlay_count; ++i) {
            auto &state = p.channel->data()->slots[i];
            if (!admitted[i])
                state.cockpitDepthActive = 0;
            auto pointer = pointers[i];
            if (!p.inputFocused.load() || p.focusRevision.load() != focusRevision)
                pointer.valid = 0;
            if (admitted[i] && state.enabled && pointer.configRevision == state.revision &&
                pointer.sourceRevision == state.sourceRevision &&
                pointer.recenter == state.recenter &&
                pointer.sourceGeneration ==
                    state.frames[state.current < 2 ? state.current : 0].generation)
                state.pointer = pointer;
            else
                state.pointer.valid = 0;
        }
        p.channel->unlock();
    }
}
} // namespace og
