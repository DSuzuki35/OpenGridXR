#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "xr_layer.hpp"
#include "xr_capture.hpp"
#include "focus_overlay.hpp"
#include "xr_sizes.hpp"
#include "focus_box.hpp"
#ifdef OPENGRIDXR_LMU_INTERNAL
#include "lmu_integration.hpp"
#include "lmu_diagnostics.hpp"
#endif
#include <limits>
#include "view_crop.hpp"
#include "outer_resample.hpp"
#include "render_profile.hpp"
#include "swapchain_sampling.hpp"
#include "submission_pose.hpp"
#include "stereo_audit.hpp"
#include "vrs_gaze.hpp"
#include "vrs_foveation.hpp"
#include "benchmark_overlay.hpp"
#include "benchmark_gpu.hpp"
#include "async_log.hpp"
#include "diagnostic_sampling.hpp"
#include "cpu_cache_affinity.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <condition_variable>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
constexpr char layerName[] = "XR_APILAYER_LMU_quadview";
std::recursive_mutex stateMutex;
std::condition_variable_any trackingFinished;
thread_local unsigned runtimeCallDepth=0;
template<class F> auto runtimeCall(F&& function){
    struct Scope {Scope(){++runtimeCallDepth;}~Scope(){--runtimeCallDepth;}} scope;
    return function();
}
template<class T> T structure(XrStructureType type) { T value{}; value.type=type; return value; }
void logf(const char* format, ...) {
    if(!lmuqv::diagnostics_enabled())return;
    char text[2048]{};
    va_list args; va_start(args,format); std::vsnprintf(text,sizeof(text),format,args); va_end(args);
    lmuqv::log_line(text);
}
bool quadMode() {
    char value[32]{};
    GetEnvironmentVariableA("LMU_QV_MODE",value,sizeof(value));
    return !std::strcmp(value,"quad");
}
bool vrsMode(){char value[32]{};GetEnvironmentVariableA("LMU_QV_MODE",value,sizeof(value));return !std::strcmp(value,"vrs");}
lmuqv::VrsGaze vrsGaze;
std::wstring environmentWide(const wchar_t* name) {
    DWORD size=GetEnvironmentVariableW(name,nullptr,0);if(!size||size>32768)return {};
    std::wstring value(size,L'\0');DWORD written=GetEnvironmentVariableW(name,value.data(),size);
    if(written>=size)return {};
    value.resize(written);return value;
}
bool gameProcess() {
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr,path,32768);
    return !_wcsicmp(std::filesystem::path(path).filename().c_str(),L"Le Mans Ultimate.exe");
}
struct Dispatch {
    PFN_xrGetInstanceProcAddr get{};
#define XR_NEXT_MEMBERS(X) \
    X(DestroyInstance) X(GetSystemProperties) X(EnumerateViewConfigurations) X(GetViewConfigurationProperties) \
    X(EnumerateViewConfigurationViews) X(EnumerateEnvironmentBlendModes) X(CreateSession) \
    X(DestroySession) X(BeginSession) X(EndSession) X(WaitFrame) X(BeginFrame) X(EndFrame) \
    X(LocateViews) X(CreateSwapchain) X(DestroySwapchain) X(EnumerateSwapchainImages) \
    X(AcquireSwapchainImage) X(WaitSwapchainImage) X(ReleaseSwapchainImage)
#define DECLARE(name) PFN_xr##name name{};
    XR_NEXT_MEMBERS(DECLARE)
#undef DECLARE
    bool load(XrInstance instance) {
        bool okay=true;
#define LOAD(name) { PFN_xrVoidFunction fn{}; XrResult r=get(instance,"xr" #name,&fn); name=reinterpret_cast<PFN_xr##name>(fn); if(XR_FAILED(r)||!name) {logf("Missing downstream xr%s: %d",#name,r);okay=false;} }
        XR_NEXT_MEMBERS(LOAD)
#undef LOAD
        return okay;
    }
} next;
lmuqv::OuterResample outerResample; // Owns COM state; deliberately outside the copy-resettable XR State.
lmuqv::OuterResample cropResample; // Separate cache: does not consume native source SRV slots.
struct Outer {
    XrSwapchain chain{};
    std::vector<XrSwapchainImageD3D11KHR> images;
    std::vector<ID3D11RenderTargetView*> rtvs;
    bool acquired=false, waited=false,runtimeBusy=false;
    uint32_t index=0;
    uint32_t width=0,height=0;
    uint32_t outputWidth=0,outputHeight=0;
    ID3D11Texture2D* croppedTexture{};
    ID3D11RenderTargetView* croppedRtv{};
};
struct AppImageOwnership { uint32_t index=0;bool waited=false; };
struct AppChain {
    XrSwapchainCreateInfo info{};
    unsigned eye=2;
    // Borrowed runtime textures; never dereference after successful DestroySwapchain.
    std::vector<ID3D11Texture2D*> images;
    std::vector<ID3D11RenderTargetView*> focusRtvs;
    std::deque<AppImageOwnership> acquired;
    bool trackingInvalid=false,runtimeBusy=false;
    uint64_t focusLease=0,leaseFrame=0;DWORD leaseOwner=0;
    bool releasePending=false;
    XrSwapchainImageReleaseInfo pendingRelease{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO,nullptr};
};
struct State {
    XrInstance instance{};
    XrSession session{};
    XrSystemId system{};
    bool quad=false, vrs=false, running=false, frame=false, shouldRender=false, located=false;
    bool locateInFlight=false;
    unsigned trackingReaders=0;
    bool renderViewsLocked=false; // Rendering, UI and submission share one locate snapshot.
    bool renderPreparationPending=false; // Normalized Begin must not use D3D on a locate thread.
    float vrsWidth=.5f,vrsHeight=.5f;
    lmuqv::VrsFocus vrsFocus[2];
    XrPosef vrsGazePose{};bool vrsTracked=false;
    uint32_t vrsWidths[2]{},vrsHeights[2]{};
    bool captures[2]{};
    bool sceneFocus[2]{};
    bool debugFocus=false;
    float outerBlur=0;
    unsigned blurEncodingWarnings=0;
    bool debugDrawn[4]{};
    bool waitPending=false, waitShouldRender=false;
    bool earlyBeginPending=false; // Runtime begin performed for LMU before its late app call.
    XrResult earlyBeginResult=XR_SUCCESS;
    XrTime pendingTime{};
    XrDuration pendingPeriod{},framePeriod{};
    bool engineGpuTimingArmed=false;
    XrTime predictedTime{}, locateTime{};
    XrSpace locateSpace{};
    XrView views[4]{};
    XrFovf fullOuterFov[2]{};
    ID3D11Device* device{};
    ID3D11DeviceContext* context{};
    Outer outer[2];
    XrSwapchainCreateInfo colorInfo{};
    std::unordered_map<XrSwapchain,AppChain> appChains;
    unsigned nextAppEye=0;
    bool runtimeSettings=false,runtimeSizesReady=false,rawFovLogged=false;
    bool sampledInputs=false;
    bool focusOwnership=false,focusBeginInFlight=false; // No production renderer enables this yet.
    bool submitInFlight=false; // Pins submitted frame resources while EndFrame is unlocked.
    unsigned appImageOperations=0; // Pins session/map/borrowed textures during unlocked image calls.
    bool requestEyeTracking=true;
    bool eyeTrackingActive=false,foveationResolved=false;
    XrSystemId foveationSystem=XR_NULL_SYSTEM_ID;
    uint32_t runtimeFocusWidth=0,runtimeFocusHeight=0;
    uint32_t runtimeOuterWidth[2]{},runtimeOuterHeight[2]{};
    uint32_t runtimeOutputWidth[2]{},runtimeOutputHeight[2]{};
    lmuqv::XrOutputSizes outputSizes;
    lmuqv::FocusBoxSize focusBox;
    lmuqv::FocusOffset focusOffsetX,focusOffsetY;
    lmuqv::CropPercent cropWidth,cropHeight;
    lmuqv::CropEdges cropEdges;
    uint64_t hostProcessed[4]{};
    bool pixelEnabled=false,pixelDone=false,pixelRequireScene=false;
    uint64_t pixelFrame=0;
    unsigned pixelMask=0,pixelSuccessMask=0;
    lmuqv::capture_detail::Config capture;
    unsigned captureMask=0;
    uint64_t frames=0;
} s;
#ifdef OPENGRIDXR_LMU_INTERNAL
og::lmu::Frame hostFrame() {
    og::lmu::Frame frame;
    frame.id=s.frames;frame.time=s.locateTime;frame.space=s.locateSpace;
    std::copy_n(s.views,4,frame.views);std::copy_n(s.fullOuterFov,2,frame.fullOuterFov);
    for(unsigned eye=0;eye<2;++eye){
        frame.widths[eye]=s.outer[eye].width;frame.heights[eye]=s.outer[eye].height;
        frame.widths[eye+2]=s.colorInfo.width;frame.heights[eye+2]=s.colorInfo.height;
    }
    return frame;
}
void hostImage(unsigned slot,ID3D11Texture2D* texture) {
    if(!og::lmu::settings.quad||!og::lmu::host.image||slot>=4||!texture||!s.frame||
       !s.renderViewsLocked||!s.located||!s.captures[slot%2]||s.hostProcessed[slot]==s.frames||
       (s.colorInfo.createFlags&XR_SWAPCHAIN_CREATE_PROTECTED_CONTENT_BIT))return;
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    const auto frame=hostFrame();
    if(desc.Width!=frame.widths[slot]||desc.Height!=frame.heights[slot]||desc.ArraySize!=1||desc.SampleDesc.Count!=1)return;
    // Do not repeat non-idempotent grading if a downstream release is retried.
    s.hostProcessed[slot]=s.frames;
    og::lmu::host.image(s.session,frame,slot,texture,static_cast<DXGI_FORMAT>(s.colorInfo.format),{0,0,0,desc.Width,desc.Height});
}
#endif
bool frameOperationInFlight(bool includeAppImages=true){return (includeAppImages&&s.appImageOperations)||s.locateInFlight||s.trackingReaders||s.focusBeginInFlight||s.submitInFlight||s.outer[0].runtimeBusy||s.outer[1].runtimeBusy;}
bool graphicsOperationInFlight(bool includeAppImages=true){return (includeAppImages&&s.appImageOperations)||s.focusBeginInFlight||s.submitInFlight||s.outer[0].runtimeBusy||s.outer[1].runtimeBusy;}
// Tracking-only readers do not own graphics resources. A normal late Locate
// also cannot overwrite a snapshot which the renderer has already locked.
bool renderingOperationInFlight(){return graphicsOperationInFlight()||(s.locateInFlight&&!s.renderViewsLocked);}
// Independent application chains may advance together. Destruction, frame
// mutation and borrowed-image users still see the combined lifetime reservation.
bool reserveAppImageOperation(XrSwapchain chain){
    if(graphicsOperationInFlight(false))return false;
    const auto found=s.appChains.find(chain);
    if(found!=s.appChains.end()){
        if(found->second.runtimeBusy)return false;
        found->second.runtimeBusy=true;
    }else if(s.appImageOperations)return false; // Forward an unknown handle once for runtime validation.
    ++s.appImageOperations;return true;
}
void retireAppImageOperation(XrSwapchain chain){
    const auto found=s.appChains.find(chain);if(found!=s.appChains.end())found->second.runtimeBusy=false;
    --s.appImageOperations;
}
std::atomic<bool> quadActive{false},vrsActive{false};
// Called under stateMutex when lifecycle state changes. These are coarse mode
// snapshots, not ownership of the session or any of its graphics resources.
void publishActiveModes(){
    const bool running=s.session&&s.running;
    quadActive.store(s.quad&&running,std::memory_order_release);
    vrsActive.store(s.vrs&&running,std::memory_order_release);
}
lmuqv::diagnostic_logging::FrameSampling diagnosticFrameSampling;
bool xrDiagnosticFrameLog(){
    const bool enabled=lmuqv::diagnostics_enabled();
    const bool periodic=diagnosticFrameSampling.sample(s.frames,enabled,[]{return GetTickCount64();});
    lmuqv::diagnostic_logging::publish_frame(periodic);
    return enabled&&(s.frames<lmuqv::diagnostic_logging::FrameSampling::initial_frames||periodic);
}
lmuqv::benchmark::Transport benchmarkTransport;
lmuqv::benchmark::Overlay benchmarkOverlay;
lmuqv::benchmark::GpuSpan<> benchmarkGpu;
// Profiling can start at an engine hook after BeginFrame. Reserve its GPU
// timing while armed, so disjoint queries never overlap in the same context.
bool frameGpuSamplingAllowed(){return !s.engineGpuTimingArmed&&!lmuqv::renderProfile.enabled();}
bool benchmarkEnabled=false,benchmarkOverlayReady=false,benchmarkOverlayCapacityFailure=false;
uint32_t benchmarkLayerLimit=0;
int64_t benchmarkBeginQpc=0,benchmarkPreviousSceneQpc=0;
void benchmarkReset(){benchmarkGpu.reset();benchmarkOverlay.reset();benchmarkTransport.reset();benchmarkEnabled=benchmarkOverlayReady=benchmarkOverlayCapacityFailure=false;benchmarkBeginQpc=benchmarkPreviousSceneQpc=0;benchmarkLayerLimit=0;}
void benchmarkInitialize(){
    benchmarkReset();char setting[8]{};
    if(!lmuqv::diagnostics_enabled()||GetEnvironmentVariableA("LMU_QV_BENCHMARK",setting,sizeof(setting))!=1||setting[0]!='1')return;
    benchmarkEnabled=benchmarkTransport.open();if(!benchmarkEnabled){logf("benchmark unavailable reason=mapping_failed error=%lu",GetLastError());return;}
    auto systemProperties=structure<XrSystemProperties>(XR_TYPE_SYSTEM_PROPERTIES);
    if(next.GetSystemProperties&&XR_SUCCEEDED(next.GetSystemProperties(s.instance,s.system,&systemProperties)))benchmarkLayerLimit=systemProperties.graphicsProperties.maxLayerCount;
    benchmarkOverlayReady=benchmarkOverlay.initialize(next.get,s.instance,s.session);
    if(!benchmarkOverlayReady){benchmarkTransport.data()->header.status=3;benchmarkTransport.data()->header.reason=4;logf("benchmark overlay unavailable reason=overlay_creation result=%d (sampling active)",benchmarkOverlay.result());}
}
void benchmarkRuntimeResult(XrResult result){
    if(!benchmarkEnabled)return;auto& h=benchmarkTransport.data()->header;h.result=result;h.running=s.running?1:0;
    if(XR_FAILED(result)){h.status=4;h.reason=0;}
}
void benchmarkPublish(unsigned submitted,int reason,int64_t preSubmit,int64_t postSubmit,XrResult result){
    if(!benchmarkEnabled)return;auto& data=*benchmarkTransport.data();auto& h=data.header;
    lmuqv::benchmark::Sample sample{};sample.qpc=postSubmit;sample.frame=s.frames;
    const bool scene=s.sceneFocus[0]&&s.sceneFocus[1];double scale=1000.0/double(h.frequency);
    lmuqv::benchmark::FocusTelemetry focus;
    focus.frame=s.frames;focus.scale=100;
    if(s.quad&&scene&&s.shouldRender&&submitted&&XR_SUCCEEDED(result)){
        constexpr float degrees=57.29577951308232f;
        focus.leftHorizontal=(s.views[2].fov.angleRight-s.views[2].fov.angleLeft)*degrees;
        focus.leftVertical=(s.views[2].fov.angleUp-s.views[2].fov.angleDown)*degrees;
        focus.rightHorizontal=(s.views[3].fov.angleRight-s.views[3].fov.angleLeft)*degrees;
        focus.rightVertical=(s.views[3].fov.angleUp-s.views[3].fov.angleDown)*degrees;
        const float extents[]{focus.leftHorizontal,focus.leftVertical,focus.rightHorizontal,focus.rightVertical};
        if(std::all_of(std::begin(extents),std::end(extents),[](float v){return std::isfinite(v)&&v>0&&v<180;}))focus.flags|=1;
    }
    lmuqv::benchmark::publish_focus(data,focus);
    sample.intervalMs=scene&&benchmarkPreviousSceneQpc?double(benchmarkBeginQpc-benchmarkPreviousSceneQpc)*scale:lmuqv::benchmark::missing();
    sample.appMs=benchmarkBeginQpc?double(preSubmit-benchmarkBeginQpc)*scale:lmuqv::benchmark::missing();sample.providerMs=double(postSubmit-preSubmit)*scale;
    sample.gpuMs=lmuqv::benchmark::missing();bool gpu=false;
    gpu=benchmarkGpu.poll(sample.gpuMs,sample.gpuFrame);
    sample.flags=(scene?1u:0u)|(s.shouldRender?2u:0u)|(submitted?4u:0u)|(gpu?8u:0u);sample.result=result;
    benchmarkPreviousSceneQpc=scene&&XR_SUCCEEDED(result)?benchmarkBeginQpc:0;
    h.running=s.running?1:0;h.result=result;h.projections=int32_t(submitted);h.gpuFailures=benchmarkGpu.failures;
    h.outer0Width=s.outer[0].width;h.outer0Height=s.outer[0].height;h.outer1Width=s.outer[1].width;h.outer1Height=s.outer[1].height;h.focusWidth=s.colorInfo.width;h.focusHeight=s.colorInfo.height;
    if(s.vrs){h.outer0Width=s.vrsWidths[0];h.outer0Height=s.vrsHeights[0];h.outer1Width=s.vrsWidths[1];h.outer1Height=s.vrsHeights[1];h.focusWidth=h.focusHeight=0;}
    const bool overlayFailure=!benchmarkOverlayReady||benchmarkOverlayCapacityFailure||benchmarkOverlay.result()!=XR_SUCCESS;
    h.status=XR_FAILED(result)?4:!s.shouldRender?1:!submitted?2:overlayFailure?3:0;
    h.reason=XR_FAILED(result)?reason:!s.shouldRender?0:reason?reason:overlayFailure?4:0;lmuqv::benchmark::publish(data,sample);
}
uint64_t focusLeaseSerial=0; // Never reset with session/frame state.
bool trackAppImages(){
#ifdef OPENGRIDXR_LMU_INTERNAL
    if(s.quad&&og::lmu::settings.enabled)return true;
#endif
    return s.pixelEnabled||s.debugFocus||s.focusOwnership;
}
bool heldFocusImages(){for(const auto& pair:s.appChains)if(pair.second.focusLease)return true;return false;}
void clearFrame() {
    s.frame=false;s.shouldRender=false;s.located=false;s.captures[0]=s.captures[1]=false;
    s.earlyBeginPending=false;
    s.renderViewsLocked=false;
    s.renderPreparationPending=false;
    s.sceneFocus[0]=s.sceneFocus[1]=false;
    std::fill(std::begin(s.debugDrawn),std::end(s.debugDrawn),false);
}
void clearFocusRtvs(AppChain& app){for(auto* rtv:app.focusRtvs)if(rtv)rtv->Release();app.focusRtvs.clear();}
bool sameFocusPose(unsigned eye){
    const auto& a=s.views[eye].pose;const auto& b=s.views[eye+2].pose;
    float sign=a.orientation.x*b.orientation.x+a.orientation.y*b.orientation.y+a.orientation.z*b.orientation.z+a.orientation.w*b.orientation.w<0?-1.f:1.f;
    return std::abs(a.position.x-b.position.x)<1e-5f&&std::abs(a.position.y-b.position.y)<1e-5f&&std::abs(a.position.z-b.position.z)<1e-5f&&
           std::abs(a.orientation.x-sign*b.orientation.x)<1e-5f&&std::abs(a.orientation.y-sign*b.orientation.y)<1e-5f&&
           std::abs(a.orientation.z-sign*b.orientation.z)<1e-5f&&std::abs(a.orientation.w-sign*b.orientation.w)<1e-5f;
}
void drawFocusDebug(unsigned eye,bool inner,ID3D11RenderTargetView* target){
    unsigned slot=eye+(inner?2:0);
    if(eye>1||!target||!s.debugFocus||!s.quad||!s.frame||!s.shouldRender||!s.located||!s.captures[eye]||s.locateTime!=s.predictedTime||s.debugDrawn[slot]||!sameFocusPose(eye))return;
    auto angles=[](XrFovf f){return lmuqv::FocusFov{f.angleLeft,f.angleRight,f.angleUp,f.angleDown};};
    bool drawn=lmuqv::draw_focus_border(s.context,target,angles(s.views[eye+(inner?2:0)].fov),angles(s.views[eye+2].fov),true);
    if(drawn)s.debugDrawn[slot]=true;
    if(xrDiagnosticFrameLog())logf("debug_focus frame=%llu eye=%u target=%s drawn=%d (runtime focus FOV marker)",static_cast<unsigned long long>(s.frames),eye,inner?"inner":"outer",drawn);
}
void drawAppFocusDebug(AppChain& app){
    if(!s.debugFocus||app.eye>1||app.acquired.empty()||!app.acquired.front().waited||app.acquired.front().index>=app.images.size())return;
    if(app.info.arraySize!=1||app.info.sampleCount!=1||app.info.mipCount!=1)return;
    if(app.focusRtvs.size()!=app.images.size()){clearFocusRtvs(app);app.focusRtvs.resize(app.images.size(),nullptr);}
    unsigned index=app.acquired.front().index;
    if(!app.focusRtvs[index]){
        HRESULT hr=lmuqv::focus_detail::create_rtv(s.device,app.images[index],static_cast<DXGI_FORMAT>(app.info.format),&app.focusRtvs[index]);
        if(FAILED(hr)){if(xrDiagnosticFrameLog())logf("debug_focus inner RTV failed eye=%u result=%08lx",app.eye,static_cast<unsigned long>(hr));return;}
    }
    drawFocusDebug(app.eye,true,app.focusRtvs[index]);
}
bool readPixelStats(ID3D11Texture2D* texture,const char* slot,unsigned slotIndex) {
    if(!texture||!s.device||!s.context)return false;
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    bool bgra=desc.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS||desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM||desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    bool rgba=desc.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS||desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM||desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    if((!rgba&&!bgra)||desc.SampleDesc.Count!=1||desc.ArraySize!=1||desc.MipLevels!=1||!desc.Width||!desc.Height){
        logf("pixel_stats_error slot=%s unsupported texture=%ux%u format=%u samples=%u array=%u mips=%u",slot,desc.Width,desc.Height,desc.Format,desc.SampleDesc.Count,desc.ArraySize,desc.MipLevels);return false;
    }
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ID3D11Texture2D* staging=nullptr;HRESULT hr=s.device->CreateTexture2D(&desc,nullptr,&staging);
    if(FAILED(hr)){logf("pixel_stats_error slot=%s createStaging=%08lx",slot,static_cast<unsigned long>(hr));return false;}
    s.context->CopyResource(staging,texture);
    D3D11_MAPPED_SUBRESOURCE map{};hr=s.context->Map(staging,0,D3D11_MAP_READ,0,&map);
    if(FAILED(hr)){logf("pixel_stats_error slot=%s map=%08lx",slot,static_cast<unsigned long>(hr));staging->Release();return false;}
    if(!map.pData||map.RowPitch<uint64_t(desc.Width)*4){s.context->Unmap(staging,0);staging->Release();return false;}
    uint64_t hash=14695981039346656037ull,nonzero=0;
    unsigned first[4]{};
    const auto* data=static_cast<const unsigned char*>(map.pData);
    for(unsigned component=0;component<4;++component)first[component]=data[bgra&&component<3?2-component:component];
    for(uint32_t y=0;y<desc.Height;++y){
        const auto* row=data+size_t(y)*map.RowPitch;
        for(uint32_t x=0;x<desc.Width;++x){
            const auto* pixel=row+size_t(x)*4;nonzero+=(pixel[0]|pixel[1]|pixel[2])!=0;
            for(unsigned component=0;component<4;++component){hash^=pixel[component];hash*=1099511628211ull;}
        }
    }
    if(s.capture.requested){
        DWORD error=0;
        if(lmuqv::capture_detail::write_bmp(s.capture,slotIndex,desc.Width,desc.Height,map.RowPitch,data,bgra,error)){
            s.captureMask|=1u<<slotIndex;
            auto path=s.capture.directory/(std::string(slot)+".bmp");
            logf("capture_bmp frame=%llu slot=%s path=%s bytes=%llu (offscreen texture before release)",static_cast<unsigned long long>(s.pixelFrame),slot,path.u8string().c_str(),static_cast<unsigned long long>(122+uint64_t(desc.Width)*desc.Height*4));
        }else logf("capture_bmp_error frame=%llu slot=%s error=%lu (no overwrite; partial file retained if write failed)",static_cast<unsigned long long>(s.pixelFrame),slot,error);
    }
    s.context->Unmap(staging,0);staging->Release();
    logf("pixel_stats frame=%llu slot=%s width=%u height=%u nonzeroRGB=%llu pixels=%llu hash=%016llx firstRGBA=%u,%u,%u,%u resourceFormat=%u",static_cast<unsigned long long>(s.pixelFrame),slot,desc.Width,desc.Height,static_cast<unsigned long long>(nonzero),static_cast<unsigned long long>(uint64_t(desc.Width)*desc.Height),static_cast<unsigned long long>(hash),first[0],first[1],first[2],first[3],desc.Format);
    return true;
}
void diagnosePixelSlot(unsigned slot,ID3D11Texture2D* texture) {
    if(!s.pixelEnabled||s.pixelDone||s.pixelFrame!=s.frames||!s.frame||!s.shouldRender||slot>=4||(s.pixelMask&(1u<<slot))||
       (s.pixelRequireScene&&(!s.sceneFocus[0]||!s.sceneFocus[1])))return;
    static const char* names[]={"outer0","outer1","app0","app1"};
    s.pixelMask|=1u<<slot;
    if(readPixelStats(texture,names[slot],slot))s.pixelSuccessMask|=1u<<slot;
}
// Caller owns the one outer-operation reservation while the runtime is free
// to block. Lifecycle/Begin/End entry points reject work until it completes.
bool acquireOuterImage(unsigned eye,std::unique_lock<std::recursive_mutex>& lock){
    auto& outer=s.outer[eye];
    if(outer.runtimeBusy||!outer.chain||outer.images.empty())return false;
    if(outer.acquired&&outer.waited)return true;
    const auto chain=outer.chain;
    bool acquired=outer.acquired,waited=outer.waited;uint32_t index=outer.index;
    outer.runtimeBusy=true;
    lock.unlock();
    XrResult result=XR_SUCCESS;
    if(!acquired){
        auto info=structure<XrSwapchainImageAcquireInfo>(XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO);
        result=runtimeCall([&]{return next.AcquireSwapchainImage(chain,&info,&index);});
        acquired=XR_SUCCEEDED(result);
    }
    if(acquired&&!waited){
        auto info=structure<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);
        // Prefetch/copy may run from Locate on late-Begin runtimes. Never ask
        // the runtime to wait indefinitely in the tracking path. A timeout
        // keeps this exact acquired index for a later wait/release attempt.
        info.timeout=s.framePeriod>0?(std::min)(s.framePeriod,XrDuration(5000000)):XrDuration(5000000);
        result=runtimeCall([&]{return next.WaitSwapchainImage(chain,&info);});waited=result==XR_SUCCESS;
#ifdef OPENGRIDXR_LMU_INTERNAL
        if(result==XR_TIMEOUT_EXPIRED)og::lmu::note_outer_timeout();
#endif
    }
    lock.lock();
    outer.index=index;outer.acquired=acquired;outer.waited=waited;outer.runtimeBusy=false;
    if(!acquired||!waited)logf("outer acquire/wait eye=%u acquired=%d waited=%d result=%d (ownership retained)",eye,acquired,waited,result);
    return acquired&&waited;
}
bool releaseOuters(std::unique_lock<std::recursive_mutex>& lock) {
    bool okay=true;
    for(unsigned eye=0;eye<2;++eye) {
        auto& outer=s.outer[eye];
        if(!outer.acquired)continue;
        if(outer.runtimeBusy){okay=false;continue;}
        outer.runtimeBusy=true;
        if(!outer.waited){
            auto wait=structure<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);wait.timeout=0;
            lock.unlock();
            XrResult r=runtimeCall([&]{return next.WaitSwapchainImage(outer.chain,&wait);});
            lock.lock();
            if(r!=XR_SUCCESS){outer.runtimeBusy=false;logf("outer cleanup wait eye=%u result=%d (acquisition retained)",eye,r);okay=false;continue;}
            outer.waited=true;
        }
        if(outer.index<outer.rtvs.size())drawFocusDebug(eye,false,outer.croppedRtv?outer.croppedRtv:outer.rtvs[outer.index]);
#ifdef OPENGRIDXR_LMU_INTERNAL
        if(outer.index<outer.images.size())hostImage(eye,outer.croppedTexture?outer.croppedTexture:outer.images[outer.index].texture);
#endif
        if(outer.croppedTexture&&s.captures[eye]){
            std::array<float,4> bounds{};
            bool padded=s.frame&&s.located&&outer.index<outer.rtvs.size()&&
                lmuqv::crop_bounds(s.fullOuterFov[eye],s.views[eye].fov,bounds);
            if(padded){
                const float w=float(outer.outputWidth),h=float(outer.outputHeight);
                const D3D11_VIEWPORT viewport{bounds[0]*w,bounds[1]*h,
                    bounds[2]*w-bounds[0]*w,bounds[3]*h-bounds[1]*h,0,1};
                padded=cropResample.draw(s.context,outer.croppedTexture,outer.rtvs[outer.index],
                    outer.outputWidth,outer.outputHeight,0,&viewport);
            }
            if(!padded){logf("outer crop embedding failed eye=%u; suppressing projection",eye);s.captures[eye]=false;okay=false;}
        }
        if(outer.index<outer.images.size())diagnosePixelSlot(eye,outer.images[outer.index].texture);
        auto release=structure<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);
        lock.unlock();
        XrResult r=runtimeCall([&]{return next.ReleaseSwapchainImage(outer.chain,&release);});
        lock.lock();outer.runtimeBusy=false;
        if(XR_SUCCEEDED(r)){outer.acquired=false;outer.waited=false;}
        else {logf("release outer %u result=%d",eye,r);okay=false;}
    }
    return okay;
}
void destroyOuter(std::unique_lock<std::recursive_mutex>& lock) {
    releaseOuters(lock);
    for(auto& eye:s.outer) {
        for(auto* rtv:eye.rtvs) if(rtv)rtv->Release();
        if(eye.croppedRtv)eye.croppedRtv->Release();
        if(eye.croppedTexture)eye.croppedTexture->Release();
        if(eye.chain) {eye.runtimeBusy=true;lock.unlock();XrResult r=next.DestroySwapchain(eye.chain);lock.lock();logf("destroy outer swapchain result=%d acquired=%d",r,eye.acquired);}
        eye=Outer{};
    }
    s.colorInfo={};
}
void clearSession(std::unique_lock<std::recursive_mutex>& lock) {
    vrsGaze.resetSession();
    benchmarkReset();
    for(auto& entry:s.appChains)clearFocusRtvs(entry.second);
    destroyOuter(lock);
    outerResample.reset();cropResample.reset();
    if(s.context) s.context->Release();
    if(s.device) s.device->Release();
    s.context=nullptr;s.device=nullptr;s.session=XR_NULL_HANDLE;s.running=false;
    publishActiveModes();
    s.appChains.clear();s.nextAppEye=0;s.waitPending=false;s.focusOwnership=s.focusBeginInFlight=false;clearFrame();
}
XrResult selectFoveation(XrInstance instance,XrSystemId system){
    if(s.foveationResolved&&s.foveationSystem==system)return XR_SUCCESS;
    bool supported=false;
    if(s.requestEyeTracking){
        auto properties=structure<XrSystemProperties>(XR_TYPE_SYSTEM_PROPERTIES);
        auto foveated=structure<XrSystemFoveatedRenderingPropertiesVARJO>(XR_TYPE_SYSTEM_FOVEATED_RENDERING_PROPERTIES_VARJO);
        properties.next=&foveated;
        const XrResult result=next.GetSystemProperties(instance,system,&properties);
        if(XR_FAILED(result)){logf("Foveation capability query failed result=%d",result);return result;}
        supported=foveated.supportsFoveatedRendering==XR_TRUE;
    }
    s.eyeTrackingActive=s.requestEyeTracking&&supported;s.foveationSystem=system;s.foveationResolved=true;
    logf("Foveation selected=%s requested=%s (provider capability; not proof of live gaze)",
        s.eyeTrackingActive?"eye-tracked":"fixed",s.requestEyeTracking?"auto":"fixed");
    return XR_SUCCESS;
}
XrResult queryQuad(XrInstance instance,XrSystemId system,std::array<XrViewConfigurationView,4>& out) {
    const XrResult selection=selectFoveation(instance,system);if(XR_FAILED(selection))return selection;
    std::array<XrFoveatedViewConfigurationViewVARJO,4> foveated{};
    for(unsigned i=0;i<4;++i) {
        out[i]=structure<XrViewConfigurationView>(XR_TYPE_VIEW_CONFIGURATION_VIEW);
        foveated[i]=structure<XrFoveatedViewConfigurationViewVARJO>(XR_TYPE_FOVEATED_VIEW_CONFIGURATION_VIEW_VARJO);
        foveated[i].foveatedRenderingActive=s.eyeTrackingActive?XR_TRUE:XR_FALSE;
        out[i].next=&foveated[i];
    }
    uint32_t count=0;
    XrResult r=next.EnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO,4,&count,out.data());
    for(auto& v:out) v.next=nullptr;
    if(XR_FAILED(r)||count!=4){
        logf("Quad view recommendation query failed result=%d count=%u expected=4 eyeTracked=%d; check provider/runtime startup log",r,count,s.eyeTrackingActive);
        return XR_FAILED(r)?r:XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    }
    return XR_SUCCESS;
}
XrResult selectRuntimeSizes(const std::array<XrViewConfigurationView,4>& runtime){
    const uint32_t width=(std::max)(runtime[2].recommendedImageRectWidth,runtime[3].recommendedImageRectWidth);
    const uint32_t height=(std::max)(runtime[2].recommendedImageRectHeight,runtime[3].recommendedImageRectHeight);
    logf("runtime recommendations outer0=%ux%u outer1=%ux%u focus0=%ux%u focus1=%ux%u",runtime[0].recommendedImageRectWidth,runtime[0].recommendedImageRectHeight,runtime[1].recommendedImageRectWidth,runtime[1].recommendedImageRectHeight,runtime[2].recommendedImageRectWidth,runtime[2].recommendedImageRectHeight,runtime[3].recommendedImageRectWidth,runtime[3].recommendedImageRectHeight);
    bool valid=width&&height&&width<=D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION&&height<=D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    for(unsigned eye=0;eye<2;++eye){
        const auto& outer=runtime[eye];const auto& focus=runtime[eye+2];
        valid=valid&&outer.recommendedImageRectWidth&&outer.recommendedImageRectHeight&&
            outer.recommendedImageRectWidth<=width&&outer.recommendedImageRectHeight<=height&&
            outer.recommendedImageRectWidth<=outer.maxImageRectWidth&&outer.recommendedImageRectHeight<=outer.maxImageRectHeight&&
            width<=focus.maxImageRectWidth&&height<=focus.maxImageRectHeight;
    }
    if(!valid){logf("runtime sizing incompatible: outer recommendations must be nonzero and <= common focus %ux%u on EACH axis and within runtime/device limits; refusing session/hooks",width,height);return XR_ERROR_LIMIT_REACHED;}
    if(s.session&&s.runtimeSizesReady){
        bool same=width==s.runtimeFocusWidth&&height==s.runtimeFocusHeight;
        for(unsigned eye=0;eye<2;++eye)same=same&&runtime[eye].recommendedImageRectWidth==s.runtimeOutputWidth[eye]&&runtime[eye].recommendedImageRectHeight==s.runtimeOutputHeight[eye];
        if(!same){logf("Runtime recommendations changed during session; restart required");return XR_ERROR_RUNTIME_FAILURE;}
    }
    s.runtimeFocusWidth=width;s.runtimeFocusHeight=height;
    for(unsigned eye=0;eye<2;++eye){
        s.runtimeOutputWidth[eye]=runtime[eye].recommendedImageRectWidth;s.runtimeOutputHeight[eye]=runtime[eye].recommendedImageRectHeight;
        s.runtimeOuterWidth[eye]=lmuqv::crop_dimension(s.runtimeOutputWidth[eye],s.cropWidth.percent);
        s.runtimeOuterHeight[eye]=lmuqv::crop_dimension(s.runtimeOutputHeight[eye],s.cropHeight.percent);
    }
    s.runtimeSizesReady=true;
    logf("Runtime selected outer0=%ux%u outer1=%ux%u common focus=%ux%u; crop=%g/%g%%, focus resolution unchanged",s.runtimeOuterWidth[0],s.runtimeOuterHeight[0],s.runtimeOuterWidth[1],s.runtimeOuterHeight[1],width,height,s.cropWidth.percent,s.cropHeight.percent);
    return XR_SUCCESS;
}
XrResult createInputSwapchain(XrSession session,const XrSwapchainCreateInfo& info,XrSwapchain* chain,const char* role){
    lmuqv::SamplingAttempt attempt;
    const auto result=lmuqv::create_sampled_input(next.CreateSwapchain,session,info,chain,
        s.quad&&s.sampledInputs&&s.device,attempt);
    if(attempt.requested)logf("sampled_input role=%s size=%ux%u accepted=%d retry_original=%d sampled_result=%d result=%d original_usage=%llu requested_usage=%llu",
        role,info.width,info.height,attempt.accepted,attempt.retried,attempt.sampledResult,result,
        static_cast<unsigned long long>(info.usageFlags),static_cast<unsigned long long>(info.usageFlags|XR_SWAPCHAIN_USAGE_SAMPLED_BIT));
    return result;
}
bool createOuter(const XrSwapchainCreateInfo& info,std::unique_lock<std::recursive_mutex>& lock) {
    if(s.outer[0].chain && s.outer[1].chain) return true;
    if(!s.device || info.sampleCount!=1 || info.arraySize!=1 || info.faceCount!=1 || info.mipCount!=1 ||
       info.createFlags || !(info.usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT)) {
        logf("Unsupported app color swapchain for outer images: %ux%u samples=%u array=%u mips=%u flags=%llu",info.width,info.height,info.sampleCount,info.arraySize,info.mipCount,static_cast<unsigned long long>(info.createFlags));
        return false;
    }
    XrSwapchainCreateInfo own=info;own.next=nullptr;
    own.width=lmuqv::outer_dimension(info.width,s.outputSizes);own.height=lmuqv::outer_dimension(info.height,s.outputSizes);
    if(!own.width||!own.height)return false;
    own.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    for(unsigned i=0;i<2;++i) {
        auto& eye=s.outer[i];
        if(s.runtimeSettings){own.width=s.runtimeOutputWidth[i];own.height=s.runtimeOutputHeight[i];}
        XrResult r=createInputSwapchain(s.session,own,&eye.chain,i?"outer1":"outer0");
        if(XR_FAILED(r)) { logf("create outer %u result=%d",i,r);destroyOuter(lock);return false; }
        eye.outputWidth=own.width;eye.outputHeight=own.height;
        eye.width=lmuqv::crop_dimension(own.width,s.cropWidth.percent);eye.height=lmuqv::crop_dimension(own.height,s.cropHeight.percent);
        uint32_t count=0;r=next.EnumerateSwapchainImages(eye.chain,0,&count,nullptr);
        if(XR_FAILED(r)||!count||count>64) {logf("outer image count %u result=%d count=%u",i,r,count);destroyOuter(lock);return false;}
        eye.images.resize(count);
        for(auto& image:eye.images) image=structure<XrSwapchainImageD3D11KHR>(XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR);
        r=next.EnumerateSwapchainImages(eye.chain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(eye.images.data()));
        if(XR_FAILED(r)||count>eye.images.size()) {logf("enumerate outer %u result=%d",i,r);destroyOuter(lock);return false;}
        eye.images.resize(count);
        eye.rtvs.resize(count,nullptr);
        D3D11_RENDER_TARGET_VIEW_DESC desc{};
        const auto swapchainFormat=static_cast<DXGI_FORMAT>(info.format);
        // LMU's HUD is drawn through an UNORM RTV before its SRGB swapchain copy.
        // Prefer the same linear RTV on typeless runtime images to avoid encoding UI twice.
        auto uiFormat=swapchainFormat;
        if(uiFormat==DXGI_FORMAT_R8G8B8A8_TYPELESS||uiFormat==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)uiFormat=DXGI_FORMAT_R8G8B8A8_UNORM;
        if(uiFormat==DXGI_FORMAT_B8G8R8A8_TYPELESS||uiFormat==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)uiFormat=DXGI_FORMAT_B8G8R8A8_UNORM;
        if(uiFormat==DXGI_FORMAT_B8G8R8X8_TYPELESS||uiFormat==DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)uiFormat=DXGI_FORMAT_B8G8R8X8_UNORM;
        desc.Format=uiFormat;desc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        for(uint32_t image=0;image<count;++image){
            HRESULT hr=eye.images[image].texture?s.device->CreateRenderTargetView(eye.images[image].texture,&desc,&eye.rtvs[image]):E_POINTER;
            if(FAILED(hr)&&eye.images[image].texture&&uiFormat!=swapchainFormat){
                logf("outer linear RTV unsupported eye=%u image=%u format=%u result=%08lx; fallback swapchain RTV format=%u (UI gamma may differ)",i,image,uiFormat,static_cast<unsigned long>(hr),swapchainFormat);
                desc.Format=swapchainFormat;
                hr=s.device->CreateRenderTargetView(eye.images[image].texture,&desc,&eye.rtvs[image]);
            }
            if(FAILED(hr)){logf("outer RTV eye=%u image=%u result=%08lx",i,image,static_cast<unsigned long>(hr));destroyOuter(lock);return false;}
            D3D11_TEXTURE2D_DESC resource{};eye.images[image].texture->GetDesc(&resource);
            logf("outer RTV eye=%u image=%u resourceFormat=%u swapchainFormat=%u uiFormat=%u",i,image,resource.Format,swapchainFormat,desc.Format);
            desc.Format=uiFormat;
        }
        if(s.cropWidth.percent!=100||s.cropHeight.percent!=100){
            D3D11_TEXTURE2D_DESC cropped{};eye.images[0].texture->GetDesc(&cropped);
            cropped.Width=eye.width;cropped.Height=eye.height;cropped.Usage=D3D11_USAGE_DEFAULT;
            cropped.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;cropped.CPUAccessFlags=0;cropped.MiscFlags=0;
            HRESULT hr=s.device->CreateTexture2D(&cropped,nullptr,&eye.croppedTexture);
            eye.rtvs[0]->GetDesc(&desc);
            if(SUCCEEDED(hr))hr=s.device->CreateRenderTargetView(eye.croppedTexture,&desc,&eye.croppedRtv);
            if(FAILED(hr)){logf("outer crop target failed eye=%u result=%08lx",i,static_cast<unsigned long>(hr));destroyOuter(lock);return false;}
            logf("outer crop target eye=%u render=%ux%u fullFovOutput=%ux%u",i,eye.width,eye.height,eye.outputWidth,eye.outputHeight);
        }
    }
    s.colorInfo=info;s.colorInfo.next=nullptr; // App/source validation uses focus dimensions, not outer output dimensions.
    if(!s.runtimeSettings)logf("Created two outer swapchains format=%lld outer=%ux%u appFocus=%ux%u outerPixelsPercent=%g focusPixelsPercent=%g",static_cast<long long>(info.format),own.width,own.height,info.width,info.height,s.outputSizes.outerPercent,s.outputSizes.focusPercent);
    if(s.runtimeSettings)logf("Actual swapchains outer0=%ux%u outer1=%ux%u focus=%ux%u",s.outer[0].width,s.outer[0].height,s.outer[1].width,s.outer[1].height,info.width,info.height);
    return true;
}
// CopyResource permits different views in the same DXGI typeless family.
unsigned formatFamily(DXGI_FORMAT f) {
    unsigned v=unsigned(f);
    if(v>=27&&v<=32) return 27; // R8G8B8A8
    if(v==87||v==90||v==91) return 87; // B8G8R8A8
    if(v==88||v==92||v==93) return 88; // B8G8R8X8
    if(v>=9&&v<=14) return 9; // R16G16B16A16
    if(v>=23&&v<=25) return 23;
    if(v>=1&&v<=4) return 1;
    return v;
}

