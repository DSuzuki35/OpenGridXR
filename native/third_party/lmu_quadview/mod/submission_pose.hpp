#pragma once
#include <openxr/openxr.h>
#include <cmath>
#include <limits>

namespace lmuqv {
namespace submission_pose_detail {
inline bool valid(const XrPosef& pose) {
    const auto& q=pose.orientation;
    const double norm2=double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z+double(q.w)*q.w;
    // Accept small normalization drift (length within 1%), never zero/invalid rotations.
    return std::isfinite(norm2)&&norm2>=0.9801&&norm2<=1.0201&&
           std::isfinite(pose.position.x)&&std::isfinite(pose.position.y)&&std::isfinite(pose.position.z);
}
inline bool equal(const XrPosef& a,const XrPosef& b) {
    return a.orientation.x==b.orientation.x&&a.orientation.y==b.orientation.y&&
           a.orientation.z==b.orientation.z&&a.orientation.w==b.orientation.w&&
           a.position.x==b.position.x&&a.position.y==b.position.y&&a.position.z==b.position.z;
}
struct Quaternion { double x,y,z,w; };
inline Quaternion normalized(const XrQuaternionf& q) {
    const double scale=1/std::sqrt(double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z+double(q.w)*q.w);
    return {q.x*scale,q.y*scale,q.z*scale,q.w*scale};
}
inline Quaternion multiply(const Quaternion& a,const Quaternion& b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
            a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
}

// Incoming stereo poses describe the rendered outer cameras in the submitted
// projection space. Preserve their upper-layer correction even if the space
// handle is unchanged. Transfer each eye's cached outer-to-focus relationship:
// focusSubmitted = outerSubmitted * inverse(outerLocated) * focusLocated.
// No new LocateViews call: poses and FOV must stay tied to the captured images.
inline bool submission_focus_pose(const XrPosef& outerSubmitted,const XrPosef& outerLocated,
                                  const XrPosef& focusLocated,XrPosef& focusSubmitted) {
    using namespace submission_pose_detail;
    if(!valid(outerSubmitted)||!valid(outerLocated)||!valid(focusLocated))return false;
    // Common unmodified path preserves exact runtime values without quaternion math.
    if(equal(outerSubmitted,outerLocated)){focusSubmitted=focusLocated;return true;}
    if(equal(focusLocated,outerLocated)){focusSubmitted=outerSubmitted;return true;}
    const auto outer=normalized(outerLocated.orientation);
    const auto correction=multiply(normalized(outerSubmitted.orientation),{-outer.x,-outer.y,-outer.z,outer.w});
    const auto orientation=multiply(correction,normalized(focusLocated.orientation));
    // Rotate the relative displacement, avoiding cancellation of large translations.
    const double x=double(focusLocated.position.x)-outerLocated.position.x;
    const double y=double(focusLocated.position.y)-outerLocated.position.y;
    const double z=double(focusLocated.position.z)-outerLocated.position.z;
    const double tx=2*(correction.y*z-correction.z*y);
    const double ty=2*(correction.z*x-correction.x*z);
    const double tz=2*(correction.x*y-correction.y*x);
    const double px=outerSubmitted.position.x+x+correction.w*tx+correction.y*tz-correction.z*ty;
    const double py=outerSubmitted.position.y+y+correction.w*ty+correction.z*tx-correction.x*tz;
    const double pz=outerSubmitted.position.z+z+correction.w*tz+correction.x*ty-correction.y*tx;
    constexpr double limit=std::numeric_limits<float>::max();
    if(!std::isfinite(px)||!std::isfinite(py)||!std::isfinite(pz)||
       std::abs(px)>limit||std::abs(py)>limit||std::abs(pz)>limit)return false;
    const XrPosef result={{float(orientation.x),float(orientation.y),float(orientation.z),float(orientation.w)},
                          {float(px),float(py),float(pz)}};
    if(!valid(result))return false;
    focusSubmitted=result;
    return true;
}
}
