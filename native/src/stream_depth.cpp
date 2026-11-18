#include "stream_depth.hpp"
#include "channel.hpp"
#include "cockpit_depth.hpp"
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <algorithm>

namespace og {
namespace {
using Microsoft::WRL::ComPtr;

bool immediateOn(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (!device || !context || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
    ComPtr<ID3D11Device> owner;
    context->GetDevice(&owner);
    return device == owner.Get();
}

// A separate context state works on single-threaded game devices too. Hold
// the immediate context lock across both swaps when the device supports it.
struct DepthContext {
    ComPtr<ID3D11DeviceContext1> immediate;
    ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11Multithread> guard;

    bool initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
        ComPtr<ID3D11Device1> device1;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device1))) ||
            FAILED(context->QueryInterface(IID_PPV_ARGS(&immediate)))) return false;
        const bool single = (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0;
        if (!single && FAILED(context->QueryInterface(IID_PPV_ARGS(&guard)))) return false;
        const auto feature = device->GetFeatureLevel();
        const UINT flags = single ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
        return SUCCEEDED(device1->CreateDeviceContextState(flags, &feature, 1, D3D11_SDK_VERSION,
                         __uuidof(ID3D11Device), nullptr, &state));
    }

    struct Scope {
        DepthContext& context;
        ComPtr<ID3DDeviceContextState> prior;
        explicit Scope(DepthContext& c) : context(c) {
            if (c.guard) c.guard->Enter();
            c.immediate->SwapDeviceContextState(c.state.Get(), &prior);
            c.immediate->SetPredication(nullptr, FALSE);
        }
        ~Scope() {
            context.immediate->SwapDeviceContextState(prior.Get(), nullptr);
            if (context.guard) context.guard->Leave();
        }
    };
};

bool validMapping(const CapturedDepth& depth) {
    for (float tangent : depth.fov) if (!std::isfinite(tangent)) return false;
    return depth.fov[0] < depth.fov[1] && depth.fov[3] < depth.fov[2] &&
           depth.minDepth >= 0 && depth.minDepth < depth.maxDepth && depth.maxDepth <= 1 &&
           std::isfinite(depth.invNear) && std::isfinite(depth.invFar) &&
           depth.invNear >= 0 && depth.invFar >= 0 && depth.invNear != depth.invFar;
}

bool describe(ID3D11Device* device, const CockpitDepthEye& eye, CapturedDepth& depth) {
    if (!eye.view) return false;
    ComPtr<ID3D11Device> owner;
    eye.view->GetDevice(&owner);
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    eye.view->GetDesc(&srv);
    if (owner.Get() != device || srv.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2DARRAY ||
        eye.slice >= srv.Texture2DArray.ArraySize) return false;
    switch (srv.Format) {
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: break;
    default: return false;
    }
    ComPtr<ID3D11Resource> resource;
    eye.view->GetResource(&resource);
    ComPtr<ID3D11Texture2D> source;
    if (FAILED(resource.As(&source))) return false;
    D3D11_TEXTURE2D_DESC d{};
    source->GetDesc(&d);
    const auto& r = eye.rect;
    if (d.SampleDesc.Count != 1 || srv.Texture2DArray.MostDetailedMip != 0 ||
        r.offset.x < 0 || r.offset.y < 0 || r.extent.width <= 0 || r.extent.height <= 0 ||
        uint64_t(r.offset.x) + r.extent.width > d.Width ||
        uint64_t(r.offset.y) + r.extent.height > d.Height) return false;
    const float angles[] = {eye.fov.angleLeft, eye.fov.angleRight, eye.fov.angleUp, eye.fov.angleDown};
    for (size_t i=0; i<4; ++i) {
        if (!(std::abs(angles[i]) < 1.57079632679f)) return false;
        depth.fov[i] = std::tan(angles[i]);
    }
    // The stream only needs depth to mask a dashboard. Bound both the GPU
    // conversion and the cross-device copy independently of headset supersampling.
    const float scale = 1280.f / (std::max)(r.extent.width, r.extent.height);
    depth.width = scale < 1 ? (std::max)(1L, std::lround(r.extent.width * scale)) : r.extent.width;
    depth.height = scale < 1 ? (std::max)(1L, std::lround(r.extent.height * scale)) : r.extent.height;
    depth.minDepth = eye.minDepth;
    depth.maxDepth = eye.maxDepth;
    depth.invNear = eye.invNear;
    depth.invFar = eye.invFar;
    return validMapping(depth);
}

