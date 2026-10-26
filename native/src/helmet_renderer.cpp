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
std::filesystem::path own_directory() {
    HMODULE module{};wchar_t path[32768]{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&own_directory),&module))return {};
    DWORD n=GetModuleFileNameW(module,path,32768);
    return n&&n<32768?std::filesystem::path(path).parent_path():std::filesystem::path{};
}
struct Eye {
    XrSwapchain swap{};
    Bitmap bitmap;
    std::vector<ComPtr<ID3D11Texture2D>> textures;
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    uint32_t acquired{UINT32_MAX};
    bool waited{},ready{};
    UINT contentWidth{},contentHeight{},padX{},padY{};
};
void pad_for_motion(Eye &eye,bool moving) {
    auto &b=eye.bitmap;eye.contentWidth=b.width;eye.contentHeight=b.height;
    if(!moving)return;
    eye.padX=UINT(std::ceil(b.width*.03f))+1;eye.padY=UINT(std::ceil(b.height*.03f))+1;
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
    XrSession session{};XrSpace view{},local{};uint32_t maxLayers{};
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
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11On12Device> on12;
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
    void reset() {
        for(auto &eye:eyes)clear(eye);
        if(view&&api.destroySpace)api.destroySpace(view);
        if(local&&api.destroySpace)api.destroySpace(local);
        view=local=XR_NULL_HANDLE;session=XR_NULL_HANDLE; supported=false;
        telemetryMemory.close();telemetryLock.close();telemetrySeeded=telemetryAdvanced=false;
        on12.Reset();context.Reset();device.Reset();config={};report={};motion={};
        vehicle={};externalVehicle=false;stampKnown=false;lastPoll=0;
        viewType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    }
    bool graphics(const void *chain) {
        for(auto n=static_cast<const XrBaseInStructure*>(chain);n;n=n->next) {
            if(n->type==XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
                device=reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(n)->device;
                if(device)device->GetImmediateContext(&context);break;
            }
            if(n->type==XR_TYPE_GRAPHICS_BINDING_D3D12_KHR) {
                auto b=reinterpret_cast<const XrGraphicsBindingD3D12KHR*>(n);
                if(!b->device||!b->queue)return false;
                IUnknown *queues[]{b->queue};
                if(FAILED(D3D11On12CreateDevice(b->device,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,
                                               queues,1,0,&device,&context,nullptr)))return false;
                if(FAILED(device.As(&on12)))return false;break;
            }
        }
        if(!device||!context)return false;
        ComPtr<ID3D11Multithread> guard;
        if(SUCCEEDED(context.As(&guard)))guard->SetMultithreadProtected(TRUE);
        return true;
    }
    bool create(Eye &eye,unsigned index) {
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
    bool upload(Eye &eye) {
        if(eye.ready)return true;
        if(eye.acquired==UINT32_MAX) {
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            if(XR_FAILED(api.acquire(eye.swap,&acquire,&eye.acquired))){report.error="Helmet image acquisition failed";config.enabled=false;return false;}
        }
        if(!eye.waited) {
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=0;
            auto result=api.wait(eye.swap,&wait);
            if(result!=XR_SUCCESS){if(result!=XR_TIMEOUT_EXPIRED){report.error="Helmet image wait failed";config.enabled=false;}return false;}
            eye.waited=true;
        }
        if(eye.acquired>=eye.textures.size()||!eye.textures[eye.acquired]){report.error="Invalid helmet swapchain image";config.enabled=false;return false;}
        ID3D11Resource *target=eye.textures[eye.acquired].Get();
        if(on12)on12->AcquireWrappedResources(&target,1);
        // Upload straight RGBA once. UpdateSubresource changes no pipeline bindings,
        // so the application's graphics state is untouched; no per-frame draw/copy.
        context->UpdateSubresource(target,0,nullptr,eye.bitmap.pixels.data(),eye.bitmap.width*4,0);
        if(on12){on12->ReleaseWrappedResources(&target,1);context->Flush();}
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        auto released=api.release(eye.swap,&release);
        eye.acquired=UINT32_MAX;eye.waited=false;
        if(XR_FAILED(released)||FAILED(device->GetDeviceRemovedReason())){report.error="Helmet image upload failed";config.enabled=false;return false;}
        eye.ready=true;eye.bitmap.pixels.clear();eye.bitmap.pixels.shrink_to_fit();++report.uploads;return true;
    }
    void poll(bool force=false) {
        auto now=GetTickCount64();if(!force&&now-lastPoll<500)return;lastPoll=now;
        std::error_code ec;auto modified=std::filesystem::last_write_time(configPath,ec);
        if(!ec&&stampKnown&&modified==stamp)return;
        if(ec&&!stampKnown)return;
        stampKnown=!ec;stamp=modified;
        for(auto &eye:eyes)clear(eye);
        config={};motion={};report.ready=false;report.error.clear();telemetryMemory.close();telemetryLock.close();
        telemetrySeeded=telemetryAdvanced=false;vehicle={};
        if(ec)return;
        auto size=std::filesystem::file_size(configPath,ec);
        if(ec||size>2048){report.error="Invalid helmet configuration size";return;}
        std::ifstream file(configPath,std::ios::binary);std::string text((std::istreambuf_iterator<char>(file)),{});
        if(!file||!parse_helmet_config(text,game,config)){config={};report.error="Invalid helmet configuration";return;}
        if(!config.enabled)return;
        bool valid=png(directory/config.leftImage,eyes[0].bitmap)&&png(directory/config.rightImage,eyes[1].bitmap)&&
                   eyes[0].bitmap.width==eyes[1].bitmap.width&&eyes[0].bitmap.height==eyes[1].bitmap.height;
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
            config={};stampKnown=false;report.error="Helmet assets changed while loading; retrying";return;
        }
        if(valid){for(auto &eye:eyes)pad_for_motion(eye,config.headMotion>0||config.vehicleMotion>0);valid=create(eyes[0],0)&&create(eyes[1],1);}
        if(!valid) {
            for(auto &eye:eyes)clear(eye);config.enabled=false;report.error="Helmet PNG or swapchain unavailable";
        }
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
    const OverlayDispatch &api,PFN_xrLocateViews locate,const std::string &game,const std::filesystem::path &directory) {
    auto &p=*impl_;std::lock_guard lock(p.mutex);p.reset();
    if((game!="iracing"&&game!="lmu")||!session||!api.formats||!api.create||!api.images||!api.acquire||
       !api.wait||!api.release||!api.destroy||!api.createSpace||!api.destroySpace||!api.locate||!locate)return false;
    p.api=api;p.locateViews=locate;p.session=session;p.maxLayers=maxLayers;p.game=game;
    p.directory=directory.empty()?own_directory():directory;
    if(p.directory.empty()||!p.graphics(binding))return false;
    p.configPath=p.directory/("OpenGridXR-helmet-"+game+".json");
    XrReferenceSpaceCreateInfo ci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};ci.poseInReferenceSpace.orientation.w=1;
    ci.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
    if(XR_FAILED(api.createSpace(session,&ci,&p.view)))return false;
    ci.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
    if(XR_FAILED(api.createSpace(session,&ci,&p.local)))return false;
    p.supported=true;p.poll(true);return true;
}
void HelmetRenderer::reset(){auto &p=*impl_;std::lock_guard lock(p.mutex);p.reset();}
void HelmetRenderer::viewConfiguration(XrViewConfigurationType type){auto&p=*impl_;std::lock_guard lock(p.mutex);p.viewType=type;p.motion={};}
void HelmetRenderer::vehicleSample(HelmetVehicleSample sample){auto&p=*impl_;std::lock_guard lock(p.mutex);p.externalVehicle=true;p.vehicle=sample;p.vehicleSetTime=GetTickCount64();}
HelmetStatus HelmetRenderer::status()const{auto&p=*impl_;std::lock_guard lock(p.mutex);return p.report;}
void HelmetRenderer::append(const XrFrameEndInfo &info,bool shouldRender,std::vector<const XrCompositionLayerBaseHeader*> &layers) {
    auto&p=*impl_;std::unique_lock lock(p.mutex,std::try_to_lock);
    if(!lock.owns_lock()||!p.supported)return;
    p.report.ready=false;
    p.poll();
    if(!p.config.enabled||!shouldRender||!info.layerCount||p.maxLayers<layers.size()+2)return;
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
    for(unsigned i=0;i<2;++i) {
        auto &eye=p.eyes[i];
        if(!helmet_quad_geometry(views[i],0,0,eye.quad.pose,eye.quad.size))return;
        // Move the sampling window over an edge-extended image, not the outer
        // quad. This prevents exposed scene slivers at the headset's outer edges.
        float x=std::clamp(std::tan(offset[0])/(std::tan(views[i].fov.angleRight)-std::tan(views[i].fov.angleLeft)),-.03f,.03f);
        float y=std::clamp(std::tan(offset[1])/(std::tan(views[i].fov.angleUp)-std::tan(views[i].fov.angleDown)),-.03f,.03f);
        const int32_t capX=int32_t(std::floor(eye.contentWidth*.03f)),capY=int32_t(std::floor(eye.contentHeight*.03f));
        eye.quad.subImage.imageRect.offset={int32_t(eye.padX)-std::clamp(int32_t(std::lround(x*eye.contentWidth)),-capX,capX),
                                            int32_t(eye.padY)+std::clamp(int32_t(std::lround(y*eye.contentHeight)),-capY,capY)};
    }
    for(auto &eye:p.eyes)layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&eye.quad));
    p.report.ready=true;
}
} // namespace og
