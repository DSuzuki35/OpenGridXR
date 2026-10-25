#include "fixtures.hpp"
#include <cmath>
#include <iostream>
#include <limits>
using namespace og::telemetry;
static void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static bool near(std::optional<double> v,double expected){return v&&std::abs(*v-expected)<.0001;}
struct ChangingMemory:Memory{
    mutable fixture::Bytes bytes;
    mutable unsigned reads{};bool iracing;
    explicit ChangingMemory(bool ir):bytes(ir?fixture::iracing():fixture::lmu()),iracing(ir){}
    std::size_t size()const override{return bytes.size();}
    bool copy(std::size_t offset,void*out,std::size_t count)const override{
        ++reads;bool ok=bytes.copy(offset,out,count);
        if(iracing&&offset==4352&&count==256)bytes.put<unsigned>(64,100+reads);
        if(!iracing&&offset==130356&&count==540)bytes.put<double>(130356+12,123.5+reads);
        return ok;
    }
};
int main(){try{
    IRacingDecoder ir;
    auto ib=fixture::iracing();auto i=ir.read(ib);
    check(i.status==ReadStatus::Valid,"valid iRacing ABI fixture is decoded");
    check(near(i.data.speedMps,50)&&i.data.gear==4&&near(i.data.rpm,6500),"iRacing motion channels");
    check(near(i.data.throttle,.9)&&near(i.data.brake,.2)&&near(i.data.clutch,.25),"raw pedals and clutch depression normalized");
    check(near(i.data.fuelLiters,42)&&i.data.lap==5&&i.data.lapsCompleted==4,"iRacing fuel and lap channels");
    check(i.data.flags.yellow==true&&i.data.flags.blue==true&&i.data.inPits==true,"iRacing flags and pit status");
    auto bad=fixture::iracing();bad.put<int>(28,-1);
    check(ir.read(bad).status==ReadStatus::Invalid,"negative descriptor offsets rejected");
    bad=fixture::iracing();bad.put<int>(112+8,INT32_MAX);
    check(ir.read(bad).status==ReadStatus::Invalid,"variable count multiplication is bounded");
    bad=fixture::iracing();bad.put<int>(68,8000);
    check(ir.read(bad).status==ReadStatus::Invalid,"truncated telemetry row rejected");
    bad=fixture::iracing();bad.put<int>(0,3);
    check(ir.read(bad).status==ReadStatus::Unsupported,"unknown SDK layout version rejected");
    bad=fixture::iracing();bad.put<int>(4,0);
    check(ir.read(bad).status==ReadStatus::Disconnected,"iRacing connection status honored");
    IRacingDecoder finite;
    bad=fixture::iracing();bad.put<float>(4352,std::numeric_limits<float>::quiet_NaN());
    check(!finite.read(bad).data.speedMps,"nonfinite telemetry never becomes a JSON number");
    LmuDecoder lm;
    auto lb=fixture::lmu();auto l=lm.read(lb);
    check(l.status==ReadStatus::Valid,"valid native LMU layout decoded");
    check(near(l.data.speedMps,50)&&l.data.gear==4&&near(l.data.rpm,6500),"LMU player index and velocity magnitude");
    check(near(l.data.fuelLiters,42)&&near(l.data.lastLapSeconds,91.25)&&l.data.lapsCompleted==4,
        "LMU scoring matched by vehicle ID rather than array index");
    check(l.data.flags.yellow==true&&l.data.flags.blue==true,"LMU player flags");
    check(!l.data.flags.black&&!l.data.flags.white,"unpublished LMU flags remain unknown");
    bad=fixture::lmu();bad.put<unsigned char>(128465,104);
    check(lm.read(bad).status==ReadStatus::Invalid,"LMU invalid player index rejected");
    bad=fixture::lmu();bad.put<unsigned char>(128466,0);
    check(lm.read(bad).status==ReadStatus::Disconnected,"LMU menu does not report a live car");
    bad=fixture::lmu();bad.put<int>(64,15000);
    check(lm.read(bad).status==ReadStatus::Unsupported,"unverified future LMU ABI rejected");
    fixture::Bytes lock(8);lock.put<int>(4,1);
    check(lm.read(lb,&lock).status==ReadStatus::Busy,"busy producer is never stalled or modified");
    check(lock.data[4]==1,"read-only decoder leaves synchronization data unchanged");
    ChangingMemory changingIr(true),changingLmu(false);
    check(ir.read(changingIr).status==ReadStatus::Busy,"iRacing row overwritten during copy is not published");
    check(lm.read(changingLmu).status==ReadStatus::Busy,"LMU changing snapshot is not published");
    check(changingIr.reads<100&&changingLmu.reads<30,"unstable producers have bounded retries");
    fixture::Bytes truncated(128468);
    check(lm.read(truncated).status==ReadStatus::Invalid,"short LMU mapping rejected");
    Tracker tracker;
    auto r=tracker.observe(Game::IRacing,i,100,1000);
    check(r.stale&&std::string(r.status)=="stale","initial frozen mapping is not assumed live");
    ++i.token;r=tracker.observe(Game::IRacing,i,150,1050);
    check(!r.stale&&std::string(r.status)=="live"&&r.ageMs==0,"observed source advance establishes live data");
    r=tracker.observe(Game::IRacing,i,1300,2200);
    check(r.stale&&r.ageMs==1150,"unchanged source becomes stale despite successful memory reads");
    r=tracker.observe(Game::IRacing,{},1400,2300);
    check(!r.connected&&!r.data.speedMps,"disconnect clears previous car values");
    const auto json=json_line(r);
    check(json.find("\"schemaVersion\":1")!=std::string::npos&&json.back()=='\n',"bounded JSON-lines wire envelope");
    check(json.find("\"speedMps\":null")!=std::string::npos,"unknown values serialize as null");
    // Exercise attacker-controlled layout metadata using a reproducible sequence.
    // All accesses still pass through the bounded Memory implementation.
    unsigned random=0x12345678;
    for(unsigned n=0;n<512;++n){
        auto corrupted=fixture::iracing();random=random*1664525+1013904223;
        const std::size_t offsets[]{0,24,28,32,36,52,68,112,116,120};
        corrupted.put<unsigned>(offsets[n%10],random);
        auto sample=ir.read(corrupted);
        check(sample.status!=ReadStatus::Valid,"malformed metadata never becomes a live fixture");
    }
    std::cout<<"Telemetry ABI, validation, stale-state and JSON tests passed\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
