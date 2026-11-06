#pragma once
#include "image_processing.hpp"
#include "color_control.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace og {
struct Point {
    float x{}, y{};
};
struct Edges {
    double left{}, right{}, top{}, bottom{};
};
struct Config {
    struct Lmu {
        std::string mode{"off"}, provider{"native"}, gaze{"auto"};
        double focusWidth{50}, focusHeight{50}, offsetX{}, offsetY{};
        unsigned shadingRate{2};
        bool nativeOuter{true}, debugFocus{};
        bool cpuUploadReuse{}, cpuUploadBatch{}, cpuOuterReuse{}, outerSsr{}, cpuCacheAffinity{};
    } lmu;
    bool enabled{}, streaming{}, mask{}, enhancements{};
    double renderScale{100}, padding{}, smoothing{}, horizon{}, extraLeft{}, extraRight{},
        offsetX{}, offsetY{};
    unsigned eye{}, width{1920}, height{1080}, fps{60};
    bool mirror{};
    bool adaptiveHorizon{};
    double pitchLock{}, mirrorLimit{-1}, mirrorSoftness{}, mirrorStretch{};
    Edges crop{}, reserve{};
    std::array<std::vector<Point>, 2> contours;
    SharpeningSettings sharpening;
    SceneColorSettings visorColor;
    ColorConfig color;
};
struct Projection {
    double left{-1}, right{1}, up{1}, down{-1};
};
struct Mesh {
    std::vector<Point> vertices;
    std::vector<std::uint32_t> indices;
};
Config read_config(const std::wstring &, const std::string &);
std::string game_from_executable(std::wstring);
Projection adjust_projection(Projection, const Config &, bool mutableFov);
std::array<double, 2> dimension_factors(const Config &, bool mutableFov);
Mesh hidden_mesh(const std::vector<Point> &, double padding, Projection);
bool valid_polygon(const std::vector<Point> &);
} // namespace og
