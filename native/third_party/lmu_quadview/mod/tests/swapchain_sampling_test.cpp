#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../swapchain_sampling.hpp"
#include <cassert>
#include <iostream>
#include <vector>
namespace {
std::vector<XrSwapchainCreateInfo> calls;
XrResult first=XR_SUCCESS,last=XR_SUCCESS;
XrResult XRAPI_CALL create(XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* output){
    calls.push_back(*info);*output=reinterpret_cast<XrSwapchain>(uintptr_t(123));
    return calls.size()==1?first:last;
}
void preserved(const XrSwapchainCreateInfo& a,const XrSwapchainCreateInfo& b){
    assert(a.type==b.type&&a.next==b.next&&a.createFlags==b.createFlags&&a.format==b.format&&
        a.sampleCount==b.sampleCount&&a.width==b.width&&a.height==b.height&&
        a.faceCount==b.faceCount&&a.arraySize==b.arraySize&&a.mipCount==b.mipCount);
}
}
int main(){
    using namespace lmuqv;
    XrSwapchainCreateInfo info{};info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;
    info.width=655;info.height=611;info.sampleCount=info.arraySize=info.faceCount=info.mipCount=1;
    info.format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    int extension=42;info.next=&extension;
    SamplingAttempt attempt;XrSwapchain output{};
    assert(create_sampled_input(create,{},info,&output,false,attempt)==XR_SUCCESS&&calls.size()==1&&!attempt.requested);
    assert(calls[0].usageFlags==info.usageFlags);preserved(calls[0],info);calls.clear();
    assert(create_sampled_input(create,{},info,&output,true,attempt)==XR_SUCCESS&&attempt.accepted&&calls.size()==1);
    assert(calls[0].usageFlags==(info.usageFlags|XR_SWAPCHAIN_USAGE_SAMPLED_BIT));preserved(calls[0],info);
    assert(!(info.usageFlags&XR_SWAPCHAIN_USAGE_SAMPLED_BIT));
    for(auto rejected:{XR_ERROR_FEATURE_UNSUPPORTED,XR_ERROR_VALIDATION_FAILURE,XR_ERROR_RUNTIME_FAILURE}){
        calls.clear();first=rejected;last=XR_SUCCESS;
        assert(create_sampled_input(create,{},info,&output,true,attempt)==XR_SUCCESS&&calls.size()==2&&attempt.retried&&!attempt.accepted);
        assert(attempt.sampledResult==rejected&&calls[1].usageFlags==info.usageFlags);preserved(calls[1],info);
    }
    for(auto fatal:{XR_ERROR_SESSION_LOST,XR_ERROR_OUT_OF_MEMORY,XR_ERROR_LIMIT_REACHED}){
        calls.clear();first=fatal;output=XR_NULL_HANDLE;
        assert(create_sampled_input(create,{},info,&output,true,attempt)==fatal&&calls.size()==1&&!attempt.retried&&output==XR_NULL_HANDLE);
    }
    calls.clear();first=XR_ERROR_FEATURE_UNSUPPORTED;last=XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
    assert(create_sampled_input(create,{},info,&output,true,attempt)==last&&calls.size()==2);
    first=last=XR_SUCCESS;
    for(unsigned variant=0;variant<8;++variant){
        calls.clear();auto in=info;
        switch(variant){
            case 0:in.usageFlags|=XR_SWAPCHAIN_USAGE_SAMPLED_BIT;break;
            case 1:in.usageFlags=XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;break;
            case 2:in.usageFlags|=XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;break;
            case 3:in.sampleCount=2;break;
            case 4:in.arraySize=2;break;
            case 5:in.faceCount=6;break;
            case 6:in.mipCount=2;break;
            case 7:in.createFlags=XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;break;
        }
        assert(create_sampled_input(create,{},in,&output,true,attempt)==XR_SUCCESS&&calls.size()==1&&!attempt.requested);
        assert(calls[0].usageFlags==in.usageFlags);preserved(calls[0],in);
    }
    std::cout<<"Swapchain sampling tests passed: opt-in, immutable metadata/next, accepted flags, original-usage fallback, fatal errors, depth/array/MSAA/static/mip guards\n";
}
