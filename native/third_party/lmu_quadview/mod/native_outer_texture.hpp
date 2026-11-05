#pragma once
#include <d3d11.h>
#include <utility>

namespace lmuqv {
namespace native_outer_detail {
template<class T>struct Com {
    T* p=nullptr;
    ~Com(){if(p)p->Release();}
    Com()=default;Com(const Com&)=delete;Com& operator=(const Com&)=delete;
};
inline bool support(ID3D11Device* device,DXGI_FORMAT format,UINT required){
    UINT flags=0;
    return SUCCEEDED(device->CheckFormatSupport(format,&flags))&&(flags&required)==required;
}
template<class T>bool belongs(T* view,ID3D11Texture2D* original,ID3D11Device* device){
    if(!view)return true;
    Com<ID3D11Resource> resource;view->GetResource(&resource.p);
    Com<ID3D11Device> owner;view->GetDevice(&owner.p);
    return resource.p==static_cast<ID3D11Resource*>(original)&&owner.p==device;
}
}

// Creates independent storage and matching views; never copies image contents.
// Each successful create replaces the previous clone. Failure preserves it.
// Getters are borrowed pointers valid until the next successful create/reset.
class NativeOuterTexture {
    ID3D11Texture2D* texture_=nullptr;
    ID3D11DepthStencilView* dsv_=nullptr;
    ID3D11ShaderResourceView* srv_=nullptr;
    ID3D11RenderTargetView* rtv_=nullptr;
    ID3D11UnorderedAccessView* uav_=nullptr;
    void swap(NativeOuterTexture& other)noexcept{
        std::swap(texture_,other.texture_);std::swap(dsv_,other.dsv_);
        std::swap(srv_,other.srv_);std::swap(rtv_,other.rtv_);std::swap(uav_,other.uav_);
    }
public:
    NativeOuterTexture()=default;
    NativeOuterTexture(const NativeOuterTexture&)=delete;
    NativeOuterTexture& operator=(const NativeOuterTexture&)=delete;
    ~NativeOuterTexture(){reset();}
    bool create(ID3D11Texture2D* original,ID3D11DepthStencilView* dsv,
                ID3D11ShaderResourceView* srv,ID3D11RenderTargetView* rtv,
                ID3D11UnorderedAccessView* uav,UINT width,UINT height,bool allowMipChain=false){
        using namespace native_outer_detail;
        if(!original||!width||!height||width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION||
           height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)return false;
        D3D11_TEXTURE2D_DESC desc{};original->GetDesc(&desc);
        if(width>desc.Width||height>desc.Height||desc.ArraySize!=1||(!allowMipChain&&desc.MipLevels!=1)||
           !desc.SampleDesc.Count||desc.Usage==D3D11_USAGE_IMMUTABLE)return false;
        // SSR exposes a whole mip chain through one SRV and a base-level RTV.
        // Shrinking storage removes only levels that cannot physically exist.
        // Per-mip RTV/DSV arrays and partial SRVs are deliberately unsupported.
        UINT mipLevels=desc.MipLevels;
        if(allowMipChain){
            if(dsv||uav||!srv||!rtv||desc.SampleDesc.Count!=1||
               (desc.MiscFlags&~D3D11_RESOURCE_MISC_GENERATE_MIPS))return false;
            UINT limit=1;for(UINT extent=width>height?width:height;extent>1;extent>>=1)++limit;
            if(mipLevels>limit)mipLevels=limit;
        }
        Com<ID3D11Device> device;original->GetDevice(&device.p);
        if(!device.p||FAILED(device.p->GetDeviceRemovedReason())||
           !support(device.p,desc.Format,D3D11_FORMAT_SUPPORT_TEXTURE2D)||
           !belongs(dsv,original,device.p)||!belongs(srv,original,device.p)||
           !belongs(rtv,original,device.p)||!belongs(uav,original,device.p))return false;
        const bool msaa=desc.SampleDesc.Count>1;
        D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
        if(dsv){
            dsv->GetDesc(&dd);
            if((msaa?dd.ViewDimension!=D3D11_DSV_DIMENSION_TEXTURE2DMS:
                dd.ViewDimension!=D3D11_DSV_DIMENSION_TEXTURE2D||dd.Texture2D.MipSlice!=0)||
               !support(device.p,dd.Format,D3D11_FORMAT_SUPPORT_DEPTH_STENCIL))return false;
        }
        if(srv){
            srv->GetDesc(&sd);
            if((msaa?sd.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2DMS:
                sd.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||sd.Texture2D.MostDetailedMip!=0||
                (sd.Texture2D.MipLevels!=desc.MipLevels&&sd.Texture2D.MipLevels!=UINT(-1)))||
               !support(device.p,sd.Format,D3D11_FORMAT_SUPPORT_SHADER_LOAD))return false;
        }
        if(allowMipChain)sd.Texture2D.MipLevels=mipLevels;
        if(rtv){
            rtv->GetDesc(&rd);
            if((msaa?rd.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2DMS:
                rd.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D||rd.Texture2D.MipSlice!=0)||
               !support(device.p,rd.Format,D3D11_FORMAT_SUPPORT_RENDER_TARGET))return false;
        }
        if(uav){
            uav->GetDesc(&ud);
            if(msaa||ud.ViewDimension!=D3D11_UAV_DIMENSION_TEXTURE2D||ud.Texture2D.MipSlice!=0||
               !support(device.p,ud.Format,D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW))return false;
        }
        // D3D creation validates exact format, usage, binding and MSAA quality
        // combinations. Only SSR may also trim the mip chain to fit W/H.
        desc.Width=width;desc.Height=height;desc.MipLevels=mipLevels;
        NativeOuterTexture candidate;
        if(FAILED(device.p->CreateTexture2D(&desc,nullptr,&candidate.texture_))||
           (dsv&&FAILED(device.p->CreateDepthStencilView(candidate.texture_,&dd,&candidate.dsv_)))||
           (srv&&FAILED(device.p->CreateShaderResourceView(candidate.texture_,&sd,&candidate.srv_)))||
           (rtv&&FAILED(device.p->CreateRenderTargetView(candidate.texture_,&rd,&candidate.rtv_)))||
           (uav&&FAILED(device.p->CreateUnorderedAccessView(candidate.texture_,&ud,&candidate.uav_)))||
           FAILED(device.p->GetDeviceRemovedReason()))return false;
        swap(candidate);return true;
    }
    void reset()noexcept{
        if(uav_)uav_->Release();
        if(rtv_)rtv_->Release();
        if(srv_)srv_->Release();
        if(dsv_)dsv_->Release();
        if(texture_)texture_->Release();
        texture_=nullptr;dsv_=nullptr;srv_=nullptr;rtv_=nullptr;uav_=nullptr;
    }
    ID3D11Texture2D* texture()const noexcept{return texture_;}
    ID3D11DepthStencilView* dsv()const noexcept{return dsv_;}
    ID3D11ShaderResourceView* srv()const noexcept{return srv_;}
    ID3D11RenderTargetView* rtv()const noexcept{return rtv_;}
    ID3D11UnorderedAccessView* uav()const noexcept{return uav_;}
};
}
