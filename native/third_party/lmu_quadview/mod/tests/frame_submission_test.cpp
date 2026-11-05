// Exercise the real submission adapter with a synchronous, allocation-free runtime.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <cstdlib>
#include <new>
#include <malloc.h>

namespace allocation_probe {
bool enabled=false;
size_t count=0;
void record(){if(enabled)++count;}
}
void* operator new(size_t size){allocation_probe::record();if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}
void* operator new(size_t size,std::align_val_t alignment){allocation_probe::record();if(void* p=_aligned_malloc(size?size:1,static_cast<size_t>(alignment)))return p;throw std::bad_alloc();}
void* operator new[](size_t size,std::align_val_t alignment){return ::operator new(size,alignment);}
void operator delete(void* p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete[](void* p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete(void* p,size_t,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete[](void* p,size_t,std::align_val_t)noexcept{_aligned_free(p);}

#include "../xr_layer.cpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <limits>

namespace {
unsigned auditEnds=0,runtimeEnds=0,releases=0,waits=0;
bool releaseFails=false,inspectSubmission=true;
XrResult downstreamResult=XR_SUCCESS;
std::array<unsigned,8> events{};
unsigned eventCount=0;
void event(unsigned value){if(eventCount<events.size())events[eventCount++]=value;}
struct Submission {
    XrSession session{};
    const XrFrameEndInfo* original{};
    XrFrameEndInfo info{};
    std::array<const XrCompositionLayerBaseHeader*,128> layers{};
    XrCompositionLayerProjection projection{};
    std::array<XrCompositionLayerProjectionView,4> views{};
    unsigned projectionCount=0;
} received;
XrResult XRAPI_CALL fakeEnd(XrSession session,const XrFrameEndInfo* info){
    ++runtimeEnds;event(3);
    if(inspectSubmission){
        received={};received.session=session;received.original=info;received.info=*info;
        assert(info->layerCount<=received.layers.size());
        assert(info->layerCount==0||info->layers);
        for(uint32_t i=0;i<info->layerCount;++i){
            const auto* layer=info->layers[i];received.layers[i]=layer;
            if(layer&&layer->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION){
                ++received.projectionCount;
                const auto& projection=*reinterpret_cast<const XrCompositionLayerProjection*>(layer);
                received.projection=projection;
                assert(projection.views&&projection.viewCount<=received.views.size());
                for(uint32_t view=0;view<projection.viewCount;++view)received.views[view]=projection.views[view];
            }
        }
    }
    return downstreamResult;
}
XrResult XRAPI_CALL fakeRelease(XrSwapchain chain,const XrSwapchainImageReleaseInfo* info){
    const unsigned eye=chain==s.outer[0].chain?0:1;
    assert(chain==s.outer[eye].chain&&s.outer[eye].acquired&&s.outer[eye].waited);
    assert(info&&info->type==XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO&&!info->next);
    ++releases;event(eye+1);return releaseFails?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;
}
XrResult XRAPI_CALL fakeWait(XrSwapchain,const XrSwapchainImageWaitInfo* info){
    assert(info&&info->type==XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO&&info->timeout==0);++waits;return XR_SUCCESS;
}
}
namespace lmuqv {
bool engine_start(ID3D11DeviceContext*){assert(false);return false;}
void engine_stop(){renderProfile.shutdown();}
unsigned engine_game_eye(XrSwapchain){return 2;}
bool stereo_audit_prepare(bool,const wchar_t*,void(*)(const char*)){return false;}
void stereo_audit_stop(){}
void stereo_audit_frame_end(){++auditEnds;event(4);}
}
namespace {
struct Fixture {
    int frameNext=1,projectionNext=2,leftNext=3,rightNext=4;
    std::array<XrCompositionLayerProjectionView,2> views{};
    XrCompositionLayerProjection projection{};
    std::array<XrCompositionLayerQuad,2> overlays{};
    std::array<const XrCompositionLayerBaseHeader*,128> layers{};
    XrFrameEndInfo info{};
    Fixture(){
        s=State{};s.quad=true;s.frames=11;s.session=reinterpret_cast<XrSession>(1);
        s.locateSpace=reinterpret_cast<XrSpace>(2);s.locateTime=s.predictedTime=12345;
        s.colorInfo.format=DXGI_FORMAT_R8G8B8A8_UNORM;s.colorInfo.width=1024;s.colorInfo.height=768;
        for(unsigned eye=0;eye<2;++eye){
            const auto chain=reinterpret_cast<XrSwapchain>(uintptr_t(10+eye));
            AppChain app;app.eye=eye;app.info=s.colorInfo;app.info.sampleCount=1;app.info.arraySize=2;
            s.appChains.emplace(chain,std::move(app));
            s.outer[eye].chain=reinterpret_cast<XrSwapchain>(uintptr_t(20+eye));
            s.outer[eye].width=eye?600:640;s.outer[eye].height=eye?400:480;
            views[eye]=structure<XrCompositionLayerProjectionView>(XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW);
            views[eye].next=eye?&rightNext:&leftNext;views[eye].pose.orientation.w=1;
            views[eye].subImage={chain,{{int32_t(7+eye),int32_t(9+eye)},{100,200}},eye};
            overlays[eye]=structure<XrCompositionLayerQuad>(XR_TYPE_COMPOSITION_LAYER_QUAD);
            overlays[eye].space=reinterpret_cast<XrSpace>(uintptr_t(30+eye));
        }
        for(unsigned view=0;view<4;++view){
            s.views[view]=structure<XrView>(XR_TYPE_VIEW);s.views[view].pose.orientation.w=1;
            s.views[view].pose.position={float(100+view),float(200+view),float(300+view)};
            s.views[view].fov={float(-4-int(view)),float(4+view),float(5+view),float(-5-int(view))};
        }
        for(unsigned eye=0;eye<2;++eye)views[eye].pose=s.views[eye].pose;
        projection=structure<XrCompositionLayerProjection>(XR_TYPE_COMPOSITION_LAYER_PROJECTION);
        projection.next=&projectionNext;projection.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
        projection.space=s.locateSpace;projection.viewCount=2;projection.views=views.data();
        layers[0]=header(projection);
        info=structure<XrFrameEndInfo>(XR_TYPE_FRAME_END_INFO);info.next=&frameNext;
        info.displayTime=12345;info.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
        info.layerCount=1;info.layers=layers.data();
        next.EndFrame=fakeEnd;next.ReleaseSwapchainImage=fakeRelease;next.WaitSwapchainImage=fakeWait;
        runtimeEnds=auditEnds=releases=waits=eventCount=0;releaseFails=false;downstreamResult=XR_SUCCESS;inspectSubmission=true;
        begin();
    }
    template<class T> static const XrCompositionLayerBaseHeader* header(const T& layer){return reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);}
    void begin(){
        s.frame=s.shouldRender=s.located=true;s.captures[0]=s.captures[1]=true;
        for(auto& outer:s.outer)outer.acquired=outer.waited=true;
        eventCount=0;
    }
    size_t submit(){
        allocation_probe::count=0;allocation_probe::enabled=true;
        const auto result=layerEndFrame(s.session,&info);
        allocation_probe::enabled=false;
        assert(result==downstreamResult);
        return allocation_probe::count;
    }
    void checkConversion()const{
        assert(received.session==s.session&&received.info.type==XR_TYPE_FRAME_END_INFO&&received.info.next==&frameNext);
        assert(received.info.displayTime==12345&&received.info.environmentBlendMode==XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND);
        assert(received.projectionCount==1&&received.projection.viewCount==4);
        assert(received.projection.next==&projectionNext&&received.projection.layerFlags==projection.layerFlags&&received.projection.space==reinterpret_cast<XrSpace>(2));
        for(unsigned view=0;view<4;++view){
            const auto& actual=received.views[view];
            assert(actual.type==XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW);
            assert(actual.pose.orientation.x==0&&actual.pose.orientation.y==0&&actual.pose.orientation.z==0&&actual.pose.orientation.w==1);
            assert(actual.pose.position.x==float(100+view)&&actual.pose.position.y==float(200+view)&&actual.pose.position.z==float(300+view));
            assert(actual.fov.angleLeft==float(-4-int(view))&&actual.fov.angleRight==float(4+view)&&actual.fov.angleUp==float(5+view)&&actual.fov.angleDown==float(-5-int(view)));
        }
        for(unsigned eye=0;eye<2;++eye){
            const auto& outer=received.views[eye];const auto& focus=received.views[eye+2];
            assert(!outer.next&&outer.subImage.swapchain==reinterpret_cast<XrSwapchain>(uintptr_t(20+eye)));
            assert(outer.subImage.imageRect.offset.x==0&&outer.subImage.imageRect.offset.y==0&&outer.subImage.imageArrayIndex==0);
            assert(outer.subImage.imageRect.extent.width==(eye?600:640)&&outer.subImage.imageRect.extent.height==(eye?400:480));
            assert(focus.next==(eye?&rightNext:&leftNext)&&focus.subImage.swapchain==reinterpret_cast<XrSwapchain>(uintptr_t(10+eye)));
            assert(focus.subImage.imageRect.offset.x==int32_t(7+eye)&&focus.subImage.imageRect.offset.y==int32_t(9+eye));
            assert(focus.subImage.imageRect.extent.width==100&&focus.subImage.imageRect.extent.height==200&&focus.subImage.imageArrayIndex==eye);
        }
        // The adapter must neither rewrite the caller's layers nor retain ownership.
        assert(projection.viewCount==2&&projection.views==views.data()&&views[0].pose.position.x==100);
        assert(!s.frame&&!s.shouldRender&&!s.located&&!s.captures[0]&&!s.captures[1]);
        assert(!s.outer[0].acquired&&!s.outer[1].acquired&&releases==2&&waits==0);
        assert(eventCount==4&&events[0]==1&&events[1]==2&&events[2]==3&&events[3]==4);
    }
};

void semantics(){
    {
        Fixture f;auto invalid=f.projection;invalid.viewCount=1;auto duplicate=f.projection;duplicate.layerFlags=0;
        f.layers[0]=Fixture::header(f.overlays[0]);f.layers[1]=nullptr;f.layers[2]=Fixture::header(invalid);
        f.layers[3]=Fixture::header(f.projection);f.layers[4]=Fixture::header(f.overlays[1]);f.layers[5]=Fixture::header(duplicate);f.info.layerCount=6;
        f.submit();f.checkConversion();
        assert(received.info.layerCount==3&&received.layers[0]==Fixture::header(f.overlays[0])&&received.layers[2]==Fixture::header(f.overlays[1]));
        assert(received.layers[1]!=Fixture::header(f.projection)&&runtimeEnds==1&&auditEnds==1);
    }
    for(unsigned count:{16u,17u,97u}){
        Fixture f;f.info.layerCount=count;
        for(unsigned i=0;i<count;++i)f.layers[i]=Fixture::header(f.overlays[i%2]);
        f.layers[count/2]=Fixture::header(f.projection);f.submit();f.checkConversion();
        assert(received.info.layerCount==count);
        for(unsigned i=0;i<count;++i)if(i!=count/2)assert(received.layers[i]==Fixture::header(f.overlays[i%2]));
    }
    // Each malformed or stale input must drop only the projection and keep overlays.
    for(unsigned scenario=0;scenario<12;++scenario){
        Fixture f;f.info.layerCount=2;f.layers[1]=Fixture::header(f.overlays[0]);
        switch(scenario){
        case 0:f.projection.views=nullptr;break;
        case 1:f.projection.viewCount=4;break;
        case 2:f.views[0].subImage.swapchain=f.views[1].subImage.swapchain;break;
        case 3:f.views[1].subImage.imageRect.extent.width=2000;break;
        case 4:f.projection.space=XR_NULL_HANDLE;break;
        case 5:s.captures[1]=false;break;
        case 6:s.locateTime=99;break;
        case 7:s.frame=false;break;
        case 8:s.shouldRender=false;break;
        case 9:s.located=false;break;
        case 10:s.appChains.at(f.views[0].subImage.swapchain).info.format=DXGI_FORMAT_R16G16B16A16_FLOAT;break;
        case 11:releaseFails=true;break;
        }
        f.submit();assert(received.info.layerCount==1&&received.layers[0]==Fixture::header(f.overlays[0])&&received.projectionCount==0);
        assert(runtimeEnds==1&&auditEnds==1&&!s.frame);
    }
    {
        Fixture f;f.info.layerCount=0;f.info.layers=nullptr;f.submit();
        assert(received.info.layerCount==0&&!received.info.layers&&runtimeEnds==1&&auditEnds==1&&releases==2);
    }
    {
        Fixture f;f.layers[0]=nullptr;f.submit();assert(received.info.layerCount==0&&!received.info.layers);
    }
    {
        Fixture f;s.quad=false;f.submit();assert(received.original==&f.info&&received.info.layers==f.layers.data());
        assert(received.projection.viewCount==2&&releases==0&&auditEnds==0&&!s.frame);
    }
    {
        Fixture f;s.quad=false;s.vrs=true;
        s.vrsTracked=true;s.vrsGazePose.orientation={0,std::sin(-.1f),0,std::cos(-.1f)};
        s.vrsWidth=s.vrsHeight=.1f;
        float rendered[16]{};rendered[0]=2;rendered[5]=4;rendered[8]=.2f;rendered[9]=-.1f;rendered[11]=1;
        lmuqv::VrsFocus actual;
        assert(lmuqv::xr_vrs_render_focus(0,rendered,actual));
        assert(std::abs((actual.left+actual.right)*.5f-(std::tan(.2f)+.6f))<1e-5f);
        s.vrsTracked=false;assert(lmuqv::xr_vrs_render_focus(0,rendered,actual));
        assert(std::abs(actual.left-.45f)<1e-5f); // Per-frame fixed fallback.
        rendered[11]=0;assert(!lmuqv::xr_vrs_render_focus(0,rendered,actual));
        f.views[0].pose.position.x=123;f.views[1].pose.position.x=456;
        f.submit();
        assert(received.original==&f.info&&received.info.layers==f.layers.data());
        assert(received.projection.viewCount==2&&releases==0&&auditEnds==0&&!s.frame);
        assert(received.projection.views[0].pose.position.x==123&&received.projection.views[1].pose.position.x==456);
        assert(s.vrsWidths[0]>0&&s.vrsHeights[1]>0);
    }
    {
        Fixture f;s.outer[0].waited=false;downstreamResult=XR_ERROR_RUNTIME_FAILURE;f.submit();
        assert(waits==1&&releases==2&&!s.frame&&auditEnds==1);
    }
    {
        Fixture f;s.focusOwnership=true;s.appChains.at(f.views[0].subImage.swapchain).focusLease=1;
        assert(layerEndFrame(s.session,&f.info)==XR_ERROR_CALL_ORDER_INVALID);
        assert(runtimeEnds==0&&releases==0&&auditEnds==0&&s.frame&&s.outer[0].acquired&&s.outer[1].acquired);
    }
}

void checkPose(const XrPosef& actual,const XrPosef& expected){
    const auto close=[](float a,float b){return std::abs(a-b)<0.00001f;};
    assert(close(actual.position.x,expected.position.x)&&close(actual.position.y,expected.position.y)&&close(actual.position.z,expected.position.z));
    assert(close(actual.orientation.x,expected.orientation.x)&&close(actual.orientation.y,expected.orientation.y)&&
           close(actual.orientation.z,expected.orientation.z)&&close(actual.orientation.w,expected.orientation.w));
}
void compensatedSubmission(bool differentSpace){
    Fixture f;
    if(differentSpace)f.projection.space=reinterpret_cast<XrSpace>(99);
    constexpr float h=0.7071067811865475f;
    // Left: cached outer is Z+90, focus is Z+180 and two units up.
    // Submitted outer is Z+180: its Z+90 correction moves focus two units left.
    s.views[0].pose={{0,0,h,h},{10,20,30}};
    s.views[2].pose={{0,0,1,0},{10,22,30}};
    f.views[0].pose={{0,0,1,0},{5,6,7}};
    // Right deliberately has a different correction (Y+180) and focus offset.
    s.views[1].pose={{0,0,0,1},{-1,2,3}};
    s.views[3].pose={{0,0,0,1},{-1,2,4}};
    f.views[1].pose={{0,1,0,0},{4,5,6}};
    const auto sourceViews=f.views;
    f.info.layerCount=2;f.layers[1]=Fixture::header(f.overlays[0]);
    assert(f.submit()==0);
    assert(received.projectionCount==1&&received.projection.viewCount==4);
    assert(received.projection.space==f.projection.space&&received.info.displayTime==f.info.displayTime);
    assert(received.info.layerCount==2&&received.layers[1]==Fixture::header(f.overlays[0]));
    for(unsigned eye=0;eye<2;++eye){
        assert(std::memcmp(&received.views[eye].pose,&sourceViews[eye].pose,sizeof(XrPosef))==0);
        assert(std::memcmp(&f.views[eye],&sourceViews[eye],sizeof(sourceViews[eye]))==0);
        assert(received.views[eye+2].next==sourceViews[eye].next);
        assert(std::memcmp(&received.views[eye+2].subImage,&sourceViews[eye].subImage,sizeof(XrSwapchainSubImage))==0);
    }
    checkPose(received.views[2].pose,{{0,0,h,-h},{3,6,7}});
    checkPose(received.views[3].pose,{{0,1,0,0},{4,5,5}});
    for(unsigned view=0;view<4;++view)assert(std::memcmp(&received.views[view].fov,&s.views[view].fov,sizeof(XrFovf))==0);
    assert(releases==2&&runtimeEnds==1&&!s.frame);
}
void invalidPoses(){
    for(unsigned source=0;source<6;++source)for(unsigned bad=0;bad<6;++bad){
        Fixture f;f.info.layerCount=2;f.layers[1]=Fixture::header(f.overlays[0]);
        auto& pose=source<2?f.views[source].pose:s.views[source-2].pose;
        switch(bad){
        case 0:pose.position.x=std::numeric_limits<float>::infinity();break;
        case 1:pose.position.z=std::numeric_limits<float>::quiet_NaN();break;
        case 2:pose.orientation.y=std::numeric_limits<float>::infinity();break;
        case 3:pose.orientation={0,0,0,0};break;
        case 4:pose.orientation={0,0,0,2};break;
        case 5:pose.orientation.w=std::numeric_limits<float>::quiet_NaN();break;
        }
        assert(f.submit()==0);
        assert(received.projectionCount==0&&received.info.layerCount==1&&received.layers[0]==Fixture::header(f.overlays[0]));
        assert(releases==2&&runtimeEnds==1&&!s.frame);
    }
}
void compensationEdges(){
    {
        // A layer may move its reference space without rewriting the pose values.
        Fixture f;f.projection.space=reinterpret_cast<XrSpace>(99);
        const auto cached=s.views;
        assert(f.submit()==0&&received.projectionCount==1&&received.projection.space==f.projection.space);
        for(unsigned view=0;view<4;++view)assert(std::memcmp(&received.views[view].pose,&cached[view].pose,sizeof(XrPosef))==0);
    }
    {
        // A physically identical quaternion with the opposite sign must not flip the offset.
        Fixture f;f.views[0].pose.orientation.w=-1;
        assert(f.submit()==0&&received.projectionCount==1);
        checkPose(received.views[2].pose,{{0,0,0,-1},{102,202,302}});
    }
    {
        // Accepted normalization drift must not scale the rotated focus displacement.
        Fixture f;s.views[0].pose.orientation.w=1.005f;s.views[2].pose.orientation.w=0.995f;
        f.views[0].pose.orientation={0,0,1.005f,0};
        assert(f.submit()==0&&received.projectionCount==1);
        assert(received.views[0].pose.orientation.z==1.005f);
        checkPose(received.views[2].pose,{{0,0,1,0},{98,198,302}});
    }
    {
        // Coincident outer/focus cameras preserve the submitted pose exactly.
        Fixture f;s.views[2].pose=s.views[0].pose;f.views[0].pose={{0,1,0,0},{1,2,3}};
        assert(f.submit()==0&&received.projectionCount==1);
        assert(std::memcmp(&received.views[2].pose,&f.views[0].pose,sizeof(XrPosef))==0);
    }
    {
        // Finite inputs whose transformed position cannot fit XrPosef must be rejected.
        Fixture f;const float large=std::numeric_limits<float>::max()*0.75f;
        s.views[0].pose.position.x=-large;s.views[2].pose.position.x=large;f.views[0].pose.position.x=large;
        assert(f.submit()==0&&received.projectionCount==0);
    }
}

size_t allocationTest(unsigned layerCount){
    Fixture f;f.info.layerCount=layerCount;
    for(unsigned i=1;i<layerCount;++i)f.layers[i]=Fixture::header(f.overlays[i%2]);
    const auto count=f.submit();f.checkConversion();
    assert(received.info.layerCount==layerCount);
    std::cout<<"EndFrame layers="<<layerCount<<" allocations="<<count<<'\n';return count;
}
void benchmark(){
    constexpr unsigned iterations=200000,samples=7;
    for(unsigned count:{1u,4u,16u,97u}){
        Fixture f;f.info.layerCount=count;
        for(unsigned i=1;i<count;++i)f.layers[i]=Fixture::header(f.overlays[i%2]);
        inspectSubmission=false;
        for(unsigned i=0;i<10000;++i){f.begin();assert(layerEndFrame(s.session,&f.info)==XR_SUCCESS);}
        std::array<double,samples> ns{};size_t allocations=0;
        for(unsigned sample=0;sample<samples;++sample){
            allocation_probe::count=0;allocation_probe::enabled=true;
            const auto start=std::chrono::steady_clock::now();
            for(unsigned i=0;i<iterations;++i){f.begin();assert(layerEndFrame(s.session,&f.info)==XR_SUCCESS);}
            const auto stop=std::chrono::steady_clock::now();allocation_probe::enabled=false;
            allocations=allocation_probe::count;ns[sample]=std::chrono::duration<double,std::nano>(stop-start).count()/iterations;
        }
        std::sort(ns.begin(),ns.end());
        std::cout<<"EndFrame benchmark diagnostics=0 layers="<<count<<" iterations="<<iterations<<" samples="<<samples
                 <<" ns/call median="<<ns[samples/2]<<" min="<<ns.front()<<" max="<<ns.back()
                 <<" allocations/call="<<double(allocations)/iterations<<" (fake runtime; no GPU work or waits)\n";
    }
}
void profileTest(){
    Fixture f;lmuqv::renderProfile.configure(true,nullptr);lmuqv::renderProfile.frame(nullptr,true);
    lmuqv::renderProfile.xr_frame_begin();lmuqv::renderProfile.frame(nullptr,false);f.submit();
    assert(lmuqv::renderProfile.samples(lmuqv::ProfileSlot::provider_submit)==1);
    f.begin();lmuqv::renderProfile.xr_frame_begin();f.submit();
    assert(lmuqv::renderProfile.samples(lmuqv::ProfileSlot::non_scene_provider_submit)==1);
    lmuqv::renderProfile.shutdown();std::cout<<"EndFrame profile scene/menu submission boundaries PASS\n";
}
void fullOuterFovTest(){
    Fixture f;
    s.fullOuterFov[0]={-.8f,.7f,.6f,-.55f};
    s.fullOuterFov[1]={-.7f,.8f,.6f,-.55f};
    XrFovf output[2]{};
    assert(lmuqv::xr_frame_full_outer_fov(output));
    assert(std::memcmp(output,s.fullOuterFov,sizeof(output))==0);
    assert(output[0].angleUp!=s.views[0].fov.angleUp);
    for(unsigned invalid=0;invalid<5;++invalid){
        s.quad=s.frame=s.shouldRender=s.located=true;s.locateTime=s.predictedTime;
        switch(invalid){
        case 0:s.quad=false;break;
        case 1:s.frame=false;break;
        case 2:s.shouldRender=false;break;
        case 3:s.located=false;break;
        case 4:--s.locateTime;break;
        }
        std::memset(output,0,sizeof(output));
        assert(!lmuqv::xr_frame_full_outer_fov(output));
        assert(output[0].angleUp==0&&output[1].angleDown==0);
    }
}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    const bool runBenchmark=argc>1&&!std::strcmp(argv[1],"--benchmark");
    const bool runProfile=argc>1&&!std::strcmp(argv[1],"--profile");
    SetEnvironmentVariableA("LMU_QV_DIAGNOSTICS",runProfile?"1":"0");
    if(runProfile){profileTest();return 0;}
    fullOuterFovTest();
    if(argc>1&&!std::strcmp(argv[1],"--different-space")){compensatedSubmission(true);return 0;}
    compensatedSubmission(false);compensatedSubmission(true);invalidPoses();compensationEdges();
    std::cout<<"EndFrame same/different-space compensation and invalid poses PASS\n";
    semantics();std::cout<<"EndFrame metadata, ordering, rejection, fallback, passthrough and ownership PASS\n";
    if(runBenchmark){benchmark();return 0;}
    bool allocationFree=true;for(unsigned count:{1u,4u,16u})allocationFree=(allocationTest(count)==0)&&allocationFree;
    std::cout.flush();assert(allocationFree&&"Common EndFrame submission must not allocate");
    std::cout<<"EndFrame common submission zero-allocation PASS\n";
    return 0;
}
