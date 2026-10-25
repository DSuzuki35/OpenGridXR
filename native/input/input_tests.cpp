#include "input_edges.hpp"
#include <iostream>
#include <stdexcept>
namespace og::input {
int self_test() {
    try {
        auto check = [](bool value, const char *why) {
            if (!value)
                throw std::runtime_error(why);
        };
        Edges edges;
        Sample sample;
        sample.buttons[127] = true;
        sample.povs[3] = 7;
        check(edges.sample(sample, 128, 4).empty(),
              "startup held controls must only establish baseline");
        check(edges.sample(sample, 128, 4).empty(), "held controls do not repeat");
        sample.buttons[127] = false;
        sample.povs[3] = -1;
        check(edges.sample(sample, 128, 4).empty(), "release is not a press");
        sample.buttons[127] = true;
        sample.povs[3] = 0;
        auto events = edges.sample(sample, 128, 4);
        check(events.size() == 2 && !events[0].pov && events[0].index == 127 && events[1].pov &&
                  events[1].index == 3 && events[1].direction == 0,
              "high buttons and fourth POV supported");
        sample.povs[3] = 1;
        events = edges.sample(sample, 128, 4);
        check(events.size() == 1 && events[0].direction == 1,
              "POV direct direction change is a new edge");
        edges.reset();
        check(edges.sample(sample, 128, 4).empty(), "reacquisition and detach baseline held state");
        sample.buttons[126] = true;
        sample.povs[2] = 2;
        check(edges.sample(sample, 1, 1).empty(), "reported capabilities bound event indices");
        for (unsigned i = 0; i < 8; ++i)
            check(pov_direction(i * 4500) == int(i), "eight POV directions");
        check(pov_direction(0xffffffff) == -1 && pov_direction(65535) == -1 &&
                  pov_direction(40000) == -1 && pov_direction(35999) == 0,
              "POV neutral variants and angular wrap");
        std::cout << "Input edge self-test passed - no hardware accessed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
} // namespace og::input
