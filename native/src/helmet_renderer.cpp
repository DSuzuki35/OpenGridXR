#include "runtime_paths.hpp"
#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#ifndef XR_USE_GRAPHICS_API_D3D12
#define XR_USE_GRAPHICS_API_D3D12
#endif
#include "helmet_renderer.hpp"
#include "windows_memory.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <d3d11_4.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <fstream>
#include <mutex>
#ifdef XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#endif
#include <openxr/openxr_platform.h>
#include <wincodec.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace og {
namespace {
struct Bitmap { UINT width{},height{}; std::vector<unsigned char> pixels; };
struct Apartment {
    HRESULT hr{CoInitializeEx(nullptr,COINIT_MULTITHREADED)};
    ~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}
};
bool png(const std::filesystem::path &path,Bitmap &out) {
    std::error_code ec; auto bytes=std::filesystem::file_size(path,ec);
    if(ec||bytes<32||bytes>32*1024*1024)return false;
    Apartment apartment;
    if(FAILED(apartment.hr)&&apartment.hr!=RPC_E_CHANGED_MODE)return false;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||
       FAILED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder)))return false;
    GUID container{};UINT count{};
    if(FAILED(decoder->GetContainerFormat(&container))||container!=GUID_ContainerFormatPng||
       FAILED(decoder->GetFrameCount(&count))||count!=1||FAILED(decoder->GetFrame(0,&frame))||
       FAILED(frame->GetSize(&out.width,&out.height))||!out.width||!out.height||out.width>4096||out.height>4096)return false;
    if(FAILED(factory->CreateFormatConverter(&converter))||
       FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return false;
    out.pixels.resize(size_t(out.width)*out.height*4);
    return SUCCEEDED(converter->CopyPixels(nullptr,out.width*4,UINT(out.pixels.size()),out.pixels.data()));
}
bool resize(Bitmap &bitmap,UINT width,UINT height) {
    Apartment apartment;
    if(FAILED(apartment.hr)&&apartment.hr!=RPC_E_CHANGED_MODE)return false;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmap> source;
    ComPtr<IWICFormatConverter> premultiplied,straight;
    ComPtr<IWICBitmapScaler> scaler;
    // Interpolate premultiplied pixels so transparent borders cannot introduce
    // dark/color fringes. Return straight RGBA for the existing OpenXR contract.
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||
       FAILED(factory->CreateBitmapFromMemory(bitmap.width,bitmap.height,GUID_WICPixelFormat32bppRGBA,
            bitmap.width*4,UINT(bitmap.pixels.size()),bitmap.pixels.data(),&source))||
       FAILED(factory->CreateFormatConverter(&premultiplied))||
       FAILED(premultiplied->Initialize(source.Get(),GUID_WICPixelFormat32bppPRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))||
       FAILED(factory->CreateBitmapScaler(&scaler))||
       FAILED(scaler->Initialize(premultiplied.Get(),width,height,WICBitmapInterpolationModeFant))||
       FAILED(factory->CreateFormatConverter(&straight))||
       FAILED(straight->Initialize(scaler.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return false;
    std::vector<unsigned char> pixels(size_t(width)*height*4);
    if(FAILED(straight->CopyPixels(nullptr,width*4,UINT(pixels.size()),pixels.data())))return false;
    bitmap={width,height,std::move(pixels)};return true;
}
std::filesystem::path own_directory() {
    HMODULE module{};wchar_t path[32768]{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&own_directory),&module))return {};
    DWORD n=GetModuleFileNameW(module,path,32768);
    return n&&n<32768?og::runtime_data_directory(std::filesystem::path(path).parent_path()):std::filesystem::path{};
}
struct Eye {
    XrSwapchain swap{};
    Bitmap bitmap;
    std::vector<ComPtr<ID3D11Texture2D>> textures;
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    uint32_t acquired{UINT32_MAX};
    bool waited{},ready{};
    UINT sourceWidth{},sourceHeight{},contentWidth{},contentHeight{},padX{},padY{};
};
UINT padding(UINT content,float margin) {return margin>0?UINT(std::ceil(content*margin))+1:0;}
bool fit_for_offsets(Eye &eye,bool moving,float stereoOffset,UINT maxWidth,UINT maxHeight) {
    auto &b=eye.bitmap;eye.sourceWidth=b.width;eye.sourceHeight=b.height;
    const float marginX=std::abs(stereoOffset)*.01f+(moving?.03f:0),marginY=moving?.03f:0;
    auto largest=[](UINT source,UINT limit,float margin) {
        if(!limit)return source;
        UINT lo=0,hi=source;
        while(lo<hi) {
            const UINT mid=lo+(hi-lo+1)/2;
            if(uint64_t(mid)+2*uint64_t(padding(mid,margin))<=limit)lo=mid;else hi=mid-1;
        }
        return lo;
    };
    const double ratio=std::min(double(largest(b.width,maxWidth,marginX))/b.width,
                                double(largest(b.height,maxHeight,marginY))/b.height);
    if(ratio>=1)return true;
    const UINT width=UINT(std::floor(b.width*ratio)),height=UINT(std::floor(b.height*ratio));
    // Very small runtime limits cannot retain usable helmet detail. Fail safely
    // rather than silently turn the helmet into an indistinct low-resolution mask.
    return width>=256&&height>=144&&resize(b,width,height);
}
void pad_for_offsets(Eye &eye,bool moving,float stereoOffset) {
    auto &b=eye.bitmap;eye.contentWidth=b.width;eye.contentHeight=b.height;
    const float marginX=std::abs(stereoOffset)*.01f+(moving?.03f:0);
    const float marginY=moving?.03f:0;
    if(marginX==0&&marginY==0)return;
    eye.padX=padding(b.width,marginX);eye.padY=padding(b.height,marginY);
    UINT width=b.width+2*eye.padX,height=b.height+2*eye.padY;
    std::vector<unsigned char> padded(size_t(width)*height*4);
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x) {
        UINT sx=UINT(std::clamp(int(x)-int(eye.padX),0,int(b.width)-1));
        UINT sy=UINT(std::clamp(int(y)-int(eye.padY),0,int(b.height)-1));
        std::copy_n(b.pixels.data()+(size_t(sy)*b.width+sx)*4,4,padded.data()+(size_t(y)*width+x)*4);
    }
    b.width=width;b.height=height;b.pixels=std::move(padded);
}
} // namespace
struct HelmetRenderer::Impl {
    mutable std::mutex mutex;
    OverlayDispatch api{}; PFN_xrLocateViews locateViews{};
    XrSession session{};XrSpace view{},local{};uint32_t maxLayers{},maxWidth{},maxHeight{};
    XrViewConfigurationType viewType{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
    std::filesystem::path directory,configPath;
    std::filesystem::file_time_type stamp{};
    bool stampKnown{},supported{};
    std::string game;
    uint64_t lastPoll{};
    HelmetConfig config;
    HelmetStatus report;
    HelmetMotionState motion;
    HelmetVehicleSample vehicle;
    bool externalVehicle{};
    uint64_t vehicleSetTime{},lastTelemetryPoll{},lastToken{},lastAdvance{};
    bool telemetrySeeded{},telemetryAdvanced{};
    telemetry::MappedMemory telemetryMemory,telemetryLock;
    telemetry::IRacingDecoder iracing;
    telemetry::LmuDecoder lmu;
    std::array<Eye,2> eyes;
    std::array<Eye,2> pendingEyes;
    bool pending{},shortcutHeld{},externalShortcut{};
    uint64_t cycleDeadline{};
    HelmetShortcutSample shortcut;
    HelmetVisorPosition position{HelmetVisorPosition::Closed},pendingPosition{HelmetVisorPosition::Closed};
    std::string loadedManifest;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11On12Device> on12;
    ComPtr<ID3D11Device> source11;
    ComPtr<ID3D12Device> source12;
    ComPtr<ID3D12CommandQueue> sourceQueue;
    ~Impl(){reset();}
    void clear(Eye &eye) {
        // Session/config teardown owns these private swapchains. Do not submit a
        // half-loaded eye, and never touch or destroy an application's swapchain.
        if(eye.swap&&eye.acquired!=UINT32_MAX) {
            if(!eye.waited) {
                XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=0;
                eye.waited=api.wait(eye.swap,&wait)==XR_SUCCESS;
            }
            if(eye.waited) { XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};api.release(eye.swap,&release); }
        }
        eye.textures.clear();
        if(eye.swap&&api.destroy)api.destroy(eye.swap);
        eye=Eye{};
    }
    void releaseGraphics() {
        if(view&&api.destroySpace)api.destroySpace(view);
        if(local&&api.destroySpace)api.destroySpace(local);
        view=local=XR_NULL_HANDLE;
        on12.Reset();context.Reset();device.Reset();
    }
    void reset() {
        for(auto &eye:eyes)clear(eye);
        for(auto &eye:pendingEyes)clear(eye);
        pending=false;cycleDeadline=0;shortcutHeld=false;externalShortcut=false;shortcut={};loadedManifest.clear();
        releaseGraphics();source11.Reset();source12.Reset();sourceQueue.Reset();
        session=XR_NULL_HANDLE; supported=false;
        telemetryMemory.close();telemetryLock.close();telemetrySeeded=telemetryAdvanced=false;
        config={};report={};motion={};
        vehicle={};externalVehicle=false;stampKnown=false;lastPoll=0;
        viewType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    }
    bool retainBinding(const void *chain) {
        for(auto n=static_cast<const XrBaseInStructure*>(chain);n;n=n->next) {
            if(n->type==XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
                source11=reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(n)->device;
                return source11 != nullptr;
            }
            if(n->type==XR_TYPE_GRAPHICS_BINDING_D3D12_KHR) {
                auto b=reinterpret_cast<const XrGraphicsBindingD3D12KHR*>(n);
                if(!b->device||!b->queue)return false;
                source12=b->device;sourceQueue=b->queue;return true;
            }
        }
        return false;
    }
    bool graphics() {
        if(device&&context&&view&&local)return true;
        releaseGraphics();
        if(source11) {device=source11;device->GetImmediateContext(&context);}
        else if(source12&&sourceQueue) {
            IUnknown *queues[]{sourceQueue.Get()};
            if(FAILED(D3D11On12CreateDevice(source12.Get(),D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,
                                           queues,1,0,&device,&context,nullptr)))return false;
            if(FAILED(device.As(&on12)))return false;
        }
        if(!device||!context)return false;
        ComPtr<ID3D11Multithread> guard;
        if(SUCCEEDED(context.As(&guard)))guard->SetMultithreadProtected(TRUE);
        XrReferenceSpaceCreateInfo ci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};ci.poseInReferenceSpace.orientation.w=1;
        ci.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
        if(XR_FAILED(api.createSpace(session,&ci,&view)))return false;
        ci.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
        if(XR_FAILED(api.createSpace(session,&ci,&local)))return false;
        return true;
    }
    bool create(Eye &eye,unsigned index) {
        if((maxWidth&&eye.bitmap.width>maxWidth)||(maxHeight&&eye.bitmap.height>maxHeight))return false;
        uint32_t count{};
        if(XR_FAILED(api.formats(session,0,&count,nullptr))||!count||count>256)return false;
        std::vector<int64_t> formats(count);
        if(XR_FAILED(api.formats(session,count,&count,formats.data())))return false;
        int64_t format{};
        for(auto f:{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM})
            if(std::find(formats.begin(),formats.end(),f)!=formats.end()){format=f;break;}
        if(!format)return false;
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.createFlags=XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
        ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        ci.format=format;ci.width=eye.bitmap.width;ci.height=eye.bitmap.height;
        ci.arraySize=ci.faceCount=ci.mipCount=ci.sampleCount=1;
        auto result=api.create(session,&ci,&eye.swap);
        if(result==XR_ERROR_FEATURE_UNSUPPORTED) {ci.createFlags=0;result=api.create(session,&ci,&eye.swap);}
        if(XR_FAILED(result)||XR_FAILED(api.images(eye.swap,0,&count,nullptr))||!count||count>16)return false;
        eye.textures.resize(count);
        if(on12) {
            std::vector<XrSwapchainImageD3D12KHR> images(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
            if(XR_FAILED(api.images(eye.swap,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()))))return false;
            D3D11_RESOURCE_FLAGS flags{};flags.BindFlags=D3D11_BIND_RENDER_TARGET;
            for(unsigned i=0;i<count;++i)
                if(FAILED(on12->CreateWrappedResource(images[i].texture,&flags,D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,IID_PPV_ARGS(&eye.textures[i]))))return false;
        } else {
            std::vector<XrSwapchainImageD3D11KHR> images(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
            if(XR_FAILED(api.images(eye.swap,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()))))return false;
            for(unsigned i=0;i<count;++i)eye.textures[i]=images[i].texture;
        }
        eye.quad.space=view;
        eye.quad.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
        eye.quad.eyeVisibility=index?XR_EYE_VISIBILITY_RIGHT:XR_EYE_VISIBILITY_LEFT;
        eye.quad.subImage.swapchain=eye.swap;
        eye.quad.subImage.imageRect={{int32_t(eye.padX),int32_t(eye.padY)},
                                    {int32_t(eye.contentWidth),int32_t(eye.contentHeight)}};
        return true;
    }
    bool upload(Eye &eye,bool fatal=true) {
        if(eye.ready)return true;
        if(eye.acquired==UINT32_MAX) {
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            if(XR_FAILED(api.acquire(eye.swap,&acquire,&eye.acquired))){report.error="Helmet image acquisition failed";if(fatal)config.enabled=false;return false;}
        }
        if(!eye.waited) {
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=0;
            auto result=api.wait(eye.swap,&wait);
            if(result!=XR_SUCCESS){if(result!=XR_TIMEOUT_EXPIRED){report.error="Helmet image wait failed";if(fatal)config.enabled=false;}return false;}
            eye.waited=true;
        }
        if(eye.acquired>=eye.textures.size()||!eye.textures[eye.acquired]){report.error="Invalid helmet swapchain image";if(fatal)config.enabled=false;return false;}
        ID3D11Resource *target=eye.textures[eye.acquired].Get();
        if(on12)on12->AcquireWrappedResources(&target,1);
        // Upload straight RGBA once. UpdateSubresource changes no pipeline bindings,
        // so the application's graphics state is untouched; no per-frame draw/copy.
        context->UpdateSubresource(target,0,nullptr,eye.bitmap.pixels.data(),eye.bitmap.width*4,0);
        if(on12){on12->ReleaseWrappedResources(&target,1);context->Flush();}
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        auto released=api.release(eye.swap,&release);
        eye.acquired=UINT32_MAX;eye.waited=false;
        if(XR_FAILED(released)||FAILED(device->GetDeviceRemovedReason())){report.error="Helmet image upload failed";if(fatal)config.enabled=false;return false;}
        eye.ready=true;eye.bitmap.pixels.clear();eye.bitmap.pixels.shrink_to_fit();++report.uploads;return true;
    }
    std::string imageFor(HelmetVisorPosition selected) const {
        if(config.visorHotkey&&selected==HelmetVisorPosition::Half)return config.halfImage;
        if(config.visorHotkey&&selected==HelmetVisorPosition::Open)return config.openImage;
        return config.leftImage;
    }
    bool manifestUnchanged() const {
        std::error_code ec;const auto bytes=std::filesystem::file_size(configPath,ec);
        if(ec||bytes>2048)return false;
        std::ifstream file(configPath,std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(file)),{});
        return file&&text==loadedManifest;
    }
    void changeVisor() {
        // Only a key edge performs file IO or allocates replacement resources.
        if(pending)return;
        const auto next=position==HelmetVisorPosition::Closed?HelmetVisorPosition::Half:
                        position==HelmetVisorPosition::Half?HelmetVisorPosition::Open:HelmetVisorPosition::Closed;
        if(!manifestUnchanged()||!png(directory/imageFor(next),pendingEyes[0].bitmap)||!manifestUnchanged()||
           pendingEyes[0].bitmap.width!=eyes[0].sourceWidth||pendingEyes[0].bitmap.height!=eyes[0].sourceHeight||
           !fit_for_offsets(pendingEyes[0],config.headMotion>0||config.vehicleMotion>0,config.stereoOffset,maxWidth,maxHeight)||
           pendingEyes[0].bitmap.width!=eyes[0].contentWidth||pendingEyes[0].bitmap.height!=eyes[0].contentHeight) {
            for(auto &eye:pendingEyes)clear(eye);
            report.error="Visor image unavailable or changed; keeping current visor";return;
        }
        pendingEyes[1].bitmap=pendingEyes[0].bitmap;
        pendingEyes[1].sourceWidth=pendingEyes[0].sourceWidth;pendingEyes[1].sourceHeight=pendingEyes[0].sourceHeight;
        for(auto &eye:pendingEyes)pad_for_offsets(eye,config.headMotion>0||config.vehicleMotion>0,config.stereoOffset);
        if(!create(pendingEyes[0],0)||!create(pendingEyes[1],1)) {
            for(auto &eye:pendingEyes)clear(eye);
            report.error="Visor swapchain unavailable; keeping current visor";return;
        }
        pendingPosition=next;pending=true;report.error.clear();
    }
    void finishVisor() {
        if(!pending)return;
        for(auto &eye:pendingEyes)if(!upload(eye,false)) {
            if(!report.error.empty()) {
                for(auto &other:pendingEyes)clear(other);
                pending=false;
            }
            return;
        }
        for(auto &eye:eyes)clear(eye);
        eyes.swap(pendingEyes);position=pendingPosition;pending=false;report.error.clear();
    }
    HelmetShortcutSample keyboard() const {
        if(externalShortcut)return shortcut;
        DWORD foregroundProcess{};
        GetWindowThreadProcessId(GetForegroundWindow(),&foregroundProcess);
        return {(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0,(GetAsyncKeyState(VK_MENU)&0x8000)!=0,
                (GetAsyncKeyState('V')&0x8000)!=0,foregroundProcess==GetCurrentProcessId()};
    }
    void poll(bool force=false) {
        auto now=GetTickCount64();if(!force&&now-lastPoll<500)return;lastPoll=now;
        std::error_code ec;auto modified=std::filesystem::last_write_time(configPath,ec);
        if(!ec&&stampKnown&&modified==stamp)return;
        if(ec&&!stampKnown)return;
        stampKnown=!ec;stamp=modified;
        for(auto &eye:eyes)clear(eye);
        for(auto &eye:pendingEyes)clear(eye);
        pending=false;cycleDeadline=0;loadedManifest.clear();
        config={};motion={};report.ready=false;report.imageReduced=false;report.error.clear();telemetryMemory.close();telemetryLock.close();
        telemetrySeeded=telemetryAdvanced=false;vehicle={};
        if(ec){releaseGraphics();return;}
        auto size=std::filesystem::file_size(configPath,ec);
        if(ec||size>2048){releaseGraphics();report.error="Invalid helmet configuration size";return;}
        std::ifstream file(configPath,std::ios::binary);std::string text((std::istreambuf_iterator<char>(file)),{});
        if(!file||!parse_helmet_config(text,game,config)){releaseGraphics();config={};report.error="Invalid helmet configuration";return;}
        if(!config.enabled){releaseGraphics();return;}
        // Version 1 and static version 2 use the canonical left image. Live
        // version 2 chooses one saved visor variant, shared by both eyes.
        // Never render older sidecars' different stereo artwork as disparity.
        position=config.visorPosition;
        bool valid=png(directory/imageFor(position),eyes[0].bitmap)&&
                   fit_for_offsets(eyes[0],config.headMotion>0||config.vehicleMotion>0,config.stereoOffset,maxWidth,maxHeight);
        if(valid){eyes[1].bitmap=eyes[0].bitmap;eyes[1].sourceWidth=eyes[0].sourceWidth;eyes[1].sourceHeight=eyes[0].sourceHeight;}
        // The producer publishes a disabled manifest before replacing either fixed
        // filename, then a fresh generation after both. A manifest reread closes
        // the old-left/new-right race even when timestamp resolution aliases.
        auto afterStamp=std::filesystem::last_write_time(configPath,ec);
        auto afterSize=ec?0:std::filesystem::file_size(configPath,ec);
        std::ifstream afterFile;
        if(!ec&&afterSize<=2048)afterFile.open(configPath,std::ios::binary);
        std::string after((std::istreambuf_iterator<char>(afterFile)),{});
        if(ec||!afterFile||afterStamp!=modified||after!=text) {
            for(auto &eye:eyes)clear(eye);
            releaseGraphics();
            config={};stampKnown=false;report.error="Helmet assets changed while loading; retrying";return;
        }
        if(valid){for(auto &eye:eyes)pad_for_offsets(eye,config.headMotion>0||config.vehicleMotion>0,config.stereoOffset);valid=graphics()&&create(eyes[0],0)&&create(eyes[1],1);}
        if(!valid) {
            for(auto &eye:eyes)clear(eye);releaseGraphics();config.enabled=false;report.error="Helmet PNG or swapchain unavailable";
        } else {loadedManifest=text;report.imageReduced=eyes[0].contentWidth!=eyes[0].sourceWidth||eyes[0].contentHeight!=eyes[0].sourceHeight;}
    }
    HelmetVehicleSample vehicleMotion() {
        auto now=GetTickCount64();
        if(externalVehicle){auto sample=vehicle;sample.ageSeconds+=float(now-vehicleSetTime)/1000;return sample;}
        if(config.vehicleMotion<=0)return {};
        if(now-lastTelemetryPoll>=50) {
            lastTelemetryPoll=now;
            auto selected=game=="lmu"?telemetry::Game::Lmu:telemetry::Game::IRacing;
            if(!telemetryMemory.opened())telemetryMemory.open(telemetry::mapping_name(selected,{}));
            if(selected==telemetry::Game::Lmu&&!telemetryLock.opened())telemetryLock.open(telemetry::lock_name({}));
            auto sample=selected==telemetry::Game::IRacing?iracing.read(telemetryMemory):lmu.read(telemetryMemory,&telemetryLock);
            if(sample.status==telemetry::ReadStatus::Valid) {
                if(!telemetrySeeded||sample.token!=lastToken) {
                    telemetryAdvanced=telemetrySeeded;telemetrySeeded=true;lastToken=sample.token;lastAdvance=now;
                    vehicle={};
                    if(sample.data.lateralAccelMps2&&sample.data.longitudinalAccelMps2)
                        vehicle={telemetryAdvanced,0,float(*sample.data.lateralAccelMps2/9.80665),float(*sample.data.longitudinalAccelMps2/9.80665)};
                }
            } else if(sample.status!=telemetry::ReadStatus::Busy) {
                vehicle={};telemetryMemory.close();telemetryLock.close();telemetrySeeded=telemetryAdvanced=false;
            }
        }
        auto sample=vehicle;sample.ageSeconds=float(now-lastAdvance)/1000;return sample;
    }
};
HelmetRenderer::HelmetRenderer():impl_(std::make_unique<Impl>()){}
HelmetRenderer::~HelmetRenderer()=default;
bool HelmetRenderer::initialize(XrSession session,const void *binding,uint32_t maxLayers,
    const OverlayDispatch &api,PFN_xrLocateViews locate,const std::string &game,const std::filesystem::path &directory,
    uint32_t maxSwapchainWidth,uint32_t maxSwapchainHeight) {
    auto &p=*impl_;std::lock_guard lock(p.mutex);p.reset();
    if((game!="iracing"&&game!="lmu")||!session||!api.formats||!api.create||!api.images||!api.acquire||
       !api.wait||!api.release||!api.destroy||!api.createSpace||!api.destroySpace||!api.locate||!locate)return false;
    p.api=api;p.locateViews=locate;p.session=session;p.maxLayers=maxLayers;p.game=game;
    p.maxWidth=maxSwapchainWidth;p.maxHeight=maxSwapchainHeight;
    // The host supplies the DLL directory. Machine installs keep mutable
    // manifests/artwork in the current user's runtime, like the main layer.
    p.directory=directory.empty()?own_directory():og::runtime_data_directory(directory);
    if(p.directory.empty()||!p.retainBinding(binding))return false;
    p.configPath=p.directory/("OpenGridXR-helmet-"+game+".json");
    p.supported=true;p.poll(true);return true;
}
void HelmetRenderer::reset(){auto &p=*impl_;std::lock_guard lock(p.mutex);p.reset();}
void HelmetRenderer::viewConfiguration(XrViewConfigurationType type){auto&p=*impl_;std::lock_guard lock(p.mutex);p.viewType=type;p.motion={};}
void HelmetRenderer::vehicleSample(HelmetVehicleSample sample){auto&p=*impl_;std::lock_guard lock(p.mutex);p.externalVehicle=true;p.vehicle=sample;p.vehicleSetTime=GetTickCount64();}
void HelmetRenderer::shortcutSample(HelmetShortcutSample sample){auto&p=*impl_;std::lock_guard lock(p.mutex);p.externalShortcut=true;p.shortcut=sample;}
HelmetStatus HelmetRenderer::status()const{auto&p=*impl_;std::lock_guard lock(p.mutex);auto report=p.report;
    report.cycleAvailable=p.supported&&p.config.enabled&&p.config.visorHotkey&&p.report.ready;return report;}