XrResult XRAPI_CALL layerDestroyInstance(XrInstance instance) {
    std::unique_lock<std::recursive_mutex> lock(stateMutex);
    if(frameOperationInFlight())return XR_ERROR_CALL_ORDER_INVALID;
    logf("xrDestroyInstance");
    if(s.session) {lmuqv::engine_stop();clearSession(lock);}
    lmuqv::stereo_audit_stop(); // Also releases early hooks when no session was created.
    lock.unlock();
    XrResult r=next.DestroyInstance(instance);
    if(XR_SUCCEEDED(r)){
        {std::lock_guard<std::recursive_mutex> resetLock(stateMutex);s=State{};publishActiveModes();}
        lmuqv::stop_log(); // Drain only at explicit teardown, never under stateMutex.
    }else lmuqv::flush_log();
    return r;
}
XrResult XRAPI_CALL layerEnumerateViewConfigurations(XrInstance instance,XrSystemId system,uint32_t cap,uint32_t* count,XrViewConfigurationType* out) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    logf("xrEnumerateViewConfigurations cap=%u quad=%d",cap,s.quad);
    if(!s.quad) return next.EnumerateViewConfigurations(instance,system,cap,count,out);
    if(!count || (cap&&!out)) return XR_ERROR_VALIDATION_FAILURE;
    *count=1;if(cap)out[0]=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;return XR_SUCCESS;
}
XrResult XRAPI_CALL layerGetViewConfigurationProperties(XrInstance instance,XrSystemId system,XrViewConfigurationType type,XrViewConfigurationProperties* out) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(s.quad&&type==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        XrResult r=next.GetViewConfigurationProperties(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO,out);
        if(XR_SUCCEEDED(r))out->viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        return r;
    }
    return next.GetViewConfigurationProperties(instance,system,type,out);
}
XrResult XRAPI_CALL layerEnumerateViewConfigurationViews(XrInstance instance,XrSystemId system,XrViewConfigurationType type,uint32_t cap,uint32_t* count,XrViewConfigurationView* out) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(!s.quad||type!=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)return next.EnumerateViewConfigurationViews(instance,system,type,cap,count,out);
    if(!count||(cap&&!out))return XR_ERROR_VALIDATION_FAILURE;
    *count=2;if(!cap)return XR_SUCCESS;if(cap<2)return XR_ERROR_SIZE_INSUFFICIENT;
    std::array<XrViewConfigurationView,4> runtime{};XrResult r=queryQuad(instance,system,runtime);if(XR_FAILED(r))return r;
    if(s.runtimeSettings){
        r=selectRuntimeSizes(runtime);if(XR_FAILED(r))return r;
        for(unsigned eye=0;eye<2;++eye){
            void* chain=out[eye].next;out[eye]=runtime[eye+2];out[eye].next=chain;
            out[eye].recommendedImageRectWidth=out[eye].maxImageRectWidth=s.runtimeFocusWidth;
            out[eye].recommendedImageRectHeight=out[eye].maxImageRectHeight=s.runtimeFocusHeight;
            out[eye].recommendedSwapchainSampleCount=out[eye].maxSwapchainSampleCount=1;
            logf("XR boundary advertised stereo eye=%u recommended=%ux%u max=%ux%u",eye,out[eye].recommendedImageRectWidth,out[eye].recommendedImageRectHeight,out[eye].maxImageRectWidth,out[eye].maxImageRectHeight);
        }
        return XR_SUCCESS;
    }
    uint32_t width=(std::max)(runtime[2].recommendedImageRectWidth,runtime[3].recommendedImageRectWidth);
    uint32_t height=(std::max)(runtime[2].recommendedImageRectHeight,runtime[3].recommendedImageRectHeight);
    width=lmuqv::focus_dimension(width,s.outputSizes.focusPercent);height=lmuqv::focus_dimension(height,s.outputSizes.focusPercent);
    uint32_t outerWidth=lmuqv::outer_dimension(width,s.outputSizes),outerHeight=lmuqv::outer_dimension(height,s.outputSizes);
    if(!width||!height||!outerWidth||!outerHeight)return XR_ERROR_LIMIT_REACHED;
    for(unsigned i=0;i<2;++i)if(outerWidth>runtime[i].maxImageRectWidth||outerHeight>runtime[i].maxImageRectHeight)return XR_ERROR_LIMIT_REACHED;
    for(unsigned i=2;i<4;++i)if(width>runtime[i].maxImageRectWidth||height>runtime[i].maxImageRectHeight)return XR_ERROR_LIMIT_REACHED;
    double ratio=std::sqrt(s.outputSizes.outerPercent/s.outputSizes.focusPercent);
    auto outerLimit=[&](uint32_t first,uint32_t second){return uint32_t((std::min)(double(std::numeric_limits<uint32_t>::max()),std::floor((std::min)(first,second)/ratio)));};
    uint32_t maxWidth=(std::min)((std::min)(runtime[2].maxImageRectWidth,runtime[3].maxImageRectWidth),outerLimit(runtime[0].maxImageRectWidth,runtime[1].maxImageRectWidth));
    uint32_t maxHeight=(std::min)((std::min)(runtime[2].maxImageRectHeight,runtime[3].maxImageRectHeight),outerLimit(runtime[0].maxImageRectHeight,runtime[1].maxImageRectHeight));
    for(unsigned i=0;i<2;++i) {
        void* chain=out[i].next;out[i]=runtime[i+2];out[i].next=chain;
        out[i].recommendedImageRectWidth=width;out[i].recommendedImageRectHeight=height;
        out[i].maxImageRectWidth=maxWidth;out[i].maxImageRectHeight=maxHeight;
        out[i].recommendedSwapchainSampleCount=out[i].maxSwapchainSampleCount=1;
    }
    logf("Stereo-facing common INNER dimensions=%ux%u; runtime outer=%ux%u/%ux%u inner=%ux%u/%ux%u",width,height,runtime[0].recommendedImageRectWidth,runtime[0].recommendedImageRectHeight,runtime[1].recommendedImageRectWidth,runtime[1].recommendedImageRectHeight,runtime[2].recommendedImageRectWidth,runtime[2].recommendedImageRectHeight,runtime[3].recommendedImageRectWidth,runtime[3].recommendedImageRectHeight);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL layerEnumerateEnvironmentBlendModes(XrInstance instance,XrSystemId system,XrViewConfigurationType type,uint32_t cap,uint32_t* count,XrEnvironmentBlendMode* out) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    return next.EnumerateEnvironmentBlendModes(instance,system,s.quad&&type==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO?XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO:type,cap,count,out);
}
XrResult XRAPI_CALL layerCreateSession(XrInstance instance,const XrSessionCreateInfo* info,XrSession* session) {
    std::unique_lock<std::recursive_mutex> lock(stateMutex);
#ifdef OPENGRIDXR_LMU_INTERNAL
    og::lmu::report_diagnostic("starting","Verifying the LMU graphics binding and engine hooks.");
    bool d3d11=false;
    if(info)for(auto* binding=static_cast<const XrBaseInStructure*>(info->next);binding;binding=binding->next)
        if(binding->type==XR_TYPE_GRAPHICS_BINDING_D3D11_KHR)
            d3d11=reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(binding)->device!=nullptr;
    if(!gameProcess()||!d3d11){og::lmu::report_diagnostic("unavailable","LMU identity or D3D11 graphics binding could not be verified; session refused.");return XR_ERROR_INITIALIZATION_FAILED;}
#endif
    logf("xrCreateSession quad=%d",s.quad);
    if(s.session)return XR_ERROR_LIMIT_REACHED;
    if(s.quad){const XrResult selection=selectFoveation(instance,info->systemId);if(XR_FAILED(selection))return selection;}
    if(s.quad&&s.runtimeSettings){
        std::array<XrViewConfigurationView,4> runtime{};
        XrResult sizing=queryQuad(instance,info->systemId,runtime);if(XR_FAILED(sizing))return sizing;
        sizing=selectRuntimeSizes(runtime);if(XR_FAILED(sizing))return sizing;
    }
    XrResult r=next.CreateSession(instance,info,session);if(XR_FAILED(r)){
#ifdef OPENGRIDXR_LMU_INTERNAL
        og::lmu::report_diagnostic("unavailable","The selected OpenXR runtime/provider rejected session creation.");
#endif
        logf("Downstream xrCreateSession failed result=%d",r);return r;}
    s.session=*session;s.system=info->systemId;
    publishActiveModes();
    if(s.vrs&&s.requestEyeTracking)logf("vrs gaze initialized=%d; invalid/unavailable gaze uses fixed focus",vrsGaze.initialize(*session));
    for(auto chain=static_cast<const XrBaseInStructure*>(info->next);chain;chain=chain->next) {
        if(chain->type==XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
            s.device=reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(chain)->device;
            if(s.device) {s.device->AddRef();s.device->GetImmediateContext(&s.context);}break;
        }
    }
    logf("xrCreateSession result=%d D3D11=%d game=%d",r,s.device!=nullptr,gameProcess());
    if(s.device&&gameProcess()){
        const bool started=lmuqv::engine_start(s.context);logf("engine_start result=%d",started);
        if((s.runtimeSettings||s.vrs)&&!started){
#ifdef OPENGRIDXR_LMU_INTERNAL
            if(og::lmu::diagnostic_snapshot().state!="unavailable")
                og::lmu::report_diagnostic("unavailable","LMU engine hook initialization failed; session refused. Disable LMU rendering, save and restart to use ordinary OpenXR.");
#endif
            logf("Runtime settings mode requires successful engine hooks; refusing session");
            lmuqv::engine_stop();vrsGaze.resetSession();next.DestroySession(*session);clearSession(lock);*session=XR_NULL_HANDLE;
            return XR_ERROR_INITIALIZATION_FAILED;
        }
    }
    benchmarkInitialize();
#ifdef OPENGRIDXR_LMU_INTERNAL
    og::lmu::report_diagnostic("ready","LMU engine hooks initialized successfully.");
#endif
    return r;
}
XrResult XRAPI_CALL layerDestroySession(XrSession session) {
    std::unique_lock<std::recursive_mutex> lock(stateMutex);
    if(frameOperationInFlight())return XR_ERROR_CALL_ORDER_INVALID;
    logf("xrDestroySession");lmuqv::engine_stop();benchmarkReset();vrsGaze.resetSession();
    outerResample.reset();
    for(auto& entry:s.appChains)clearFocusRtvs(entry.second);
    destroyOuter(lock);
    XrResult r=next.DestroySession(session);
    if(XR_SUCCEEDED(r))clearSession(lock);
    return r;
}
XrResult XRAPI_CALL layerBeginSession(XrSession session,const XrSessionBeginInfo* info) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(frameOperationInFlight())return XR_ERROR_CALL_ORDER_INVALID;
    auto modified=*info;if(s.quad)modified.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO;
    XrResult r=next.BeginSession(session,&modified);s.running=XR_SUCCEEDED(r);publishActiveModes();benchmarkRuntimeResult(r);
    logf("xrBeginSession app=%d runtime=%d result=%d",info->primaryViewConfigurationType,modified.primaryViewConfigurationType,r);return r;
}
XrResult XRAPI_CALL layerEndSession(XrSession session) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(frameOperationInFlight())return XR_ERROR_CALL_ORDER_INVALID;
    if(s.focusOwnership&&heldFocusImages()){logf("stereo_pass end-session refused: focus lease must be resolved or session destroyed");return XR_ERROR_CALL_ORDER_INVALID;}
    XrResult r=next.EndSession(session);benchmarkRuntimeResult(r);
    if(XR_SUCCEEDED(r)){benchmarkGpu.stop(false);s.running=false;publishActiveModes();clearFrame();if(benchmarkEnabled){benchmarkGpu.stop(false);benchmarkPreviousSceneQpc=0;benchmarkTransport.data()->header.running=0;benchmarkTransport.data()->header.status=1;benchmarkTransport.data()->header.reason=0;}}logf("xrEndSession result=%d",r);return r;
}
XrResult XRAPI_CALL layerWaitFrame(XrSession session,const XrFrameWaitInfo* info,XrFrameState* state) {
    // A pipelined WaitFrame must not hold the lock needed by the previous EndFrame.
    XrResult r=next.WaitFrame(session,info,state);
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    benchmarkRuntimeResult(r);
    if(XR_SUCCEEDED(r)){s.pendingTime=state->predictedDisplayTime;s.pendingPeriod=state->predictedDisplayPeriod;s.waitShouldRender=state->shouldRender==XR_TRUE;s.waitPending=true;}
    if(xrDiagnosticFrameLog()||XR_FAILED(r))logf("xrWaitFrame mode=%s completedBegins=%llu result=%d shouldRender=%d time=%lld",s.quad?"quad":"trace",static_cast<unsigned long long>(s.frames),r,s.waitShouldRender,static_cast<long long>(s.pendingTime));
    return r;
}
XrResult beginFrameImpl(XrSession session,const XrFrameBeginInfo* info,bool normalize,std::unique_lock<std::recursive_mutex> lock) {
    bool hadWait=false,shouldRender=false;
    XrTime predictedTime=0;XrDuration framePeriod=0;
    if(normalize&&(s.frame||s.earlyBeginPending||!s.quad||!s.runtimeSettings||session!=s.session||!s.running||
        !s.waitPending||!s.waitShouldRender||!s.located||s.locateTime!=s.pendingTime||
        s.outer[0].acquired||s.outer[1].acquired))return XR_SUCCESS;
    if(frameOperationInFlight())return XR_ERROR_CALL_ORDER_INVALID;
    if(s.earlyBeginPending){
        if(session!=s.session)return XR_ERROR_HANDLE_INVALID;
        if(info&&(info->type!=XR_TYPE_FRAME_BEGIN_INFO||info->next))return XR_ERROR_VALIDATION_FAILURE;
        const auto result=s.earlyBeginResult;s.earlyBeginPending=false;
        if(xrDiagnosticFrameLog())logf("xrBeginFrame late app call acknowledged frame=%llu result=%d; rendered captures preserved",static_cast<unsigned long long>(s.frames),result);
        return result;
    }
    if(s.focusOwnership&&heldFocusImages()){logf("stereo_pass begin-frame refused: prior focus lease unresolved");return XR_ERROR_CALL_ORDER_INVALID;}
    s.focusBeginInFlight=true;
    // Reserve this Wait before leaving the lock. A pipelined Wait may
    // complete during the runtime call and belongs to the following frame.
    hadWait=s.waitPending;predictedTime=s.pendingTime;framePeriod=s.pendingPeriod;shouldRender=s.waitShouldRender;
    s.waitPending=false;
    lock.unlock();
    XrResult r=runtimeCall([&]{return next.BeginFrame(session,info);});
    const int64_t benchmarkStart=benchmarkEnabled?lmuqv::benchmark::ticks():0;
    if(!normalize)lmuqv::renderProfile.xr_frame_begin();
    lock.lock();
    s.focusBeginInFlight=false;benchmarkRuntimeResult(r);
    // Flush an abandoned/discarded frame before accepting new engine captures.
    bool released=normalize?true:releaseOuters(lock);
    s.renderViewsLocked=false;
    s.frame=XR_SUCCEEDED(r)&&hadWait&&released;
    if(hadWait){s.predictedTime=predictedTime;s.framePeriod=framePeriod;s.shouldRender=shouldRender;}
    s.located=s.located&&s.locateTime==s.predictedTime;
    s.captures[0]=s.captures[1]=false;++s.frames;
    s.sceneFocus[0]=s.sceneFocus[1]=false;
    if(benchmarkEnabled)benchmarkBeginQpc=benchmarkStart;
    s.renderPreparationPending=normalize;
    if(benchmarkEnabled&&!normalize){
        benchmarkGpu.stop(false);
        if(s.frame&&s.shouldRender&&frameGpuSamplingAllowed())benchmarkGpu.start(s.context,s.frames);
    }
    std::fill(std::begin(s.debugDrawn),std::end(s.debugDrawn),false);
    if(XR_FAILED(r))s.located=false;
    if(normalize){s.earlyBeginResult=r;s.earlyBeginPending=true;}
    if(s.quad&&s.frame&&s.shouldRender){
        // Runtime ownership is ready before outer/focus engine rendering. A
        // failed wait retains ownership and can be retried by the copy path.
        for(unsigned eye=0;eye<2;++eye)acquireOuterImage(eye,lock);
    }
    if(xrDiagnosticFrameLog()||XR_FAILED(r)||!s.frame)logf("xrBeginFrame mode=%s frame=%llu pendingWait=%d renderable=%d shouldRender=%d result=%d",s.quad?"quad":"trace",static_cast<unsigned long long>(s.frames),hadWait,s.frame,s.shouldRender,r);
    return r;
}
XrResult XRAPI_CALL layerBeginFrame(XrSession session,const XrFrameBeginInfo* info) {
    return beginFrameImpl(session,info,false,std::unique_lock<std::recursive_mutex>(stateMutex));
}
// Some LMU/runtime combinations call Begin only after rendering and releasing
// their eye images. Normalize that order only when the engine needs a valid
// quad rendering frame. Normalize at the successful Locate boundary, before
// returning control to engine rendering. Accessors never call the runtime.
// A concurrent tracking-only query is answered from fresh runtime poses using
// local storage. It must not replace pixels/metadata reserved by another call.
XrResult trackingOnlyLocate(XrSession session,const XrViewLocateInfo* info,XrViewState* state,uint32_t cap,uint32_t* count,XrView* out,std::unique_lock<std::recursive_mutex>& lock){
    if(runtimeCallDepth){
#ifdef OPENGRIDXR_LMU_INTERNAL
        og::lmu::note_reentry();
#endif
        return XR_ERROR_CALL_ORDER_INVALID;
    }
    if(!info||!state||!count||(cap&&!out))return XR_ERROR_VALIDATION_FAILURE;
    const bool convert=s.quad&&info->viewConfigurationType==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    const bool runtimeSettings=s.runtimeSettings;
    const auto box=s.focusBox;const auto offsetX=s.focusOffsetX,offsetY=s.focusOffsetY;
#ifdef OPENGRIDXR_LMU_INTERNAL
    const auto crop=s.cropEdges;
#else
    const auto cropWidth=s.cropWidth,cropHeight=s.cropHeight;
#endif
    if(convert){*count=2;if(!cap)return XR_SUCCESS;if(cap<2)return XR_ERROR_SIZE_INSUFFICIENT;}
    XrViewLocateInfo request=*info;
    if(convert)request.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO;
    XrView local[4]{{XR_TYPE_VIEW},{XR_TYPE_VIEW},{XR_TYPE_VIEW},{XR_TYPE_VIEW}};uint32_t found=0;
    const auto locate=next.LocateViews;
    ++s.trackingReaders;lock.unlock();
    const XrResult result=runtimeCall([&]{return locate(session,&request,state,convert?4:cap,convert?&found:count,convert?local:out);});
    lock.lock();--s.trackingReaders;trackingFinished.notify_all();
    if(!convert||XR_FAILED(result))return result;
    if(found!=4)return XR_ERROR_RUNTIME_FAILURE;
    for(unsigned eye=2;eye<4;++eye){
        if((!runtimeSettings&&!lmuqv::scale_focus_fov(local[eye].fov,box.width.percent,box.height.percent))||
           !lmuqv::offset_focus_fov(local[eye].fov,local[eye-2].fov,offsetX.percent,offsetY.percent))return XR_ERROR_RUNTIME_FAILURE;
    }
#ifdef OPENGRIDXR_LMU_INTERNAL
    if(!lmuqv::apply_view_crop(local,crop))return XR_ERROR_RUNTIME_FAILURE;
#else
    if(!lmuqv::apply_view_crop(local,cropWidth.percent,cropHeight.percent))return XR_ERROR_RUNTIME_FAILURE;
#endif
    for(unsigned eye=0;eye<2;++eye){out[eye].pose=local[eye].pose;out[eye].fov=local[eye].fov;}
    return result;
}
XrResult XRAPI_CALL layerLocateViews(XrSession session,const XrViewLocateInfo* info,XrViewState* state,uint32_t cap,uint32_t* count,XrView* out) {
    std::unique_lock<std::recursive_mutex> lock(stateMutex);
    if(frameOperationInFlight())return trackingOnlyLocate(session,info,state,cap,count,out,lock);
    if(!s.quad||info->viewConfigurationType!=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO){
        const bool vrsStereo=s.vrs&&info->viewConfigurationType==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        const bool requestEyeTracking=s.requestEyeTracking;
        s.locateInFlight=true;lock.unlock();
        XrResult r=runtimeCall([&]{return next.LocateViews(session,info,state,cap,count,out);});
        const XrViewStateFlags valid=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
        const bool validVrs=vrsStereo&&XR_SUCCEEDED(r)&&cap>=2&&count&&*count==2&&out&&state&&(state->viewStateFlags&valid)==valid;
        XrPosef gaze{};
        const bool tracked=validVrs&&requestEyeTracking&&vrsGaze.ensureAttachedAndSync(session)&&vrsGaze.sample(info->space,info->displayTime,gaze);
        lock.lock();s.locateInFlight=false;trackingFinished.notify_all();
        if(vrsStereo){
            s.located=false;
            if(validVrs){
                s.locateTime=info->displayTime;s.locateSpace=info->space;
                s.located=true;
                s.vrsTracked=tracked;s.vrsGazePose=gaze;
                bool projected[2]{};
                for(unsigned eye=0;eye<2;++eye){
                    s.views[eye]=out[eye];float x=.5f,y=.5f;
                    projected[eye]=tracked&&lmuqv::vrs_gaze_center(out[eye],gaze,x,y);
                    if(!projected[eye])x=y=.5f;
                    s.vrsFocus[eye]=lmuqv::vrs_focus(x,y,s.vrsWidth,s.vrsHeight,float(s.focusOffsetX.percent/100),float(s.focusOffsetY.percent/100));
                }
                if(xrDiagnosticFrameLog())logf("vrs focus source=%s eye0=%.3f,%.3f,%.3f,%.3f; stereo views unchanged",
                    projected[0]&&projected[1]?"gaze":"fixed",s.vrsFocus[0].left,s.vrsFocus[0].top,s.vrsFocus[0].right,s.vrsFocus[0].bottom);
            }
        }
        if(xrDiagnosticFrameLog()||XR_FAILED(r))logf("xrLocateViews mode=%s frame=%llu capacity=%u count=%u begun=%d stateFlags=%llu time=%lld result=%d",s.quad?"quad-forward":"trace",static_cast<unsigned long long>(s.frames),cap,count?*count:0,s.frame,static_cast<unsigned long long>(XR_SUCCEEDED(r)&&state?state->viewStateFlags:0),static_cast<long long>(info->displayTime),r);
        return r;
    }
    if(!count||(cap&&!out))return XR_ERROR_VALIDATION_FAILURE;
    *count=2;if(!cap)return XR_SUCCESS;if(cap<2)return XR_ERROR_SIZE_INSUFFICIENT;
    const bool preserveRendered=s.frame&&s.renderViewsLocked;
    if(!preserveRendered)s.located=false;
    XrViewLocateInfo modified=*info;modified.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO;
    auto foveated=structure<XrViewLocateFoveatedRenderingVARJO>(XR_TYPE_VIEW_LOCATE_FOVEATED_RENDERING_VARJO);
    foveated.next=info->next;foveated.foveatedRenderingActive=s.eyeTrackingActive?XR_TRUE:XR_FALSE;
    modified.next=&foveated;
    XrView runtime[4]{};for(auto& v:runtime)v.type=XR_TYPE_VIEW;
    uint32_t runtimeCount=0;
    s.locateInFlight=true;lock.unlock();
    XrResult r=runtimeCall([&]{return next.LocateViews(session,&modified,state,4,&runtimeCount,runtime);});
    lock.lock();s.locateInFlight=false;trackingFinished.notify_all();
    if(XR_FAILED(r))return r;
    if(runtimeCount!=4)return XR_ERROR_RUNTIME_FAILURE;
    XrFovf fullOuter[2]={runtime[0].fov,runtime[1].fov};
    // Cropped FOV feeds rendering/UI; output is embedded back into fullOuterFov.
    // Translate each freshly located gaze FOV once; keep poses and never clamp to the outer FOV.
    if(!s.rawFovLogged){
        for(unsigned eye=0;eye<4;++eye)logf("Raw runtime locate FOV view=%u left=%.9g right=%.9g up=%.9g down=%.9g runtimeSettings=%d",eye,runtime[eye].fov.angleLeft,runtime[eye].fov.angleRight,runtime[eye].fov.angleUp,runtime[eye].fov.angleDown,s.runtimeSettings);
        s.rawFovLogged=true;
    }
    // Apply manual sizing and offsets to the freshly located provider views.
    for(unsigned eye=2;eye<4;++eye){
        if((!s.runtimeSettings&&!lmuqv::scale_focus_fov(runtime[eye].fov,s.focusBox.width.percent,s.focusBox.height.percent))||
           !lmuqv::offset_focus_fov(runtime[eye].fov,runtime[eye-2].fov,s.focusOffsetX.percent,s.focusOffsetY.percent)){
            logf("Invalid focus/outer FOV transform eye=%u focusWidthPercent=%g focusHeightPercent=%g offsetXPercent=%g offsetYPercent=%g; rejecting located views",eye-2,s.focusBox.width.percent,s.focusBox.height.percent,s.focusOffsetX.percent,s.focusOffsetY.percent);
            return XR_ERROR_RUNTIME_FAILURE;
        }
    }
#ifdef OPENGRIDXR_LMU_INTERNAL
    const bool cropped=lmuqv::apply_view_crop(runtime,s.cropEdges);
#else
    const bool cropped=lmuqv::apply_view_crop(runtime,s.cropWidth.percent,s.cropHeight.percent);
#endif
    if(!cropped){
        logf("Invalid rendered crop FOV; rejecting located views");return XR_ERROR_RUNTIME_FAILURE;
    }
    const auto valid=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    if(!preserveRendered){
        s.located=(state->viewStateFlags&valid)==valid;s.locateTime=info->displayTime;s.locateSpace=info->space;
        std::copy(std::begin(runtime),std::end(runtime),std::begin(s.views));
        std::copy(std::begin(fullOuter),std::end(fullOuter),std::begin(s.fullOuterFov));
    }else if(xrDiagnosticFrameLog()){
        double fovDelta=0,positionDelta=0;
        for(unsigned i=0;i<4;++i){
            const auto& a=s.views[i];const auto& b=runtime[i];
            fovDelta=(std::max)({fovDelta,double(std::abs(a.fov.angleLeft-b.fov.angleLeft)),double(std::abs(a.fov.angleRight-b.fov.angleRight)),double(std::abs(a.fov.angleUp-b.fov.angleUp)),double(std::abs(a.fov.angleDown-b.fov.angleDown))});
            positionDelta=(std::max)({positionDelta,double(std::abs(a.pose.position.x-b.pose.position.x)),double(std::abs(a.pose.position.y-b.pose.position.y)),double(std::abs(a.pose.position.z-b.pose.position.z))});
        }
        logf("render_views preserved frame=%llu lateLocateFovDeltaRad=%.9g positionDeltaM=%.9g sameTime=%d sameSpace=%d",static_cast<unsigned long long>(s.frames),fovDelta,positionDelta,s.locateTime==info->displayTime,s.locateSpace==info->space);
    }
    for(unsigned i=0;i<2;++i){out[i].pose=runtime[i].pose;out[i].fov=runtime[i].fov;}
    if(xrDiagnosticFrameLog()) logf("xrLocateViews mode=quad frame=%llu runtime=%u app=2 begun=%d valid=%d time=%lld",static_cast<unsigned long long>(s.frames),runtimeCount,s.frame,s.located,static_cast<long long>(s.locateTime));
    beginFrameImpl(session,nullptr,true,std::move(lock));
    return r;
}
XrResult XRAPI_CALL layerCreateSwapchain(XrSession session,const XrSwapchainCreateInfo* info,XrSwapchain* chain) {
    std::unique_lock<std::recursive_mutex> lock(stateMutex);
    if(frameOperationInFlight())return XR_ERROR_CALL_ORDER_INVALID;
    XrResult r=createInputSwapchain(session,*info,chain,"app");
    logf("xrCreateSwapchain %ux%u format=%lld samples=%u array=%u usage=%llu result=%d",info->width,info->height,static_cast<long long>(info->format),info->sampleCount,info->arraySize,static_cast<unsigned long long>(info->usageFlags),r);
    if(XR_SUCCEEDED(r)){
        AppChain app{};app.info=*info;app.info.next=nullptr;
        // Runtime mode waits for proof from LMU's eye-handle vector at image
        // enumeration. Preserve the old heuristic only for legacy experiments.
        const bool focusCandidate=!s.runtimeSettings&&(info->usageFlags&XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT)&&s.nextAppEye<2;
        if(focusCandidate)app.eye=s.nextAppEye++;
        if(s.runtimeSettings)logf("Swapchain role=unassigned handle=%p dimensions=%ux%u; awaiting native eye identity",static_cast<void*>(*chain),info->width,info->height);
        s.appChains[*chain]=std::move(app);
        if(s.quad&&focusCandidate&&!createOuter(*info,lock)&&s.runtimeSettings){
            next.DestroySwapchain(*chain);s.appChains.erase(*chain);--s.nextAppEye;*chain=XR_NULL_HANDLE;
            return XR_ERROR_RUNTIME_FAILURE;
        }
    }
    return r;
}
XrResult XRAPI_CALL layerDestroySwapchain(XrSwapchain chain) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(frameOperationInFlight())return XR_ERROR_CALL_ORDER_INVALID;
    const auto held=s.appChains.find(chain);if(held!=s.appChains.end()&&held->second.focusLease){logf("stereo_pass destroy-swapchain refused: focus lease unresolved");return XR_ERROR_CALL_ORDER_INVALID;}
    auto found=s.appChains.find(chain);if(found!=s.appChains.end())clearFocusRtvs(found->second);
    XrResult r=next.DestroySwapchain(chain);if(XR_SUCCEEDED(r))s.appChains.erase(chain);logf("xrDestroySwapchain result=%d",r);return r;
}
XrResult XRAPI_CALL layerEnumerateSwapchainImages(XrSwapchain chain,uint32_t cap,uint32_t* count,XrSwapchainImageBaseHeader* images) {
    std::unique_lock<std::recursive_mutex> lock(stateMutex);
    if(frameOperationInFlight())return XR_ERROR_CALL_ORDER_INVALID;
    auto identity=s.appChains.find(chain);
    if(s.quad&&s.runtimeSettings&&identity!=s.appChains.end()&&identity->second.eye==2){
        const unsigned eye=lmuqv::engine_game_eye(chain);
        if(eye<2){
            auto& app=identity->second;const auto& ci=app.info;
            const bool shape=s.runtimeSizesReady&&ci.width==s.runtimeFocusWidth&&ci.height==s.runtimeFocusHeight&&
                ci.sampleCount==1&&ci.arraySize==1&&ci.faceCount==1&&ci.mipCount==1&&!ci.createFlags&&
                (ci.usageFlags&XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT);
            bool eligible=shape;
            if(!shape){
                logf("Native game eye rejected eye=%u chain=%p actual=%ux%u required=%ux%u samples=%u array=%u; another layer may have rewritten the create request",eye,static_cast<void*>(chain),ci.width,ci.height,s.runtimeFocusWidth,s.runtimeFocusHeight,ci.sampleCount,ci.arraySize);
            }
            for(const auto& existing:s.appChains)if(existing.first!=chain&&existing.second.eye==eye){
                logf("Native game eye rejected: duplicate live identity eye=%u",eye);eligible=false;
            }
            // LMU ignores enumeration errors and later dereferences the images.
            // Always preserve its image lifecycle, even when quad use is refused.
            if(eligible&&createOuter(ci,lock)){
                app.eye=eye;
                logf("Swapchain role=game-focus proof=native-eye-vector eye=%u chain=%p dimensions=%ux%u",eye,static_cast<void*>(chain),ci.width,ci.height);
            }else logf("Native game eye remains unassigned eye=%u; forwarding image enumeration to preserve app lifecycle",eye);
        }
    }
    XrResult r=next.EnumerateSwapchainImages(chain,cap,count,images);
    if(s.runtimeSettings&&XR_SUCCEEDED(r)&&cap&&count&&*count<=cap&&*count<=64&&images){
        auto* d3d=reinterpret_cast<XrSwapchainImageD3D11KHR*>(images);
        for(uint32_t i=0;i<*count;++i){
            if(d3d[i].type!=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR)break;
            if(!d3d[i].texture)continue;
            D3D11_TEXTURE2D_DESC d{};d3d[i].texture->GetDesc(&d);
            logf("XR image chain=%p index=%u resource=%p size=%ux%u resourceFmt=%u samples=%u bind=%u",static_cast<void*>(chain),i,static_cast<void*>(d3d[i].texture),d.Width,d.Height,unsigned(d.Format),d.SampleDesc.Count,d.BindFlags);
        }
    }
    auto found=s.appChains.find(chain);
    if(trackAppImages()&&s.device&&XR_SUCCEEDED(r)&&cap&&count&&*count<=cap&&*count<=64&&images&&found!=s.appChains.end()&&found->second.eye<2){
        auto& app=found->second;clearFocusRtvs(app);app.images.clear();
        auto* d3d=reinterpret_cast<XrSwapchainImageD3D11KHR*>(images);
        for(uint32_t i=0;i<*count;++i){if(d3d[i].type!=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR){app.images.clear();break;}app.images.push_back(d3d[i].texture);}
    }
    logf("xrEnumerateSwapchainImages chain=%p cap=%u count=%u result=%d",static_cast<void*>(chain),cap,count?*count:0,r);
    return r;
}
XrResult XRAPI_CALL layerAcquireSwapchainImage(XrSwapchain chain,const XrSwapchainImageAcquireInfo* info,uint32_t* index) {
    std::unique_lock<std::recursive_mutex> lock(stateMutex);
    if(s.focusOwnership){const auto pending=s.appChains.find(chain);
        if(pending!=s.appChains.end()&&pending->second.focusLease){logf("stereo_pass acquire refused: rendering owner must retire focus lease first eye=%u",pending->second.eye);return XR_ERROR_CALL_ORDER_INVALID;}}
    if(!reserveAppImageOperation(chain))return XR_ERROR_CALL_ORDER_INVALID;
    const auto acquire=next.AcquireSwapchainImage;lock.unlock();const XrResult r=runtimeCall([&]{return acquire(chain,info,index);});lock.lock();
    retireAppImageOperation(chain);
    auto found=s.appChains.find(chain);
    if(trackAppImages()&&XR_SUCCEEDED(r)&&index&&found!=s.appChains.end()&&found->second.eye<2){
        auto& app=found->second;
        if(app.acquired.size()>=64){app.trackingInvalid=true;app.acquired.clear();}
        if(!app.trackingInvalid)app.acquired.push_back({*index,false});
    }
    if(xrDiagnosticFrameLog()||XR_FAILED(r))logf("xrAcquireSwapchainImage chain=%p index=%u result=%d",static_cast<void*>(chain),XR_SUCCEEDED(r)&&index?*index:0,r);
    return r;
}
XrResult XRAPI_CALL layerWaitSwapchainImage(XrSwapchain chain,const XrSwapchainImageWaitInfo* info) {
    // Waiting for a runtime image may block while another rendering thread finishes.
    std::unique_lock<std::recursive_mutex> lock(stateMutex);
    if(!reserveAppImageOperation(chain))return XR_ERROR_CALL_ORDER_INVALID;
    const auto wait=next.WaitSwapchainImage;lock.unlock();const XrResult r=runtimeCall([&]{return wait(chain,info);});lock.lock();
    retireAppImageOperation(chain);
    auto found=s.appChains.find(chain);
    if(trackAppImages()&&r==XR_SUCCESS&&found!=s.appChains.end()&&found->second.eye<2){
        for(auto& image:found->second.acquired)if(!image.waited){image.waited=true;break;}
    }
    if(xrDiagnosticFrameLog()||r!=XR_SUCCESS)logf("xrWaitSwapchainImage result=%d",r);
    return r;
}
XrResult releaseAppImageNow(XrSwapchain chain,const XrSwapchainImageReleaseInfo* info,std::unique_lock<std::recursive_mutex>& lock) {
    if(!reserveAppImageOperation(chain))return XR_ERROR_CALL_ORDER_INVALID;
    { // End iterator/reference lifetime before entering an unlocked runtime call.
    auto found=s.appChains.find(chain);
#ifdef OPENGRIDXR_LMU_INTERNAL
    if(found!=s.appChains.end()){
        auto& app=found->second;
        if(!app.trackingInvalid&&app.eye<2&&!app.acquired.empty()&&app.acquired.front().waited&&app.acquired.front().index<app.images.size())
            hostImage(app.eye+2,app.images[app.acquired.front().index]);
    }
#endif
    if((s.pixelEnabled||s.debugFocus)&&found!=s.appChains.end()&&found->second.eye<2){
        auto& app=found->second;
        if(!app.trackingInvalid&&app.eye<2&&!app.acquired.empty()&&app.acquired.front().waited&&app.acquired.front().index<app.images.size()){
            drawAppFocusDebug(app);
            if(!s.pixelFrame&&!s.pixelDone&&s.quad&&s.frames>=30&&s.frame&&s.shouldRender&&s.located&&s.captures[0]&&s.captures[1]&&s.locateTime==s.predictedTime&&
               (!s.pixelRequireScene||(s.sceneFocus[0]&&s.sceneFocus[1]))&&
               (!s.capture.requested||lmuqv::capture_detail::triggered(s.capture))){
                s.pixelFrame=s.frames;logf("pixel_stats_begin frame=%llu requireScene=%d sceneFocus=%d/%d (single-frame readback before release)",static_cast<unsigned long long>(s.pixelFrame),s.pixelRequireScene,s.sceneFocus[0],s.sceneFocus[1]);
                if(s.capture.requested)for(unsigned i=0;i<4;++i){
                    auto v=s.views[i];
                    // Outer BMPs are read after full-FOV embedding, whereas the
                    // native scene was rendered into the cropped target.
                    if(i<2&&s.outer[i].croppedTexture)v.fov=s.fullOuterFov[i];
                    logf("capture_view frame=%llu view=%u time=%lld fov=%.9g,%.9g,%.9g,%.9g position=%.9g,%.9g,%.9g orientation=%.9g,%.9g,%.9g,%.9g",static_cast<unsigned long long>(s.pixelFrame),i,static_cast<long long>(s.locateTime),v.fov.angleLeft,v.fov.angleRight,v.fov.angleUp,v.fov.angleDown,v.pose.position.x,v.pose.position.y,v.pose.position.z,v.pose.orientation.x,v.pose.orientation.y,v.pose.orientation.z,v.pose.orientation.w);
                    if(i<2){const auto& rendered=s.views[i].fov;logf("capture_render_fov frame=%llu view=%u fov=%.9g,%.9g,%.9g,%.9g",static_cast<unsigned long long>(s.pixelFrame),i,rendered.angleLeft,rendered.angleRight,rendered.angleUp,rendered.angleDown);}
                }
            }
            diagnosePixelSlot(app.eye+2,app.images[app.acquired.front().index]);
        }
    }
    }
    const auto release=next.ReleaseSwapchainImage;lock.unlock();const XrResult r=runtimeCall([&]{return release(chain,info);});lock.lock();
    retireAppImageOperation(chain);
    auto found=s.appChains.find(chain);
    if(trackAppImages()&&XR_SUCCEEDED(r)&&found!=s.appChains.end()&&!found->second.acquired.empty())found->second.acquired.pop_front();
    if(xrDiagnosticFrameLog()||XR_FAILED(r))logf("xrReleaseSwapchainImage result=%d",r);
    return r;
}
XrResult XRAPI_CALL layerReleaseSwapchainImage(XrSwapchain chain,const XrSwapchainImageReleaseInfo* info) {
    std::unique_lock<std::recursive_mutex> lock(stateMutex);
    if(graphicsOperationInFlight(false))return XR_ERROR_CALL_ORDER_INVALID;
    {auto found=s.appChains.find(chain);
    if(found!=s.appChains.end()&&found->second.runtimeBusy)return XR_ERROR_CALL_ORDER_INVALID;
    if(found!=s.appChains.end()&&found->second.focusLease){auto& app=found->second;
        if(!info||info->type!=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO||info->next||app.releasePending||app.leaseFrame!=s.frames||app.acquired.size()!=1||!app.acquired.front().waited)return XR_ERROR_CALL_ORDER_INVALID;
        app.pendingRelease=*info;app.releasePending=true;
        logf("stereo_pass focus release deferred eye=%u frame=%llu",app.eye,static_cast<unsigned long long>(s.frames));return XR_SUCCESS;
    }
    }
    return releaseAppImageNow(chain,info,lock);
}
bool validInner(const XrSwapchainSubImage& image,unsigned eye) {
    auto found=s.appChains.find(image.swapchain);if(found==s.appChains.end())return false;
    if(found->second.eye!=eye)return false;
    const auto& info=found->second.info;
    return info.format==s.colorInfo.format&&info.width==s.colorInfo.width&&info.height==s.colorInfo.height&&
           info.sampleCount==1&&image.imageArrayIndex<info.arraySize&&image.imageRect.offset.x>=0&&image.imageRect.offset.y>=0&&
           image.imageRect.extent.width>0&&image.imageRect.extent.height>0&&
           uint64_t(image.imageRect.offset.x)+image.imageRect.extent.width<=info.width&&
           uint64_t(image.imageRect.offset.y)+image.imageRect.extent.height<=info.height;
}
// The caller's layer records and this adapter's local converted arrays stay
// alive until the runtime returns. The reservation pins session/swapchain state;
// a pipelined Wait may still publish the following frame under stateMutex.
// The optional diagnostic benchmark overlay keeps its own image lifecycle calls
// in the existing serialized prepare path; only downstream EndFrame is split.
XrResult submitFrameUnlocked(XrSession session,const XrFrameEndInfo* info,std::unique_lock<std::recursive_mutex>& lock){
    const auto submit=next.EndFrame;
    s.submitInFlight=true;
    lock.unlock();
    const XrResult result=runtimeCall([&]{
#ifdef OPENGRIDXR_LMU_INTERNAL
        if(og::lmu::host.runtimeSubmit)return og::lmu::host.runtimeSubmit(session,submit,info);
#endif
        return submit(session,info);
    });
    lock.lock();
    s.submitInFlight=false;
    return result;
}
// 0.5.0 (from 0.3.5): failing or empty frames used to log every frame. Keep the
// periodic sampler, log state changes immediately (the last line still shows the
// current state for launcher diagnostics), then the first 10 frames of a streak
// and every 120th.
struct EndFrameLogGate {
    bool known=false;unsigned lastSubmitted=0;int lastShouldRender=-1;XrResult lastResult=XR_SUCCESS;uint64_t streak=0;
    bool allow(bool periodic,unsigned submitted,bool shouldRender,XrResult result,bool interesting){
        const bool changed=!known||(submitted!=0)!=(lastSubmitted!=0)||int(shouldRender)!=lastShouldRender||result!=lastResult;
        known=true;lastSubmitted=submitted;lastShouldRender=shouldRender?1:0;lastResult=result;
        streak=changed?1:streak+1;
        if(!lmuqv::diagnostics_enabled())return false;
        if(periodic||changed)return true;
        return interesting&&(streak<=10||streak%120==0);
    }
};
EndFrameLogGate endFrameLog;
XrResult XRAPI_CALL layerEndFrame(XrSession session,const XrFrameEndInfo* info) {
    std::unique_lock<std::recursive_mutex> lock(stateMutex);
    // Locate may legally run on a tracking thread. Finish its cache update
    // before preparing submission; a runtime callback must never wait on itself.
    if((s.locateInFlight||s.trackingReaders)&&!runtimeCallDepth)
        trackingFinished.wait(lock,[]{return !s.locateInFlight&&!s.trackingReaders;});
    if(frameOperationInFlight())return XR_ERROR_CALL_ORDER_INVALID;
    if(s.earlyBeginPending){logf("xrEndFrame refused: late application xrBeginFrame not received");return XR_ERROR_CALL_ORDER_INVALID;}
    if(s.focusOwnership&&heldFocusImages()){logf("stereo_pass end-frame refused: unresolved focus image owner; no downstream release/submission");return XR_ERROR_CALL_ORDER_INVALID;}
    // Call-order checks above never read info. Spec-invalid input would otherwise be
    // dereferenced below before the runtime could reject it.
    if(!info||(info->layerCount&&!info->layers))return XR_ERROR_VALIDATION_FAILURE;
    if(!s.quad){
        unsigned submitted=0;
        if(s.vrs&&info&&info->layers)for(uint32_t i=0;i<info->layerCount;++i){
            const auto* layer=info->layers[i];if(!layer||layer->type!=XR_TYPE_COMPOSITION_LAYER_PROJECTION)continue;
            const auto* projection=reinterpret_cast<const XrCompositionLayerProjection*>(layer);
            if(projection->viewCount==2&&projection->views){++submitted;
                for(unsigned eye=0;eye<2;++eye){const auto& extent=projection->views[eye].subImage.imageRect.extent;
                    s.vrsWidths[eye]=extent.width>0?unsigned(extent.width):0;s.vrsHeights[eye]=extent.height>0?unsigned(extent.height):0;}}
        }
        auto modified=*info;bool overlayAdded=false;
        // No heap allocation per overlay frame (same inline storage as the quad path).
        std::array<const XrCompositionLayerBaseHeader*,17> inlineLayers;std::vector<const XrCompositionLayerBaseHeader*> largeLayers;
        if(benchmarkEnabled){
            benchmarkGpu.stop(s.vrs&&s.sceneFocus[0]&&s.sceneFocus[1]);
            lmuqv::benchmark::Config config;
            benchmarkOverlayCapacityFailure=lmuqv::benchmark::read_config(*benchmarkTransport.data(),config)&&config.visible&&s.shouldRender&&info->layerCount>=benchmarkLayerLimit;
            if(s.vrs&&benchmarkOverlayReady&&info->layerCount<benchmarkLayerLimit){
                const auto* overlay=benchmarkOverlay.prepare(*benchmarkTransport.data(),s.context,s.frame&&s.shouldRender);
                if(overlay){
                    const XrCompositionLayerBaseHeader** target=inlineLayers.data();
                    if(size_t(info->layerCount)+1>inlineLayers.size()){largeLayers.resize(size_t(info->layerCount)+1);target=largeLayers.data();}
                    if(info->layerCount)std::copy(info->layers,info->layers+info->layerCount,target);
                    target[info->layerCount]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(overlay);
                    modified.layers=target;modified.layerCount=info->layerCount+1;overlayAdded=true;}
            }
        }
        const int64_t before=benchmarkEnabled?lmuqv::benchmark::ticks():0;
        XrResult r=submitFrameUnlocked(session,overlayAdded?&modified:info,lock);
        if(benchmarkEnabled)benchmarkPublish(submitted,0,before,lmuqv::benchmark::ticks(),r);
        if(s.vrs)lmuqv::renderProfile.xr_frame_end(s.context);
        const bool periodic=xrDiagnosticFrameLog();
        if(endFrameLog.allow(periodic,submitted,s.shouldRender,r,XR_FAILED(r))){
            if(s.vrs)logf("xrEndFrame mode=vrs frame=%llu appLayers=%u stereoProjections=%u begun=%d shouldRender=%d time=%lld result=%d",static_cast<unsigned long long>(s.frames),info->layerCount,submitted,s.frame,s.shouldRender,static_cast<long long>(info->displayTime),r);
            else logf("xrEndFrame mode=trace frame=%llu appLayers=%u begun=%d shouldRender=%d time=%lld result=%d",static_cast<unsigned long long>(s.frames),info->layerCount,s.frame,s.shouldRender,static_cast<long long>(info->displayTime),r);
        }
        clearFrame();
        return r;
    }
    // UI drawing uses the acquired RTVs. Runtime ownership resumes before submission.
    bool released=releaseOuters(lock);
    constexpr size_t inlineLayerCapacity = 16;
    std::array<const XrCompositionLayerBaseHeader*, inlineLayerCapacity+1> stackLayers;
    std::vector<const XrCompositionLayerBaseHeader*> largeLayers;
    if (info->layerCount > inlineLayerCapacity) {
        largeLayers.resize(size_t(info->layerCount)+1);
    }
    auto** layers = largeLayers.empty() ? stackLayers.data() : largeLayers.data();
    uint32_t layerCount = 0;
    // Only one captured projection is accepted; storage outlives the downstream call.
    XrCompositionLayerProjection convertedProjection{};
    std::array<XrCompositionLayerProjectionView,4> convertedViews;
    bool captured=released&&s.frame&&s.shouldRender&&s.located&&s.captures[0]&&s.captures[1]&&s.locateTime==info->displayTime;
    unsigned submitted=0;int benchmarkReason=captured?0:1;
    for(uint32_t i=0;i<info->layerCount;++i){
        const auto* layer=info->layers[i];
        if(!layer||layer->type!=XR_TYPE_COMPOSITION_LAYER_PROJECTION){if(layer)layers[layerCount++]=layer;continue;}
        auto* projection=reinterpret_cast<const XrCompositionLayerProjection*>(layer);
        if(projection->views&&xrDiagnosticFrameLog())for(unsigned eye=0;eye<(std::min)(2u,projection->viewCount);++eye){
            const auto& sub=projection->views[eye].subImage;
            logf("Projection input eye=%u chain=%p rect=%d,%d %dx%d array=%u",eye,static_cast<void*>(sub.swapchain),sub.imageRect.offset.x,sub.imageRect.offset.y,sub.imageRect.extent.width,sub.imageRect.extent.height,sub.imageArrayIndex);
        }
        if(projection->viewCount!=2||!projection->views||
           !validInner(projection->views[0].subImage,0)||!validInner(projection->views[1].subImage,1)){
            benchmarkReason=2;
            if(s.runtimeSettings&&xrDiagnosticFrameLog())
                logf("Projection refused: native eye identity, format, or subImage bounds invalid; required focus=%ux%u. If native handles are wrapped by another layer, disable that layer's resolution override/upscaler for diagnosis",s.runtimeFocusWidth,s.runtimeFocusHeight);
            continue;
        }
        if(!captured||projection->space==XR_NULL_HANDLE){
            if(projection->space==XR_NULL_HANDLE)benchmarkReason=3;
            if(xrDiagnosticFrameLog())logf("Projection refused: released=%d begun=%d shouldRender=%d located=%d captures=%d/%d timeMatch=%d spaceMatch=%d",released,s.frame,s.shouldRender,s.located,s.captures[0],s.captures[1],s.locateTime==info->displayTime,projection->space==s.locateSpace);
            continue;
        }
        // One engine scene capture pair cannot represent a second projection layer.
        if(submitted)continue;
        std::array<XrPosef,2> focusPoses;
        if(!lmuqv::submission_focus_pose(projection->views[0].pose,s.views[0].pose,s.views[2].pose,focusPoses[0])||
           !lmuqv::submission_focus_pose(projection->views[1].pose,s.views[1].pose,s.views[3].pose,focusPoses[1])){
            benchmarkReason=3;
            if(xrDiagnosticFrameLog())logf("Projection refused: invalid submitted or cached pose");
            continue;
        }
        auto& v=convertedViews;
        for(unsigned eye=0;eye<2;++eye){
            v[eye]=structure<XrCompositionLayerProjectionView>(XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW);
            const auto& outer=s.outer[eye];
            v[eye].pose=projection->views[eye].pose;v[eye].fov=outer.croppedTexture?s.fullOuterFov[eye]:s.views[eye].fov;
            v[eye].subImage.swapchain=s.outer[eye].chain;
            v[eye].subImage.imageRect.extent={static_cast<int32_t>(outer.croppedTexture?outer.outputWidth:outer.width),static_cast<int32_t>(outer.croppedTexture?outer.outputHeight:outer.height)};
            v[eye+2]=projection->views[eye];v[eye+2].pose=focusPoses[eye];v[eye+2].fov=s.views[eye+2].fov;
        }
        convertedProjection = *projection;
        convertedProjection.viewCount = 4;
        convertedProjection.views = v.data();
        if(s.capture.requested&&s.pixelFrame==s.frames)for(unsigned slot=0;slot<4;++slot){const auto& submittedView=v[slot];
            logf("capture_submission frame=%llu view=%u space=%p time=%lld fov=%.9g,%.9g,%.9g,%.9g position=%.9g,%.9g,%.9g orientation=%.9g,%.9g,%.9g,%.9g",static_cast<unsigned long long>(s.frames),slot,static_cast<void*>(projection->space),static_cast<long long>(info->displayTime),submittedView.fov.angleLeft,submittedView.fov.angleRight,submittedView.fov.angleUp,submittedView.fov.angleDown,submittedView.pose.position.x,submittedView.pose.position.y,submittedView.pose.position.z,submittedView.pose.orientation.x,submittedView.pose.orientation.y,submittedView.pose.orientation.z,submittedView.pose.orientation.w);}
        layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&convertedProjection);
        ++submitted;
    }
    if(submitted)benchmarkReason=0;
    if(benchmarkEnabled){
        benchmarkGpu.stop(s.sceneFocus[0]&&s.sceneFocus[1]);
        lmuqv::benchmark::Config overlayConfig;
        benchmarkOverlayCapacityFailure=lmuqv::benchmark::read_config(*benchmarkTransport.data(),overlayConfig)&&overlayConfig.visible&&s.shouldRender&&layerCount>=benchmarkLayerLimit;
        if(benchmarkOverlayReady&&layerCount<benchmarkLayerLimit){const auto* overlay=benchmarkOverlay.prepare(*benchmarkTransport.data(),s.context,s.frame&&s.shouldRender);if(overlay)layers[layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(overlay);}
    }
    auto modified = *info;
    modified.layerCount = layerCount;
    modified.layers = layerCount ? layers : nullptr;
    XrResult r;
    const int64_t benchmarkPreSubmit=benchmarkEnabled?lmuqv::benchmark::ticks():0;
    {lmuqv::ProfileScope submitProfile(lmuqv::ProfileSlot::provider_submit);r=submitFrameUnlocked(session,&modified,lock);}
#ifdef OPENGRIDXR_LMU_INTERNAL
    if(XR_SUCCEEDED(r)&&submitted&&og::lmu::host.submit)og::lmu::host.submit(s.session,hostFrame(),convertedProjection,info->displayTime);
#endif
    if(benchmarkEnabled)benchmarkPublish(submitted,benchmarkReason,benchmarkPreSubmit,lmuqv::benchmark::ticks(),r);
    lmuqv::renderProfile.xr_frame_end(s.context);
    lmuqv::stereo_audit_frame_end();
    if(s.pixelEnabled&&!s.pixelDone&&s.pixelFrame==s.frames&&s.pixelFrame){
        logf("pixel_stats_complete mask=%u successMask=%u frame=%llu quadProjections=%u releaseOkay=%d result=%d captureRequested=%d captureSuccessMask=%u",s.pixelMask,s.pixelSuccessMask,static_cast<unsigned long long>(s.pixelFrame),submitted,released,r,s.capture.requested,s.captureMask);s.pixelDone=true;
    }
    const bool periodic=xrDiagnosticFrameLog();
    if(endFrameLog.allow(periodic,submitted,s.shouldRender,r,(!submitted&&s.shouldRender)||XR_FAILED(r)))logf("xrEndFrame frame=%llu quadProjections=%u captures=%d/%d located=%d shouldRender=%d result=%d",static_cast<unsigned long long>(s.frames),submitted,s.captures[0],s.captures[1],s.located,s.shouldRender,r);
    clearFrame();return r;
}
XrResult XRAPI_CALL layerAttachSessionActionSets(XrSession session,const XrSessionActionSetsAttachInfo* info){
    std::lock_guard<std::recursive_mutex> lock(stateMutex);return vrsGaze.attach(session,info);
}
XrResult XRAPI_CALL layerSyncActions(XrSession session,const XrActionsSyncInfo* info){
    std::lock_guard<std::recursive_mutex> lock(stateMutex);return vrsGaze.sync(session,info);
}
XrResult XRAPI_CALL layerGetInstanceProcAddr(XrInstance instance,const char* name,PFN_xrVoidFunction* function) {
    if(!name||!function)return XR_ERROR_VALIDATION_FAILURE;
    logf("xrGetInstanceProcAddr %s",name);
#define INTERCEPT(api) if(!std::strcmp(name,"xr" #api)){*function=reinterpret_cast<PFN_xrVoidFunction>(layer##api);return XR_SUCCESS;}
    if(s.vrs&&vrsGaze.available()){INTERCEPT(AttachSessionActionSets) INTERCEPT(SyncActions)}
    INTERCEPT(GetInstanceProcAddr) INTERCEPT(DestroyInstance) INTERCEPT(EnumerateViewConfigurations)
    INTERCEPT(GetViewConfigurationProperties) INTERCEPT(EnumerateViewConfigurationViews) INTERCEPT(EnumerateEnvironmentBlendModes)
    INTERCEPT(CreateSession) INTERCEPT(DestroySession) INTERCEPT(BeginSession) INTERCEPT(EndSession)
    INTERCEPT(WaitFrame) INTERCEPT(BeginFrame) INTERCEPT(EndFrame) INTERCEPT(LocateViews)
    INTERCEPT(CreateSwapchain) INTERCEPT(DestroySwapchain)
    INTERCEPT(EnumerateSwapchainImages) INTERCEPT(AcquireSwapchainImage) INTERCEPT(WaitSwapchainImage) INTERCEPT(ReleaseSwapchainImage)
#undef INTERCEPT
    return next.get?next.get(instance,name,function):XR_ERROR_FUNCTION_UNSUPPORTED;
}
XrResult XRAPI_CALL layerCreateApiLayerInstance(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* layer,XrInstance* instance) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(!info||!layer||!instance||layer->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO||
       layer->structVersion!=XR_API_LAYER_CREATE_INFO_STRUCT_VERSION||layer->structSize!=sizeof(XrApiLayerCreateInfo)||
       !layer->nextInfo)return XR_ERROR_INITIALIZATION_FAILED;
    // 0.5.1: the extension names are dereferenced below (strcmp); reject a spec-invalid list
    // instead of crashing inside the game's instance creation.
    if(info->enabledExtensionCount&&!info->enabledExtensionNames)return XR_ERROR_VALIDATION_FAILURE;
    for(uint32_t i=0;i<info->enabledExtensionCount;++i)if(!info->enabledExtensionNames[i])return XR_ERROR_VALIDATION_FAILURE;
    auto* chain=layer->nextInfo;
    if(chain->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO||chain->structVersion!=XR_API_LAYER_NEXT_INFO_STRUCT_VERSION||
       chain->structSize!=sizeof(XrApiLayerNextInfo)||std::strcmp(chain->layerName,layerName)||
       !chain->nextGetInstanceProcAddr||!chain->nextCreateApiLayerInstance)return XR_ERROR_INITIALIZATION_FAILED;
    if(s.instance)return XR_ERROR_LIMIT_REACHED;
    bool quad=quadMode(),vrs=vrsMode();auto modified=*info;std::vector<const char*> extensions;
    char foveationMode[16]{};
    const DWORD modeLength=GetEnvironmentVariableA("LMU_QV_FOVEATION_MODE",foveationMode,sizeof(foveationMode));
    if((quad||vrs)&&(modeLength>=sizeof(foveationMode)||
       (modeLength&&std::strcmp(foveationMode,"auto")&&std::strcmp(foveationMode,"fixed")))){
        logf("Invalid LMU_QV_FOVEATION_MODE; use auto or fixed");return XR_ERROR_VALIDATION_FAILURE;
    }
    const bool requestEyeTracking=std::strcmp(foveationMode,"fixed")!=0;
    char blurText[64]{};float outerBlur=0;
    const DWORD blurLength=GetEnvironmentVariableA("LMU_QV_OUTER_BLUR",blurText,sizeof(blurText));
    if(quad&&(blurLength>=sizeof(blurText)||!lmuqv::parse_outer_blur(blurText,outerBlur))){
        logf("Invalid LMU_QV_OUTER_BLUR; expected finite output-pixel radius 0..3");return XR_ERROR_VALIDATION_FAILURE;
    }
    for(uint32_t i=0;i<info->enabledExtensionCount;++i)extensions.push_back(info->enabledExtensionNames[i]);
    if(quad)for(const char* extension:{XR_VARJO_QUAD_VIEWS_EXTENSION_NAME,XR_VARJO_FOVEATED_RENDERING_EXTENSION_NAME}){
        if(std::none_of(extensions.begin(),extensions.end(),[&](const char* e){return !std::strcmp(e,extension);}))extensions.push_back(extension);
    }
    const bool injectGaze=vrs&&requestEyeTracking&&std::none_of(extensions.begin(),extensions.end(),[](const char* e){return !std::strcmp(e,XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);});
    if(injectGaze)extensions.push_back(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);
    modified.enabledExtensionCount=static_cast<uint32_t>(extensions.size());modified.enabledExtensionNames=extensions.data();
    auto downstream=*layer;downstream.nextInfo=chain->next;
    XrResult r=chain->nextCreateApiLayerInstance(&modified,&downstream,instance);
    bool gazeEnabled=vrs&&requestEyeTracking;
    if(r==XR_ERROR_EXTENSION_NOT_PRESENT&&injectGaze){
        extensions.pop_back();modified.enabledExtensionCount=uint32_t(extensions.size());modified.enabledExtensionNames=extensions.data();
        *instance=XR_NULL_HANDLE;r=chain->nextCreateApiLayerInstance(&modified,&downstream,instance);gazeEnabled=false;
        logf("vrs gaze extension unavailable; fixed focus fallback");
    }
    // Bounded: the fixed-size application name is not guaranteed to be terminated by every caller.
    logf("xrCreateApiLayerInstance mode=%s app=%.*s extensions=%u result=%d",quad?"quad":"trace",int(strnlen(info->applicationInfo.applicationName,XR_MAX_APPLICATION_NAME_SIZE)),info->applicationInfo.applicationName,modified.enabledExtensionCount,r);
    if(XR_FAILED(r))return r;
    next.get=chain->nextGetInstanceProcAddr;
    if(!next.load(*instance)){if(next.DestroyInstance)next.DestroyInstance(*instance);*instance=XR_NULL_HANDLE;return XR_ERROR_INITIALIZATION_FAILED;}
    // Once per process, only on explicit launcher request: prefer the 3D V-Cache CCD.
    static std::atomic<bool> cacheAffinityDone{false};
    if((quad||vrs)&&!cacheAffinityDone.exchange(true)){
        char affinity[16]{};const DWORD affinityLength=GetEnvironmentVariableA("LMU_QV_CPU_AFFINITY",affinity,sizeof(affinity));
        const auto outcome=affinityLength&&affinityLength<sizeof(affinity)&&!std::strcmp(affinity,"x3d")?
            cpu_cache::applyPreferVCache():std::string("CPU cache preference is off; Windows scheduling unchanged.");
#ifdef OPENGRIDXR_LMU_INTERNAL
        og::lmu::report_cpu_affinity(outcome.c_str());
#endif
        logf("%s",outcome.c_str());
    }
    s.instance=*instance;s.quad=quad;s.vrs=vrs;s.requestEyeTracking=requestEyeTracking;
    publishActiveModes();
    if(gazeEnabled)vrsGaze.load(*instance,next.get);
    else vrsGaze.load(XR_NULL_HANDLE,nullptr);
    if(vrs){
        auto dimension=[](const char* name){char text[64]{};const auto n=GetEnvironmentVariableA(name,text,sizeof(text));char* end=nullptr;
            const float value=std::strtof(text,&end);return n&&n<sizeof(text)&&end==text+n&&std::isfinite(value)&&value>=10&&value<=90?value/100:.5f;};
        s.vrsWidth=dimension("LMU_QV_FOCUS_WIDTH_PERCENT");s.vrsHeight=dimension("LMU_QV_FOCUS_HEIGHT_PERCENT");
        logf("vrs mode=stereo peripheral shading experimental; focus=%.1f/%.1f%%; no quad views or extra focus passes",s.vrsWidth*100,s.vrsHeight*100);
    }
    s.engineGpuTimingArmed=!environmentWide(L"LMU_QV_GPU_TIMING_TRIGGER").empty()||!environmentWide(L"LMU_QV_BENCH_CONTROL").empty();
    s.outerBlur=quad?outerBlur:0;
    if(quad)logf("Outer smoothing radius=%g output pixels; focus source untouched",s.outerBlur);
    if(quad)logf("Foveation request mode=%s; actual eye-tracking availability is determined by the downstream provider",requestEyeTracking?"auto":"fixed");
    char runtimeSetting[8]{};GetEnvironmentVariableA("LMU_QV_USE_RUNTIME_SETTINGS",runtimeSetting,sizeof(runtimeSetting));
    s.runtimeSettings=quad&&!std::strcmp(runtimeSetting,"1");