constexpr char shaders[] = R"(
    cbuffer Crop : register(b0) { int2 offset; int slice; int unused; float2 sourceScale; float2 padding; }
    Texture2DArray<float> source : register(t0);
    float4 Vertex(uint i : SV_VertexID) : SV_Position {
        return float4(i == 2 ? 3 : -1, i == 1 ? 3 : -1, 0, 1);
    }
    float Depth(float4 p : SV_Position) : SV_Target {
        return source.Load(int4(int2(p.xy * sourceScale) + offset, slice, 0));
    }
)";
bool compile(const char* entry, const char* profile, ComPtr<ID3DBlob>& code) {
    return SUCCEEDED(D3DCompile(shaders, sizeof(shaders)-1, nullptr, nullptr, nullptr, entry, profile,
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.ReleaseAndGetAddressOf(), nullptr));
}
} // namespace

struct StreamDepthPublisher::Impl {
    ComPtr<ID3D11Device> device;
    DepthContext drawContext;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;
    struct Target {
        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<IDXGIKeyedMutex> key;
        HANDLE handle{};
        UINT width{}, height{};
    } target;

    bool initialize(ID3D11Device* d, ID3D11DeviceContext* context) {
        device = d;
        ComPtr<ID3DBlob> code;
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = 32;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        return drawContext.initialize(d, context) &&
               compile("Vertex", "vs_4_0", code) &&
               SUCCEEDED(d->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vertex)) &&
               compile("Depth", "ps_4_0", code) &&
               SUCCEEDED(d->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &pixel)) &&
               SUCCEEDED(d->CreateBuffer(&cb, nullptr, &constants));
    }

    bool allocate(UINT w, UINT h) {
        Target next;
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w; d.Height = h;
        d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
        d.Format = DXGI_FORMAT_R32_FLOAT;
        d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        d.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<IDXGIResource> resource;
        if (FAILED(device->CreateTexture2D(&d, nullptr, &texture)) || FAILED(texture.As(&resource)) ||
            FAILED(resource->GetSharedHandle(&next.handle)) || !next.handle || FAILED(texture.As(&next.key)) ||
            FAILED(device->CreateRenderTargetView(texture.Get(), nullptr, &next.rtv))) return false;
        next.width = w;
        next.height = h;
        target = std::move(next);
        return true;
    }

    bool draw(ID3D11DeviceContext* context, const CockpitDepthEye& eye) {
        DepthContext::Scope scope(drawContext);
        struct Crop { int x, y, slice, unused; float scaleX, scaleY, padding[2]; };
        const Crop crop{eye.rect.offset.x, eye.rect.offset.y, int(eye.slice), 0,
                        float(eye.rect.extent.width) / target.width, float(eye.rect.extent.height) / target.height, {}};
        D3D11_VIEWPORT vp{0, 0, float(target.width), float(target.height), 0, 1};
        context->RSSetViewports(1, &vp);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.Get(), nullptr, 0);
        context->PSSetShader(pixel.Get(), nullptr, 0);
        context->UpdateSubresource(constants.Get(), 0, nullptr, &crop, 0, 0);
        context->PSSetConstantBuffers(0, 1, constants.GetAddressOf());
        context->PSSetShaderResources(0, 1, &eye.view);
        context->OMSetRenderTargets(1, target.rtv.GetAddressOf(), nullptr);
        context->Draw(3, 0);
        ID3D11ShaderResourceView* empty{};
        context->PSSetShaderResources(0, 1, &empty);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->Flush();
        return SUCCEEDED(device->GetDeviceRemovedReason());
    }
};

