#pragma once
#include "channel.hpp"
#include "overlay_transport.hpp"
#include <cmath>
#include <openxr/openxr.h>
namespace og {
inline XrQuaternionf spatial_mul(XrQuaternionf a, XrQuaternionf b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline XrVector3f spatial_rotate(XrQuaternionf q, XrVector3f v) {
    const auto r = spatial_mul(spatial_mul(q, {v.x, v.y, v.z, 0}), {-q.x, -q.y, -q.z, q.w});
    return {r.x, r.y, r.z};
}
inline bool capture_overlay_corners(const OverlayPose &panel, const XrPosef &spaceToReference,
                                    const XrPosef &eye, CapturedOverlay &out) {
    out = {};
    if (!(panel.width > 0 && panel.height > 0) || !std::isfinite(panel.width) ||
        !std::isfinite(panel.height))
        return false;
    const XrQuaternionf panelOrientation{panel.orientation[0], panel.orientation[1],
                                         panel.orientation[2], panel.orientation[3]};
    const auto validQuaternion = [](XrQuaternionf q) {
        const auto norm = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
        return std::isfinite(norm) && std::abs(norm - 1.f) < .01f;
    };
    if (!validQuaternion(panelOrientation) || !validQuaternion(spaceToReference.orientation) ||
        !validQuaternion(eye.orientation))
        return false;
    const auto orientation = spatial_mul(spaceToReference.orientation, panelOrientation);
    const auto translated = spatial_rotate(
        spaceToReference.orientation, {panel.position[0], panel.position[1], panel.position[2]});
    const XrVector3f center{translated.x + spaceToReference.position.x - eye.position.x,
                            translated.y + spaceToReference.position.y - eye.position.y,
                            translated.z + spaceToReference.position.z - eye.position.z};
    const XrQuaternionf inverseEye{-eye.orientation.x, -eye.orientation.y, -eye.orientation.z,
                                   eye.orientation.w};
    for (unsigned corner = 0; corner < 4; ++corner) {
        auto offset = spatial_rotate(orientation, {(corner & 1 ? 1 : -1) * panel.width * .5f,
                                                   (corner & 2 ? -1 : 1) * panel.height * .5f, 0});
        auto result = spatial_rotate(
            inverseEye, {center.x + offset.x, center.y + offset.y, center.z + offset.z});
        if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z)) {
            out = {};
            return false;
        }
        out.corners[corner][0] = result.x;
        out.corners[corner][1] = result.y;
        out.corners[corner][2] = result.z;
    }
    out.sourceRevision = panel.sourceRevision;
    out.pixelWidth = panel.pixelWidth;
    out.pixelHeight = panel.pixelHeight;
    out.valid = 1;
    return true;
}
} // namespace og
