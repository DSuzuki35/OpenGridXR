#pragma once
#include "overlay_renderer.hpp"
#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace og {
struct HelmetConfig {
    bool enabled{};
    std::string leftImage, rightImage, generation;
    float headMotion{}, vehicleMotion{};
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
struct HelmetStatus {
    bool ready{};
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
                    const std::filesystem::path &directory = {});
    void reset();
    void viewConfiguration(XrViewConfigurationType);
    void append(const XrFrameEndInfo &, bool shouldRender,
                std::vector<const XrCompositionLayerBaseHeader *> &);
    void vehicleSample(HelmetVehicleSample);
    HelmetStatus status() const;
};
} // namespace og
