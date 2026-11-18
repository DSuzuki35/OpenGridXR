#include <d3d11.h>
#include <openxr/openxr_platform.h>
#include "cockpit_depth_copy.hpp"
#include "depthxr/openxr_layer.h"
#include <iostream>
#include <stdexcept>

namespace {
using Microsoft::WRL::ComPtr;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
const XrSwapchain swap = reinterpret_cast<XrSwapchain>(2);
ComPtr<ID3D11Texture2D> source;
bool rejectUsage{}, timeout{}, rejectRelease{};
int creates{};

XrResult XRAPI_PTR create(XrSession, const XrSwapchainCreateInfo* info, XrSwapchain* out) {
    ++creates;
    if (rejectUsage && (info->usageFlags & XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT)) return XR_ERROR_FEATURE_UNSUPPORTED;
    *out = swap;
    return XR_SUCCESS;
}
XrResult XRAPI_PTR enumerate(XrSwapchain, uint32_t size, uint32_t* count, XrSwapchainImageBaseHeader* out) {
    *count = 1;
    if (size) reinterpret_cast<XrSwapchainImageD3D11KHR*>(out)->texture = source.Get();
    return XR_SUCCESS;
}
}

namespace depthxr {
class IRacingDepthTestPeer {
  public:
    static void run() {
        OpenXrLayer layer;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
                                         &device,nullptr,&context)), "WARP device");
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=8; desc.Height=4; desc.ArraySize=2; desc.MipLevels=desc.SampleDesc.Count=1;
        desc.BindFlags=D3D11_BIND_DEPTH_STENCIL; desc.Format=DXGI_FORMAT_D32_FLOAT;
        check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&source)), "runtime depth");
        const auto session=reinterpret_cast<XrSession>(1);
        layer.active_session_=session;
        layer.iracing_depth_session_=session;
        layer.d3d11_quadviews_compositor_.device=device.Get();
        layer.d3d11_quadviews_compositor_.context=context.Get();
        layer.next_create_swapchain_=create;
        layer.next_enumerate_swapchain_images_=enumerate;
        layer.next_acquire_swapchain_image_=[](XrSwapchain,const XrSwapchainImageAcquireInfo*,uint32_t* i){*i=0;return XR_SUCCESS;};
        layer.next_wait_swapchain_image_=[](XrSwapchain,const XrSwapchainImageWaitInfo*){return timeout?XR_TIMEOUT_EXPIRED:XR_SUCCESS;};
        layer.next_release_swapchain_image_=[](XrSwapchain,const XrSwapchainImageReleaseInfo*){
            return rejectRelease?XR_ERROR_CALL_ORDER_INVALID:XR_SUCCESS;
        };
        layer.next_destroy_swapchain_=[](XrSwapchain){return XR_SUCCESS;};
        layer.next_begin_frame_=[](XrSession,const XrFrameBeginInfo*){return XR_SUCCESS;};
        layer.next_end_frame_=[](XrSession,const XrFrameEndInfo* f){return f&&f->layerCount==1?XR_SUCCESS:XR_ERROR_RUNTIME_FAILURE;};
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.format=desc.Format;ci.width=8;ci.height=4;ci.arraySize=2;ci.sampleCount=ci.mipCount=ci.faceCount=1;
        ci.usageFlags=XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        XrSwapchain handle{};
        rejectUsage=true;
        check(layer.CreateSwapchain(session,&ci,&handle)==XR_SUCCESS && creates==2 &&
              !layer.tracked_swapchains_.at(handle).cockpit_depth_eligible, "runtime usage refusal did not fall back");
        check(layer.DestroySwapchain(handle)==XR_SUCCESS,"destroy refused capture swapchain");
        rejectUsage=false;
        check(layer.CreateSwapchain(session,&ci,&handle)==XR_SUCCESS && layer.tracked_swapchains_.at(handle).cockpit_depth_eligible,
              "native depth eligibility");
        XrSwapchainImageD3D11KHR img{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};
        uint32_t count=0;
        check(layer.EnumerateSwapchainImages(handle,1,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(&img))==XR_SUCCESS,
              "runtime images");
        auto& chain=layer.tracked_swapchains_.at(handle);
        XrCompositionLayerProjectionView views[2]{};
        XrCompositionLayerDepthInfoKHR depths[2]{};
        for (uint32_t i=0;i<2;++i) {
            depths[i]={XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR};
            depths[i].minDepth=0;depths[i].maxDepth=1;depths[i].nearZ=.25f;depths[i].farZ=100;
            depths[i].subImage={handle,{{1,1},{6,3}},i};
            views[i]={XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,&depths[i]};
            views[i].pose.orientation.w=1;views[i].pose.position.x=i?.03f:-.03f;views[i].fov={-.6f,.7f,.8f,-.9f};
        }
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        projection.views=views;projection.viewCount=2;projection.space=reinterpret_cast<XrSpace>(3);
        auto header=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
        XrFrameEndInfo frame{XR_TYPE_FRAME_END_INFO};frame.displayTime=123;frame.layerCount=1;frame.layers=&header;
        layer.desktop_running_session_=session;
        layer.submitted_visor_position_=2;
        check(layer.ReadVisorPosition(session)==2 && layer.ReadVisorPosition(reinterpret_cast<XrSession>(4))==-1,
              "only the active session can supply the headset visor");
        XrFrameEndInfo rejected{XR_TYPE_FRAME_END_INFO};
        check(XR_FAILED(layer.SubmitRuntimeFrame(session,&rejected,nullptr)) && layer.ReadVisorPosition(session)==2,
              "failed runtime submission replaced the last accepted visor");
        og::CockpitDepthFrame captured{};
        std::array<std::shared_ptr<og::CockpitDepthCopy>,2> pinned;
        const auto collect=[&]{return layer.CollectCockpitDepth(frame,layer.cockpit_depth_epoch_,captured,pinned);};
        const auto release=[&] {
            uint32_t index=10;
            check(layer.AcquireSwapchainImage(handle,nullptr,&index)==XR_SUCCESS && index==0,"acquire");
            check(layer.WaitSwapchainImage(handle,nullptr)==XR_SUCCESS,"wait");
            return layer.ReleaseSwapchainImage(handle,nullptr);
        };
        check(layer.BeginFrame(session,nullptr)==XR_SUCCESS && release()==XR_SUCCESS && !chain.cockpit_depth,
              "disabled capture allocated a resource");
        // The normal renderer test drives wantsCockpitDepth through IPC. Here
        // set its latched result so this test exercises only the game hooks.
        layer.cockpit_depth_wanted_=true;
        check(release()==XR_SUCCESS&&collect(),"current stereo release missing");
        check(captured.time==123&&captured.space==projection.space&&captured.eyes[0].slice==0&&captured.eyes[1].slice==1,
              "wrong frame/eye association");
        og::CockpitDepthFrame stream{};
        check(!layer.ReadIRacingCockpitDepth(reinterpret_cast<XrSession>(4),frame,stream),"foreign stream session accepted");
        check(layer.ReadIRacingCockpitDepth(session,frame,stream) && stream.time==123 &&
              stream.eyes[1].slice==1 && stream.eyes[0].view==captured.eyes[0].view,"raw frame depth mismatch");
        for (auto& eye : stream.eyes) eye.view->Release();
        {
            std::unique_lock held(layer.mutex_);
            auto pending = std::async(std::launch::async,[&] { return layer.ReadIRacingCockpitDepth(session,frame,stream); });
            const bool returned=pending.wait_for(std::chrono::seconds(2))==std::future_status::ready;
            held.unlock();
            check(!pending.get() && returned,"stream capture blocked on the private engine mutex");
        }
        layer.cockpit_depth_frame_open_=false;
        check(!layer.ReadIRacingCockpitDepth(session,frame,stream),"already submitted stream frame accepted");
        layer.cockpit_depth_frame_open_=true;
        const auto first=chain.cockpit_depth.get();
        pinned={};
        check(release()==XR_SUCCESS && chain.cockpit_depth.get()==first,"steady release reallocated snapshot");
        check(collect()&&release()==XR_SUCCESS&&chain.cockpit_depth!=pinned[0],"pinned frame was overwritten");
        pinned={};
        {
            std::unique_lock lock(layer.mutex_);
            check(layer.ForwardEndFrame(session,&frame,lock,layer.cockpit_depth_epoch_)==XR_SUCCESS,"normal runtime end");
        }
        check(layer.ReadVisorPosition(session)==-1,"accepted frame without a helmet retained the old visor");
        check(layer.BeginFrame(session,nullptr)==XR_SUCCESS&&!collect()&&!chain.cockpit_depth &&
              !layer.ReadIRacingCockpitDepth(session,frame,stream),
              "old release survived the frame or demand transition");
        layer.cockpit_depth_wanted_=true;
        uint32_t index{};
        check(layer.AcquireSwapchainImage(handle,nullptr,&index)==XR_SUCCESS,"acquire for timeout");
        timeout=true;
        check(layer.WaitSwapchainImage(handle,nullptr)==XR_TIMEOUT_EXPIRED,"timeout changed");
        timeout=false;
        // The runtime rejects a release after timeout: the layer must never
        // touch the image or offer depth from this or a later uncertain release.
        rejectRelease=true;
        check(layer.ReleaseSwapchainImage(handle,nullptr)==XR_ERROR_CALL_ORDER_INVALID &&
              !chain.cockpit_depth && !chain.cockpit_depth_eligible && !collect(),"read after failed wait");
        rejectRelease=false;
        check(layer.DestroySwapchain(handle)==XR_SUCCESS&&layer.tracked_swapchains_.empty(),"swapchain teardown");
        layer.ResetSwapchainState();
        check(!layer.iracing_depth_session_.load()&&!layer.cockpit_depth_wanted_,"session teardown");
        source.Reset();
    }
};
}

int TestIRacingDepth() {
    try { depthxr::IRacingDepthTestPeer::run(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
