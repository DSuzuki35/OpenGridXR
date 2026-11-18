#include "../view_crop.hpp"
#include <cassert>
#include <cstring>
#include <limits>
#include <iostream>

int main(){
    using namespace lmuqv;
    assert(parse_crop_percent(nullptr).percent==100);
    assert(parse_crop_percent("75.5").valid&&parse_crop_percent("75.5").percent==75.5);
    for(const auto* text:{"nan","inf","24.9","100.1","50tail","75,5"}){
        auto p=parse_crop_percent(text);assert(!p.valid&&p.percent==100);
    }
    assert(crop_dimension(1001,75)==751&&crop_dimension(800,50)==400);
    assert(crop_dimension(13,100)==13&&crop_dimension(1,25)==1);
    XrView views[4]{};
    for(unsigned eye=0;eye<2;++eye){
        views[eye].fov={-1.1f,.9f,.8f,-.7f};
        views[eye].pose.orientation.w=1;views[eye].pose.position.x=float(eye)*.06f;
        views[eye+2]=views[eye];views[eye+2].fov={.6f,.8f,.6f,.4f};
    }
    XrView original[4];std::memcpy(original,views,sizeof(views));
    assert(apply_view_crop(views,100,100));assert(!std::memcmp(original,views,sizeof(views)));
    assert(!apply_view_crop(views,0,50));assert(!std::memcmp(original,views,sizeof(views)));
    assert(apply_view_crop(views,75,50));
    for(unsigned eye=0;eye<2;++eye){
        const auto& a=original[eye].fov;const auto& b=views[eye].fov;const auto& f=views[eye+2].fov;
        assert(std::abs((std::tan(b.angleRight)-std::tan(b.angleLeft))/(std::tan(a.angleRight)-std::tan(a.angleLeft))-.75)<1e-6);
        assert(std::abs((std::tan(b.angleUp)-std::tan(b.angleDown))/(std::tan(a.angleUp)-std::tan(a.angleDown))-.5)<1e-6);
        assert(std::abs((std::tan(b.angleRight)+std::tan(b.angleLeft))-(std::tan(a.angleRight)+std::tan(a.angleLeft)))<1e-6);
        assert(f.angleLeft>=b.angleLeft&&f.angleRight<=b.angleRight&&f.angleDown>=b.angleDown&&f.angleUp<=b.angleUp);
        assert(std::abs((std::tan(f.angleRight)-std::tan(f.angleLeft))-(std::tan(original[eye+2].fov.angleRight)-std::tan(original[eye+2].fov.angleLeft)))<1e-6);
        assert(!std::memcmp(&views[eye].pose,&original[eye].pose,sizeof(XrPosef)));
    }
    std::memcpy(views,original,sizeof(views));views[2].fov=views[0].fov;views[3].fov=views[1].fov;
    // Actual reported height 47%, with asymmetric eyes: padding must preserve
    // each ray's full-image coordinate, rather than enlarge the cropped image.
    views[1].fov={-.85f,1.f,.75f,-.85f};
    const XrFovf full[2]{views[0].fov,views[1].fov};
    assert(apply_view_crop(views,75,47));
    for(unsigned eye=0;eye<2;++eye){
        std::array<float,4> bounds{};assert(crop_bounds(full[eye],views[eye].fov,bounds));
        assert(std::abs(bounds[0]-.125f)<1e-6&&std::abs(bounds[1]-.265f)<1e-6);
        assert(std::abs(bounds[2]-.875f)<1e-6&&std::abs(bounds[3]-.735f)<1e-6);
        const auto& f=full[eye];const auto& c=views[eye].fov;
        for(double ray:{-.1,0.,.1}){
            double cropped=(ray-std::tan(c.angleLeft))/(std::tan(c.angleRight)-std::tan(c.angleLeft));
            double padded=bounds[0]+cropped*(bounds[2]-bounds[0]);
            double expected=(ray-std::tan(f.angleLeft))/(std::tan(f.angleRight)-std::tan(f.angleLeft));
            assert(std::abs(padded-expected)<1e-6);
        }
        std::array<float,4> invalid{};assert(!crop_bounds(c,f,invalid));
    }
    std::memcpy(views,original,sizeof(views));views[2].fov=views[0].fov;views[3].fov=views[1].fov;
    assert(apply_view_crop(views,25,25));
    assert(!std::memcmp(&views[2].fov,&views[0].fov,sizeof(XrFovf)));
    std::memcpy(views,original,sizeof(views));views[3].fov.angleLeft=std::numeric_limits<float>::quiet_NaN();
    XrView invalid[4];std::memcpy(invalid,views,sizeof(views));
    assert(!apply_view_crop(views,50,50));assert(!std::memcmp(invalid,views,sizeof(views)));
    std::cout<<"Crop geometry: independent tangent FOV, containment, no-op, sizing and invalid inputs PASS\n";
}
