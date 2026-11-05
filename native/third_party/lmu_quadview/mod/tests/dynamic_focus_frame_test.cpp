// Real adapter frame state with the existing synchronous XR fixture.
#define main submission_regression_main
#include "frame_submission_test.cpp"
#undef main
XrResult XRAPI_CALL dynamicLocate(XrSession,const XrViewLocateInfo*,XrViewState* state,uint32_t,uint32_t* count,XrView* views){
    *count=4;state->viewStateFlags=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    for(unsigned i=0;i<4;++i){views[i].pose.orientation.w=1;views[i].fov={-.4f,.3f,.25f,-.2f};}
    return XR_SUCCESS;
}
int main(){
    SetEnvironmentVariableA("LMU_QV_DIAGNOSTICS","1");
    lmuqv::renderProfile.configure(true,lmuqv::log_line);
    assert(!frameGpuSamplingAllowed());
    lmuqv::renderProfile.configure(false,lmuqv::log_line);
    assert(frameGpuSamplingAllowed());
    s.engineGpuTimingArmed=true;assert(!frameGpuSamplingAllowed());
    s.engineGpuTimingArmed=false;assert(frameGpuSamplingAllowed());
    Fixture f;
    s.dynamicFocus.configure(true,70,120,2);s.framePeriod=10000000;
    // Only successfully submitted scene frames may own timing metadata.
    s.sceneFocus[0]=s.sceneFocus[1]=true;
    assert(layerEndFrame(s.session,&f.info)==XR_SUCCESS);
    assert(dynamicSamples[s.frames%128].valid&&dynamicSamples[s.frames%128].periodMs==10);
    assert(s.dynamicFocus.scale()==100); // no query result is not headroom
    ++s.frames;f.begin();s.sceneFocus[0]=s.sceneFocus[1]=false;
    assert(layerEndFrame(s.session,&f.info)==XR_SUCCESS);
    assert(!dynamicSamples[s.frames%128].valid);
    assert(!dynamicSamples[(s.frames-1)%128].valid); // pending pre-menu scene invalidated
    ++s.frames;f.begin();s.sceneFocus[0]=s.sceneFocus[1]=true;downstreamResult=XR_ERROR_RUNTIME_FAILURE;
    assert(layerEndFrame(s.session,&f.info)==XR_ERROR_RUNTIME_FAILURE);
    assert(!dynamicSamples[s.frames%128].valid);downstreamResult=XR_SUCCESS;
    ++s.frames;f.begin();s.sceneFocus[0]=s.sceneFocus[1]=true;s.shouldRender=false;
    assert(layerEndFrame(s.session,&f.info)==XR_SUCCESS);
    assert(!dynamicSamples[s.frames%128].valid);

    // The same display-time FOV must stay stable even if feedback arrives
    // between two locate calls. A new display time picks up the new scale.
    next.LocateViews=dynamicLocate;s.runtimeSettings=true;
    XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO};info.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;info.displayTime=100;
    XrViewState state{XR_TYPE_VIEW_STATE};XrView output[2]{};uint32_t count=0;
    assert(layerLocateViews(s.session,&info,&state,2,&count,output)==XR_SUCCESS);
    const XrFovf before=s.views[2].fov;
    for(uint64_t frame=8;frame<800;frame+=8)s.dynamicFocus.sample(12,10,frame,frame+1);
    assert(s.dynamicFocus.scale()==70);
    assert(layerLocateViews(s.session,&info,&state,2,&count,output)==XR_SUCCESS);
    assert(!std::memcmp(&s.views[2].fov,&before,sizeof(before)));
    info.displayTime=101;
    assert(layerLocateViews(s.session,&info,&state,2,&count,output)==XR_SUCCESS);
    const auto& after=s.views[2].fov;
    assert(std::abs((std::tan(after.angleRight)-std::tan(after.angleLeft))/(std::tan(before.angleRight)-std::tan(before.angleLeft))-.7)<1e-6);
    assert(!std::memcmp(&s.views[2].fov,&s.views[3].fov,sizeof(before)));
    assert(s.colorInfo.width==1024&&s.colorInfo.height==768);
    std::cout<<"Dynamic focus frame ownership, menu/failure guards and locate snapshot passed\n";
}
