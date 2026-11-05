#include "lmu_stream.hpp"
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
namespace og {
using Microsoft::WRL::ComPtr;
struct LmuStream::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11DeviceContext1> context1;
    ComPtr<ID3DDeviceContextState> clean;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11Texture2D> output;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11ShaderResourceView> views[2];
    struct Constants { float outer[4], focus[4], rotation[4], edge[4], focusRect[4]; };
    bool initialize() {
        if(vs)return true;
        ComPtr<ID3D11Device1> d;
        if(FAILED(device.As(&d))||FAILED(context.As(&context1)))return false;
        const D3D_FEATURE_LEVEL levels[]={device->GetFeatureLevel()};
        if(FAILED(d->CreateDeviceContextState(0,levels,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&clean)))return false;
        constexpr char shader[]=R"(
Texture2D outerImage:register(t0);Texture2D focusImage:register(t1);
SamplerState sampleLinear:register(s0);
cbuffer Geometry:register(b0){float4 outerFov;float4 focusFov;float4 q;float4 edge;float4 focusRect;}
struct V{float4 position:SV_Position;float2 uv:TEXCOORD0;};
V vs(uint id:SV_VertexID){V v;v.uv=float2((id<<1)&2,id&2);v.position=float4(v.uv*float2(2,-2)+float2(-1,1),0,1);return v;}
float4 ps(V v):SV_Target{
 float3 color=outerImage.Sample(sampleLinear,v.uv).rgb;
 float3 ray=float3(lerp(outerFov.x,outerFov.y,v.uv.x),lerp(outerFov.z,outerFov.w,v.uv.y),-1);
 ray+=2*cross(q.xyz,cross(q.xyz,ray)+q.w*ray);
 if(ray.z<-.0001){
  float2 uv=float2((ray.x/-ray.z-focusFov.x)/(focusFov.y-focusFov.x),(focusFov.z-ray.y/-ray.z)/(focusFov.z-focusFov.w));
  if(all(uv>=0)&&all(uv<=1)){
   float2 weight=saturate(min(uv,1-uv)/edge.xy);
   float2 sampleUv=focusRect.xy+uv*focusRect.zw;
   sampleUv=clamp(sampleUv,focusRect.xy+edge.zw,focusRect.xy+focusRect.zw-edge.zw);
   color=lerp(color,focusImage.Sample(sampleLinear,sampleUv).rgb,min(weight.x,weight.y));
  }
 }
 return float4(color,1);
})";
        ComPtr<ID3DBlob> code,error;
        ComPtr<ID3D11VertexShader> vertex;
        if(FAILED(D3DCompile(shader,sizeof(shader)-1,"OpenGrid stream",nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error))||
           FAILED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vertex)))return false;
        code.Reset();error.Reset();
        if(FAILED(D3DCompile(shader,sizeof(shader)-1,"OpenGrid stream",nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error))||
           FAILED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&ps)))return false;
        D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(Constants);b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if(FAILED(device->CreateBuffer(&b,nullptr,&constants)))return false;
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;
        if(FAILED(device->CreateSamplerState(&s,&sampler)))return false;
        D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;
        if(FAILED(device->CreateRasterizerState(&rs,&raster)))return false;
        vs=std::move(vertex);return true;
    }
};
LmuStream::LmuStream(ID3D11Device* d,ID3D11DeviceContext* c):impl(std::make_unique<Impl>()){impl->device=d;impl->context=c;}
LmuStream::~LmuStream()=default;
ID3D11Texture2D* LmuStream::compose(ID3D11Texture2D* outer,ID3D11Texture2D* focus,
    XrFovf outerFov,XrFovf focusFov,XrPosef outerPose,XrPosef focusPose,XrRect2Di focusRect,unsigned width,unsigned height){
    if(!outer||!focus||!width||!height)return nullptr;
    // A 2D ray compositor cannot fix different eye origins without depth.
    const auto a=outerPose.position,b=focusPose.position;
    if(!std::isfinite(a.x+a.y+a.z+b.x+b.y+b.z)||std::abs(a.x-b.x)+std::abs(a.y-b.y)+std::abs(a.z-b.z)>0.0001f)return nullptr;
    Impl::Constants values{};
    auto fov=[](XrFovf f,float* out){out[0]=std::tan(f.angleLeft);out[1]=std::tan(f.angleRight);out[2]=std::tan(f.angleUp);out[3]=std::tan(f.angleDown);
        return std::isfinite(out[0])&&std::isfinite(out[1])&&std::isfinite(out[2])&&std::isfinite(out[3])&&out[1]>out[0]&&out[2]>out[3];};
    if(!fov(outerFov,values.outer)||!fov(focusFov,values.focus))return nullptr;
    auto normalize=[](XrQuaternionf& q){float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);if(!std::isfinite(n)||n<.001f)return false;q.x/=n;q.y/=n;q.z/=n;q.w/=n;return true;};
    auto o=outerPose.orientation,f=focusPose.orientation;
    if(!normalize(o)||!normalize(f))return nullptr;
    // inverse(focus) * outer maps an outer camera ray into the submitted focus camera.
    values.rotation[0]=f.w*o.x-f.x*o.w-f.y*o.z+f.z*o.y;
    values.rotation[1]=f.w*o.y+f.x*o.z-f.y*o.w-f.z*o.x;
    values.rotation[2]=f.w*o.z-f.x*o.y+f.y*o.x-f.z*o.w;
    values.rotation[3]=f.w*o.w+f.x*o.x+f.y*o.y+f.z*o.z;
    D3D11_TEXTURE2D_DESC od{},fd{};outer->GetDesc(&od);focus->GetDesc(&fd);
    if(od.ArraySize!=1||fd.ArraySize!=1||od.SampleDesc.Count!=1||fd.SampleDesc.Count!=1||od.Format!=fd.Format)return nullptr;
    if(focusRect.offset.x<0||focusRect.offset.y<0||focusRect.extent.width<=0||focusRect.extent.height<=0||
       uint64_t(focusRect.offset.x)+focusRect.extent.width>fd.Width||uint64_t(focusRect.offset.y)+focusRect.extent.height>fd.Height)return nullptr;
    // Preserve the outer aspect; never composite at the reduced peripheral resolution.
    const double scale=std::max({1.,double(width)/od.Width,double(height)/od.Height});
    const double bounded=std::min(scale,8192./std::max(od.Width,od.Height));
    const UINT w=UINT(std::ceil(od.Width*bounded)),h=UINT(std::ceil(od.Height*bounded));
    values.edge[0]=2.f/focusRect.extent.width;values.edge[1]=2.f/focusRect.extent.height;
    values.edge[2]=.5f/fd.Width;values.edge[3]=.5f/fd.Height;
    values.focusRect[0]=float(focusRect.offset.x)/fd.Width;values.focusRect[1]=float(focusRect.offset.y)/fd.Height;
    values.focusRect[2]=float(focusRect.extent.width)/fd.Width;values.focusRect[3]=float(focusRect.extent.height)/fd.Height;
    auto& p=*impl;if(!p.initialize())return nullptr;
    ID3D11Texture2D* sources[]={outer,focus};
    for(unsigned i=0;i<2;++i){ComPtr<ID3D11Resource> previous;if(p.views[i])p.views[i]->GetResource(&previous);
        if(previous.Get()!=sources[i]){p.views[i].Reset();if(FAILED(p.device->CreateShaderResourceView(sources[i],nullptr,&p.views[i])))return nullptr;}}
    D3D11_TEXTURE2D_DESC old{};if(p.output)p.output->GetDesc(&old);
    if(!p.output||old.Width!=w||old.Height!=h||old.Format!=od.Format){
        p.target.Reset();p.output.Reset();auto d=od;d.Width=w;d.Height=h;d.MipLevels=1;d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=d.MiscFlags=0;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        if(FAILED(p.device->CreateTexture2D(&d,nullptr,&p.output))||FAILED(p.device->CreateRenderTargetView(p.output.Get(),nullptr,&p.target))){p.output.Reset();return nullptr;}
    }
    struct Restore{ID3D11DeviceContext1* c;ComPtr<ID3DDeviceContextState> previous;~Restore(){c->SwapDeviceContextState(previous.Get(),nullptr);}} restore{p.context1.Get()};
    p.context1->SwapDeviceContextState(p.clean.Get(),&restore.previous);
    p.context->UpdateSubresource(p.constants.Get(),0,nullptr,&values,0,0);
    p.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    p.context->VSSetShader(p.vs.Get(),nullptr,0);p.context->PSSetShader(p.ps.Get(),nullptr,0);
    p.context->RSSetState(p.raster.Get());p.context->OMSetBlendState(nullptr,nullptr,0xffffffff);
    p.context->PSSetConstantBuffers(0,1,p.constants.GetAddressOf());p.context->PSSetSamplers(0,1,p.sampler.GetAddressOf());
    ID3D11ShaderResourceView* views[]={p.views[0].Get(),p.views[1].Get()};p.context->PSSetShaderResources(0,2,views);
    D3D11_VIEWPORT viewport{0,0,float(w),float(h),0,1};p.context->RSSetViewports(1,&viewport);
    p.context->OMSetRenderTargets(1,p.target.GetAddressOf(),nullptr);p.context->Draw(3,0);
    // Remove bindings from the cached context state, too.
    ID3D11ShaderResourceView* empty[2]{};p.context->PSSetShaderResources(0,2,empty);p.context->OMSetRenderTargets(0,nullptr,nullptr);
    return p.output.Get();
}
}
