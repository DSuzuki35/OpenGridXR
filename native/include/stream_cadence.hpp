#pragma once
#include <chrono>

namespace og {
inline double streamSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct StreamCadence {
    double next{};
    bool due(double now) const { return now >= next; }
    void advance(double now, unsigned fps) {
        const double period = 1.0 / fps;
        // Keep phase across headset frames. After a stall start a fresh period.
        next = next > 0 && now - next < period ? next + period : now + period;
    }
};
}
