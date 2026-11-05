#pragma once
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <array>
#include <cstring>
#include <cmath>
#include <charconv>

namespace lmuqv {
inline bool parse_outer_blur(const char* text,float& radius){
    radius=0;
    if(!text||!*text)return true;
    const auto end=text+std::strlen(text);float value=0;
    const auto parsed=std::from_chars(text,end,value,std::chars_format::general);
    if(parsed.ec!=std::errc{}||parsed.ptr!=end||!std::isfinite(value)||value<0||value>3)return false;
    radius=value;return true;
}
namespace outer_resample_detail {
template<class T> class Com {
    T* pointer_{};
public:
    Com()=default;
    Com(const Com&)=delete;
    Com& operator=(const Com&)=delete;
    ~Com(){reset();}
    T* get()const{return pointer_;}
    T* operator->()const{return pointer_;}
    T** put(){return &pointer_;} // Only used on empty holders.
    void reset(){if(pointer_)pointer_->Release();pointer_=nullptr;}
    void retain(T* p){reset();pointer_=p;if(p)p->AddRef();}
};
inline unsigned family(DXGI_FORMAT format){
    switch(format){
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:return 1;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:return 2;
    default:return 0;
    }
}
inline bool srgb(DXGI_FORMAT f){return f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;}
inline bool typed(DXGI_FORMAT f){return family(f)&&f!=DXGI_FORMAT_R8G8B8A8_TYPELESS&&f!=DXGI_FORMAT_B8G8R8A8_TYPELESS;}
inline DXGI_FORMAT source_view(DXGI_FORMAT source,DXGI_FORMAT destination){
    if(typed(source))return source;
    if(family(source)==1)return srgb(destination)?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;
    return srgb(destination)?DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:DXGI_FORMAT_B8G8R8A8_UNORM;
}
}

// Render-thread-owned output resampler. Requires D3D11.1 context-state support;
// unsupported contexts/formats fail closed. No CPU wait/readback or Flush.
// First valid draw lazily creates shaders/pipeline state; first use of a source
// creates one cached SRV. At most eight source/format pairs are retained, with
// no eviction/recompilation/allocation on subsequent cached draws. reset() must
// run on the same owning thread after draw has returned.
class OuterResample {
    template<class T> using Com=outer_resample_detail::Com<T>;
    Com<ID3D11Device> device_;
    Com<ID3D11DeviceContext1> context1_;
    Com<ID3DDeviceContextState> state_;
    Com<ID3D11VertexShader> vs_;
    Com<ID3D11PixelShader> ps_;
    Com<ID3D11PixelShader> blurPs_;
    Com<ID3D11Buffer> blurConstants_;
    Com<ID3D11SamplerState> sampler_;
    Com<ID3D11RasterizerState> raster_;
    Com<ID3D11BlendState> blend_;
    Com<ID3D11DepthStencilState> depth_;
    struct Entry {
        Com<ID3D11Texture2D> source;
        Com<ID3D11ShaderResourceView> view;
        DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    };
    std::array<Entry,8> cache_;
    unsigned cacheCount_=0;
    bool failed_=false;

    bool initialize(ID3D11Device* device,ID3D11DeviceContext* context){
        if(device_.get())return device_.get()==device&&!failed_;
        device_.retain(device);
        // Failure is latched until reset(), avoiding repeated shader compilation
        // or resource creation on a device which cannot support this path.
        failed_=true;
        Com<ID3D11Device1> device1;
        if(FAILED(device->QueryInterface(__uuidof(ID3D11Device1),reinterpret_cast<void**>(device1.put())))||
           FAILED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(context1_.put()))))return false;
        const D3D_FEATURE_LEVEL level=device->GetFeatureLevel();
        if(level<D3D_FEATURE_LEVEL_10_0||level>D3D_FEATURE_LEVEL_11_1)return false;
        const UINT flags=(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)
            ?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
        if(FAILED(device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,
            __uuidof(ID3D11Device),nullptr,state_.put())))return false;
        constexpr const char* source=R"(