#ifdef OPENGRIDXR_LMU_INTERNAL
    s.runtimeSettings=quad;
#endif
    char sampledSetting[8]{};
    const auto sampledLength=GetEnvironmentVariableA("LMU_QV_VECTORXR_DIRECT_INPUTS",sampledSetting,sizeof(sampledSetting));
    s.sampledInputs=quad&&sampledLength==1&&sampledSetting[0]=='1';
    if(s.sampledInputs)logf("sampled_input enabled for eligible color swapchains; requests SAMPLED usage; downstream copy fallback retained");
    char cropWidth[128]{},cropHeight[128]{};
    const auto cropWidthLength=GetEnvironmentVariableA("LMU_QV_CROP_WIDTH_PERCENT",cropWidth,sizeof(cropWidth));
    const auto cropHeightLength=GetEnvironmentVariableA("LMU_QV_CROP_HEIGHT_PERCENT",cropHeight,sizeof(cropHeight));
    s.cropWidth=lmuqv::parse_crop_percent(cropWidthLength>=sizeof(cropWidth)?"invalid":cropWidth);
    s.cropHeight=lmuqv::parse_crop_percent(cropHeightLength>=sizeof(cropHeight)?"invalid":cropHeight);
    s.cropEdges={(100-s.cropWidth.percent)/2,(100-s.cropWidth.percent)/2,(100-s.cropHeight.percent)/2,(100-s.cropHeight.percent)/2};
