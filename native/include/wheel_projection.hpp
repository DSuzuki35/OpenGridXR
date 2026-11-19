#pragma once
#include <openxr/openxr.h>
#include <cmath>

namespace og {
// The mesh shader projects onto the wheel's local Z=0 aperture. Express that
// same off-axis camera as an OpenXR projection view, keeping its eye position
// but using the wheel orientation as the camera basis.
inline XrCompositionLayerProjectionView wheel_projection_view(
    const XrPosef &cameraPose, XrVector3f eyeLocal, const float *aperture,
    const XrSwapchainSubImage &image) {
    XrCompositionLayerProjectionView view{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
    view.subImage = image;
    view.pose = cameraPose;
    view.fov = {std::atan((aperture[0] - aperture[2] * .5f - eyeLocal.x) / eyeLocal.z),
                std::atan((aperture[0] + aperture[2] * .5f - eyeLocal.x) / eyeLocal.z),
                std::atan((aperture[1] + aperture[3] * .5f - eyeLocal.y) / eyeLocal.z),
                std::atan((aperture[1] - aperture[3] * .5f - eyeLocal.y) / eyeLocal.z)};
    return view;
}
} // namespace og
