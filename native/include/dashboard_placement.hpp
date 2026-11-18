#pragma once
#include "overlay_geometry.hpp"
#include <algorithm>

namespace og {
// Offsets live in neutral panel coordinates. A zero pivot spins the panel at
// its centre. A displaced pivot preserves the wheel hub throughout rotation.
inline XrPosef steered_dashboard(XrPosef neutral, XrVector3f pivot, float tiltDegrees,
                                 float radians) {
    const float tilt = tiltDegrees * .01745329252f, half = radians * .5f;
    XrQuaternionf spin{0, -std::sin(tilt) * std::sin(half), std::cos(tilt) * std::sin(half),
                       std::cos(half)};
    const auto turned = spatial_rotate(spin, pivot);
    const auto shift = spatial_rotate(neutral.orientation,
                                      {pivot.x - turned.x, pivot.y - turned.y, pivot.z - turned.z});
    neutral.position.x += shift.x;
    neutral.position.y += shift.y;
    neutral.position.z += shift.z;
    neutral.orientation = spatial_mul(neutral.orientation, spin);
    return neutral;
}

struct PlacementAdjustment {
    uint64_t token{UINT64_MAX}, nextStep{};
    XrPosef panelInHead{};
    float x{}, y{}, distance{}, startingDistance{}, width{}, yaw{}, pitch{}, roll{};
    unsigned previousKeys{};
    void start(uint64_t id, float z, float w) {
        token = id;
        x = y = yaw = pitch = roll = 0;
        startingDistance = distance = std::clamp(-z, .1f, 10.f);
        width = w;
        nextStep = 0;
        previousKeys = 0;
    }
    enum Key : unsigned {
        Left = 1,
        Right = 2,
        Up = 4,
        Down = 8,
        Control = 16,
        Alt = 32,
        Shift = 64
    };
    void nudge(unsigned keys, uint64_t now) {
        if ((keys & 15) == 0) {
            previousKeys = 0;
            return;
        }
        const bool first = keys != previousKeys;
        if (!first && now < nextStep)
            return;
        previousKeys = keys;
        nextStep = now + (first ? 260 : 60);
        const float step = keys & Shift ? .001f : .025f;
        const float degrees = keys & Shift ? .1f : 1.f;
        const int horizontal = int(bool(keys & Right)) - int(bool(keys & Left));
        const int vertical = int(bool(keys & Up)) - int(bool(keys & Down));
        if ((keys & (Control | Alt)) == (Control | Alt)) {
            pitch = std::remainder(pitch + degrees * vertical, 360.f);
        } else if (keys & Control) {
            distance = std::clamp(distance + step * vertical, .1f, 10.f);
            yaw = std::remainder(yaw + degrees * horizontal, 360.f);
        } else if (keys & Alt) {
            width = std::clamp(width + step * vertical, .1f, 3.f);
            roll = std::remainder(roll + degrees * horizontal, 360.f);
        } else {
            x = std::clamp(x + step * horizontal, -5.f, 5.f);
            y = std::clamp(y + step * vertical, -5.f, 5.f);
        }
    }
    DashboardPlacement orient(DashboardPlacement pose) const {
        if (pose.tick) {
            pose.yaw = std::remainder(pose.yaw + yaw, 360.f);
            pose.pitch = std::remainder(pose.pitch + pitch, 360.f);
            pose.roll = std::remainder(pose.roll + roll, 360.f);
        }
        return pose;
    }
};

// The saved angles use the renderer's yaw (Y), pitch (X), roll (Z) order.
inline DashboardPlacement dashboard_placement(const XrPosef &panel, float width, uint64_t tick) {
    const auto forward = spatial_rotate(panel.orientation, {0, 0, -1});
    const auto right = spatial_rotate(panel.orientation, {1, 0, 0});
    const auto up = spatial_rotate(panel.orientation, {0, 1, 0});
    const float horizontal = std::hypot(forward.x, forward.z);
    constexpr float deg = 57.29577951f;
    // Looking straight up or down leaves only a combined yaw and roll.
    const float yaw = horizontal > .000001f ? std::atan2(-forward.x, -forward.z)
                                             : std::atan2(-right.z, right.x);
    const float roll = horizontal > .000001f ? std::atan2(right.y, up.y) : 0;
    return {std::clamp(panel.position.x, -5.f, 5.f), std::clamp(panel.position.y, -5.f, 5.f),
            std::clamp(panel.position.z, -10.f, -.1f), yaw * deg,
            std::atan2(forward.y, horizontal) * deg, roll * deg, width, tick};
}

inline DashboardPlacement dashboard_ahead(const XrPosef &head, const XrPosef &anchor,
                                          float distance, float dx, float dy, float width,
                                          uint64_t tick) {
    const XrQuaternionf inverse{-anchor.orientation.x, -anchor.orientation.y, -anchor.orientation.z,
                                anchor.orientation.w};
    const auto headOffset = spatial_rotate(inverse, {head.position.x - anchor.position.x,
                                                     head.position.y - anchor.position.y,
                                                     head.position.z - anchor.position.z});
    const auto forward = spatial_rotate(spatial_mul(inverse, head.orientation), {0, 0, -1});
    constexpr float deg = 57.29577951f;
    const float x = headOffset.x + forward.x * distance + dx,
                y = headOffset.y + forward.y * distance + dy,
                z = headOffset.z + forward.z * distance;
    if (!(x >= -5 && x <= 5 && y >= -5 && y <= 5 && z >= -10 && z <= -.1f))
        return {};
    return {x,
            y,
            z,
            std::atan2(-forward.x, -forward.z) * deg,
            std::asin(std::clamp(forward.y, -1.f, 1.f)) * deg,
            0,
            width,
            tick};
}
} // namespace og
