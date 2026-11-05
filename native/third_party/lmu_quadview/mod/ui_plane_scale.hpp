#pragma once
#include <cmath>
#include <cstring>

namespace lmuqv {
// The native mesh bakes XY from 2*tan(planeFov/2). Its shader uses planeFov
// again only to normalize curvature, then multiplies XY by the user's scale.
// Recover the uncropped XY extent without changing depth or that normalization.
inline float ui_plane_scale_factor(float planeFov,float renderedSpan,float fullSpan){
    if(!std::isfinite(planeFov)||!std::isfinite(renderedSpan)||!std::isfinite(fullSpan)||
       !(planeFov>1e-6f&&renderedSpan>1e-6f&&fullSpan<3.1415926f&&fullSpan>renderedSpan)||
       std::abs(planeFov-renderedSpan)>1e-4f)return 1.f;
    const float factor=std::tan(fullSpan*.5f)/std::tan(planeFov*.5f);
    return std::isfinite(factor)&&factor>1.f?factor:1.f;
}
class UiPlaneScaleScope {
public:
    UiPlaneScaleScope(void* scale,float factor){
        if(!scale||factor==1.f||!std::isfinite(factor)||factor<=0)return;
        float current{};std::memcpy(&current,scale,sizeof(current));
        const float corrected=current*factor;
        if(!std::isfinite(current)||!std::isfinite(corrected)||current<=0)return;
        field_=scale;std::memcpy(saved_,scale,sizeof(saved_));
        std::memcpy(scale,&corrected,sizeof(corrected));
    }
    ~UiPlaneScaleScope(){if(field_)std::memcpy(field_,saved_,sizeof(saved_));}
    UiPlaneScaleScope(const UiPlaneScaleScope&)=delete;
    UiPlaneScaleScope& operator=(const UiPlaneScaleScope&)=delete;
private:
    void* field_=nullptr;
    unsigned char saved_[sizeof(float)]{};
};
}
