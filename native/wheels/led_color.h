#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace ovrlay {
// SimHub channels have only 256 possible values. Build the exact transfer
// function once, instead of evaluating pow for every LED on every frame.
inline float linearLedChannel(uint8_t value) noexcept {
    static const auto table=[] {
        std::array<float,256> result{};
        for(unsigned i=0;i<result.size();++i) {
            float s=i/255.f;
            result[i]=s<=.04045f?s/12.92f:std::pow((s+.055f)/1.055f,2.4f);
        }
        return result;
    }();
    return table[value];
}
}
