#include "../dynamic_focus.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
int main(){
    using lmuqv::DynamicFocus;
    DynamicFocus c;
    assert(c.configure(true,70,120,2));
    assert(c.scale()==100);
    // Sustained GPU overload reduces FOV, bounded at configured minimum.
    for(unsigned i=1;i<=1000;++i)c.sample(11,10,i*8,i*8+2);
    assert(c.scale()==70);
    // Sustained headroom restores and expands it, bounded at maximum.
    for(unsigned i=1001;i<=4000;++i)c.sample(5,10,i*8,i*8+2);
    assert(c.scale()==120);
    assert(c.configure(true,70,120,2));
    for(unsigned i=1;i<100;++i)c.sample(9.6,10,i*8,i*8+1);
    assert(c.scale()==100); // dead band: 4% reserve
    c.sample(20,10,800,801);assert(c.scale()==100); // one spike cannot step
    auto before=c.scale();
    c.sample(std::numeric_limits<double>::quiet_NaN(),10,808,809);
    c.sample(0,10,816,817);c.sample(10,0,824,825);
    c.sample(20,10,832,1000); // stale
    assert(c.scale()==before);
    c.configure(true,70,120,2);
    for(uint64_t frame:{8u,16u,24u})c.sample(12,10,frame,frame+1);
    c.sample(std::numeric_limits<double>::quiet_NaN(),10,32,33);
    c.sample(12,10,40,41);
    assert(c.scale()==100); // invalid interval cannot complete sustained overload
    c.configure(true,70,120,2);
    for(uint64_t frame:{8u,16u,24u})c.sample(12,10,frame,frame+1);
    c.sample(12,10,40,41); // missing query at 32 must break continuity too
    assert(c.scale()==100);
    assert(c.configure(true,80,80,2));
    for(unsigned i=1;i<100;++i)c.sample(20,10,i*8,i*8+1);
    assert(c.scale()==80);
    assert(!c.configure(true,120,70,2));assert(c.scale()==100&&!c.enabled());
    assert(!c.configure(true,0,100,2));
    assert(!c.configure(true,70,201,2));
    assert(!c.configure(true,70,100,std::numeric_limits<double>::infinity()));
    assert(c.configure(false,70,120,2));
    for(unsigned i=1;i<100;++i)c.sample(20,10,i*8,i*8+1);
    assert(c.scale()==100);
    std::cout<<"Dynamic focus bounds, hysteresis, invalid samples and opt-out passed\n";
}
