#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../xr_layer.cpp"
#include <cassert>
#include <iostream>
#include <thread>
namespace lmuqv {
bool engine_start(ID3D11DeviceContext*){assert(false);return false;}
void engine_stop(){renderProfile.shutdown();}
unsigned engine_game_eye(XrSwapchain){return 2;}
bool stereo_audit_prepare(bool,const wchar_t*,void(*)(const char*)){return false;}
void stereo_audit_stop(){}
void stereo_audit_frame_end(){}
}
namespace {
bool runtimeOwned=true,waited=true,failRuntimeRelease=false;
unsigned releases=0,acquires=0,ends=0,begins=0,destroys=0;
XrResult XRAPI_CALL fakeRelease(XrSwapchain,const XrSwapchainImageReleaseInfo*){
    assert(runtimeOwned&&waited);++releases;if(failRuntimeRelease)return XR_ERROR_RUNTIME_FAILURE;runtimeOwned=waited=false;return XR_SUCCESS;
}
XrResult XRAPI_CALL fakeAcquire(XrSwapchain,const XrSwapchainImageAcquireInfo*,uint32_t* index){assert(!runtimeOwned);runtimeOwned=true;waited=false;++acquires;*index=0;return XR_SUCCESS;}
XrResult XRAPI_CALL fakeWait(XrSwapchain,const XrSwapchainImageWaitInfo*){assert(runtimeOwned);waited=true;return XR_SUCCESS;}
XrResult XRAPI_CALL fakeBegin(XrSession,const XrFrameBeginInfo*){++begins;return XR_SUCCESS;}
XrResult XRAPI_CALL fakeEnd(XrSession,const XrFrameEndInfo*){++ends;return XR_SUCCESS;}
XrResult XRAPI_CALL fakeDestroy(XrSwapchain){++destroys;return XR_SUCCESS;}
}
int main(){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetEnvironmentVariableA("LMU_QV_DIAGNOSTICS","0");
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;D3D_FEATURE_LEVEL level{};assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=8;td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET;ID3D11Texture2D* texture=nullptr;assert(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&texture)));
    next.ReleaseSwapchainImage=fakeRelease;next.AcquireSwapchainImage=fakeAcquire;next.WaitSwapchainImage=fakeWait;next.BeginFrame=fakeBegin;next.EndFrame=fakeEnd;next.DestroySwapchain=fakeDestroy;
    s.quad=true;s.session=reinterpret_cast<XrSession>(1);s.device=device;s.context=context;device->AddRef();context->AddRef();assert(lmuqv::xr_prepare_focus_image_ownership());
    const auto chain=reinterpret_cast<XrSwapchain>(11);AppChain app;app.eye=0;app.images.push_back(texture);app.acquired.push_back({0,true});s.appChains.emplace(chain,std::move(app));
    assert(!lmuqv::xr_prepare_focus_image_ownership());s.frame=s.shouldRender=s.located=true;s.frames=7;s.predictedTime=s.locateTime=100;
    lmuqv::FocusImageLease lease,other;assert(lmuqv::xr_hold_focus_image(0,lease)&&lease.texture==texture);const auto stale=lease;assert(lmuqv::xr_focus_lease_valid(lease));assert(!lmuqv::xr_hold_focus_image(0,other));
    bool foreignValid=true,foreignRetire=true;std::thread wrongThread([&]{foreignValid=lmuqv::xr_focus_lease_valid(lease);foreignRetire=lmuqv::xr_retire_focus_image(lease);});wrongThread.join();assert(!foreignValid&&!foreignRetire&&lmuqv::xr_focus_lease_valid(lease));
    auto ri=structure<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);assert(layerReleaseSwapchainImage(chain,&ri)==XR_SUCCESS&&releases==0&&runtimeOwned);assert(layerReleaseSwapchainImage(chain,&ri)==XR_ERROR_CALL_ORDER_INVALID);
    auto ai=structure<XrSwapchainImageAcquireInfo>(XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO);uint32_t index=99;assert(layerAcquireSwapchainImage(chain,&ai,&index)==XR_ERROR_CALL_ORDER_INVALID&&acquires==0);
    auto bi=structure<XrFrameBeginInfo>(XR_TYPE_FRAME_BEGIN_INFO);auto ei=structure<XrFrameEndInfo>(XR_TYPE_FRAME_END_INFO);assert(layerBeginFrame(s.session,&bi)==XR_ERROR_CALL_ORDER_INVALID&&begins==0);assert(layerEndFrame(s.session,&ei)==XR_ERROR_CALL_ORDER_INVALID&&ends==0);assert(layerEndSession(s.session)==XR_ERROR_CALL_ORDER_INVALID&&lmuqv::xr_focus_lease_valid(lease));assert(layerDestroySwapchain(chain)==XR_ERROR_CALL_ORDER_INVALID&&destroys==0);
    // The layer may queue GPU work while it still owns the waited image, even
    // after the application's release was acknowledged. It must retire before
    // downstream release/submission. No packet or eye draw is suppressed here.
    assert(lmuqv::xr_focus_lease_valid(lease));ID3D11RenderTargetView* rtv=nullptr;assert(SUCCEEDED(device->CreateRenderTargetView(lease.texture,nullptr,&rtv)));const float marker[4]{.25f,.5f,.75f,1};context->ClearRenderTargetView(rtv,marker);rtv->Release();
    failRuntimeRelease=true;assert(!lmuqv::xr_retire_focus_image(lease)&&lmuqv::xr_focus_lease_valid(lease)&&runtimeOwned);failRuntimeRelease=false;
    assert(lmuqv::xr_retire_focus_image(lease)&&lease.token==0&&!runtimeOwned&&releases==2);assert(!lmuqv::xr_focus_lease_valid(stale));assert(!lmuqv::xr_retire_focus_image(lease));
    // Reacquisition receives a new token, and no hold is legal before a wait.
    assert(layerAcquireSwapchainImage(chain,&ai,&index)==XR_SUCCESS);assert(!lmuqv::xr_hold_focus_image(0,other));auto wi=structure<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);wi.timeout=0;assert(layerWaitSwapchainImage(chain,&wi)==XR_SUCCESS);
    s.focusBeginInFlight=true;assert(!lmuqv::xr_hold_focus_image(0,other));s.focusBeginInFlight=false;assert(lmuqv::xr_hold_focus_image(0,other)&&other.token!=stale.token);
    assert(lmuqv::xr_retire_focus_image(other)&&runtimeOwned);assert(layerReleaseSwapchainImage(chain,&ri)==XR_SUCCESS&&!runtimeOwned);
    assert(layerEndFrame(s.session,&ei)==XR_SUCCESS&&ends==1&&!s.frame);assert(!lmuqv::xr_focus_lease_valid(stale));assert(layerDestroySwapchain(chain)==XR_SUCCESS&&destroys==1);
    {std::unique_lock<std::recursive_mutex> lock(stateMutex);clearSession(lock);}assert(!s.focusOwnership);context->ClearState();texture->Release();context->Release();device->Release();
    std::cout<<"Focus image ownership PASS: deferred downstream release, owner-thread access, retry, reacquire, stale tokens, frame/submission/destruction barriers; no production scheduler enabled\n";
}
