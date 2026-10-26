#include "image_processing.hpp"
#include "image_processing_shader.hpp"
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace og {
namespace {
using Microsoft::WRL::ComPtr;

DXGI_FORMAT family(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM: return DXGI_FORMAT_R10G10B10A2_TYPELESS;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
DXGI_FORMAT encodedFormat(DXGI_FORMAT f) {
    if(f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) return DXGI_FORMAT_R8G8B8A8_UNORM;
    if(f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) return DXGI_FORMAT_B8G8R8A8_UNORM;
    return f;
}

// Store every binding that our draw changes, including implicit OM UAV unbinding.
// Keep append/consume counters with -1 on restore. Predication must not suppress
// our pass, and the game's predicate must be restored afterward.
struct SavedState {
    ID3D11DeviceContext *c;
    std::array<ID3D11RenderTargetView*,8> targets{};
    ID3D11DepthStencilView *depthView{};
    std::array<ID3D11UnorderedAccessView*,64> uavs{};
    UINT uavSlots{}, targetCount{};
    ID3D11BlendState *blend{}; FLOAT blendFactor[4]{}; UINT sampleMask{};
    ID3D11DepthStencilState *depth{}; UINT stencil{};
    ID3D11RasterizerState *raster{};
    std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
    UINT viewportCount{UINT(viewports.size())};
    ID3D11InputLayout *layout{}; D3D11_PRIMITIVE_TOPOLOGY topology{};
    ID3D11VertexShader *vs{}; ID3D11PixelShader *ps{}; ID3D11GeometryShader *gs{};
    ID3D11HullShader *hs{}; ID3D11DomainShader *ds{};
    std::array<ID3D11ClassInstance*,256> vsClasses{},psClasses{},gsClasses{},hsClasses{},dsClasses{};
    UINT vsCount{256},psCount{256},gsCount{256},hsCount{256},dsCount{256};
    ID3D11Buffer *constants{}; ID3D11ShaderResourceView *resource{}; ID3D11SamplerState *sampler{};
    ComPtr<ID3D11DeviceContext1> context1;
    UINT firstConstant{},constantCount{};
    ID3D11Predicate *predicate{}; BOOL predicateValue{};
    explicit SavedState(ID3D11DeviceContext *context, D3D_FEATURE_LEVEL feature) : c(context) {
        c->OMGetRenderTargets(UINT(targets.size()),targets.data(),&depthView);
        for(UINT i=0;i<targets.size();++i) if(targets[i]) targetCount=i+1;
        uavSlots=feature>=D3D_FEATURE_LEVEL_11_1 ? 64u : 8u;
        c->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,uavSlots,uavs.data());
        c->OMGetBlendState(&blend,blendFactor,&sampleMask);
        c->OMGetDepthStencilState(&depth,&stencil);
        c->RSGetState(&raster); c->RSGetViewports(&viewportCount,viewports.data());
        c->IAGetInputLayout(&layout); c->IAGetPrimitiveTopology(&topology);
        c->VSGetShader(&vs,vsClasses.data(),&vsCount); c->PSGetShader(&ps,psClasses.data(),&psCount);
        c->GSGetShader(&gs,gsClasses.data(),&gsCount); c->HSGetShader(&hs,hsClasses.data(),&hsCount);
        c->DSGetShader(&ds,dsClasses.data(),&dsCount);
        if(SUCCEEDED(c->QueryInterface(IID_PPV_ARGS(&context1))))
            context1->PSGetConstantBuffers1(0,1,&constants,&firstConstant,&constantCount);
        else c->PSGetConstantBuffers(0,1,&constants);
        c->PSGetShaderResources(0,1,&resource);
        c->PSGetSamplers(0,1,&sampler); c->GetPredication(&predicate,&predicateValue);
    }
    template<class T> static void release(T *p) { if(p) p->Release(); }
    ~SavedState() {
        ID3D11ShaderResourceView *empty=nullptr; c->PSSetShaderResources(0,1,&empty);
        std::array<UINT,64> counts; counts.fill(UINT(-1));
        c->OMSetRenderTargetsAndUnorderedAccessViews(targetCount,targets.data(),depthView,
            targetCount,uavSlots-targetCount,uavs.data()+targetCount,counts.data());
        c->OMSetBlendState(blend,blendFactor,sampleMask); c->OMSetDepthStencilState(depth,stencil);
        c->RSSetState(raster); c->RSSetViewports(viewportCount,viewports.data());
        c->IASetInputLayout(layout); c->IASetPrimitiveTopology(topology);
        c->VSSetShader(vs,vsClasses.data(),vsCount); c->PSSetShader(ps,psClasses.data(),psCount);
        c->GSSetShader(gs,gsClasses.data(),gsCount); c->HSSetShader(hs,hsClasses.data(),hsCount);
        c->DSSetShader(ds,dsClasses.data(),dsCount);
        if(context1) context1->PSSetConstantBuffers1(0,1,&constants,&firstConstant,&constantCount);
        else c->PSSetConstantBuffers(0,1,&constants);
        c->PSSetShaderResources(0,1,&resource);
        c->PSSetSamplers(0,1,&sampler); c->SetPredication(predicate,predicateValue);
        for(auto p:targets) release(p); release(depthView);
        for(auto p:uavs) release(p);
        release(blend); release(depth); release(raster); release(layout);
        release(vs); release(ps); release(gs); release(hs); release(ds);
        for(auto p:vsClasses) release(p); for(auto p:psClasses) release(p);
        for(auto p:gsClasses) release(p); for(auto p:hsClasses) release(p); for(auto p:dsClasses) release(p);
        release(constants); release(resource); release(sampler); release(predicate);
    }
};
struct Constants {
    float reciprocalSize[2],minimumPixel[2],maximumPixel[2],slice,strength;
    float clamp,radius; UINT pattern,mode;
    float contrast,brightness,saturation; UINT colorFlags;
};
static_assert(sizeof(Constants)==64,"HLSL buffer layout");
} // namespace

struct ImageProcessor::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11Texture2D> resolved,output;
    ComPtr<ID3D11ShaderResourceView> sourceView;
    std::vector<ComPtr<ID3D11RenderTargetView>> targets;
    D3D11_TEXTURE2D_DESC cached{};
    DXGI_FORMAT cachedView{DXGI_FORMAT_UNKNOWN};
    HRESULT initialize() {
        if(vertex) return S_OK;
        ComPtr<ID3DBlob> code,errors;
        HRESULT hr=D3DCompile(imageProcessingShader,sizeof(imageProcessingShader)-1,
            "OpenGridXR image processing",nullptr,nullptr,"vertexMain","vs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(FAILED(hr)) return hr;
        ComPtr<ID3D11VertexShader> newVertex;
        hr=device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&newVertex);
        if(FAILED(hr)) return hr;
        code.Reset(); errors.Reset();
        hr=D3DCompile(imageProcessingShader,sizeof(imageProcessingShader)-1,
            "OpenGridXR image processing",nullptr,nullptr,"pixelMain","ps_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(FAILED(hr)) return hr;
        hr=device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&pixel);
        if(FAILED(hr)) return hr;
        D3D11_BUFFER_DESC cb{}; cb.ByteWidth=sizeof(Constants); cb.Usage=D3D11_USAGE_DEFAULT;
        cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        hr=device->CreateBuffer(&cb,nullptr,&constants); if(FAILED(hr)) return hr;
        D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD=D3D11_FLOAT32_MAX; sd.ComparisonFunc=D3D11_COMPARISON_NEVER;
        hr=device->CreateSamplerState(&sd,&sampler); if(FAILED(hr)) return hr;
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE;
        rd.DepthClipEnable=TRUE; rd.MultisampleEnable=TRUE;
        hr=device->CreateRasterizerState(&rd,&raster); if(FAILED(hr)) return hr;
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable=FALSE; dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
        hr=device->CreateDepthStencilState(&dd,&depth); if(FAILED(hr)) return hr;
        vertex=std::move(newVertex);
        return S_OK;
    }
    HRESULT prepare(const D3D11_TEXTURE2D_DESC &d, DXGI_FORMAT view) {
        if(output && cached.Width==d.Width && cached.Height==d.Height && cached.ArraySize==d.ArraySize &&
            cached.MipLevels==d.MipLevels && cached.Format==d.Format &&
            cached.SampleDesc.Count==d.SampleDesc.Count && cached.SampleDesc.Quality==d.SampleDesc.Quality &&
            cachedView==view) return S_OK;
        // Build all replacement resources before touching a game image or context state.
        D3D11_TEXTURE2D_DESC scratch=d;
        scratch.Usage=D3D11_USAGE_DEFAULT; scratch.CPUAccessFlags=0; scratch.MiscFlags=0;
        scratch.Format=family(d.Format); scratch.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> nextOutput,nextResolved;
        HRESULT hr=device->CreateTexture2D(&scratch,nullptr,&nextOutput); if(FAILED(hr)) return hr;
        scratch.SampleDesc={1,0}; scratch.MipLevels=1; scratch.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        hr=device->CreateTexture2D(&scratch,nullptr,&nextResolved); if(FAILED(hr)) return hr;
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format=view; sv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        sv.Texture2DArray.MipLevels=1; sv.Texture2DArray.ArraySize=d.ArraySize;
        ComPtr<ID3D11ShaderResourceView> nextSourceView;
        hr=device->CreateShaderResourceView(nextResolved.Get(),&sv,&nextSourceView); if(FAILED(hr)) return hr;
        std::vector<ComPtr<ID3D11RenderTargetView>> nextTargets(d.ArraySize);
        for(UINT slice=0;slice<d.ArraySize;++slice) {
            D3D11_RENDER_TARGET_VIEW_DESC rv{}; rv.Format=view;
            if(d.SampleDesc.Count>1) {
                rv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
                rv.Texture2DMSArray.FirstArraySlice=slice; rv.Texture2DMSArray.ArraySize=1;
            } else {
                rv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                rv.Texture2DArray.FirstArraySlice=slice; rv.Texture2DArray.ArraySize=1;
            }
            hr=device->CreateRenderTargetView(nextOutput.Get(),&rv,&nextTargets[slice]);
            if(FAILED(hr)) return hr;
        }
        output=std::move(nextOutput); resolved=std::move(nextResolved);
        sourceView=std::move(nextSourceView); targets=std::move(nextTargets);
        cached=d; cachedView=view;
        return S_OK;
    }
};
ImageProcessor::ImageProcessor(ID3D11Device *device, ID3D11DeviceContext *context) : impl_(new Impl) {
    impl_->device=device; impl_->context=context;
}
ImageProcessor::~ImageProcessor() = default;

