#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>
#include <algorithm>
#include <cstring>
#include <string>

namespace og {
inline std::wstring monitorIdentity(const RECT& bounds) {
    MONITORINFOEXW monitor{};
    monitor.cbSize=sizeof(monitor);
    if(!GetMonitorInfoW(MonitorFromRect(&bounds,MONITOR_DEFAULTTONULL),&monitor) || !EqualRect(&bounds,&monitor.rcMonitor))
        return {};
    DISPLAY_DEVICEW device{};
    device.cb=sizeof(device);
    if(!EnumDisplayDevicesW(monitor.szDevice,0,&device,EDD_GET_DEVICE_INTERFACE_NAME))return {};
    return device.DeviceID;
}
class MonitorClone {
    template<class T> using Com=Microsoft::WRL::ComPtr<T>;
    Com<ID3D11Device> device_;
    Com<ID3D11DeviceContext> context_;
    Com<IDXGIOutputDuplication> duplication_;
    Com<ID3D11Texture2D> staging_;
    std::vector<unsigned char> pixels_;
    unsigned width_{},height_{};
    DXGI_MODE_ROTATION rotation_{};
    ULONGLONG retryAt_{};
    bool start(const RECT& bounds,const std::wstring& identity) {
        if(identity.empty() || monitorIdentity(bounds)!=identity)return false;
        Com<IDXGIFactory1> factory;
        if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return false;
        for(UINT a=0;;++a){
            Com<IDXGIAdapter1> adapter;
            if(factory->EnumAdapters1(a,&adapter)!=S_OK)return false;
            for(UINT o=0;;++o){
                Com<IDXGIOutput> output;
                if(adapter->EnumOutputs(o,&output)!=S_OK)break;
                DXGI_OUTPUT_DESC desc{};
                if(FAILED(output->GetDesc(&desc)) || !desc.AttachedToDesktop || !EqualRect(&desc.DesktopCoordinates,&bounds))continue;
                Com<IDXGIOutput1> source;
                if(FAILED(output.As(&source)) ||
                   FAILED(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,
                       D3D11_SDK_VERSION,&device_,nullptr,&context_)) ||
                   FAILED(source->DuplicateOutput(device_.Get(),&duplication_)))return false;
                rotation_=desc.Rotation;
                return true;
            }
        }
    }
    HRESULT capture() {
        Com<IDXGIResource> resource;
        DXGI_OUTDUPL_FRAME_INFO info{};
        HRESULT hr=duplication_->AcquireNextFrame(0,&info,&resource);
        if(FAILED(hr))return hr;
        struct Release { IDXGIOutputDuplication* dup; ~Release(){dup->ReleaseFrame();} } release{duplication_.Get()};
        Com<ID3D11Texture2D> frame;
        if(FAILED(hr=resource.As(&frame)))return hr;
        D3D11_TEXTURE2D_DESC desc{};
        frame->GetDesc(&desc);
        if(desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM)return E_FAIL;
        if(!staging_){
            desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            if(FAILED(hr=device_->CreateTexture2D(&desc,nullptr,&staging_)))return hr;
            const bool portrait=rotation_==DXGI_MODE_ROTATION_ROTATE90 || rotation_==DXGI_MODE_ROTATION_ROTATE270;
            width_=portrait?desc.Height:desc.Width;height_=portrait?desc.Width:desc.Height;
            pixels_.resize(size_t(width_)*height_*4);
        }
        if(!info.LastPresentTime.QuadPart && hasImage_)return S_OK;
        context_->CopyResource(staging_.Get(),frame.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        if(FAILED(hr=context_->Map(staging_.Get(),0,D3D11_MAP_READ,0,&map)))return hr;
        for(UINT y=0;y<desc.Height;++y){
            const auto* src=static_cast<const unsigned char*>(map.pData)+size_t(y)*map.RowPitch;
            if(rotation_<=DXGI_MODE_ROTATION_IDENTITY){
                std::memcpy(pixels_.data()+size_t(y)*width_*4,src,size_t(width_)*4);
            }else for(UINT x=0;x<desc.Width;++x){
                const UINT px=rotation_==DXGI_MODE_ROTATION_ROTATE90?width_-1-y:
                    rotation_==DXGI_MODE_ROTATION_ROTATE180?width_-1-x:y;
                const UINT py=rotation_==DXGI_MODE_ROTATION_ROTATE90?x:
                    rotation_==DXGI_MODE_ROTATION_ROTATE180?height_-1-y:height_-1-x;
                std::memcpy(pixels_.data()+(size_t(py)*width_+px)*4,src+size_t(x)*4,4);
            }
        }
        context_->Unmap(staging_.Get(),0);hasImage_=true;
        return S_OK;
    }
    bool hasImage_{};
public:
    bool paint(HDC target,RECT client,const RECT& bounds,HWND window,const std::wstring& identity) {
        RECT windowBounds{},overlap{};
        if(GetWindowRect(window,&windowBounds) && IntersectRect(&overlap,&windowBounds,&bounds)){
            *this={};
            return false;
        }
        const auto now=GetTickCount64();
        if(!duplication_ && now<retryAt_)return false;
        if(!duplication_ && !start(bounds,identity)){*this={};retryAt_=now+1000;return false;}
        const HRESULT hr=capture();
        if(FAILED(hr)&&hr!=DXGI_ERROR_WAIT_TIMEOUT){*this={};retryAt_=now+1000;return false;}
        if(!hasImage_)return false;
        const double scale=std::min(double(client.right)/width_,double(client.bottom)/height_);
        const int w=int(width_*scale),h=int(height_*scale);
        BITMAPINFO info{};auto& b=info.bmiHeader;
        b.biSize=sizeof(b);b.biWidth=LONG(width_);b.biHeight=-LONG(height_);b.biPlanes=1;b.biBitCount=32;b.biCompression=BI_RGB;
        PatBlt(target,0,0,client.right,client.bottom,BLACKNESS);
        SetStretchBltMode(target,HALFTONE);SetBrushOrgEx(target,0,0,nullptr);
        return StretchDIBits(target,(client.right-w)/2,(client.bottom-h)/2,w,h,0,0,width_,height_,pixels_.data(),
            &info,DIB_RGB_COLORS,SRCCOPY)>0;
    }
};
}
