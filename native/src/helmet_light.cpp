#include "helmet_light.hpp"
#include <d3dcompiler.h>
#include <initializer_list>

namespace og {
namespace {
using Microsoft::WRL::ComPtr;
constexpr char shaders[] = R"(
cbuffer Params : register(b0) { float4 rect; float4 parameters; }
float4 Vertex(uint id : SV_VertexID) : SV_Position {
    return float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, 0, 1);
}
#ifdef MULTISAMPLE
Texture2DMSArray<float4> scene : register(t0);
#else
Texture2DArray<float4> scene : register(t0);
#endif
float4 Probe(float4 pos : SV_Position) : SV_Target {
    float3 sum = 0;
    // Sixteen stratified samples per quadrant. Restrict all taps to the
    // submitted rect (including atlased eyes). Cap HDR outliers before mixing.
    for (uint y = 0; y < 4; ++y) for (uint x = 0; x < 4; ++x) {
        float2 uv = (floor(pos.xy) + (float2(x,y) + .5) / 4) * .5;
        int2 pixel = int2(rect.xy + floor(uv * rect.zw));
#ifdef MULTISAMPLE
        float3 rgb = scene.Load(int3(pixel, 0), 0).rgb;
#else
        float3 rgb = scene.Load(int4(pixel, 0, 0)).rgb;
#endif
        sum += clamp(rgb, 0, 2);
    }
    return float4(sum / 16, parameters.x);
}
)";
constexpr char shading[] = R"(
cbuffer Params : register(b0) { float4 rect; float4 parameters; }
Texture2D<float4> art : register(t0);
Texture2D<float4> probe : register(t1);
SamplerState linearClamp : register(s0);
float4 Shade(float4 pos : SV_Position) : SV_Target {
    float4 pixel = art.Load(int3(pos.xy, 0));
    float3 ambient = probe.SampleLevel(linearClamp, pos.xy * rect.xy, 0).rgb;
    float luma = dot(ambient, float3(.2126, .7152, .0722));
    float3 gain = clamp(.40 + 1.5 * lerp(luma, ambient, .65), .40, 1.55);
    return float4(pixel.rgb * lerp(1, gain, parameters.x), pixel.a);
}
)";
bool compile(const char* source, size_t size, const char* entry, const char* target,
             ComPtr<ID3DBlob>& code, bool ms = false) {
    const D3D_SHADER_MACRO defines[] = {{"MULTISAMPLE","1"}, {nullptr,nullptr}};
    return SUCCEEDED(D3DCompile(source, size, nullptr, ms ? defines : nullptr, nullptr, entry, target,
                                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.ReleaseAndGetAddressOf(), nullptr));
}
}
bool HelmetLight::initialize(ID3D11Device* device) {
    device_ = device;
    ComPtr<ID3DBlob> code;
    if (FAILED(device->CreateDeferredContext(0, &commands_)) ||
        !compile(shaders, sizeof(shaders)-1, "Vertex", "vs_5_0", code) ||
        FAILED(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vertex_))) return false;
    for (bool ms : {false, true}) {
        if (!compile(shaders, sizeof(shaders)-1, "Probe", "ps_5_0", code, ms) ||
            FAILED(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                             ms ? sampleMs_.GetAddressOf() : sample_.GetAddressOf()))) return false;
    }
    if (!compile(shading, sizeof(shading)-1, "Shade", "ps_5_0", code) ||
        FAILED(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shade_))) return false;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = 2;
    td.ArraySize = td.MipLevels = td.SampleDesc.Count = 1;
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 8 * sizeof(float);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_BLEND_DESC blend{};
    auto& rt = blend.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_ZERO;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    return SUCCEEDED(device->CreateBuffer(&bd, nullptr, &constants_)) &&
           SUCCEEDED(device->CreateBlendState(&blend, &smooth_)) &&
           SUCCEEDED(device->CreateSamplerState(&sampler, &linear_)) &&
           SUCCEEDED(device->CreateTexture2D(&td, nullptr, &texture)) &&
           SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &probeTarget_)) &&
           SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &probe_));
}
bool HelmetLight::draw(ID3D11DeviceContext* immediate, ID3D11PixelShader* shader, ID3D11RenderTargetView* target,
                       UINT width, UINT height, const float (&constants)[8],
                       ID3D11ShaderResourceView* const* inputs, UINT count, bool smooth) {
    D3D11_VIEWPORT vp{0, 0, float(width), float(height), 0, 1};
    commands_->RSSetViewports(1, &vp);
    commands_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands_->VSSetShader(vertex_.Get(), nullptr, 0);
    commands_->PSSetShader(shader, nullptr, 0);
    commands_->UpdateSubresource(constants_.Get(), 0, nullptr, constants, 0, 0);
    commands_->PSSetConstantBuffers(0, 1, constants_.GetAddressOf());
    commands_->PSSetSamplers(0, 1, linear_.GetAddressOf());
    commands_->PSSetShaderResources(0, count, inputs);
    commands_->OMSetRenderTargets(1, &target, nullptr);
    commands_->OMSetBlendState(smooth ? smooth_.Get() : nullptr, nullptr, UINT_MAX);
    commands_->Draw(3, 0);
    ComPtr<ID3D11CommandList> list;
    if (FAILED(commands_->FinishCommandList(FALSE, &list))) return false;
    immediate->ExecuteCommandList(list.Get(), TRUE);
    return true;
}
bool HelmetLight::sample(ID3D11DeviceContext* context, ID3D11Texture2D* source, DXGI_FORMAT format,
                         const XrSwapchainSubImage& region, float weight) {
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    const auto& r = region.imageRect;
    if (!(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) || region.imageArrayIndex >= desc.ArraySize ||
        r.offset.x < 0 || r.offset.y < 0 || r.extent.width <= 0 || r.extent.height <= 0 ||
        uint64_t(r.offset.x) + r.extent.width > desc.Width || uint64_t(r.offset.y) + r.extent.height > desc.Height) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = format;
    const bool ms = desc.SampleDesc.Count > 1;
    if (ms) {
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
        view.Texture2DMSArray.FirstArraySlice = region.imageArrayIndex;
        view.Texture2DMSArray.ArraySize = 1;
    } else {
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        view.Texture2DArray.MipLevels = view.Texture2DArray.ArraySize = 1;
        view.Texture2DArray.FirstArraySlice = region.imageArrayIndex;
    }
    ComPtr<ID3D11ShaderResourceView> scene;
    if (FAILED(device_->CreateShaderResourceView(source, &view, &scene))) return false;
    const float constants[]{float(r.offset.x), float(r.offset.y), float(r.extent.width), float(r.extent.height), weight, 0, 0, 0};
    return draw(context, ms ? sampleMs_.Get() : sample_.Get(), probeTarget_.Get(), 2, 2,
                constants, scene.GetAddressOf(), 1, weight < 1);
}
bool HelmetLight::shade(ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
                        ID3D11ShaderResourceView* art, UINT width, UINT height, float strength) {
    const float constants[]{1.f/width, 1.f/height, 0, 0, strength, 0, 0, 0};
    ID3D11ShaderResourceView* inputs[]{art, probe_.Get()};
    return draw(context, shade_.Get(), target, width, height, constants, inputs, 2, false);
}
}
