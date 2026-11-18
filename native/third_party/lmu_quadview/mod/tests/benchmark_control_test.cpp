#include "../benchmark_control.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

using Control=lmuqv::BenchmarkControl;
using State=Control::State;

static void unchanged(Control& c,std::string_view command){
    const auto id=c.phaseId();const auto rate=c.requestedRate();
    const auto remaining=c.warmupRemaining();const auto state=c.state();
    assert(!c.beginCommand(command));
    assert(c.phaseId()==id&&c.requestedRate()==rate&&c.warmupRemaining()==remaining&&c.state()==state);
}
static void lifecycle(){
    Control c;
    assert(c.phaseId()==0&&c.requestedRate()==1&&c.state()==State::idle);
    assert(!c.onFrame());c.complete();assert(c.state()==State::idle);
    assert(c.beginCommand("1 1 0"));assert(c.state()==State::warming);
    c.complete();assert(c.state()==State::warming);
    assert(c.onFrame());assert(c.state()==State::measuring);
    for(int i=0;i<10;++i)assert(!c.onFrame());
    c.complete();c.complete();assert(c.state()==State::complete);
    assert(!c.onFrame());unchanged(c,"1 4 0");unchanged(c,"0 4 0");
    assert(c.beginCommand("2 4 2"));assert(!c.onFrame());
    assert(c.warmupRemaining()==1);
    assert(c.beginCommand("3 2 1"));assert(!c.onFrame());assert(c.onFrame());
    assert(c.beginCommand("4 4 0"));assert(c.onFrame());
    unchanged(c,"3 1 0");
}
static void warmup(){
    for(unsigned n:{0u,1u,2u,1800u}){
        Control c;
        assert(c.beginCommand("1 4 "+std::to_string(n)));
        for(unsigned i=0;i<n;++i){
            assert(c.warmupRemaining()==n-i);
            assert(!c.onFrame());assert(c.state()==State::warming);
        }
        assert(c.warmupRemaining()==0);assert(c.onFrame());assert(!c.onFrame());
    }
}
static void parsing(){
    Control c;
    assert(c.beginCommand(" \t\r\n\v\f0001\t02\n0000\r\n "));
    assert(c.phaseId()==1&&c.requestedRate()==2);
    const std::string_view invalid[]={
        "", " ", "2", "2 4", "2 4 0 extra", "2 4 0 0", "0 4 0", "1 4 0",
        "-2 4 0", "+2 4 0", "2 -4 0", "2 +4 0", "2 4 -1", "2 4 +1",
        "NaN 4 0", "2 NaN 0", "2 4 NaN", "2.0 4 0", "2 4.0 0", "2 4 0.0",
        "0x2 4 0", "2 0x4 0", "2 4 0x0", "2 4 1e2", "2 4 1801",
        "2 0 0", "2 3 0", "2 8 0", "2 4 0x", "2x 4 0", "2 4x 0",
        "18446744073709551616 4 0", "2 18446744073709551616 0", "2 4 18446744073709551616",
        "2,4,0", "2 4\xA0" "0", "2 4 0\xA0", "2 4 99999999999999999999999999999999999"
    };
    for(auto s:invalid)unchanged(c,s);
    unchanged(c,std::string_view("2 4 0\0junk",10));
    assert(c.onFrame());
    for(auto s:invalid)unchanged(c,s);
    c.complete();
    for(auto s:invalid)unchanged(c,s);
    std::string command="2 1 1";
    assert(c.beginCommand(command));command.assign(100,'x');
    assert(c.phaseId()==2&&c.requestedRate()==1&&c.warmupRemaining()==1);
    assert(c.beginCommand("18446744073709551615 4 0"));
    assert(c.phaseId()==std::numeric_limits<uint64_t>::max());
    unchanged(c,"18446744073709551615 1 0");
    unchanged(c,"18446744073709551616 1 0");
    unchanged(c,"1 1 0");
}
int main(){
    lifecycle();warmup();parsing();
    std::cout<<"BenchmarkControl tests passed: parsing, overflow, whitespace, monotonic IDs, warmup boundaries, lifecycle, replacement\n";
}
