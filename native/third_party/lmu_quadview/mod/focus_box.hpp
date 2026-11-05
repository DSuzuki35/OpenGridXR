#pragma once
#include <charconv>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include "openxr/openxr.h"

namespace lmuqv {
struct FocusBox { double percent=100;bool valid=true; };
inline FocusBox parse_focus_box(const char* text) {
    FocusBox box;
    if(!text||!*text)return box;
    const char* end=text+std::strlen(text);
    auto parsed=std::from_chars(text,end,box.percent,std::chars_format::fixed);
    if(parsed.ec!=std::errc{}||parsed.ptr!=end||!std::isfinite(box.percent)||box.percent<25||box.percent>200){
        box.percent=100;box.valid=false;
    }
    return box;
}
struct FocusOffset { double percent=0;bool valid=true; };
inline FocusOffset parse_focus_offset(const char* text) {
    FocusOffset offset;
    if(!text||!*text)return offset;
    const char* end=text+std::strlen(text);
    auto parsed=std::from_chars(text,end,offset.percent,std::chars_format::general);
    if(parsed.ec!=std::errc{}||parsed.ptr!=end||!std::isfinite(offset.percent)||offset.percent<-50||offset.percent>50){
        offset.percent=0;offset.valid=false;
    }
    return offset;
}
inline bool offset_focus_fov(XrFovf& focus,const XrFovf& outer,double horizontal,double vertical) {
    auto validPercent=[](double p){return std::isfinite(p)&&p>=-50&&p<=50;};
    if(!validPercent(horizontal)||!validPercent(vertical))return false;
    if(horizontal==0&&vertical==0)return true; // Preserve default behavior and every input bit.
    auto valid=[](const XrFovf& fov){
        constexpr double halfPi=1.57079632679489661923;
        for(float angle:{fov.angleLeft,fov.angleRight,fov.angleUp,fov.angleDown})
            if(!std::isfinite(angle)||angle<=-halfPi||angle>=halfPi)return false;
        return fov.angleLeft<fov.angleRight&&fov.angleDown<fov.angleUp;
    };
    if(!valid(focus)||!valid(outer))return false;
    const double dx=(std::tan(double(outer.angleRight))-std::tan(double(outer.angleLeft)))*horizontal/100;
    const double dy=(std::tan(double(outer.angleUp))-std::tan(double(outer.angleDown)))*vertical/100;
    XrFovf shifted=focus;
    if(horizontal!=0){shifted.angleLeft=float(std::atan(std::tan(double(focus.angleLeft))+dx));shifted.angleRight=float(std::atan(std::tan(double(focus.angleRight))+dx));}
    if(vertical!=0){shifted.angleUp=float(std::atan(std::tan(double(focus.angleUp))+dy));shifted.angleDown=float(std::atan(std::tan(double(focus.angleDown))+dy));}
    if(!valid(shifted))return false;
    focus=shifted;return true;
}
struct FocusBoxSize { FocusBox width,height; };
inline FocusBoxSize parse_focus_box_size(const char* width,const char* height,const char* legacy) {
    return {parse_focus_box(width&&*width?width:legacy),parse_focus_box(height&&*height?height:legacy)};
}
inline bool scale_focus_fov(XrFovf& fov,double widthPercent,double heightPercent) {
    constexpr double halfPi=1.57079632679489661923;
    auto valid=[&](const XrFovf& value){
        for(float angle:{value.angleLeft,value.angleRight,value.angleUp,value.angleDown})
            if(!std::isfinite(angle)||angle<=-halfPi||angle>=halfPi)return false;
        return value.angleLeft<value.angleRight&&value.angleDown<value.angleUp;
    };
    auto validPercent=[](double percent){return std::isfinite(percent)&&percent>=25&&percent<=200;};
    if(!validPercent(widthPercent)||!validPercent(heightPercent)||!valid(fov))return false;
    if(widthPercent==100&&heightPercent==100)return true; // No tangent round trip: preserve every input bit.
    const double left=std::tan(double(fov.angleLeft)),right=std::tan(double(fov.angleRight));
    const double up=std::tan(double(fov.angleUp)),down=std::tan(double(fov.angleDown));
    const double centerX=(left+right)/2,centerY=(down+up)/2;
    const double halfWidth=(right-left)*widthPercent/200,halfHeight=(up-down)*heightPercent/200;
    const XrFovf scaled{widthPercent==100?fov.angleLeft:float(std::atan(centerX-halfWidth)),
                        widthPercent==100?fov.angleRight:float(std::atan(centerX+halfWidth)),
                        heightPercent==100?fov.angleUp:float(std::atan(centerY+halfHeight)),
                        heightPercent==100?fov.angleDown:float(std::atan(centerY-halfHeight))};
    if(!valid(scaled))return false; // Includes float rounding to a singular or degenerate FOV.
    fov=scaled;
    return true;
}
inline bool scale_focus_fov(XrFovf& fov,double percent) { return scale_focus_fov(fov,percent,percent); }
}
