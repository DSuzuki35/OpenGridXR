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
    check(near(i.data.lateralAccelMps2,4.5)&&near(i.data.longitudinalAccelMps2,-6)&&near(i.data.verticalAccelMps2,9.8),"SDK acceleration uses declared SI units");
    auto wrongUnits=fixture::iracing();wrongUnits.put<char>(112+17*144+112,'g');
    check(!ir.read(wrongUnits).data.lateralAccelMps2,"unrecognized acceleration units are not guessed");
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
    check(near(l.data.lateralAccelMps2,4.5)&&near(l.data.longitudinalAccelMps2,-6)&&near(l.data.verticalAccelMps2,9.8),"LMU local acceleration normalizes backward z to forward longitudinal");
    auto invalidAccel=fixture::lmu();invalidAccel.put<double>(130356+208,std::numeric_limits<double>::infinity());
    check(!lm.read(invalidAccel).data.lateralAccelMps2,"nonfinite motion telemetry remains unavailable");
    check(near(l.data.fuelLiters,42)&&near(l.data.lastLapSeconds,91.25)&&l.data.lapsCompleted==4,
        "LMU scoring matched by vehicle ID rather than array index");
    check(l.data.flags.yellow==true&&l.data.flags.blue==true,"LMU player flags");
    check(!l.data.flags.black&&!l.data.flags.white,"unpublished LMU flags remain unknown");
    check(near(l.data.airTempC,22)&&near(l.data.trackTempC,35)&&near(l.data.rainIntensity,.25),"LMU verified scoring weather offsets");
    check(near(l.data.trackWetness,.4)&&near(l.data.timeOfDaySeconds,72000)&&l.data.cloudCover=="mostly-cloudy","LMU wetness fraction, clock and sky category");
    check(l.data.carsNearby==1&&l.data.carsNearbyMethod=="world-distance"&&l.data.carsNearbyRadiusMeters==100,"LMU 3D world distance includes boundary and excludes player");
    auto distant=lb;distant.put<double>(2192+584+280,2080.1);
    check(lm.read(distant).data.carsNearby==0,"LMU excludes outside 100m");
    auto invalidPosition=lb;invalidPosition.put<double>(2192+584+264,std::numeric_limits<double>::quiet_NaN());
    check(!lm.read(invalidPosition).data.carsNearby,"incomplete opponent positions are unknown, not an undercount");
    auto context=fixture::iracing_context();auto ic=ir.read(context);
    check(ic.status==ReadStatus::Valid,"bounded SDK array channels supported");
    check(near(ic.data.airTempC,22)&&near(ic.data.rainIntensity,.25)&&near(ic.data.timeOfDaySeconds,72000),"iRacing context units preserved");
    check(ic.data.cloudCover=="partly-cloudy"&&ic.data.trackWetnessState=="mostly-dry"&&!ic.data.trackWetness,"SDK categorical weather never fabricated as fractions");
    check(ic.data.carsNearby==1&&ic.data.carsNearbyMethod=="track-distance","SDK nearby distance wraps start/finish and excludes not-in-world slots");
    check(ic.data.trackName=="Test circuit"&&ic.data.sessionId=="iracing-42-1","only whitelisted track and non-PII session identifiers exposed");
    check(!ic.data.carsVisible&&!l.data.carsVisible&&!i.data.carsNearby,"visibility and missing channels are unavailable");
    auto missingLength=context;missingLength.put<char>(16384+40,'x');missingLength.put<int>(12,8);
    check(!ir.read(missingLength).data.carsNearby,"unverified track length cannot produce nearby meters");
    auto wrongWeatherUnits=context;wrongWeatherUnits.put<char>(112+20*144+112,'F');
    check(!ir.read(wrongWeatherUnits).data.airTempC,"weather temperature units are verified");
    auto invalidOpponent=context;invalidOpponent.put<float>(9216+132,std::numeric_limits<float>::quiet_NaN());
    check(!ir.read(invalidOpponent).data.carsNearby,"unavailable in-world opponent distance is not undercounted");
    auto oldContext=lb;oldContext.put<int>(64,12000);
    check(!lm.read(oldContext).data.cloudCover&&!lm.read(oldContext).data.timeOfDaySeconds,"new LMU fields remain unknown on older unverified versions");
    auto staleScore=lb;staleScore.put<double>(1700,100);
    check(!lm.read(staleScore).data.carsNearby&&!lm.read(staleScore).data.airTempC,"old scoring context is not attached to fresh physics");
    Tracker contextTracker;auto cr=contextTracker.observe(Game::IRacing,ic,0,1000);
    check(cr.stale&&cr.data.carsNearby==1,"initial context is marked stale until source advances");
    ++ic.token;cr=contextTracker.observe(Game::IRacing,ic,50,1050);
    check(!cr.stale&&cr.data.airTempC==22,"advancing source establishes fresh context");
    cr=contextTracker.observe(Game::IRacing,ic,1100,2100);
    check(cr.stale,"frozen context is never treated as live");
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
    check(!r.data.carsNearby&&!r.data.airTempC,"disconnect clears benchmark context");
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
