#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../focus_overlay.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
template<class T> void drop(T*& pointer){if(pointer){pointer->Release();pointer=nullptr;}}
struct Fixture {
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    ID3D11Texture2D *texture=nullptr,*other=nullptr;
    ID3D11RenderTargetView *target=nullptr,*otherTarget=nullptr;
    ID3D11ShaderResourceView* resource=nullptr;
    ID3D11BlendState* blend=nullptr;ID3D11RasterizerState* raster=nullptr;ID3D11DepthStencilState* depth=nullptr;
    static constexpr uint32_t base=0x7f0a2040;
    Fixture(){
        D3D_FEATURE_LEVEL level{};
        assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=128;desc.ArraySize=desc.MipLevels=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&texture))&&SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&other)));
        assert(SUCCEEDED(device->CreateRenderTargetView(texture,nullptr,&target))&&SUCCEEDED(device->CreateRenderTargetView(other,nullptr,&otherTarget)));
        assert(SUCCEEDED(device->CreateShaderResourceView(texture,nullptr,&resource)));
        D3D11_BLEND_DESC bd{};bd.RenderTarget[0].BlendEnable=TRUE;bd.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend=D3D11_BLEND_ONE;bd.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;bd.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO;
        bd.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;bd.RenderTarget[0].RenderTargetWriteMask=1;
        assert(SUCCEEDED(device->CreateBlendState(&bd,&blend)));
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_WIREFRAME;rd.CullMode=D3D11_CULL_FRONT;rd.ScissorEnable=TRUE;
        assert(SUCCEEDED(device->CreateRasterizerState(&rd,&raster)));
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=TRUE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;dd.DepthFunc=D3D11_COMPARISON_LESS;
        assert(SUCCEEDED(device->CreateDepthStencilState(&dd,&depth)));
        resetPixels();
        context->OMSetRenderTargets(1,&otherTarget,nullptr);context->PSSetShaderResources(7,1,&resource);
        const float factor[]={.1f,.2f,.3f,.4f};context->OMSetBlendState(blend,factor,0x13579bdf);
        context->OMSetDepthStencilState(depth,17);context->RSSetState(raster);
        D3D11_VIEWPORT vp{5,7,30,31,.2f,.8f};D3D11_RECT scissor{11,13,20,21};
        context->RSSetViewports(1,&vp);context->RSSetScissorRects(1,&scissor);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
    }
    void resetPixels(){std::vector<uint32_t> pixels(128*128,base);context->UpdateSubresource(texture,0,nullptr,pixels.data(),128*4,0);}
    std::vector<uint32_t> pixels(){
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.BindFlags=0;
        ID3D11Texture2D* staging=nullptr;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)));
        context->CopyResource(staging,texture);D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&mapped)));
        std::vector<uint32_t> result(128*128);
        for(unsigned y=0;y<128;++y)std::memcpy(result.data()+128*y,static_cast<const unsigned char*>(mapped.pData)+size_t(y)*mapped.RowPitch,128*4);
        context->Unmap(staging,0);staging->Release();return result;
    }
    void unchangedState(){
        ID3D11RenderTargetView* t=nullptr;ID3D11DepthStencilView* dsv=nullptr;context->OMGetRenderTargets(1,&t,&dsv);assert(t==otherTarget&&!dsv);drop(t);
        ID3D11ShaderResourceView* srv=nullptr;context->PSGetShaderResources(7,1,&srv);assert(srv==resource);drop(srv);
        ID3D11BlendState* b=nullptr;float factor[4]{};UINT mask=0;context->OMGetBlendState(&b,factor,&mask);
        assert(b==blend&&factor[0]==.1f&&factor[3]==.4f&&mask==0x13579bdf);drop(b);
        ID3D11RasterizerState* r=nullptr;context->RSGetState(&r);assert(r==raster);drop(r);
        ID3D11DepthStencilState* ds=nullptr;UINT ref=0;context->OMGetDepthStencilState(&ds,&ref);assert(ds==depth&&ref==17);drop(ds);
        UINT count=1;D3D11_VIEWPORT vp{};context->RSGetViewports(&count,&vp);assert(count==1&&vp.TopLeftX==5&&vp.Height==31&&vp.MinDepth==.2f&&vp.MaxDepth==.8f);
        count=1;D3D11_RECT sc{};context->RSGetScissorRects(&count,&sc);assert(count==1&&sc.left==11&&sc.bottom==21);
        D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);assert(topology==D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
    }
    ~Fixture(){context->ClearState();drop(depth);drop(raster);drop(blend);drop(resource);drop(target);drop(otherTarget);drop(texture);drop(other);drop(context);drop(device);}
};
lmuqv::FocusFov fov(float l,float r,float u,float d){return {std::atan(l),std::atan(r),std::atan(u),std::atan(d)};}
bool lime(uint32_t pixel){return ((pixel>>8)&255)==255&&((pixel>>16)&255)==0&&(pixel&255)>80&&(pixel>>24)==255;}
}
int main(){
    Fixture fixture;const auto outer=fov(-1,1,1,-1),focus=fov(-.5f,.5f,.5f,-.5f);
    assert(!lmuqv::draw_focus_border(fixture.context,fixture.target,outer,focus,false));
    for(auto p:fixture.pixels())assert(p==Fixture::base);
    assert(lmuqv::draw_focus_border(fixture.context,fixture.target,outer,focus,true));
    auto pixels=fixture.pixels();assert(lime(pixels[64*128+33]));assert(pixels[64*128+64]==Fixture::base&&pixels[10*128+10]==Fixture::base);
    fixture.unchangedState();
    fixture.resetPixels();auto right=fov(-.25f,.75f,.5f,-.5f);
    assert(lmuqv::draw_focus_border(fixture.context,fixture.target,outer,right,true));pixels=fixture.pixels();
    assert(lime(pixels[64*128+49])&&pixels[64*128+33]==Fixture::base);fixture.unchangedState();
    fixture.resetPixels();auto left=fov(-.75f,.25f,.5f,-.5f);
    assert(lmuqv::draw_focus_border(fixture.context,fixture.target,outer,left,true));pixels=fixture.pixels();
    assert(lime(pixels[64*128+17])&&pixels[64*128+49]==Fixture::base);
    fixture.resetPixels();assert(lmuqv::draw_focus_border(fixture.context,fixture.target,right,right,true));pixels=fixture.pixels();
    assert(lime(pixels[64*128+3])&&pixels[64*128+64]==Fixture::base);
    fixture.resetPixels();auto invalid=focus;invalid.left=invalid.right;
    assert(!lmuqv::draw_focus_border(fixture.context,fixture.target,outer,invalid,true));
    for(auto p:fixture.pixels())assert(p==Fixture::base);
    fixture.unchangedState();
    std::cout<<"focus overlay WARP tests passed: visible border, unchanged interior/state, moving/asymmetric FOV, inner view, disabled/invalid no-op\n";
}
