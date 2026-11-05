#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace lmuqv {
// Percentages scale tangent-space extents of the configured baseline focus.
// GPU span excludes compositor work: reserve is an estimate, not utilization.
class DynamicFocus {
    bool enabled_=false,evidence_=false;
    double min_=70,max_=100,reserve_=2,scale_=100,filtered_=0,low_=0,high_=0;
    uint64_t last_=0;
public:
    bool configure(bool enabled,double minimum,double maximum,double reserve){
        *this=DynamicFocus{};
#ifdef OPENGRIDXR_LMU_INTERNAL
        // Adaptive focus is deliberately absent from the OpenGrid product.
        // Keep the upstream standalone implementation/tests intact.
        (void)enabled;(void)minimum;(void)maximum;(void)reserve;
        return true;
#else
        if(!std::isfinite(minimum)||!std::isfinite(maximum)||!std::isfinite(reserve)||
           minimum<25||maximum>200||minimum>maximum||reserve<1||reserve>30)return false;
        enabled_=enabled;min_=minimum;max_=maximum;reserve_=reserve;
        if(enabled_)scale_=std::clamp(100.,min_,max_);
        return true;
#endif
    }
    bool enabled()const{return enabled_;}
    double scale()const{return scale_;}
    void resetEvidence(){filtered_=low_=high_=0;evidence_=false;}
    bool sample(double gpuMs,double periodMs,uint64_t frame,uint64_t now){
        if(!enabled_||frame<=last_)return false;
        if(!std::isfinite(gpuMs)||!std::isfinite(periodMs)||gpuMs<=0||
           periodMs<1||periodMs>100||!frame||frame>now||now-frame>32){resetEvidence();return false;}
        const double dt=evidence_?double(frame-last_)*periodMs/1000.:0;
        // Queries are issued every eighth frame. Missing samples, pauses and
        // rejected intervals cannot count as continuous measured pressure.
        if(dt>0.5||frame-last_>8)resetEvidence();
        const double ratio=gpuMs/periodMs;
        filtered_=evidence_?filtered_+(ratio-filtered_)*(1-std::exp(-dt/0.35)):ratio;
        const double elapsed=evidence_?dt:0;last_=frame;evidence_=true;
        const double headroom=100*(1-filtered_);
        low_=headroom<reserve_?low_+elapsed:0;
        high_=headroom>reserve_+5?high_+elapsed:0;
        const auto before=scale_;
        if(low_>=0.25){scale_=std::max(min_,scale_-2);low_=0;}
        else if(high_>=1.0){scale_=std::min(max_,scale_+1);high_=0;}
        return before!=scale_;
    }
};
}
