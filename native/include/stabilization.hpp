#pragma once
#include "core.hpp"
#include <algorithm>
#include <cmath>
namespace og {
class UprightReference {
    bool initialized_{};
    double sign_{1};
    bool pitchAnchored_{};
    double pitchAnchor_{};

  public:
    void reset() {
        initialized_ = false;
        sign_ = 1;
        pitchAnchored_ = false;
        pitchAnchor_ = 0;
    }
    vrst::Quat correction(vrst::Quat source, vrst::Quat target, double strength,
                          bool adaptive = false, double pitchStrength = 0) {
        if (!vrst::valid(source) || !vrst::valid(target))
            return {};
        if (!pitchAnchored_) {
            const auto forward = vrst::rotate(source, {0, 0, -1});
            const double horizontal = std::hypot(forward.x, forward.z);
            if (horizontal > .25) {
                pitchAnchor_ = std::atan2(forward.y, horizontal);
                pitchAnchored_ = true;
            }
        }
        if (!initialized_) {
            double up = vrst::rotate(source, {0, 1, 0}).y;
            if (std::abs(up) >= .25) {
                sign_ = up < 0 ? -1 : 1;
                initialized_ = true;
            }
        }
        constexpr double knee = 15 * vrst::pi / 180;
        if (pitchAnchored_ && std::isfinite(pitchStrength) && pitchStrength > 0) {
            const auto forward = vrst::rotate(target, {0, 0, -1});
            const double horizontal = std::hypot(forward.x, forward.z);
            const double fade = std::clamp((horizontal - .05) / .20, 0., 1.);
            if (fade > 0) {
                const double delta = std::atan2(forward.y, horizontal) - pitchAnchor_;
                const double amount = delta * std::clamp(pitchStrength, 0., 100.) / 100 * fade /
                                      (1 + delta * delta / (knee * knee));
                const vrst::Vec3 right{-forward.z / horizontal, 0, forward.x / horizontal};
                target = vrst::normalized(vrst::multiply(vrst::axis_angle(right, -amount), target));
            }
        }
        if (!initialized_)
            return vrst::horizon_correction(source, target, 0);
        if (adaptive && std::isfinite(strength) && strength > 0) {
            const auto right = vrst::rotate(target, {1, 0, 0});
            const auto up = vrst::rotate(target, {0, 1, 0});
            const double fade = std::clamp((std::hypot(up.y, right.y) - .05) / .20, 0., 1.);
            const double tilt = std::atan2(sign_ * right.y, sign_ * up.y);
            const double remaining = 1 - std::clamp(strength, 0., 100.) / 100;
            const double blend = tilt * tilt / (tilt * tilt + knee * knee);
            const double gentle = remaining * remaining * remaining;
            const double response = remaining * (gentle + (1 - gentle) * blend);
            target = vrst::normalized(vrst::multiply(
                vrst::axis_angle(vrst::rotate(target, {0, 0, -1}), tilt * (1 - response) * fade), target));
            return vrst::normalized(vrst::multiply(vrst::conjugate(source), target));
        }
        if (sign_ < 0) {
            auto flip = vrst::axis_angle({0, 0, 1}, vrst::pi);
            source = vrst::multiply(flip, source);
            target = vrst::multiply(flip, target);
        }
        return vrst::horizon_correction(source, target, strength);
    }
};
// Apply the budget to final output rays, after aspect fit and edge reserves.
// The cropped source rectangle is the reflection boundary in the finite mode.
inline void limit_stream_correction(vrst::Warp &warp, vrst::Quat correction, bool mirror,
                                    double limitPercent) {
    if (!mirror || !std::isfinite(limitPercent) || limitPercent < 0) {
        vrst::limit_warp_correction(warp, correction, mirror);
        return;
    }
    const double limit = std::clamp(limitPercent, 0., 50.) / 100;
    warp.correction = vrst::valid(correction) ? vrst::normalized(correction) : vrst::Quat{};
    warp.correction_fraction = 1;
    const auto fits = [&] {
        for (double u : {0., 1.}) for (double v : {0., 1.}) {
            const auto ray = vrst::rotate(warp.correction,
                {warp.output.left + u * (warp.output.right - warp.output.left),
                 warp.output.up + v * (warp.output.down - warp.output.up), -1});
            if (ray.z > -.05) return false;
            const double x = (ray.x / -ray.z - warp.input.left) / (warp.input.right - warp.input.left);
            const double y = (warp.input.up - ray.y / -ray.z) / (warp.input.up - warp.input.down);
            if (x < -limit || x > 1 + limit || y < -limit || y > 1 + limit) return false;
        }
        return true;
    };
    if (fits()) return;
    const auto desired = warp.correction;
    double lo = 0, hi = 1;
    for (int i = 0; i < 24; ++i) {
        const double amount = (lo + hi) / 2;
        warp.correction = vrst::slerp({}, desired, amount);
        if (fits()) lo = amount;
        else hi = amount;
    }
    warp.correction_fraction = lo;
    warp.correction = vrst::slerp({}, desired, lo);
}
} // namespace og
