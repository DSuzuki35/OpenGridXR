#pragma once
#include "telemetry.hpp"
#include <cstring>
#include <stdexcept>
#include <vector>

// Test-only byte fixtures. Literal offsets are taken from the pinned reference
// ABIs, independently of the decoder implementation; no game data is recorded.
namespace fixture {
struct Bytes : og::telemetry::Memory {
    std::vector<unsigned char> data;
    explicit Bytes(std::size_t size):data(size){}
    std::size_t size() const override{return data.size();}
    bool copy(std::size_t o,void *p,std::size_t n)const override{
        if(o>data.size() || n>data.size()-o)return false;
        std::memcpy(p,data.data()+o,n);return true;
    }
    template<class T> void put(std::size_t o,T value){
        if(o>data.size() || sizeof(T)>data.size()-o)throw std::runtime_error("fixture bounds");
        std::memcpy(data.data()+o,&value,sizeof(value));
    }
};
inline Bytes iracing(){
    Bytes b(8192);
    b.put<int>(0,2);b.put<int>(4,1);b.put<int>(8,60);b.put<int>(12,7);
    b.put<int>(24,20);b.put<int>(28,112);b.put<int>(32,2);b.put<int>(36,256);
    b.put<int>(48,99);b.put<int>(52,4096);b.put<int>(64,100);b.put<int>(68,4352);
    struct Var{const char *name;int type,offset;const char *unit;};
    const Var vars[]{
        {"Speed",4,0,"m/s"},{"Gear",2,4,""},{"RPM",4,8,"rpm"},
        {"Throttle",4,12,"%"},{"Brake",4,16,"%"},{"Clutch",4,20,"%"},
        {"FuelLevel",4,24,"l"},{"Lap",2,28,""},{"LapCompleted",2,32,""},
        {"LapCurrentLapTime",4,36,"s"},{"LapLastLapTime",4,40,"s"},
        {"LapBestLapTime",4,44,"s"},{"SessionFlags",3,48,""},
        {"OnPitRoad",1,52,""},{"IsOnTrackCar",1,53,""},
        {"ThrottleRaw",4,56,"%"},{"ClutchRaw",4,60,"%"},
        {"LatAccel",4,64,"m/s^2"},{"LongAccel",4,68,"m/s^2"},{"VertAccel",4,72,"m/s^2"}};
    for(std::size_t i=0;i<20;++i){auto o=112+i*144;b.put<int>(o,vars[i].type);
        b.put<int>(o+4,vars[i].offset);b.put<int>(o+8,1);
        std::memcpy(b.data.data()+o+16,vars[i].name,std::strlen(vars[i].name));
        std::memcpy(b.data.data()+o+112,vars[i].unit,std::strlen(vars[i].unit));}
    for(std::size_t o:{4096u,4352u}){
        b.put<float>(o,50);b.put<int>(o+4,4);b.put<float>(o+8,6500);
        b.put<float>(o+12,.8f);b.put<float>(o+16,.2f);b.put<float>(o+20,.75f);
        b.put<float>(o+24,42);b.put<int>(o+28,5);b.put<int>(o+32,4);
        b.put<float>(o+36,32.5f);b.put<float>(o+40,91.25f);b.put<float>(o+44,90.75f);
        b.put<unsigned>(o+48,0x28);b.put<unsigned char>(o+52,1);b.put<unsigned char>(o+53,1);
        b.put<float>(o+56,.9f);b.put<float>(o+60,.75f);
        b.put<float>(o+64,4.5f);b.put<float>(o+68,-6.0f);b.put<float>(o+72,9.8f);
    }return b;
}
inline Bytes lmu(){
    Bytes b(324820);
    b.put<int>(64,14200);b.put<unsigned>(24,3);b.put<unsigned char>(96,3);
    b.put<double>(1700,123.5);b.put<int>(1736,2);b.put<unsigned char>(1740,5);
    b.put<unsigned char>(1747,1);
    b.put<unsigned char>(128464,2);b.put<unsigned char>(128465,1);b.put<unsigned char>(128466,1);
    // Player telemetry slot 1; scoring is deliberately in a different order.
    const std::size_t t=130356;
    b.put<int>(t,77);b.put<double>(t+12,123.5);b.put<int>(t+20,5);b.put<double>(t+24,91);
    b.put<double>(t+184,30);b.put<double>(t+192,0);b.put<double>(t+200,40);
    b.put<double>(t+208,4.5);b.put<double>(t+216,9.8);b.put<double>(t+224,6.0);
    b.put<int>(t+352,4);b.put<double>(t+356,6500);
    b.put<double>(t+388,.9);b.put<double>(t+396,.2);b.put<double>(t+412,.25);
    b.put<double>(t+524,42);
    const std::size_t s=2192;
    b.put<int>(s,77);b.put<short>(s+100,4);b.put<unsigned char>(s+196,1);
    b.put<unsigned char>(s+198,1);b.put<double>(s+144,90.75);b.put<double>(s+168,91.25);
    b.put<unsigned char>(s+504,6);b.put<unsigned char>(s+505,1);
    b.put<int>(s+584,88);
    b.put<double>(1632+220,.25);b.put<double>(1632+228,22);b.put<double>(1632+236,35);
    b.put<double>(1632+332,.4);b.put<float>(1632+344,72000);b.put<unsigned char>(1632+350,3);
    std::memcpy(b.data.data()+1632,"Test circuit",12);
    b.put<double>(s+264,1000);b.put<double>(s+272,10);b.put<double>(s+280,2000);
    b.put<unsigned char>(s+584+197,2);b.put<double>(s+584+264,1060);b.put<double>(s+584+272,10);b.put<double>(s+584+280,2080);
    return b;
}
inline Bytes iracing_context(){
    const auto original=iracing();Bytes b(32768);
    std::memcpy(b.data.data(),original.data.data(),112+20*144);
    std::memcpy(b.data.data()+8192,original.data.data()+4096,256);
    std::memcpy(b.data.data()+9216,original.data.data()+4352,256);
    b.put<int>(36,1024);b.put<int>(52,8192);b.put<int>(68,9216);
    const char yaml[]="WeekendInfo:\n TrackName: Test circuit\n TrackLength: 5.000 km\nDriverInfo:\n DriverName: Private\n";
    b.put<int>(16,sizeof(yaml));b.put<int>(20,16384);std::memcpy(b.data.data()+16384,yaml,sizeof(yaml));
    struct Var{const char *name;int type,offset,count;const char *unit;};
    const Var vars[]{
        {"AirTemp",4,80,1,"C"},{"TrackTempCrew",4,84,1,"C"},{"Precipitation",4,88,1,"%"},
        {"Skies",2,92,1,""},{"TrackWetness",2,96,1,"irsdk_TrackWetness"},
        {"SessionTime",5,100,1,"s"},{"SessionTimeOfDay",4,108,1,"s"},
        {"PlayerCarIdx",2,112,1,""},{"CarIdxLapDistPct",4,128,4,"%"},{"CarIdxTrackSurface",2,144,4,"irsdk_TrkLoc"},
        {"SolarAltitude",4,160,1,"rad"},{"SessionUniqueID",2,164,1,""},{"SessionNum",2,168,1,""}};
    b.put<int>(24,33);
    for(std::size_t n=0;n<13;++n){const auto o=112+(20+n)*144;
        b.put<int>(o,vars[n].type);b.put<int>(o+4,vars[n].offset);b.put<int>(o+8,vars[n].count);
        std::memcpy(b.data.data()+o+16,vars[n].name,std::strlen(vars[n].name));
        std::memcpy(b.data.data()+o+112,vars[n].unit,std::strlen(vars[n].unit));}
    for(std::size_t o:{8192u,9216u}){
        b.put<float>(o+80,22);b.put<float>(o+84,35);b.put<float>(o+88,.25f);
        b.put<int>(o+92,1);b.put<int>(o+96,2);b.put<double>(o+100,123.5);b.put<float>(o+108,72000);
        b.put<int>(o+112,0);b.put<float>(o+128,.99f);b.put<float>(o+132,.005f);b.put<float>(o+136,.02f);b.put<float>(o+140,.99f);
        b.put<int>(o+144,3);b.put<int>(o+148,3);b.put<int>(o+152,3);b.put<int>(o+156,-1);
        b.put<float>(o+160,-.1f);b.put<int>(o+164,42);b.put<int>(o+168,1);
    }return b;
}
}
