#pragma once
#include <d3d11_1.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace lmuqv {
struct FocusFov {float left,right,up,down;};
namespace focus_detail {
struct Tangents {double left,right,up,down;};
inline bool tangents(FocusFov f,Tangents& out){
    const float angles[]={f.left,f.right,f.up,f.down};
    for(float a:angles)if(!std::isfinite(a)||std::abs(a)>=1.5707f)return false;
    out={std::tan(double(f.left)),std::tan(double(f.right)),std::tan(double(f.up)),std::tan(double(f.down))};
    return out.left<out.right&&out.down<out.up;
}
inline unsigned rectangles(Tangents view,Tangents focus,UINT width,UINT height,double inset,double stroke,std::array<D3D11_RECT,4>& result){
    const double fw=focus.right-focus.left,fh=focus.up-focus.down;
    double l=focus.left+fw*inset,r=focus.right-fw*inset,u=focus.up-fh*inset,d=focus.down+fh*inset;
    const double sx=fw*stroke,sy=fh*stroke;
    unsigned count=0;
    auto add=[&](double left,double right,double up,double down){
        const double x0=(left-view.left)/(view.right-view.left)*width,x1=(right-view.left)/(view.right-view.left)*width;
        const double y0=(view.up-up)/(view.up-view.down)*height,y1=(view.up-down)/(view.up-view.down)*height;
        auto clamp=[](double v,double maximum){return (std::max)(0.0,(std::min)(v,maximum));};
        D3D11_RECT rect{LONG(std::floor(clamp(x0,width))),LONG(std::floor(clamp(y0,height))),LONG(std::ceil(clamp(x1,width))),LONG(std::ceil(clamp(y1,height)))};
        if(rect.left<rect.right&&rect.top<rect.bottom)result[count++]=rect;
    };
    add(l,r,u,u-sy);add(l,r,d+sy,d);add(l,l+sx,u,d);add(r-sx,r,u,d);
    return count;
}
inline DXGI_FORMAT linear_format(DXGI_FORMAT format){
    if(format==DXGI_FORMAT_R8G8B8A8_TYPELESS||format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)return DXGI_FORMAT_R8G8B8A8_UNORM;
    if(format==DXGI_FORMAT_B8G8R8A8_TYPELESS||format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)return DXGI_FORMAT_B8G8R8A8_UNORM;
    if(format==DXGI_FORMAT_B8G8R8X8_TYPELESS||format==DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)return DXGI_FORMAT_B8G8R8X8_UNORM;
    return format;
}
inline HRESULT create_rtv(ID3D11Device* device,ID3D11Texture2D* texture,DXGI_FORMAT format,ID3D11RenderTargetView** output){
    if(!device||!texture||!output)return E_POINTER;
    *output=nullptr;
    D3D11_TEXTURE2D_DESC td{};texture->GetDesc(&td);
    if(td.SampleDesc.Count!=1||td.ArraySize!=1||td.MipLevels!=1||!(td.BindFlags&D3D11_BIND_RENDER_TARGET))return E_INVALIDARG;
    D3D11_RENDER_TARGET_VIEW_DESC desc{};desc.Format=linear_format(format);desc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    HRESULT hr=device->CreateRenderTargetView(texture,&desc,output);
    if(FAILED(hr)&&desc.Format!=format){desc.Format=format;hr=device->CreateRenderTargetView(texture,&desc,output);}
    return hr;
}
}
// ClearView writes explicit RTV rectangles without binding or altering any D3D11 pipeline state.
inline bool draw_focus_border(ID3D11DeviceContext* context,ID3D11RenderTargetView* target,FocusFov viewFov,FocusFov focusFov,bool enabled) {
    if(!enabled||!context||!target||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
    focus_detail::Tangents view{},focus{};
    if(!focus_detail::tangents(viewFov,view)||!focus_detail::tangents(focusFov,focus))return false;
    D3D11_RENDER_TARGET_VIEW_DESC rtv{};target->GetDesc(&rtv);
    if(rtv.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D||rtv.Texture2D.MipSlice)return false;
    ID3D11Resource* resource=nullptr;target->GetResource(&resource);if(!resource)return false;
    ID3D11Texture2D* texture=nullptr;HRESULT hr=resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture));resource->Release();
    if(FAILED(hr)||!texture)return false;
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    ID3D11Device *contextDevice=nullptr,*textureDevice=nullptr;context->GetDevice(&contextDevice);texture->GetDevice(&textureDevice);texture->Release();
    bool compatible=contextDevice&&contextDevice==textureDevice&&desc.SampleDesc.Count==1&&desc.ArraySize==1&&desc.MipLevels==1&&desc.Width&&desc.Height;
    if(textureDevice)textureDevice->Release();
    if(!compatible){if(contextDevice)contextDevice->Release();return false;}
    std::array<D3D11_RECT,4> black{},lime{};
    UINT blackCount=focus_detail::rectangles(view,focus,desc.Width,desc.Height,.02,.012,black);
    UINT limeCount=focus_detail::rectangles(view,focus,desc.Width,desc.Height,.023,.006,lime);
    if(!blackCount||!limeCount){contextDevice->Release();return false;}
    ID3D11DeviceContext1* context1=nullptr;
    hr=context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(&context1));
    if(FAILED(hr)||!context1){contextDevice->Release();return false;}
    const FLOAT dark[]={0,0,0,1},green[]={.5f,1,0,1};
    context1->ClearView(target,dark,black.data(),blackCount);context1->ClearView(target,green,lime.data(),limeCount);
    context1->Release();bool okay=SUCCEEDED(contextDevice->GetDeviceRemovedReason());contextDevice->Release();return okay;
}
}
