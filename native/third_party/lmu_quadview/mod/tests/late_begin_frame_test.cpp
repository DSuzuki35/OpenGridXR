// Reproduce Pimax report: Wait -> Locate -> render/release -> Begin -> End.
#define main submission_regression_main
#include "frame_submission_test.cpp"
#undef main
#include <atomic>
#include <thread>
namespace {
unsigned begins=0;
XrResult beginResult=XR_SUCCESS;
bool pipelineDuringBegin=false;
bool trackingValid=true;
void requireUnlocked(){
    std::atomic<bool> available{false};
    std::thread observer([&]{if(stateMutex.try_lock()){available=true;stateMutex.unlock();}});
    observer.join();
    if(!available)throw std::runtime_error("runtime BeginFrame called with stateMutex held");
}
XrResult XRAPI_CALL lateBegin(XrSession,const XrFrameBeginInfo*){
    requireUnlocked();++begins;
    XrView views[4]{};XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};uint32_t count=0;
    if(layerBeginFrame(s.session,nullptr)!=XR_ERROR_CALL_ORDER_INVALID||
       layerLocateViews(s.session,&locate,nullptr,0,&count,nullptr)!=XR_ERROR_CALL_ORDER_INVALID||
       layerEndFrame(s.session,nullptr)!=XR_ERROR_CALL_ORDER_INVALID||
       layerEndSession(s.session)!=XR_ERROR_CALL_ORDER_INVALID||
       layerDestroySession(s.session)!=XR_ERROR_CALL_ORDER_INVALID||
       layerDestroyInstance(s.instance)!=XR_ERROR_CALL_ORDER_INVALID||lmuqv::xr_frame_views(views))
        throw std::runtime_error("in-flight Begin did not protect frame/session ownership");
    if(pipelineDuringBegin){std::lock_guard<std::recursive_mutex> lock(stateMutex);
        s.waitPending=true;s.pendingTime=s.locateTime+100;s.waitShouldRender=false;}
    return beginResult;
}
XrResult XRAPI_CALL lateLocate(XrSession,const XrViewLocateInfo*,XrViewState* state,uint32_t,uint32_t* count,XrView* views){
    *count=4;state->viewStateFlags=trackingValid?XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT:0;
    for(unsigned i=0;i<4;++i){views[i]=s.views[i];views[i].fov=i<2?XrFovf{-.8f,.8f,.7f,-.7f}:XrFovf{-.4f,.4f,.3f,-.3f};}
    return XR_SUCCESS;
}
void normalize(){
    XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO};info.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    info.space=s.locateSpace;info.displayTime=s.locateTime;
    XrViewState state{XR_TYPE_VIEW_STATE};XrView views[2]{};uint32_t count=0;
    if(layerLocateViews(s.session,&info,&state,2,&count,views)!=XR_SUCCESS)throw std::runtime_error("test locate failed");
}
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void pending(Fixture& f){
    clearFrame();s.runtimeSettings=true;s.running=true;s.waitPending=true;s.waitShouldRender=true;
    s.pendingTime=f.info.displayTime;s.pendingPeriod=11111111;s.locateTime=s.pendingTime;s.located=true;
    for(auto& outer:s.outer)outer.acquired=outer.waited=false;
    next.BeginFrame=lateBegin;next.LocateViews=lateLocate;begins=0;beginResult=XR_SUCCESS;pipelineDuringBegin=false;trackingValid=true;
}
}
int main(){try{
    Fixture f;pending(f);
    XrView views[4]{};
    XrViewLocateInfo countInfo{XR_TYPE_VIEW_LOCATE_INFO};countInfo.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    uint32_t count=0;
    require(layerLocateViews(s.session,&countInfo,nullptr,0,&count,nullptr)==XR_SUCCESS&&count==2&&begins==0,
        "count-only Locate began a frame");
    require(layerLocateViews(s.session,&countInfo,nullptr,1,&count,views)==XR_ERROR_SIZE_INSUFFICIENT&&begins==0,
        "insufficient-capacity Locate began a frame");
    require(!lmuqv::xr_frame_views(views)&&begins==0,"engine accessor performed a runtime Begin");
    normalize();
    require(s.renderPreparationPending,"normalized Begin did not defer D3D preparation to engine access");
    require(lmuqv::xr_frame_views(views),"late BeginFrame: render views unavailable after successful Wait/Locate");
    require(!s.renderPreparationPending,"engine access did not consume deferred preparation");
    require(begins==1&&s.frame,"runtime frame was not begun before capture");
    s.captures[0]=s.captures[1]=true;s.sceneFocus[0]=s.sceneFocus[1]=true;
    for(auto& outer:s.outer)outer.acquired=outer.waited=true;
    require(lmuqv::xr_frame_views(views)&&begins==1,"second eye began the frame again");
    require(layerEndFrame(s.session,&f.info)==XR_ERROR_CALL_ORDER_INVALID&&runtimeEnds==0&&releases==0,"End accepted before application Begin");
    auto frame=s.frames;
    // The next Wait may finish while the app is still about to acknowledge Begin.
    s.waitPending=true;s.pendingTime=f.info.displayTime+100;s.waitShouldRender=false;
    require(layerBeginFrame(s.session,nullptr)==XR_SUCCESS,"application begin failed");
    require(begins==1&&s.frames==frame&&s.captures[0]&&s.captures[1]&&s.renderViewsLocked&&s.sceneFocus[0],"late application begin reset rendered captures");
    require(s.waitPending&&s.pendingTime==f.info.displayTime+100&&s.predictedTime==f.info.displayTime&&s.shouldRender,"pipelined wait replaced current frame");
    f.submit();require(received.projectionCount==1&&received.projection.viewCount==4&&releases==2,"late-begin frame lost projection or image ownership");
    require(layerBeginFrame(s.session,nullptr)==XR_SUCCESS&&begins==2&&!s.shouldRender&&!s.located,"next waited frame was lost");
    for(int mode=0;mode<6;++mode){
        pending(f);
        if(mode==0)s.waitShouldRender=false;
        if(mode==1)s.waitPending=false;
        if(mode==2)s.locateTime++;
        if(mode==3)trackingValid=false;
        if(mode==4)s.runtimeSettings=false;
        if(mode==5)s.focusBeginInFlight=true;
        if(mode!=5)normalize(); // Concurrent Locate is rejected during a reserved Begin.
        require(!lmuqv::xr_frame_views(views)&&begins==0,"invalid pending render triggered runtime Begin");
        s.focusBeginInFlight=false;
    }
    for(auto result:{XR_ERROR_RUNTIME_FAILURE,XR_FRAME_DISCARDED}){
        pending(f);beginResult=result;
        normalize();
        XrFovf fovs[2]{};
        require(lmuqv::xr_frame_full_outer_fov(fovs)==XR_SUCCEEDED(result)&&begins==1,"early Begin result mishandled");
        if(XR_SUCCEEDED(result))require(s.renderPreparationPending,"read-only FOV accessor performed deferred render preparation");
        require(lmuqv::xr_frame_views(views)==XR_SUCCEEDED(result)&&begins==1,"failed/discarded early begin retried");
        require(layerBeginFrame(s.session,nullptr)==result&&begins==1,"app did not receive original runtime result");
    }
    pending(f);pipelineDuringBegin=true;normalize();
    require(s.waitPending&&s.pendingTime==f.info.displayTime+100&&s.predictedTime==f.info.displayTime&&s.shouldRender,
        "Wait completing inside runtime Begin replaced the reserved frame");
    require(layerBeginFrame(s.session,nullptr)==XR_SUCCESS&&begins==1,"normalized Begin forwarded twice");
    pending(f);pipelineDuringBegin=true;
    require(layerBeginFrame(s.session,nullptr)==XR_SUCCESS&&s.waitPending&&s.pendingTime==f.info.displayTime+100&&
        s.predictedTime==f.info.displayTime&&s.shouldRender,"normal Begin consumed a newer pipelined Wait");
    pending(f);s.outer[0].acquired=true;normalize();
    require(begins==0&&!s.frame,"Locate normalized an abandoned acquired image on a foreign D3D thread");
    std::cout<<"Late BeginFrame regression PASS: one runtime begin, captures preserved, four views submitted\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
