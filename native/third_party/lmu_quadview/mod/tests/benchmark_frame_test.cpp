// Reuse the real adapter fixture and its stubs, then exercise benchmark sampling.
#define main frame_submission_regression_main
#include "frame_submission_test.cpp"
#undef main
namespace benchmarkFake {
ID3D11Device* device=nullptr;ID3D11Texture2D* texture=nullptr;unsigned acquired=0,released=0,waits=0,destroyed=0;bool timeout=false,releaseFail=false,rgba=false;XrSpace view=reinterpret_cast<XrSpace>(2);
XrResult XRAPI_CALL formats(XrSession,uint32_t cap,uint32_t* count,int64_t* out){*count=1;if(cap)*out=rgba?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_B8G8R8A8_UNORM;return XR_SUCCESS;}
XrResult XRAPI_CALL space(XrSession,const XrReferenceSpaceCreateInfo* info,XrSpace* out){assert(info->referenceSpaceType==XR_REFERENCE_SPACE_TYPE_VIEW&&info->poseInReferenceSpace.orientation.w==1);*out=view;return XR_SUCCESS;}
XrResult XRAPI_CALL destroySpace(XrSpace){return XR_SUCCESS;}
XrResult XRAPI_CALL create(XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* out){D3D11_TEXTURE2D_DESC d{};d.Width=info->width;d.Height=info->height;d.Format=DXGI_FORMAT(info->format);d.ArraySize=d.MipLevels=d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET;assert(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&texture)));*out=reinterpret_cast<XrSwapchain>(3);return XR_SUCCESS;}
XrResult XRAPI_CALL destroy(XrSwapchain){++destroyed;texture->Release();texture=nullptr;return XR_SUCCESS;}
XrResult XRAPI_CALL images(XrSwapchain,uint32_t cap,uint32_t* count,XrSwapchainImageBaseHeader* out){*count=1;if(cap)reinterpret_cast<XrSwapchainImageD3D11KHR*>(out)->texture=texture;return XR_SUCCESS;}
XrResult XRAPI_CALL acquire(XrSwapchain,const XrSwapchainImageAcquireInfo*,uint32_t* index){++acquired;*index=0;return XR_SUCCESS;}
XrResult XRAPI_CALL wait(XrSwapchain,const XrSwapchainImageWaitInfo* info){assert(info->timeout==0);++waits;return timeout?XR_TIMEOUT_EXPIRED:XR_SUCCESS;}
XrResult XRAPI_CALL release(XrSwapchain,const XrSwapchainImageReleaseInfo*){++released;return releaseFail?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;}
XrResult XRAPI_CALL get(XrInstance,const char* name,PFN_xrVoidFunction* out){
#define FN(x,f) if(!strcmp(name,"xr" #x)){*out=reinterpret_cast<PFN_xrVoidFunction>(f);return XR_SUCCESS;}
 FN(EnumerateSwapchainFormats,formats) FN(CreateReferenceSpace,space) FN(DestroySpace,destroySpace) FN(CreateSwapchain,create) FN(DestroySwapchain,destroy) FN(EnumerateSwapchainImages,images) FN(AcquireSwapchainImage,acquire) FN(WaitSwapchainImage,wait) FN(ReleaseSwapchainImage,release)
 return XR_ERROR_FUNCTION_UNSUPPORTED;
}
}
int main(int argc,char** argv){
 if(argc>1&&!strcmp(argv[1],"--diagnostics-off")){SetEnvironmentVariableA("LMU_QV_DIAGNOSTICS","0");SetEnvironmentVariableA("LMU_QV_BENCHMARK","1");benchmarkInitialize();assert(!benchmarkEnabled);std::cout<<"benchmark diagnostics-off PASS\n";return 0;}
 SetEnvironmentVariableA("LMU_QV_BENCHMARK",nullptr);benchmarkInitialize();assert(!benchmarkEnabled);

 Fixture f;assert(benchmarkTransport.open());benchmarkEnabled=true;benchmarkBeginQpc=lmuqv::benchmark::ticks();
 s.sceneFocus[0]=s.sceneFocus[1]=true;s.running=true;
 s.dynamicFocus.configure(true,70,120,2);s.dynamicFrameScale=80; // next controller value is 100
 s.views[2].fov={-.4f,.3f,.25f,-.2f};s.views[3].fov={-.35f,.4f,.3f,-.25f};
 assert(layerEndFrame(s.session,&f.info)==XR_SUCCESS);
 auto* data=benchmarkTransport.data();assert(data&&data->header.counter==1&&data->header.projections==1);
 assert((data->header.focusFlags&3)==3&&data->header.focusScale==80&&data->header.focusFrame==s.frames);
 assert(std::abs(data->header.leftHorizontal-.7f*57.2957795f)<1e-4f&&std::abs(data->header.rightVertical-.55f*57.2957795f)<1e-4f);
 lmuqv::benchmark::Sample sample{};assert(lmuqv::benchmark::read_sample(*data,1,sample));assert((sample.flags&7)==7&&std::isnan(sample.gpuMs)&&sample.appMs>=0&&sample.providerMs>=0);
 f.begin();benchmarkBeginQpc=lmuqv::benchmark::ticks();s.sceneFocus[0]=s.sceneFocus[1]=true;assert(layerEndFrame(s.session,&f.info)==XR_SUCCESS);assert(lmuqv::benchmark::read_sample(*data,2,sample)&&std::isfinite(sample.intervalMs));
 f.begin();benchmarkBeginQpc=lmuqv::benchmark::ticks();s.sceneFocus[0]=true;s.sceneFocus[1]=false;assert(layerEndFrame(s.session,&f.info)==XR_SUCCESS);assert(lmuqv::benchmark::read_sample(*data,3,sample)&&!(sample.flags&1)&&std::isnan(sample.intervalMs));
 assert(!(data->header.focusFlags&1)); // do not display stale scene FOV over a menu
 f.begin();benchmarkBeginQpc=lmuqv::benchmark::ticks();f.views[0].subImage.imageRect.extent.width=-1;assert(layerEndFrame(s.session,&f.info)==XR_SUCCESS);assert(data->header.reason==2&&data->header.status==2);
 f.begin();benchmarkBeginQpc=lmuqv::benchmark::ticks();s.shouldRender=false;assert(layerEndFrame(s.session,&f.info)==XR_SUCCESS);assert(data->header.status==1&&data->header.reason==0);
 // A real adapter EndFrame preserves the scene when the overlay cannot acquire or fit.
 D3D_FEATURE_LEVEL feature{};assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&benchmarkFake::device,&feature,&s.context)));
 benchmarkOverlayReady=benchmarkOverlay.initialize(benchmarkFake::get,reinterpret_cast<XrInstance>(1),s.session);assert(benchmarkOverlayReady);benchmarkLayerLimit=128;
 data->header.visible=1;data->header.bitmapSequence=2;data->header.bitmapWidth=2;data->header.bitmapHeight=1;
 f.views[0].subImage.imageRect.extent.width=100;
 f.begin();benchmarkBeginQpc=lmuqv::benchmark::ticks();benchmarkFake::timeout=true;assert(layerEndFrame(s.session,&f.info)==XR_SUCCESS);assert(received.info.layerCount==1&&received.projectionCount==1&&data->header.status==3);
 f.begin();benchmarkBeginQpc=lmuqv::benchmark::ticks();benchmarkFake::timeout=false;assert(layerEndFrame(s.session,&f.info)==XR_SUCCESS);assert(received.info.layerCount==2&&received.projectionCount==1&&data->header.status==0);
 f.begin();benchmarkBeginQpc=lmuqv::benchmark::ticks();benchmarkLayerLimit=1;assert(layerEndFrame(s.session,&f.info)==XR_SUCCESS);assert(received.info.layerCount==1&&received.projectionCount==1);
 benchmarkReset();s.context->Release();s.context=nullptr;benchmarkFake::device->Release();benchmarkFake::device=nullptr;assert(!benchmarkEnabled&&!benchmarkTransport.data());std::cout<<"benchmark frame integration PASS\n";
}