#ifdef OPENGRIDXR_LMU_INTERNAL
    const auto& configuredCrop=og::lmu::settings.crop;
    s.cropEdges={configuredCrop.left,configuredCrop.right,configuredCrop.top,configuredCrop.bottom};
    if(!lmuqv::valid_crop_edges(s.cropEdges)){
        next.DestroyInstance(*instance);*instance=XR_NULL_HANDLE;s=State{};return XR_ERROR_VALIDATION_FAILURE;
    }
    s.cropWidth={100-configuredCrop.left-configuredCrop.right,true};
    s.cropHeight={100-configuredCrop.top-configuredCrop.bottom,true};
#endif
    if(!s.cropWidth.valid||!s.cropHeight.valid)logf("Invalid crop percentage; affected axis reset to 100 (range 25..100)");
    if(quad)logf("Rendered crop width=%g%% height=%g%%; outer FOV and target dimensions reduced together; focus fitted within crop; restart required",s.cropWidth.percent,s.cropHeight.percent);
    if(s.runtimeSettings)logf("Runtime settings enabled: runtime focus size preserved; mod focus offsets and crop applied; legacy percentage controls ignored");
    if(!s.runtimeSettings){
    char outerPercent[128]{},focusPercent[128]{};
    DWORD outerLength=GetEnvironmentVariableA("LMU_QV_OUTER_PIXELS_PERCENT",outerPercent,sizeof(outerPercent));
    DWORD focusLength=GetEnvironmentVariableA("LMU_QV_FOCUS_PIXELS_PERCENT",focusPercent,sizeof(focusPercent));
    s.outputSizes=lmuqv::parse_output_sizes(outerLength>=sizeof(outerPercent)?"invalid":outerPercent,focusLength>=sizeof(focusPercent)?"invalid":focusPercent);
    if(!s.outputSizes.valid)logf("Invalid output percentages; using safe defaults outer=100 focus=100 (outer range 1..400, focus range 25..400, outer<=focus)");
    if(quad)logf("Output pixel percentages outer=%g focus=%g (relative to original common INNER recommendation)",s.outputSizes.outerPercent,s.outputSizes.focusPercent);
    char boxPercent[128]{},widthPercent[128]{},heightPercent[128]{};
    DWORD boxLength=GetEnvironmentVariableA("LMU_QV_FOCUS_BOX_PERCENT",boxPercent,sizeof(boxPercent));
    DWORD widthLength=GetEnvironmentVariableA("LMU_QV_FOCUS_WIDTH_PERCENT",widthPercent,sizeof(widthPercent));
    DWORD heightLength=GetEnvironmentVariableA("LMU_QV_FOCUS_HEIGHT_PERCENT",heightPercent,sizeof(heightPercent));
    s.focusBox=lmuqv::parse_focus_box_size(widthLength>=sizeof(widthPercent)?"invalid":widthPercent,
        heightLength>=sizeof(heightPercent)?"invalid":heightPercent,boxLength>=sizeof(boxPercent)?"invalid":boxPercent);
    if(!s.focusBox.width.valid)logf("Invalid focus width percentage; using safe default 100 (decimal range 25..200)");
    if(!s.focusBox.height.valid)logf("Invalid focus height percentage; using safe default 100 (decimal range 25..200)");
    if(quad)logf("Focus box width=%g%% height=%g%% (tangent-space extents around runtime focus center)",s.focusBox.width.percent,s.focusBox.height.percent);
    }
    char offsetX[128]{},offsetY[128]{};
    const auto offsetXLength=GetEnvironmentVariableA("LMU_QV_FOCUS_OFFSET_X_PERCENT",offsetX,sizeof(offsetX));
    const auto offsetYLength=GetEnvironmentVariableA("LMU_QV_FOCUS_OFFSET_Y_PERCENT",offsetY,sizeof(offsetY));
    s.focusOffsetX=lmuqv::parse_focus_offset(offsetXLength>=sizeof(offsetX)?"invalid":offsetX);
    s.focusOffsetY=lmuqv::parse_focus_offset(offsetYLength>=sizeof(offsetY)?"invalid":offsetY);
    if(!s.focusOffsetX.valid)logf("Invalid horizontal focus offset; using safe default 0 (decimal range -50..50)");
    if(!s.focusOffsetY.valid)logf("Invalid vertical focus offset; using safe default 0 (decimal range -50..50)");
    if(quad)logf("Focus offsets horizontal=%g%% vertical=%g%% (per-eye full outer tangent span; positive right/up)",s.focusOffsetX.percent,s.focusOffsetY.percent);
    char pixelEnv[8]{};GetEnvironmentVariableA("LMU_QV_PIXEL_STATS",pixelEnv,sizeof(pixelEnv));
    s.pixelRequireScene=lmuqv::diagnostics_enabled()&&quad&&!std::strcmp(pixelEnv,"scene");
    s.pixelEnabled=lmuqv::diagnostics_enabled()&&quad&&(!std::strcmp(pixelEnv,"1")||s.pixelRequireScene);
    s.capture=lmuqv::diagnostics_enabled()?lmuqv::capture_detail::config(environmentWide(L"LMU_QV_CAPTURE_DIR"),environmentWide(L"LMU_QV_CAPTURE_TRIGGER")):lmuqv::capture_detail::Config{};
    if(quad&&s.capture.requested){
        s.pixelEnabled=true;s.pixelRequireScene=true;
        logf("capture_config valid=%d existingDirectory=%s trigger=%s (four fixed BMP names; CREATE_NEW; scene gate)",s.capture.valid,s.capture.directory.u8string().c_str(),s.capture.trigger.u8string().c_str());
    }
    if(s.pixelEnabled)logf("pixel_stats_enabled minimumFrame=30 oneFrame=1 requireScene=%d",s.pixelRequireScene);
    char auditSetting[8]{};
    if(quad&&lmuqv::diagnostics_enabled()&&GetEnvironmentVariableA("LMU_QV_STEREO_AUDIT",auditSetting,sizeof(auditSetting))==1&&auditSetting[0]=='1'){
        const auto auditDirectory=environmentWide(L"LMU_QV_STEREO_AUDIT_DIR");
        lmuqv::stereo_audit_prepare(true,auditDirectory.c_str(),lmuqv::log_line);
    }
    char debugEnv[8]{};GetEnvironmentVariableA("LMU_QV_DEBUG_FOCUS",debugEnv,sizeof(debugEnv));s.debugFocus=quad&&!std::strcmp(debugEnv,"1");
    if(s.debugFocus)logf("debug_focus enabled: lime runtime Focus-FOV outline; current cached FOV, not a synthetic gaze signal");
    return r;
}
} // namespace