StreamDepthPublisher::StreamDepthPublisher() = default;
StreamDepthPublisher::~StreamDepthPublisher() = default;
void StreamDepthPublisher::discardTexture() {
    if (impl_) impl_->target = {};
}
void StreamDepthPublisher::reset() { impl_.reset(); }
bool StreamDepthPublisher::publish(ID3D11Device* device, ID3D11DeviceContext* context,
                                   const CockpitDepthEye& eye, CapturedDepth& output) {
    output = {};
    CapturedDepth depth{};
    if (!immediateOn(device, context) || !describe(device, eye, depth)) return false;
    if (!impl_ || impl_->device.Get() != device) {
        auto impl = std::make_unique<Impl>();
        if (!impl->initialize(device, context)) return false;
        impl_ = std::move(impl);
    }
    auto& target = impl_->target;
    if (target.width != depth.width || target.height != depth.height) {
        if (!impl_->allocate(depth.width, depth.height)) return false;
        ++generation_;
    }
    // The color channel serializes publication and retirement. If the reader
    // skipped the retired depth, reclaim key 1. If it still owns the GPU
    // resource, skip this sample and reuse the allocation once released.
    if (target.key->AcquireSync(0, 0) != S_OK && target.key->AcquireSync(1, 0) != S_OK)
        return false;
    const bool drawn = impl_->draw(context, eye);
    if (target.key->ReleaseSync(drawn ? 1 : 0) != S_OK || !drawn) {
        target = {};
        return false;
    }
    depth.texture = reinterpret_cast<uint64_t>(target.handle);
    depth.generation = generation_;
    output = depth;
    return true;
}

struct StreamDepthReader::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<IDXGIKeyedMutex> key;
    ComPtr<ID3D11Texture2D> shared, local;
    ComPtr<ID3D11ShaderResourceView> localView;
    DepthContext copyContext;
    uint64_t handle{}, generation{};
    UINT width{}, height{};

    bool open(ID3D11Device* d, ID3D11DeviceContext* context, const CapturedDepth& depth) {
        if (FAILED(d->OpenSharedResource(reinterpret_cast<HANDLE>(depth.texture), IID_PPV_ARGS(&shared))) ||
            FAILED(shared.As(&key))) return false;
        D3D11_TEXTURE2D_DESC desc{};
        shared->GetDesc(&desc);
        if (desc.Width != depth.width || desc.Height != depth.height || desc.Format != DXGI_FORMAT_R32_FLOAT ||
            desc.ArraySize != 1 || desc.MipLevels != 1 || desc.SampleDesc.Count != 1 ||
            desc.SampleDesc.Quality || !(desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX)) return false;
        desc.MiscFlags = desc.CPUAccessFlags = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(d->CreateTexture2D(&desc, nullptr, &local)) ||
            FAILED(d->CreateShaderResourceView(local.Get(), nullptr, &localView)) ||
            !copyContext.initialize(d, context)) return false;
        device = d;
        handle = depth.texture;
        generation = depth.generation;
        width = desc.Width;
        height = desc.Height;
        return true;
    }
};

StreamDepthReader::StreamDepthReader() = default;
StreamDepthReader::~StreamDepthReader() = default;
StreamDepthReader::StreamDepthReader(StreamDepthReader&&) noexcept = default;
StreamDepthReader& StreamDepthReader::operator=(StreamDepthReader&&) noexcept = default;
void StreamDepthReader::reset() {
    valid_ = false;
    impl_.reset();
}
ID3D11ShaderResourceView* StreamDepthReader::view() const {
    return valid_ && impl_ ? impl_->localView.Get() : nullptr;
}
bool StreamDepthReader::consume(ID3D11Device* device, ID3D11DeviceContext* context,
                                const CapturedDepth& depth, bool newSession) {
    valid_ = false;
    if (newSession) reset();
    if (!depth.texture || !depth.generation || !depth.width || !depth.height ||
        !validMapping(depth) || !immediateOn(device, context)) return false;
    if (!impl_ || impl_->device.Get() != device || impl_->handle != depth.texture ||
        impl_->generation != depth.generation) {
        auto next = std::make_unique<Impl>();
        if (!next->open(device, context, depth)) return false;
        impl_ = std::move(next);
    }
    if (depth.width != impl_->width || depth.height != impl_->height ||
        impl_->key->AcquireSync(1, 0) != S_OK) return false;
    {
        DepthContext::Scope scope(impl_->copyContext);
        context->CopyResource(impl_->local.Get(), impl_->shared.Get());
        context->Flush();
    }
    valid_ = impl_->key->ReleaseSync(0) == S_OK && SUCCEEDED(device->GetDeviceRemovedReason());
    return valid_;
}
} // namespace og
