#pragma once
#include <array>
#include <cmath>
namespace lmuqv {
using Matrix = std::array<float,16>;
struct Fov { float left, right, up, down; };
// LMU may reverse Z after constructing its XR projection. Retain the native depth mapping.
inline Matrix replace_fov(const Matrix& current, const Matrix& fovProjection) {
    Matrix result=current;
    for(unsigned i:{0u,5u,8u,9u})result[i]=fovProjection[i];
    return result;
}
inline Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix out{};
    for(unsigned row=0;row<4;++row) for(unsigned col=0;col<4;++col)
        for(unsigned k=0;k<4;++k) out[row*4+col]+=a[row*4+k]*b[k*4+col];
    return out;
}
// Post-projection FOV warp; valid for the same clipping planes and pose.
inline Matrix remap_fov(const Matrix& composite, const Matrix& old_p, const Matrix& new_p) {
    Matrix warp{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    warp[0]=new_p[0]/old_p[0]; warp[5]=new_p[5]/old_p[5];
    warp[12]=(new_p[8]-warp[0]*old_p[8])/old_p[11];
    warp[13]=(new_p[9]-warp[5]*old_p[9])/old_p[11];
    return multiply(composite,warp);
}
inline bool make_engine_projection(Fov f, float near_z, float far_z,
                                   float scale_x, float scale_y, Matrix& result) {
    if (!(near_z > 0 && far_z > near_z && scale_x > 0 && scale_y > 0) ||
        !std::isfinite(far_z)) return false;
    const float angles[]{f.left*scale_x, f.right*scale_x, f.up*scale_y, f.down*scale_y};
    for (float a : angles) if (!std::isfinite(a) || std::abs(a) >= 1.5707963f) return false;
    float l=std::tan(angles[0]), r=std::tan(angles[1]);
    float u=std::tan(angles[2]), d=std::tan(angles[3]);
    if (!(r-l>1e-6f && u-d>1e-6f)) return false;
    Matrix p{};
    p[0]=2/(r-l); p[5]=2/(u-d);
    p[8]=-(r+l)/(r-l); p[9]=-(u+d)/(u-d);
    p[10]=far_z/(far_z-near_z); p[11]=1;
    p[14]=-near_z*far_z/(far_z-near_z);
    for (float coefficient : p) if (!std::isfinite(coefficient)) return false;
    result=p;
    return true;
}
}
