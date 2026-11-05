#pragma once
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace lmuqv {
struct XrOutputSizes {double outerPercent=100,focusPercent=100;bool valid=true;};
inline XrOutputSizes parse_output_sizes(const char* outer,const char* focus){
    XrOutputSizes sizes{};
    auto read=[](const char* text,double minimum,double maximum,double& result){
        if(!text||!*text){result=100;return true;}
        auto end=text+std::strlen(text);auto parsed=std::from_chars(text,end,result,std::chars_format::general);
        return parsed.ec==std::errc{}&&parsed.ptr==end&&std::isfinite(result)&&result>=minimum&&result<=maximum;
    };
    if(!read(outer,1,400,sizes.outerPercent)||!read(focus,25,400,sizes.focusPercent)||sizes.outerPercent>sizes.focusPercent){
        sizes={};sizes.valid=false;
    }
    return sizes;
}
inline uint32_t focus_dimension(uint32_t base,double percent){
    if(percent==100)return base; // Preserve the default recommendation exactly, even if not aligned.
    double value=std::ceil(double(base)*std::sqrt(percent/100.0)/4.0)*4.0;
    return std::isfinite(value)&&value>0&&value<=std::numeric_limits<uint32_t>::max()?uint32_t(value):0;
}
inline uint32_t outer_dimension(uint32_t app,XrOutputSizes sizes){
    if(sizes.outerPercent==sizes.focusPercent)return app;
    double value=std::ceil(double(app)*std::sqrt(sizes.outerPercent/sizes.focusPercent));
    return std::isfinite(value)&&value>0&&value<=std::numeric_limits<uint32_t>::max()?uint32_t(value):0;
}
}