ImageProcessResult ImageProcessor::process(ID3D11Texture2D *image, DXGI_FORMAT format,
    const std::vector<ImageRegion> &regions, const SharpeningSettings &settings,
    const SceneColorSettings &color) {
    const bool sharpen=sharpening_active(settings), grade=scene_color_active(color);
    if((!sharpen && !grade) || regions.empty()) return {};
    auto &p=*impl_;
    if(!image || !p.device || !p.context || p.context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE ||
        (settings.mode!=SharpeningMode::Off && settings.mode!=SharpeningMode::Luma && settings.mode!=SharpeningMode::Cas) ||
        !std::isfinite(settings.strength) || settings.strength<0 || settings.strength>100 ||
        !std::isfinite(settings.clamp) || settings.clamp<0 || settings.clamp>.1f ||
        !std::isfinite(settings.radius) || settings.radius<.1f || settings.radius>6 ||
        settings.pattern<1 || settings.pattern>4) return {ImageProcessStatus::Invalid,E_INVALIDARG};
    if(color.enabled && (!std::isfinite(color.contrast) || color.contrast<50 || color.contrast>150 ||
        !std::isfinite(color.brightness) || color.brightness<50 || color.brightness>150 ||
        !std::isfinite(color.saturation) || color.saturation<0 || color.saturation>150))
        return {ImageProcessStatus::Invalid,E_INVALIDARG};
    D3D11_TEXTURE2D_DESC desc{}; image->GetDesc(&desc);
    ComPtr<ID3D11Device> owner,contextOwner;
    image->GetDevice(&owner); p.context->GetDevice(&contextOwner);
    if(owner.Get()!=p.device.Get() || contextOwner.Get()!=p.device.Get())
        return {ImageProcessStatus::Invalid,E_INVALIDARG};
    const auto group=family(format);
    if(group==DXGI_FORMAT_UNKNOWN || group==format || family(desc.Format)!=group ||
        (desc.Format!=group && desc.Format!=format) || desc.Usage!=D3D11_USAGE_DEFAULT ||
        p.device->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0)
        return {ImageProcessStatus::Unsupported,DXGI_ERROR_UNSUPPORTED};
    for(size_t i=0;i<regions.size();++i) {
        const auto &r=regions[i];
        if(r.arraySlice>=desc.ArraySize || !r.width || !r.height || r.x>=desc.Width || r.y>=desc.Height ||
            r.width>desc.Width-r.x || r.height>desc.Height-r.y)
            return {ImageProcessStatus::Invalid,E_INVALIDARG};
        for(size_t j=0;j<i;++j) {
            const auto &q=regions[j];
            bool same=r.x==q.x && r.y==q.y && r.width==q.width && r.height==q.height;
            if(r.arraySlice==q.arraySlice && !same && r.x<q.x+q.width && q.x<r.x+r.width &&
                r.y<q.y+q.height && q.y<r.y+r.height) return {ImageProcessStatus::Invalid,E_INVALIDARG};
        }
    }
    UINT support=0;
    HRESULT hr=p.device->CheckFormatSupport(format,&support);
    if(FAILED(hr) || (desc.SampleDesc.Count>1 && !(support&D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE)))
        return {ImageProcessStatus::Unsupported,FAILED(hr)?hr:DXGI_ERROR_UNSUPPORTED};
    hr=p.initialize(); if(FAILED(hr)) return {ImageProcessStatus::Failed,hr};
    // LumaSharpen's original works in display-encoded values. CAS works in linear
    // values: the sRGB SRV/RTV pair performs the decode/encode in hardware.
    DXGI_FORMAT view=sharpen && settings.mode==SharpeningMode::Cas ? format : encodedFormat(format);
    UINT colorFlags=grade ? 1u : 0u;
    if(view==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || view==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) colorFlags|=2u;
    hr=p.prepare(desc,view); if(FAILED(hr)) return {ImageProcessStatus::Failed,hr};
    SavedState saved(p.context.Get(),p.device->GetFeatureLevel());
    p.context->SetPredication(nullptr,FALSE);
    p.context->CopyResource(p.output.Get(),image);
    for(UINT slice=0;slice<desc.ArraySize;++slice) {
        UINT subresource=D3D11CalcSubresource(0,slice,desc.MipLevels);
        if(desc.SampleDesc.Count>1)
            p.context->ResolveSubresource(p.resolved.Get(),slice,image,subresource,format);
        else p.context->CopySubresourceRegion(p.resolved.Get(),slice,0,0,0,image,subresource,nullptr);
    }
    p.context->IASetInputLayout(nullptr);
    p.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    p.context->VSSetShader(p.vertex.Get(),nullptr,0); p.context->PSSetShader(p.pixel.Get(),nullptr,0);
    p.context->GSSetShader(nullptr,nullptr,0); p.context->HSSetShader(nullptr,nullptr,0);
    p.context->DSSetShader(nullptr,nullptr,0);
    p.context->OMSetBlendState(nullptr,nullptr,UINT(-1)); p.context->OMSetDepthStencilState(p.depth.Get(),0);
    p.context->RSSetState(p.raster.Get());
    p.context->PSSetConstantBuffers(0,1,p.constants.GetAddressOf());
    p.context->PSSetShaderResources(0,1,p.sourceView.GetAddressOf());
    p.context->PSSetSamplers(0,1,p.sampler.GetAddressOf());
    for(const auto &r:regions) {
        Constants c{{1.f/desc.Width,1.f/desc.Height},{r.x+.5f,r.y+.5f},
            {r.x+r.width-.5f,r.y+r.height-.5f},float(r.arraySlice),settings.strength*.01f,
            settings.clamp,settings.radius,settings.pattern,sharpen?(settings.mode==SharpeningMode::Luma?1u:2u):0u,
            grade?color.contrast*.01f:1.f,grade?color.brightness*.01f:1.f,grade?color.saturation*.01f:1.f,colorFlags};
        p.context->UpdateSubresource(p.constants.Get(),0,nullptr,&c,0,0);
        D3D11_VIEWPORT viewport{float(r.x),float(r.y),float(r.width),float(r.height),0,1};
        p.context->RSSetViewports(1,&viewport);
        p.context->OMSetRenderTargets(1,p.targets[r.arraySlice].GetAddressOf(),nullptr);
        p.context->Draw(3,0);
    }
    // Detach the scratch output before the final copy. Every earlier fallible
    // allocation/compile/validation finished before the original image is changed.
    p.context->OMSetRenderTargets(0,nullptr,nullptr);
    p.context->CopyResource(image,p.output.Get());
    hr=p.device->GetDeviceRemovedReason();
    return {SUCCEEDED(hr)?ImageProcessStatus::Applied:ImageProcessStatus::Failed,hr};
}
} // namespace og
