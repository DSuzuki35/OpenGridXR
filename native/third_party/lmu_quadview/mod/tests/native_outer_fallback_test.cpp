// Exercise the real render hook with a synthetic eye-loop caller and XR boundary.
// No game code, game memory or graphics device is used.
#include <cstdint>
#undef NOMINMAX
static std::uintptr_t syntheticCaller=0;
static void* testReturnAddress(unsigned){return reinterpret_cast<void*>(syntheticCaller);}
#define LMU_CALLER_ADDRESS testReturnAddress(0)
#include "../engine_bridge.cpp"
#undef LMU_CALLER_ADDRESS
#include <iostream>
#include <cstdlib>

namespace lmuqv {
unsigned originalCalls=0,viewRequests=0;
bool xr_runtime_sizes(unsigned,uint32_t& ow,uint32_t& oh,uint32_t& w,uint32_t& h){ow=650;oh=642;w=2224;h=2182;return true;}
bool xr_vrs_active(){return false;}
bool xr_vrs_focus(unsigned,VrsFocus&){return false;}
bool xr_vrs_render_focus(unsigned,const float*,VrsFocus&){return false;}
void xr_vrs_scene_complete(unsigned){}
bool xr_quad_active(){return true;}
bool xr_frame_views(XrView (&)[4]){++viewRequests;return false;}
bool xr_frame_full_outer_fov(XrFovf (&)[2]){return false;}
bool xr_copy_outer(unsigned,ID3D11Texture2D*){return false;}
void xr_scene_focus_complete(unsigned){}
ID3D11RenderTargetView* xr_outer_rtv(unsigned){return nullptr;}
OuterTarget xr_outer_target(unsigned){return {};}
void log_line(const char*){}
void flush_log(){}
}
static void original(void*,bool){++lmuqv::originalCalls;}
static XrResult XRAPI_CALL enumerateOriginal(XrInstance,XrSystemId,XrViewConfigurationType,uint32_t cap,uint32_t* count,XrViewConfigurationView* views){
    *count=2;
    if(cap>=2)for(unsigned eye=0;eye<2;++eye){views[eye].recommendedImageRectWidth=5928;views[eye].recommendedImageRectHeight=5798;}
    return XR_SUCCESS;
}
static const float* originalFovScale(void* renderer){
    return reinterpret_cast<const float*>(static_cast<std::byte*>(renderer)+0x770e0);
}
static void require(bool condition,const char* message){
    if(!condition){std::cerr<<message<<'\n';std::exit(1);}
}
int main(){
    using namespace lmuqv;
    {
        nativeEnumerateOriginal=enumerateOriginal;nativeRuntimeSettings=true;base=0x10000000;
        XrViewConfigurationView views[2]{};for(auto& v:views)v.type=XR_TYPE_VIEW_CONFIGURATION_VIEW;
        uint32_t count=0;syntheticCaller=base+0x5bd214;
        require(nativeEnumerateHook(XR_NULL_HANDLE,1,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,views)==XR_SUCCESS&&
                views[0].recommendedImageRectWidth==2224&&views[1].recommendedImageRectHeight==2182,
                "native enumeration hook did not correct the reported upstream size override");
        syntheticCaller=base+0x1234;
        require(nativeEnumerateHook(XR_NULL_HANDLE,1,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,views)==XR_SUCCESS&&
                views[0].recommendedImageRectWidth==5928,"non-game enumeration changed");
        nativeRuntimeSettings=false;syntheticCaller=base+0x5bd214;
        require(nativeEnumerateHook(XR_NULL_HANDLE,1,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,views)==XR_SUCCESS&&
                views[1].recommendedImageRectHeight==5798,"legacy enumeration changed");
    }
    {
        // Saved native FOV cropping is independent of the runtime's image sizes.
        // The captured 70% vertical crop turns a 1990-pixel eye into 1148 pixels.
        constexpr float angle=0.905413151f;
        require(std::lround(1990.f*std::tan(angle*0.7f)/std::tan(angle))==1148,
                "captured native vertical crop no longer reproduces");
        std::vector<std::byte> globals(0x1b12bf0),renderer(0x770e8);
        base=reinterpret_cast<std::uintptr_t>(globals.data());
        *address<std::uintptr_t>(0x1b12be8)=reinterpret_cast<std::uintptr_t>(renderer.data());
        const float savedScale[2]{1.f,0.7f};
        std::memcpy(renderer.data()+0x770e0,savedScale,sizeof(savedScale));
        XrView view{};view.fov={-0.901517153f,0.726984441f,angle,-angle};
        Matrix actual{},expected{};
        nativeRuntimeSettings=true;
        require(projection(view,0.1f,1000.f,actual)&&
                make_engine_projection(fov(view),0.1f,1000.f,1.f,1.f,expected)&&actual==expected,
                "runtime projection still applies saved LMU FOV crop");
        nativeRuntimeSettings=false;
        require(projection(view,0.1f,1000.f,actual)&&
                make_engine_projection(fov(view),0.1f,1000.f,1.f,0.7f,expected)&&actual==expected,
                "legacy projection lost native FOV crop");
        require(!std::memcmp(renderer.data()+0x770e0,savedScale,sizeof(savedScale)),
                "runtime projection changed saved native FOV scaling");
        fovScaleOriginal=originalFovScale;
        nativeRuntimeSettings=true;
        nativeOuterOptIn=false;
        for(auto caller:fovScaleCallers){
            syntheticCaller=base+caller;
            const auto* effective=fovScaleHook(renderer.data());
            require(effective[0]==1.f&&effective[1]==1.f,
                    "runtime native allocation/projection kept saved crop");
            require(std::lround(1990.f*std::tan(angle*effective[1])/std::tan(angle))==1990,
                    "runtime native allocation still creates a cropped image");
            require(std::lround(1990.f*(std::tan(angle)-std::tan(angle*effective[1]))/(2.f*std::tan(angle)))==0,
                    "runtime native submission still offsets the full eye image");
            syntheticCaller=base+caller+1;
            require(fovScaleHook(renderer.data())==originalFovScale(renderer.data()),
                    "FOV crop bypass accepted an adjacent caller");
        }
        for(auto caller:{0x2f25ecu,0x2f2619u,0x1234u}){
            syntheticCaller=base+caller;
            require(fovScaleHook(renderer.data())==originalFovScale(renderer.data()),
                    "FOV crop bypass changed a configuration reader");
        }
        nativeRuntimeSettings=false;syntheticCaller=base+fovScaleCallers[0];
        require(fovScaleHook(renderer.data())==originalFovScale(renderer.data()),
                "legacy native allocation lost saved FOV crop");
        require(!std::memcmp(renderer.data()+0x770e0,savedScale,sizeof(savedScale)),
                "FOV getter hook changed saved native preferences");
        XrSwapchain eyes[]{reinterpret_cast<XrSwapchain>(std::uintptr_t(0x1000)),
                           reinterpret_cast<XrSwapchain>(std::uintptr_t(0x2000))};
        auto* nativeVector=address<std::uintptr_t>(0x19b28d0);
        nativeVector[0]=reinterpret_cast<std::uintptr_t>(eyes);
        nativeVector[1]=nativeVector[0]+sizeof(eyes);
        nativeRuntimeSettings=true;
        require(engine_game_eye(eyes[0])==2,"uninstalled bridge classified a native swapchain");
        installed.store(true);
        require(engine_game_eye(eyes[0])==0&&engine_game_eye(eyes[1])==1,
                "native swapchain identity did not follow the actual eye vector");
        require(engine_game_eye(reinterpret_cast<XrSwapchain>(std::uintptr_t(0x3000)))==2&&
                engine_game_eye(XR_NULL_HANDLE)==2,"unrelated swapchain acquired a native eye role");
        std::swap(eyes[0],eyes[1]);
        require(engine_game_eye(eyes[0])==0&&engine_game_eye(eyes[1])==1,
                "native swapchain identity depended on handle order");
        const auto right=eyes[1];eyes[1]=eyes[0];
        require(engine_game_eye(eyes[0])==2,"duplicate native handles accepted");
        eyes[1]=XR_NULL_HANDLE;
        require(engine_game_eye(eyes[0])==0,"first eye could not enumerate before second eye creation");
        eyes[1]=reinterpret_cast<XrSwapchain>(std::uintptr_t(1));
        require(engine_game_eye(eyes[0])==0&&engine_game_eye(eyes[1])==1,
                "valid opaque handle 1 rejected despite unique native eye identity");
        eyes[1]=right;nativeVector[1]+=sizeof(XrSwapchain);
        require(engine_game_eye(eyes[0])==2,"oversized native eye vector accepted");
        nativeVector[1]-=sizeof(XrSwapchain);nativeRuntimeSettings=false;
        require(engine_game_eye(eyes[0])==2,"legacy mode classified native swapchains");
        nativeRuntimeSettings=true;nativeVector[0]=0x1000;nativeVector[1]=0x1010;
        require(engine_game_eye(eyes[0])==2,"unreadable native handles accepted");
        installed.store(false);
    }
    base=0x10000000;syntheticCaller=base+0xcd1763;
    installed.store(true);renderOriginal=original;
    nativeRuntimeSettings=true;nativeOuterOptIn=true;nativeOuterFailed=true;
    // Resource-bank creation failed before binding: the original renderer is intact.
    renderHook(nullptr,false);
    require(originalCalls==1,"fallback skipped original rendering");
    require(viewRequests==1,"recoverable native-bank failure suppresses quad scene submission");
    // Failure persists for the session, but live scene frames must keep flowing.
    renderHook(nullptr,false);
    require(originalCalls==2&&viewRequests==2,"fallback stopped on the following frame");
    syntheticCaller=base+0x1234;
    renderHook(nullptr,false);
    require(originalCalls==3&&viewRequests==2,"fallback accepted an unverified eye-loop caller");
    syntheticCaller=base+0xcd1763;
    nativeQuadSubmissionBlocked=true;
    renderHook(nullptr,false);
    require(originalCalls==4&&viewRequests==2,"unsafe dimensions/restoration failure allowed quad submission");
    nativeRuntimeSettings=false;
    renderHook(nullptr,false);
    require(originalCalls==5&&viewRequests==2,"legacy mode bypassed unsafe restoration guard");
    installed.store(false);
    std::cout<<"Native outer fallback and runtime FOV crop: caller guards, saved settings and projection checks passed\n";
}
