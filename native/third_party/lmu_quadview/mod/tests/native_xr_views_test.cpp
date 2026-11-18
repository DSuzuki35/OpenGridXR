#include "../native_xr_views.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
int main(){
    XrViewConfigurationView views[2]{};
    int marker=1;
    for(auto& v:views){v.type=XR_TYPE_VIEW_CONFIGURATION_VIEW;v.next=&marker;
        v.recommendedImageRectWidth=v.maxImageRectWidth=5928;
        v.recommendedImageRectHeight=v.maxImageRectHeight=5798;}
    XrViewConfigurationView before[2];std::memcpy(before,views,sizeof(views));
    using lmuqv::normalize_native_views;
    for(auto caller:{0x5bd0ebu,0x1234u}){
        assert(!normalize_native_views(true,caller,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,2,views,2224,2182));
        assert(std::memcmp(before,views,sizeof(views))==0);
    }
    assert(!normalize_native_views(false,0x5bd214,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,2,views,2224,2182));
    assert(!normalize_native_views(true,0x5bd214,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,1,2,views,2224,2182));
    assert(!normalize_native_views(true,0x5bd214,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,4,views,2224,2182));
    assert(!normalize_native_views(true,0x5bd214,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO,2,2,views,2224,2182));
    assert(std::memcmp(before,views,sizeof(views))==0);
    assert(normalize_native_views(true,0x5bd214,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,2,views,2224,2182));
    for(const auto& v:views){assert(v.recommendedImageRectWidth==2224&&v.maxImageRectWidth==2224);
        assert(v.recommendedImageRectHeight==2182&&v.maxImageRectHeight==2182);
        assert(v.next==&marker&&v.type==XR_TYPE_VIEW_CONFIGURATION_VIEW);}
    // A relaunch with log 2's runtime selection must not reuse log 1's sizes.
    assert(normalize_native_views(true,0x5bd214,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,2,views,1682,1990));
    assert(views[0].recommendedImageRectWidth==1682&&views[1].recommendedImageRectHeight==1990);
    std::cout<<"Native XR view boundary: reported dimensions, next chains, caller and capacity guards passed\n";
}
