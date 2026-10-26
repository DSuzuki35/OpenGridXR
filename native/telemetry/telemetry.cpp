#include "telemetry.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <vector>
namespace og::telemetry {
namespace {
constexpr std::size_t MaxRow=1024*1024,MaxVars=4096;
bool range(std::size_t size,std::size_t offset,std::size_t length){return offset<=size&&length<=size-offset;}
template<class T> T value(const unsigned char *bytes,std::size_t offset){T v{};std::memcpy(&v,bytes+offset,sizeof(T));return v;}
template<class T,std::size_t N> T value(const std::array<unsigned char,N>& bytes,std::size_t offset){return value<T>(bytes.data(),offset);}
Sample fail(ReadStatus status,const char *reason){Sample s;s.status=status;s.reason=reason;return s;}
std::optional<double> number(double n,double lo,double hi){
    if(!std::isfinite(n)||n<lo||n>hi)return {};return n;
}
std::optional<int> integer(double n,int lo,int hi){
    if(!std::isfinite(n)||n<lo||n>hi||std::floor(n)!=n)return {};return int(n);
}
struct IrHeader{
    int version,status,tickRate,update,sessionLength,sessionOffset,numVars,varOffset,numBuf,rowLength,pad[2];
    struct Buffer{std::uint32_t tick;int offset,pad[2];} buffers[4];
};
static_assert(sizeof(IrHeader)==112,"SDK header ABI");
bool ir_shape(const IrHeader& h,std::size_t size){
    if(h.numVars<=0||h.numVars>int(MaxVars)||h.numBuf<1||h.numBuf>4||h.rowLength<=0||
        h.rowLength>int(MaxRow)||h.varOffset<112||!range(size,h.varOffset,std::size_t(h.numVars)*144)||
        h.sessionLength<0||h.sessionOffset<0||!range(size,h.sessionOffset,h.sessionLength))return false;
    for(int i=0;i<h.numBuf;++i){
        if(h.buffers[i].offset<112||!range(size,h.buffers[i].offset,h.rowLength))return false;
        const auto a=std::size_t(h.buffers[i].offset),z=a+h.rowLength;
        if(a<std::size_t(h.varOffset)+h.numVars*144&&std::size_t(h.varOffset)<z)return false;
        for(int j=0;j<i;++j)if(a<std::size_t(h.buffers[j].offset)+h.rowLength&&
            std::size_t(h.buffers[j].offset)<z)return false;
    }return true;
}
bool same_schema(const IrHeader&a,const IrHeader&b){
    return a.version==b.version&&a.update==b.update&&a.numVars==b.numVars&&
        a.varOffset==b.varOffset&&a.rowLength==b.rowLength&&a.sessionLength==b.sessionLength&&a.sessionOffset==b.sessionOffset;
}
std::optional<std::string> safe_text(const unsigned char*p,std::size_t size){
    const auto end=std::find(p,p+size,0);if(end==p||end==p+size)return {};
    if(std::any_of(p,end,[](unsigned char c){return c<32||c>126;}))return {};
    return std::string(p,end);
}
std::string text32(const unsigned char *p){
    auto end=std::find(p,p+32,0);if(end==p+32)return {};return std::string(p,end);
}
const char *const Channels[]{"Speed","Gear","RPM","Throttle","ThrottleRaw","Brake","BrakeRaw",
    "Clutch","ClutchRaw","FuelLevel","Lap","LapCompleted","LapCurrentLapTime","LapLastLapTime",
    "LapBestLapTime","SessionFlags","OnPitRoad","IsOnTrackCar","IsOnTrack","LatAccel","LongAccel","VertAccel",
    "AirTemp","TrackTempCrew","TrackTemp","Precipitation","Skies","TrackWetness","SessionTime","SessionTimeOfDay",
    "PlayerCarIdx","CarIdxLapDistPct","CarIdxTrackSurface","SolarAltitude","SessionUniqueID","SessionNum"};
struct IrVariable{
    std::string name,unit;int type{},offset{},count{};std::size_t descriptor{};
    std::array<unsigned char,144> original{};
};
constexpr std::size_t LmuSize=324820,LmuScoring=1632,LmuVehicles=2192,LmuTelemetry=128464;
constexpr std::size_t VehicleCount=104,ScoringStride=584,TelemetryStride=1888;
struct LmuSnapshot{
    std::array<unsigned char,100> header{};
    std::array<unsigned char,548> scoring{};
    std::array<unsigned char,VehicleCount*ScoringStride> vehicles{};
    std::array<unsigned char,4> metadata{};
    std::array<unsigned char,540> player{};
};
bool lmu_copy(const Memory&m,LmuSnapshot&s){
    if(!m.copy(0,s.header.data(),s.header.size())||!m.copy(LmuScoring,s.scoring.data(),s.scoring.size())||
        !m.copy(LmuTelemetry,s.metadata.data(),s.metadata.size()))return false;
    if(s.metadata[0]>VehicleCount||s.metadata[1]>=VehicleCount||s.metadata[2]>1)return false;
    if(!m.copy(LmuVehicles,s.vehicles.data(),s.vehicles.size()))return false;
    return m.copy(LmuTelemetry+4+std::size_t(s.metadata[1])*TelemetryStride,s.player.data(),s.player.size());
}
bool lmu_equal(const LmuSnapshot&a,const LmuSnapshot&b){
    // FFB and unrelated event callbacks are intentionally not freshness witnesses.
    return value<int>(a.header,64)==value<int>(b.header,64)&&value<unsigned>(a.header,24)==value<unsigned>(b.header,24)&&
        a.header[96]==b.header[96]&&a.scoring==b.scoring&&a.vehicles==b.vehicles&&
        a.metadata==b.metadata&&a.player==b.player;
}
bool lock_busy(const Memory*lock){int busy=0;return lock&&(!lock->copy(4,&busy,sizeof(busy))||busy!=0);}
template<class T> void json_value(std::ostringstream&o,const std::optional<T>&v){if(v)o<<*v;else o<<"null";}
template<> void json_value(std::ostringstream&o,const std::optional<bool>&v){if(v)o<<(*v?"true":"false");else o<<"null";}
template<> void json_value(std::ostringstream&o,const std::optional<std::string>&v){if(v)o<<std::quoted(*v);else o<<"null";}
}
struct IRacingDecoder::Impl {
    IrHeader schema{};bool configured{};
    std::vector<IrVariable> variables;
    std::vector<unsigned char> row;
    std::optional<double> trackLength;
    std::optional<std::string> trackName;
    bool configure(const Memory&m,const IrHeader&h){
        configured=false;variables.clear();trackLength.reset();trackName.reset();
        static constexpr std::size_t widths[]{1,1,4,4,4,8};
        for(int n=0;n<h.numVars;++n){
            IrVariable v;v.descriptor=std::size_t(h.varOffset)+std::size_t(n)*144;
            if(!m.copy(v.descriptor,v.original.data(),144))return false;
            v.type=value<int>(v.original,0);v.offset=value<int>(v.original,4);int count=value<int>(v.original,8);
            if(v.type<0||v.type>5||v.offset<0||count<1||std::size_t(count)>MaxRow/widths[v.type]||
                !range(h.rowLength,v.offset,std::size_t(count)*widths[v.type]))return false;
            v.name=text32(v.original.data()+16);v.unit=text32(v.original.data()+112);
            bool wanted=std::any_of(std::begin(Channels),std::end(Channels),[&](auto name){return v.name==name;});
            if(!wanted)continue;
            const bool array=v.name=="CarIdxLapDistPct"||v.name=="CarIdxTrackSurface";
            if((array?count>128:count!=1)||std::any_of(variables.begin(),variables.end(),[&](auto&x){return x.name==v.name;}))return false;
            v.count=count;
            variables.push_back(std::move(v));
        }
        // Only two WeekendInfo scalars are consumed; no driver/session YAML is exported.
        if(h.sessionLength>0&&h.sessionLength<=int(MaxRow)){
            std::string yaml(h.sessionLength,'\0');
            if(m.copy(h.sessionOffset,yaml.data(),yaml.size())){
                std::istringstream lines(yaml);std::string line;bool weekend=false;
                while(std::getline(lines,line)){
                    if(!line.empty()&&line.back()=='\r')line.pop_back();
                    if(line=="WeekendInfo:"){weekend=true;continue;}
                    if(!line.empty()&&line[0]!=' ')weekend=false;
                    if(!weekend||line.size()>128)continue;
                    if(line.rfind(" TrackName: ",0)==0){const auto name=line.substr(12);trackName=safe_text(reinterpret_cast<const unsigned char*>(name.c_str()),name.size()+1);}
                    if(line.rfind(" TrackLength: ",0)==0){
                        std::istringstream input(line.substr(14));input.imbue(std::locale::classic());double km;std::string unit,extra;
                        if(input>>km>>unit&&unit=="km"&&!(input>>extra))trackLength=number(km*1000,100,200000);
                    }
                }
            }
        }
        row.resize(h.rowLength);schema=h;configured=true;return true;
    }
    const IrVariable *find(const char*name)const{
        for(const auto&v:variables)if(v.name==name)return &v;return nullptr;
    }
    std::optional<double> get(const char*name,int index=0)const{
        auto v=find(name);if(!v||index<0||index>=v->count)return {};
        static constexpr std::size_t widths[]{1,1,4,4,4,8};const auto offset=v->offset+std::size_t(index)*widths[v->type];
        switch(v->type){
        case 1:if(row[offset]>1)return {};return row[offset];
        case 2:return value<std::int32_t>(row.data(),offset);
        case 3:return value<std::uint32_t>(row.data(),offset);
        case 4:return value<float>(row.data(),offset);
        case 5:return value<double>(row.data(),offset);
        default:return {};
        }
    }
    std::optional<double> bounded(const char*name,double lo,double hi)const{
        auto n=get(name);return n?number(*n,lo,hi):std::nullopt;
    }
    std::optional<int> int_value(const char*name,int lo,int hi)const{
        auto n=get(name);return n?integer(*n,lo,hi):std::nullopt;
    }
    std::optional<double> pedal(const char*raw,const char*fallback)const{
        auto n=bounded(raw,0,1);return n?n:bounded(fallback,0,1);
    }
};
IRacingDecoder::IRacingDecoder():impl_(new Impl){}
IRacingDecoder::~IRacingDecoder()=default;
Sample IRacingDecoder::read(const Memory &memory){
    auto&p=*impl_;
    for(int attempt=0;attempt<2;++attempt){
        IrHeader h{};if(!memory.copy(0,&h,sizeof(h)))return fail(ReadStatus::Invalid,"truncated-sdk-header");
        if(h.version!=2)return fail(ReadStatus::Unsupported,"unsupported-sdk-version");
        if(!(h.status&1)){p.configured=false;return fail(ReadStatus::Disconnected,"sim-not-connected");}
        if(!ir_shape(h,memory.size())){p.configured=false;return fail(ReadStatus::Invalid,"invalid-sdk-layout");}
        bool changed=!p.configured||!same_schema(h,p.schema);
        if(!changed)for(const auto&v:p.variables){
            std::array<unsigned char,144> current{};
            if(!memory.copy(v.descriptor,current.data(),current.size())||current!=v.original){changed=true;break;}
        }
        if(changed&&!p.configure(memory,h))return fail(ReadStatus::Invalid,"invalid-sdk-variable");
        int latest=0;
        for(int n=1;n<h.numBuf;++n)if(std::int32_t(h.buffers[n].tick-h.buffers[latest].tick)>0)latest=n;
        if(!memory.copy(h.buffers[latest].offset,p.row.data(),p.row.size()))return fail(ReadStatus::Invalid,"truncated-sdk-row");
        IrHeader after{};
        if(!memory.copy(0,&after,sizeof(after)))return fail(ReadStatus::Invalid,"truncated-sdk-header");
        if(!same_schema(h,after)||after.status!=h.status||after.numBuf!=h.numBuf||
            after.buffers[latest].tick!=h.buffers[latest].tick||after.buffers[latest].offset!=h.buffers[latest].offset)continue;
        auto onTrack=p.get("IsOnTrackCar");if(!onTrack)onTrack=p.get("IsOnTrack");
        if(onTrack&&*onTrack==0)return fail(ReadStatus::Disconnected,"no-player-vehicle");
        Sample result;result.status=ReadStatus::Valid;result.reason=nullptr;
        result.token=(std::uint64_t(std::uint32_t(h.update))<<32)|h.buffers[latest].tick;
        auto&d=result.data;
        const auto acceleration=[&](const char*name)->std::optional<double>{
            auto variable=p.find(name);
            if(!variable||variable->unit!="m/s^2")return {};
            return p.bounded(name,-200,200);
        };
        d.lateralAccelMps2=acceleration("LatAccel");
        d.longitudinalAccelMps2=acceleration("LongAccel");
        d.verticalAccelMps2=acceleration("VertAccel");
        const auto unitValue=[&](const char*name,const char*unit,double lo,double hi)->std::optional<double>{
            const auto v=p.find(name);return v&&v->unit==unit?p.bounded(name,lo,hi):std::nullopt;
        };
        d.airTempC=unitValue("AirTemp","C",-100,100);d.trackTempC=unitValue("TrackTempCrew","C",-100,150);
        if(!d.trackTempC)d.trackTempC=unitValue("TrackTemp","C",-100,150);
        d.rainIntensity=unitValue("Precipitation","%",0,1);
        d.sessionTimeSeconds=unitValue("SessionTime","s",0,1e9);d.timeOfDaySeconds=unitValue("SessionTimeOfDay","s",0,86400);
        d.solarAltitudeRadians=unitValue("SolarAltitude","rad",-1.570797,1.570797);
        if(auto sky=p.int_value("Skies",0,3)){const char*names[]{"clear","partly-cloudy","mostly-cloudy","overcast"};d.cloudCover=names[*sky];}
        if(auto wet=p.find("TrackWetness");wet&&wet->unit=="irsdk_TrackWetness")if(auto level=p.int_value("TrackWetness",1,7)){
            const char*names[]{"dry","mostly-dry","very-lightly-wet","lightly-wet","moderately-wet","very-wet","extremely-wet"};d.trackWetnessState=names[*level-1];
        }
        d.trackName=p.trackName;
        const auto session=p.int_value("SessionUniqueID",1,INT32_MAX),sessionNum=p.int_value("SessionNum",0,10000);
        if(session&&sessionNum)d.sessionId="iracing-"+std::to_string(*session)+"-"+std::to_string(*sessionNum);
        const auto distances=p.find("CarIdxLapDistPct"),surfaces=p.find("CarIdxTrackSurface");
        const auto player=p.int_value("PlayerCarIdx",0,127);
        if(p.trackLength&&player&&distances&&surfaces&&distances->unit=="%"&&surfaces->unit=="irsdk_TrkLoc"&&distances->count==surfaces->count&&*player<distances->count){
            const auto position=p.get("CarIdxLapDistPct",*player),surface=p.get("CarIdxTrackSurface",*player);
            if(position&&number(*position,0,1)&&surface&&integer(*surface,0,3)){
                int nearby=0;bool complete=true;
                for(int n=0;n<distances->count;++n){if(n==*player)continue;
                    const auto on=p.get("CarIdxTrackSurface",n);if(on&&*on==-1)continue;
                    const auto at=p.get("CarIdxLapDistPct",n);
                    if(!on||!integer(*on,0,3)||!at||!number(*at,0,1)){complete=false;break;}
                    const double delta=std::abs(*at-*position);if(std::min(delta,1-delta)* *p.trackLength<=100)++nearby;
                }
                if(complete){d.carsNearby=nearby;d.carsNearbyRadiusMeters=100;d.carsNearbyMethod="track-distance";}
            }
        }
        d.speedMps=p.bounded("Speed",0,200);d.rpm=p.bounded("RPM",0,30000);d.gear=p.int_value("Gear",-1,20);
        d.throttle=p.pedal("ThrottleRaw","Throttle");d.brake=p.pedal("BrakeRaw","Brake");
        d.clutch=p.pedal("ClutchRaw","Clutch");if(d.clutch)d.clutch=1-*d.clutch; // SDK reports clutch engagement.
        if(auto fuel=p.find("FuelLevel");fuel&&(fuel->unit=="l"||fuel->unit=="L"))d.fuelLiters=p.bounded("FuelLevel",0,1000);
        d.lap=p.int_value("Lap",0,1000000);d.lapsCompleted=p.int_value("LapCompleted",0,1000000);
        d.lapTimeSeconds=p.bounded("LapCurrentLapTime",0,86400);
        d.lastLapSeconds=p.bounded("LapLastLapTime",0.001,86400);d.bestLapSeconds=p.bounded("LapBestLapTime",0.001,86400);
        if(auto pit=p.bounded("OnPitRoad",0,1))d.inPits=*pit!=0;
        if(auto flags=p.bounded("SessionFlags",0,4294967295.0)){
            auto f=std::uint32_t(*flags);d.flags.yellow=bool(f&(8|256|16384|32768));d.flags.blue=bool(f&32);
            d.flags.green=bool(f&(4|1024|0x80000000u));d.flags.white=bool(f&2);d.flags.checkered=bool(f&1);
            d.flags.red=bool(f&16);d.flags.black=bool(f&(65536|131072));
        }
        if(!p.find("Speed")&&!p.find("RPM"))return fail(ReadStatus::Invalid,"missing-telemetry-channels");
        return result;
    }return fail(ReadStatus::Busy,"snapshot-changing");
}
struct LmuDecoder::Impl {LmuSnapshot first,second;};
LmuDecoder::LmuDecoder():impl_(new Impl){}
LmuDecoder::~LmuDecoder()=default;
Sample LmuDecoder::read(const Memory &memory,const Memory*lock){
    if(memory.size()<LmuSize)return fail(ReadStatus::Invalid,"truncated-lmu-layout");
    auto&p=*impl_;
    for(int attempt=0;attempt<2;++attempt){
        if(lock_busy(lock))return fail(ReadStatus::Busy,"producer-busy");
        if(!lmu_copy(memory,p.first))return fail(ReadStatus::Invalid,"invalid-lmu-player-index");
        if(lock_busy(lock))continue;
        if(!lmu_copy(memory,p.second))return fail(ReadStatus::Invalid,"invalid-lmu-player-index");
        if(lock_busy(lock)||!lmu_equal(p.first,p.second))continue;
        const auto&s=p.second;const int version=value<int>(s.header,64);
        if(version==0)return fail(ReadStatus::Disconnected,"sim-not-connected");
        if(version<12000||version>=15000)return fail(ReadStatus::Unsupported,"unsupported-lmu-version");
        if(!s.metadata[2]||!s.metadata[0])return fail(ReadStatus::Disconnected,"no-player-vehicle");
        if(s.metadata[1]>=s.metadata[0]||s.header[96]>3)return fail(ReadStatus::Invalid,"invalid-lmu-player-index");
        const int count=value<int>(s.scoring,104);
        if(count<0||count>104||s.scoring[108]>9)return fail(ReadStatus::Invalid,"invalid-lmu-scoring");
        const int id=value<int>(s.player,0);const double elapsed=value<double>(s.player,12);
        if(id<0||!number(elapsed,0,1e9))return fail(ReadStatus::Invalid,"invalid-lmu-clock");
        Sample result;result.status=ReadStatus::Valid;result.reason=nullptr;
        std::uint64_t clockBits{};std::memcpy(&clockBits,&elapsed,sizeof(clockBits));
        result.token=clockBits^(std::uint64_t(value<unsigned>(s.header,24))<<32)^std::uint32_t(id);
        auto&d=result.data;
        const double scoringTime=value<double>(s.scoring,68);
        const bool scoringFresh=std::isfinite(scoringTime)&&scoringTime>=0&&std::abs(scoringTime-elapsed)<=2;
        d.sessionTimeSeconds=number(elapsed,0,1e9);
        if(scoringFresh){
            d.airTempC=number(value<double>(s.scoring,228),-100,100);d.trackTempC=number(value<double>(s.scoring,236),-100,150);
            d.rainIntensity=number(value<double>(s.scoring,220),0,1);d.trackWetness=number(value<double>(s.scoring,332),0,1);
            d.trackName=safe_text(s.scoring.data(),64);
            // These expansion fields are verified against the pinned 1.4.2 ABI only.
            if(version>=14200){
                d.timeOfDaySeconds=number(value<float>(s.scoring,344),0,86400);
                if(s.scoring[350]<=10){const char*names[]{"clear","light-clouds","partly-cloudy","mostly-cloudy","overcast","cloudy-drizzle","cloudy-light-rain","overcast-light-rain","overcast-rain","overcast-heavy-rain","overcast-storm"};d.cloudCover=names[s.scoring[350]];}
            }
        }
        const auto vx=value<double>(s.player,184),vy=value<double>(s.player,192),vz=value<double>(s.player,200);
        // LMU mLocalAccel follows mLocalVel: +x left, +y up, +z backward.
        d.lateralAccelMps2=number(value<double>(s.player,208),-200,200);
        d.verticalAccelMps2=number(value<double>(s.player,216),-200,200);
        d.longitudinalAccelMps2=number(-value<double>(s.player,224),-200,200);
        d.speedMps=number(std::hypot(std::hypot(vx,vy),vz),0,200);
        d.gear=integer(value<int>(s.player,352),-1,20);d.rpm=number(value<double>(s.player,356),0,30000);
        d.throttle=number(value<double>(s.player,388),0,1);d.brake=number(value<double>(s.player,396),0,1);
        d.clutch=number(value<double>(s.player,412),0,1);d.fuelLiters=number(value<double>(s.player,524),0,1000);
        d.lap=integer(value<int>(s.player,20),0,1000000);
        d.lapTimeSeconds=number(elapsed-value<double>(s.player,24),0,86400);
        const unsigned char*playerScore=nullptr;
        for(int n=0;n<count;++n){auto q=s.vehicles.data()+std::size_t(n)*ScoringStride;
            if(value<int>(q,0)==id&&q[196]==1){if(playerScore)return fail(ReadStatus::Invalid,"ambiguous-player-scoring");playerScore=q;}}
        if(playerScore){
            const auto position=[](const unsigned char*q,int axis){return number(value<double>(q,264+axis*8),-1e7,1e7);};
            bool complete=true;int nearby=0;
            for(int n=0;n<count;++n){const auto q=s.vehicles.data()+std::size_t(n)*ScoringStride;
                if(q==playerScore||value<std::int8_t>(q,197)==-1)continue;
                if(value<int>(q,0)<0||q[197]>3){complete=false;break;}
                double distance2=0;
                for(int axis=0;axis<3;++axis){const auto a=position(playerScore,axis),b=position(q,axis);
                    if(!a||!b){complete=false;break;}distance2+=(*a-*b)*(*a-*b);}
                if(!complete)break;if(distance2<=10000)++nearby;
            }
            for(int axis=0;axis<3;++axis)if(!position(playerScore,axis))complete=false;
            if(complete&&scoringFresh){d.carsNearby=nearby;d.carsNearbyRadiusMeters=100;d.carsNearbyMethod="world-distance";}
            d.lapsCompleted=integer(value<short>(playerScore,100),0,32767);
            d.lastLapSeconds=number(value<double>(playerScore,168),.001,86400);
            d.bestLapSeconds=number(value<double>(playerScore,144),.001,86400);
            if(playerScore[198]<=1)d.inPits=playerScore[198]!=0;
            d.flags.blue=playerScore[504]==6;
            d.flags.yellow=s.scoring[108]==6||playerScore[505]==1;
        }else d.flags.yellow=s.scoring[108]==6;
        d.flags.green=s.scoring[108]==5;
        // A session ending, pause, or retirement does not prove a particular
        // marshal flag. Unpublished white/red/black/checkered stay null.
        return result;
    }return fail(ReadStatus::Busy,"snapshot-changing");
}
Record Tracker::observe(Game game,const Sample&s,std::uint64_t now,std::uint64_t utc,bool testData){
    Record r;r.game=game;r.timestampMs=utc;r.sequence=++sequence_;r.testData=testData;
    if(s.status==ReadStatus::Disconnected){reset();r.reason=s.reason;return r;}
    r.connected=true;
    if(s.status==ReadStatus::Invalid||s.status==ReadStatus::Unsupported){
        r.status=s.status==ReadStatus::Invalid?"invalid":"unsupported";r.reason=s.reason;reset();return r;
    }
    if(s.status==ReadStatus::Valid){
        if(!seeded_){seeded_=true;token_=s.token;lastChange_=now;}
        else if(s.token!=token_){advanced_=true;token_=s.token;lastChange_=now;}
        lastData_=s.data;
    }
    if(seeded_){r.ageMs=now>=lastChange_?now-lastChange_:0;r.data=lastData_;}
    r.stale=!advanced_||!r.ageMs||*r.ageMs>=1000;
    r.status=r.stale?"stale":"live";
    r.reason=r.stale?(advanced_?"no-recent-telemetry":"awaiting-source-update"):nullptr;
    if(s.status==ReadStatus::Busy&&r.stale)r.reason=s.reason;
    return r;
}
void Tracker::reset(){seeded_=advanced_=false;token_=lastChange_=0;lastData_={};}
std::string json_line(const Record&r){
    std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(9);
    o<<"{\"schemaVersion\":1,\"game\":\""<<game_name(r.game)<<"\",\"source\":\""
     <<(r.game==Game::IRacing?"iracing-sdk":"lmu-native")<<"\",\"status\":\""<<r.status
     <<"\",\"connected\":"<<(r.connected?"true":"false")<<",\"stale\":"<<(r.stale?"true":"false")
     <<",\"testData\":"<<(r.testData?"true":"false")<<",\"sequence\":"<<r.sequence
     <<",\"timestampMs\":"<<r.timestampMs<<",\"ageMs\":";json_value(o,r.ageMs);
    o<<",\"data\":{";
    bool first=true;
    auto field=[&](const char*name,const auto&v){if(!first)o<<',';first=false;o<<'"'<<name<<"\":";json_value(o,v);};
    field("speedMps",r.data.speedMps);field("gear",r.data.gear);field("rpm",r.data.rpm);
    field("lateralAccelMps2",r.data.lateralAccelMps2);field("longitudinalAccelMps2",r.data.longitudinalAccelMps2);field("verticalAccelMps2",r.data.verticalAccelMps2);
    field("throttle",r.data.throttle);field("brake",r.data.brake);field("clutch",r.data.clutch);
    field("fuelLiters",r.data.fuelLiters);field("lap",r.data.lap);field("lapsCompleted",r.data.lapsCompleted);
    field("lapTimeSeconds",r.data.lapTimeSeconds);field("lastLapSeconds",r.data.lastLapSeconds);
    field("bestLapSeconds",r.data.bestLapSeconds);field("inPits",r.data.inPits);
    field("airTempC",r.data.airTempC);field("trackTempC",r.data.trackTempC);field("rainIntensity",r.data.rainIntensity);
    field("cloudCover",r.data.cloudCover);field("trackWetness",r.data.trackWetness);field("trackWetnessState",r.data.trackWetnessState);
    field("sessionTimeSeconds",r.data.sessionTimeSeconds);field("timeOfDaySeconds",r.data.timeOfDaySeconds);field("solarAltitudeRadians",r.data.solarAltitudeRadians);
    field("carsNearby",r.data.carsNearby);field("carsNearbyRadiusMeters",r.data.carsNearbyRadiusMeters);field("carsNearbyMethod",r.data.carsNearbyMethod);
    field("carsVisible",r.data.carsVisible);field("trackName",r.data.trackName);field("sessionId",r.data.sessionId);
    o<<",\"flags\":{";first=true;
    field("yellow",r.data.flags.yellow);field("blue",r.data.flags.blue);field("green",r.data.flags.green);
    field("white",r.data.flags.white);field("checkered",r.data.flags.checkered);
    field("red",r.data.flags.red);field("black",r.data.flags.black);
    o<<"}},\"reason\":";if(r.reason)o<<'"'<<r.reason<<'"';else o<<"null";o<<"}\n";return o.str();
}
const char *game_name(Game g){return g==Game::IRacing?"iracing":"lmu";}
}
