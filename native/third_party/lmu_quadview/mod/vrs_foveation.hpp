#pragma once
#include "openxr/openxr.h"
#include <algorithm>
#include <cmath>
#include <vector>
namespace lmuqv {
struct VrsFocus {float left=.25f,top=.25f,right=.75f,bottom=.75f;};
inline VrsFocus vrs_focus(float x,float y,float width,float height,float offsetX,float offsetY){
    if(!std::isfinite(x)||!std::isfinite(y)){x=y=.5f;}
    if(!std::isfinite(width)||width<.1f||width>.9f)width=.5f;
    if(!std::isfinite(height)||height<.1f||height>.9f)height=.5f;
    if(!std::isfinite(offsetX)||std::abs(offsetX)>.5f)offsetX=0;
    if(!std::isfinite(offsetY)||std::abs(offsetY)>.5f)offsetY=0;
    const float hx=width*.5f,hy=height*.5f;
    x=std::clamp(x+offsetX,hx,1-hx);y=std::clamp(y-offsetY,hy,1-hy);
    return {x-hx,y-hy,x+hx,y+hy};
}
inline bool vrs_quaternion(const XrQuaternionf& q){
    const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(n)&&std::abs(n-1)<.01f;
}
inline bool vrs_projection_fov(const float* p,XrFovf& f){
    if(!p||p[11]!=1||p[15]!=0||!std::isfinite(p[0])||!std::isfinite(p[5])||
       !std::isfinite(p[8])||!std::isfinite(p[9])||p[0]<=0||p[5]<=0)return false;
    f={std::atan((-1-p[8])/p[0]),std::atan((1-p[8])/p[0]),
       std::atan((1-p[9])/p[5]),std::atan((-1-p[9])/p[5])};
    return f.angleRight>f.angleLeft&&f.angleUp>f.angleDown;
}
inline XrVector3f vrs_rotate(const XrQuaternionf& q,XrVector3f v){
    const XrVector3f t{2*(q.y*v.z-q.z*v.y),2*(q.z*v.x-q.x*v.z),2*(q.x*v.y-q.y*v.x)};
    return {v.x+q.w*t.x+q.y*t.z-q.z*t.y,v.y+q.w*t.y+q.z*t.x-q.x*t.z,v.z+q.w*t.z+q.x*t.y-q.y*t.x};
}
// Project a gaze direction at infinity into each eye; no fabricated fixation depth.
inline bool vrs_gaze_center(const XrView& eye,const XrPosef& gaze,float& x,float& y){
    if(!vrs_quaternion(eye.pose.orientation)||!vrs_quaternion(gaze.orientation))return false;
    const auto& q=eye.pose.orientation;
    const auto d=vrs_rotate({-q.x,-q.y,-q.z,q.w},vrs_rotate(gaze.orientation,{0,0,-1}));
    if(!std::isfinite(d.z)||d.z>=-.001f)return false;
    const auto& f=eye.fov;
    for(float a:{f.angleLeft,f.angleRight,f.angleUp,f.angleDown})if(!std::isfinite(a)||std::abs(a)>=1.5707f)return false;
    const float l=std::tan(f.angleLeft),r=std::tan(f.angleRight),u=std::tan(f.angleUp),b=std::tan(f.angleDown);
    if(r<=l||u<=b)return false;
    x=(d.x/-d.z-l)/(r-l);y=(u-d.y/-d.z)/(u-b);
    return std::isfinite(x)&&std::isfinite(y)&&x>=0&&x<=1&&y>=0&&y<=1;
}
inline std::vector<unsigned char> vrs_map(unsigned width,unsigned height,const VrsFocus& f){
    if(!width||!height||width>16384||height>16384)return {};
    const unsigned cols=(width+15)/16,rows=(height+15)/16;
    std::vector<unsigned char> map(size_t(cols)*rows,0);
    if(!std::isfinite(f.left)||!std::isfinite(f.top)||!std::isfinite(f.right)||!std::isfinite(f.bottom)||
       f.left<0||f.top<0||f.right>1||f.bottom>1||f.left>=f.right||f.top>=f.bottom)return map;
    for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<cols;++x){
        // Only tiles entirely outside the sharp rectangle may use coarse shading.
        const bool outside=(x+1)*16<=f.left*width||x*16>=f.right*width||
                           (y+1)*16<=f.top*height||y*16>=f.bottom*height;
        map[size_t(y)*cols+x]=outside?1:0;
    }
    return map;
}
}