struct V {float4 position:SV_Position;float2 uv:TEXCOORD0;};
V VS(uint id:SV_VertexID){V v;v.uv=float2((id<<1)&2,id&2);
v.position=float4(v.uv*float2(2,-2)+float2(-1,1),0,1);return v;}
Texture2D image:register(t0);SamplerState linearClamp:register(s0);
float4 PS(V v):SV_Target{return image.SampleLevel(linearClamp,v.uv,0);}
cbuffer BlurParameters:register(b0){float2 blurStep;float2 unused;};
float4 PSBlur(V v):SV_Target{
    float4 color=image.SampleLevel(linearClamp,v.uv,0)*0.5;
    color+=image.SampleLevel(linearClamp,v.uv+float2(blurStep.x,0),0)*0.125;
    color+=image.SampleLevel(linearClamp,v.uv-float2(blurStep.x,0),0)*0.125;
    color+=image.SampleLevel(linearClamp,v.uv+float2(0,blurStep.y),0)*0.125;
    color+=image.SampleLevel(linearClamp,v.uv-float2(0,blurStep.y),0)*0.125;
    return color;
}
)";
        Com<ID3DBlob> vertex,pixel,errors;
        const UINT compileFlags=D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3;
        if(FAILED(D3DCompile(source,std::strlen(source),"outer-resample",nullptr,nullptr,
            "VS","vs_4_0",compileFlags,0,vertex.put(),errors.put())))return false;
        errors.reset();
        if(FAILED(D3DCompile(source,std::strlen(source),"outer-resample",nullptr,nullptr,
            "PS","ps_4_0",compileFlags,0,pixel.put(),errors.put())))return false;
        if(FAILED(device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,vs_.put()))||
           FAILED(device->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,ps_.put())))return false;
        pixel.reset();errors.reset();
        if(FAILED(D3DCompile(source,std::strlen(source),"outer-blur",nullptr,nullptr,
            "PSBlur","ps_4_0",compileFlags,0,pixel.put(),errors.put()))||
           FAILED(device->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,blurPs_.put())))return false;
        D3D11_BUFFER_DESC constants{};constants.ByteWidth=16;constants.Usage=D3D11_USAGE_DEFAULT;
        constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if(FAILED(device->CreateBuffer(&constants,nullptr,blurConstants_.put())))return false;
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
        if(FAILED(device->CreateSamplerState(&sampler,sampler_.put())))return false;
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        if(FAILED(device->CreateRasterizerState(&raster,raster_.put())))return false;
        D3D11_BLEND_DESC blend{};blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        if(FAILED(device->CreateBlendState(&blend,blend_.put())))return false;
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
        if(FAILED(device->CreateDepthStencilState(&depth,depth_.put())))return false;
        failed_=false;return true;
    }
    ID3D11ShaderResourceView* sourceView(ID3D11Texture2D* source,DXGI_FORMAT format){
        for(unsigned i=0;i<cacheCount_;++i)
            if(cache_[i].source.get()==source&&cache_[i].format==format)return cache_[i].view.get();
        if(cacheCount_==cache_.size())return nullptr;
        auto& entry=cache_[cacheCount_++];entry.source.retain(source);entry.format=format;
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format=format;desc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;desc.Texture2D.MipLevels=1;
        // Failed views are retained as negative cache entries; no repeated
        // allocation attempts for the same source/format every frame.
        if(FAILED(device_->CreateShaderResourceView(source,&desc,entry.view.put())))return nullptr;
        return entry.view.get();
    }
