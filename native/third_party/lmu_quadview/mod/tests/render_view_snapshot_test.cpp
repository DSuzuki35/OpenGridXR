// LMU can locate again after rendering, as seen in the live track log.
#define main submission_regression_main
#include "frame_submission_test.cpp"
#undef main
namespace {
int locateCall=0;
bool trackingValid=true;
XrResult locateResult=XR_SUCCESS;
XrResult XRAPI_CALL movingLocate(XrSession,const XrViewLocateInfo*,XrViewState* state,uint32_t,uint32_t* count,XrView* views){
    if(XR_FAILED(locateResult))return locateResult;
    ++locateCall;*count=4;state->viewStateFlags=trackingValid?XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT:0;
    for(unsigned i=0;i<4;++i){
        views[i].pose.orientation.w=1;views[i].pose.position.x=float(locateCall);
        views[i].fov=i<2?XrFovf{-.8f+.001f*locateCall,.8f,.7f,-.7f}:XrFovf{-.4f+.01f*locateCall,.4f+.01f*locateCall,.3f,-.3f};
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL beginSnapshot(XrSession,const XrFrameBeginInfo*){return XR_SUCCESS;}
}
int main(){
    Fixture f;next.LocateViews=movingLocate;
    XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO};info.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    info.space=s.locateSpace;info.displayTime=s.predictedTime;
    XrViewState state{XR_TYPE_VIEW_STATE};XrView output[2]{};uint32_t count=0;
    assert(layerLocateViews(s.session,&info,&state,2,&count,output)==XR_SUCCESS);
    XrView rendered[4]{};assert(lmuqv::xr_frame_views(rendered));
    XrFovf fullOuter[2]{};assert(lmuqv::xr_frame_full_outer_fov(fullOuter));
    for(unsigned eye=0;eye<2;++eye)f.views[eye].pose=rendered[eye].pose;
    assert(layerLocateViews(s.session,&info,&state,2,&count,output)==XR_SUCCESS);
    assert(output[0].pose.position.x==2); // caller still gets a fresh tracking result
    XrView secondEye[4]{};assert(lmuqv::xr_frame_views(secondEye));
    assert(secondEye[2].fov.angleLeft==rendered[2].fov.angleLeft);
    assert(secondEye[0].pose.position.x==rendered[0].pose.position.x);
    // Even a failed late tracking query must not erase the rendered frame.
    locateResult=XR_ERROR_RUNTIME_FAILURE;
    assert(layerLocateViews(s.session,&info,&state,2,&count,output)==XR_ERROR_RUNTIME_FAILURE);
    assert(lmuqv::xr_frame_views(secondEye));locateResult=XR_SUCCESS;
    trackingValid=false;
    assert(layerLocateViews(s.session,&info,&state,2,&count,output)==XR_SUCCESS&&state.viewStateFlags==0);
    assert(lmuqv::xr_frame_views(secondEye));trackingValid=true;
    info.displayTime+=10;info.space=reinterpret_cast<XrSpace>(99);
    assert(layerLocateViews(s.session,&info,&state,2,&count,output)==XR_SUCCESS);
    assert(s.locateTime==f.info.displayTime&&s.locateSpace==f.projection.space);
    XrFovf repeatedOuter[2]{};assert(lmuqv::xr_frame_full_outer_fov(repeatedOuter));
    assert(!std::memcmp(fullOuter,repeatedOuter,sizeof(fullOuter)));
    f.submit();
    assert(received.projectionCount==1);
    assert(received.views[2].fov.angleLeft==rendered[2].fov.angleLeft);
    assert(received.views[2].pose.position.x==rendered[2].pose.position.x);
    // Following frame must accept new gaze, never retain the old snapshot.
    next.BeginFrame=beginSnapshot;s.waitPending=true;s.waitShouldRender=true;s.pendingTime=info.displayTime;
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    assert(layerBeginFrame(s.session,&begin)==XR_SUCCESS);
    assert(layerLocateViews(s.session,&info,&state,2,&count,output)==XR_SUCCESS);
    assert(lmuqv::xr_frame_views(secondEye));
    assert(secondEye[2].fov.angleLeft!=rendered[2].fov.angleLeft);
    std::cout<<"Rendered view snapshot survives late, failed and foreign-space locate calls; next frame refreshes\n";
}
