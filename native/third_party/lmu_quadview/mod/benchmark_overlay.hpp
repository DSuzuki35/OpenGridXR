#pragma once
#include "xr_layer.hpp"
#include "benchmark_native.hpp"
#include <array>
#include <memory>
namespace lmuqv { namespace benchmark {
class Overlay {
#define BENCH_XR(X) X(EnumerateSwapchainFormats) X(CreateReferenceSpace) X(DestroySpace) X(CreateSwapchain) X(DestroySwapchain) X(EnumerateSwapchainImages) X(AcquireSwapchainImage) X(WaitSwapchainImage) X(ReleaseSwapchainImage)
#define MEMBER(n) PFN_xr##n n{};
 BENCH_XR(MEMBER)
#undef MEMBER
 XrSwapchain chain_=XR_NULL_HANDLE;XrSpace space_=XR_NULL_HANDLE;
 std::array<XrSwapchainImageD3D11KHR,64> images_{};uint32_t imageCount_=0,index_=0;
 std::unique_ptr<uint8_t[]> bitmap_;int32_t bitmapSequence_=0,width_=0,height_=0;
 bool acquired_=false,waited_=false,ready_=false,rgba_=false;
 Config config_;XrCompositionLayerQuad quad_{XR_TYPE_COMPOSITION_LAYER_QUAD};
 XrResult last_=XR_SUCCESS;
 bool fail(XrResult result){last_=result;return false;}
 bool retire(){
 if(!acquired_)return true;
 if(!waited_){XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=0;auto result=WaitSwapchainImage(chain_,&wait);if(result!=XR_SUCCESS)return fail(result);waited_=true;}
 XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};auto result=ReleaseSwapchainImage(chain_,&release);if(XR_FAILED(result))return fail(result);
 acquired_=waited_=false;return true;
 }
public:
 ~Overlay(){reset();}
 XrResult result()const{return last_;}
 bool available()const{return chain_&&space_;}
 bool initialize(PFN_xrGetInstanceProcAddr get,XrInstance instance,XrSession session){
 reset();if(!get)return fail(XR_ERROR_FUNCTION_UNSUPPORTED);
#define LOAD(n) {PFN_xrVoidFunction fn{};auto r=get(instance,"xr" #n,&fn);n=reinterpret_cast<PFN_xr##n>(fn);if(XR_FAILED(r)||!n)return fail(XR_ERROR_FUNCTION_UNSUPPORTED);}
 BENCH_XR(LOAD)
#undef LOAD
 uint32_t count=0;auto r=EnumerateSwapchainFormats(session,0,&count,nullptr);if(XR_FAILED(r)||count>128||!count)return fail(XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED);
 std::array<int64_t,128> formats{};r=EnumerateSwapchainFormats(session,count,&count,formats.data());if(XR_FAILED(r)||count>formats.size())return fail(r);
 int64_t format=0;for(auto wanted:{DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB}){for(uint32_t i=0;i<count;++i)if(formats[i]==wanted)format=wanted;if(format)break;}
 if(!format)return fail(XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED);rgba_=format==DXGI_FORMAT_R8G8B8A8_UNORM||format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
 XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;space.poseInReferenceSpace.orientation.w=1;
 r=CreateReferenceSpace(session,&space,&space_);if(XR_FAILED(r))return fail(r);
 XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;ci.format=format;ci.sampleCount=ci.arraySize=ci.faceCount=ci.mipCount=1;ci.width=MaxWidth;ci.height=MaxHeight;
 r=CreateSwapchain(session,&ci,&chain_);if(XR_FAILED(r))return fail(r);
 r=EnumerateSwapchainImages(chain_,0,&imageCount_,nullptr);if(XR_FAILED(r)||!imageCount_||imageCount_>images_.size())return fail(XR_ERROR_RUNTIME_FAILURE);
 for(auto& image:images_)image.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
 r=EnumerateSwapchainImages(chain_,imageCount_,&imageCount_,reinterpret_cast<XrSwapchainImageBaseHeader*>(images_.data()));if(XR_FAILED(r)||!imageCount_||imageCount_>images_.size())return fail(XR_ERROR_RUNTIME_FAILURE);
 for(uint32_t i=0;i<imageCount_;++i)if(!images_[i].texture)return fail(XR_ERROR_RUNTIME_FAILURE);
 bitmap_.reset(new(std::nothrow) uint8_t[MaxWidth*MaxHeight*4]);if(!bitmap_)return fail(XR_ERROR_OUT_OF_MEMORY);last_=XR_SUCCESS;return true;
 }
 void reset(){if(chain_&&DestroySwapchain){retire();DestroySwapchain(chain_);}if(space_&&DestroySpace)DestroySpace(space_);chain_=XR_NULL_HANDLE;space_=XR_NULL_HANDLE;bitmap_.reset();acquired_=waited_=ready_=false;imageCount_=0;bitmapSequence_=width_=height_=0;images_={};config_=Config{};}
 const XrCompositionLayerQuad* prepare(Shared& shared,ID3D11DeviceContext* context,bool shouldRender){
 last_=XR_SUCCESS;read_config(shared,config_);
 if(!available()||!bitmap_||!context){fail(XR_ERROR_INITIALIZATION_FAILED);return nullptr;}
 if(!config_.visible||!shouldRender){retire();return nullptr;}
 auto& h=shared.header;const int32_t sequence=sequence_load(h.bitmapSequence);
 bool update=sequence!=bitmapSequence_&&!(sequence&1)&&sequence!=0;
 int32_t width=h.bitmapWidth,height=h.bitmapHeight;
 if(update){if(width<=0||height<=0||width>int32_t(MaxWidth)||height>int32_t(MaxHeight)){fail(XR_ERROR_VALIDATION_FAILURE);return nullptr;}
 std::memcpy(bitmap_.get(),shared.bitmap,size_t(width)*height*4);MemoryBarrier();if(sequence_load(h.bitmapSequence)!=sequence)update=false;}
 if(update){
 // A failed release is retried before touching the image again; ownership is retained.
 if(acquired_&&waited_&&!retire())return nullptr;
 if(!acquired_){XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};auto r=AcquireSwapchainImage(chain_,&acquire,&index_);if(XR_FAILED(r)){fail(r);return nullptr;}acquired_=true;}
 if(!waited_){XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=0;auto r=WaitSwapchainImage(chain_,&wait);if(r!=XR_SUCCESS){fail(r);return nullptr;}waited_=true;}
 if(index_>=imageCount_){retire();fail(XR_ERROR_RUNTIME_FAILURE);return nullptr;}
 if(rgba_)for(size_t i=0;i<size_t(width)*height*4;i+=4)std::swap(bitmap_[i],bitmap_[i+2]);
 D3D11_BOX box{0,0,0,UINT(width),UINT(height),1};context->UpdateSubresource(images_[index_].texture,0,&box,bitmap_.get(),UINT(width)*4,0);
 if(!retire())return nullptr;bitmapSequence_=sequence;width_=width;height_=height;ready_=true;
 }else if(acquired_){if(!retire())return nullptr;}
 if(!ready_)return nullptr;
 quad_={XR_TYPE_COMPOSITION_LAYER_QUAD};quad_.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;quad_.space=space_;quad_.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
 quad_.subImage.swapchain=chain_;quad_.subImage.imageRect.extent={width_,height_};quad_.pose.orientation.w=1;quad_.pose.position={config_.x,config_.y,-config_.distance};quad_.size={config_.width,config_.width*height_/width_};return &quad_;
 }
#undef BENCH_XR
};
} }