public:
    OuterResample()=default;
    OuterResample(const OuterResample&)=delete;
    OuterResample& operator=(const OuterResample&)=delete;
    bool draw(ID3D11DeviceContext* context,ID3D11Texture2D* source,
              ID3D11RenderTargetView* destination,unsigned width,unsigned height,float blurRadius=0.f,
              const D3D11_VIEWPORT* content=nullptr){
        using namespace outer_resample_detail;
        if(!std::isfinite(blurRadius)||blurRadius<0||blurRadius>3||!context||!source||!destination||!width||!height||
           width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION||height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION||
           context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
        if(content&&(!std::isfinite(content->TopLeftX)||!std::isfinite(content->TopLeftY)||
           !std::isfinite(content->Width)||!std::isfinite(content->Height)||
           content->TopLeftX<0||content->TopLeftY<0||content->Width<=0||content->Height<=0||
           content->TopLeftX+content->Width>float(width)||content->TopLeftY+content->Height>float(height)||
           content->MinDepth!=0||content->MaxDepth!=1))return false;
        D3D11_TEXTURE2D_DESC input{};source->GetDesc(&input);
        D3D11_RENDER_TARGET_VIEW_DESC outputView{};destination->GetDesc(&outputView);
        if(!family(input.Format)||!typed(outputView.Format)||!input.Width||!input.Height||
           input.ArraySize!=1||input.MipLevels!=1||input.SampleDesc.Count!=1||input.SampleDesc.Quality||
           !(input.BindFlags&D3D11_BIND_SHADER_RESOURCE)||
           outputView.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D||outputView.Texture2D.MipSlice)return false;
        Com<ID3D11Resource> resource;destination->GetResource(resource.put());
        Com<ID3D11Texture2D> output;
        if(!resource.get()||FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(output.put())))||
           output.get()==source)return false;
        D3D11_TEXTURE2D_DESC target{};output->GetDesc(&target);
        if(target.Width!=width||target.Height!=height||target.ArraySize!=1||target.MipLevels!=1||
           target.SampleDesc.Count!=1||target.SampleDesc.Quality||!(target.BindFlags&D3D11_BIND_RENDER_TARGET)||
           family(target.Format)!=family(outputView.Format))return false;
        Com<ID3D11Device> device,sourceDevice,targetDevice;
        context->GetDevice(device.put());source->GetDevice(sourceDevice.put());destination->GetDevice(targetDevice.put());
        if(!device.get()||device.get()!=sourceDevice.get()||device.get()!=targetDevice.get()||
           FAILED(device->GetDeviceRemovedReason())||!initialize(device.get(),context))return false;
        auto* srv=sourceView(source,source_view(input.Format,outputView.Format));
        if(!srv)return false;
        // Microsoft: immediate-context state objects capture the D3D pipeline;
        // the prior/default state object is returned even on the first swap.
        Com<ID3DDeviceContextState> previous;
        context1_->SwapDeviceContextState(state_.get(),previous.put());
        context->ClearState();
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs_.get(),nullptr,0);context->PSSetShader(blurRadius>0?blurPs_.get():ps_.get(),nullptr,0);
        if(blurRadius>0){
            const float parameters[4]{blurRadius/float(width),blurRadius/float(height),0,0};
            context->UpdateSubresource(blurConstants_.get(),0,nullptr,parameters,0,0);
            auto* buffer=blurConstants_.get();context->PSSetConstantBuffers(0,1,&buffer);
        }
        ID3D11SamplerState* sampler=sampler_.get();context->PSSetSamplers(0,1,&sampler);
        context->PSSetShaderResources(0,1,&srv);
        context->OMSetRenderTargets(1,&destination,nullptr);
        context->OMSetBlendState(blend_.get(),nullptr,0xffffffffu);
        context->OMSetDepthStencilState(depth_.get(),0);context->RSSetState(raster_.get());
        // Embed a cropped projection in a full-FOV peripheral image. QVFR's
        // compositor assumes its original peripheral FOV regardless of submission.
        if(content){const float black[4]{0,0,0,1};context->ClearRenderTargetView(destination,black);}
        const D3D11_VIEWPORT viewport=content?*content:D3D11_VIEWPORT{0,0,float(width),float(height),0,1};
        context->RSSetViewports(1,&viewport);context->Draw(3,0);
        // Do not retain the acquired destination or source in our dormant
        // context-state object. No shader writes touch application resources.
        context->ClearState();
        context1_->SwapDeviceContextState(previous.get(),nullptr);
        return SUCCEEDED(device->GetDeviceRemovedReason());
    }
    void reset(){
        for(auto& entry:cache_){entry.view.reset();entry.source.reset();entry.format=DXGI_FORMAT_UNKNOWN;}
        cacheCount_=0;state_.reset();vs_.reset();ps_.reset();blurPs_.reset();blurConstants_.reset();sampler_.reset();raster_.reset();
        blend_.reset();depth_.reset();context1_.reset();device_.reset();failed_=false;
    }
};
}
