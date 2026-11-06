#define main upstream_submission_main
#include "../third_party/lmu_quadview/mod/tests/frame_submission_test.cpp"
#undef main
#include "lmu_integration.hpp"
#include <wrl/client.h>
#include <atomic>
#include <thread>
namespace {
unsigned hostSubmissions=0;
unsigned runtimeBridges=0;
XrFovf hostFov{};
og::lmu::Frame submittedFrame{};
unsigned imageCalls[4]{};
void hostSubmit(XrSession,const og::lmu::Frame& frame,const XrCompositionLayerProjection& projection,XrTime time) {
    assert(time==frame.time && projection.viewCount==4);
    assert(!s.outer[0].acquired&&!s.outer[1].acquired);
    hostFov=frame.views[2].fov;
    submittedFrame=frame;
    ++hostSubmissions;
}
void hostBorrowed(XrSession,const og::lmu::Frame& frame,unsigned slot,ID3D11Texture2D*,DXGI_FORMAT,const og::ImageRegion& region){
    assert(frame.id==s.frames&&region.arraySlice==0&&region.x==0&&region.y==0&&region.width==32&&region.height==32);
    if(slot<2)assert(s.outer[slot].acquired&&s.outer[slot].waited);
    else {const auto& app=s.appChains.at(reinterpret_cast<XrSwapchain>(uintptr_t(10+slot-2)));assert(!app.acquired.empty()&&app.acquired.front().waited);}
    ++imageCalls[slot];
}
void snapshotAndOwnership(){
    Fixture fixture;s.runtimeSettings=true;s.cropEdges={0,0,8,21};
    static int locateVersion=0;
    next.LocateViews=[](XrSession,const XrViewLocateInfo*,XrViewState* state,uint32_t,uint32_t* count,XrView* views)->XrResult{
        ++locateVersion;*count=4;state->viewStateFlags=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
        for(unsigned i=0;i<4;++i){views[i].pose.orientation.w=1;views[i].pose.position.x=float(locateVersion);
            views[i].fov=i<2?XrFovf{float(std::atan(-1.)),float(std::atan(1.)),float(std::atan(1.)),float(std::atan(-1.))}:
                XrFovf{float(std::atan(-.2+.01*locateVersion)),float(std::atan(.2+.01*locateVersion)),float(std::atan(.2)),float(std::atan(-.2))};}
        return XR_SUCCESS;
    };
    XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO};info.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;info.displayTime=s.predictedTime;info.space=s.locateSpace;
    XrViewState state{XR_TYPE_VIEW_STATE};XrView output[2]{},locked[4]{},again[4]{};uint32_t count=0;
    assert(layerLocateViews(s.session,&info,&state,2,&count,output)==XR_SUCCESS&&lmuqv::xr_frame_views(locked));
    assert(std::abs(std::tan(locked[0].fov.angleUp)-.84)<1e-6&&std::abs(std::tan(locked[0].fov.angleDown)+.58)<1e-6);
    assert(std::abs(std::tan(s.fullOuterFov[0].angleUp)-1)<1e-6);
    assert(layerLocateViews(s.session,&info,&state,2,&count,output)==XR_SUCCESS&&output[0].pose.position.x==2);
    assert(lmuqv::xr_frame_views(again)&&!std::memcmp(locked,again,sizeof(locked)));
    next.LocateViews=[](XrSession,const XrViewLocateInfo*,XrViewState*,uint32_t,uint32_t*,XrView*)->XrResult{return XR_ERROR_RUNTIME_FAILURE;};
    info.displayTime+=123;info.space=reinterpret_cast<XrSpace>(99);
    assert(layerLocateViews(s.session,&info,&state,2,&count,output)==XR_ERROR_RUNTIME_FAILURE&&lmuqv::xr_frame_views(again));
    assert(!std::memcmp(locked,again,sizeof(locked)));
    for(unsigned eye=0;eye<2;++eye)fixture.views[eye].pose=locked[eye].pose;
    fixture.submit();assert(!std::memcmp(&hostFov,&locked[2].fov,sizeof(hostFov)));

