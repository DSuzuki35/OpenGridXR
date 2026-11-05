#pragma once
#include "overlay_renderer.hpp"
#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace og {
enum class HelmetVisorPosition { Closed, Half, Open };
struct HelmetShortcutSample { bool control{}, alt{}, v{}, foreground{}; };
// Update even while inactive, so holding the shortcut while switching focus
// cannot cycle the visor. Returns true only for an allowed rising edge.
bool helmet_visor_shortcut(bool &held, bool allowed, HelmetShortcutSample);
struct HelmetConfig {
    bool enabled{};
    std::string leftImage, rightImage, generation;
    float headMotion{}, vehicleMotion{};
    // Percent of image width per eye; positive separates the artwork outward.
    float stereoOffset{};
    bool visorHotkey{};
    HelmetVisorPosition visorPosition{HelmetVisorPosition::Closed};
    std::string halfImage, openImage;
};
bool parse_helmet_config(const std::string &, const std::string &game, HelmetConfig &);
struct HelmetVehicleSample {
    bool valid{};
    float ageSeconds{}, lateralG{}, longitudinalG{};
};
struct HelmetMotionState {
    XrQuaternionf previous{0, 0, 0, 1};
    double time{};
    float yaw{}, pitch{};
    bool initialized{};
};
// Returns radians; invalid tracking, stale telemetry and time discontinuities cannot
// leave a displaced helmet. Combined angular movement is bounded to one degree.
std::array<float, 2> helmet_motion(HelmetMotionState &, XrQuaternionf head, bool valid,
                                 double timeSeconds, float headStrength,
                                 float vehicleStrength, HelmetVehicleSample = {});
bool helmet_quad_geometry(const XrView &, float yaw, float pitch, XrPosef &, XrExtent2Df &);
// A common head-space image envelope prevents asymmetric/canted eye frusta
// from mapping the same flat image pixel to two different viewing directions.
bool helmet_shared_fov(const XrView &left, const XrView &right, XrFovf &);
struct HelmetStatus {
    bool ready{};
    bool imageReduced{}; // Artwork was fitted once to the runtime's padded image limit.
    unsigned uploads{};
    std::string error;
};
class HelmetRenderer {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    HelmetRenderer();
    ~HelmetRenderer();
    bool initialize(XrSession, const void *graphicsBinding, uint32_t maxLayers,
                    const OverlayDispatch &, PFN_xrLocateViews, const std::string &game,
                    const std::filesystem::path &directory = {},
                    uint32_t maxSwapchainWidth = 0, uint32_t maxSwapchainHeight = 0);
    void reset();
    void viewConfiguration(XrViewConfigurationType);
    void append(const XrFrameEndInfo &, bool shouldRender,
                std::vector<const XrCompositionLayerBaseHeader *> &);
    void vehicleSample(HelmetVehicleSample);
    // Explicit input seam for deterministic tests/hosts; no OS key injection.
    void shortcutSample(HelmetShortcutSample);
    HelmetStatus status() const;
};
} // namespace og
