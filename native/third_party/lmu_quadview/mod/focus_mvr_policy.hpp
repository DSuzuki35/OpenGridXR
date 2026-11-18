#pragma once
#include "quad_math.hpp"
#include <algorithm>
#include <chrono>
namespace lmuqv {
// Per-eye retry timing adapted from Williem3's Sim-QVFR MVR recovery.
struct FocusRetry {
    using Clock = std::chrono::steady_clock;
    Clock::time_point retryAt{};
    unsigned waitSeconds{}, stableFrames{}, missingResolves{};
    bool ready(Clock::time_point now) {
        if (now < retryAt) return false;
        retryAt = {};
        return true;
    }
    void failed(Clock::time_point now) {
        waitSeconds = waitSeconds ? std::min(2 * waitSeconds, 20u) : 3u;
        retryAt = now + std::chrono::seconds(waitSeconds);
        stableFrames = missingResolves = 0;
    }
    void reused() {
        if (waitSeconds && ++stableFrames == 600) *this = {};
    }
};
struct FocusAtlas {unsigned width=0,height=0;float scale=0;};
inline bool make_focus_atlas(unsigned w,unsigned h,unsigned fw,unsigned fh,FocusAtlas& out){
    constexpr unsigned limit=16384;
    if(!w||!h||!fw||!fh||w>limit||h>limit||fw>limit-w||fh>limit)return false;
    unsigned aw = w + fw, ah = std::max(h, fh);
    // The limiting axis sets the scale. Round the other allocation down:
    // LMU itself ceils each (float) product to get the rendered viewport.
    if (aw * h >= ah * w) ah = aw * h / w;
    else aw = ah * w / h;
    if (aw > limit || ah > limit) return false;
    float scale = std::min(float(w) / aw, float(h) / ah);
    if (aw * scale > w || ah * scale > h)
        scale = std::nextafter(scale, 0.0f);
    // At near-unity scales float multiplication can round a product exactly
    // onto the previous pixel. Include it if the allocation still fits.
    if (std::ceil(aw * scale) < w) ++aw;
    if (std::ceil(ah * scale) < h) ++ah;
    if (aw > limit || ah > limit || std::ceil(aw * scale) != w || std::ceil(ah * scale) != h)
        return false;
    out = {aw, ah, scale};
    return true;
}
inline bool focus_clip_remap(const Matrix& outer,const Matrix& focus,std::array<float,4>& r){
    if(outer[11]!=1||focus[11]!=1||outer[15]!=0||focus[15]!=0||!(outer[0]>0&&outer[5]>0))return false;
    const float x=focus[0]/outer[0],y=focus[5]/outer[5];
    r={x,focus[8]-x*outer[8],y,focus[9]-y*outer[9]};
    for(auto v:r)if(!std::isfinite(v))return false;
    // Culling uses the outer frustum. Each focus edge must lie inside it.
    return x>=1&&y>=1&&std::abs(r[1])<=x-1.0f+.0001f&&std::abs(r[3])<=y-1.0f+.0001f;
}
}
