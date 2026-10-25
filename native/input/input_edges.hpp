#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
namespace og::input {
struct Sample {
    std::array<bool, 128> buttons{};
    std::array<int, 4> povs{-1, -1, -1, -1};
};
struct Press {
    bool pov{};
    unsigned index{};
    int direction{-1};
};
inline int pov_direction(std::uint32_t value) {
    return (value & 0xffffu) == 0xffffu || value >= 36000 ? -1 : int(((value + 2250) / 4500) % 8);
}
class Edges {
    bool primed{};
    Sample previous;

  public:
    void reset() {
        primed = false;
        previous = {};
    }
    std::vector<Press> sample(const Sample &current, unsigned buttons, unsigned povs) {
        std::vector<Press> events;
        if (primed) {
            for (unsigned i = 0; i < std::min(buttons, 128u); ++i)
                if (current.buttons[i] && !previous.buttons[i])
                    events.push_back({false, i, -1});
            for (unsigned i = 0; i < std::min(povs, 4u); ++i)
                if (current.povs[i] >= 0 && current.povs[i] < 8 &&
                    current.povs[i] != previous.povs[i])
                    events.push_back({true, i, current.povs[i]});
        }
        previous = current;
        primed = true;
        return events;
    }
};
int self_test();
} // namespace og::input
