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
#include "overlay_transport.hpp"
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
constexpr char shader[] = R"(
Texture2D image:register(t0);SamplerState sampler0:register(s0);cbuffer Settings:register(b0){float opacity;float3 pad;};
struct V{float4 position:SV_Position;float2 uv:TEXCOORD0;};
V vs(uint id:SV_VertexID){V o;float2 uv=float2((id<<1)&2,id&2);o.position=float4(uv*float2(2,-2)+float2(-1,1),0,1);o.uv=uv;return o;}
float4 ps(V i):SV_Target{return image.Sample(sampler0,i.uv)*opacity;}
)";
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
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    bool ready{}, waited{};
    uint32_t acquired{UINT32_MAX};
};
} // namespace
struct OverlayRenderer::Impl {
    std::mutex mutex;
    std::unique_ptr<OverlayChannel> channel;
    OverlayDispatch api;
    XrSession session{};
    XrSpace view{}, local{};
    uint32_t maxLayers{};
    LUID adapterLuid{};
    uint64_t sessionId{};
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
    uint32_t producer{};
    HANDLE producerProcess{};
    bool supported{};
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
            auto q = location.pose.orientation;
            float yaw = std::atan2(2 * (q.w * q.y + q.x * q.z), 1 - 2 * (q.y * q.y + q.x * q.x));
            slot.anchor = {{0, std::sin(yaw / 2), 0, std::cos(yaw / 2)}, location.pose.position};
            slot.recenter = config.recenter;
        }
        pose = compose(slot.anchor, pose);
        return true;
    }
    ~Impl() { reset(); }
    bool clear(RuntimeSlot &s, bool preserveAnchor = false) {
        auto anchor = s.anchor;
        auto recenter = s.recenter;
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
            s.sourceRevision = sourceRevision;
        }
        return true;
    }
    void reset() {
        for (auto &s : slots) {
            clear(s);
            s = RuntimeSlot{};
        }
        if (view && api.destroySpace)
            api.destroySpace(view);
        if (local && api.destroySpace)
            api.destroySpace(local);
        view = local = XR_NULL_HANDLE;
        if (channel && channel->lock()) {
            auto *d = channel->data();
            if (d->consumerPid == GetCurrentProcessId()) {
                d->consumerPid = 0;
                d->consumerBeat = 0;
                d->active = 0;
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
        constants.Reset();
        sampler.Reset();
        ps.Reset();
        vs.Reset();
        drawState.Reset();
        context1.Reset();
        context.Reset();
        device1.Reset();
        device.Reset();
        on12.Reset();
    }
    bool graphics(const void *chain) {
        for (auto *n = static_cast<const XrBaseInStructure *>(chain); n; n = n->next) {
            if (n->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
                device = reinterpret_cast<const XrGraphicsBindingD3D11KHR *>(n)->device;
                if (device)
                    device->GetImmediateContext(&context);
                break;
            }
            if (n->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR) {
                auto *b = reinterpret_cast<const XrGraphicsBindingD3D12KHR *>(n);
                IUnknown *queues[]{b->queue};
                if (FAILED(D3D11On12CreateDevice(b->device, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                                 nullptr, 0, queues, 1, 0, &device, &context,
                                                 nullptr)))
                    return false;
                device.As(&on12);
                break;
            }
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
        return SUCCEEDED(device->CreateSamplerState(&sd, &sampler)) &&
               SUCCEEDED(device->CreateBuffer(&bd, nullptr, &constants));
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
    bool upload(RuntimeSlot &s, unsigned buffer, float opacity) {
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
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if (XR_FAILED(api.release(s.swap, &release)))
            return false;
        s.acquired = UINT32_MAX;
        s.waited = false;
        return true;
    }
};
OverlayRenderer::OverlayRenderer() : impl_(std::make_unique<Impl>()) {}
OverlayRenderer::~OverlayRenderer() = default;
bool OverlayRenderer::initialize(XrSession session, const void *binding, uint32_t maxLayers,
                                 const OverlayDispatch &api, const std::string &game) {
    auto &p = *impl_;
    std::lock_guard lock(p.mutex);
    p.reset();
    p.api = api;
    p.session = session;
    p.maxLayers = maxLayers;
    if (!p.graphics(binding))
        return false;
    p.channel = std::make_unique<OverlayChannel>();
    if (!p.channel->open(game))
        return false;
    XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    space.poseInReferenceSpace.orientation.w = 1;
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (XR_FAILED(api.createSpace(session, &space, &p.view)))
        return false;
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (XR_FAILED(api.createSpace(session, &space, &p.local)))
        return false;
    ComPtr<IDXGIDevice> dx;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(p.device.As(&dx)) || FAILED(dx->GetAdapter(&adapter)) ||
        FAILED(adapter->GetDesc(&desc)))
        return false;
    p.adapterLuid = desc.AdapterLuid;
    p.sessionId = ++nextOverlaySession;
    if (p.channel->lock()) {
        auto *d = p.channel->data();
        d->consumerPid = GetCurrentProcessId();
        d->consumerBeat = GetTickCount64();
        d->adapterLow = desc.AdapterLuid.LowPart;
        d->adapterHigh = desc.AdapterLuid.HighPart;
        d->session = p.sessionId;
        d->active = 0;
        d->capacity = maxLayers > 0 ? std::min(overlay_count, maxLayers - 1) : 0;
        p.channel->unlock();
    }
    p.supported = true;
    return true;
}
void OverlayRenderer::reset() {
    auto &p = *impl_;
    std::lock_guard lock(p.mutex);
    p.reset();
}
void OverlayRenderer::append(const XrFrameEndInfo &info, bool enabled, bool shouldRender,
                             std::vector<const XrCompositionLayerBaseHeader *> &layers) {
    auto &p = *impl_;
    std::unique_lock lock(p.mutex, std::try_to_lock);
    if (!lock.owns_lock() || !p.supported || !p.channel)
        return;
    if (!p.channel->lock())
        return;
    auto *d = p.channel->data();
    d->consumerPid = GetCurrentProcessId();
    d->adapterLow = p.adapterLuid.LowPart;
    d->adapterHigh = p.adapterLuid.HighPart;
    d->session = p.sessionId;
    d->consumerBeat = GetTickCount64();
    d->capacity = p.maxLayers > layers.size()
                      ? uint32_t(std::min<size_t>(overlay_count, p.maxLayers - layers.size()))
                      : 0;
    d->active = 0;
    if (!enabled || !d->producerPid || GetTickCount64() - d->producerBeat > 3000) {
        for (auto &config : d->slots)
            config.pose.valid = 0;
        p.channel->unlock();
        for (auto &slot : p.slots)
            if (slot.swap || slot.ready)
                p.clear(slot, true);
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
            s.ready = false;
        }
    }
    p.channel->unlock();
    bool created = false;
    for (unsigned i = 0; i < overlay_count; ++i) {
        if (!p.channel->lock())
            break;
        auto &sdata = p.channel->data()->slots[i];
        auto config = sdata;
        auto &slot = p.slots[i];
        if (slot.sourceRevision != config.sourceRevision) {
            p.channel->unlock();
            if (!p.clear(slot, true))
                continue;
            slot.sourceRevision = config.sourceRevision;
            if (!p.channel->lock())
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
        XrPosef pose{};
        if (!p.resolvePose(slot, config, info.displayTime, pose) || !p.channel->lock())
            continue;
        if (!sdata.enabled || sdata.sourceRevision != config.sourceRevision ||
            sdata.revision != config.revision ||
            sdata.frames[sdata.current < 2 ? sdata.current : 0].generation != frame.generation) {
            p.channel->unlock();
            continue;
        }
        sdata.pose.valid = 1;
        sdata.pose.sourceRevision = config.sourceRevision;
        sdata.pose.configRevision = config.revision;
        sdata.pose.sourceGeneration = frame.generation;
        sdata.pose.recenter = config.recenter;
        sdata.pose.space = reinterpret_cast<uint64_t>(config.anchor ? p.local : p.view);
        sdata.pose.orientation[0] = pose.orientation.x;
        sdata.pose.orientation[1] = pose.orientation.y;
        sdata.pose.orientation[2] = pose.orientation.z;
        sdata.pose.orientation[3] = pose.orientation.w;
        sdata.pose.position[0] = pose.position.x;
        sdata.pose.position[1] = pose.position.y;
        sdata.pose.position[2] = pose.position.z;
        sdata.pose.width = config.widthMeters;
        sdata.pose.height = config.widthMeters * float(frame.height) / frame.width;
        sdata.pose.pixelWidth = frame.width;
        sdata.pose.pixelHeight = frame.height;
        if (layers.size() >= p.maxLayers) {
            sdata.state = OverlayState::LayerLimit;
            p.channel->unlock();
            continue;
        }
        if (!slot.swap || slot.width != frame.width || slot.height != frame.height) {
            p.channel->unlock();
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
            if (!p.channel->lock())
                continue;
        }
        if (!sdata.enabled || sdata.sourceRevision != config.sourceRevision) {
            p.channel->unlock();
            continue;
        }
        bool updated = slot.frame == frame.frameId && slot.lastGeneration == frame.generation &&
                       slot.opacity == config.opacity;
        if (!updated) {
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
                updated = p.upload(slot, buffer, config.opacity);
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
        slot.quad.space = config.anchor ? p.local : p.view;
        slot.quad.pose = pose;
        slot.quad.size = {config.widthMeters, config.widthMeters * float(slot.height) / slot.width};
        if (p.channel->lock()) {
            auto &state = p.channel->data()->slots[i];
            if (state.enabled && state.sourceRevision == config.sourceRevision) {
                layers.push_back(
                    reinterpret_cast<const XrCompositionLayerBaseHeader *>(&slot.quad));
                state.state = OverlayState::Ready;
                state.consumedFrame = slot.frame;
                ++p.channel->data()->active;
            }
            p.channel->unlock();
        }
    }
}
} // namespace og
