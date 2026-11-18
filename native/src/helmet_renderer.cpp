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
#include "helmet_light.hpp"
#include "session_d3d11on12.hpp"
#include <atomic>
#include <array>
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
    HMODULE module{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&own_directory),&module))return {};
    const auto path = lmuqv::win::module_path(module);
    return !path.empty()?og::runtime_data_directory(std::filesystem::path(path).parent_path()):std::filesystem::path{};
}
struct Eye {
    XrSwapchain swap{};
    Bitmap bitmap;
    ComPtr<ID3D11ShaderResourceView> artwork;
    std::vector<ComPtr<ID3D11RenderTargetView>> lightTargets;
    uint64_t shadedSample{};
    bool shadeFailed{};
    std::vector<ComPtr<ID3D11Texture2D>> textures;
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    uint32_t acquired{UINT32_MAX};
    bool waited{},ready{};
    UINT sourceWidth{},sourceHeight{},contentWidth{},contentHeight{},padX{};
};
UINT padding(UINT content,float margin) {return margin>0?UINT(std::ceil(content*margin))+1:0;}
bool fit_for_offsets(Eye &eye,float stereoOffset,UINT maxWidth,UINT maxHeight) {
    auto &b=eye.bitmap;eye.sourceWidth=b.width;eye.sourceHeight=b.height;
    const float marginX=std::abs(stereoOffset)*.01f;
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
                                double(maxHeight?std::min(b.height,maxHeight):b.height)/b.height);
    if(ratio>=1)return true;
    const UINT width=UINT(std::floor(b.width*ratio)),height=UINT(std::floor(b.height*ratio));
    // Very small runtime limits cannot retain usable helmet detail. Fail safely
    // rather than silently turn the helmet into an indistinct low-resolution mask.
    return width>=256&&height>=144&&resize(b,width,height);
}
void pad_for_offsets(Eye &eye,float stereoOffset) {
    auto &b=eye.bitmap;eye.contentWidth=b.width;eye.contentHeight=b.height;
    const float marginX=std::abs(stereoOffset)*.01f;
    if(marginX==0)return;
    eye.padX=padding(b.width,marginX);
    UINT width=b.width+2*eye.padX,height=b.height;
    std::vector<unsigned char> padded(size_t(width)*height*4);
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x) {
        UINT sx=UINT(std::clamp(int(x)-int(eye.padX),0,int(b.width)-1));
        std::copy_n(b.pixels.data()+(size_t(y)*b.width+sx)*4,4,padded.data()+(size_t(y)*width+x)*4);
    }
    b.width=width;b.height=height;b.pixels=std::move(padded);
}
} // namespace
struct HelmetRenderer::Impl {
    mutable std::mutex mutex;
    OverlayDispatch api{}; PFN_xrLocateViews locateViews{};
    XrSession session{};XrSpace view{};uint32_t maxLayers{},maxWidth{},maxHeight{};
    XrViewConfigurationType viewType{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
    std::filesystem::path directory,configPath;
    std::filesystem::file_time_type stamp{};
    bool stampKnown{},supported{},allowSceneLight{};
    std::string game;
    uint64_t lastPoll{};
    HelmetConfig config;
    enum LightConsumer : unsigned { helmetLight = 1, wheelLight = 2, readableHelmetScene = 4 };
    std::atomic<unsigned> lightConsumers{};
    std::unique_ptr<HelmetLight> light;
    XrSwapchainSubImage scene{};
    uint64_t lastCapture{}, sampleSerial{};
    bool freshLight{};
    HelmetStatus report;
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
    std::shared_ptr<SessionD3D11On12> bridge;
    std::shared_ptr<D3D11On12Binding> binding12;
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
        eye.lightTargets.clear();
        eye.textures.clear();
        if(eye.swap&&api.destroy)api.destroy(eye.swap);
        eye=Eye{};
    }
    void releaseViewSpace() {
        if(view&&api.destroySpace)api.destroySpace(view);
        view=XR_NULL_HANDLE;
    }
    void releaseGraphics() {
        releaseViewSpace();
        lightConsumers.store(0);
        light.reset();scene={};lastCapture=sampleSerial=0;freshLight=false;
        on12.Reset();context.Reset();device.Reset();
        binding12.reset();
    }
    void reset() {
        for(auto &eye:eyes)clear(eye);
        for(auto &eye:pendingEyes)clear(eye);
        pending=false;cycleDeadline=0;shortcutHeld=false;externalShortcut=false;shortcut={};loadedManifest.clear();
        releaseGraphics();bridge.reset();source11.Reset();source12.Reset();sourceQueue.Reset();
        session=XR_NULL_HANDLE; supported=false;
        config={};report={};stampKnown=false;lastPoll=0;
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
    bool graphicsDevice() {
        if(device&&context)return true;
        if(source11) {device=source11;device->GetImmediateContext(&context);}
        else if(source12&&sourceQueue) {
            if(!bridge)bridge=std::make_shared<SessionD3D11On12>();
            binding12=bridge->acquire(source12.Get(),sourceQueue.Get());
            if(!binding12)return false;
            device=binding12->device;context=binding12->context;on12=binding12->on12;
        }
        if(!device||!context)return false;
        ComPtr<ID3D11Multithread> guard;
        if(SUCCEEDED(context.As(&guard)))guard->SetMultithreadProtected(TRUE);
        return true;
    }
    bool ensureViewSpace() {
        if(view)return true;
        XrReferenceSpaceCreateInfo ci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};ci.poseInReferenceSpace.orientation.w=1;
        ci.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
        return XR_SUCCEEDED(api.createSpace(session,&ci,&view));
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
        // The neutral right image remains static. When lit, both eye quads
        // reference the same dynamic left image with their original crop offsets.
        bool dynamic = light && config.ambientLight>0 && index == 0;
        ci.createFlags=dynamic ? 0 : XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
        ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        ci.format=format;ci.width=eye.bitmap.width;ci.height=eye.bitmap.height;
        ci.arraySize=ci.faceCount=ci.mipCount=ci.sampleCount=1;
        auto result=api.create(session,&ci,&eye.swap);
        if(XR_FAILED(result) && dynamic) {
            dynamic=false;ci.createFlags=XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
            result=api.create(session,&ci,&eye.swap);
        }
        if(result==XR_ERROR_FEATURE_UNSUPPORTED && ci.createFlags) {ci.createFlags=0;result=api.create(session,&ci,&eye.swap);}
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
        if (dynamic) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width=ci.width;desc.Height=ci.height;desc.ArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
            desc.Format=static_cast<DXGI_FORMAT>(format);
            desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            desc.Usage=D3D11_USAGE_IMMUTABLE;
            D3D11_SUBRESOURCE_DATA data{eye.bitmap.pixels.data(),ci.width*4,0};
            ComPtr<ID3D11Texture2D> artwork;
            if (SUCCEEDED(device->CreateTexture2D(&desc,&data,&artwork)) &&
                SUCCEEDED(device->CreateShaderResourceView(artwork.Get(),nullptr,&eye.artwork))) {
                D3D11_RENDER_TARGET_VIEW_DESC target{};
                target.Format=desc.Format;target.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
                eye.lightTargets.resize(count);
                for (unsigned i=0;i<count;++i)
                    if (FAILED(device->CreateRenderTargetView(eye.textures[i].Get(),&target,&eye.lightTargets[i]))) {
                        eye.artwork.Reset();eye.lightTargets.clear();break;
                    }
            }
        }
        eye.quad.space=view;
        eye.quad.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
        eye.quad.eyeVisibility=index?XR_EYE_VISIBILITY_RIGHT:XR_EYE_VISIBILITY_LEFT;
        eye.quad.subImage.swapchain=eye.swap;
        // Positive stereo correction samples farther right in the left eye.
        const int32_t stereo=int32_t(std::lround(config.stereoOffset*.01f*eye.contentWidth));
        eye.quad.subImage.imageRect={{int32_t(eye.padX)+(index?-stereo:stereo),0},
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
        if (eye.artwork) {
            ComPtr<ID3D11Resource> original;
            eye.artwork->GetResource(&original);
            context->CopyResource(target,original.Get());
        } else {
            context->UpdateSubresource(target,0,nullptr,eye.bitmap.pixels.data(),eye.bitmap.width*4,0);
        }
        if(on12){on12->ReleaseWrappedResources(&target,1);context->Flush();}
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        auto released=api.release(eye.swap,&release);
        eye.acquired=UINT32_MAX;eye.waited=false;
        if(XR_FAILED(released)||FAILED(device->GetDeviceRemovedReason())){report.error="Helmet image upload failed";if(fatal)config.enabled=false;return false;}
        eye.ready=true;eye.bitmap.pixels.clear();eye.bitmap.pixels.shrink_to_fit();++report.uploads;return true;
    }
    XrSwapchain litImage() {
        auto& eye=eyes[0];
        if (!eye.artwork) return XR_NULL_HANDLE;
        auto neutral=eyes[1].swap;
        const auto now=GetTickCount64();
        if (!freshLight || now-lastCapture>500 || eye.shadeFailed) return neutral;
        if (eye.shadedSample==sampleSerial) return eye.swap;
        // A zero-timeout wait keeps the last available image usable on a busy
        // compositor. The next frame retries the outstanding acquire.
        if (eye.acquired==UINT32_MAX) {
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            if (XR_FAILED(api.acquire(eye.swap,&acquire,&eye.acquired))) return neutral;
        }
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        auto waited=api.wait(eye.swap,&wait);
        if (waited!=XR_SUCCESS) {
            if (waited!=XR_TIMEOUT_EXPIRED) eye.shadeFailed=true;
            return eye.shadedSample && !eye.shadeFailed ? eye.swap : neutral;
        }
        eye.waited=true;
        bool shaded=false;
        if (eye.acquired<eye.lightTargets.size()) {
            auto* target=static_cast<ID3D11Resource*>(eye.textures[eye.acquired].Get());
            if (on12) on12->AcquireWrappedResources(&target,1);
            shaded=light->shade(context.Get(),eye.lightTargets[eye.acquired].Get(),eye.artwork.Get(),
                                eye.bitmap.width,eye.bitmap.height,config.ambientLight*.01f);
            if (on12) {on12->ReleaseWrappedResources(&target,1);context->Flush();}
        }
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        auto result=api.release(eye.swap,&release);
        eye.acquired=UINT32_MAX;eye.waited=false;
        if (!shaded || XR_FAILED(result)) {eye.shadeFailed=true;return neutral;}
        eye.shadedSample=sampleSerial;
        return eye.swap;
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
           !fit_for_offsets(pendingEyes[0],config.stereoOffset,maxWidth,maxHeight)||
           pendingEyes[0].bitmap.width!=eyes[0].contentWidth||pendingEyes[0].bitmap.height!=eyes[0].contentHeight) {
            for(auto &eye:pendingEyes)clear(eye);
            report.error="Visor image unavailable or changed; keeping current visor";return;
        }
        pendingEyes[1].bitmap=pendingEyes[0].bitmap;
        pendingEyes[1].sourceWidth=pendingEyes[0].sourceWidth;pendingEyes[1].sourceHeight=pendingEyes[0].sourceHeight;
        for(auto &eye:pendingEyes)pad_for_offsets(eye,config.stereoOffset);
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
        lightConsumers.store(0);light.reset();freshLight=false;scene={};lastCapture=0;
        config={};report.ready=false;report.imageReduced=false;report.error.clear();
        if(ec){releaseGraphics();return;}
        auto size=std::filesystem::file_size(configPath,ec);
        if(ec||size>2048){releaseGraphics();report.error="Invalid helmet configuration size";return;}
        std::ifstream file(configPath,std::ios::binary);std::string text((std::istreambuf_iterator<char>(file)),{});
        if(!file||!parse_helmet_config(text,game,config)){releaseGraphics();config={};report.error="Invalid helmet configuration";return;}
        if(allowSceneLight && (config.wheelAmbientLight>0 || (config.enabled && config.ambientLight>0)) && graphicsDevice()) {
            light=std::make_unique<HelmetLight>();
            if(!light->initialize(device.Get()))light.reset();
        }
        const bool wheelProbe=light&&config.wheelAmbientLight>0;
        if(!config.enabled) {
            if(wheelProbe){releaseViewSpace();lightConsumers.store(wheelLight);}
            else releaseGraphics();
            return;
        }
        // Version 1 and static version 2 use the canonical left image. Live
        // version 2 chooses one saved visor variant, shared by both eyes.
        // Never render older sidecars' different stereo artwork as disparity.
        position=config.visorPosition;
        bool valid=png(directory/imageFor(position),eyes[0].bitmap)&&
                   fit_for_offsets(eyes[0],config.stereoOffset,maxWidth,maxHeight);
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
        if(valid){
            for(auto &eye:eyes)pad_for_offsets(eye,config.stereoOffset);
            valid=graphicsDevice()&&ensureViewSpace()&&create(eyes[0],0)&&create(eyes[1],1);
        }
        if(!valid) {
            for(auto &eye:eyes)clear(eye);
            if(wheelProbe)releaseViewSpace();else releaseGraphics();
            config.enabled=false;report.error="Helmet PNG or swapchain unavailable";
        } else {loadedManifest=text;report.imageReduced=eyes[0].contentWidth!=eyes[0].sourceWidth||eyes[0].contentHeight!=eyes[0].sourceHeight;}
        lightConsumers.store((wheelProbe ? wheelLight : 0u) | (eyes[0].artwork ? helmetLight : 0u) |
                             (allowSceneLight && valid ? readableHelmetScene : 0u));
    }

};
HelmetRenderer::HelmetRenderer():impl_(std::make_unique<Impl>()){}
HelmetRenderer::~HelmetRenderer()=default;
bool HelmetRenderer::initialize(XrSession session,const void *binding,uint32_t maxLayers,
    const OverlayDispatch &api,PFN_xrLocateViews locate,const std::string &game,const std::filesystem::path &directory,
    uint32_t maxSwapchainWidth,uint32_t maxSwapchainHeight,bool allowSceneLight,std::shared_ptr<SessionD3D11On12> bridge) {
    auto &p=*impl_;std::lock_guard lock(p.mutex);p.reset();
    if((game!="iracing"&&game!="lmu")||!session||!api.formats||!api.create||!api.images||!api.acquire||
       !api.wait||!api.release||!api.destroy||!api.createSpace||!api.destroySpace||!locate)return false;
    p.api=api;p.locateViews=locate;p.session=session;p.maxLayers=maxLayers;p.game=game;
    p.allowSceneLight=allowSceneLight;
    p.bridge=std::move(bridge);
    p.maxWidth=maxSwapchainWidth;p.maxHeight=maxSwapchainHeight;
    // The host supplies the DLL directory. Machine installs keep mutable
    // manifests/artwork in the current user's runtime, like the main layer.
    p.directory=directory.empty()?own_directory():og::runtime_data_directory(directory);
    if(p.directory.empty()||!p.retainBinding(binding))return false;
    p.configPath=p.directory/("OpenGridXR-helmet-"+game+".json");
    p.supported=true;p.poll(true);return true;
}
void HelmetRenderer::reset(){auto &p=*impl_;std::lock_guard lock(p.mutex);p.reset();}
void HelmetRenderer::viewConfiguration(XrViewConfigurationType type){auto&p=*impl_;std::lock_guard lock(p.mutex);p.viewType=type;}
bool HelmetRenderer::needsReadableScene() const { return impl_->lightConsumers.load()!=0; }
bool HelmetRenderer::wantsSceneLight() const { return (impl_->lightConsumers.load() & (Impl::helmetLight | Impl::wheelLight)) != 0; }
ComPtr<ID3D11ShaderResourceView> HelmetRenderer::wheelLightProbe() const {
    auto& p=*impl_;
    if (!(p.lightConsumers.load() & Impl::wheelLight)) return {};
    // xrEndFrame must not wait for a concurrent image release or reload.
    std::unique_lock lock(p.mutex,std::try_to_lock);
    if (!lock || !p.light || p.config.wheelAmbientLight<=0 || !p.freshLight || GetTickCount64()-p.lastCapture>500) return {};
    return p.light->probe();
}
void HelmetRenderer::sceneProjection(const XrFrameEndInfo& frame) {
    auto& p=*impl_;
    if (!wantsSceneLight()) return;
    std::lock_guard lock(p.mutex);
    XrSwapchainSubImage next{};
    for (uint32_t i=0;frame.layers && i<frame.layerCount;++i) {
        const auto* layer=frame.layers[i];
        if (!layer || layer->type!=XR_TYPE_COMPOSITION_LAYER_PROJECTION) continue;
        const auto* projection=reinterpret_cast<const XrCompositionLayerProjection*>(layer);
        if ((projection->viewCount==2 || projection->viewCount==4) && projection->views)
            next=projection->views[0].subImage;
        break;
    }
    const auto& a=next.imageRect;const auto& b=p.scene.imageRect;
    if (next.swapchain!=p.scene.swapchain || next.imageArrayIndex!=p.scene.imageArrayIndex ||
        a.offset.x!=b.offset.x || a.offset.y!=b.offset.y || a.extent.width!=b.extent.width || a.extent.height!=b.extent.height) {
        p.scene=next;p.freshLight=false;p.lastCapture=0;
    }
}
void HelmetRenderer::forgetScene(XrSwapchain swap) {
    auto& p=*impl_;
    if (!wantsSceneLight()) return;
    std::lock_guard lock(p.mutex);
    if (p.scene.swapchain==swap) {p.scene={};p.freshLight=false;p.lastCapture=0;}
}
void HelmetRenderer::captureScene(XrSwapchain swap,int64_t format,ID3D11Texture2D* source,ID3D12Resource* source12) {
    auto& p=*impl_;
    if (!wantsSceneLight()) return;
    std::unique_lock lock(p.mutex,std::try_to_lock);
    if (!lock || !p.light || !swap || p.scene.swapchain!=swap) return;
    const auto now=GetTickCount64();
    if (p.lastCapture && now-p.lastCapture<100) return;
    const float weight=!p.freshLight || now-p.lastCapture>500 ? 1.f :
        1.f-std::exp(-float(now-p.lastCapture)/180.f);
    p.lastCapture=now;p.freshLight=false;
    ComPtr<ID3D11Texture2D> wrapper;
    if (p.on12) {
        if (!source12) return;
        D3D11_RESOURCE_FLAGS flags{};flags.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(p.on12->CreateWrappedResource(source12,&flags,D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,IID_PPV_ARGS(&wrapper)))) return;
        source=wrapper.Get();
    } else {
        if (!source) return;
        ComPtr<ID3D11Device> device;
        source->GetDevice(&device);
        if (device.Get()!=p.device.Get()) return;
    }
    ID3D11Resource* resource=source;
    if (p.on12) p.on12->AcquireWrappedResources(&resource,1);
    p.freshLight=p.light->sample(p.context.Get(),source,static_cast<DXGI_FORMAT>(format),p.scene,weight);
    if (p.on12) {p.on12->ReleaseWrappedResources(&resource,1);p.context->Flush();}
    if (p.freshLight) ++p.sampleSerial;
}
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
DashboardLayerOrder HelmetRenderer::append(const XrFrameEndInfo &info,bool shouldRender,std::vector<const XrCompositionLayerBaseHeader*> &layers,int32_t *visorPosition) {
    if(visorPosition)*visorPosition=-1;
    auto&p=*impl_;std::unique_lock lock(p.mutex,std::try_to_lock);
    if(!lock.owns_lock()||!p.supported)return {};
    p.report.ready=false;
    p.poll();
    const auto deadline=p.cycleDeadline;p.cycleDeadline=0;
    const auto keyboard=p.keyboard();
    const bool cycle=helmet_visor_shortcut(p.shortcutHeld,p.config.enabled&&p.config.visorHotkey&&shouldRender,keyboard)||
        (deadline&&GetTickCount64()<=deadline&&keyboard.foreground);
    if(!p.config.enabled||!shouldRender||!info.layerCount)return {};
    if(p.maxLayers<layers.size()+2)return {nullptr,true};
    if(cycle)p.changeVisor();
    p.finishVisor();
    for(auto &eye:p.eyes)if(!eye.swap||!p.upload(eye))return {};
    if(p.viewType!=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO&&p.viewType!=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO)return {};
    const uint32_t expected=p.viewType==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO?4:2;
    // XR_VARJO_quad_views orders the whole-FOV context pair first, focus pair last.
    std::array<XrView,4> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW},{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};uint32_t count{};
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=p.viewType;
    locate.displayTime=info.displayTime;locate.space=p.view;XrViewState state{XR_TYPE_VIEW_STATE};
    if(XR_FAILED(p.locateViews(p.session,&locate,&state,expected,&count,views.data()))||count!=expected||
       (state.viewStateFlags&(XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT))!=
       (XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT))return {};
    XrFovf imageFov{};
    if(!helmet_shared_fov(views[0],views[1],imageFov))return {};
    const auto shared=p.litImage();
    for(unsigned i=0;i<2;++i) {
        auto &eye=p.eyes[i];
        eye.quad.subImage.swapchain=shared ? shared : eye.swap;
        // Keep every image pixel in the same head-space direction for both eyes.
        // The per-eye origin cancels IPD parallax; a shared, head-aligned envelope
        // avoids stretching it differently for asymmetric or canted eye frusta.
        XrView imageView=views[i];
        imageView.pose.orientation={0,0,0,1};imageView.fov=imageFov;
        if(!helmet_quad_geometry(imageView,eye.quad.pose,eye.quad.size))return {};
    }
    // Keep the game's own layer order. Dashboards submitted later insert before
    // the first helmet eye, including when they use a separate quad for each eye.
    auto position=layers.begin();
    for(auto it=layers.begin();it!=layers.end();++it)
        if(*it&&(*it)->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION)position=it+1;
    const XrCompositionLayerBaseHeader *helmetLayers[]{
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&p.eyes[0].quad),
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&p.eyes[1].quad)};
    layers.insert(position,std::begin(helmetLayers),std::end(helmetLayers));
    p.report.ready=true;
    if(visorPosition)*visorPosition=static_cast<int32_t>(p.position);
    return {helmetLayers[0],false};
}
} // namespace og
