#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../benchmark_native.hpp"
#include <cassert>
#include <memory>
#include <iostream>
#include <fstream>
int main(int argc,char** argv){
 using namespace lmuqv::benchmark;
 if(argc>=3&&!strcmp(argv[1],"--dump")){
 auto fixture=std::make_unique<Shared>();initialize(*fixture,4242,10000000);fixture->header.running=1;fixture->header.status=0;fixture->header.outer0Width=1600;fixture->header.focusWidth=1200;
 Sample sample{};sample.qpc=123456789;sample.frame=42;sample.intervalMs=11.25;sample.appMs=8.5;sample.providerMs=1.75;sample.gpuMs=6.125;sample.gpuFrame=40;sample.flags=15;sample.result=0;publish(*fixture,sample);
 publish_focus(*fixture,{3,80,45,40,46,41,42});
 std::ofstream out(argv[2],std::ios::binary);out.write(reinterpret_cast<const char*>(fixture.get()),sizeof(Shared));return out?0:1;
 }
 if(argc>=3&&!strcmp(argv[1],"--serve")){
 Transport transport;assert(transport.open());auto& shared=*transport.data();shared.header.running=1;shared.header.status=0;shared.header.projections=1;
 std::cout<<GetCurrentProcessId()<<std::endl;int seconds=atoi(argv[2]);if(seconds<1||seconds>120)seconds=10;const auto end=ticks()+shared.header.frequency*seconds;
 uint64_t frame=0;while(ticks()<end){Sample sample{};sample.qpc=ticks();sample.frame=++frame;sample.intervalMs=11.25;sample.appMs=8.5;sample.providerMs=1.75;sample.gpuMs=frame%8?missing():6.125;sample.gpuFrame=frame%8?0:frame;sample.flags=frame%8?7:15;publish(shared,sample);Sleep(11);}return 0;
 }

 auto shared=std::make_unique<Shared>();initialize(*shared,123,1000);
 assert(sizeof(Shared)==1900800&&offsetof(Shared,bitmap)==327936);
 assert(shared->header.pid==123&&shared->header.distance==1.2f);
 publish_focus(*shared,{3,80,45,40,46,41,42});
 assert(shared->header.focusSequence==2&&shared->header.focusFlags==3&&shared->header.focusScale==80&&shared->header.rightVertical==41&&shared->header.focusFrame==42);
 Sample sample{};sample.frame=99;sample.intervalMs=7;
 for(int i=1;i<=4100;++i)publish(*shared,sample);
 Sample read{};assert(!read_sample(*shared,1,read));assert(read_sample(*shared,4100,read));assert(read.frame==99&&read.commit==4100);
 shared->ring[3].commit=-4100;assert(!read_sample(*shared,4100,read));
 Config config{};shared->header.configSequence=1;assert(!read_config(*shared,config));shared->header.configSequence=2;shared->header.visible=1;assert(read_config(*shared,config)&&config.visible);
 std::cout<<"benchmark native ABI/ring PASS\n";
}