namespace lmuqv {
void xr_scene_focus_complete(unsigned eye) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(eye>1||!s.quad||!s.frame||!s.shouldRender||!s.located||s.locateTime!=s.predictedTime)return;
    s.sceneFocus[eye]=true;
    if(xrDiagnosticFrameLog()||(s.pixelRequireScene&&!s.pixelDone&&s.sceneFocus[0]&&s.sceneFocus[1]))
        logf("scene_focus_complete frame=%llu eye=%u completed=%d/%d",static_cast<unsigned long long>(s.frames),eye,s.sceneFocus[0],s.sceneFocus[1]);
}
OuterTarget xr_outer_target(unsigned eye) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if (renderingOperationInFlight() || eye > 1 || !s.frame || !s.shouldRender || !s.captures[eye]) return {};
    const auto& outer = s.outer[eye];
    if (!outer.acquired || !outer.waited || outer.index >= outer.rtvs.size()) return {};
    if (!outer.rtvs[outer.index]) return {};
    return {outer.croppedRtv?outer.croppedRtv:outer.rtvs[outer.index], outer.width, outer.height};
}
ID3D11RenderTargetView* xr_outer_rtv(unsigned eye) {
    return xr_outer_target(eye).rtv;
}
void log_line(const char* message) {
#ifdef OPENGRIDXR_LMU_INTERNAL
    // A bounded first refusal survives even when verbose diagnostics are off.
    // These messages are emitted on startup or on a latched native-bank failure.
    if(message&&((!std::strncmp(message,"engine:",7)&&std::strstr(message,"hooks refused"))||
        std::strstr(message,"quad rendering refused"))){
        if(og::lmu::diagnostic_snapshot().state!="unavailable")og::lmu::report_diagnostic("unavailable",message);
    }else if(message&&!std::strncmp(message,"native_outer",12)&&std::strstr(message,"refused")){
        og::lmu::note_native_outer_refusal(message);
    }
#endif
    if(!diagnostics_enabled())return;
    char line[2304]{};SYSTEMTIME now{};GetSystemTime(&now);
    int count=std::snprintf(line,sizeof(line),"%02u:%02u:%02u.%03u [%lu] %s\r\n",now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,GetCurrentThreadId(),message?message:"");
    if(count<=0)return;
    // Queue copies at most sizeof(line), marks truncation and counts it. Passing
    // snprintf's required length never permits it to read beyond this buffer.
    async_log_enqueue(line,static_cast<std::size_t>(count));
}
void flush_log(){
    if(!diagnostics_enabled())return;
    async_log_request_flush();
}
void stop_log(){async_log_stop();}
bool xr_runtime_sizes(unsigned eye,uint32_t& outerWidth,uint32_t& outerHeight,uint32_t& focusWidth,uint32_t& focusHeight){
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(eye>1||!s.runtimeSettings||!s.runtimeSizesReady)return false;
    outerWidth=s.runtimeOuterWidth[eye];outerHeight=s.runtimeOuterHeight[eye];focusWidth=s.runtimeFocusWidth;focusHeight=s.runtimeFocusHeight;return true;
}
bool xr_quad_active(){return quadActive.load(std::memory_order_acquire);}
bool xr_vrs_active(){return vrsActive.load(std::memory_order_acquire);}
bool xr_vrs_focus(unsigned eye,VrsFocus& focus){std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(graphicsOperationInFlight()||eye>1||!s.vrs||!s.frame||!s.shouldRender||!s.located||s.locateTime!=s.predictedTime)return false;
    focus=s.vrsFocus[eye];return true;}
