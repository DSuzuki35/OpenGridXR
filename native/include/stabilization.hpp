#pragma once
#include "core.hpp"
#include <cmath>
namespace og {
class UprightReference {
    bool initialized_{};
    double sign_{1};

  public:
    void reset() {
        initialized_ = false;
        sign_ = 1;
    }
    vrst::Quat correction(vrst::Quat source, vrst::Quat target, double strength) {
        if (!initialized_) {
            double up = vrst::rotate(source, {0, 1, 0}).y;
            if (std::abs(up) >= .25) {
                sign_ = up < 0 ? -1 : 1;
                initialized_ = true;
            }
        }
        if (!initialized_)
            return vrst::horizon_correction(source, target, 0);
        if (sign_ < 0) {
            auto flip = vrst::axis_angle({0, 0, 1}, vrst::pi);
            source = vrst::multiply(flip, source);
            target = vrst::multiply(flip, target);
        }
        return vrst::horizon_correction(source, target, strength);
    }
};
} // namespace og
