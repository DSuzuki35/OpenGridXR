#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../xr_layer.hpp"
#include "../benchmark_overlay.hpp"
#include <cassert>
#include <iostream>
namespace {
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
int main(){
 ID3D11DeviceContext* context=nullptr;D3D_FEATURE_LEVEL level{};assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
 auto shared=std::make_unique<lmuqv::benchmark::Shared>();lmuqv::benchmark::initialize(*shared,1,1000);shared->header.visible=1;shared->header.bitmapWidth=2;shared->header.bitmapHeight=1;shared->header.bitmapSequence=2;shared->bitmap[0]=11;shared->bitmap[1]=22;shared->bitmap[2]=33;shared->bitmap[3]=255;
 for(bool useRgba:{false,true}){rgba=useRgba;lmuqv::benchmark::Overlay overlay;assert(overlay.initialize(get,reinterpret_cast<XrInstance>(1),reinterpret_cast<XrSession>(1)));
 timeout=true;unsigned before=acquired;assert(!overlay.prepare(*shared,context,true));assert(acquired==before+1);assert(!overlay.prepare(*shared,context,true)&&acquired==before+1);
 timeout=false;const auto* quad=overlay.prepare(*shared,context,true);assert(quad&&quad->space==view&&quad->subImage.imageRect.extent.width==2&&quad->pose.position.z==-1.2f);
 D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Texture2D* staging=nullptr;assert(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging)));context->CopyResource(staging,texture);D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&map)));const auto* bytes=static_cast<const uint8_t*>(map.pData);assert(bytes[0]==(rgba?33:11)&&bytes[2]==(rgba?11:33));context->Unmap(staging,0);staging->Release();
 shared->header.visible=0;before=acquired;assert(!overlay.prepare(*shared,context,true)&&acquired==before);shared->header.visible=1;
 shared->header.bitmapSequence+=2;releaseFail=true;assert(!overlay.prepare(*shared,context,true));before=acquired;assert(!overlay.prepare(*shared,context,true)&&acquired==before);releaseFail=false;assert(overlay.prepare(*shared,context,true));
 shared->header.bitmapSequence|=1;assert(overlay.prepare(*shared,context,true));shared->header.bitmapSequence+=1;overlay.reset();}
 assert(destroyed==2&&released>0&&waits>=released);context->Release();device->Release();std::cout<<"benchmark overlay fake runtime PASS\n";
}
