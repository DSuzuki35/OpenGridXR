// Fake runtime: image ownership and callbacks only, no D3D/headset/game calls.
#define main submission_regression_main
#include "frame_submission_test.cpp"
#undef main
#include <atomic>
#include <thread>
#include <stdexcept>
namespace {
unsigned callDepth=0,appCalls[3]{};
XrResult imageResults[3]{XR_SUCCESS,XR_SUCCESS,XR_SUCCESS};
bool otherChain=true;
lmuqv::FocusImageLease* retiringLease=nullptr;
const auto leftChain=reinterpret_cast<XrSwapchain>(10);
const auto rightChain=reinterpret_cast<XrSwapchain>(11);
void check(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
void inspectImageCall(XrSwapchain chain){
    std::atomic<bool> available{false};std::thread observer([&]{if(stateMutex.try_lock()){available=true;stateMutex.unlock();}});observer.join();
    check(available,"application image runtime call held stateMutex");
    check(frameOperationInFlight(),"application image lifetime not reserved");
    uint32_t index=99,count=0;auto ai=structure<XrSwapchainImageAcquireInfo>(XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO);
    auto wi=structure<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);auto ri=structure<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);
    check(layerAcquireSwapchainImage(chain,&ai,&index)==XR_ERROR_CALL_ORDER_INVALID&&layerWaitSwapchainImage(chain,&wi)==XR_ERROR_CALL_ORDER_INVALID&&layerReleaseSwapchainImage(chain,&ri)==XR_ERROR_CALL_ORDER_INVALID,"same-chain image reentry reached runtime");
    check(layerBeginFrame(s.session,nullptr)==XR_ERROR_CALL_ORDER_INVALID&&layerEndFrame(s.session,nullptr)==XR_ERROR_CALL_ORDER_INVALID&&
          layerLocateViews(s.session,nullptr,nullptr,0,&count,nullptr)==XR_ERROR_CALL_ORDER_INVALID&&layerBeginSession(s.session,nullptr)==XR_ERROR_CALL_ORDER_INVALID&&
          layerEndSession(s.session)==XR_ERROR_CALL_ORDER_INVALID&&layerDestroySession(s.session)==XR_ERROR_CALL_ORDER_INVALID&&
          layerDestroyInstance(s.instance)==XR_ERROR_CALL_ORDER_INVALID&&layerCreateSwapchain(s.session,nullptr,nullptr)==XR_ERROR_CALL_ORDER_INVALID&&
          layerDestroySwapchain(chain)==XR_ERROR_CALL_ORDER_INVALID&&layerEnumerateSwapchainImages(chain,0,&count,nullptr)==XR_ERROR_CALL_ORDER_INVALID,"image reservation did not pin session/map/texture lifetime");
    lmuqv::FocusImageLease borrowed;const auto eye=s.appChains.at(chain).eye;
    check(!lmuqv::xr_hold_focus_image(eye,borrowed),"borrowed a runtime-busy application image");
    if(retiringLease&&chain==leftChain)check(!lmuqv::xr_focus_lease_valid(*retiringLease)&&!lmuqv::xr_retire_focus_image(*retiringLease),"focus retirement reentered busy release");
    if(callDepth==1&&otherChain){const auto other=chain==leftChain?rightChain:leftChain;
        check(layerAcquireSwapchainImage(other,&ai,&index)==XR_SUCCESS&&layerWaitSwapchainImage(other,&wi)==XR_SUCCESS&&layerReleaseSwapchainImage(other,&ri)==XR_SUCCESS,"independent app chain was blocked");
        check(frameOperationInFlight(),"nested image call retired outer reservation");}
}
XrResult XRAPI_CALL acquireImage(XrSwapchain chain,const XrSwapchainImageAcquireInfo*,uint32_t* index){++callDepth;++appCalls[0];inspectImageCall(chain);const auto r=callDepth==1?imageResults[0]:XR_SUCCESS;if(XR_SUCCEEDED(r))*index=0;--callDepth;return r;}
XrResult XRAPI_CALL waitImage(XrSwapchain chain,const XrSwapchainImageWaitInfo*){++callDepth;++appCalls[1];inspectImageCall(chain);const auto r=callDepth==1?imageResults[1]:XR_SUCCESS;--callDepth;return r;}
XrResult XRAPI_CALL releaseImage(XrSwapchain chain,const XrSwapchainImageReleaseInfo*){++callDepth;++appCalls[2];inspectImageCall(chain);const auto r=callDepth==1?imageResults[2]:XR_SUCCESS;--callDepth;return r;}
}
int main(){try{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetEnvironmentVariableA("LMU_QV_DIAGNOSTICS","0");
    Fixture f;s.focusOwnership=true;next.AcquireSwapchainImage=acquireImage;next.WaitSwapchainImage=waitImage;next.ReleaseSwapchainImage=releaseImage;
    auto ai=structure<XrSwapchainImageAcquireInfo>(XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO);auto wi=structure<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);auto ri=structure<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);uint32_t index=99;
    imageResults[0]=XR_ERROR_RUNTIME_FAILURE;check(layerAcquireSwapchainImage(leftChain,&ai,&index)==XR_ERROR_RUNTIME_FAILURE&&s.appChains.at(leftChain).acquired.empty()&&!frameOperationInFlight(),"failed acquire changed queue/reservation");
    imageResults[0]=XR_SUCCESS;check(layerAcquireSwapchainImage(leftChain,&ai,&index)==XR_SUCCESS&&s.appChains.at(leftChain).acquired.size()==1,"acquire was not recorded");
    imageResults[1]=XR_TIMEOUT_EXPIRED;check(layerWaitSwapchainImage(leftChain,&wi)==XR_TIMEOUT_EXPIRED&&!s.appChains.at(leftChain).acquired.front().waited&&!frameOperationInFlight(),"wait timeout changed ownership");
    imageResults[1]=XR_SUCCESS;check(layerWaitSwapchainImage(leftChain,&wi)==XR_SUCCESS&&s.appChains.at(leftChain).acquired.front().waited,"successful wait was not recorded");
    imageResults[2]=XR_ERROR_RUNTIME_FAILURE;check(layerReleaseSwapchainImage(leftChain,&ri)==XR_ERROR_RUNTIME_FAILURE&&s.appChains.at(leftChain).acquired.size()==1&&s.appChains.at(leftChain).acquired.front().waited&&!frameOperationInFlight(),"failed release consumed waited image");
    imageResults[2]=XR_SUCCESS;check(layerReleaseSwapchainImage(leftChain,&ri)==XR_SUCCESS&&s.appChains.at(leftChain).acquired.empty()&&!frameOperationInFlight(),"release did not retire ownership");
    otherChain=false;s.appChains.at(leftChain).images.push_back(reinterpret_cast<ID3D11Texture2D*>(1));
    check(layerAcquireSwapchainImage(leftChain,&ai,&index)==XR_SUCCESS&&layerWaitSwapchainImage(leftChain,&wi)==XR_SUCCESS,"lease fixture acquisition failed");
    for(auto* inFlight:{&s.locateInFlight,&s.focusBeginInFlight,&s.submitInFlight,&s.outer[0].runtimeBusy,&s.outer[1].runtimeBusy}){
        *inFlight=true;lmuqv::FocusImageLease blocked;check(!lmuqv::xr_hold_focus_image(0,blocked),"borrowed focus image during frame runtime operation");*inFlight=false;
    }
    lmuqv::FocusImageLease lease;check(lmuqv::xr_hold_focus_image(0,lease),"lease fixture hold failed");check(layerReleaseSwapchainImage(leftChain,&ri)==XR_SUCCESS&&s.appChains.at(leftChain).releasePending,"release was not deferred");
    retiringLease=&lease;imageResults[2]=XR_ERROR_RUNTIME_FAILURE;check(!lmuqv::xr_retire_focus_image(lease)&&lmuqv::xr_focus_lease_valid(lease)&&s.appChains.at(leftChain).releasePending&&s.appChains.at(leftChain).acquired.size()==1&&!frameOperationInFlight(),"failed deferred release lost lease/retry");
    imageResults[2]=XR_SUCCESS;check(lmuqv::xr_retire_focus_image(lease)&&!lease.token&&!s.appChains.at(leftChain).releasePending&&s.appChains.at(leftChain).acquired.empty()&&!frameOperationInFlight(),"deferred release did not finish");retiringLease=nullptr;
    std::cout<<"Application image lock regression PASS: unlocked Acquire/Wait/Release, pinned lifetime, same-chain guard, independent chains, timeout/failure retry, focus lease retirement\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
