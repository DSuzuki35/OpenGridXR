// Fake runtime tests: no loader, headset, GPU or game attachment.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "xr_layer.hpp"
#include "xr_capture.hpp"
#include "xr_sizes.hpp"
#include "render_profile.hpp"
#include "async_log.hpp"
#include <thread>
#include <cmath>
#include <cassert>
#include <cstring>
#include <iostream>
#include <fstream>
#include <iterator>
#include <array>
#include <vector>

namespace lmuqv {
XrSwapchain nativeEyeChains[2]{};
unsigned engine_game_eye(XrSwapchain chain){
    for(unsigned eye=0;eye<2;++eye)if(chain&&chain==nativeEyeChains[eye])return eye;
    return 2;
}
bool engine_start(ID3D11DeviceContext*) { assert(false && "Test executable must never hook engine"); return false; }
unsigned stereoAuditFrameEnds=0;
unsigned stereoAuditPrepares=0;
bool stereo_audit_prepare(bool enable,const wchar_t* path,void(*)(const char*)){assert(enable&&path&&*path);++stereoAuditPrepares;return true;}
void stereo_audit_stop() {}
void stereo_audit_frame_end() {++stereoAuditFrameEnds;} // Verify wiring without installing hooks.
void engine_stop() {renderProfile.shutdown();}
ID3D11RenderTargetView* xr_outer_rtv(unsigned eye);
}
namespace {
bool chained = false, extensions = false;
bool fixedFoveation=false;
bool noEyeTracking=false;
bool failCapabilities=false;
bool failInstanceCreation=false;
bool rejectGazeExtension=false;
unsigned instanceCreateCalls=0;
const void* expectedLocateNext=nullptr;
bool runtimeShouldRender=true;
bool traceRuntime=false, failRelease=false;
bool srgbRuntime=false;
bool statsRequested=false, expectStatsBeforeRelease=false;
bool sceneStatsRequested=false;
bool captureRequested=false,captureMissing=false,captureInvalid=false;
bool movingFocusTest=false;
bool cropRequested=false,invalidCropRequested=false;
XrView renderedCropViews[4]{};
bool outerBlurRequested=false;
bool typedSrgbRuntime=false;
bool sampledInputsRequested=false,rejectSampledInputs=false;
bool profileOnCloseRequested=false,profileCloseOtherThread=false,profileMenuFrames=false;
unsigned sampledAttempts=0,sampledAccepted=0;
bool legacyDynamicRequested=false;
double focusBoxPercent=100;
double focusHeightPercent=100;
double focusOffsetX=0,focusOffsetY=0;
bool invalidFocusOffsetRequested=false;
bool asymmetricFocusRequested=false;
bool invalidFocusBoxRequested=false,invalidFocusFov=false,invalidOuterFov=false;
bool debugPhaseRequested=false,debugRuntime=false;
unsigned fixtureDimension=32;
bool scaledRequested=false;
bool runtimeSettings=false,runtimeEqual=false,runtimeAxisEqual=false,runtimeTooWide=false,runtimeTooTall=false;
unsigned runtimeSessionCreates=0;
bool overlayTestRequested=false,overlayTestActive=false;
XrSwapchain overlayLeft{},overlayRight{},overlayOuter0{},overlayOuter1{};
const XrCompositionLayerBaseHeader* expectedOverlay=nullptr;
unsigned expectedOverlayProjections=1;
unsigned runtimeOuterWidth(unsigned eye){return runtimeEqual||(runtimeAxisEqual&&eye==0)?fixtureDimension:(eye?fixtureDimension*5/8:fixtureDimension*3/4)+(runtimeTooWide?fixtureDimension:0);}
unsigned runtimeOuterHeight(unsigned eye){return runtimeEqual||(runtimeAxisEqual&&eye==1)?fixtureDimension:(eye?fixtureDimension*7/8:fixtureDimension/2)+(runtimeTooTall?fixtureDimension:0);}
bool invalidSizesRequested=false;
unsigned movingFocusSubmissions=0;
XrView renderedMovingViews[4]{};
std::filesystem::path captureDirectory,captureTrigger;
bool failAcquire=false;
struct Ownership { bool acquired=false, waited=false; };
std::vector<Ownership> ownership;
std::filesystem::path testBuildRoot() {
    wchar_t executable[32768]{};
    const auto length=GetModuleFileNameW(nullptr,executable,32768);
    assert(length && length<32768);
    return std::filesystem::path(executable).parent_path();
}
std::string currentLog() {
    // Test-only barrier: production flush_log is deliberately asynchronous.
    // The service can restart when the next adapter message is queued.
    lmuqv::stop_log();
    std::ifstream input(testBuildRoot()/"xr-layer-test-trace.log");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
bool traceLogsAccountedFor(const std::string& text,const lmuqv::AsyncLogStats& before,const lmuqv::AsyncLogStats& after) {
    unsigned missing=0;
    for(const char* marker:{"xrBeginFrame mode=trace frame=1","xrLocateViews mode=trace frame=1","xrEndFrame mode=trace frame=1"})
        if(text.find(marker)==std::string::npos)++missing;
    // Only intentional bounded-queue losses during this frame can explain a
    // missing marker. Earlier drops, pending writes and sink failures cannot.
    return after.accepted==after.written&&after.writeFailures==0&&after.flushFailures==0&&
        after.truncated==before.truncated&&after.droppedStopped==before.droppedStopped&&
        after.droppedUnavailable==before.droppedUnavailable&&
        after.droppedContention>=before.droppedContention&&after.droppedOverflow>=before.droppedOverflow&&
        missing<=after.droppedContention-before.droppedContention+after.droppedOverflow-before.droppedOverflow;
}
void traceLogAccountingTests() {
    const std::string complete="xrBeginFrame mode=trace frame=1\nxrLocateViews mode=trace frame=1\nxrEndFrame mode=trace frame=1\n";
    const std::string missingLocate="xrBeginFrame mode=trace frame=1\nxrEndFrame mode=trace frame=1\n";
    lmuqv::AsyncLogStats before{},after{};before.droppedContention=after.droppedContention=7;
    assert(traceLogsAccountedFor(complete,before,after));
    assert(!traceLogsAccountedFor(missingLocate,before,after)); // Old drops do not excuse a missing call.
    ++after.droppedContention;
    assert(traceLogsAccountedFor(missingLocate,before,after));
    assert(!traceLogsAccountedFor("",before,after)); // One drop cannot explain three missing calls.
    ++after.accepted;
    assert(!traceLogsAccountedFor(complete,before,after)); // Drain is mandatory even with every marker present.
    ++after.written;++after.writeFailures;
    assert(!traceLogsAccountedFor(complete,before,after));
}
XrResult waitSwapchainResult=XR_SUCCESS;
XrTime runtimeTime=123;
uint32_t submittedViews=999, submittedLayers=999, acquireCount=0, releaseCount=0;
uint32_t successfulReleases=0;
ID3D11Device* device=nullptr;
std::vector<ID3D11Texture2D*> textures;
std::vector<XrSwapchain> swapchains;
XrApiLayerNextInfo* expectedNext = nullptr;
XrInstance fakeInstance = reinterpret_cast<XrInstance>(1);
std::string frameCalls;
void assertFov(const XrFovf& fov,float left,float right,float up,float down) {
    assert(fov.angleLeft==left&&fov.angleRight==right&&fov.angleUp==up&&fov.angleDown==down);
}
void assertOuterFov(const XrFovf& actual,float left,float right,float up,float down) {
    if(!cropRequested){assertFov(actual,left,right,up,down);return;}
    const double l=std::tan(double(left)),r=std::tan(double(right));
    const double u=std::tan(double(up)),d=std::tan(double(down));
    const double al=std::tan(double(actual.angleLeft)),ar=std::tan(double(actual.angleRight));
    const double au=std::tan(double(actual.angleUp)),ad=std::tan(double(actual.angleDown));
    assert(std::abs((ar-al)/(r-l)-.75)<1e-6);
    assert(std::abs((au-ad)/(u-d)-.50)<1e-6);
    assert(std::abs(al+ar-l-r)<1e-6&&std::abs(au+ad-u-d)<1e-6);
}
void assertFocusContained(const XrView* views) {
    for(unsigned eye=0;eye<2;++eye){
        const auto& outer=views[eye].fov;const auto& focus=views[eye+2].fov;
        assert(focus.angleLeft>=outer.angleLeft-1e-6f&&focus.angleRight<=outer.angleRight+1e-6f);
        assert(focus.angleDown>=outer.angleDown-1e-6f&&focus.angleUp<=outer.angleUp+1e-6f);
        assert(focus.angleLeft<focus.angleRight&&focus.angleDown<focus.angleUp);
    }
}
unsigned expectedCropWidth(unsigned size){return cropRequested?(size*3+3)/4:size;}
unsigned expectedCropHeight(unsigned size){return cropRequested?(size+1)/2:size;}
// Catch angle-space scaling, moving the gaze center, double application, and
// submitting a different FOV from the one used by the engine.
void assertFocusFov(const XrFovf& actual,float left,float right,float up,float down,unsigned eye=0) {
    const XrFovf original{left,right,up,down};
    if(!cropRequested&&focusBoxPercent==100&&focusHeightPercent==100&&focusOffsetX==0&&focusOffsetY==0){assert(std::memcmp(&actual,&original,sizeof(original))==0);return;}
    const double l=std::tan(double(left)),r=std::tan(double(right));
    const double u=std::tan(double(up)),d=std::tan(double(down));
    const double al=std::tan(double(actual.angleLeft)),ar=std::tan(double(actual.angleRight));
    const double au=std::tan(double(actual.angleUp)),ad=std::tan(double(actual.angleDown));
    if(cropRequested){
        // Preserve focus span when it fits, then move its center only far enough
        // to fit. An oversized focus fills the available cropped outer extent.
        const double ol=std::tan(double(eye?-0.85f:-1.1f)),orr=std::tan(double(eye?1.0f:0.9f));
        const double od=std::tan(double(eye?-0.85f:-0.7f)),ou=std::tan(double(eye?0.75f:0.8f));
        const double halfW=(orr-ol)*.75/2,halfH=(ou-od)*.50/2;
        const double minX=(ol+orr)/2-halfW,maxX=(ol+orr)/2+halfW;
        const double minY=(od+ou)/2-halfH,maxY=(od+ou)/2+halfH;
        const double width=std::min((r-l)*focusBoxPercent/100,2*halfW);
        const double height=std::min((u-d)*focusHeightPercent/100,2*halfH);
        const double centerX=std::max(minX+width/2,std::min(maxX-width/2,(l+r)/2+(orr-ol)*focusOffsetX/100));
        const double centerY=std::max(minY+height/2,std::min(maxY-height/2,(u+d)/2+(ou-od)*focusOffsetY/100));
        assert(std::abs(ar-al-width)<1e-6&&std::abs(au-ad-height)<1e-6);
        assert(std::abs((al+ar)/2-centerX)<1e-6&&std::abs((au+ad)/2-centerY)<1e-6);
        return;
    }
    assert(std::abs((ar-al)/(r-l)-focusBoxPercent/100)<1e-6);
    assert(std::abs((au-ad)/(u-d)-focusHeightPercent/100)<1e-6);
    const double dx=(std::tan(double(eye?1.0f:0.9f))-std::tan(double(eye?-0.85f:-1.1f)))*focusOffsetX/100;
    const double dy=(std::tan(double(eye?0.75f:0.8f))-std::tan(double(eye?-0.85f:-0.7f)))*focusOffsetY/100;
    assert(std::abs((al+ar)-(l+r)-2*dx)<1e-6&&std::abs((au+ad)-(u+d)-2*dy)<1e-6);
}
void outputSizeTests(){
    auto standard=lmuqv::parse_output_sizes(nullptr,nullptr);assert(standard.valid&&standard.outerPercent==100&&standard.focusPercent==100);
    assert(lmuqv::focus_dimension(2387,100)==2387&&lmuqv::focus_dimension(2423,100)==2423);
    auto demo=lmuqv::parse_output_sizes("10","200");assert(demo.valid&&demo.outerPercent==10&&demo.focusPercent==200);
    assert(lmuqv::focus_dimension(2388,200)==3380&&lmuqv::focus_dimension(2424,200)==3432);
    assert(lmuqv::outer_dimension(3380,demo)==756&&lmuqv::outer_dimension(3432,demo)==768);
    for(auto value:{"NaN","inf","100junk","0x64","0","401","-10","1e9999"}){
        auto invalid=lmuqv::parse_output_sizes(value,"100");assert(!invalid.valid&&invalid.outerPercent==100&&invalid.focusPercent==100);
    }
    for(auto value:{"NaN","24.9","401","garbage"})assert(!lmuqv::parse_output_sizes("10",value).valid);
    assert(!lmuqv::parse_output_sizes("200","100").valid);
    assert(lmuqv::parse_output_sizes("1","25").valid&&lmuqv::parse_output_sizes("400","400").valid);
}
const std::string& testRunToken() {
    static const auto token=[] {
        LARGE_INTEGER ticks{};assert(QueryPerformanceCounter(&ticks));
        return std::to_string(GetCurrentProcessId())+"-"+std::to_string(ticks.QuadPart);
    }();
    return token;
}
void captureBmpTests() {
    namespace cap=lmuqv::capture_detail;
    auto directory=testBuildRoot()/("bmp-helper-"+testRunToken());
    assert(std::filesystem::create_directory(directory));
    auto trigger=directory/L"capture.request";
    auto config=cap::config(directory.wstring(),trigger.wstring());
    assert(config.requested&&config.valid&&!cap::triggered(config));
    unsigned char data[8*48]{};
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){auto* p=data+y*48+x*4;p[0]=10+x;p[1]=20+y;p[2]=30;p[3]=40+x+y;}
    DWORD error=0;
    assert(!cap::write_bmp(config,0,8,8,48,data,false,error));
    assert(!std::filesystem::exists(directory/L"outer0.bmp"));
    {std::ofstream request(trigger);request<<"explicit offscreen capture\n";}
    assert(cap::triggered(config));
    assert(cap::write_bmp(config,0,8,8,48,data,false,error));
    std::ifstream file(directory/L"outer0.bmp",std::ios::binary);
    std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    assert(bytes.size()==378);
    BITMAPFILEHEADER fh{};BITMAPV4HEADER vh{};
    std::memcpy(&fh,bytes.data(),sizeof(fh));std::memcpy(&vh,bytes.data()+sizeof(fh),sizeof(vh));
    assert(fh.bfType==0x4d42&&fh.bfSize==378&&fh.bfOffBits==122&&vh.bV4Size==108);
    assert(vh.bV4Width==8&&vh.bV4Height==-8&&vh.bV4BitCount==32&&vh.bV4Planes==1&&vh.bV4V4Compression==BI_BITFIELDS);
    assert(vh.bV4RedMask==0x00ff0000&&vh.bV4GreenMask==0x0000ff00&&vh.bV4BlueMask==0x000000ff&&vh.bV4AlphaMask==0xff000000);
    assert(bytes[122]==30&&bytes[123]==20&&bytes[124]==10&&bytes[125]==40);
    assert(bytes[154]==30&&bytes[155]==21&&bytes[156]==10&&bytes[157]==41);
    assert(bytes[374]==30&&bytes[375]==27&&bytes[376]==17&&bytes[377]==54);
    assert(!cap::write_bmp(config,0,8,8,48,data,false,error)&&error==ERROR_FILE_EXISTS);
    assert(cap::write_bmp(config,1,8,8,48,data,true,error));
    std::ifstream bgraFile(directory/L"outer1.bmp",std::ios::binary);
    std::vector<unsigned char> bgraBytes{std::istreambuf_iterator<char>(bgraFile),std::istreambuf_iterator<char>()};
    assert(bgraBytes[122]==10&&bgraBytes[123]==20&&bgraBytes[124]==30&&bgraBytes[125]==40);
    auto relative=cap::config(L"relative-output",trigger.wstring());assert(relative.requested&&!relative.valid);
    auto absent=cap::config((directory/L"does-not-exist").wstring(),trigger.wstring());assert(absent.requested&&!absent.valid);
    auto invalidTrigger=cap::config(directory.wstring(),L"relative.request");assert(!invalidTrigger.valid);
    assert(!cap::write_bmp(relative,2,8,8,48,data,false,error));
    assert(!cap::write_bmp(absent,2,8,8,48,data,false,error));
    assert(!cap::write_bmp(config,4,8,8,48,data,false,error));
    assert(!cap::write_bmp(config,2,8,8,16,data,false,error));
    assert(!std::filesystem::exists(directory/L"does-not-exist"));
    assert(!std::filesystem::exists(directory/L"app0.bmp"));
}
XrResult XRAPI_CALL generic() { return XR_SUCCESS; }
XrResult XRAPI_CALL createSession(XrInstance,const XrSessionCreateInfo*,XrSession* out) {
    ++runtimeSessionCreates;*out=reinterpret_cast<XrSession>(2);return XR_SUCCESS;
}
XrResult XRAPI_CALL beginSession(XrSession,const XrSessionBeginInfo* in) {
    assert(in->primaryViewConfigurationType==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO);return XR_SUCCESS;
}
XrResult XRAPI_CALL waitFrame(XrSession,const XrFrameWaitInfo*,XrFrameState* out) {
    frameCalls+='W';
    out->predictedDisplayTime=runtimeTime;out->shouldRender=runtimeShouldRender?XR_TRUE:XR_FALSE;return XR_SUCCESS;
}
XrResult XRAPI_CALL beginFrame(XrSession,const XrFrameBeginInfo*) { frameCalls+='B';return XR_SUCCESS; }
XrResult XRAPI_CALL createSwapchain(XrSession,const XrSwapchainCreateInfo* in,XrSwapchain* out) {
    if(in->usageFlags&XR_SWAPCHAIN_USAGE_SAMPLED_BIT){
        ++sampledAttempts;
        if(rejectSampledInputs)return XR_ERROR_FEATURE_UNSUPPORTED;
        ++sampledAccepted;
    }
    // Both private outer chains must match actual app dimensions, format and sample count.
    bool own=textures.size()==1||textures.size()==2;
    unsigned expected=scaledRequested&&own?(fixtureDimension==32?8:29):fixtureDimension;
    unsigned expectedW=runtimeSettings&&own?runtimeOuterWidth(unsigned(textures.size()-1)):expected;
    unsigned expectedH=runtimeSettings&&own?runtimeOuterHeight(unsigned(textures.size()-1)):expected;
    // QVFR composites peripheral images against its original, uncropped FOV.
    // Cropped rendering must be padded back into this full-size output.
    assert(overlayTestActive||(in->width==expectedW&&in->height==expectedH&&in->sampleCount==1&&in->format==(srgbRuntime?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM)));
    D3D11_TEXTURE2D_DESC desc{};desc.Width=in->width;desc.Height=in->height;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=srgbRuntime?(typedSrgbRuntime?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_TYPELESS):DXGI_FORMAT(in->format);desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|((scaledRequested||runtimeSettings||cropRequested)?D3D11_BIND_SHADER_RESOURCE:0);
    if(in->usageFlags&XR_SWAPCHAIN_USAGE_SAMPLED_BIT)desc.BindFlags|=D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* texture=nullptr;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&texture)));
    *out=reinterpret_cast<XrSwapchain>(textures.size()+10);textures.push_back(texture);swapchains.push_back(*out);ownership.emplace_back();return XR_SUCCESS;
}
XrResult XRAPI_CALL enumerateImages(XrSwapchain chain,uint32_t cap,uint32_t* count,XrSwapchainImageBaseHeader* out) {
    *count=1;if(cap){assert(out->type==XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR);reinterpret_cast<XrSwapchainImageD3D11KHR*>(out)->texture=textures[reinterpret_cast<uintptr_t>(chain)-10];}return XR_SUCCESS;
}
XrResult XRAPI_CALL acquire(XrSwapchain chain,const XrSwapchainImageAcquireInfo*,uint32_t* index){
    if(failAcquire)return XR_ERROR_RUNTIME_FAILURE;
    auto& state=ownership[reinterpret_cast<uintptr_t>(chain)-10];assert(!state.acquired);state.acquired=true;state.waited=false;
    ++acquireCount;*index=0;return XR_SUCCESS;
}
XrResult XRAPI_CALL waitSwapchain(XrSwapchain chain,const XrSwapchainImageWaitInfo*){
    auto& state=ownership[reinterpret_cast<uintptr_t>(chain)-10];assert(state.acquired);
    if(waitSwapchainResult==XR_SUCCESS)state.waited=true;
    return waitSwapchainResult;
}
uint32_t texturePixel(ID3D11Texture2D* texture,unsigned x,unsigned y){
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging=nullptr;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)));
    ID3D11DeviceContext* context=nullptr;device->GetImmediateContext(&context);context->CopyResource(staging,texture);
    D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&map)));
    uint32_t pixel=0;std::memcpy(&pixel,static_cast<const unsigned char*>(map.pData)+size_t(y)*map.RowPitch+x*4,4);
    context->Unmap(staging,0);context->Release();staging->Release();return pixel;
}
XrResult XRAPI_CALL release(XrSwapchain chain,const XrSwapchainImageReleaseInfo*){
    unsigned ordinal=unsigned(reinterpret_cast<uintptr_t>(chain)-10);auto& state=ownership[ordinal];
    if(!state.acquired||!state.waited)return XR_ERROR_CALL_ORDER_INVALID;
    if(debugRuntime&&!overlayTestActive){
        unsigned x=3;
        if(ordinal==1)x=scaledRequested?(runtimeTime==701?7:11):(runtimeTime==701?33:49);
        if(ordinal==2)x=scaledRequested?(runtimeTime==701?11:3):(runtimeTime==701?49:17);
        // Hand-derived pixels for the 50%/200% tangent extents and existing inset border.
        if(focusBoxPercent==50){
            if(ordinal==1)x=scaledRequested?(runtimeTime==701?11:14):(runtimeTime==701?48:64);
            if(ordinal==2)x=scaledRequested?(runtimeTime==701?14:7):(runtimeTime==701?64:32);
        }
        if(focusBoxPercent==200){
            if(ordinal==1)x=scaledRequested?(runtimeTime==701?0:4):(runtimeTime==701?3:19);
            if(ordinal==2)x=scaledRequested?(runtimeTime==701?4:24):(runtimeTime==701?19:109);
        }
        D3D11_TEXTURE2D_DESC debugDesc{};textures[ordinal]->GetDesc(&debugDesc);
        if(runtimeSettings&&(ordinal==1||ordinal==2)){
            const unsigned fraction=ordinal==1?(runtimeTime==701?4:6):(runtimeTime==701?6:2);
            x=debugDesc.Width*fraction/16+1;
        }
        if(ordinal==1||ordinal==2)x+=unsigned(debugDesc.Width*focusOffsetX/100);
        auto border=texturePixel(textures[ordinal],x,debugDesc.Height/2);
        assert(((border>>8)&255)==255&&((border>>16)&255)==0&&(border&255)>80); // Must be painted before runtime Release.
        unsigned interior=debugDesc.Width/2;
        if(ordinal==1&&runtimeTime==702)interior=debugDesc.Width*5/8;
        if(ordinal==2)interior=debugDesc.Width*(runtimeTime==701?5:3)/8;
        if(ordinal==1||ordinal==2)interior+=unsigned(debugDesc.Width*focusOffsetX/100);
        assert(texturePixel(textures[ordinal],interior,debugDesc.Height/2)==0x7f0a2040u);
        if(runtimeSettings&&(ordinal==1||ordinal==2)){
            // Runtime vertical tangent extent is +/- .5; outer extent is +/- 1.
            // 2.3% inset lands in row 16/29/33 at heights 64/112/128.
            const unsigned topRow=(debugDesc.Height==64?16:debugDesc.Height==112?29:33)+unsigned(debugDesc.Height*(-focusOffsetY)/100);
            auto top=texturePixel(textures[ordinal],interior,topRow);
            assert(((top>>8)&255)==255&&((top>>16)&255)==0&&(top&255)>80);
        }
        if(asymmetricFocusRequested&&(ordinal==1||ordinal==2)){
            // 150% height gives tangent top .75, independently of the 50% width.
            const auto topBorder=texturePixel(textures[ordinal],interior,scaledRequested?4:18);
            assert(((topBorder>>8)&255)==255&&((topBorder>>16)&255)==0&&(topBorder&255)>80);
        }
    }
    if(expectStatsBeforeRelease&&statsRequested){
        const char* slots[]={"slot=app0","slot=outer0","slot=outer1","slot=app1"};
        assert(currentLog().find(slots[ordinal])!=std::string::npos);
    }
    ++releaseCount;if(failRelease)return XR_ERROR_RUNTIME_FAILURE;
    ++successfulReleases;state={};return XR_SUCCESS;
}
void assertCopiedPixels(ID3D11Texture2D* destination) {
    D3D11_TEXTURE2D_DESC desc{};destination->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;
    desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ID3D11Texture2D* staging=nullptr;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)));
    ID3D11DeviceContext* context=nullptr;device->GetImmediateContext(&context);context->CopyResource(staging,destination);
    D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&map)));
    assert(*static_cast<const uint32_t*>(map.pData)==0x7f0a2040u);
    context->Unmap(staging,0);context->Release();staging->Release();
}
void assertRenderPixels(unsigned eye){
    auto target=lmuqv::xr_outer_target(eye);assert(target.rtv);
    ID3D11Resource* resource=nullptr;target.rtv->GetResource(&resource);
    ID3D11Texture2D* texture=nullptr;
    assert(SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture))));
    assertCopiedPixels(texture);texture->Release();resource->Release();
}
void assertPaddedPixels(unsigned eye){
    if(!cropRequested)return;
    auto* texture=textures[eye+1];D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    assert(texturePixel(texture,desc.Width/2,desc.Height/2)==0x7f0a2040u);
    assert(texturePixel(texture,desc.Width/2,0)==0xff000000u);
    assert(texturePixel(texture,desc.Width/2,desc.Height-1)==0xff000000u);
    assert(texturePixel(texture,0,desc.Height/2)==0xff000000u);
    assert(texturePixel(texture,desc.Width-1,desc.Height/2)==0xff000000u);
}
XrResult XRAPI_CALL endFrame(XrSession,const XrFrameEndInfo* in) {
    frameCalls+='E';
    if(overlayTestActive){
        assert(in->layerCount==expectedOverlayProjections+1);
        assert(in->layers[in->layerCount-1]==expectedOverlay);
        if(expectedOverlayProjections){
            const auto* projection=reinterpret_cast<const XrCompositionLayerProjection*>(in->layers[0]);
            assert(projection->viewCount==4);
            assert(projection->views[0].subImage.swapchain==overlayOuter0&&projection->views[1].subImage.swapchain==overlayOuter1);
            assert(projection->views[2].subImage.swapchain==overlayLeft&&projection->views[3].subImage.swapchain==overlayRight);
        }
        return XR_SUCCESS;
    }
    submittedLayers=in->layerCount;submittedViews=0;
    for(uint32_t i=0;i<in->layerCount;++i) if(in->layers[i]->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION){
        const auto* p=reinterpret_cast<const XrCompositionLayerProjection*>(in->layers[i]);submittedViews=p->viewCount;
        assert(p->viewCount==4&&p->views[0].subImage.swapchain==swapchains[1]&&p->views[1].subImage.swapchain==swapchains[2]);
        assert(p->views[2].subImage.swapchain==swapchains[0]&&p->views[3].subImage.swapchain==swapchains[3]);
        assert(p->views[0].pose.position.x==10&&p->views[2].pose.position.x==(debugRuntime?10:12));
        unsigned outerSize=scaledRequested?(fixtureDimension==32?8:29):fixtureDimension;
        for(unsigned eye=0;eye<2;++eye){
            assert(p->views[eye].subImage.imageRect.extent.width==int(runtimeSettings?runtimeOuterWidth(eye):outerSize));
            assert(p->views[eye].subImage.imageRect.extent.height==int(runtimeSettings?runtimeOuterHeight(eye):outerSize));
        }
        assert(p->views[2].subImage.imageRect.extent.width==int(fixtureDimension)&&p->views[3].subImage.imageRect.extent.height==int(fixtureDimension));
        if(cropRequested){
            for(unsigned view=2;view<4;++view)
                assert(std::memcmp(&p->views[view].fov,&renderedCropViews[view].fov,sizeof(XrFovf))==0);
        }
        if(movingFocusTest){
            for(unsigned eye=0;eye<4;++eye){
                if(eye>=2||!cropRequested)assert(std::memcmp(&p->views[eye].fov,&renderedMovingViews[eye].fov,sizeof(XrFovf))==0);
                assert(std::memcmp(&p->views[eye].pose,&renderedMovingViews[eye].pose,sizeof(XrPosef))==0);
            }
            assertFov(p->views[0].fov,-1.1f,0.9f,0.8f,-0.7f);
            assertFov(p->views[1].fov,-0.85f,1.0f,0.75f,-0.85f);
            if(in->displayTime==501){
                assertFocusFov(p->views[2].fov,-0.30f,0.20f,0.25f,-0.15f);
                assertFocusFov(p->views[3].fov,-0.15f,0.35f,0.20f,-0.30f,1);
            }else{
                assert(in->displayTime==502);
                assertFocusFov(p->views[2].fov,-0.05f,0.45f,0.35f,-0.10f);
                assertFocusFov(p->views[3].fov,-0.40f,0.10f,0.15f,-0.35f,1);
            }
            ++movingFocusSubmissions;
        }
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL views(XrInstance, XrSystemId, XrViewConfigurationType type, uint32_t cap,
                        uint32_t* count, XrViewConfigurationView* out) {
    assert(type == (traceRuntime?XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO:XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO));
    *count = traceRuntime?2:4;
    if (!cap) return XR_SUCCESS;
    if(cap<*count)return XR_ERROR_SIZE_INSUFFICIENT;
    for (unsigned i = 0; i != *count; ++i) {
        if(!traceRuntime){
            auto* f=static_cast<const XrFoveatedViewConfigurationViewVARJO*>(out[i].next);
            assert(f&&f->type==XR_TYPE_FOVEATED_VIEW_CONFIGURATION_VIEW_VARJO);
            assert(f->foveatedRenderingActive==((fixedFoveation||noEyeTracking)?XR_FALSE:XR_TRUE));
        }
        out[i].recommendedImageRectWidth = i == 3 ? 1400 : 1200;
        out[i].recommendedImageRectHeight = i == 2 ? 1600 : 1300;
        if(runtimeSettings&&!traceRuntime){
            out[i].recommendedImageRectWidth=i<2?runtimeOuterWidth(i):fixtureDimension-(i==2?1:0);
            out[i].recommendedImageRectHeight=i<2?runtimeOuterHeight(i):fixtureDimension-(i==3?1:0);
        }
        out[i].maxImageRectWidth = out[i].maxImageRectHeight = 4096;
        out[i].recommendedSwapchainSampleCount = 1;
        out[i].maxSwapchainSampleCount = 4;
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL locate(XrSession, const XrViewLocateInfo* in, XrViewState* state,
                          uint32_t cap, uint32_t* count, XrView* out) {
    frameCalls+='L';
    assert(in->viewConfigurationType == (traceRuntime?XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO:XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO));
    assert(cap == (traceRuntime?2u:4u));
    auto f = static_cast<const XrViewLocateFoveatedRenderingVARJO*>(in->next);
    if(!traceRuntime){assert(f && f->type == XR_TYPE_VIEW_LOCATE_FOVEATED_RENDERING_VARJO);assert(f->foveatedRenderingActive==((fixedFoveation||noEyeTracking)?XR_FALSE:XR_TRUE));assert(f->next==expectedLocateNext);}
    else assert(in->next==expectedLocateNext);
    *count = cap;
    state->viewStateFlags = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    for (unsigned i=0; i!=cap; ++i) {out[i].pose.position.x = float(i+10);out[i].pose.orientation.w=1;out[i].fov={-.4f,.4f,.4f,-.4f};}
    if(movingFocusTest){
        // Simulated per-frame focus movement, independently asymmetric for the two eyes.
        out[0].fov={-1.1f,0.9f,0.8f,-0.7f};out[1].fov={-0.85f,1.0f,0.75f,-0.85f};
        if(in->displayTime==501){out[2].fov={-0.30f,0.20f,0.25f,-0.15f};out[3].fov={-0.15f,0.35f,0.20f,-0.30f};}
        else if(in->displayTime==502){out[2].fov={-0.05f,0.45f,0.35f,-0.10f};out[3].fov={-0.40f,0.10f,0.15f,-0.35f};}
        else {assert(in->displayTime==503);out[2].fov={-0.50f,0.10f,0.40f,-0.20f};out[3].fov={-0.10f,0.50f,0.30f,-0.10f};}
    }
    if(debugRuntime){
        for(unsigned i=0;i<4;++i)out[i].pose.orientation.w=1;
        out[2].pose=out[0].pose;out[3].pose=out[1].pose;
        const float a=std::atan(1.f),h=std::atan(.5f);
        out[0].fov=out[1].fov={-a,a,a,-a};
        if(in->displayTime==701){out[2].fov={-h,h,h,-h};out[3].fov={std::atan(-.25f),std::atan(.75f),h,-h};}
        else{assert(in->displayTime==702);out[2].fov={std::atan(-.25f),std::atan(.75f),h,-h};out[3].fov={std::atan(-.75f),std::atan(.25f),h,-h};}
    }
    if(invalidOuterFov)out[1].fov.angleDown=out[1].fov.angleUp;
    if(invalidFocusFov)out[3].fov.angleLeft=out[3].fov.angleRight;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL systemProperties(XrInstance,XrSystemId,XrSystemProperties* props){
    if(failCapabilities)return XR_ERROR_RUNTIME_FAILURE;
    assert(props&&props->type==XR_TYPE_SYSTEM_PROPERTIES);
    auto* f=static_cast<XrSystemFoveatedRenderingPropertiesVARJO*>(props->next);
    assert(f&&f->type==XR_TYPE_SYSTEM_FOVEATED_RENDERING_PROPERTIES_VARJO);
    f->supportsFoveatedRendering=noEyeTracking?XR_FALSE:XR_TRUE;return XR_SUCCESS;
}
XrResult XRAPI_CALL get(XrInstance, const char* name, PFN_xrVoidFunction* out) {
    *out = reinterpret_cast<PFN_xrVoidFunction>(generic);
    if (!std::strcmp(name,"xrGetSystemProperties")) *out = reinterpret_cast<PFN_xrVoidFunction>(systemProperties);
    if (!std::strcmp(name,"xrEnumerateViewConfigurationViews")) *out = reinterpret_cast<PFN_xrVoidFunction>(views);
    if (!std::strcmp(name,"xrLocateViews")) *out = reinterpret_cast<PFN_xrVoidFunction>(locate);
    if(!std::strcmp(name,"xrCreateSession"))*out=reinterpret_cast<PFN_xrVoidFunction>(createSession);
    if(!std::strcmp(name,"xrBeginSession"))*out=reinterpret_cast<PFN_xrVoidFunction>(beginSession);
    if(!std::strcmp(name,"xrWaitFrame"))*out=reinterpret_cast<PFN_xrVoidFunction>(waitFrame);
    if(!std::strcmp(name,"xrBeginFrame"))*out=reinterpret_cast<PFN_xrVoidFunction>(beginFrame);
    if(!std::strcmp(name,"xrCreateSwapchain"))*out=reinterpret_cast<PFN_xrVoidFunction>(createSwapchain);
    if(!std::strcmp(name,"xrEnumerateSwapchainImages"))*out=reinterpret_cast<PFN_xrVoidFunction>(enumerateImages);
    if(!std::strcmp(name,"xrAcquireSwapchainImage"))*out=reinterpret_cast<PFN_xrVoidFunction>(acquire);
    if(!std::strcmp(name,"xrWaitSwapchainImage"))*out=reinterpret_cast<PFN_xrVoidFunction>(waitSwapchain);
    if(!std::strcmp(name,"xrReleaseSwapchainImage"))*out=reinterpret_cast<PFN_xrVoidFunction>(release);
    if(!std::strcmp(name,"xrEndFrame"))*out=reinterpret_cast<PFN_xrVoidFunction>(endFrame);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL create(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* layer, XrInstance* out) {
    ++instanceCreateCalls;
    chained = layer->nextInfo == expectedNext;
    bool quad=false, foveated=false;
    for (uint32_t i=0;i<info->enabledExtensionCount;++i) {
        if(rejectGazeExtension&&!std::strcmp(info->enabledExtensionNames[i],XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME))return XR_ERROR_EXTENSION_NOT_PRESENT;
        quad |= !std::strcmp(info->enabledExtensionNames[i], XR_VARJO_QUAD_VIEWS_EXTENSION_NAME);
        foveated |= !std::strcmp(info->enabledExtensionNames[i], XR_VARJO_FOVEATED_RENDERING_EXTENSION_NAME);
    }
    extensions=quad && foveated;
    if(failInstanceCreation)return XR_ERROR_INITIALIZATION_FAILED;
    *out=fakeInstance;
    return XR_SUCCESS;
}
}
int main(int argc,char** argv) {
    bool diagnosticsOff=false;for(int arg=1;arg<argc;++arg)diagnosticsOff|=!std::strcmp(argv[arg],"--diagnostics-off");
    if(diagnosticsOff)SetEnvironmentVariableA("LMU_QV_DIAGNOSTICS","0");
    traceLogAccountingTests();
    outputSizeTests();
    captureBmpTests();
    const auto logPath=testBuildRoot()/L"xr-layer-test-trace.log";
    SetEnvironmentVariableA("LMU_QV_STEREO_AUDIT","1");SetEnvironmentVariableW(L"LMU_QV_STEREO_AUDIT_DIR",testBuildRoot().c_str());
    DeleteFileW(logPath.c_str());SetEnvironmentVariableW(L"LMU_QV_LOG",logPath.c_str());
    if(diagnosticsOff){lmuqv::log_line("must not create a log");lmuqv::flush_log();assert(GetFileAttributesW(logPath.c_str())==INVALID_FILE_ATTRIBUTES);}
    for(int arg=1;arg<argc;++arg){
        profileOnCloseRequested|=!std::strcmp(argv[arg],"--profile-on-close");
        profileMenuFrames|=!std::strcmp(argv[arg],"--profile-menu-frames");
        profileCloseOtherThread|=!std::strcmp(argv[arg],"--profile-close-other-thread");
        legacyDynamicRequested|=!std::strcmp(argv[arg],"--legacy-dynamic-env");
        sampledInputsRequested|=!std::strcmp(argv[arg],"--sampled-inputs");
        rejectSampledInputs|=!std::strcmp(argv[arg],"--reject-sampled-inputs");
        outerBlurRequested|=!std::strcmp(argv[arg],"--outer-blur");
        typedSrgbRuntime|=!std::strcmp(argv[arg],"--typed-srgb-runtime");
        fixedFoveation|=!std::strcmp(argv[arg],"--fixed-foveation");
        noEyeTracking|=!std::strcmp(argv[arg],"--no-eye-tracking");
        srgbRuntime|=!std::strcmp(argv[arg],"--srgb");statsRequested|=!std::strcmp(argv[arg],"--stats");sceneStatsRequested|=!std::strcmp(argv[arg],"--scene-stats");
        captureRequested|=!std::strcmp(argv[arg],"--capture");captureMissing|=!std::strcmp(argv[arg],"--capture-no-trigger");captureInvalid|=!std::strcmp(argv[arg],"--capture-invalid");
        debugPhaseRequested|=!std::strcmp(argv[arg],"--debug-focus");
        if(!std::strcmp(argv[arg],"--focus-offset")){focusOffsetX=12.5;focusOffsetY=-6.25;}
        invalidFocusOffsetRequested|=!std::strcmp(argv[arg],"--invalid-focus-offset");
        if(!std::strcmp(argv[arg],"--focus-box"))focusBoxPercent=50;
        if(!std::strcmp(argv[arg],"--focus-box-large"))focusBoxPercent=200;
        invalidFocusBoxRequested|=!std::strcmp(argv[arg],"--invalid-focus-box");
        asymmetricFocusRequested|=!std::strcmp(argv[arg],"--focus-asymmetric");
        cropRequested|=!std::strcmp(argv[arg],"--crop");
        invalidCropRequested|=!std::strcmp(argv[arg],"--invalid-crop");
        scaledRequested|=!std::strcmp(argv[arg],"--scaled");
        invalidSizesRequested|=!std::strcmp(argv[arg],"--invalid-sizes");
        runtimeSettings|=!std::strcmp(argv[arg],"--runtime-settings");
        overlayTestRequested|=!std::strcmp(argv[arg],"--runtime-overlay");
        runtimeEqual|=!std::strcmp(argv[arg],"--runtime-equal");
        runtimeAxisEqual|=!std::strcmp(argv[arg],"--runtime-axis-equal");
        runtimeTooWide|=!std::strcmp(argv[arg],"--runtime-too-wide");
        runtimeTooTall|=!std::strcmp(argv[arg],"--runtime-too-tall");
    }
    captureRequested|=captureMissing||captureInvalid;sceneStatsRequested|=captureRequested;
    srgbRuntime|=typedSrgbRuntime;
    statsRequested|=sceneStatsRequested;
    if(captureRequested){
        captureDirectory=testBuildRoot()/("capture-dump-"+testRunToken());
        captureTrigger=testBuildRoot()/("capture-trigger-"+testRunToken()+".request");
        if(!captureInvalid)assert(std::filesystem::create_directory(captureDirectory));
        else {std::ofstream request(captureTrigger);request<<"invalid destination must not read or write\n";}
        SetEnvironmentVariableW(L"LMU_QV_CAPTURE_DIR",captureDirectory.c_str());SetEnvironmentVariableW(L"LMU_QV_CAPTURE_TRIGGER",captureTrigger.c_str());
    }else{SetEnvironmentVariableW(L"LMU_QV_CAPTURE_DIR",nullptr);SetEnvironmentVariableW(L"LMU_QV_CAPTURE_TRIGGER",nullptr);}
    SetEnvironmentVariableA("LMU_QV_PIXEL_STATS",sceneStatsRequested?"scene":statsRequested?"1":nullptr);
    if(diagnosticsOff){SetEnvironmentVariableA("LMU_QV_PIXEL_STATS","scene");SetEnvironmentVariableA("LMU_QV_PROFILE","1");}
    SetEnvironmentVariableA("LMU_QV_DEBUG_FOCUS",nullptr);
    SetEnvironmentVariableA("LMU_QV_MODE", "quad");
    SetEnvironmentVariableA("LMU_QV_FOVEATION_MODE",fixedFoveation?"fixed":nullptr);
    SetEnvironmentVariableA("LMU_QV_OUTER_BLUR",outerBlurRequested?"1":nullptr);
    if(invalidCropRequested)cropRequested=false;
    SetEnvironmentVariableA("LMU_QV_CROP_WIDTH_PERCENT",invalidCropRequested?"NaN":cropRequested?"75":nullptr);
    SetEnvironmentVariableA("LMU_QV_CROP_HEIGHT_PERCENT",invalidCropRequested?"50junk":cropRequested?"50":nullptr);
    if(invalidSizesRequested)scaledRequested=false;
    SetEnvironmentVariableA("LMU_QV_OUTER_PIXELS_PERCENT",invalidSizesRequested?"NaN":scaledRequested?"10":nullptr);
    SetEnvironmentVariableA("LMU_QV_FOCUS_PIXELS_PERCENT",invalidSizesRequested||scaledRequested?"200":nullptr);
    if(invalidFocusBoxRequested)focusBoxPercent=100;
    SetEnvironmentVariableA("LMU_QV_FOCUS_OFFSET_X_PERCENT",invalidFocusOffsetRequested?"NaN":focusOffsetX?"12.5":nullptr);
    SetEnvironmentVariableA("LMU_QV_FOCUS_OFFSET_Y_PERCENT",invalidFocusOffsetRequested?"999":focusOffsetY?"-6.25":nullptr);
    SetEnvironmentVariableA("LMU_QV_FOCUS_BOX_PERCENT",invalidFocusBoxRequested?"NaN":focusBoxPercent==50?"50":focusBoxPercent==200?"200":nullptr);
    focusHeightPercent=focusBoxPercent;
    if(asymmetricFocusRequested){focusBoxPercent=50;focusHeightPercent=150;}
    SetEnvironmentVariableA("LMU_QV_FOCUS_WIDTH_PERCENT",asymmetricFocusRequested?"50":nullptr);
    SetEnvironmentVariableA("LMU_QV_FOCUS_HEIGHT_PERCENT",asymmetricFocusRequested?"150":nullptr);
    runtimeSettings|=runtimeEqual||runtimeAxisEqual||runtimeTooWide||runtimeTooTall||overlayTestRequested;
    SetEnvironmentVariableA("LMU_QV_USE_RUNTIME_SETTINGS",runtimeSettings?"1":nullptr);
    sampledInputsRequested|=rejectSampledInputs;
    SetEnvironmentVariableA("LMU_QV_VECTORXR_DIRECT_INPUTS",sampledInputsRequested?"1":nullptr);
    if(runtimeSettings){
        // Stale legacy controls must not override the runtime's exact FOV or sizes.
        SetEnvironmentVariableA("LMU_QV_FOCUS_BOX_PERCENT","200");
        SetEnvironmentVariableA("LMU_QV_FOCUS_WIDTH_PERCENT","50");
        SetEnvironmentVariableA("LMU_QV_FOCUS_HEIGHT_PERCENT","150");
        SetEnvironmentVariableA("LMU_QV_OUTER_PIXELS_PERCENT","10");
        SetEnvironmentVariableA("LMU_QV_FOCUS_PIXELS_PERCENT","200");
        focusBoxPercent=focusHeightPercent=100;
    }
    SetEnvironmentVariableA("LMU_QV_DYNAMIC_FOCUS",legacyDynamicRequested?"1":nullptr);
    SetEnvironmentVariableA("LMU_QV_DYNAMIC_FOCUS_MIN","80");
    SetEnvironmentVariableA("LMU_QV_DYNAMIC_FOCUS_MAX","80");
    XrNegotiateLoaderInfo loader{XR_LOADER_INTERFACE_STRUCT_LOADER_INFO,1,sizeof(XrNegotiateLoaderInfo),1,1,XR_MAKE_VERSION(1,0,0),XR_CURRENT_API_VERSION};
    XrNegotiateApiLayerRequest req{};
    req.structType=XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST;req.structVersion=1;req.structSize=sizeof(req);
    assert(xrNegotiateLoaderApiLayerInterface(&loader,"XR_APILAYER_LMU_quadview",&req)==XR_SUCCESS);
    assert(req.layerInterfaceVersion == 1 && req.createApiLayerInstance && req.getInstanceProcAddr);
    loader.maxInterfaceVersion=0;
    assert(xrNegotiateLoaderApiLayerInterface(&loader,"XR_APILAYER_LMU_quadview",&req)==XR_ERROR_INITIALIZATION_FAILED);
    XrApiLayerNextInfo tail{}; expectedNext=&tail;
    XrApiLayerNextInfo next{XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO,1,sizeof(XrApiLayerNextInfo),{},get,create,&tail};
    std::strcpy(next.layerName,"XR_APILAYER_LMU_quadview");
    XrApiLayerCreateInfo chain{XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO,1,sizeof(XrApiLayerCreateInfo),nullptr,{},&next};
    XrInstanceCreateInfo info{}; info.type=XR_TYPE_INSTANCE_CREATE_INFO;
    XrInstance instance{};
    for(const char* invalid:{"NaN","inf","-1","3.1","1junk","0x1","1,5"}){
        SetEnvironmentVariableA("LMU_QV_OUTER_BLUR",invalid);
        assert(req.createApiLayerInstance(&info,&chain,&instance)==XR_ERROR_VALIDATION_FAILURE&&instanceCreateCalls==0);
    }
    SetEnvironmentVariableA("LMU_QV_OUTER_BLUR",outerBlurRequested?"1":nullptr);
    SetEnvironmentVariableA("LMU_QV_FOVEATION_MODE","invalid");
    assert(req.createApiLayerInstance(&info,&chain,&instance)==XR_ERROR_VALIDATION_FAILURE&&instanceCreateCalls==0);
    SetEnvironmentVariableA("LMU_QV_FOVEATION_MODE","xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    assert(req.createApiLayerInstance(&info,&chain,&instance)==XR_ERROR_VALIDATION_FAILURE&&instanceCreateCalls==0);
    SetEnvironmentVariableA("LMU_QV_FOVEATION_MODE",fixedFoveation?"fixed":nullptr);
    failInstanceCreation=true;
    assert(req.createApiLayerInstance(&info,&chain,&instance)==XR_ERROR_INITIALIZATION_FAILED);
    failInstanceCreation=false;
    assert(req.createApiLayerInstance(&info,&chain,&instance)==XR_SUCCESS && chained && extensions);
    assert(lmuqv::stereoAuditPrepares==(diagnosticsOff?0u:1u));
    PFN_xrVoidFunction fn{};
    assert(req.getInstanceProcAddr(instance,"xrEnumerateViewConfigurationViews",&fn)==XR_SUCCESS);
    auto enumerate=reinterpret_cast<PFN_xrEnumerateViewConfigurationViews>(fn);
    struct { XrViewConfigurationView views[2]; uint64_t guard; } bounded{};
    bounded.views[0].type=bounded.views[1].type=XR_TYPE_VIEW_CONFIGURATION_VIEW;
    bounded.guard=0x123456789abcdef0ull;
    uint32_t count=0;
    assert(enumerate(instance,1,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr)==XR_SUCCESS && count==2);
    assert(enumerate(instance,1,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,1,&count,bounded.views)==XR_ERROR_SIZE_INSUFFICIENT && count==2);
    if(!fixedFoveation){
        failCapabilities=true;
        assert(enumerate(instance,1,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,bounded.views)==XR_ERROR_RUNTIME_FAILURE);
        failCapabilities=false;
    }
    if(runtimeTooWide||runtimeTooTall){
        assert(enumerate(instance,1,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,bounded.views)==XR_ERROR_LIMIT_REACHED);
        PFN_xrVoidFunction createFn{};req.getInstanceProcAddr(instance,"xrCreateSession",&createFn);
        XrSessionCreateInfo invalid{};invalid.type=XR_TYPE_SESSION_CREATE_INFO;invalid.systemId=1;XrSession rejected{};
        assert(reinterpret_cast<PFN_xrCreateSession>(createFn)(instance,&invalid,&rejected)==XR_ERROR_LIMIT_REACHED&&!rejected);
        assert(runtimeSessionCreates==0);
        assert(currentLog().find("runtime sizing incompatible")!=std::string::npos);
        std::cout<<"runtime oversized outer rejected before session/hooks\n";return 0;
    }
    assert(enumerate(instance,1,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,bounded.views)==XR_SUCCESS && count==2);
    assert(bounded.guard==0x123456789abcdef0ull);
    assert(bounded.views[0].recommendedImageRectWidth==(runtimeSettings?32u:scaledRequested?1980u:1400u) && bounded.views[1].recommendedImageRectWidth==(runtimeSettings?32u:scaledRequested?1980u:1400u));
    assert(bounded.views[0].recommendedImageRectHeight==(runtimeSettings?32u:scaledRequested?2264u:1600u) && bounded.views[1].recommendedImageRectHeight==(runtimeSettings?32u:scaledRequested?2264u:1600u));
    req.getInstanceProcAddr(instance,"xrLocateViews",&fn);
    auto locateViews=reinterpret_cast<PFN_xrLocateViews>(fn);
    struct { XrView views[2]; uint64_t guard; } located{};
    located.views[0].type=located.views[1].type=XR_TYPE_VIEW;
    located.guard=0x1122334455667788ull;
    XrViewLocateInfo li{};li.type=XR_TYPE_VIEW_LOCATE_INFO;li.space=reinterpret_cast<XrSpace>(1);li.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    XrBaseInStructure appLocateNext{};appLocateNext.type=XR_TYPE_UNKNOWN;
    li.next=expectedLocateNext=&appLocateNext; // Opaque application chain must remain linked, not rewritten.
    XrViewState state{};state.type=XR_TYPE_VIEW_STATE;
    assert(locateViews(reinterpret_cast<XrSession>(2),&li,&state,2,&count,located.views)==XR_SUCCESS && count==2);
    assert(located.guard==0x1122334455667788ull && located.views[0].pose.position.x==10 && located.views[1].pose.position.x==11);
    assert(locateViews(reinterpret_cast<XrSession>(2),&li,&state,1,&count,located.views)==XR_ERROR_SIZE_INSUFFICIENT && count==2);
    // Real WARP D3D11 resources plus fake XR lifecycle exercise CopyResource and four-view composition.
    D3D_FEATURE_LEVEL feature{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&feature,nullptr)));
    auto load=[&](const char* name){PFN_xrVoidFunction value{};assert(req.getInstanceProcAddr(instance,name,&value)==XR_SUCCESS);return value;};
    auto createSessionFn=reinterpret_cast<PFN_xrCreateSession>(load("xrCreateSession"));
    XrGraphicsBindingD3D11KHR binding{};binding.type=XR_TYPE_GRAPHICS_BINDING_D3D11_KHR;binding.device=device;
    XrSessionCreateInfo si{};si.type=XR_TYPE_SESSION_CREATE_INFO;si.next=&binding;si.systemId=1;
    XrSession session{};assert(createSessionFn(instance,&si,&session)==XR_SUCCESS);
    XrSessionBeginInfo bi{};bi.type=XR_TYPE_SESSION_BEGIN_INFO;bi.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    assert(reinterpret_cast<PFN_xrBeginSession>(load("xrBeginSession"))(session,&bi)==XR_SUCCESS);
    auto createChain=reinterpret_cast<PFN_xrCreateSwapchain>(load("xrCreateSwapchain"));
    auto createEye=[&](unsigned eye,const XrSwapchainCreateInfo* info,XrSwapchain* chain){
        auto result=createChain(session,info,chain);if(XR_FAILED(result))return result;
        lmuqv::nativeEyeChains[eye]=*chain;
        uint32_t images=0;
        return reinterpret_cast<PFN_xrEnumerateSwapchainImages>(load("xrEnumerateSwapchainImages"))(*chain,0,&images,nullptr);
    };
    XrSwapchainCreateInfo ci{};ci.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;ci.width=ci.height=32;ci.sampleCount=ci.arraySize=ci.mipCount=ci.faceCount=1;
    ci.format=srgbRuntime?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    XrSwapchain left{},right{};
    // Same-size auxiliary buffers must not consume the native eye identities.
    // Covered independently below by the overlay regression sequence.
    assert(createEye(0,&ci,&left)==XR_SUCCESS);assert(createEye(1,&ci,&right)==XR_SUCCESS);
    assert(ci.usageFlags==XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT);
    if(sampledInputsRequested){
        assert(sampledAttempts==4&&sampledAccepted==(rejectSampledInputs?0u:4u));
        if(!rejectSampledInputs)for(auto* texture:textures){
            D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);assert(desc.BindFlags&D3D11_BIND_SHADER_RESOURCE);
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT(ci.format);view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
            ID3D11ShaderResourceView* srv=nullptr;assert(SUCCEEDED(device->CreateShaderResourceView(texture,&view,&srv)));srv->Release();
        }
    }else assert(sampledAttempts==0);
    for(auto appChain:{left,right}){
        XrSwapchainImageD3D11KHR image{};image.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
        assert(reinterpret_cast<PFN_xrEnumerateSwapchainImages>(load("xrEnumerateSwapchainImages"))(appChain,1,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(&image))==XR_SUCCESS&&count==1);
    }
    D3D11_TEXTURE2D_DESC sourceDesc{};textures[0]->GetDesc(&sourceDesc);sourceDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    uint32_t sourcePixels[32*32]{};for(auto& pixel:sourcePixels)pixel=0x7f0a2040u;
    D3D11_SUBRESOURCE_DATA sourceData{};sourceData.pSysMem=sourcePixels;sourceData.SysMemPitch=32*sizeof(uint32_t);
    ID3D11Texture2D* source=nullptr;assert(SUCCEEDED(device->CreateTexture2D(&sourceDesc,&sourceData,&source)));
    ID3D11Texture2D* wrongSize=nullptr;sourceDesc.Width=31;assert(SUCCEEDED(device->CreateTexture2D(&sourceDesc,nullptr,&wrongSize)));
    uint32_t blackPixels[32*32]{};for(auto& pixel:blackPixels)pixel=0xff000000u;
    D3D11_SUBRESOURCE_DATA blackData{};blackData.pSysMem=blackPixels;blackData.SysMemPitch=32*sizeof(uint32_t);
    ID3D11Texture2D* black=nullptr;sourceDesc.Width=32;assert(SUCCEEDED(device->CreateTexture2D(&sourceDesc,&blackData,&black)));
    XrCompositionLayerProjectionView pv[2]{};for(unsigned i=0;i<2;++i){pv[i].type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;pv[i].pose=located.views[i].pose;pv[i].fov=located.views[i].fov;pv[i].subImage.swapchain=i?right:left;pv[i].subImage.imageRect.extent={32,32};}
    XrCompositionLayerProjection projection{};projection.type=XR_TYPE_COMPOSITION_LAYER_PROJECTION;projection.space=li.space;projection.viewCount=2;projection.views=pv;
    const auto* layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
    XrFrameEndInfo end{};end.type=XR_TYPE_FRAME_END_INFO;end.displayTime=123;end.layerCount=1;end.layers=&layer;
    auto begin=[&](bool consumeRenderViews=true){
        XrFrameState fs{};fs.type=XR_TYPE_FRAME_STATE;
        XrView engineViews[4]{};
        assert(!lmuqv::xr_frame_views(engineViews));
        assert(reinterpret_cast<PFN_xrWaitFrame>(load("xrWaitFrame"))(session,nullptr,&fs)==XR_SUCCESS);
        assert(!lmuqv::xr_frame_views(engineViews));
        assert(reinterpret_cast<PFN_xrBeginFrame>(load("xrBeginFrame"))(session,nullptr)==XR_SUCCESS);
        assert(!lmuqv::xr_frame_views(engineViews));
        li.displayTime=123;assert(locateViews(session,&li,&state,2,&count,located.views)==XR_SUCCESS);
        if(!consumeRenderViews)return; // Exercise failed queries before rendering locks the snapshot.
        assert(lmuqv::xr_frame_views(engineViews)==runtimeShouldRender);
        for(unsigned eye=0;eye<2;++eye){pv[eye].pose=located.views[eye].pose;pv[eye].fov=located.views[eye].fov;}
        if(runtimeShouldRender){
            assert(engineViews[0].pose.position.x==10&&engineViews[3].pose.position.x==13);
            for(unsigned eye=0;eye<2;++eye)assertOuterFov(located.views[eye].fov,-.4f,.4f,.4f,-.4f);
            if(cropRequested){assertFocusContained(engineViews);std::memcpy(renderedCropViews,engineViews,sizeof(engineViews));}
        }
    };
    auto finish=[&](){
        const auto auditEnds=lmuqv::stereoAuditFrameEnds;
        assert(reinterpret_cast<PFN_xrEndFrame>(load("xrEndFrame"))(session,&end)==XR_SUCCESS);
        assert(lmuqv::stereoAuditFrameEnds==auditEnds+1);
        XrView engineViews[4]{};assert(!lmuqv::xr_frame_views(engineViews));
    };
    begin();finish();assert(submittedLayers==0&&submittedViews==0);
    begin();assert(!lmuqv::xr_copy_outer(0,wrongSize));finish();assert(submittedLayers==0&&acquireCount==successfulReleases);
    if(scaledRequested||outerBlurRequested){
        auto noSampleDesc=sourceDesc;noSampleDesc.Width=32;noSampleDesc.BindFlags&=~D3D11_BIND_SHADER_RESOURCE;
        ID3D11Texture2D* noSample=nullptr;assert(SUCCEEDED(device->CreateTexture2D(&noSampleDesc,nullptr,&noSample)));
        begin();assert(!lmuqv::xr_copy_outer(0,noSample));finish();assert(submittedLayers==0&&acquireCount==successfulReleases);noSample->Release();
    }
    auto checkOuterTarget=[&](unsigned eye,bool available){
        const auto target=lmuqv::xr_outer_target(eye);
        assert(target.rtv==lmuqv::xr_outer_rtv(eye));
        if(!available){assert(!target.rtv&&!target.width&&!target.height);return;}
        assert(target.rtv);
        ID3D11Resource* resource=nullptr;target.rtv->GetResource(&resource);
        ID3D11Texture2D* texture=nullptr;
        assert(SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture))));
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
        assert(target.width==desc.Width&&target.height==desc.Height);
        texture->Release();resource->Release();
    };
    checkOuterTarget(0,false);checkOuterTarget(1,false);checkOuterTarget(2,false);
    const auto partialReleaseBase=releaseCount;
    begin();assert(lmuqv::xr_copy_outer(0,source));assert(releaseCount==partialReleaseBase);assert(lmuqv::xr_outer_rtv(0));
    checkOuterTarget(0,true);checkOuterTarget(1,false);
    D3D11_RENDER_TARGET_VIEW_DESC outerView{};lmuqv::xr_outer_rtv(0)->GetDesc(&outerView);
    assert(outerView.Format==(typedSrgbRuntime?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM));
    assertRenderPixels(0);
    finish();assert(submittedLayers==0&&releaseCount==partialReleaseBase+2);
    assertPaddedPixels(0);
    assert(!lmuqv::xr_outer_rtv(0));
    checkOuterTarget(0,false);
    frameCalls.clear();
    const auto completeReleaseBase=releaseCount;
    begin();assert(lmuqv::xr_copy_outer(0,source));assert(lmuqv::xr_copy_outer(1,source));assert(releaseCount==completeReleaseBase);
    checkOuterTarget(0,true);checkOuterTarget(1,true);
    finish();assert(submittedLayers==1&&submittedViews==4);checkOuterTarget(0,false);checkOuterTarget(1,false);
    assert(frameCalls=="WBLE"); // LMU 5cb94b -> 5cb96b -> 5cba9c -> render -> 5cb6a2.
    runtimeShouldRender=false;begin();assert(!lmuqv::xr_copy_outer(0,source));finish();assert(submittedLayers==0);
    assert(acquireCount==successfulReleases&&releaseCount==successfulReleases);
    runtimeShouldRender=true;
    begin();assert(lmuqv::xr_copy_outer(0,source));assert(lmuqv::xr_copy_outer(1,source));
    // The next WaitFrame is permitted while current-frame engine/UI work remains active.
    runtimeTime=124;XrFrameState nextState{};nextState.type=XR_TYPE_FRAME_STATE;
    assert(reinterpret_cast<PFN_xrWaitFrame>(load("xrWaitFrame"))(session,nullptr,&nextState)==XR_SUCCESS);
    finish();assert(submittedViews==4);runtimeTime=123;
    begin();assert(lmuqv::xr_copy_outer(0,source));assert(lmuqv::xr_copy_outer(1,source));
    failRelease=true;finish();assert(submittedLayers==0);failRelease=false;
    begin();finish();assert(submittedLayers==0); // Prior acquired images released on retry, never reused as fresh captures.
    waitSwapchainResult=XR_TIMEOUT_EXPIRED;begin();assert(!lmuqv::xr_copy_outer(0,source));finish();assert(submittedLayers==0);
    waitSwapchainResult=XR_SUCCESS;begin();finish();assert(acquireCount==successfulReleases);
    // Full observed LMU sequence: frame-start fragment -> engine scene -> per-eye app submit -> EndFrame.
    frameCalls.clear();begin();
    assert(lmuqv::xr_copy_outer(0,source)&&lmuqv::xr_copy_outer(1,source));
    for(auto appChain:{left,right}){
        uint32_t image=999;XrSwapchainImageAcquireInfo ai{};ai.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
        assert(reinterpret_cast<PFN_xrAcquireSwapchainImage>(load("xrAcquireSwapchainImage"))(appChain,&ai,&image)==XR_SUCCESS&&image==0);
        XrSwapchainImageWaitInfo wi{};wi.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wi.timeout=XR_INFINITE_DURATION;
        assert(reinterpret_cast<PFN_xrWaitSwapchainImage>(load("xrWaitSwapchainImage"))(appChain,&wi)==XR_SUCCESS);
        XrSwapchainImageReleaseInfo ri{};ri.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        assert(reinterpret_cast<PFN_xrReleaseSwapchainImage>(load("xrReleaseSwapchainImage"))(appChain,&ri)==XR_SUCCESS);
    }
    finish();assert(frameCalls=="WBLE"&&submittedViews==4&&acquireCount==successfulReleases);
    if(runtimeSettings){
        // Pimax report: the app delays Begin until after rendering/releasing.
        // Exercise real D3D outer copies; the app Begin must not erase them.
        frameCalls.clear();XrFrameState fs{XR_TYPE_FRAME_STATE};
        assert(reinterpret_cast<PFN_xrWaitFrame>(load("xrWaitFrame"))(session,nullptr,&fs)==XR_SUCCESS);
        li.displayTime=fs.predictedDisplayTime;
        assert(locateViews(session,&li,&state,2,&count,located.views)==XR_SUCCESS);
        assert(frameCalls=="WLB"); // Normalized before returning to engine rendering.
        XrFovf full[2]{};assert(lmuqv::xr_frame_full_outer_fov(full));
        XrView rendered[4]{};assert(lmuqv::xr_frame_views(rendered));
        for(unsigned eye=0;eye<2;++eye){
            pv[eye].pose=located.views[eye].pose;pv[eye].fov=located.views[eye].fov;
            assert(lmuqv::xr_copy_outer(eye,source));assertRenderPixels(eye);
        }
        assert(frameCalls=="WLB");
        assert(reinterpret_cast<PFN_xrBeginFrame>(load("xrBeginFrame"))(session,nullptr)==XR_SUCCESS);
        finish();assert(frameCalls=="WLBE"&&submittedViews==4&&acquireCount==successfulReleases);
    }
    for(unsigned frame=0;frame<25;++frame){begin();finish();}
    assert(currentLog().find("pixel_stats frame=")==std::string::npos);
    // A timed-out app wait must never trigger readback or permit release.
    begin();assert(lmuqv::xr_copy_outer(0,source)&&lmuqv::xr_copy_outer(1,source));
    uint32_t imageIndex=999;XrSwapchainImageAcquireInfo ai{};ai.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
    XrSwapchainImageWaitInfo wi{};wi.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wi.timeout=XR_INFINITE_DURATION;
    XrSwapchainImageReleaseInfo ri{};ri.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
    auto appAcquire=reinterpret_cast<PFN_xrAcquireSwapchainImage>(load("xrAcquireSwapchainImage"));
    auto appWait=reinterpret_cast<PFN_xrWaitSwapchainImage>(load("xrWaitSwapchainImage"));
    auto appRelease=reinterpret_cast<PFN_xrReleaseSwapchainImage>(load("xrReleaseSwapchainImage"));
    failAcquire=true;assert(appAcquire(left,&ai,&imageIndex)==XR_ERROR_RUNTIME_FAILURE);failAcquire=false;
    assert(appRelease(left,&ri)==XR_ERROR_CALL_ORDER_INVALID&&currentLog().find("pixel_stats frame=")==std::string::npos);
    assert(appAcquire(left,&ai,&imageIndex)==XR_SUCCESS);waitSwapchainResult=XR_TIMEOUT_EXPIRED;
    assert(appWait(left,&wi)==XR_TIMEOUT_EXPIRED&&appRelease(left,&ri)==XR_ERROR_CALL_ORDER_INVALID);
    assert(currentLog().find("pixel_stats frame=")==std::string::npos);
    waitSwapchainResult=XR_SUCCESS;assert(appWait(left,&wi)==XR_SUCCESS);
    if(sceneStatsRequested){
        // Successful menu captures alone must not consume the scene diagnostic.
        assert(appRelease(left,&ri)==XR_SUCCESS);
        assert(appAcquire(right,&ai,&imageIndex)==XR_SUCCESS&&appWait(right,&wi)==XR_SUCCESS&&appRelease(right,&ri)==XR_SUCCESS);
        finish();assert(currentLog().find("pixel_stats frame=")==std::string::npos);
        // Marks outside the active rendering frame must not survive into a later frame.
        lmuqv::xr_scene_focus_complete(0);lmuqv::xr_scene_focus_complete(1);
        auto releaseBoth=[&](){
            for(auto chainToRelease:{left,right})assert(appAcquire(chainToRelease,&ai,&imageIndex)==XR_SUCCESS&&appWait(chainToRelease,&wi)==XR_SUCCESS&&appRelease(chainToRelease,&ri)==XR_SUCCESS);
            finish();assert(currentLog().find("pixel_stats frame=")==std::string::npos);
        };
        begin();assert(lmuqv::xr_copy_outer(0,source)&&lmuqv::xr_copy_outer(1,source));
        lmuqv::xr_scene_focus_complete(0);releaseBoth();
        // The previous frame's left-eye mark must reset before this right-only frame.
        begin();assert(lmuqv::xr_copy_outer(0,source)&&lmuqv::xr_copy_outer(1,source));
        lmuqv::xr_scene_focus_complete(1);releaseBoth();
        begin();assert(lmuqv::xr_copy_outer(0,source)&&lmuqv::xr_copy_outer(1,source));
        lmuqv::xr_scene_focus_complete(2);lmuqv::xr_scene_focus_complete(0);lmuqv::xr_scene_focus_complete(1);
        assert(appAcquire(left,&ai,&imageIndex)==XR_SUCCESS&&appWait(left,&wi)==XR_SUCCESS);
    }
    if(captureRequested&&!captureMissing&&!captureInvalid){
        // Even a complete marked scene frame may not read without the external trigger.
        assert(appRelease(left,&ri)==XR_SUCCESS);
        assert(appAcquire(right,&ai,&imageIndex)==XR_SUCCESS&&appWait(right,&wi)==XR_SUCCESS&&appRelease(right,&ri)==XR_SUCCESS);
        finish();assert(currentLog().find("pixel_stats frame=")==std::string::npos);
        assert(std::filesystem::is_empty(captureDirectory));
        {std::ofstream request(captureTrigger);request<<"capture one offscreen frame\n";}
        begin();assert(lmuqv::xr_copy_outer(0,source)&&lmuqv::xr_copy_outer(1,source));
        lmuqv::xr_scene_focus_complete(0);lmuqv::xr_scene_focus_complete(1);
        assert(appAcquire(left,&ai,&imageIndex)==XR_SUCCESS&&appWait(left,&wi)==XR_SUCCESS);
    }
    ID3D11DeviceContext* statsContext=nullptr;device->GetImmediateContext(&statsContext);
    statsContext->CopyResource(textures[0],source);statsContext->CopyResource(textures[3],black);statsContext->Release();
    const bool expectReadback=statsRequested&&!captureMissing&&!captureInvalid;
    expectStatsBeforeRelease=expectReadback;assert(appRelease(left,&ri)==XR_SUCCESS);
    assert(appAcquire(right,&ai,&imageIndex)==XR_SUCCESS&&appWait(right,&wi)==XR_SUCCESS&&appRelease(right,&ri)==XR_SUCCESS);
    finish();expectStatsBeforeRelease=false;
    assert(appRelease(left,&ri)==XR_ERROR_CALL_ORDER_INVALID); // Already released: do not inspect borrowed image again.
    // No second frame is read, even when later complete captures remain available.
    begin();assert(lmuqv::xr_copy_outer(0,source)&&lmuqv::xr_copy_outer(1,source));finish();
    auto statsLog=currentLog();size_t statsCount=0,statsPos=0;
    if(invalidFocusOffsetRequested){
        const auto log=currentLog();
        assert(log.find("Invalid horizontal focus offset; using safe default 0")!=std::string::npos);
        assert(log.find("Invalid vertical focus offset; using safe default 0")!=std::string::npos);
    }
    if(invalidFocusBoxRequested){
        assert(statsLog.find("Invalid focus width percentage; using safe default 100")!=std::string::npos);
        assert(statsLog.find("Invalid focus height percentage; using safe default 100")!=std::string::npos);
    }
    if(invalidSizesRequested)assert(statsLog.find("Invalid output percentages; using safe defaults outer=100 focus=100")!=std::string::npos);
    while((statsPos=statsLog.find("pixel_stats frame=",statsPos))!=std::string::npos){++statsCount;++statsPos;}
    assert(statsCount==(expectReadback?4u:0u));
    if(expectReadback){
        assert(statsLog.find("width=32 height=32 nonzeroRGB=1024 pixels=1024 hash=f09bb159dc401b25 firstRGBA=64,32,10,127")!=std::string::npos);
        assert(statsLog.find("width=32 height=32 nonzeroRGB=0 pixels=1024 hash=9f12bba67eb6c325 firstRGBA=0,0,0,255")!=std::string::npos);
        assert(statsLog.find("pixel_stats_complete mask=15 successMask=15")!=std::string::npos);
    }
    if(captureRequested){
        if(captureInvalid)assert(!std::filesystem::exists(captureDirectory));
        else if(captureMissing)assert(std::filesystem::is_empty(captureDirectory));
        else{
            unsigned files=0;
            for(auto slot:{"outer0","outer1","app0","app1"}){
                std::ifstream bitmap(captureDirectory/(std::string(slot)+".bmp"),std::ios::binary);
                std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(bitmap),std::istreambuf_iterator<char>()};
                unsigned capturedSize=scaledRequested&&slot[0]=='o'?8:32;
                assert(bytes.size()==122+capturedSize*capturedSize*4);
                bool blackSlot=!std::strcmp(slot,"app1");
                assert(bytes[122]==(blackSlot?0:10)&&bytes[123]==(blackSlot?0:32)&&bytes[124]==(blackSlot?0:64)&&bytes[125]==(blackSlot?255:127));
                ++files;
            }
            assert(files==4&&std::distance(std::filesystem::directory_iterator(captureDirectory),std::filesystem::directory_iterator())==4);
            assert(statsLog.find("captureSuccessMask=15")!=std::string::npos);
        }
        if(!captureMissing)assert(std::filesystem::is_regular_file(captureTrigger));
    }
    // Gaze-FOV forwarding regression only: no claim of actual eye tracking or headset/runtime behavior.
    movingFocusTest=true;
    for(XrTime frameTime:{XrTime(501),XrTime(502),XrTime(503)}){
        XrView cached[4]{};assert(!lmuqv::xr_frame_views(cached));
        if(frameTime!=503){
            runtimeTime=frameTime;XrFrameState fs{};fs.type=XR_TYPE_FRAME_STATE;
            assert(reinterpret_cast<PFN_xrWaitFrame>(load("xrWaitFrame"))(session,nullptr,&fs)==XR_SUCCESS&&fs.predictedDisplayTime==frameTime);
        } // Frame503's WaitFrame is deliberately pipelined before EndFrame502 below.
        assert(reinterpret_cast<PFN_xrBeginFrame>(load("xrBeginFrame"))(session,nullptr)==XR_SUCCESS);
        assert(!lmuqv::xr_frame_views(cached));
        li.displayTime=frameTime;
        assert(locateViews(session,&li,&state,2,&count,located.views)==XR_SUCCESS&&count==2);
        assert(located.guard==0x1122334455667788ull);
        assertOuterFov(located.views[0].fov,-1.1f,0.9f,0.8f,-0.7f);
        assertOuterFov(located.views[1].fov,-0.85f,1.0f,0.75f,-0.85f);
        assert(lmuqv::xr_frame_views(cached));
        XrView repeated[4]{};
        assert(locateViews(session,&li,&state,2,&count,located.views)==XR_SUCCESS);
        assert(lmuqv::xr_frame_views(repeated));
        for(unsigned eye=0;eye<4;++eye){
            assert(std::memcmp(&cached[eye].fov,&repeated[eye].fov,sizeof(XrFovf))==0);
            assert(std::memcmp(&cached[eye].pose,&repeated[eye].pose,sizeof(XrPosef))==0);
        }
        std::memcpy(renderedMovingViews,cached,sizeof(cached));
        if(cropRequested){assertFocusContained(cached);std::memcpy(renderedCropViews,cached,sizeof(cached));}
        for(unsigned eye=0;eye<2;++eye){pv[eye].pose=located.views[eye].pose;pv[eye].fov=located.views[eye].fov;}
        assert(cached[2].pose.position.x==12&&cached[3].pose.position.x==13&&cached[2].pose.orientation.w==1&&cached[3].pose.orientation.w==1);
        assertOuterFov(cached[0].fov,-1.1f,0.9f,0.8f,-0.7f);
        assertOuterFov(cached[1].fov,-0.85f,1.0f,0.75f,-0.85f);
        if(frameTime==501){
            assertFocusFov(cached[2].fov,-0.30f,0.20f,0.25f,-0.15f);
            assertFocusFov(cached[3].fov,-0.15f,0.35f,0.20f,-0.30f,1);
        }else if(frameTime==502){
            assertFocusFov(cached[2].fov,-0.05f,0.45f,0.35f,-0.10f);
            assertFocusFov(cached[3].fov,-0.40f,0.10f,0.15f,-0.35f,1);
        }else{
            assertFocusFov(cached[2].fov,-0.50f,0.10f,0.40f,-0.20f);
            assertFocusFov(cached[3].fov,-0.10f,0.50f,0.30f,-0.10f,1);
        }
        assert(lmuqv::xr_copy_outer(0,source)&&lmuqv::xr_copy_outer(1,source));
        if(frameTime==502){
            runtimeTime=503;XrFrameState next{};next.type=XR_TYPE_FRAME_STATE;
            assert(reinterpret_cast<PFN_xrWaitFrame>(load("xrWaitFrame"))(session,nullptr,&next)==XR_SUCCESS&&next.predictedDisplayTime==503);
            assert(lmuqv::xr_frame_views(cached)); // Future Wait must preserve frame502's current focus FOVs.
            assertFocusFov(cached[2].fov,-0.05f,0.45f,0.35f,-0.10f);
            assertFocusFov(cached[3].fov,-0.40f,0.10f,0.15f,-0.35f,1);
        }
        // A fresh frame503 cache may never be submitted with the old frame502 timestamp.
        end.displayTime=frameTime==503?502:frameTime;finish();
        assert(submittedViews==(frameTime==503?0u:4u));
    }
    assert(movingFocusSubmissions==2&&acquireCount==successfulReleases);
    movingFocusTest=false;runtimeTime=123;li.displayTime=123;end.displayTime=123;
    if(focusBoxPercent!=100||focusOffsetX!=0||focusOffsetY!=0){
        begin(false);invalidFocusFov=true;
        assert(locateViews(session,&li,&state,2,&count,located.views)==XR_ERROR_RUNTIME_FAILURE);
        XrView cached[4]{};assert(!lmuqv::xr_frame_views(cached));
        assert(!lmuqv::xr_copy_outer(0,source));
        finish();assert(submittedLayers==0);invalidFocusFov=false;
        if(focusOffsetX!=0||focusOffsetY!=0){
            begin(false);invalidOuterFov=true;
            assert(locateViews(session,&li,&state,2,&count,located.views)==XR_ERROR_RUNTIME_FAILURE);
            assert(!lmuqv::xr_frame_views(cached)&&!lmuqv::xr_copy_outer(0,source));
            finish();assert(submittedLayers==0);invalidOuterFov=false;
        }
    }
    if(runtimeSettings){
        for(unsigned eye=0;eye<2;++eye){
            D3D11_TEXTURE2D_DESC nativeDesc{};source->GetDesc(&nativeDesc);
            nativeDesc.Width=expectedCropWidth(runtimeOuterWidth(eye));nativeDesc.Height=expectedCropHeight(runtimeOuterHeight(eye));nativeDesc.BindFlags=outerBlurRequested?D3D11_BIND_SHADER_RESOURCE:0;
            std::vector<uint32_t> pixels(nativeDesc.Width*nativeDesc.Height,0x7f0a2040u);
            D3D11_SUBRESOURCE_DATA initial{pixels.data(),nativeDesc.Width*4,0};ID3D11Texture2D* native=nullptr;
            assert(SUCCEEDED(device->CreateTexture2D(&nativeDesc,&initial,&native)));
            begin();assert(lmuqv::xr_copy_outer(eye,native));assertRenderPixels(eye);finish();assertPaddedPixels(eye);native->Release();
        }
        // Arbitrary color chain sizes may belong to an OSD. Projection ownership
        // is checked at EndFrame; creation alone cannot identify their purpose.
    }
    if(outerBlurRequested){
        assert(runtimeEqual); // Deliberately exercise the old same-size CopyResource route.
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
        std::vector<uint32_t> checker(desc.Width*desc.Height);
        for(unsigned y=0;y<desc.Height;++y)for(unsigned x=0;x<desc.Width;++x)checker[y*desc.Width+x]=((x+y)%2)?0xffffffffu:0xff000000u;
        D3D11_SUBRESOURCE_DATA pixels{checker.data(),desc.Width*4,0};ID3D11Texture2D* checkSource=nullptr;
        assert(SUCCEEDED(device->CreateTexture2D(&desc,&pixels,&checkSource)));
        begin();assert(lmuqv::xr_copy_outer(0,checkSource)&&lmuqv::xr_copy_outer(1,checkSource));
        const auto color=texturePixel(textures[1],4,4);
        if(typedSrgbRuntime)assert(color==0xff000000u); // Opposite encoding mismatch: retain raw copy.
        else assert((color&255)>=126&&(color&255)<=129); // Would be 0 with CopyResource.
        assert(texturePixel(checkSource,4,4)==0xff000000u); // Focus/native source untouched.
        finish();assert(submittedViews==4);checkSource->Release();
        // Optional smoothing must not change old CopyResource color encoding.
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        std::fill(checker.begin(),checker.end(),0x7f808080u);
        ID3D11Texture2D* encodedSource=nullptr;assert(SUCCEEDED(device->CreateTexture2D(&desc,&pixels,&encodedSource)));
        begin();assert(lmuqv::xr_copy_outer(0,encodedSource)&&lmuqv::xr_copy_outer(1,encodedSource));
        assert(texturePixel(textures[1],4,4)==0x7f808080u);
        finish();assert(submittedViews==4);encodedSource->Release();
        assert(currentLog().find("outer smoothing unavailable for source/RTV encoding mismatch; preserving unblurred copy")!=std::string::npos);
    }
    if(scaledRequested){
        // A genuinely small native scene already matches the private outer
        // swapchain: accept it and copy exactly, without requiring a full-size
        // intermediate or a shader-resource bind flag for resampling.
        D3D11_TEXTURE2D_DESC nativeDesc{};source->GetDesc(&nativeDesc);
        nativeDesc.Width=expectedCropWidth(8);nativeDesc.Height=expectedCropHeight(8);nativeDesc.BindFlags=0;
        std::array<uint32_t,64> pixels{};pixels.fill(0x7f0a2040u);
        D3D11_SUBRESOURCE_DATA initial{pixels.data(),8*4,0};
        ID3D11Texture2D* nativeSmall=nullptr;
        assert(SUCCEEDED(device->CreateTexture2D(&nativeDesc,&initial,&nativeSmall)));
        begin();assert(lmuqv::xr_copy_outer(0,nativeSmall)&&lmuqv::xr_copy_outer(1,nativeSmall));
        assertRenderPixels(0);assertRenderPixels(1);finish();assertPaddedPixels(0);assertPaddedPixels(1);
        assert(submittedViews==4);nativeSmall->Release();
    }
    profileOnCloseRequested|=profileCloseOtherThread;
    const auto beforeCloseLog=currentLog().size();
    if(profileMenuFrames){
        lmuqv::renderProfile.configure(true,lmuqv::log_line,120,lmuqv::flush_log);
        for(unsigned i=0;i<4;++i){begin();lmuqv::renderProfile.frame(nullptr,i==0);finish();}
        for(unsigned i=0;i<64;++i){begin();finish();}
    }
    if(profileOnCloseRequested){
        lmuqv::renderProfile.configure(true,lmuqv::log_line,120,lmuqv::flush_log);
        lmuqv::renderProfile.frame(nullptr,true);
        auto completed=lmuqv::renderProfile.begin(lmuqv::ProfileSlot::outer0);lmuqv::renderProfile.end(completed);
    }
    auto closeSession=[&]{assert(reinterpret_cast<PFN_xrDestroySession>(load("xrDestroySession"))(session)==XR_SUCCESS);};
    if(profileCloseOtherThread){std::thread closer(closeSession);closer.join();}else closeSession();
    if(profileMenuFrames){
        const auto saved=currentLog().substr(beforeCloseLog);
        assert(saved.find("frames=4 xr_frames=68 non_scene_frames=64;")!=std::string::npos);
        assert(saved.find("cpu slot=provider_submit samples=4 calls=4")!=std::string::npos);
        assert(saved.find("cpu slot=non_scene_provider_submit samples=64 calls=64")!=std::string::npos);
        std::cout<<"XR frame integration: four scene frames and 64 menu frames remain separate in saved log\n";
    }
    if(profileOnCloseRequested){
        const auto saved=currentLog().substr(beforeCloseLog);
        assert(saved.find("reason=session_end")!=std::string::npos&&saved.find("cpu slot=outer0 samples=1")!=std::string::npos&&saved.find("render_profile complete")!=std::string::npos);
        if(profileCloseOtherThread)assert(saved.find("partial saved on teardown thread")!=std::string::npos);
        std::cout<<"Active capture saved and flushed to real log on "<<(profileCloseOtherThread?"another thread":"render thread")<<" session close\n";
    }
    assert(!lmuqv::xr_quad_active());
    if(diagnosticsOff)assert(GetFileAttributesW(logPath.c_str())==INVALID_FILE_ATTRIBUTES);
    for(auto* texture:textures)texture->Release();
    source->Release();wrongSize->Release();black->Release();
    device->Release();
    assert(reinterpret_cast<PFN_xrDestroyInstance>(load("xrDestroyInstance"))(instance)==XR_SUCCESS);
    if(debugPhaseRequested){
        debugRuntime=true;fixtureDimension=128;textures.clear();swapchains.clear();ownership.clear();
        SetEnvironmentVariableA("LMU_QV_DEBUG_FOCUS","1");SetEnvironmentVariableA("LMU_QV_PIXEL_STATS",nullptr);
        SetEnvironmentVariableW(L"LMU_QV_CAPTURE_DIR",nullptr);SetEnvironmentVariableW(L"LMU_QV_CAPTURE_TRIGGER",nullptr);
        assert(req.createApiLayerInstance(&info,&chain,&instance)==XR_SUCCESS);
        assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&feature,nullptr)));
        binding.device=device;assert(createSessionFn(instance,&si,&session)==XR_SUCCESS);
        assert(reinterpret_cast<PFN_xrBeginSession>(load("xrBeginSession"))(session,&bi)==XR_SUCCESS);
        ci.width=ci.height=128;assert(createEye(0,&ci,&left)==XR_SUCCESS&&createEye(1,&ci,&right)==XR_SUCCESS);
        for(auto appChain:{left,right}){
            XrSwapchainImageD3D11KHR image{};image.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
            assert(reinterpret_cast<PFN_xrEnumerateSwapchainImages>(load("xrEnumerateSwapchainImages"))(appChain,1,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(&image))==XR_SUCCESS);
        }
        std::vector<uint32_t> pixels(128*128,0x7f0a2040u);
        D3D11_TEXTURE2D_DESC sd{};textures[0]->GetDesc(&sd);sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=pixels.data();initial.SysMemPitch=128*4;
        ID3D11Texture2D* debugSource=nullptr;assert(SUCCEEDED(device->CreateTexture2D(&sd,&initial,&debugSource)));
        for(XrTime time:{XrTime(701),XrTime(702)}){
            runtimeTime=time;XrFrameState fs{};fs.type=XR_TYPE_FRAME_STATE;
            assert(reinterpret_cast<PFN_xrWaitFrame>(load("xrWaitFrame"))(session,nullptr,&fs)==XR_SUCCESS);
            assert(reinterpret_cast<PFN_xrBeginFrame>(load("xrBeginFrame"))(session,nullptr)==XR_SUCCESS);
            li.displayTime=time;assert(locateViews(session,&li,&state,2,&count,located.views)==XR_SUCCESS&&count==2);
            assert(lmuqv::xr_copy_outer(0,debugSource)&&lmuqv::xr_copy_outer(1,debugSource));
            for(auto appChain:{left,right}){
                assert(appAcquire(appChain,&ai,&imageIndex)==XR_SUCCESS);
                ID3D11DeviceContext* ctx=nullptr;device->GetImmediateContext(&ctx);
                ctx->UpdateSubresource(textures[appChain==left?0:3],0,nullptr,pixels.data(),128*4,0);ctx->Release();
                assert(appRelease(appChain,&ri)==XR_ERROR_CALL_ORDER_INVALID);
                assert(texturePixel(textures[appChain==left?0:3],3,64)==0x7f0a2040u); // No paint before successful Wait.
                assert(appWait(appChain,&wi)==XR_SUCCESS&&appRelease(appChain,&ri)==XR_SUCCESS);
            }
            for(unsigned eye=0;eye<2;++eye){pv[eye].pose=located.views[eye].pose;pv[eye].fov=located.views[eye].fov;}
            pv[0].subImage.swapchain=left;pv[1].subImage.swapchain=right;pv[0].subImage.imageRect.extent=pv[1].subImage.imageRect.extent={128,128};
            end.displayTime=time;assert(reinterpret_cast<PFN_xrEndFrame>(load("xrEndFrame"))(session,&end)==XR_SUCCESS&&submittedViews==4);
        }
        assert(reinterpret_cast<PFN_xrDestroySession>(load("xrDestroySession"))(session)==XR_SUCCESS);
        for(auto* texture:textures)texture->Release();
        debugSource->Release();device->Release();
        assert(reinterpret_cast<PFN_xrDestroyInstance>(load("xrDestroyInstance"))(instance)==XR_SUCCESS);
        debugRuntime=false;fixtureDimension=32;SetEnvironmentVariableA("LMU_QV_DEBUG_FOCUS",nullptr);li.displayTime=123;
    }
    if(overlayTestRequested){
        overlayTestActive=true;debugRuntime=true;fixtureDimension=128;
        textures.clear();swapchains.clear();ownership.clear();
        SetEnvironmentVariableA("LMU_QV_DEBUG_FOCUS","1");SetEnvironmentVariableA("LMU_QV_PIXEL_STATS","1");
        assert(req.createApiLayerInstance(&info,&chain,&instance)==XR_SUCCESS);
        assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&feature,nullptr)));
        binding.device=device;assert(createSessionFn(instance,&si,&session)==XR_SUCCESS);
        assert(reinterpret_cast<PFN_xrBeginSession>(load("xrBeginSession"))(session,&bi)==XR_SUCCESS);
        auto overlayInfo=ci;overlayInfo.width=overlayInfo.height=128;
        XrSwapchain before{},after{},sameSize{};
        assert(createChain(session,&overlayInfo,&before)==XR_SUCCESS&&textures.size()==1);
        ci.width=ci.height=128;
        assert(createEye(0,&ci,&overlayLeft)==XR_SUCCESS&&textures.size()==4);
        overlayOuter0=swapchains[2];overlayOuter1=swapchains[3];
        assert(createEye(1,&ci,&overlayRight)==XR_SUCCESS&&textures.size()==5);
        overlayInfo.width=960;overlayInfo.height=768;
        assert(createChain(session,&overlayInfo,&after)==XR_SUCCESS&&textures.size()==6);
        assert(createChain(session,&ci,&sameSize)==XR_SUCCESS&&textures.size()==7);
        for(auto appChain:{before,overlayLeft,overlayRight,after,sameSize}){
            XrSwapchainImageD3D11KHR image{};image.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
            assert(reinterpret_cast<PFN_xrEnumerateSwapchainImages>(load("xrEnumerateSwapchainImages"))(appChain,1,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(&image))==XR_SUCCESS);
        }
        // Populate every chain, including overlays, with a distinctive uniform image.
        ID3D11DeviceContext* ctx=nullptr;device->GetImmediateContext(&ctx);
        for(auto* texture:textures){
            D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
            std::vector<uint32_t> pixels(desc.Width*desc.Height,0x7f0a2040u);
            ctx->UpdateSubresource(texture,0,nullptr,pixels.data(),desc.Width*4,0);
        }
        ctx->Release();
        XrCompositionLayerQuad osd{};osd.type=XR_TYPE_COMPOSITION_LAYER_QUAD;
        osd.subImage.swapchain=after;osd.subImage.imageRect.extent={960,768};
        osd.pose.orientation.w=1;osd.size={.8f,.6f};osd.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        const auto osdBefore=osd;expectedOverlay=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&osd);
        XrCompositionLayerProjectionView eyeViews[2]{};
        for(unsigned eye=0;eye<2;++eye){eyeViews[eye].type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;eyeViews[eye].subImage.swapchain=eye?overlayRight:overlayLeft;eyeViews[eye].subImage.imageRect.extent={128,128};}
        XrCompositionLayerProjection eyeLayer{};eyeLayer.type=XR_TYPE_COMPOSITION_LAYER_PROJECTION;eyeLayer.space=li.space;eyeLayer.viewCount=2;eyeLayer.views=eyeViews;
        const XrCompositionLayerBaseHeader* layerList[]={reinterpret_cast<const XrCompositionLayerBaseHeader*>(&eyeLayer),expectedOverlay};
        auto osdEnd=end;osdEnd.displayTime=701;osdEnd.layerCount=2;osdEnd.layers=layerList;
        runtimeTime=701;li.displayTime=701;
        auto beginOverlay=[&](){
            XrFrameState fs{};fs.type=XR_TYPE_FRAME_STATE;
            assert(reinterpret_cast<PFN_xrWaitFrame>(load("xrWaitFrame"))(session,nullptr,&fs)==XR_SUCCESS);
            assert(reinterpret_cast<PFN_xrBeginFrame>(load("xrBeginFrame"))(session,nullptr)==XR_SUCCESS);
            assert(locateViews(session,&li,&state,2,&count,located.views)==XR_SUCCESS);
            for(unsigned eye=0;eye<2;++eye){eyeViews[eye].pose=located.views[eye].pose;eyeViews[eye].fov=located.views[eye].fov;}
        };
        const auto statsStart=currentLog().size();
        expectedOverlayProjections=0;
        for(unsigned frame=0;frame<29;++frame){beginOverlay();assert(reinterpret_cast<PFN_xrEndFrame>(load("xrEndFrame"))(session,&osdEnd)==XR_SUCCESS);}
        beginOverlay();assert(lmuqv::xr_copy_outer(0,textures[1])&&lmuqv::xr_copy_outer(1,textures[4]));
        for(auto aux:{before,after,sameSize}){
            assert(appAcquire(aux,&ai,&imageIndex)==XR_SUCCESS&&appWait(aux,&wi)==XR_SUCCESS&&appRelease(aux,&ri)==XR_SUCCESS);
            auto* texture=textures[reinterpret_cast<uintptr_t>(aux)-10];
            D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ID3D11Texture2D* staging=nullptr;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)));
            device->GetImmediateContext(&ctx);ctx->CopyResource(staging,texture);
            D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(ctx->Map(staging,0,D3D11_MAP_READ,0,&mapped)));
            for(unsigned y=0;y<desc.Height;++y)for(unsigned x=0;x<desc.Width;++x)
                assert(reinterpret_cast<const uint32_t*>(static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch)[x]==0x7f0a2040u);
            ctx->Unmap(staging,0);ctx->Release();staging->Release();
        }
        assert(currentLog().substr(statsStart).find("pixel_stats_begin")==std::string::npos);
        for(auto eyeChain:{overlayLeft,overlayRight})assert(appAcquire(eyeChain,&ai,&imageIndex)==XR_SUCCESS&&appWait(eyeChain,&wi)==XR_SUCCESS&&appRelease(eyeChain,&ri)==XR_SUCCESS);
        expectedOverlayProjections=1;assert(reinterpret_cast<PFN_xrEndFrame>(load("xrEndFrame"))(session,&osdEnd)==XR_SUCCESS);
        assert(std::memcmp(&osd,&osdBefore,sizeof(osd))==0);
        assert(currentLog().substr(statsStart).find("pixel_stats_complete mask=15 successMask=15")!=std::string::npos);
        // Neither same-sized auxiliary chains, wrong-sized OSDs, nor duplicated
        // game-left chains may masquerade as a valid left/right projection pair.
        expectedOverlayProjections=0;
        for(auto impostor:{sameSize,before,overlayRight}){
            beginOverlay();assert(lmuqv::xr_copy_outer(0,textures[1])&&lmuqv::xr_copy_outer(1,textures[4]));
            eyeViews[0].subImage.swapchain=impostor;
            assert(reinterpret_cast<PFN_xrEndFrame>(load("xrEndFrame"))(session,&osdEnd)==XR_SUCCESS);
            assert(std::memcmp(&osd,&osdBefore,sizeof(osd))==0);
        }
        // A rewritten native-eye size must remain enumerable: LMU ignores the
        // XR error code and dereferences image[0]. Refuse quad role, not images.
        auto rewrittenInfo=ci;rewrittenInfo.width=256;rewrittenInfo.height=240;
        XrSwapchain rewritten{};
        assert(createEye(0,&rewrittenInfo,&rewritten)==XR_SUCCESS);
        XrSwapchainImageD3D11KHR rewrittenImage{};rewrittenImage.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
        assert(reinterpret_cast<PFN_xrEnumerateSwapchainImages>(load("xrEnumerateSwapchainImages"))(rewritten,1,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(&rewrittenImage))==XR_SUCCESS&&count==1&&rewrittenImage.texture);
        assert(currentLog().find("Native game eye rejected eye=0")!=std::string::npos);
        beginOverlay();assert(lmuqv::xr_copy_outer(0,textures[1])&&lmuqv::xr_copy_outer(1,textures[4]));
        eyeViews[0].subImage.swapchain=rewritten;
        assert(reinterpret_cast<PFN_xrEndFrame>(load("xrEndFrame"))(session,&osdEnd)==XR_SUCCESS);
        assert(reinterpret_cast<PFN_xrDestroySession>(load("xrDestroySession"))(session)==XR_SUCCESS);
        for(auto* texture:textures)texture->Release();
        device->Release();
        assert(reinterpret_cast<PFN_xrDestroyInstance>(load("xrDestroyInstance"))(instance)==XR_SUCCESS);
        overlayTestActive=false;debugRuntime=false;fixtureDimension=32;li.displayTime=123;runtimeTime=123;
        SetEnvironmentVariableA("LMU_QV_DEBUG_FOCUS",nullptr);SetEnvironmentVariableA("LMU_QV_PIXEL_STATS",nullptr);
    }
    SetEnvironmentVariableA("LMU_QV_MODE","trace");traceRuntime=true;
    assert(req.createApiLayerInstance(&info,&chain,&instance)==XR_SUCCESS&&chained&&!extensions);
    assert(enumerate(instance,1,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,bounded.views)==XR_SUCCESS&&count==2);
    assert(bounded.views[0].recommendedImageRectWidth==1200&&bounded.views[0].recommendedImageRectHeight==1300);
    frameCalls.clear();
    const auto traceLogBefore=lmuqv::async_log_stats();
    XrFrameState traceFrame{};traceFrame.type=XR_TYPE_FRAME_STATE;
    assert(reinterpret_cast<PFN_xrWaitFrame>(load("xrWaitFrame"))(session,nullptr,&traceFrame)==XR_SUCCESS);
    assert(reinterpret_cast<PFN_xrBeginFrame>(load("xrBeginFrame"))(session,nullptr)==XR_SUCCESS);
    assert(locateViews(session,&li,&state,2,&count,located.views)==XR_SUCCESS);
    XrFrameEndInfo traceEnd{};traceEnd.type=XR_TYPE_FRAME_END_INFO;traceEnd.displayTime=123;
    assert(reinterpret_cast<PFN_xrEndFrame>(load("xrEndFrame"))(session,&traceEnd)==XR_SUCCESS);
    assert(frameCalls=="WBLE");
    const auto logText=currentLog();
    if(!diagnosticsOff){
        assert(traceLogsAccountedFor(logText,traceLogBefore,lmuqv::async_log_stats()));
    }else assert(logText.empty()&&GetFileAttributesW(logPath.c_str())==INVALID_FILE_ATTRIBUTES);
    assert(reinterpret_cast<PFN_xrDestroyInstance>(load("xrDestroyInstance"))(instance)==XR_SUCCESS);
    // VRS requests ordinary stereo, leaves runtime views untouched and falls
    // back when the optional gaze extension is absent. No quad provider needed.
    SetEnvironmentVariableA("LMU_QV_MODE","vrs");
    SetEnvironmentVariableA("LMU_QV_FOCUS_WIDTH_PERCENT","40");
    SetEnvironmentVariableA("LMU_QV_FOCUS_HEIGHT_PERCENT","60");
    SetEnvironmentVariableA("LMU_QV_FOCUS_OFFSET_X_PERCENT","0");
    SetEnvironmentVariableA("LMU_QV_FOCUS_OFFSET_Y_PERCENT","0");
    rejectGazeExtension=true;
    for(bool fixed:{true,false}){
        SetEnvironmentVariableA("LMU_QV_FOVEATION_MODE",fixed?"fixed":"auto");
        const auto before=instanceCreateCalls;
        assert(req.createApiLayerInstance(&info,&chain,&instance)==XR_SUCCESS&&!extensions);
        assert(instanceCreateCalls==before+(fixed?1:2));
        assert(enumerate(instance,1,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,bounded.views)==XR_SUCCESS&&count==2);
        assert(reinterpret_cast<PFN_xrWaitFrame>(load("xrWaitFrame"))(session,nullptr,&traceFrame)==XR_SUCCESS);
        assert(reinterpret_cast<PFN_xrBeginFrame>(load("xrBeginFrame"))(session,nullptr)==XR_SUCCESS);
        li.displayTime=runtimeTime;
        assert(locateViews(session,&li,&state,2,&count,located.views)==XR_SUCCESS&&count==2);
        assert(located.views[0].pose.position.x==10&&located.views[1].pose.position.x==11);
        lmuqv::VrsFocus focus;
        assert(lmuqv::xr_vrs_focus(0,focus)&&std::abs(focus.left-.3f)<1e-5&&std::abs(focus.top-.2f)<1e-5);
        assert(!lmuqv::xr_vrs_focus(2,focus));
        assert(reinterpret_cast<PFN_xrEndFrame>(load("xrEndFrame"))(session,&traceEnd)==XR_SUCCESS);
        assert(!lmuqv::xr_vrs_focus(0,focus));
        assert(reinterpret_cast<PFN_xrDestroyInstance>(load("xrDestroyInstance"))(instance)==XR_SUCCESS);
    }
    rejectGazeExtension=false;
    // Switching modes after destruction must not retain a previous instance's request.
    traceRuntime=false;SetEnvironmentVariableA("LMU_QV_MODE","quad");
    for(bool fixed:{true,false}){
        fixedFoveation=fixed;SetEnvironmentVariableA("LMU_QV_FOVEATION_MODE",fixed?"fixed":"auto");
        assert(req.createApiLayerInstance(&info,&chain,&instance)==XR_SUCCESS&&extensions);
        assert(enumerate(instance,1,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,bounded.views)==XR_SUCCESS);
        assert(locateViews(session,&li,&state,2,&count,located.views)==XR_SUCCESS);
        assert(reinterpret_cast<PFN_xrDestroyInstance>(load("xrDestroyInstance"))(instance)==XR_SUCCESS);
    }
    std::cout << "xr-layer tests passed (" << (typedSrgbRuntime?"typed SRGB images / UNORM source":srgbRuntime?"SRGB chain / typeless images / UNORM source":"UNORM chain and source") << "): negotiation, chain advance, dimensions, count isolation, WARP copy/linear RTV, size rejection, quad composition, skipped/incomplete frames, release/wait retry, pipelined WaitFrame, moving asymmetric focus FOVs, stale-time rejection, trace pass-through\n";
}