bool xr_vrs_render_focus(unsigned eye,const float* projection,VrsFocus& focus){std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(!xr_vrs_focus(eye,focus))return false;
    auto rendered=s.views[eye];if(!vrs_projection_fov(projection,rendered.fov))return false;
    float x=.5f,y=.5f;
    if(!s.vrsTracked||!vrs_gaze_center(rendered,s.vrsGazePose,x,y))x=y=.5f;
    focus=vrs_focus(x,y,s.vrsWidth,s.vrsHeight,float(s.focusOffsetX.percent/100),float(s.focusOffsetY.percent/100));return true;
}
void xr_vrs_scene_complete(unsigned eye){std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(eye<2&&s.vrs&&s.frame&&s.shouldRender)s.sceneFocus[eye]=true;}
// Called only by the engine rendering accessors, on their existing D3D thread.
void prepareNormalizedRenderFrame(){
    if(!s.renderPreparationPending)return;
    s.renderPreparationPending=false;
    renderProfile.xr_frame_begin();
    if(benchmarkEnabled){
        benchmarkGpu.stop(false);
        if(s.frame&&s.shouldRender&&frameGpuSamplingAllowed())benchmarkGpu.start(s.context,s.frames);
    }
}
bool xr_frame_views(XrView (&views)[4]) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(renderingOperationInFlight()||!s.quad||!s.frame||!s.shouldRender||!s.located||s.locateTime!=s.predictedTime)return false;
    prepareNormalizedRenderFrame();
    s.renderViewsLocked=true;
    std::copy(std::begin(s.views),std::end(s.views),std::begin(views));return true;
}
bool xr_frame_full_outer_fov(XrFovf (&fov)[2]) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(renderingOperationInFlight()||!s.quad||!s.frame||!s.shouldRender||!s.located||s.locateTime!=s.predictedTime)return false;
    std::copy(std::begin(s.fullOuterFov),std::end(s.fullOuterFov),std::begin(fov));return true;
}
bool xr_prepare_focus_image_ownership(){
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(!s.quad||!s.session||!s.context||s.frame||!s.appChains.empty())return false;
    s.focusOwnership=true;return true;
}
bool xr_hold_focus_image(unsigned eye,FocusImageLease& lease){
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(lease.token||eye>1||!s.focusOwnership||frameOperationInFlight(false)||!s.frame||!s.shouldRender||!s.located||s.locateTime!=s.predictedTime||focusLeaseSerial==UINT64_MAX)return false;
    AppChain* selected=nullptr;
    for(auto& pair:s.appChains)if(pair.second.eye==eye){auto& app=pair.second;
        if(app.runtimeBusy||app.focusLease||app.releasePending||app.trackingInvalid||app.acquired.size()!=1||!app.acquired.front().waited||app.acquired.front().index>=app.images.size())continue;
        if(selected)return false;selected=&app;
    }
    if(!selected)return false;auto& app=*selected;auto* texture=app.images[app.acquired.front().index];if(!texture)return false;
    app.focusLease=++focusLeaseSerial;app.leaseFrame=s.frames;app.leaseOwner=GetCurrentThreadId();
    lease={app.focusLease,s.frames,eye,app.acquired.front().index,texture};return true;
}
bool xr_focus_lease_valid(const FocusImageLease& lease){
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(!lease.token||!s.frame||lease.frame!=s.frames||!s.focusOwnership)return false;
    for(const auto& pair:s.appChains){const auto& app=pair.second;
        if(app.focusLease==lease.token)return !app.runtimeBusy&&app.eye==lease.eye&&app.leaseFrame==lease.frame&&app.leaseOwner==GetCurrentThreadId()&&app.acquired.size()==1&&app.acquired.front().index==lease.index&&lease.index<app.images.size()&&app.images[lease.index]==lease.texture;
    }return false;
}
bool xr_retire_focus_image(FocusImageLease& lease){
    std::unique_lock<std::recursive_mutex> lock(stateMutex);if(!xr_focus_lease_valid(lease))return false;
    XrSwapchain chain=XR_NULL_HANDLE;bool releasePending=false;XrSwapchainImageReleaseInfo pending{};
    for(const auto& pair:s.appChains)if(pair.second.focusLease==lease.token){chain=pair.first;releasePending=pair.second.releasePending;pending=pair.second.pendingRelease;break;}
    if(!chain)return false;
    if(releasePending&&XR_FAILED(releaseAppImageNow(chain,&pending,lock)))return false;
    // Re-find by handle: no map iterator, AppChain reference or member-info
    // pointer crosses the runtime call. Failure above preserves the lease/retry.
    auto found=s.appChains.find(chain);if(found==s.appChains.end()||found->second.focusLease!=lease.token)return false;
    auto& app=found->second;app.focusLease=app.leaseFrame=0;app.leaseOwner=0;app.releasePending=false;lease={};return true;
}
bool xr_copy_outer(unsigned eye,ID3D11Texture2D* source) {
    if(eye>1)return false;
    ProfileScope lockProfile(profile_eye(ProfileSlot::outer_lock0,eye));
    std::unique_lock<std::recursive_mutex> lock(stateMutex);
    lockProfile.end();
    if(renderingOperationInFlight()||!source||!s.quad||!s.frame||!s.shouldRender||!s.located||!s.context||s.captures[eye])return false;
    auto& outer=s.outer[eye];if(!outer.chain)return false;
    D3D11_TEXTURE2D_DESC src{};source->GetDesc(&src);
    const bool fullSize=src.Width==s.colorInfo.width&&src.Height==s.colorInfo.height;
    const bool nativeOuterSize=src.Width==outer.width&&src.Height==outer.height;
    if((!fullSize&&!nativeOuterSize)||src.SampleDesc.Count!=1||src.ArraySize!=1||src.MipLevels!=1||formatFamily(src.Format)!=formatFamily(static_cast<DXGI_FORMAT>(s.colorInfo.format))) {
        logf("outer copy incompatible eye=%u source=%ux%u fmt=%u samples=%u array=%u mips=%u expected=%ux%u fmt=%lld",eye,src.Width,src.Height,src.Format,src.SampleDesc.Count,src.ArraySize,src.MipLevels,s.colorInfo.width,s.colorInfo.height,static_cast<long long>(s.colorInfo.format));return false;
    }
    bool resized=src.Width!=outer.width||src.Height!=outer.height;
    const bool shaderTransfer=resized||s.outerBlur>0;
    bool resampleFormat=src.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS||src.Format==DXGI_FORMAT_R8G8B8A8_UNORM||src.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||
                        src.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS||src.Format==DXGI_FORMAT_B8G8R8A8_UNORM||src.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    if(shaderTransfer&&(!(src.BindFlags&D3D11_BIND_SHADER_RESOURCE)||!resampleFormat)){
        logf("outer resample incompatible source eye=%u format=%u bindFlags=%u",eye,src.Format,src.BindFlags);return false;
    }
    ID3D11Device* sourceDevice{};source->GetDevice(&sourceDevice);bool same=sourceDevice==s.device;if(sourceDevice)sourceDevice->Release();if(!same){logf("outer copy rejected different device");return false;}
    ProfileScope waitProfile(profile_eye(ProfileSlot::outer_wait0,eye));
    if(!acquireOuterImage(eye,lock))return false;
    waitProfile.end();
    bool copied=false;
    if(outer.index<outer.images.size()&&outer.images[outer.index].texture){
        ID3D11Texture2D* destination=outer.croppedTexture?outer.croppedTexture:outer.images[outer.index].texture;
        ID3D11RenderTargetView* target=outer.croppedRtv?outer.croppedRtv:outer.rtvs[outer.index];
        D3D11_TEXTURE2D_DESC dst{};destination->GetDesc(&dst);
        if(dst.Width==outer.width&&dst.Height==outer.height&&dst.ArraySize==src.ArraySize&&dst.MipLevels==src.MipLevels&&dst.SampleDesc.Count==1&&formatFamily(dst.Format)==formatFamily(src.Format)){
            bool preserveCopy=!shaderTransfer;
            if(!resized&&s.outerBlur>0&&outer_resample_detail::typed(src.Format)&&outer.index<outer.rtvs.size()&&outer.rtvs[outer.index]){
                D3D11_RENDER_TARGET_VIEW_DESC view{};target->GetDesc(&view);
                if(outer_resample_detail::srgb(src.Format)!=outer_resample_detail::srgb(view.Format)){
                    preserveCopy=true;
                    if(s.blurEncodingWarnings++<4||lmuqv::diagnostic_logging::periodic_frame())
                        logf("outer smoothing unavailable for source/RTV encoding mismatch; preserving unblurred copy eye=%u sourceFmt=%u rtvFmt=%u",eye,unsigned(src.Format),unsigned(view.Format));
                }
            }
            if(xrDiagnosticFrameLog())logf("outer transfer eye=%u source=%p sourceFmt=%u size=%ux%u destination=%p resourceFmt=%u size=%ux%u chain=%p image=%u operation=%s",eye,static_cast<void*>(source),unsigned(src.Format),src.Width,src.Height,static_cast<void*>(destination),unsigned(dst.Format),dst.Width,dst.Height,static_cast<void*>(outer.chain),outer.index,preserveCopy?"CopyResource":s.outerBlur>0?"shader-blur":"shader-resample");
            if(preserveCopy){s.context->CopyResource(destination,source);copied=SUCCEEDED(s.device->GetDeviceRemovedReason());}
            else if(outer.index<outer.rtvs.size())copied=outerResample.draw(s.context,source,target,outer.width,outer.height,s.outerBlur);
        }
    }
    s.captures[eye]=copied;
    if(xrDiagnosticFrameLog())logf("outer capture eye=%u copied=%d resized=%d output=%ux%u (image held for UI)",eye,copied,resized,outer.width,outer.height);
    return s.captures[eye];
}
} // namespace lmuqv

