#pragma once
#include "bridge.hpp"
#include <cstdint>
namespace lmuqv {
// Only the pinned LMU enumeration caller may normalize the final answer after
// upper layers return. Keep next chains, types and non-size fields intact.
inline bool normalize_native_views(bool enabled,std::uintptr_t callerRva,XrViewConfigurationType type,
                                  uint32_t capacity,uint32_t count,XrViewConfigurationView* views,
                                  uint32_t width,uint32_t height){
    if(!enabled||callerRva!=0x5bd214||type!=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO||
       capacity<2||count!=2||!views||!width||!height||width>16384||height>16384)return false;
    for(unsigned eye=0;eye<2;++eye)if(views[eye].type!=XR_TYPE_VIEW_CONFIGURATION_VIEW)return false;
    for(unsigned eye=0;eye<2;++eye){
        auto& v=views[eye];v.recommendedImageRectWidth=v.maxImageRectWidth=width;
        v.recommendedImageRectHeight=v.maxImageRectHeight=height;
        v.recommendedSwapchainSampleCount=v.maxSwapchainSampleCount=1;
    }
    return true;
}
}
