#pragma once
#include <d3d11.h>
#include <cstdio>
#include <iterator>
namespace lmuqv {
inline void diagnose_eye_resource(const char* stage,unsigned eye,ID3D11Texture2D* source,
                                 ID3D11RenderTargetView* rtv,ID3D11ShaderResourceView* srv,
                                 ID3D11DeviceContext* context,void(*log)(const char*)){
    D3D11_TEXTURE2D_DESC d{};if(source)source->GetDesc(&d);
    D3D11_RENDER_TARGET_VIEW_DESC rd{};ID3D11Resource* rr=nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};ID3D11Resource* sr=nullptr;
    if(rtv){rtv->GetDesc(&rd);rtv->GetResource(&rr);}
    if(srv){srv->GetDesc(&sd);srv->GetResource(&sr);}
    char line[640]{};std::snprintf(line,sizeof(line),
        "resource stage=%s eye=%u texture=%p size=%ux%u resourceFmt=%u samples=%u quality=%u mips=%u array=%u bind=%u rtv=%p rtvResource=%p rtvFmt=%u rtvDim=%u srv=%p srvResource=%p srvFmt=%u srvDim=%u",
        stage,eye,static_cast<void*>(source),d.Width,d.Height,unsigned(d.Format),d.SampleDesc.Count,d.SampleDesc.Quality,d.MipLevels,d.ArraySize,d.BindFlags,
        static_cast<void*>(rtv),static_cast<void*>(rr),unsigned(rd.Format),unsigned(rd.ViewDimension),static_cast<void*>(srv),static_cast<void*>(sr),unsigned(sd.Format),unsigned(sd.ViewDimension));log(line);
    if(rr)rr->Release();
    if(sr)sr->Release();
    if(context){
        D3D11_VIEWPORT v[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};UINT n=std::size(v);context->RSGetViewports(&n,v);
        D3D11_RECT r[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};UINT m=std::size(r);context->RSGetScissorRects(&m,r);
        std::snprintf(line,sizeof(line),"resource stage=%s eye=%u viewportCount=%u first=%.1f,%.1f %.1fx%.1f scissorCount=%u first=%ld,%ld,%ld,%ld",
            stage,eye,n,v[0].TopLeftX,v[0].TopLeftY,v[0].Width,v[0].Height,m,r[0].left,r[0].top,r[0].right,r[0].bottom);log(line);
    }
}
}
