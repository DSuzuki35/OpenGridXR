#include "input_edges.hpp"
#include "../include/steering_input.hpp"
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
        const auto releases=edges.sample(sample, 128, 4);
        check(releases.size() == 2 && !releases[0].held && !releases[1].held,
              "held controls emit explicit releases");
        sample.buttons[127] = true;
        sample.povs[3] = 0;
        auto events = edges.sample(sample, 128, 4);
        check(events.size() == 2 && !events[0].pov && events[0].index == 127 && events[1].pov &&
                  events[1].index == 3 && events[1].direction == 0,
              "high buttons and fourth POV supported");
        sample.povs[3] = 1;
        events = edges.sample(sample, 128, 4);
        check(events.size() == 2 && !events[0].held && events[0].direction == 0 && events[1].held && events[1].direction == 1,
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
        using namespace og::steering;
        check(normalize_axis(0,0,65535)==-1 && normalize_axis(65535,0,65535)==1,
              "unsigned DirectInput ranges retain both steering endpoints");
        check(normalize_axis(0,-1000,1000)==0 && normalize_axis(-1000,-1000,1000)==-1 &&
              normalize_axis(1000,-1000,1000)==1,"signed ranges preserve center and endpoints");
        check(normalize_axis(1000,100,900)==1 && normalize_axis(0,100,900)==-1,
              "driver samples outside its declared range are clamped");
        check(!normalize_axis(1,1,1) && !normalize_axis(1,100,-100) &&
              !normalize_axis(0,INT32_MIN,INT32_MAX),"missing/unrestricted ranges are never guessed");
        const float turn=3.14159265358979323846f*2.5f;
        check(std::abs(radians(1,900,false)+turn)<0.00001f &&
              std::abs(radians(-1,900,false)-turn)<0.00001f &&
              std::abs(radians(1,900,true)-turn)<0.00001f && radians(0,900,false)==0,
              "900 degree total lock maps to signed +/-450 degrees with inversion");
        check(radians(1,0,false)==0 && radians(NAN,900,false)==0,
              "invalid configuration or samples stay neutral");
        check(fresh(1000,1250) && !fresh(1000,1251) && !fresh(1000,999),
              "250ms freshness bound rejects old and future samples");
#ifdef _WIN32
        const std::wstring name=L"Local\\OpenGridXR.SteeringInput.Test."+std::to_wstring(GetCurrentProcessId());
        Reader reader(name.c_str());
        {
            Writer writer(name.c_str());check(writer.available(),"test mapping must open");
            check(!reader.read(77,1000),"new mappings must begin invalid");
            writer.publish(-turn,77,true,1000);
            check(reader.read(77,1000).value_or(0)==-turn && !reader.read(78,1000) && !reader.read(0,1000),
                  "shared steering reads require the current profile token");
            check(reader.read(77,1250).has_value() && !reader.read(77,1251),"shared reader expires a stalled helper");
            writer.publish(0,77,false,1001);check(!reader.read(77,1001),"disconnect clears output immediately");
            writer.publish(turn,77,true,1002);check(reader.read(77,1002).value_or(0)==turn,"reconnect resumes steering");
            reader.reset();check(reader.read(77,1002).value_or(0)==turn,"reset releases and reopens the reader");
        }
        check(!reader.read(77),"writer shutdown invalidates a reader-retained mapping");
        {
            Writer restarted(name.c_str());restarted.publish(1.25f,88,true,2000);
            check(reader.read(88,2000).value_or(0)==1.25f && !reader.read(77,2000),
                  "restarted helper reuses retained mapping with fresh sequence and profile token");
        }
#endif
        std::cout << "Input edges, steering ranges/inversion and shared-map lifecycle self-test passed - no hardware accessed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
} // namespace og::input