    Fixture images;s.renderViewsLocked=true;s.colorInfo.width=s.colorInfo.height=32;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr)));
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=32;desc.ArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> textures[4];
    for(unsigned slot=0;slot<4;++slot){assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&textures[slot])));
        if(slot<2){s.outer[slot].width=s.outer[slot].height=32;s.outer[slot].images.push_back({XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR,nullptr,textures[slot].Get()});}
        else{auto& app=s.appChains.at(reinterpret_cast<XrSwapchain>(uintptr_t(10+slot-2)));app.images.push_back(textures[slot].Get());app.acquired.push_back({0,true});
            app.info.width=app.info.height=32;images.views[slot-2].subImage.imageArrayIndex=0;images.views[slot-2].subImage.imageRect={{0,0},{32,32}};}}
    og::lmu::host.image=hostBorrowed;
    next.ReleaseSwapchainImage=[](XrSwapchain chain,const XrSwapchainImageReleaseInfo* info)->XrResult{
        if(chain==reinterpret_cast<XrSwapchain>(10)||chain==reinterpret_cast<XrSwapchain>(11))return releaseFails?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;
        return fakeRelease(chain,info);
    };
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    {
    std::unique_lock<std::recursive_mutex> lock(stateMutex);
    releaseFails=true;
    for(unsigned retry=0;retry<2;++retry)assert(releaseAppImageNow(reinterpret_cast<XrSwapchain>(10),&release,lock)==XR_ERROR_RUNTIME_FAILURE);
    assert(imageCalls[2]==1);
    releaseFails=false;
    assert(releaseAppImageNow(reinterpret_cast<XrSwapchain>(10),&release,lock)==XR_SUCCESS);
    assert(releaseAppImageNow(reinterpret_cast<XrSwapchain>(11),&release,lock)==XR_SUCCESS);
    }
    images.submit();for(unsigned slot=0;slot<4;++slot)assert(imageCalls[slot]==1);
    og::lmu::host.image=nullptr;
}
void pipelinedSubmission(){
    for(auto result:{XR_SUCCESS,XR_ERROR_RUNTIME_FAILURE}){
        Fixture fixture;
        const auto before=hostSubmissions;
        const auto current=hostFrame();
        next.EndFrame=[](XrSession session,const XrFrameEndInfo* info)->XrResult{
            std::atomic<bool> unlocked{false};
            std::thread observer([&]{if(stateMutex.try_lock()){unlocked=true;stateMutex.unlock();}});
            observer.join();assert(unlocked&&s.submitInFlight);
            next.WaitFrame=[](XrSession,const XrFrameWaitInfo*,XrFrameState* state)->XrResult{
                state->predictedDisplayTime=23456;state->predictedDisplayPeriod=22222222;state->shouldRender=XR_FALSE;
                return XR_SUCCESS;
            };
            XrFrameState state{XR_TYPE_FRAME_STATE};
            assert(layerWaitFrame(session,nullptr,&state)==XR_SUCCESS);
            assert(s.waitPending&&s.pendingTime==23456&&s.predictedTime==12345);
            return fakeEnd(session,info);
        };
        downstreamResult=result;
        assert(layerEndFrame(s.session,&fixture.info)==result);
        assert(s.waitPending&&s.pendingTime==23456&&!s.submitInFlight);
        assert(hostSubmissions==before+(XR_SUCCEEDED(result)?1:0));
        if(XR_SUCCEEDED(result)){
            assert(submittedFrame.id==current.id&&submittedFrame.time==current.time&&submittedFrame.space==current.space);
            assert(!std::memcmp(submittedFrame.views,current.views,sizeof(current.views)));
        }
    }
}
}
int main() {
    const auto diagnosticDirectory=std::filesystem::temp_directory_path()/(L"opengrid-adapter-diagnostic-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(diagnosticDirectory);
    SetEnvironmentVariableW(L"OPENGRIDXR_STATUS_DIR",diagnosticDirectory.c_str());
    SetEnvironmentVariableA("LMU_QV_DIAGNOSTICS","0");
    char* upstreamArgs[]={const_cast<char*>("submission")};upstream_submission_main(1,upstreamArgs);
    Fixture fixture;
    og::lmu::settings.enabled=og::lmu::settings.quad=true;
    og::lmu::host.submit=hostSubmit;
    og::lmu::host.runtimeSubmit=[](XrSession session,PFN_xrEndFrame submit,const XrFrameEndInfo* info)->XrResult{
        assert(s.submitInFlight);++runtimeBridges;
        std::atomic<bool> unlocked{false};
        std::thread observer([&]{if(stateMutex.try_lock()){unlocked=true;stateMutex.unlock();}});observer.join();
        assert(unlocked);return submit(session,info);
    };
    fixture.submit();
    assert(runtimeBridges==1);
    if(hostSubmissions!=1) { std::cerr<<"Finalized four-view submission never reaches OpenGrid host\n"; return 1; }
    assert(hostFov.angleLeft==received.views[2].fov.angleLeft);
    snapshotAndOwnership();
    pipelinedSubmission();
    og::lmu::report_diagnostic("ready","Hooks ready");
    lmuqv::log_line("engine: outer/focus pose mismatch");
    assert(og::lmu::diagnostic_snapshot().state=="ready");
    lmuqv::log_line("native_outer refused reason=resource_not_ready; full-resolution fallback");
    assert(og::lmu::diagnostic_snapshot().state=="ready"&&og::lmu::diagnostic_snapshot().nativeOuterRefusalCount==1);
    lmuqv::log_line("native_outer failed to restore nested bindings; quad rendering refused");
    assert(og::lmu::diagnostic_snapshot().state=="unavailable");
    // Invalid or unrecognized executables must never get a functioning session,
    // including the no-D3D path which formerly bypassed engine_start entirely.
    XrSessionCreateInfo create{XR_TYPE_SESSION_CREATE_INFO};XrSession session{};
    s.session=XR_NULL_HANDLE;
    next.CreateSession=[](XrInstance,const XrSessionCreateInfo*,XrSession* out)->XrResult{*out=reinterpret_cast<XrSession>(123);return XR_SUCCESS;};
    s.quad=false;s.runtimeSettings=false;
    if(layerCreateSession(reinterpret_cast<XrInstance>(1),&create,&session)!=XR_ERROR_INITIALIZATION_FAILED){std::cerr<<"Integrated session bypassed executable/D3D guard\n";return 1;}
    const auto diagnostic=og::lmu::diagnostic_snapshot();
    assert(diagnostic.state=="unavailable"&&diagnostic.reason.find("D3D11")!=std::string::npos);
    assert(std::filesystem::exists(diagnosticDirectory/L"OpenGridXR-lmu-diagnostics.json"));
    assert(diagnostic.reentryCount==0); // Explicit tests below count only callback Locate attempts.
    {
        Fixture rejected;s.submitInFlight=true;
        runtimeCall([&]{uint32_t count=0;return layerLocateViews(s.session,nullptr,nullptr,0,&count,nullptr);});
        s.submitInFlight=false;
        assert(og::lmu::diagnostic_snapshot().reentryCount==1);
    }
    og::lmu::reset();
    SetEnvironmentVariableW(L"OPENGRIDXR_STATUS_DIR",nullptr);
    std::filesystem::remove_all(diagnosticDirectory);
    std::cout<<"Integrated adapter publishes finalized four-view geometry after releases\n";
}