bool HelmetRenderer::cycleVisor(bool focused) {
    auto&p=*impl_;std::lock_guard lock(p.mutex);
    const auto now=GetTickCount64();
    if(!focused||!p.supported||!p.config.enabled||!p.config.visorHotkey||!p.report.ready||p.pending||p.cycleDeadline>now)return false;
    // Background control delivery only queues an edge. Preparation remains in
    // the existing frame path, so holding this mutex cannot blank the helmet.
    p.cycleDeadline=now+500;return true;
}
void HelmetRenderer::cancelVisorCycle(){auto&p=*impl_;std::lock_guard lock(p.mutex);p.cycleDeadline=0;}
void HelmetRenderer::append(const XrFrameEndInfo &info,bool shouldRender,std::vector<const XrCompositionLayerBaseHeader*> &layers) {
    auto&p=*impl_;std::unique_lock lock(p.mutex,std::try_to_lock);
    if(!lock.owns_lock()||!p.supported)return;
    p.report.ready=false;
    p.poll();
    const auto deadline=p.cycleDeadline;p.cycleDeadline=0;
    const auto keyboard=p.keyboard();
    const bool cycle=helmet_visor_shortcut(p.shortcutHeld,p.config.enabled&&p.config.visorHotkey&&shouldRender,keyboard)||
        (deadline&&GetTickCount64()<=deadline&&keyboard.foreground);
    if(!p.config.enabled||!shouldRender||!info.layerCount||p.maxLayers<layers.size()+2)return;
    if(cycle)p.changeVisor();
    p.finishVisor();
    for(auto &eye:p.eyes)if(!eye.swap||!p.upload(eye))return;
    if(p.viewType!=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO&&p.viewType!=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO)return;
    const uint32_t expected=p.viewType==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO?4:2;
    // XR_VARJO_quad_views orders the whole-FOV context pair first, focus pair last.
    std::array<XrView,4> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW},{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};uint32_t count{};
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=p.viewType;
    locate.displayTime=info.displayTime;locate.space=p.view;XrViewState state{XR_TYPE_VIEW_STATE};
    if(XR_FAILED(p.locateViews(p.session,&locate,&state,expected,&count,views.data()))||count!=expected||
       (state.viewStateFlags&(XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT))!=
       (XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT)){p.motion={};return;}
    XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
    bool tracking=XR_SUCCEEDED(p.api.locate(p.view,p.local,info.displayTime,&head))&&
                  (head.locationFlags&XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
    auto offset=helmet_motion(p.motion,head.pose.orientation,tracking,double(info.displayTime)*1e-9,
                              p.config.headMotion,p.config.vehicleMotion,p.vehicleMotion());
    XrFovf imageFov{};
    if(!helmet_shared_fov(views[0],views[1],imageFov))return;
    const float x=std::clamp(std::tan(offset[0])/(std::tan(imageFov.angleRight)-std::tan(imageFov.angleLeft)),-.03f,.03f);
    const float y=std::clamp(std::tan(offset[1])/(std::tan(imageFov.angleUp)-std::tan(imageFov.angleDown)),-.03f,.03f);
    for(unsigned i=0;i<2;++i) {
        auto &eye=p.eyes[i];
        // Keep every image pixel in the same head-space direction for both eyes.
        // The per-eye origin cancels IPD parallax; a shared, head-aligned envelope
        // avoids stretching it differently for asymmetric or canted eye frusta.
        XrView imageView=views[i];
        imageView.pose.orientation={0,0,0,1};imageView.fov=imageFov;
        if(!helmet_quad_geometry(imageView,0,0,eye.quad.pose,eye.quad.size))return;
        // Move the sampling window over an edge-extended image, not the outer
        // quad. This prevents exposed scene slivers at the headset's outer edges.
        const int32_t capX=int32_t(std::floor(eye.contentWidth*.03f)),capY=int32_t(std::floor(eye.contentHeight*.03f));
        const int32_t swayX=std::clamp(int32_t(std::lround(x*eye.contentWidth)),-capX,capX);
        const int32_t swayY=std::clamp(int32_t(std::lround(y*eye.contentHeight)),-capY,capY);
        const int32_t stereo=int32_t(std::lround(p.config.stereoOffset*.01f*eye.contentWidth));
        // Sampling farther right moves the artwork left. A positive setting
        // therefore adds to the left eye's crop and subtracts from the right's.
        const int32_t sampleX=std::clamp(-swayX+(i?-stereo:stereo),-int32_t(eye.padX),int32_t(eye.padX));
        const int32_t sampleY=std::clamp(swayY,-int32_t(eye.padY),int32_t(eye.padY));
        eye.quad.subImage.imageRect.offset={int32_t(eye.padX)+sampleX,int32_t(eye.padY)+sampleY};
    }
    // OpenXR composites in list order. The helmet belongs above the game scene
    // and below existing game quads, HUD and spatial overlays, even though this
    // renderer runs after those overlays have been appended.
    auto position=layers.begin();
    for(auto it=layers.begin();it!=layers.end();++it)
        if(*it&&(*it)->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION)position=it+1;
    const XrCompositionLayerBaseHeader *helmetLayers[]{
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&p.eyes[0].quad),
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&p.eyes[1].quad)};
    layers.insert(position,std::begin(helmetLayers),std::end(helmetLayers));
    p.report.ready=true;
}
} // namespace og
