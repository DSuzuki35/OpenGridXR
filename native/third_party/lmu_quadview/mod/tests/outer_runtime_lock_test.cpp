// Runtime ownership may block; it must not block state readers or teardown
// validation. Exercise prefetch, timeout retention and cleanup against the real
// adapter, without a GPU or a running OpenXR runtime.
#define main submission_regression_main
#include "frame_submission_test.cpp"
#undef main
#include <atomic>
#include <thread>
namespace {
unsigned acquisitions=0,blockingWaits=0,cleanupWaits=0;
XrResult acquisitionResult=XR_SUCCESS,waitResult=XR_SUCCESS;
void runtimeUnlocked(){
    std::atomic<bool> available{false};
    std::thread observer([&]{if(stateMutex.try_lock()){available=true;stateMutex.unlock();}});observer.join();
    if(!available)throw std::runtime_error("outer runtime call held stateMutex");
    if(lmuqv::xr_outer_target(0).rtv||lmuqv::xr_outer_target(1).rtv||lmuqv::xr_outer_rtv(0))
        throw std::runtime_error("outer runtime operation exposed a borrowed target");
    if(layerDestroySession(s.session)!=XR_ERROR_CALL_ORDER_INVALID||
       layerDestroyInstance(s.instance)!=XR_ERROR_CALL_ORDER_INVALID||
       layerEndSession(s.session)!=XR_ERROR_CALL_ORDER_INVALID||
       layerEndFrame(s.session,nullptr)!=XR_ERROR_CALL_ORDER_INVALID||
       layerBeginFrame(s.session,nullptr)!=XR_ERROR_CALL_ORDER_INVALID)
        throw std::runtime_error("outer runtime reservation did not protect resource lifetime");
}
XrResult XRAPI_CALL acquireUnlocked(XrSwapchain,const XrSwapchainImageAcquireInfo*,uint32_t* index){
    runtimeUnlocked();++acquisitions;*index=0;return acquisitionResult;
}
XrResult XRAPI_CALL waitUnlocked(XrSwapchain,const XrSwapchainImageWaitInfo* info){
    runtimeUnlocked();if(info->timeout!=0){assert(info->timeout>0&&info->timeout<=10000000);++blockingWaits;}else{++cleanupWaits;}
    return waitResult;
}
XrResult XRAPI_CALL releaseUnlocked(XrSwapchain chain,const XrSwapchainImageReleaseInfo* info){
    runtimeUnlocked();return fakeRelease(chain,info);
}
XrResult XRAPI_CALL beginUnlocked(XrSession,const XrFrameBeginInfo*){return XR_SUCCESS;}
XrResult XRAPI_CALL destroyUnlocked(XrSwapchain){runtimeUnlocked();return XR_SUCCESS;}
void prepare(){
    s.frame=false;s.waitPending=s.waitShouldRender=true;s.pendingTime=12345;
    for(auto& outer:s.outer){outer.acquired=outer.waited=false;outer.images.resize(1);}
    next.BeginFrame=beginUnlocked;next.AcquireSwapchainImage=acquireUnlocked;
    next.WaitSwapchainImage=waitUnlocked;next.ReleaseSwapchainImage=releaseUnlocked;next.DestroySwapchain=destroyUnlocked;
}
}
int main(){try{
    Fixture fixture;prepare();
    assert(layerBeginFrame(s.session,nullptr)==XR_SUCCESS);
    assert(acquisitions==2&&blockingWaits==2&&s.outer[0].waited&&s.outer[1].waited);
    {std::unique_lock<std::recursive_mutex> lock(stateMutex);
        assert(acquireOuterImage(0,lock)&&acquisitions==2&&blockingWaits==2); // No rendering-path wait.
        assert(releaseOuters(lock)&&releases==2);
        waitResult=XR_TIMEOUT_EXPIRED;
        assert(!acquireOuterImage(0,lock)&&s.outer[0].acquired&&!s.outer[0].waited);
        const auto acquired=acquisitions;
        assert(!releaseOuters(lock)&&cleanupWaits==1&&s.outer[0].acquired);
        waitResult=XR_SUCCESS;
        assert(acquireOuterImage(0,lock)&&acquisitions==acquired);
        assert(releaseOuters(lock)&&!s.outer[0].acquired);
        acquisitionResult=XR_ERROR_RUNTIME_FAILURE;
        const auto waited=blockingWaits;
        assert(!acquireOuterImage(0,lock)&&!s.outer[0].acquired&&blockingWaits==waited);
    }
    s.frame=false;s.waitPending=true;s.waitShouldRender=false;
    const auto acquired=acquisitions;
    assert(layerBeginFrame(s.session,nullptr)==XR_SUCCESS&&acquisitions==acquired);
    {std::unique_lock<std::recursive_mutex> lock(stateMutex);destroyOuter(lock);}
    std::cout<<"Outer runtime lock regression PASS: prefetched images, unlocked waits/releases, retained timeout ownership\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
