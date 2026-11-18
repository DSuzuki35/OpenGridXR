// Exercise the production lifecycle adapter without an XR runtime or GPU.
#define main submission_regression_main
#include "frame_submission_test.cpp"
#undef main
#include <future>
#include <thread>

namespace {
XrResult beginResult=XR_SUCCESS,endResult=XR_SUCCESS,destroyResult=XR_SUCCESS;
XrViewConfigurationType begunType=XR_VIEW_CONFIGURATION_TYPE_MAX_ENUM;
XrResult XRAPI_CALL beginActive(XrSession,const XrSessionBeginInfo* info){begunType=info->primaryViewConfigurationType;return beginResult;}
XrResult XRAPI_CALL endActive(XrSession){return endResult;}
XrResult XRAPI_CALL destroyActive(XrSession){return destroyResult;}
XrResult XRAPI_CALL destroyActiveInstance(XrInstance){return XR_SUCCESS;}
void modes(bool quad,bool vrs,bool session=true){
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    s=State{};s.quad=quad;s.vrs=vrs;s.session=session?reinterpret_cast<XrSession>(1):XR_NULL_HANDLE;
    publishActiveModes();
    next.BeginSession=beginActive;next.EndSession=endActive;next.DestroySession=destroyActive;next.DestroyInstance=destroyActiveInstance;
    beginResult=endResult=destroyResult=XR_SUCCESS;
}
void expect(bool quad,bool vrs){assert(lmuqv::xr_quad_active()==quad&&lmuqv::xr_vrs_active()==vrs);}
void start(){XrSessionBeginInfo info{XR_TYPE_SESSION_BEGIN_INFO};info.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    assert(layerBeginSession(s.session,&info)==beginResult);}
void lifecycle(){
    expect(false,false);
    for(unsigned mode=0;mode<3;++mode){
        modes(mode==0,mode==1);expect(false,false);
        beginResult=XR_ERROR_RUNTIME_FAILURE;start();expect(false,false);
        beginResult=XR_SUCCESS;start();expect(mode==0,mode==1);
        assert(begunType==(mode==0?XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO:XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO));
        endResult=XR_ERROR_RUNTIME_FAILURE;assert(layerEndSession(s.session)==endResult);expect(mode==0,mode==1);
        endResult=XR_SUCCESS;assert(layerEndSession(s.session)==XR_SUCCESS);expect(false,false);
        start();expect(mode==0,mode==1);
        destroyResult=XR_ERROR_RUNTIME_FAILURE;assert(layerDestroySession(s.session)==destroyResult);expect(mode==0,mode==1);
        destroyResult=XR_SUCCESS;assert(layerDestroySession(s.session)==XR_SUCCESS);expect(false,false);
        modes(mode==0,mode==1);start();expect(mode==0,mode==1);
        {std::unique_lock<std::recursive_mutex> lock(stateMutex);clearSession(lock);}expect(false,false);
        modes(mode==0,mode==1);start();expect(mode==0,mode==1);
        assert(layerDestroyInstance(reinterpret_cast<XrInstance>(1))==XR_SUCCESS);expect(false,false);
    }
    // A successful downstream callback cannot activate a missing session.
    modes(true,false,false);start();expect(false,false);
    modes(false,true,false);start();expect(false,false);
    modes(true,false);start();expect(true,false);
    beginResult=XR_ERROR_RUNTIME_FAILURE;start();expect(false,false); // Preserve existing running-state semantics.
}
void readersDoNotTakeStateMutex(){
    modes(true,false);start();expect(true,false);
    std::promise<void> locked,unlock;
    auto lockReady=locked.get_future(),releaseLock=unlock.get_future();
    std::thread writer([&]{
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        // Readers must use the published snapshot, not these unpublished fields.
        s.quad=false;s.vrs=true;s.running=false;
        locked.set_value();releaseLock.wait();publishActiveModes();
    });
    lockReady.wait();
    auto reader=std::async(std::launch::async,[]{return std::make_pair(lmuqv::xr_quad_active(),lmuqv::xr_vrs_active());});
    const bool finishedWhileLocked=reader.wait_for(std::chrono::seconds(1))==std::future_status::ready;
    unlock.set_value();writer.join();
    const auto snapshot=reader.get();assert(finishedWhileLocked&&snapshot.first&&!snapshot.second);
    expect(false,false);
}
}
int main(){
    SetEnvironmentVariableA("LMU_QV_DIAGNOSTICS","0");
    lifecycle();readersDoNotTakeStateMutex();
    std::cout<<"XR active snapshots publish lifecycle results and read without stateMutex\n";
}
