#include "stream_overlay_renderer.hpp"
#include "channel.hpp"
#include "core.hpp"
#include "overlay_transport.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace og {
namespace {
constexpr char shader[] = R"(
Texture2D image:register(t0);SamplerState sample0:register(s0);
cbuffer Settings:register(b0){float4 corners[4];float opacity;float3 padding;};
struct V{float4 position:SV_Position;float2 uv:TEXCOORD0;};
V vs(uint id:SV_VertexID){const uint indices[6]={0,1,2,2,1,3};const float2 uv[4]={float2(0,0),float2(1,0),float2(0,1),float2(1,1)};uint corner=indices[id];V o;o.uv=uv[corner];o.position=corners[corner];return o;}
float4 ps(V v):SV_Target{return image.Sample(sample0,v.uv)*opacity;}
)";
struct Imported {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<IDXGIKeyedMutex> key;
    uint64_t generation{}, handle{};
};
struct Slot {
    std::array<Imported, 2> imported;
    ComPtr<ID3D11Texture2D> local;
    ComPtr<ID3D11ShaderResourceView> view;
    uint64_t sourceRevision{}, generation{}, frame{};
    uint32_t width{}, height{}, format{};
    bool ready{};
};
bool valid(const OverlaySlot &s, const OverlayFrame &f) {
    return s.enabled && s.showInStream && s.streamCurrent < 2 && f.generation && f.handle &&
           f.width && f.width <= 1920 && f.height && f.height <= 1080 &&
           (f.format == DXGI_FORMAT_R8G8B8A8_UNORM || f.format == DXGI_FORMAT_B8G8R8A8_UNORM) &&
           std::isfinite(s.opacity) && s.opacity > 0 && s.opacity <= 1;
}
struct DrawConstants {
    float corners[4][4]{}, opacity{}, padding[3]{};
};
bool project(const CapturedOverlay &geometry, const SharedFrame &frame, const vrst::Warp &warp,
             DrawConstants &out) {
    vrst::Projection raw{frame.fov[0], frame.fov[1], frame.fov[2], frame.fov[3]};
    if (!geometry.valid || !vrst::valid(raw) || !vrst::valid(warp.input) ||
        !vrst::valid(warp.output) || !vrst::valid(warp.correction) ||
        !std::isfinite(warp.uv_left) || !std::isfinite(warp.uv_right) ||
        !std::isfinite(warp.uv_top) || !std::isfinite(warp.uv_bottom) ||
        warp.uv_right - warp.uv_left < 1e-5 || warp.uv_bottom - warp.uv_top < 1e-5)
        return false;
    bool inFront = false;
    for (unsigned i = 0; i < 4; ++i) {
        const auto &c = geometry.corners[i];
        for (float value : c)
            if (!std::isfinite(value) || std::abs(value) > 10000)
                return false;
        double depth = -c[2];
        // Invert the output pixel shader's source-UV mapping without dividing
        // by depth. This preserves perspective, including vertices behind the
        // near plane, and respects asymmetric source FOV and input cropping.
        double uDepth = (c[0] - raw.left * depth) / (raw.right - raw.left);
        double vDepth = (raw.up * depth - c[1]) / (raw.up - raw.down);
        vrst::Vec3 input{warp.input.left * depth + (uDepth - warp.uv_left * depth) /
                                                       (warp.uv_right - warp.uv_left) *
                                                       (warp.input.right - warp.input.left),
                         warp.input.up * depth - (vDepth - warp.uv_top * depth) /
                                                     (warp.uv_bottom - warp.uv_top) *
                                                     (warp.input.up - warp.input.down),
                         c[2]};
        // The game shader maps output rays to raw input with correction; quad
        // vertices travel in the opposite direction, hence its inverse here.
        auto p = vrst::rotate(vrst::conjugate(vrst::normalized(warp.correction)), input);
        double w = -p.z;
        double clip[]{(2 * p.x - (warp.output.right + warp.output.left) * w) /
                          (warp.output.right - warp.output.left),
                      (2 * p.y - (warp.output.up + warp.output.down) * w) /
                          (warp.output.up - warp.output.down),
                      w - .001, w};
        for (unsigned j = 0; j < 4; ++j) {
            if (!std::isfinite(clip[j]) || std::abs(clip[j]) > 1e9)
                return false;
            out.corners[i][j] = float(clip[j]);
        }
        inFront = inFront || w > .001;
    }
    return inFront;
}
} // namespace
struct StreamOverlayRenderer::Impl {
    std::unique_ptr<OverlayChannel> channel;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Device1> device1;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11DeviceContext1> context1;
    ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11RasterizerState> raster;
    std::array<Slot, overlay_count> slots;
    HANDLE producerProcess{};
    uint32_t producer{};
    uint64_t session{};
    ~Impl() { reset(); }
    void clearSlots() {
        for (auto &slot : slots)
            slot = Slot{};
    }
    void reset() {
        if (channel && channel->lock()) {
            auto *d = channel->data();
            if (d->streamConsumerPid == GetCurrentProcessId()) {
                d->streamConsumerPid = 0;
                d->streamConsumerBeat = 0;
            }
            channel->unlock();
        }
        clearSlots();
        channel.reset();
        if (producerProcess)
            CloseHandle(producerProcess);
        producerProcess = nullptr;
        producer = 0;
        session = 0;
        state.Reset();
        raster.Reset();
        blend.Reset();
        sampler.Reset();
        constants.Reset();
        ps.Reset();
        vs.Reset();
        context1.Reset();
        context.Reset();
        device1.Reset();
        device.Reset();
    }
    bool graphics(ID3D11Device *d, ID3D11DeviceContext *c) {
        device = d;
        context = c;
        if (!device || !context || FAILED(device.As(&device1)) || FAILED(context.As(&context1)))
            return false;
        auto feature = device->GetFeatureLevel();
        UINT flags = (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED)
                         ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                         : 0;
        if (FAILED(device1->CreateDeviceContextState(flags, &feature, 1, D3D11_SDK_VERSION,
                                                     __uuidof(ID3D11Device), nullptr, &state)))
            return false;
        ComPtr<ID3DBlob> v, p;
        if (FAILED(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "vs", "vs_5_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &v, nullptr)) ||
            FAILED(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "ps", "ps_5_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &p, nullptr)) ||
            FAILED(device->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), nullptr,
                                              &vs)) ||
            FAILED(
                device->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), nullptr, &ps)))
            return false;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(DrawConstants);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        D3D11_BLEND_DESC blendDesc{};
        auto &rt = blendDesc.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_ONE;
        rt.DestBlend = rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_BACK;
        rd.DepthClipEnable = TRUE;
        return SUCCEEDED(device->CreateBuffer(&bd, nullptr, &constants)) &&
               SUCCEEDED(device->CreateSamplerState(&sd, &sampler)) &&
               SUCCEEDED(device->CreateBlendState(&blendDesc, &blend)) &&
               SUCCEEDED(device->CreateRasterizerState(&rd, &raster));
    }
    bool import(Slot &s, const OverlayFrame &f, unsigned index) {
        auto &shared = s.imported[index];
        if (shared.generation != f.generation || shared.handle != f.handle || !shared.texture) {
            shared = Imported{};
            HANDLE handle{};
            if (!producerProcess ||
                !DuplicateHandle(producerProcess, reinterpret_cast<HANDLE>(f.handle),
                                 GetCurrentProcess(), &handle, 0, FALSE, DUPLICATE_SAME_ACCESS))
                return false;
            auto result = device1->OpenSharedResource1(handle, IID_PPV_ARGS(&shared.texture));
            CloseHandle(handle);
            if (FAILED(result) || FAILED(shared.texture.As(&shared.key)))
                return false;
            D3D11_TEXTURE2D_DESC desc{};
            shared.texture->GetDesc(&desc);
            auto typeless = f.format == DXGI_FORMAT_B8G8R8A8_UNORM ? DXGI_FORMAT_B8G8R8A8_TYPELESS
                                                                   : DXGI_FORMAT_R8G8B8A8_TYPELESS;
            if (desc.Width != f.width || desc.Height != f.height || desc.ArraySize != 1 ||
                desc.MipLevels != 1 || desc.SampleDesc.Count != 1 ||
                (desc.Format != f.format && desc.Format != typeless)) {
                shared = Imported{};
                return false;
            }
            shared.generation = f.generation;
            shared.handle = f.handle;
        }
        if (!s.local || s.width != f.width || s.height != f.height || s.format != f.format) {
            s.ready = false;
            s.local.Reset();
            s.view.Reset();
            D3D11_TEXTURE2D_DESC d{};
            d.Width = f.width;
            d.Height = f.height;
            d.ArraySize = d.MipLevels = d.SampleDesc.Count = 1;
            d.Format = DXGI_FORMAT(f.format);
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(device->CreateTexture2D(&d, nullptr, &s.local)) ||
                FAILED(device->CreateShaderResourceView(s.local.Get(), nullptr, &s.view)))
                return false;
            s.width = f.width;
            s.height = f.height;
            s.format = f.format;
        }
        return true;
    }
    void update(unsigned index, const OverlaySlot &config, const OverlayFrame &f) {
        auto &s = slots[index];
        if (s.sourceRevision != config.sourceRevision) {
            s = Slot{};
            s.sourceRevision = config.sourceRevision;
        }
        if (s.ready && s.generation == f.generation && s.frame == f.frameId)
            return;
        unsigned buffer = config.streamCurrent;
        if (!import(s, f, buffer))
            return;
        // Recheck after importing outside the metadata lock. Only the current
        // published buffer may use key zero (a previous Output consumed it).
        // The producer reclaims noncurrent buffers under this same short lock.
        if (!channel->lock())
            return;
        const auto *d = channel->data();
        const auto &current = d->slots[index];
        const auto &frame = current.streamFrames[buffer];
        bool same = d->producerPid == producer && d->session == session && current.enabled &&
                    current.showInStream && current.sourceRevision == config.sourceRevision &&
                    current.streamCurrent == buffer && frame.generation == f.generation &&
                    frame.frameId == f.frameId && frame.handle == f.handle;
        HRESULT acquired = WAIT_TIMEOUT;
        if (same) {
            acquired = s.imported[buffer].key->AcquireSync(1, 0);
            if (acquired == WAIT_TIMEOUT)
                acquired = s.imported[buffer].key->AcquireSync(0, 0);
        }
        channel->unlock();
        if (acquired != S_OK)
            return;
        context->CopyResource(s.local.Get(), s.imported[buffer].texture.Get());
        context->Flush();
        s.imported[buffer].key->ReleaseSync(0);
        s.generation = f.generation;
        s.frame = f.frameId;
        s.ready = true;
    }
};
StreamOverlayRenderer::StreamOverlayRenderer() : impl_(std::make_unique<Impl>()) {}
StreamOverlayRenderer::~StreamOverlayRenderer() = default;
StreamOverlayRenderer::StreamOverlayRenderer(StreamOverlayRenderer &&) noexcept = default;
StreamOverlayRenderer &
StreamOverlayRenderer::operator=(StreamOverlayRenderer &&) noexcept = default;
bool StreamOverlayRenderer::initialize(ID3D11Device *device, ID3D11DeviceContext *context,
                                       const std::string &game) {
    reset();
    if (!impl_)
        impl_ = std::make_unique<Impl>();
    auto &p = *impl_;
    if (!p.graphics(device, context)) {
        p.reset();
        return false;
    }
    p.channel = std::make_unique<OverlayChannel>();
    if (!p.channel->open(game)) {
        p.reset();
        return false;
    }
    return true;
}
void StreamOverlayRenderer::reset() {
    if (impl_)
        impl_->reset();
}
void StreamOverlayRenderer::draw(ID3D11RenderTargetView *target, uint32_t width, uint32_t height,
                                 const SharedFrame &captured, const vrst::Warp &warp) {
    if (!impl_ || !target || !width || !height || width > 16384 || height > 16384)
        return;
    auto &p = *impl_;
    if (!p.channel || !p.channel->lock())
        return;
    auto *d = p.channel->data();
    auto now = GetTickCount64();
    if (d->streamConsumerPid && d->streamConsumerPid != GetCurrentProcessId() &&
        now - d->streamConsumerBeat < 3000) {
        p.channel->unlock();
        p.clearSlots();
        return;
    }
    d->streamConsumerPid = GetCurrentProcessId();
    d->streamConsumerBeat = now;
    OverlayShared snapshot = *d;
    p.channel->unlock();
    if (!snapshot.producerPid || now - snapshot.producerBeat > 3000) {
        p.clearSlots();
        if (p.producerProcess)
            CloseHandle(p.producerProcess);
        p.producerProcess = nullptr;
        p.producer = 0;
        p.session = 0;
        return;
    }
    if (p.producer != snapshot.producerPid || p.session != snapshot.session) {
        p.clearSlots();
        if (p.producerProcess)
            CloseHandle(p.producerProcess);
        p.producer = snapshot.producerPid;
        p.session = snapshot.session;
        p.producerProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, p.producer);
    }
    ComPtr<ID3DDeviceContextState> previous;
    p.context1->SwapDeviceContextState(p.state.Get(), &previous);
    // CopyResource is predicated too. Isolate both the incoming copy and draw
    // from the caller's state, then restore its display-pass bindings below.
    p.context->SetPredication(nullptr, FALSE);
    for (unsigned i = 0; i < overlay_count; ++i) {
        const auto &config = snapshot.slots[i];
        const auto &f = config.streamFrames[config.streamCurrent < 2 ? config.streamCurrent : 0];
        if (!valid(config, f)) {
            p.slots[i] = Slot{};
            continue;
        }
        p.update(i, config, f);
    }
    p.context->OMSetRenderTargets(1, &target, nullptr);
    p.context->OMSetBlendState(p.blend.Get(), nullptr, UINT_MAX);
    p.context->RSSetState(p.raster.Get());
    D3D11_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
    p.context->RSSetViewports(1, &viewport);
    p.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    p.context->VSSetShader(p.vs.Get(), nullptr, 0);
    p.context->PSSetShader(p.ps.Get(), nullptr, 0);
    auto *sampler = p.sampler.Get();
    auto *cb = p.constants.Get();
    p.context->PSSetSamplers(0, 1, &sampler);
    p.context->VSSetConstantBuffers(0, 1, &cb);
    p.context->PSSetConstantBuffers(0, 1, &cb);
    for (unsigned i = 0; i < overlay_count; ++i) {
        const auto &s = p.slots[i];
        if (!s.ready)
            continue;
        const auto &config = snapshot.slots[i];
        const auto &geometry = captured.overlays[i];
        if (captured.overlaySession != snapshot.session ||
            captured.overlayConsumerPid != snapshot.consumerPid ||
            captured.overlayProducerPid != snapshot.producerPid)
            continue;
        if (geometry.sourceRevision != config.sourceRevision || geometry.pixelWidth != s.width ||
            geometry.pixelHeight != s.height)
            continue;
        DrawConstants constants{};
        constants.opacity = config.opacity;
        if (!project(geometry, captured, warp, constants))
            continue;
        p.context->UpdateSubresource(p.constants.Get(), 0, nullptr, &constants, 0, 0);
        auto *view = s.view.Get();
        p.context->PSSetShaderResources(0, 1, &view);
        p.context->Draw(6, 0);
    }
    ID3D11ShaderResourceView *empty{};
    p.context->PSSetShaderResources(0, 1, &empty);
    p.context->OMSetRenderTargets(0, nullptr, nullptr);
    p.context1->SwapDeviceContextState(previous.Get(), nullptr);
}
} // namespace og
