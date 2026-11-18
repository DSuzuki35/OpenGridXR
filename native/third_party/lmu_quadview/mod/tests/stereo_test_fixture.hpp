#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../stereo_shader.hpp"
#include "../stereo_instancing.hpp"
#include <d3dcompiler.h>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

namespace {
template<class T> void drop(T*& p){if(p){p->Release();p=nullptr;}}
ID3DBlob* compile(const char* source,const char* profile){
    ID3DBlob *code=nullptr,*errors=nullptr;const auto hr=D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,"main",profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if(FAILED(hr)&&errors)std::cerr<<static_cast<char*>(errors->GetBufferPointer());drop(errors);assert(SUCCEEDED(hr));return code;
}
const char* vsSource=R"(
cbuffer Object:register(b0){row_major float4x4 viewProj;float4 tint;};
cbuffer CameraExtra:register(b4){float4 extraColor;};
struct Out{float4 pos:SV_Position;float3 color:COLOR0;};
Out main(float3 p:POSITION,float3 c:COLOR0){Out o;o.pos=mul(float4(p,1),viewProj);o.color=c*tint.rgb+extraColor.rgb;return o;}
)";
const char* psSource=R"(
cbuffer Frame:register(b3){float4 scale;};
float4 main(float4 pos:SV_Position,float3 color:COLOR0):SV_Target{return float4(color*scale.rgb,1);}
)";
struct alignas(16) Camera {float matrix[16]{};float tint[4]{1,1,1,1};};
struct Image {std::array<std::vector<uint32_t>,2> color;std::array<std::vector<float>,2> depth;};
struct Fixture {
    static constexpr UINT width=96;
    ID3D11Device* d=nullptr;ID3D11DeviceContext* c=nullptr;
    ID3D11Texture2D *color=nullptr,*depth=nullptr;ID3D11RenderTargetView *rt=nullptr,*eyeRt[2]{};ID3D11DepthStencilView *ds=nullptr,*eyeDs[2]{};
    ID3D11VertexShader *referenceVs=nullptr,*pairedVs=nullptr;ID3D11PixelShader *referencePs=nullptr,*pairedPs=nullptr;ID3D11GeometryShader* gs=nullptr;
    ID3D11Buffer *vertices=nullptr,*indices=nullptr,*camera=nullptr,*extra=nullptr,*pixel=nullptr,*pairedCamera=nullptr,*pairedPixel=nullptr;
    ID3D11InputLayout* layout=nullptr;ID3D11RasterizerState* raster=nullptr;ID3D11DepthStencilState* depthState=nullptr;
    lmuqv::StereoShaderVariant vertex,pixelVariant;std::array<Camera,2> cameras;float scales[2][4]{{1,.75f,.5f,1},{.5f,1,.8f,1}};
    float extras[2][4]{{0,0,.125f,0},{.125f,0,0,0}};
    ID3D11Buffer* buffer(UINT bytes,UINT bind,const void* data=nullptr){D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;desc.BindFlags=bind;D3D11_SUBRESOURCE_DATA initial{data,0,0};ID3D11Buffer* b=nullptr;assert(SUCCEEDED(d->CreateBuffer(&desc,data?&initial:nullptr,&b)));return b;}
    Fixture(bool hardware,DXGI_FORMAT colorFormat=DXGI_FORMAT_R8G8B8A8_UNORM,UINT deviceFlags=0){
        D3D_FEATURE_LEVEL level{};assert(SUCCEEDED(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,deviceFlags,nullptr,0,D3D11_SDK_VERSION,&d,&level,&c)));
        D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=width;td.ArraySize=2;td.MipLevels=td.SampleDesc.Count=1;td.Format=colorFormat;td.BindFlags=D3D11_BIND_RENDER_TARGET;assert(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&color)));
        td.Format=DXGI_FORMAT_D32_FLOAT;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;assert(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&depth)));
        D3D11_RENDER_TARGET_VIEW_DESC r{};r.Format=colorFormat;r.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;r.Texture2DArray.ArraySize=2;assert(SUCCEEDED(d->CreateRenderTargetView(color,&r,&rt)));
        D3D11_DEPTH_STENCIL_VIEW_DESC s{};s.Format=DXGI_FORMAT_D32_FLOAT;s.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DARRAY;s.Texture2DArray.ArraySize=2;assert(SUCCEEDED(d->CreateDepthStencilView(depth,&s,&ds)));
        for(UINT i=0;i<2;++i){r.Texture2DArray.ArraySize=s.Texture2DArray.ArraySize=1;r.Texture2DArray.FirstArraySlice=s.Texture2DArray.FirstArraySlice=i;assert(SUCCEEDED(d->CreateRenderTargetView(color,&r,&eyeRt[i])));assert(SUCCEEDED(d->CreateDepthStencilView(depth,&s,&eyeDs[i])));}
        auto* code=compile(vsSource,"vs_4_0");assert(SUCCEEDED(d->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&referenceVs)));
        D3D11_INPUT_ELEMENT_DESC e[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}};
        assert(SUCCEEDED(d->CreateInputLayout(e,2,code->GetBufferPointer(),code->GetBufferSize(),&layout)));drop(code);
        code=compile(psSource,"ps_4_0");assert(SUCCEEDED(d->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&referencePs)));drop(code);
        const float v[]={-.65f,-.65f,.25f,1,0,0, 0,.7f,.25f,1,0,0, .65f,-.65f,.25f,1,0,0,
                         -.9f,-.9f,.75f,0,1,0, 0,.95f,.75f,0,1,0, .9f,-.9f,.75f,0,1,0};const UINT ix[]={0,1,2,3,4,5};
        vertices=buffer(sizeof(v),D3D11_BIND_VERTEX_BUFFER,v);indices=buffer(sizeof(ix),D3D11_BIND_INDEX_BUFFER,ix);camera=buffer(sizeof(Camera),D3D11_BIND_CONSTANT_BUFFER);extra=buffer(16,D3D11_BIND_CONSTANT_BUFFER);pixel=buffer(16,D3D11_BIND_CONSTANT_BUFFER);
        for(unsigned eye=0;eye<2;++eye){auto& m=cameras[eye].matrix;m[0]=eye?1.07f:.91f;m[5]=eye?.93f:1.06f;m[10]=m[15]=1;m[12]=eye?.13f:-.17f;cameras[eye].tint[0]=eye?.7f:1;cameras[eye].tint[1]=eye?1:.6f;}
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;assert(SUCCEEDED(d->CreateRasterizerState(&rd,&raster)));
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=TRUE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;dd.DepthFunc=D3D11_COMPARISON_LESS;assert(SUCCEEDED(d->CreateDepthStencilState(&dd,&depthState)));
    }
    void convert(bool direct){
        c->ClearState();drop(pairedVs);drop(pairedPs);drop(gs);drop(pairedCamera);drop(pairedPixel);std::string error;
        auto* code=compile(vsSource,"vs_4_0");const auto okay=lmuqv::convert_stereo_shader(code->GetBufferPointer(),code->GetBufferSize(),lmuqv::StereoShaderStage::vertex,1|(1u<<4),direct,vertex,error);if(!okay)std::cerr<<error<<'\n';assert(okay);drop(code);
        auto hr=d->CreateVertexShader(vertex.bytecode.data(),vertex.bytecode.size(),nullptr,&pairedVs);if(FAILED(hr))std::cerr<<"create VS 0x"<<std::hex<<unsigned(hr)<<std::dec<<'\n';assert(SUCCEEDED(hr));
        code=compile(psSource,"ps_4_0");const auto pixelOkay=lmuqv::convert_stereo_shader(code->GetBufferPointer(),code->GetBufferSize(),lmuqv::StereoShaderStage::pixel,1u<<3,false,pixelVariant,error);if(!pixelOkay)std::cerr<<error<<'\n';assert(pixelOkay);drop(code);hr=d->CreatePixelShader(pixelVariant.bytecode.data(),pixelVariant.bytecode.size(),nullptr,&pairedPs);if(FAILED(hr))std::cerr<<"create PS 0x"<<std::hex<<unsigned(hr)<<std::dec<<'\n';assert(SUCCEEDED(hr));
        if(!direct){code=compile(vertex.geometrySource.c_str(),"gs_5_0");assert(SUCCEEDED(d->CreateGeometryShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&gs)));drop(code);}
        assert(vertex.constants.size()==2&&vertex.constants[0].sourceSlot==0&&vertex.constants[1].sourceSlot==4&&vertex.eyeStride==6&&pixelVariant.eyeStride==1);
        std::vector<uint8_t> packed(2*vertex.eyeStride*16);
        for(unsigned eye=0;eye<2;++eye)for(const auto& range:vertex.constants){const void* source=range.sourceSlot==0?static_cast<const void*>(&cameras[eye]):static_cast<const void*>(extras[eye]);std::memcpy(packed.data()+16*(eye*vertex.eyeStride+range.offset),source,16*range.registers);}
        pairedCamera=buffer(UINT(packed.size()),D3D11_BIND_CONSTANT_BUFFER,packed.data());pairedPixel=buffer(2*pixelVariant.eyeStride*16,D3D11_BIND_CONSTANT_BUFFER,scales);
    }
    Image draw(bool stereo){
        const float clear[4]{0,0,0,1};c->ClearRenderTargetView(rt,clear);c->ClearDepthStencilView(ds,D3D11_CLEAR_DEPTH,1,0);const UINT stride=24,offset=0;c->IASetVertexBuffers(0,1,&vertices,&stride,&offset);c->IASetIndexBuffer(indices,DXGI_FORMAT_R32_UINT,0);c->IASetInputLayout(layout);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D11_VIEWPORT viewport{0,0,float(width),float(width),0,1};c->RSSetViewports(1,&viewport);c->RSSetState(raster);c->OMSetDepthStencilState(depthState,0);
        c->VSSetShader(stereo?pairedVs:referenceVs,nullptr,0);c->PSSetShader(stereo?pairedPs:referencePs,nullptr,0);c->GSSetShader(stereo?gs:nullptr,nullptr,0);
        if(stereo){c->VSSetConstantBuffers(13,1,&pairedCamera);c->PSSetConstantBuffers(13,1,&pairedPixel);c->OMSetRenderTargets(1,&rt,ds);assert(lmuqv::submit_stereo_indexed(c,6,1,0,0,0));}
        else for(unsigned eye=0;eye<2;++eye){c->UpdateSubresource(camera,0,nullptr,&cameras[eye],0,0);c->UpdateSubresource(extra,0,nullptr,extras[eye],0,0);c->UpdateSubresource(pixel,0,nullptr,scales[eye],0,0);c->VSSetConstantBuffers(0,1,&camera);c->VSSetConstantBuffers(4,1,&extra);c->PSSetConstantBuffers(3,1,&pixel);c->OMSetRenderTargets(1,&eyeRt[eye],eyeDs[eye]);c->DrawIndexed(6,0,0);}
        c->OMSetRenderTargets(0,nullptr,nullptr);Image result;
        auto read=[&](ID3D11Texture2D* source,unsigned eye,auto& values){D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);desc.ArraySize=1;desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Texture2D* staging=nullptr;assert(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&staging)));c->CopySubresourceRegion(staging,0,0,0,0,source,eye,nullptr);D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(c->Map(staging,0,D3D11_MAP_READ,0,&mapped)));values.resize(width*width);for(unsigned y=0;y<width;++y)std::memcpy(values.data()+y*width,static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch,width*4);c->Unmap(staging,0);drop(staging);};
        for(unsigned eye=0;eye<2;++eye){read(color,eye,result.color[eye]);read(depth,eye,result.depth[eye]);}return result;
    }
    ~Fixture(){c->ClearState();drop(pairedPixel);drop(pairedCamera);drop(pixel);drop(extra);drop(camera);drop(indices);drop(vertices);drop(layout);drop(referencePs);drop(referenceVs);drop(pairedPs);drop(pairedVs);drop(gs);drop(raster);drop(depthState);for(auto*& p:eyeRt)drop(p);for(auto*& p:eyeDs)drop(p);drop(rt);drop(ds);drop(color);drop(depth);drop(c);drop(d);}
};
inline std::vector<uint8_t> file(const char* path){std::ifstream stream(path,std::ios::binary);assert(stream);return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};}
}
