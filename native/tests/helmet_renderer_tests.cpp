#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#include "helmet_renderer.hpp"
#include "session_d3d11on12.hpp"
#include <filesystem>
#include <fstream>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <d3d11_4.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <iostream>
#include <map>
#include <openxr/openxr_platform.h>
#include <stdexcept>
#include <thread>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
ComPtr<ID3D11Device> device11;
ComPtr<ID3D11DeviceContext> context11;
ComPtr<ID3D12Device> device12;
ComPtr<ID3D12CommandQueue> queue12;
struct RuntimeImage {
    ComPtr<ID3D11Texture2D> texture11;
    ComPtr<ID3D12Resource> texture12;
    uint32_t width{}, height{};
};
std::map<XrSwapchain, RuntimeImage> images;
uint64_t nextSwap = 20;
bool d12{}, timeout{};
unsigned acquisitions{}, releases{}, creations{}, spaceCreations{};
bool rejectStatic{}, verifyReleasedViews{};
bool quadViews{};
bool viewTracking{true};
XrResult XRAPI_CALL formats(XrSession, uint32_t cap, uint32_t *count, int64_t *out) {
    *count = 1;
    if (cap)
        out[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL create(XrSession, const XrSwapchainCreateInfo *info, XrSwapchain *out) {
    if(rejectStatic&&(info->createFlags&XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT))return XR_ERROR_FEATURE_UNSUPPORTED;
    ++creations;
    RuntimeImage image;
    image.width = info->width;
    image.height = info->height;
    if (d12) {
        D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = info->width;
        desc.Height = info->height;
        desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (FAILED(device12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                     D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                     IID_PPV_ARGS(&image.texture12))))
            return XR_ERROR_RUNTIME_FAILURE;
    } else {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = info->width;
        desc.Height = info->height;
        desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        desc.Usage = D3D11_USAGE_DEFAULT;
        if (FAILED(device11->CreateTexture2D(&desc, nullptr, &image.texture11)))
            return XR_ERROR_RUNTIME_FAILURE;
    }
    *out = reinterpret_cast<XrSwapchain>(++nextSwap);
    images.emplace(*out, std::move(image));
    return XR_SUCCESS;
}
XrResult XRAPI_CALL enumerate(XrSwapchain swap, uint32_t cap, uint32_t *count,
                              XrSwapchainImageBaseHeader *out) {
    *count = 1;
    if (cap) {
        if (d12)
            reinterpret_cast<XrSwapchainImageD3D12KHR *>(out)->texture =
                images.at(swap).texture12.Get();
        else
            reinterpret_cast<XrSwapchainImageD3D11KHR *>(out)->texture =
                images.at(swap).texture11.Get();
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL acquire(XrSwapchain, const XrSwapchainImageAcquireInfo *, uint32_t *out) {
    ++acquisitions;
    *out = 0;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL waitImage(XrSwapchain, const XrSwapchainImageWaitInfo *) {
    return timeout ? XR_TIMEOUT_EXPIRED : XR_SUCCESS;
}
XrResult XRAPI_CALL releaseImage(XrSwapchain, const XrSwapchainImageReleaseInfo *) {
    ++releases;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL destroy(XrSwapchain s) {
    if (verifyReleasedViews && !d12) {
        auto* texture=images.at(s).texture11.Get();
        texture->AddRef();
        check(texture->Release()==1,"all renderer texture and RTV references released before xrDestroySwapchain");
    }
    images.erase(s);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL createSpace(XrSession, const XrReferenceSpaceCreateInfo *info, XrSpace *out) {
    ++spaceCreations;
    *out = reinterpret_cast<XrSpace>(
        uintptr_t(info->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_VIEW ? 1 : 2));
    return XR_SUCCESS;
}
XrResult XRAPI_CALL destroySpace(XrSpace) { return XR_SUCCESS; }
uint32_t pixel(const RuntimeImage &image, unsigned eighths=4) {
    if (!d12) {
        D3D11_TEXTURE2D_DESC desc{};
        image.texture11->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> read;
        check(SUCCEEDED(device11->CreateTexture2D(&desc, nullptr, &read)), "readback allocation");
        context11->CopyResource(read.Get(), image.texture11.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(SUCCEEDED(context11->Map(read.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "readback map");
        auto value = *reinterpret_cast<const uint32_t *>(static_cast<const char *>(mapped.pData) +
                                                         (image.height / 2) * mapped.RowPitch +
                                                         (image.width * eighths / 8) * 4);
        context11->Unmap(read.Get(), 0);
        return value;
    }
    auto desc = image.texture12->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes{};
    device12->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = bytes;
    bd.Height = bd.DepthOrArraySize = bd.MipLevels = bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> read;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> command;
    ComPtr<ID3D12Fence> fence;
    check(SUCCEEDED(device12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd,
                                                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                      IID_PPV_ARGS(&read))),
          "D3D12 readback");
    device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                IID_PPV_ARGS(&command));
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {image.texture12.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                          D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE};
    command->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION to{}, from{};
    to.pResource = read.Get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint = footprint;
    from.pResource = image.texture12.Get();
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    command->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command->ResourceBarrier(1, &barrier);
    command->Close();
    ID3D12CommandList *list = command.Get();
    queue12->ExecuteCommandLists(1, &list);
    device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    queue12->Signal(fence.Get(), 1);
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    fence->SetEventOnCompletion(1, event);
    check(WaitForSingleObject(event, 3000) == WAIT_OBJECT_0, "D3D12 copy fence");
    CloseHandle(event);
    void *mapped{};
    read->Map(0, nullptr, &mapped);
    uint32_t value = *reinterpret_cast<const uint32_t *>(
        static_cast<const char *>(mapped) + footprint.Offset +
        image.height / 2 * footprint.Footprint.RowPitch + image.width * eighths / 8 * 4);
    read->Unmap(0, nullptr);
    return value;
}
XrResult XRAPI_CALL locateViews(XrSession,const XrViewLocateInfo *info,XrViewState *state,uint32_t cap,uint32_t *count,XrView *views){
    check(info->space==reinterpret_cast<XrSpace>(1),"helmet locates original eye geometry in VIEW space");
    check(info->viewConfigurationType==(quadViews?XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO:XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO),"runtime active view configuration respected");
    *count=quadViews?4:2;if(cap<*count)return XR_ERROR_SIZE_INSUFFICIENT;
    state->viewStateFlags=viewTracking?(XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT):0;
    for(unsigned i=0;i<2;++i){
        views[i].pose={{0,i?-std::sin(.06f):std::sin(.06f),0,std::cos(.06f)},{i?.032f:-.032f,0,0}};
        // Real headsets expose asymmetric, different eye frusta. Fitting the
        // same image separately to them gives its features binocular disparity.
        views[i].fov=i?XrFovf{-.6f,.85f,.7f,-.7f}:XrFovf{-.9f,.65f,.75f,-.65f};
    }
    if(quadViews)for(unsigned i=2;i<4;++i){views[i]=views[i-2];views[i].fov={-.2f,.2f,.2f,-.2f};}
    return XR_SUCCESS;
}
void writePng(const std::filesystem::path &path,uint32_t rgba,UINT width=256,UINT height=144){
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;
    check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"WIC fixture factory");
    check(SUCCEEDED(factory->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE)),"WIC fixture file");
    check(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)),"PNG fixture encoder");
    check(SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr))&&SUCCEEDED(frame->Initialize(nullptr))&&SUCCEEDED(frame->SetSize(width,height)),"PNG fixture frame");
    auto format=GUID_WICPixelFormat32bppBGRA;check(SUCCEEDED(frame->SetPixelFormat(&format))&&format==GUID_WICPixelFormat32bppBGRA,"fixture pixel format");
    uint32_t bgra=(rgba&0xFF00FF00u)|((rgba&255)<<16)|((rgba>>16)&255);
    std::vector<uint32_t> data(size_t(width)*height,bgra);
    check(SUCCEEDED(frame->WritePixels(height,width*4,UINT(data.size()*4),reinterpret_cast<BYTE*>(data.data())))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit()),"PNG fixture output");
}
int main(int argc,char **argv){
    auto root=std::filesystem::temp_directory_path()/(L"opengrid-helmet-test-"+std::to_wstring(GetCurrentProcessId()));
    try{
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        d12=argc>1&&std::string(argv[1])=="--d3d12";
        std::filesystem::create_directories(root);
        auto left=root/"OpenGridXR-helmet-iracing-left.png",right=root/"OpenGridXR-helmet-iracing-right.png",config=root/"OpenGridXR-helmet-iracing.json";
        writePng(left,0x800000FF);writePng(right,0x40FF0000);
        const auto enabled=R"({"version":1,"enabled":true,"leftImage":"OpenGridXR-helmet-iracing-left.png","rightImage":"OpenGridXR-helmet-iracing-right.png","headMotion":1,"vehicleMotion":0})";
        {std::ofstream file(config);file<<enabled;}
        ComPtr<IDXGIFactory4> factory;check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),"DXGI factory");
        ComPtr<IDXGIAdapter1> adapter;check(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))),"WARP adapter");
        check(SUCCEEDED(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device11,nullptr,&context11)),"D3D11 WARP device");
        XrGraphicsBindingD3D11KHR b11{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};b11.device=device11.Get();
        XrGraphicsBindingD3D12KHR b12{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};
        if(d12){
            check(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device12))),"D3D12 WARP device");
            D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
            check(SUCCEEDED(device12->CreateCommandQueue(&q,IID_PPV_ARGS(&queue12))),"D3D12 queue");b12.device=device12.Get();b12.queue=queue12.Get();
        }
        og::OverlayDispatch api{formats,create,enumerate,acquire,waitImage,releaseImage,destroy,createSpace,destroySpace};
        og::HelmetRenderer renderer;
        auto initialize=[&](uint32_t maxLayers){return renderer.initialize(reinterpret_cast<XrSession>(10),d12?static_cast<const void*>(&b12):&b11,maxLayers,api,locateViews,"iracing",root);};
        XrCompositionLayerProjection scene{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        const XrCompositionLayerBaseHeader *base=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&scene);
        XrFrameEndInfo info{XR_TYPE_FRAME_END_INFO};info.displayTime=1000000000;info.layerCount=1;info.layers=&base;
        ComPtr<ID3D11Multithread> multithread;
        check(SUCCEEDED(context11.As(&multithread)),"fixture multithread interface");
        const auto protectionBefore=multithread->GetMultithreadProtected();
        {std::ofstream file(config);file<<R"({"version":1,"enabled":false})";}
        check(initialize(16),"disabled helmet initialize");
        for(unsigned frame=0;frame<90;++frame) {
            std::vector<const XrCompositionLayerBaseHeader*> idleLayers{base};
            renderer.append(info,true,idleLayers);
            check(idleLayers.size()==1,"disabled helmet submits no layers");
        }
        check(spaceCreations==0&&creations==0&&acquisitions==0&&images.empty(),"disabled helmet creates no reference spaces or swapchains");
        check(!renderer.status().cycleAvailable&&!renderer.cycleVisor(true),"disabled helmet rejects controller cycling without creating resources");
        check(multithread->GetMultithreadProtected()==protectionBefore,"disabled helmet leaves game context protection unchanged");
        {std::ofstream file(config);file<<enabled;}
        std::filesystem::last_write_time(config,std::filesystem::file_time_type::clock::now()+std::chrono::seconds(1));
        Sleep(520); // Hot enable initializes graphics from the retained binding, not its original stack address.
        context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        timeout=true;std::vector<const XrCompositionLayerBaseHeader*> layers{base};renderer.append(info,true,layers);
        check(layers.size()==1,"pending image does not submit a partial stereo pair");timeout=false;
        renderer.append(info,true,layers);check(layers.size()==3,"two eye overlays appended");
        check(spaceCreations==1,"hot enable creates only the VIEW reference space");
        auto l=reinterpret_cast<const XrCompositionLayerQuad*>(layers[1]);auto r=reinterpret_cast<const XrCompositionLayerQuad*>(layers[2]);
        check(l->eyeVisibility==XR_EYE_VISIBILITY_LEFT&&r->eyeVisibility==XR_EYE_VISIBILITY_RIGHT,"eye isolation");
        check(l->pose.orientation.x==0&&l->pose.orientation.y==0&&l->pose.orientation.z==0&&l->pose.orientation.w==1&&
              r->pose.orientation.x==0&&r->pose.orientation.y==0&&r->pose.orientation.z==0&&r->pose.orientation.w==1,
              "both eye images remain head-aligned on a canted headset");
        check(std::abs((l->pose.position.x+.032f)- (r->pose.position.x-.032f))<.00001f&&
              std::abs(l->pose.position.y-r->pose.position.y)<.00001f&&
              std::abs(l->size.width-r->size.width)<.00001f&&std::abs(l->size.height-r->size.height)<.00001f,
              "identical helmet pixels have identical binocular directions despite asymmetric eye FOVs");
        check(l->layerFlags&XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT,"straight alpha contract");
        check(pixel(images.at(l->subImage.swapchain))==0x800000FF,"left PNG RGBA preserved");
        check(pixel(images.at(r->subImage.swapchain))==0x800000FF,"both eyes use the canonical mono PNG even with legacy stereo assets");
        D3D11_PRIMITIVE_TOPOLOGY topology;context11->IAGetPrimitiveTopology(&topology);check(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"game state preserved");
        check(renderer.status().ready&&renderer.status().uploads==2,"one upload per eye");
        XrCompositionLayerProjection secondScene{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        XrCompositionLayerQuad gameQuad{XR_TYPE_COMPOSITION_LAYER_QUAD},hud{XR_TYPE_COMPOSITION_LAYER_QUAD},spatial{XR_TYPE_COMPOSITION_LAYER_QUAD};
        const auto secondBase=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&secondScene);
        const auto gameBase=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&gameQuad);
        const auto hudBase=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&hud);
        const auto spatialBase=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&spatial);
        const std::vector<const XrCompositionLayerBaseHeader*> existing{base,secondBase,gameBase,hudBase,spatialBase};
        layers=existing;auto dashboardOrder=renderer.append(info,true,layers);
        check(dashboardOrder.firstHelmetEye==reinterpret_cast<const XrCompositionLayerBaseHeader*>(l)&&
              !dashboardOrder.helmetAtLayerLimit,"helmet exposes the first eye as the dashboard insertion point");
        check(layers.size()==7&&layers[0]==base&&layers[1]==secondBase&&
              layers[2]==reinterpret_cast<const XrCompositionLayerBaseHeader*>(l)&&
              layers[3]==reinterpret_cast<const XrCompositionLayerBaseHeader*>(r)&&
              layers[4]==gameBase&&layers[5]==hudBase&&layers[6]==spatialBase,
              "helmet keeps game projections and existing foreground overlays in order");
        for(int n=0;n<90;++n){layers={base};info.displayTime+=11111111;renderer.append(info,true,layers);check(layers.size()==3,"cached helmet submission");}
        check(acquisitions==2&&releases==2&&creations==2&&renderer.status().uploads==2,"no per-frame uploads or swapchain acquisition");
        const auto outerWidth=l->size.width;
        quadViews=true;renderer.viewConfiguration(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO);
        layers={base};renderer.append(info,true,layers);
        check(layers.size()==3&&l->size.width==outerWidth,"native quad views use outer context pair rather than focus inset");
        quadViews=false;renderer.viewConfiguration(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO);
        const auto neutralPose=l->pose;
        check(l->subImage.imageRect.offset.x==0 && l->subImage.imageRect.offset.y==0 &&
              images.at(l->subImage.swapchain).width==l->subImage.imageRect.extent.width &&
              images.at(l->subImage.swapchain).height==l->subImage.imageRect.extent.height,
              "legacy sidecar keeps the artwork at full resolution without extra padding");
        viewTracking=false;layers={base};renderer.append(info,true,layers);
        check(layers.size()==1,"invalid view tracking suppresses the complete helmet pair");
        viewTracking=true;layers={base};renderer.append(info,true,layers);
        check(layers.size()==3&&renderer.status().uploads==2,"view tracking recovery reuses both cached eyes");
        {std::ofstream file(config);file<<R"({"version":1,"enabled":false})";}
        std::filesystem::last_write_time(config,std::filesystem::file_time_type::clock::now()+std::chrono::seconds(1));
        Sleep(520);layers={base};renderer.append(info,true,layers);
        check(layers.size()==1&&images.empty()&&!renderer.status().ready,"disabled sidecar unloads both eyes on reload");
        {std::ofstream file(config);file<<enabled;}
        std::filesystem::last_write_time(config,std::filesystem::file_time_type::clock::now()+std::chrono::seconds(2));
        Sleep(520);layers={base};renderer.append(info,true,layers);
        check(layers.size()==3&&renderer.status().uploads==4,"new configuration uploads each eye once");
        layers={base};renderer.append(info,false,layers);check(layers.size()==1,"shouldRender false suppresses helmet");
        renderer.reset();check(images.empty(),"swapchains cleaned");
        rejectStatic=true;check(initialize(2),"ordinary swapchain fallback");layers={base};renderer.append(info,true,layers);check(layers.size()==1,"layer limit preserves application layers");renderer.reset();rejectStatic=false;
        for(uint32_t capacity:{5u,6u,7u}) {
            check(initialize(capacity),"overlay layer capacity initialize");
            layers=existing;auto order=renderer.append(info,true,layers);
            if(capacity<7) {
                check(order.helmetAtLayerLimit&&!order.firstHelmetEye&&layers==existing&&!renderer.status().ready&&renderer.status().uploads==0,
                      "layer cap preserves all existing overlays and skips both helmet uploads");
            } else {
                check(layers.size()==7&&layers[0]==base&&layers[1]==secondBase&&
                      layers[2]->type==XR_TYPE_COMPOSITION_LAYER_QUAD&&layers[3]->type==XR_TYPE_COMPOSITION_LAYER_QUAD&&
                      layers[4]==gameBase&&layers[5]==hudBase&&layers[6]==spatialBase&&
                      renderer.status().ready&&renderer.status().uploads==2,
                      "exact layer capacity uploads the helmet below every existing overlay");
            }
            renderer.reset();
        }
        for(float stereo:{0.f,5.f,-5.f}) {
            {std::ofstream file(config);file<<R"({"version":1,"enabled":true,"leftImage":"OpenGridXR-helmet-iracing-left.png","rightImage":"OpenGridXR-helmet-iracing-right.png","headMotion":1,"vehicleMotion":1,"stereoOffset":)"<<stereo<<"}";}
            check(initialize(16),"stereo correction initialize");
            layers={base};renderer.append(info,true,layers);check(layers.size()==3,"stereo correction submits both eyes");
            auto correctedLeft=reinterpret_cast<const XrCompositionLayerQuad*>(layers[1]);
            auto correctedRight=reinterpret_cast<const XrCompositionLayerQuad*>(layers[2]);
            auto origin=[&](const XrCompositionLayerQuad *quad){
                const auto &image=images.at(quad->subImage.swapchain);
                return int((image.width-quad->subImage.imageRect.extent.width)/2);
            };
            const int correction=int(std::lround(stereo*.01f*correctedLeft->subImage.imageRect.extent.width));
            check(correctedLeft->subImage.imageRect.offset.x-origin(correctedLeft)==correction&&
                  correctedRight->subImage.imageRect.offset.x-origin(correctedRight)==-correction,
                  "positive stereo correction moves left artwork left and right artwork right; negative reverses it");
            check(correctedLeft->pose.position.x==neutralPose.position.x&&correctedLeft->pose.position.y==neutralPose.position.y&&
                  correctedLeft->size.width==outerWidth,"stereo correction never shifts outer quad geometry");
            for(auto quad:{correctedLeft,correctedRight}){
                const auto &image=images.at(quad->subImage.swapchain);
                const auto rect=quad->subImage.imageRect;
                check(rect.offset.x>=0&&rect.offset.x+rect.extent.width<=int(image.width)&&
                      rect.offset.y==0&&rect.extent.height==int(image.height),"static stereo crop stays inside the artwork");
            }
            renderer.reset();
        }
        auto half=root/"OpenGridXR-helmet-iracing-half.png",open=root/"OpenGridXR-helmet-iracing-open.png";
        writePng(half,0x8000FF00);writePng(open,0x80FF0000);
        const auto live=R"({"version":2,"generation":"live-1","enabled":true,"leftImage":"OpenGridXR-helmet-iracing-left.png","rightImage":"OpenGridXR-helmet-iracing-right.png","visorHotkey":true,"visorPosition":"half","halfImage":"OpenGridXR-helmet-iracing-half.png","openImage":"OpenGridXR-helmet-iracing-open.png"})";
        {std::ofstream file(config);file<<live;}
        check(initialize(16),"live visor initialize");
        int32_t submittedVisor = -1;
        auto sample=[&](bool down,bool foreground=true,bool render=true){
            renderer.shortcutSample({down,down,down,foreground});layers={base};info.displayTime+=11111111;renderer.append(info,render,layers,&submittedVisor);
        };
        auto shown=[&](uint32_t expected){
            check(layers.size()==3,"live visor keeps exactly two eye layers");
            for(unsigned eye=1;eye<=2;++eye)check(pixel(images.at(reinterpret_cast<const XrCompositionLayerQuad*>(layers[eye])->subImage.swapchain))==expected,"live visor uses same selected image for both eyes");
            check(images.size()==2,"completed switch retires previous static swapchains");
        };
        sample(false);shown(0x8000FF00);
        check(submittedVisor==1,"saved half visor is reported with its actual eye images");
        check(renderer.status().cycleAvailable,"ready live visor advertises controller cycling");
        check(!renderer.cycleVisor(false),"unfocused controller press cannot change visor");sample(false);shown(0x8000FF00);
        for(const auto expected:{0x80FF0000u,0x800000FFu,0x8000FF00u}) {
            const auto beforeCreates=creations;
            check(renderer.cycleVisor(true),"focused controller cycles to the next visor variant");
            check(creations==beforeCreates,"queuing a controller press does not prepare images or create swapchains on the background thread");
            check(!renderer.cycleVisor(true)&&renderer.status().cycleAvailable,"busy transition rejects duplicate press without withdrawing session capability");
            sample(false);shown(expected);check(renderer.status().cycleAvailable,"completed transition accepts the next press");
            check(submittedVisor==(expected==0x80FF0000u?2:expected==0x800000FFu?0:1),"reported controller position follows the displayed pair");
        }
        check(renderer.cycleVisor(true),"queue before focus loss");sample(false,false);shown(0x8000FF00);sample(false);shown(0x8000FF00);
        check(renderer.cycleVisor(true),"queue before focus event without an intervening frame");renderer.cancelVisorCycle();sample(false);shown(0x8000FF00);
        check(renderer.cycleVisor(true),"queue before rendering pause");sample(false,true,false);sample(false);shown(0x8000FF00);
        check(renderer.cycleVisor(true),"queue before frame timeout");Sleep(520);sample(false);shown(0x8000FF00);
        sample(true);shown(0x80FF0000);
        const auto liveUploads=renderer.status().uploads,liveCreates=creations;
        for(unsigned n=0;n<40;++n)sample(true);
        shown(0x80FF0000);check(renderer.status().uploads==liveUploads&&creations==liveCreates,"held visor shortcut does not reload or upload");
        sample(false);sample(true,false);sample(true);shown(0x80FF0000);
        sample(false);sample(true,true,false);check(layers.size()==1&&submittedVisor==-1,"inactive rendering suppresses visor");sample(true);shown(0x80FF0000);
        sample(false);timeout=true;sample(true);
        check(submittedVisor==2,"pending keyboard transition reports the previous displayed visor");
        check(layers.size()==3&&pixel(images.at(reinterpret_cast<const XrCompositionLayerQuad*>(layers[1])->subImage.swapchain))==0x80FF0000,"pending variant retains previous image");
        timeout=false;sample(true);shown(0x800000FF);
        {std::ofstream file(half);file<<"invalid";}
        check(renderer.cycleVisor(true),"controller queues unavailable variant without decoding on the background thread");sample(false);shown(0x800000FF);
        sample(false);sample(true);shown(0x800000FF);
        check(!renderer.status().error.empty()&&renderer.status().ready,"broken variant keeps last valid visor with error");
        check(submittedVisor==0,"failed visor image keeps the previously submitted position");
        writePng(half,0x8000FF00,128,72);sample(false);sample(true);shown(0x800000FF);
        check(!renderer.status().error.empty(),"mismatched variant dimensions cannot change fitted crop geometry");
        writePng(half,0x8000FF00);sample(true);shown(0x800000FF);
        auto manifestStamp=std::filesystem::last_write_time(config);
        std::string changed=live;changed.replace(changed.find("live-1"),6,"live-2");
        {std::ofstream file(config);file<<changed;}
        std::filesystem::last_write_time(config,manifestStamp);
        sample(false);sample(true);shown(0x800000FF);
        check(!renderer.status().error.empty(),"generation change with identical timestamp cannot mix old config and new variant");
        {std::ofstream file(config);file<<live;}
        std::filesystem::last_write_time(config,manifestStamp);
        sample(false);sample(true);shown(0x8000FF00);
        sample(false);sample(true);shown(0x80FF0000);
        {std::ofstream file(config);file<<live;}
        std::filesystem::last_write_time(config,std::filesystem::file_time_type::clock::now()+std::chrono::seconds(4));
        Sleep(520);sample(true);shown(0x8000FF00);
        check(renderer.status().ready,"saved manifest reload resets to saved visor state without held key repeat");
        renderer.reset();
        {std::ofstream file(config);file<<R"({"version":2,"enabled":true,"leftImage":"OpenGridXR-helmet-iracing-left.png","rightImage":"OpenGridXR-helmet-iracing-right.png","visorHotkey":false,"visorPosition":"open"})";}
        check(initialize(16),"static v2 initialize");sample(false);shown(0x800000FF);sample(true);shown(0x800000FF);
        check(submittedVisor==2,"static image baked with an open visor reports open");
        check(renderer.status().uploads==2,"static v2 uses selected baked canonical image and ignores shortcut");
        check(!renderer.status().cycleAvailable&&!renderer.cycleVisor(true),"disabled live visor rejects controller cycling");renderer.reset();
        writePng(left,0x800000FF,4096,2304);
        writePng(half,0x8000FF00,4096,2304);writePng(open,0x80FF0000,4096,2304);
        std::string detailed=live;detailed.replace(detailed.find("\"visorPosition\":\"half\""),22,"\"visorPosition\":\"closed\"");
        detailed.pop_back();detailed+=R"(,"headMotion":1,"vehicleMotion":1,"stereoOffset":5})";
        {std::ofstream file(config);file<<detailed;}
        check(renderer.initialize(reinterpret_cast<XrSession>(10),d12?static_cast<const void*>(&b12):&b11,16,api,locateViews,"iracing",root,4096,4096),"4096 detail initializes with runtime limits");
        sample(false);shown(0x800000FF);
        for(const auto &entry:images)check(entry.second.width<=4096&&entry.second.height<=4096,"detail plus stereo padding fits runtime image limit");
        check(renderer.status().ready&&renderer.status().imageReduced&&renderer.status().error.empty(),"4096 detail remains ready and reports a non-error load-time reduction");
        auto fittedRect=reinterpret_cast<const XrCompositionLayerQuad*>(layers[1])->subImage.imageRect.extent;
        check(fittedRect.width<4096&&std::abs(int64_t(fittedRect.width)*2304-int64_t(fittedRect.height)*4096)<=4096,"runtime fit preserves source aspect ratio within one pixel");
        sample(true);shown(0x8000FF00);sample(false);sample(true);shown(0x80FF0000);
        for(unsigned eye=1;eye<=2;++eye) {
            const auto rect=reinterpret_cast<const XrCompositionLayerQuad*>(layers[eye])->subImage.imageRect;
            check(rect.extent.width==fittedRect.width&&rect.extent.height==fittedRect.height,"all fitted visor states preserve identical geometry");
            const auto &target=images.at(reinterpret_cast<const XrCompositionLayerQuad*>(layers[eye])->subImage.swapchain);
            check(rect.offset.x>=0&&rect.offset.y>=0&&rect.offset.x+rect.extent.width<=int(target.width)&&rect.offset.y+rect.extent.height<=int(target.height),"fitted visor with stereo correction stays inside runtime dimensions");
        }
        const auto fittedUploads=renderer.status().uploads;
        for(unsigned frame=0;frame<30;++frame)sample(true);
        check(renderer.status().uploads==fittedUploads&&renderer.status().imageReduced,"fitted visor remains cached without per-frame resizing or uploads");
        renderer.reset();writePng(left,0x800000FF);
        {std::ofstream file(config);file<<enabled;}
        check(renderer.initialize(reinterpret_cast<XrSession>(10),d12?static_cast<const void*>(&b12):&b11,16,api,locateViews,"iracing",root,255,143),"limited runtime initializes safely");
        sample(false);check(layers.size()==1&&images.empty()&&!renderer.status().error.empty(),"image beyond minimum usable runtime dimensions fails closed");renderer.reset();
        {std::ofstream file(config);file<<R"({"version":1,"enabled":true,"leftImage":"OpenGridXR-helmet-iracing-left.png","rightImage":"OpenGridXR-helmet-iracing-right.png","stereoOffset":5.1})";}
        check(initialize(16),"invalid stereo correction initialize safe");layers={base};renderer.append(info,true,layers);
        check(layers.size()==1&&!renderer.status().error.empty(),"out-of-range stereo correction fails closed");renderer.reset();
        {std::ofstream file(config);file<<R"({"version":1,"enabled":true,"leftImage":"../outside.png","rightImage":"OpenGridXR-helmet-iracing-right.png"})";}
        check(initialize(16),"invalid config fails closed");layers={base};renderer.append(info,true,layers);check(layers.size()==1&&!renderer.status().error.empty(),"invalid paths never rendered");renderer.reset();
        {std::ofstream file(config);file<<enabled;}{std::ofstream file(left);file<<"invalid PNG";}
        check(initialize(16),"invalid PNG initialization safe");layers={base};renderer.append(info,true,layers);check(layers.size()==1&&!renderer.status().error.empty(),"invalid image disables both eyes");renderer.reset();
        {
            // The real enhancements host supplies the machine DLL directory,
            // while the editor writes mutable artwork to the current user's runtime.
            struct LocalDataScope {
                std::wstring previous;
                LocalDataScope() { wchar_t value[32768]{};auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",value,32768);if(n&&n<32768)previous=value; }
                ~LocalDataScope(){SetEnvironmentVariableW(L"LOCALAPPDATA",previous.empty()?nullptr:previous.c_str());}
            } localData;
            const auto machine=root/L"machine",user=root/L"test-user",artwork=user/L"OpenGridXR"/L"runtime";
            std::filesystem::create_directories(machine);std::filesystem::create_directories(artwork);
            check(SetEnvironmentVariableW(L"LOCALAPPDATA",user.c_str())!=0,"isolated user data environment");
            {std::ofstream marker(machine/L"OpenGridXR.machine");marker<<"fixture";}
            {std::ofstream file(machine/L"OpenGridXR-helmet-iracing.json");file<<R"({"version":1,"enabled":false})";}
            {std::ofstream file(artwork/L"OpenGridXR-helmet-iracing.json");file<<enabled;}
            writePng(artwork/L"OpenGridXR-helmet-iracing-left.png",0x8000FF00);
            check(renderer.initialize(reinterpret_cast<XrSession>(10),d12?static_cast<const void*>(&b12):&b11,16,api,locateViews,"iracing",machine),"machine-installed helmet initializes");
            layers={base};renderer.append(info,true,layers);
            check(layers.size()==3&&renderer.status().ready,"machine-installed helmet reads user artwork, not adjacent stale config");
            for(unsigned eye=1;eye<=2;++eye)check(pixel(images.at(reinterpret_cast<const XrCompositionLayerQuad*>(layers[eye])->subImage.swapchain))==0x8000FF00,"machine-installed eyes use the user's PNG");
            renderer.reset();check(images.empty(),"machine-installed helmet releases private swapchains");
        }
        // The application normally creates swapchains before its first frame.
        // Observe that the opt-in is already live at session creation.
        writePng(left,0x80808080);
        {std::ofstream file(config);file<<R"({"version":2,"enabled":true,"leftImage":"OpenGridXR-helmet-iracing-left.png","rightImage":"OpenGridXR-helmet-iracing-right.png","ambientLight":100})";}
        check(initialize(16)&&renderer.wantsSceneLight()&&!renderer.wheelLightProbe(),"helmet lighting active before application swapchain creation");
        const auto gameSwap=reinterpret_cast<XrSwapchain>(uintptr_t(12345678));
        XrCompositionLayerProjectionView gameViews[2]{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
        scene.viewCount=2;scene.views=gameViews;
        gameViews[0].subImage={gameSwap,{{16,0},{32,16}},1};
        renderer.sceneProjection(info);
        auto litFrame=[&] {
            info.displayTime+=11111111;layers={base};renderer.append(info,true,layers);
            check(layers.size()==3,"static or lit helmet retains two quads");
            auto first=reinterpret_cast<const XrCompositionLayerQuad*>(layers[1])->subImage.swapchain;
            check(first==reinterpret_cast<const XrCompositionLayerQuad*>(layers[2])->subImage.swapchain,
                  "both eyes use exactly one lighting solution");
            return first;
        };
        auto neutral=litFrame();
        check(pixel(images.at(neutral))==0x80808080,"no matching scene falls back to original artwork");
        ComPtr<ID3D11Device> writer=device11;
        ComPtr<ID3D11DeviceContext> writerContext=context11;
        ComPtr<ID3D11On12Device> writerBridge;
        if (d12) {
            IUnknown* queues[]{queue12.Get()};
            check(SUCCEEDED(D3D11On12CreateDevice(device12.Get(),0,nullptr,0,queues,1,0,
                writer.ReleaseAndGetAddressOf(),writerContext.ReleaseAndGetAddressOf(),nullptr)) &&
                SUCCEEDED(writer.As(&writerBridge)),"D3D12 game texture writer");
        }
        auto makeScene=[&](unsigned samples,ComPtr<ID3D11Texture2D>& texture,ComPtr<ID3D12Resource>& resource) {
            D3D11_TEXTURE2D_DESC td{};
            td.Width=64;td.Height=16;td.ArraySize=2;td.MipLevels=1;td.SampleDesc.Count=samples;
            td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            if (d12) {
                D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT};
                D3D12_RESOURCE_DESC desc{};
                desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=td.Width;desc.Height=td.Height;
                desc.DepthOrArraySize=2;desc.MipLevels=1;desc.SampleDesc.Count=samples;
                desc.Format=td.Format;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
                check(SUCCEEDED(device12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&resource))),"game D3D12 render target array");
                D3D11_RESOURCE_FLAGS flags{};flags.BindFlags=td.BindFlags;
                check(SUCCEEDED(writerBridge->CreateWrappedResource(resource.Get(),&flags,D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,IID_PPV_ARGS(&texture))),"write projection through game's queue");
            } else check(SUCCEEDED(writer->CreateTexture2D(&td,nullptr,&texture)),"game D3D11 render target array");
        };
        ComPtr<ID3D11Texture2D> gameTexture;
        ComPtr<ID3D12Resource> gameResource;
        makeScene(1,gameTexture,gameResource);
        auto* gameImage=static_cast<ID3D11Resource*>(gameTexture.Get());
        if(d12) writerBridge->AcquireWrappedResources(&gameImage,1);
        std::vector<uint32_t> colors(64*16,0xff00ff00);
        writerContext->UpdateSubresource(gameImage,0,nullptr,colors.data(),64*4,0);
        for(unsigned y=0;y<16;++y)for(unsigned x=16;x<48;++x)
            colors[y*64+x]=x<32?0xff0000ff:0xffff0000;
        writerContext->UpdateSubresource(gameImage,1,nullptr,colors.data(),64*4,0);
        if(d12){writerBridge->ReleaseWrappedResources(&gameImage,1);writerContext->Flush();}
        auto capture=[&](XrSwapchain swap) {
            renderer.captureScene(swap,DXGI_FORMAT_R8G8B8A8_UNORM,d12?nullptr:gameTexture.Get(),gameResource.Get());
        };
        auto uploads=renderer.status().uploads;
        capture(reinterpret_cast<XrSwapchain>(uintptr_t(123)));
        check(litFrame()==neutral,"other swapchain releases cannot light the helmet");
        context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
        capture(gameSwap);
        check(!renderer.wheelLightProbe(),"helmet-only capture leaves wheel lighting disabled");
        timeout=true;
        check(litFrame()==neutral,"busy dynamic image keeps original helmet visible");
        timeout=false;
        auto lit=litFrame();
        check(lit!=neutral,"sampled scene relights shared swapchain");
        const auto warm=pixel(images.at(lit),1),cool=pixel(images.at(lit),7);
        check((warm>>24)==128 && (cool>>24)==128,"scene lighting preserves straight alpha");
        check((warm&255)>((warm>>16)&255)+20 && ((cool>>16)&255)>(cool&255)+20 &&
              ((warm>>8)&255)<128 && ((cool>>8)&255)<128,
              "left red/right blue follow submitted subrect and slice, excluding green outside");
        D3D11_PRIMITIVE_TOPOLOGY afterLight;
        context11->IAGetPrimitiveTopology(&afterLight);
        check(afterLight==D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP,"probe and shading preserve game pipeline state");
        const auto drawn=releases;
        for(unsigned n=0;n<16;++n)check(litFrame()==lit,"unchanged scene remains lit");
        check(releases==drawn && renderer.status().uploads==uploads,"cached light adds no per-frame acquisition or full image uploads");
        renderer.forgetScene(gameSwap);
        check(litFrame()==neutral,"destroyed source restores static artwork");
        renderer.sceneProjection(info);
        gameTexture.Reset();gameResource.Reset();makeScene(4,gameTexture,gameResource);
        gameImage=gameTexture.Get();
        if(d12)writerBridge->AcquireWrappedResources(&gameImage,1);
        D3D11_RENDER_TARGET_VIEW_DESC rv{};rv.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        rv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;rv.Texture2DMSArray.ArraySize=2;
        ComPtr<ID3D11RenderTargetView> msTarget;
        check(SUCCEEDED(writer->CreateRenderTargetView(gameTexture.Get(),&rv,&msTarget)),"multisampled game scene view");
        const float black[]{0,0,0,1};writerContext->ClearRenderTargetView(msTarget.Get(),black);
        if(d12){writerBridge->ReleaseWrappedResources(&gameImage,1);writerContext->Flush();}
        capture(gameSwap);lit=litFrame();
        auto dark=pixel(images.at(lit));check((dark&255)<110 && (dark>>24)==128,"dark MSAA scene dims helmet");
        gameViews[0].subImage.imageRect.extent.width=1000;
        renderer.sceneProjection(info);capture(gameSwap);
        check(litFrame()==neutral,"invalid projection bounds restore neutral image");
        check(renderer.status().uploads==uploads,"GPU lighting never reuploads baked artwork");
        verifyReleasedViews=true;renderer.reset();verifyReleasedViews=false;
        const auto priorSpaces=spaceCreations,priorSwaps=creations,priorAcquires=acquisitions;
        const std::string wheelOnly=R"({"version":2,"enabled":false,"wheelAmbientLight":70})";
        {std::ofstream file(config);file<<wheelOnly;}
        auto bridge=std::make_shared<og::SessionD3D11On12>();
        check(renderer.initialize(reinterpret_cast<XrSession>(10),d12?static_cast<const void*>(&b12):&b11,1,api,locateViews,
                                  "iracing",root,0,0,true,bridge)&&renderer.wantsSceneLight()&&!renderer.wheelLightProbe(),
              "wheel alone opts in during session creation");
        gameViews[0].subImage.imageRect.extent.width=32;
        renderer.sceneProjection(info);
        capture(gameSwap);
        auto probe=renderer.wheelLightProbe();
        check(probe!=nullptr,"wheel receives fresh probe even with no helmet");
        std::weak_ptr<og::D3D11On12Binding> sessionBinding;
        if(d12) {
            auto consumer=bridge->acquire(device12.Get(),queue12.Get());
            ComPtr<ID3D11Device> probeDevice;probe->GetDevice(&probeDevice);
            check(consumer&&consumer->device==probeDevice,"wheel and scene sample use the same session D3D11On12 device");
            sessionBinding=consumer;
        }
        ComPtr<ID3D11Resource> probeResource;
        ComPtr<ID3D11Texture2D> probeTexture;
        probe->GetResource(&probeResource);
        check(SUCCEEDED(probeResource.As(&probeTexture)),"probe is a shader-readable texture");
        D3D11_TEXTURE2D_DESC probeDesc{};probeTexture->GetDesc(&probeDesc);
        check(probeDesc.Width==2&&probeDesc.Height==2,"wheel uses four GPU light samples");
        layers={base};renderer.append(info,true,layers);
        check(layers.size()==1&&!renderer.status().ready&&renderer.status().uploads==0&&images.empty()
              &&spaceCreations==priorSpaces&&creations==priorSwaps&&acquisitions==priorAcquires,
              "wheel-only probe creates no helmet reference spaces or swapchains");
        std::this_thread::sleep_for(std::chrono::milliseconds(520));
        check(!renderer.wheelLightProbe(),"stale light restores neutral wheel material");
        capture(gameSwap);
        check(renderer.wheelLightProbe()!=nullptr,"wheel recovers on the next valid sample");
        renderer.forgetScene(gameSwap);
        check(!renderer.wheelLightProbe(),"destroyed projection invalidates wheel probe immediately");
        renderer.sceneProjection(info);capture(gameSwap);
        gameViews[0].subImage.imageRect.extent.width=1024;
        renderer.sceneProjection(info);capture(gameSwap);
        check(!renderer.wheelLightProbe(),"unavailable projection restores neutral wheel material");
        renderer.reset();
        check(!renderer.wheelLightProbe()&&!renderer.wantsSceneLight(),"session reset clears wheel capture");
        check(sessionBinding.expired(),"idle session bridge releases its binding");
        D3D11_SHADER_RESOURCE_VIEW_DESC retainedView{};probe->GetDesc(&retainedView);
        check(retainedView.Format==DXGI_FORMAT_R16G16B16A16_FLOAT,"caller's probe remains valid after session reset");
        {std::ofstream file(config);file<<R"({"version":2,"enabled":true,"leftImage":"OpenGridXR-helmet-iracing-left.png","rightImage":"OpenGridXR-helmet-iracing-right.png","ambientLight":100})";}
        check(renderer.initialize(reinterpret_cast<XrSession>(10),d12?static_cast<const void*>(&b12):&b11,
                                  16,api,locateViews,"iracing",root,0,0,false) && !renderer.wantsSceneLight(),
              "separate queues and bridges retain the static path despite opted-in configuration");
        layers={base};renderer.append(info,true,layers);
        check(layers.size()==3,"static fallback still submits both quads");
        renderer.reset();
        std::filesystem::remove_all(root);CoUninitialize();
        std::cout<<"Helmet "<<(d12?"D3D11On12":"D3D11")<<" WARP mono PNG upload, visor cycling, layer ordering, alpha, cached submission, state, timeout, limits and failures passed.\n";
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';std::filesystem::remove_all(root);return 1;}
}
