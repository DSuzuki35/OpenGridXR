#include "lmu_mask.hpp"
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstring>
namespace og {
using Microsoft::WRL::ComPtr;
struct LmuMask::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11DeviceContext1> context1;
    ComPtr<ID3DDeviceContextState> cleanState;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11RasterizerState> raster;
    struct Eye { ComPtr<ID3D11Buffer> vertices; XrFovf fov{}; UINT count{}; } eyes[2];
    bool initialize() {
        if(vertex)return true;
        ComPtr<ID3D11Device1> device1;
        if(FAILED(device.As(&device1))||FAILED(context.As(&context1)))return false;
        const D3D_FEATURE_LEVEL levels[]={device->GetFeatureLevel()};
        if(FAILED(device1->CreateDeviceContextState(0,levels,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&cleanState)))return false;
        constexpr char source[]=R"(
cbuffer Geometry : register(b0) { float4 bounds; };
float4 vs(float2 ray : POSITION) : SV_POSITION { return float4((ray.x-bounds.x)*bounds.z-1,(ray.y-bounds.y)*bounds.w-1,0,1); }
float4 ps() : SV_TARGET { return float4(0,0,0,1); }
)";
        ComPtr<ID3DBlob> code,error;
        if(FAILED(D3DCompile(source,sizeof(source)-1,"OpenGrid aperture",nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error)))return false;
        ComPtr<ID3D11VertexShader> newVertex;
        if(FAILED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&newVertex)))return false;
        const D3D11_INPUT_ELEMENT_DESC input={"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        if(FAILED(device->CreateInputLayout(&input,1,code->GetBufferPointer(),code->GetBufferSize(),&layout)))return false;
        code.Reset();error.Reset();
        if(FAILED(D3DCompile(source,sizeof(source)-1,"OpenGrid aperture",nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error)))return false;
        if(FAILED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&pixel)))return false;
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=16;cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if(FAILED(device->CreateBuffer(&cb,nullptr,&constants)))return false;
        D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;
        if(FAILED(device->CreateRasterizerState(&rs,&raster)))return false;
        vertex=std::move(newVertex);return true;
    }
};
LmuMask::LmuMask(ID3D11Device* device,ID3D11DeviceContext* context):impl(std::make_unique<Impl>()){impl->device=device;impl->context=context;}
LmuMask::~LmuMask()=default;
ImageProcessStatus LmuMask::draw(ID3D11Texture2D* texture,DXGI_FORMAT format,const Config& config,unsigned eye,XrFovf full,XrFovf rendered){
    if(!config.mask)return ImageProcessStatus::Skipped;
    if(!texture||eye>1||!valid_polygon(config.contours[eye]))return ImageProcessStatus::Invalid;
    const double l=std::tan(rendered.angleLeft),r=std::tan(rendered.angleRight),u=std::tan(rendered.angleUp),d=std::tan(rendered.angleDown);
    if(!std::isfinite(l)||!std::isfinite(r)||!std::isfinite(u)||!std::isfinite(d)||r<=l||u<=d)return ImageProcessStatus::Invalid;
    auto& p=*impl;if(!p.initialize())return ImageProcessStatus::Unsupported;
    auto& cached=p.eyes[eye];
    if(!cached.vertices||std::memcmp(&cached.fov,&full,sizeof(full))){
        auto mesh=hidden_mesh(config.contours[eye],config.padding,{std::tan(full.angleLeft),std::tan(full.angleRight),std::tan(full.angleUp),std::tan(full.angleDown)});
        if(mesh.indices.empty())return ImageProcessStatus::Skipped;
        std::vector<Point> points;points.reserve(mesh.indices.size());
        for(auto index:mesh.indices)points.push_back(mesh.vertices[index]);
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(points.size()*sizeof(Point));desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=points.data();ComPtr<ID3D11Buffer> buffer;
        if(FAILED(p.device->CreateBuffer(&desc,&data,&buffer)))return ImageProcessStatus::Failed;
        cached.vertices=std::move(buffer);cached.fov=full;cached.count=UINT(points.size());
    }
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    if(desc.ArraySize!=1||desc.SampleDesc.Count!=1)return ImageProcessStatus::Unsupported;
    D3D11_RENDER_TARGET_VIEW_DESC rv{};rv.Format=format;rv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> target;if(FAILED(p.device->CreateRenderTargetView(texture,&rv,&target)))return ImageProcessStatus::Unsupported;
    const float bounds[]={float(l),float(d),float(2/(r-l)),float(2/(u-d))};p.context->UpdateSubresource(p.constants.Get(),0,nullptr,bounds,0,0);
    struct Restore { ID3D11DeviceContext1* c;ComPtr<ID3DDeviceContextState> prior;~Restore(){c->SwapDeviceContextState(prior.Get(),nullptr);} } restore{p.context1.Get()};
    p.context1->SwapDeviceContextState(p.cleanState.Get(),&restore.prior);
    UINT stride=sizeof(Point),offset=0;p.context->IASetVertexBuffers(0,1,cached.vertices.GetAddressOf(),&stride,&offset);
    p.context->IASetInputLayout(p.layout.Get());p.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    p.context->VSSetShader(p.vertex.Get(),nullptr,0);p.context->PSSetShader(p.pixel.Get(),nullptr,0);
    p.context->VSSetConstantBuffers(0,1,p.constants.GetAddressOf());p.context->RSSetState(p.raster.Get());
    D3D11_VIEWPORT viewport{0,0,float(desc.Width),float(desc.Height),0,1};p.context->RSSetViewports(1,&viewport);
    p.context->OMSetRenderTargets(1,target.GetAddressOf(),nullptr);p.context->Draw(cached.count,0);
    return ImageProcessStatus::Applied;
}
}
