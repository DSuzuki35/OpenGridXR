#pragma once
#include "lmu_cockpit_depth.hpp"
#include "cockpit_depth_copy.hpp"
#include <d3dcompiler.h>
#include <cmath>
#include <string>

namespace og::lmu {
inline bool cockpit_mapping(const CockpitProjection& p, CockpitDepthEye& eye) {
    for (float x : p) if (!std::isfinite(x)) return false;
    if (!(p[0]>0 && p[5]>0 && p[11]==1 && p[14]!=0) ||
        p[1] || p[2] || p[3] || p[4] || p[6] || p[7] || p[12] || p[13] || p[15]) return false;
    // LMU's row-vector projection gives device Z = P22 + P32 / forward.
    eye.invNear = -p[10]/p[14];
    eye.invFar = (1-p[10])/p[14];
    if (!std::isfinite(eye.invNear) || !std::isfinite(eye.invFar) ||
        eye.invNear < 0 || eye.invFar < 0 || eye.invNear == eye.invFar) return false;
    eye.fov = {std::atan((-1-p[8])/p[0]), std::atan((1-p[8])/p[0]),
               std::atan((1-p[9])/p[5]), std::atan((-1-p[9])/p[5])};
    return true;
}

class CockpitMsaaResolve {
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    ComPtr<ID3D11ShaderResourceView> source_, output_;
    ID3D11Texture2D* input_{}; // Owned by source_.
    ComPtr<ID3D11CommandList> commands_;
    ComPtr<ID3D11ComputeShader> shaders_[2];
    UINT sampleCounts_[2]{};
    XrRect2Di rect_{};
    bool reverse_{};

    bool prepare(ID3D11Device* device, ID3D11ShaderResourceView* source,
                 const D3D11_TEXTURE2D_DESC& desc, const XrRect2Di& rect, bool reverse) {
        static constexpr char code[] = R"(
            cbuffer Region : register(b0) { uint2 origin; uint2 extent; }
            Texture2DMS<float, SAMPLES> depth : register(t0);
            RWTexture2D<float> result : register(u0);
            [numthreads(8,8,1)] void main(uint3 pos : SV_DispatchThreadID) {
                if(any(pos.xy >= extent)) return;
                uint2 pixel = origin + pos.xy;
                float z=depth.Load(pixel,0);
                [unroll] for(uint i=1;i<SAMPLES;++i) z=NEAREST(z,depth.Load(pixel,i));
                result[pos.xy]=z;
            })";
        if (!shaders_[reverse] || sampleCounts_[reverse]!=desc.SampleDesc.Count) {
            ComPtr<ID3DBlob> bytecode;
            const auto samples=std::to_string(desc.SampleDesc.Count);
            const D3D_SHADER_MACRO macros[] = {
                {"NEAREST",reverse?"max":"min"},{"SAMPLES",samples.c_str()},{nullptr,nullptr}};
            ComPtr<ID3D11ComputeShader> shader;
            if (FAILED(D3DCompile(code,sizeof(code)-1,nullptr,macros,nullptr,"main","cs_5_0",
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bytecode,nullptr)) ||
                FAILED(device->CreateComputeShader(bytecode->GetBufferPointer(),bytecode->GetBufferSize(),
                                                    nullptr,&shader))) return false;
            shaders_[reverse]=std::move(shader);
            sampleCounts_[reverse]=desc.SampleDesc.Count;
        }
        D3D11_TEXTURE2D_DESC target{};
        target.Width=rect.extent.width; target.Height=rect.extent.height; target.ArraySize=target.MipLevels=1;
        target.SampleDesc.Count=1; target.Format=DXGI_FORMAT_R32_FLOAT;
        target.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11UnorderedAccessView> uav;
        ComPtr<ID3D11ShaderResourceView> output;
        ComPtr<ID3D11DeviceContext> recorder;
        D3D11_SHADER_RESOURCE_VIEW_DESC sampled{};
        sampled.Format=target.Format; sampled.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        sampled.Texture2DArray.MipLevels=sampled.Texture2DArray.ArraySize=1;
        if (FAILED(device->CreateTexture2D(&target,nullptr,&texture)) ||
            FAILED(device->CreateUnorderedAccessView(texture.Get(),nullptr,&uav)) ||
            FAILED(device->CreateShaderResourceView(texture.Get(),&sampled,&output)) ||
            FAILED(device->CreateDeferredContext(0,&recorder))) return false;
        const UINT region[]{UINT(rect.offset.x),UINT(rect.offset.y),target.Width,target.Height};
        D3D11_BUFFER_DESC constants{};
        constants.ByteWidth=sizeof(region); constants.Usage=D3D11_USAGE_IMMUTABLE;
        constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{region};
        ComPtr<ID3D11Buffer> bounds;
        if (FAILED(device->CreateBuffer(&constants,&initial,&bounds))) return false;
        recorder->CSSetConstantBuffers(0,1,bounds.GetAddressOf());
        recorder->SetPredication(nullptr,FALSE);
        recorder->OMSetRenderTargets(0,nullptr,nullptr);
        recorder->CSSetShader(shaders_[reverse].Get(),nullptr,0);
        recorder->CSSetShaderResources(0,1,&source);
        recorder->CSSetUnorderedAccessViews(0,1,uav.GetAddressOf(),nullptr);
        recorder->Dispatch((target.Width+7)/8,(target.Height+7)/8,1);
        ComPtr<ID3D11CommandList> list;
        if (FAILED(recorder->FinishCommandList(FALSE,&list))) return false;
        source_=source; reverse_=reverse; rect_=rect; commands_=std::move(list); output_=std::move(output);
        return true;
    }
  public:
    ID3D11ShaderResourceView* capture(ID3D11Device* device, ID3D11DeviceContext* context,
            ID3D11ShaderResourceView* source, ID3D11Texture2D* texture,
            const D3D11_TEXTURE2D_DESC& desc, const XrRect2Di& rect, bool reverse) {
        if (!source || device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0) return nullptr;
        if (source!=source_.Get() || texture!=input_ || reverse_!=reverse ||
            rect.offset.x!=rect_.offset.x || rect.offset.y!=rect_.offset.y ||
            rect.extent.width!=rect_.extent.width || rect.extent.height!=rect_.extent.height) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv{}; source->GetDesc(&srv);
            if (srv.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2DMS ||
                (srv.Format!=DXGI_FORMAT_R32_FLOAT && srv.Format!=DXGI_FORMAT_R16_UNORM &&
                 srv.Format!=DXGI_FORMAT_R24_UNORM_X8_TYPELESS &&
                 srv.Format!=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS)) return nullptr;
            ComPtr<ID3D11Resource> input; source->GetResource(&input);
            if (input.Get()!=texture || !prepare(device,source,desc,rect,reverse)) return nullptr;
            input_=texture;
        }
        context->ExecuteCommandList(commands_.Get(),TRUE);
        return SUCCEEDED(device->GetDeviceRemovedReason()) ? output_.Get() : nullptr;
    }
};
}