#ifdef OPENGRIDXR_LMU_INTERNAL
namespace og::lmu {
XrResult XRAPI_CALL get_proc(XrInstance instance,const char* name,PFN_xrVoidFunction* out) {
    return layerGetInstanceProcAddr(instance,name,out);
}
XrResult XRAPI_CALL create_instance(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* layer,XrInstance* instance) {
    return layerCreateApiLayerInstance(info,layer,instance);
}
}
#else
extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loader,const char* name,XrNegotiateApiLayerRequest* request) {
    if(!loader||!name||std::strcmp(name,layerName)||!request||
       loader->structType!=XR_LOADER_INTERFACE_STRUCT_LOADER_INFO||loader->structVersion!=XR_LOADER_INFO_STRUCT_VERSION||loader->structSize!=sizeof(XrNegotiateLoaderInfo)||
       request->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST||request->structVersion!=XR_API_LAYER_INFO_STRUCT_VERSION||request->structSize!=sizeof(XrNegotiateApiLayerRequest)||
       loader->minInterfaceVersion>1||loader->maxInterfaceVersion<1||loader->minApiVersion>XR_CURRENT_API_VERSION||loader->maxApiVersion<XR_MAKE_VERSION(1,0,0))return XR_ERROR_INITIALIZATION_FAILED;
    request->layerInterfaceVersion=1;request->layerApiVersion=(std::min)(loader->maxApiVersion,XR_CURRENT_API_VERSION);
    request->getInstanceProcAddr=layerGetInstanceProcAddr;request->createApiLayerInstance=layerCreateApiLayerInstance;
    logf("Loader negotiated API layer interface v1");return XR_SUCCESS;
}
#endif
