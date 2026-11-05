#pragma once
#include "bridge.hpp"

namespace lmuqv {
struct SamplingAttempt {
    bool requested=false,accepted=false,retried=false;
    XrResult sampledResult=XR_SUCCESS;
};
inline bool sampled_input_eligible(const XrSwapchainCreateInfo& info){
    return (info.usageFlags&XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT)&&
        !(info.usageFlags&XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)&&
        !(info.usageFlags&XR_SWAPCHAIN_USAGE_SAMPLED_BIT)&&!info.createFlags&&
        info.sampleCount==1&&info.arraySize==1&&info.faceCount==1&&info.mipCount==1;
}
// Only the optional usage bit changes. In particular, preserve the caller's next
// chain, format and dimensions and never retry a lost session or allocation error.
inline XrResult create_sampled_input(PFN_xrCreateSwapchain create,XrSession session,
    const XrSwapchainCreateInfo& info,XrSwapchain* output,bool enabled,SamplingAttempt& attempt){
    attempt={};
    if(!enabled||!sampled_input_eligible(info))return create(session,&info,output);
    auto sampled=info;sampled.usageFlags|=XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    XrSwapchain candidate=XR_NULL_HANDLE;
    attempt.requested=true;
    const XrResult result=create(session,&sampled,&candidate);attempt.sampledResult=result;
    if(XR_SUCCEEDED(result)){*output=candidate;attempt.accepted=true;return result;}
    if(result!=XR_ERROR_FEATURE_UNSUPPORTED&&result!=XR_ERROR_VALIDATION_FAILURE&&result!=XR_ERROR_RUNTIME_FAILURE)
        return result;
    attempt.retried=true;return create(session,&info,output);
}
}
