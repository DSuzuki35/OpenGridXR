#pragma once
#include <array>
#include <cstdint>

namespace depthxr {
struct QuadViewsExtent {
    std::uint32_t width{0};
    std::uint32_t height{0};
};
struct QuadViewsFov {
    float left{0.0f}; // radians, XrFovf order
    float right{0.0f};
    float up{0.0f};
    float down{0.0f};
};

struct QuadViewsComposeOverlay {
    bool shown{false};
    bool eye_tracking{false}; // the inset follows the eyes: its outline turns red without a gaze
    bool gaze{false};         // eye tracking had a gaze for the frame: its markers are drawn
    std::array<float, 4> gaze_markers{};      // the raw gaze (xy) and the smoothed gaze (zw)
    std::array<float, 4> reference_markers{}; // the head's centre (xy) and the configured offset (zw)
};

struct QuadViewsComposeEye {
    std::array<float, 4> focus_rect{};            // left, top, right, bottom in output uv
    std::array<float, 2> feather{};               // the blend band in output uv
    float sharpen{0.0f};                          // 0-1
    std::array<float, 2> focus_texel{};           // 1 / the focus view's extent
    std::array<float, 2> output_pixel_in_focus{}; // one output pixel in focus uv
    QuadViewsComposeOverlay overlay;               // drawn over the eye when shown
};
struct QuadViewsComposition {
    QuadViewsExtent output;                // each eye's composed image
    std::array<QuadViewsComposeEye, 2> eyes; // one per output image, in output order
};
QuadViewsComposeEye BuildQuadViewsComposeEye(const QuadViewsFov& full, const QuadViewsFov& focus, QuadViewsExtent output,
                                           QuadViewsExtent focus_extent, double transition_percent,
                                           double sharpness_percent);

// HLSL of the quad views compositor (Direct3D 11): VSMain draws a full-screen
// triangle and PSMain composes one eye from its peripheral (t0) and focus
// (t1) views with the QuadViewsConstants cbuffer (b0), whose focus rectangle,
// blend band and sharpening BuildQuadViewsComposeEye computes.
const char* QuadViewsD3D11ShaderSource();

} // namespace depthxr
