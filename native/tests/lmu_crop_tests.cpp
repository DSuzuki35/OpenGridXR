#include <openxr/openxr.h>
#include "../third_party/lmu_quadview/mod/view_crop.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <cstring>
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    XrView views[4]{};
    for (auto& view : views) view.fov = {float(std::atan(-1.)),float(std::atan(1.)),float(std::atan(1.)),float(std::atan(-1.))};
    for (unsigned eye=2;eye<4;++eye) views[eye].fov={float(std::atan(-.2)),float(std::atan(.2)),float(std::atan(.2)),float(std::atan(-.2))};
    require(lmuqv::apply_view_crop(views,lmuqv::CropEdges{0,0,8,21}),"crop accepted");
    require(std::abs(std::tan(views[0].fov.angleUp)-.84)<1e-6,"8 percent top retains upper tangent .84");
    require(std::abs(std::tan(views[0].fov.angleDown)+.58)<1e-6,"21 percent bottom retains lower tangent -.58");
    std::array<float,4> bounds{};
    const XrFovf full{float(std::atan(-1.)),float(std::atan(1.)),float(std::atan(1.)),float(std::atan(-1.))};
    require(lmuqv::crop_bounds(full,views[0].fov,bounds)&&std::abs(bounds[1]-.08f)<1e-6&&std::abs(bounds[3]-.79f)<1e-6,"embedding preserves exact asymmetric ray bounds");
    require(lmuqv::crop_dimension(1000,71)==710,"outer target height follows remaining FOV fraction");
    XrView before[4];std::memcpy(before,views,sizeof(views));
    for(const auto bad:{lmuqv::CropEdges{-1,0,0,0},lmuqv::CropEdges{50,40,0,0},lmuqv::CropEdges{0,0,std::numeric_limits<double>::quiet_NaN(),0}}){
        require(!lmuqv::apply_view_crop(views,bad)&&!std::memcmp(before,views,sizeof(views)),"invalid edge crop rejects transactionally");}
    for(auto& view:views)view.fov=full;
    views[1].fov={float(std::atan(-2.)),float(std::atan(1.)),float(std::atan(2.)),float(std::atan(-1.))};
    for(unsigned eye=2;eye<4;++eye)views[eye].fov={float(std::atan(-.2)),float(std::atan(.2)),float(std::atan(1.)),float(std::atan(.6))};
    require(lmuqv::apply_view_crop(views,lmuqv::CropEdges{10,20,8,21}),"four independent edge crop accepted");
    require(std::abs(std::tan(views[1].fov.angleLeft)+1.7)<1e-6&&std::abs(std::tan(views[1].fov.angleRight)-.4)<1e-6&&
        std::abs(std::tan(views[1].fov.angleUp)-1.76)<1e-6&&std::abs(std::tan(views[1].fov.angleDown)+.37)<1e-6,"asymmetric right eye uses its own full tangent span");
    require(std::abs(std::tan(views[2].fov.angleUp)-.84)<1e-6&&std::abs(std::tan(views[2].fov.angleDown)-.44)<1e-6,"gaze at crop edge translates fixed focus span without adaptive scaling");
    std::cout << "Asymmetric outer crop matches native camera\n";
}
