#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#include "helmet_renderer.hpp"
#include <filesystem>
#include <fstream>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <d3d11_4.h>
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
unsigned acquisitions{}, releases{}, creations{};
bool rejectStatic{};
bool quadViews{};
XrPosef head{{0, 0, 0, 1}, {0, 0, 0}};
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
    images.erase(s);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL createSpace(XrSession, const XrReferenceSpaceCreateInfo *info, XrSpace *out) {
    *out = reinterpret_cast<XrSpace>(
        uintptr_t(info->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_VIEW ? 1 : 2));
    return XR_SUCCESS;
}
XrResult XRAPI_CALL destroySpace(XrSpace) { return XR_SUCCESS; }
XrResult XRAPI_CALL locate(XrSpace, XrSpace, XrTime, XrSpaceLocation *out) {
    out->pose = head;
    out->locationFlags =
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    return XR_SUCCESS;
}
uint32_t pixel(const RuntimeImage &image) {
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
                                                         (image.width / 2) * 4);
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
        image.height / 2 * footprint.Footprint.RowPitch + image.width / 2 * 4);
    read->Unmap(0, nullptr);
    return value;
}
XrResult XRAPI_CALL locateViews(XrSession,const XrViewLocateInfo *info,XrViewState *state,uint32_t cap,uint32_t *count,XrView *views){
    check(info->space==reinterpret_cast<XrSpace>(1),"helmet locates original eye geometry in VIEW space");
    check(info->viewConfigurationType==(quadViews?XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO:XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO),"runtime active view configuration respected");
    *count=quadViews?4:2;if(cap<*count)return XR_ERROR_SIZE_INSUFFICIENT;
    state->viewStateFlags=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    for(unsigned i=0;i<2;++i){views[i].pose={{0,0,0,1},{i?.032f:-.032f,0,0}};views[i].fov={-.8f,.7f,.75f,-.65f};}
    if(quadViews)for(unsigned i=2;i<4;++i){views[i]=views[i-2];views[i].fov={-.2f,.2f,.2f,-.2f};}
    return XR_SUCCESS;
}
void writePng(const std::filesystem::path &path,uint32_t rgba){
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;
    check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"WIC fixture factory");
    check(SUCCEEDED(factory->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE)),"WIC fixture file");
    check(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)),"PNG fixture encoder");
    check(SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr))&&SUCCEEDED(frame->Initialize(nullptr))&&SUCCEEDED(frame->SetSize(256,144)),"PNG fixture frame");
    auto format=GUID_WICPixelFormat32bppBGRA;check(SUCCEEDED(frame->SetPixelFormat(&format))&&format==GUID_WICPixelFormat32bppBGRA,"fixture pixel format");
    uint32_t bgra=(rgba&0xFF00FF00u)|((rgba&255)<<16)|((rgba>>16)&255);
    std::vector<uint32_t> data(256*144,bgra);
    check(SUCCEEDED(frame->WritePixels(144,256*4,UINT(data.size()*4),reinterpret_cast<BYTE*>(data.data())))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit()),"PNG fixture output");
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
        og::OverlayDispatch api{formats,create,enumerate,acquire,waitImage,releaseImage,destroy,createSpace,destroySpace,locate};
        og::HelmetRenderer renderer;
        auto initialize=[&](uint32_t maxLayers){return renderer.initialize(reinterpret_cast<XrSession>(10),d12?static_cast<const void*>(&b12):&b11,maxLayers,api,locateViews,"iracing",root);};
        check(initialize(16),"helmet initialize");
        XrCompositionLayerProjection scene{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        const XrCompositionLayerBaseHeader *base=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&scene);
        XrFrameEndInfo info{XR_TYPE_FRAME_END_INFO};info.displayTime=1000000000;info.layerCount=1;info.layers=&base;
        context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        timeout=true;std::vector<const XrCompositionLayerBaseHeader*> layers{base};renderer.append(info,true,layers);
        check(layers.size()==1,"pending image does not submit a partial stereo pair");timeout=false;
        renderer.append(info,true,layers);check(layers.size()==3,"two eye overlays appended");
        auto l=reinterpret_cast<const XrCompositionLayerQuad*>(layers[1]);auto r=reinterpret_cast<const XrCompositionLayerQuad*>(layers[2]);
        check(l->eyeVisibility==XR_EYE_VISIBILITY_LEFT&&r->eyeVisibility==XR_EYE_VISIBILITY_RIGHT,"eye isolation");
        check(l->layerFlags&XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT,"straight alpha contract");
        check(pixel(images.at(l->subImage.swapchain))==0x800000FF,"left PNG RGBA preserved");
        check(pixel(images.at(r->subImage.swapchain))==0x40FF0000,"right PNG RGBA preserved");
        D3D11_PRIMITIVE_TOPOLOGY topology;context11->IAGetPrimitiveTopology(&topology);check(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"game state preserved");
        check(renderer.status().ready&&renderer.status().uploads==2,"one upload per eye");
        for(int n=0;n<90;++n){layers={base};info.displayTime+=11111111;renderer.append(info,true,layers);check(layers.size()==3,"cached helmet submission");}
        check(acquisitions==2&&releases==2&&creations==2&&renderer.status().uploads==2,"no per-frame uploads or swapchain acquisition");
        const auto outerWidth=l->size.width;
        quadViews=true;renderer.viewConfiguration(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO);
        layers={base};renderer.append(info,true,layers);
        check(layers.size()==3&&l->size.width==outerWidth,"native quad views use outer context pair rather than focus inset");
        quadViews=false;renderer.viewConfiguration(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO);
        auto neutralRect=l->subImage.imageRect;auto neutralPose=l->pose;
        for(int n=1;n<=12;++n){head.orientation={0,std::sin(n*.1f),0,std::cos(n*.1f)};info.displayTime+=11111111;layers={base};renderer.append(info,true,layers);}
        check(l->subImage.imageRect.offset.x!=neutralRect.offset.x,"head movement shifts cached sampling window");
        check(l->pose.position.x==neutralPose.position.x&&l->pose.position.y==neutralPose.position.y,"outer coverage stays fixed during sway");
        auto& texture=images.at(l->subImage.swapchain);auto rect=l->subImage.imageRect;
        check(rect.offset.x>=0&&rect.offset.y>=0&&rect.offset.x+rect.extent.width<=int(texture.width)&&rect.offset.y+rect.extent.height<=int(texture.height),"motion never samples beyond padded image");
        check(renderer.status().uploads==2,"motion adds no uploads");
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
        {std::ofstream file(config);file<<R"({"version":1,"enabled":true,"leftImage":"../outside.png","rightImage":"OpenGridXR-helmet-iracing-right.png"})";}
        check(initialize(16),"invalid config fails closed");layers={base};renderer.append(info,true,layers);check(layers.size()==1&&!renderer.status().error.empty(),"invalid paths never rendered");renderer.reset();
        {std::ofstream file(config);file<<enabled;}{std::ofstream file(left);file<<"invalid PNG";}
        check(initialize(16),"invalid PNG initialization safe");layers={base};renderer.append(info,true,layers);check(layers.size()==1&&!renderer.status().error.empty(),"invalid image disables both eyes");renderer.reset();
        std::filesystem::remove_all(root);CoUninitialize();
        std::cout<<"Helmet "<<(d12?"D3D11On12":"D3D11")<<" WARP stereo PNG upload, alpha, cached submission, state, timeout, limits and failures passed.\n";
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';std::filesystem::remove_all(root);return 1;}
}
