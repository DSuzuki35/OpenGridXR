#include "../focus_box.hpp"
#include <cassert>
#include <iostream>
#include <limits>

int main() {
    assert(lmuqv::parse_focus_box(nullptr).percent==100);
    assert(lmuqv::parse_focus_box("").valid);
    for(auto text:{"25","50.5","100","200"}){
        auto box=lmuqv::parse_focus_box(text);assert(box.valid);
        assert(box.percent==std::strtod(text,nullptr));
    }
    for(auto text:{"NaN","inf","24.9","200.1","-50","100junk","0x64","1e2"," 50","+50"}){
        auto box=lmuqv::parse_focus_box(text);assert(!box.valid&&box.percent==100);
    }
    assert(lmuqv::parse_focus_offset(nullptr).percent==0);
    for(auto text:{"-50","-12.5","0","0.25","50","1e1","1E-05","-2.5e-05"}){
        auto offset=lmuqv::parse_focus_offset(text);assert(offset.valid&&offset.percent==std::strtod(text,nullptr));
    }
    for(auto text:{"NaN","inf","-50.1","50.1","12junk","1e9999","-1e9999","51e0","1e-5junk"," 5"}){
        auto offset=lmuqv::parse_focus_offset(text);assert(!offset.valid&&offset.percent==0);
    }
    const XrFovf outer{float(std::atan(-2.)),float(std::atan(1.)),float(std::atan(1.5)),float(std::atan(-.5))};
    const XrFovf target{float(std::atan(-.25)),float(std::atan(.75)),float(std::atan(.5)),float(std::atan(-.5))};
    auto translated=target;assert(lmuqv::offset_focus_fov(translated,outer,25,-12.5));
    assert(std::abs(std::tan(translated.angleLeft)-.5)<1e-6&&std::abs(std::tan(translated.angleRight)-1.5)<1e-6);
    assert(std::abs(std::tan(translated.angleUp)-.25)<1e-6&&std::abs(std::tan(translated.angleDown)+.75)<1e-6);
    assert(translated.angleRight>outer.angleRight);
    translated=target;assert(lmuqv::offset_focus_fov(translated,outer,0,0));assert(std::memcmp(&translated,&target,sizeof(target))==0);
    assert(lmuqv::offset_focus_fov(translated,outer,-50,0));
    assert(std::memcmp(&translated.angleUp,&target.angleUp,sizeof(float))==0&&std::memcmp(&translated.angleDown,&target.angleDown,sizeof(float))==0);
    translated=target;assert(lmuqv::offset_focus_fov(translated,outer,0,50));
    assert(std::memcmp(&translated.angleLeft,&target.angleLeft,sizeof(float))==0&&std::memcmp(&translated.angleRight,&target.angleRight,sizeof(float))==0);
    for(auto invalid:{XrFovf{0,0,.3f,-.3f},XrFovf{.4f,-.4f,.3f,-.3f},XrFovf{-.4f,.4f,-.3f,.3f},
                     XrFovf{NAN,.4f,.3f,-.3f},XrFovf{-.4f,INFINITY,.3f,-.3f},XrFovf{-1.571f,.4f,.3f,-.3f}}){
        translated=target;assert(!lmuqv::offset_focus_fov(translated,invalid,25,0));assert(std::memcmp(&translated,&target,sizeof(target))==0);
        auto before=invalid;assert(!lmuqv::offset_focus_fov(invalid,outer,0,25));assert(std::memcmp(&invalid,&before,sizeof(before))==0);
    }
    for(double invalid:{-50.1,50.1,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        translated=target;assert(!lmuqv::offset_focus_fov(translated,outer,invalid,10));assert(std::memcmp(&translated,&target,sizeof(target))==0);
        assert(!lmuqv::offset_focus_fov(translated,outer,10,invalid));assert(std::memcmp(&translated,&target,sizeof(target))==0);
    }
    const XrFovf tiny{-std::numeric_limits<float>::denorm_min(),std::numeric_limits<float>::denorm_min(),.3f,-.3f};
    translated=tiny;assert(!lmuqv::offset_focus_fov(translated,outer,50,0));assert(std::memcmp(&translated,&tiny,sizeof(tiny))==0);
    XrFovf untouched{NAN,0,0,0};auto beforeNoOp=untouched;
    assert(lmuqv::offset_focus_fov(untouched,outer,0,0));assert(std::memcmp(&untouched,&beforeNoOp,sizeof(untouched))==0);
    const XrFovf original{std::atan(-.25f),std::atan(.75f),std::atan(.5f),std::atan(-.5f)};
    auto size=lmuqv::parse_focus_box_size(nullptr,nullptr,nullptr);
    assert(size.width.percent==100&&size.height.percent==100);
    size=lmuqv::parse_focus_box_size("50.5","150","200");
    assert(size.width.percent==50.5&&size.height.percent==150);
    size=lmuqv::parse_focus_box_size("50",nullptr,"200");
    assert(size.width.percent==50&&size.height.percent==200);
    size=lmuqv::parse_focus_box_size(nullptr,"150","25");
    assert(size.width.percent==25&&size.height.percent==150);
    size=lmuqv::parse_focus_box_size("NaN","150","200");
    assert(!size.width.valid&&size.width.percent==100&&size.height.valid&&size.height.percent==150);
    size=lmuqv::parse_focus_box_size("50","201","200");
    assert(size.width.valid&&size.width.percent==50&&!size.height.valid&&size.height.percent==100);
    auto asymmetric=original;assert(lmuqv::scale_focus_fov(asymmetric,50,150));
    assert(std::abs(std::tan(asymmetric.angleLeft))<1e-6&&std::abs(std::tan(asymmetric.angleRight)-.5f)<1e-6);
    assert(std::abs(std::tan(asymmetric.angleUp)-.75f)<1e-6&&std::abs(std::tan(asymmetric.angleDown)+.75f)<1e-6);
    auto widthOnly=original;assert(lmuqv::scale_focus_fov(widthOnly,50,100));
    assert(widthOnly.angleUp==original.angleUp&&widthOnly.angleDown==original.angleDown);
    auto heightOnly=original;assert(lmuqv::scale_focus_fov(heightOnly,100,150));
    assert(heightOnly.angleLeft==original.angleLeft&&heightOnly.angleRight==original.angleRight);
    auto exact=original;assert(lmuqv::scale_focus_fov(exact,100));
    assert(std::memcmp(&exact,&original,sizeof(exact))==0);
    auto small=original;assert(lmuqv::scale_focus_fov(small,50));
    assert(std::abs(std::tan(small.angleLeft))<1e-6&&std::abs(std::tan(small.angleRight)-.5f)<1e-6);
    assert(std::abs(std::tan(small.angleUp)-.25f)<1e-6&&std::abs(std::tan(small.angleDown)+.25f)<1e-6);
    auto large=original;assert(lmuqv::scale_focus_fov(large,200));
    assert(std::abs(std::tan(large.angleLeft)+.75f)<1e-6&&std::abs(std::tan(large.angleRight)-1.25f)<1e-6);
    assert(std::abs(std::tan(large.angleUp)-1.f)<1e-6&&std::abs(std::tan(large.angleDown)+1.f)<1e-6);
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const float inf=std::numeric_limits<float>::infinity();
    for(auto invalid:{XrFovf{0,0,.3f,-.3f},XrFovf{.4f,-.4f,.3f,-.3f},
                     XrFovf{-.4f,.4f,-.3f,.3f},XrFovf{nan,.4f,.3f,-.3f},
                     XrFovf{-.4f,inf,.3f,-.3f},XrFovf{-1.571f,.4f,.3f,-.3f},
                     XrFovf{-.4f,1.571f,.3f,-.3f}}){
        const auto before=invalid;assert(!lmuqv::scale_focus_fov(invalid,50));
        assert(std::memcmp(&invalid,&before,sizeof(invalid))==0);
    }
    for(double invalid:{0.,24.,201.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        auto fov=original;assert(!lmuqv::scale_focus_fov(fov,invalid));
        assert(std::memcmp(&fov,&original,sizeof(fov))==0);
        assert(!lmuqv::scale_focus_fov(fov,100,invalid));
        assert(std::memcmp(&fov,&original,sizeof(fov))==0);
    }
    std::cout<<"Focus box tests passed: decimal parsing, safe fallback, tangent geometry, exact default, invalid FOV rejection\n";
}
