#include "../diagnostic_sampling.hpp"
#include <cassert>
#include <iostream>

int main(){
    using namespace lmuqv::diagnostic_logging;
    FrameSampling sampling;std::uint64_t now=100,calls=0;
    auto clock=[&]{++calls;return now;};
    for(unsigned frame=0;frame<100;++frame)assert(!sampling.sample(frame,false,clock));
    assert(calls==0); // Diagnostics-off never queries the clock.
    for(unsigned frame=0;frame<10;++frame)for(unsigned draw=0;draw<20;++draw)
        assert(!sampling.sample(frame,true,clock));
    assert(calls==0);
    assert(!sampling.sample(10,true,clock)&&calls==1);
    now=10099;assert(!sampling.sample(11,true,clock)&&calls==2);
    now=10100;assert(sampling.sample(12,true,clock)&&calls==3);
    publish_frame(true);assert(periodic_frame());
    now=50000;for(unsigned draw=0;draw<1000;++draw)assert(sampling.sample(12,true,clock));
    assert(calls==3); // The entire sampled frame uses one cached decision.
    assert(sampling.sample(13,true,clock)&&calls==4); // A long pause emits once, without catch-up bursts.
    assert(!sampling.sample(14,true,clock)&&calls==5);
    publish_frame(false);assert(!periodic_frame());
    assert(!sampling.sample(14,false,clock)&&calls==5);
    assert(!sampling.sample(14,true,clock)&&calls==6); // Re-enable starts a fresh interval.
    now=60000;assert(sampling.sample(15,true,clock)&&calls==7);
    assert(!sampling.sample(0,true,clock)&&calls==7); // Session frame counter reset clears the cadence.
    now=90000;assert(!sampling.sample(10,true,clock)&&calls==8);
    now=99999;assert(!sampling.sample(11,true,clock));
    now=100000;assert(sampling.sample(12,true,clock));
    std::cout<<"Diagnostic sampling: ten-second cadence, cached frame decisions, reset and diagnostics-off clock suppression passed\n";
}
