#include "../cpu_cache_affinity.hpp"
#include <cstdlib>
#include <iostream>

using namespace cpu_cache;
static unsigned failures=0,checks=0;
static void require(bool value,const char* name){++checks;if(!value){std::cerr<<"FAILED: "<<name<<'\n';++failures;}}
static constexpr std::uint64_t MB=1ull<<20;
static std::vector<Cpu> cpus(unsigned count){
    std::vector<Cpu> out;for(unsigned i=0;i<count;++i)out.push_back({256+i,0,std::uint8_t(i),false});return out;
}
static L3 cache(std::uint64_t size,std::uint64_t mask,std::uint16_t group=0){return {size,{{group,mask}}};}
static std::vector<L3> dual(){return {cache(96*MB,0xffff),cache(32*MB,0xffff0000)};}
static bool ids(const Plan& p,std::uint32_t first,unsigned count){
    if(p.ids.size()!=count)return false;
    for(unsigned i=0;i<count;++i)if(p.ids[i]!=first+i)return false;
    return true;
}
template<class T> static void append(std::vector<unsigned char>& bytes,const T& value){
    const auto* p=reinterpret_cast<const unsigned char*>(&value);bytes.insert(bytes.end(),p,p+sizeof(value));
}
// Only these injected APIs can reach the apply path. No real affinity setter is
// called anywhere in this test, including on an actual dual-CCD X3D workstation.
struct Fake {
    inline static std::vector<unsigned char> cacheBytes,cpuBytes;
    inline static std::vector<ULONG> selected;
    inline static unsigned sets,affinityCalls,defaultCalls;
    inline static DWORD_PTR processMask,systemMask;
    inline static BOOL affinityOk,defaultsOk,setOk;
    inline static WORD groupCount;
    inline static ULONG existing;
    inline static bool cacheProbeFail,cpuProbeFail,cacheReadFail,cpuReadFail,cacheOverrun,cpuOverrun,changedPolicy;
    static BOOL WINAPI affinity(HANDLE process,PDWORD_PTR own,PDWORD_PTR system){
        require(process==GetCurrentProcess(),"query current process affinity");++affinityCalls;
        *own=processMask;*system=systemMask;
        if(changedPolicy&&affinityCalls>1)*own=0xffff;
        return affinityOk;
    }
    static WORD WINAPI groups(){return groupCount;}
    static BOOL WINAPI get(HANDLE process,PULONG out,ULONG count,PULONG needed){
        require(process==GetCurrentProcess()&&!out&&!count,"query process defaults without mutation");++defaultCalls;
        *needed=existing;SetLastError(existing?ERROR_INSUFFICIENT_BUFFER:ERROR_ACCESS_DENIED);
        return defaultsOk&&!existing;
    }
    static BOOL WINAPI set(HANDLE process,const ULONG* values,ULONG count){
        require(process==GetCurrentProcess(),"set only current process defaults");++sets;
        selected.assign(values,values+count);SetLastError(ERROR_ACCESS_DENIED);return setOk;
    }
    static BOOL WINAPI caches(LOGICAL_PROCESSOR_RELATIONSHIP relation,PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX out,PDWORD size){
        require(relation==RelationCache,"query cache relationships only");
        DWORD capacity=*size;*size=DWORD(cacheBytes.size());
        if(!out){SetLastError(cacheProbeFail?ERROR_ACCESS_DENIED:ERROR_INSUFFICIENT_BUFFER);return FALSE;}
        if(cacheReadFail)return FALSE;
        if(capacity<cacheBytes.size())return FALSE;
        std::memcpy(out,cacheBytes.data(),cacheBytes.size());if(cacheOverrun)++*size;return TRUE;
    }
    static BOOL WINAPI cpuset(PSYSTEM_CPU_SET_INFORMATION out,ULONG capacity,PULONG size,HANDLE process,ULONG flags){
        require(process==GetCurrentProcess()&&!flags,"query CPU sets for current process");*size=ULONG(cpuBytes.size());
        if(!out){SetLastError(cpuProbeFail?ERROR_ACCESS_DENIED:ERROR_INSUFFICIENT_BUFFER);return FALSE;}
        if(cpuReadFail)return FALSE;
        if(capacity<cpuBytes.size())return FALSE;
        std::memcpy(out,cpuBytes.data(),cpuBytes.size());if(cpuOverrun)++*size;return TRUE;
    }
    static Api reset(){
        sets=affinityCalls=defaultCalls=0;selected.clear();cacheBytes.clear();cpuBytes.clear();
        processMask=systemMask=0xffffffff;affinityOk=defaultsOk=setOk=TRUE;existing=0;groupCount=1;
        cacheProbeFail=cpuProbeFail=cacheReadFail=cpuReadFail=cacheOverrun=cpuOverrun=changedPolicy=false;
        for(const auto& c:dual()){
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX e{};e.Relationship=RelationCache;
            e.Size=DWORD(offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX,Cache)+sizeof(CACHE_RELATIONSHIP));
            e.Cache.Level=3;e.Cache.Type=CacheUnified;e.Cache.CacheSize=DWORD(c.size);e.Cache.GroupMask.Mask=KAFFINITY(c.masks[0].second);
            // Wire offset works with both legacy Reserved[20] and new GroupCount headers.
            WORD one=1;std::memcpy(reinterpret_cast<unsigned char*>(&e.Cache)+offsetof(CACHE_RELATIONSHIP,GroupMask)-sizeof(WORD),&one,sizeof(one));
            const auto* raw=reinterpret_cast<const unsigned char*>(&e);cacheBytes.insert(cacheBytes.end(),raw,raw+e.Size);
        }
        for(const auto& c:cpus(32)){
            SYSTEM_CPU_SET_INFORMATION e{};e.Size=sizeof(e);e.Type=CpuSetInformation;
            e.CpuSet.Id=c.id;e.CpuSet.Group=c.group;e.CpuSet.LogicalProcessorIndex=c.logical;append(cpuBytes,e);
        }
        return {&cpuset,&set,&get,&caches,&affinity,&groups,true};
    }
};
static void unchanged(const Api& api,const char* name){const auto result=applyPreferVCache(api);require(Fake::sets==0&&result.find("unchanged")!=std::string::npos,name);}
int main(){
    auto p=plan(dual(),cpus(32));require(p.status==Status::Apply&&ids(p,256,16),"dual CCD picks larger cache");
    p=plan({cache(32*MB,0xffff),cache(96*MB,0xffff0000)},cpus(32));require(p.status==Status::Apply&&ids(p,272,16),"larger CCD may be second");
    p=plan({cache(96*MB,0xfff),cache(32*MB,0xfff000)},cpus(24));require(p.status==Status::Apply&&ids(p,256,12),"7900X3D 6+6 cores");
    p=plan({cache(96*MB,0xffff)},cpus(16));require(p.status==Status::NotApplicable&&p.ids.empty(),"single CCD unchanged");
    for(auto size:{32ull,40ull,96ull}){
        p=plan({cache(size*MB,0xffff),cache((size==40?32:size)*MB,0xffff0000)},cpus(32));
        require(p.status==Status::NotApplicable&&p.ids.empty(),"symmetric or small L3 difference unchanged");
    }
    auto caches=dual();caches.push_back(cache(16*MB,0xff00000000));
    p=plan(caches,cpus(40));require(p.status==Status::Refused&&p.ids.empty(),"three L3 domains refused");
    caches=dual();caches.push_back(cache(0,0));
    p=plan(caches,cpus(32));require(p.status==Status::Refused&&p.ids.empty(),"invalid L3 cannot be silently discarded");
    caches=dual();caches.push_back(caches[0]);
    p=plan(caches,cpus(32));require(p.status==Status::Apply&&ids(p,256,16),"identical duplicate domain is harmless");
    caches=dual();caches[0].masks.push_back({std::uint16_t(0),std::uint64_t(1)});
    p=plan(caches,cpus(32));require(p.status==Status::Refused&&p.ids.empty(),"duplicate group masks refused");
    p=plan({cache(96*MB,0xffff),cache(32*MB,0xff00ff)},cpus(32));require(p.status==Status::Refused&&p.ids.empty(),"overlapping L3 refused");
    p=plan(dual(),cpus(16));require(p.status==Status::Refused&&p.ids.empty(),"missing sibling CPU records refused");
    p=plan(dual(),cpus(33));require(p.status==Status::Refused&&p.ids.empty(),"CPU outside cache masks refused");
    auto list=cpus(32);list[1].id=list[0].id;
    p=plan(dual(),list);require(p.status==Status::Refused&&p.ids.empty(),"duplicate CPU-set IDs refused");
    list=cpus(32);list[1].logical=0;
    p=plan(dual(),list);require(p.status==Status::Refused&&p.ids.empty(),"duplicate CPU locations refused");
    list=cpus(32);list[1].logical=64;
    p=plan(dual(),list);require(p.status==Status::Refused&&p.ids.empty(),"invalid logical index refused");
    list=cpus(32);list[3].unavailable=list[20].unavailable=true;
    p=plan(dual(),list);require(p.status==Status::Apply&&p.ids.size()==15&&p.totalLogical==30,"unavailable CPUs excluded");
    p=plan({cache(96*MB,3),cache(32*MB,0xfffffffc)},cpus(32));require(p.status==Status::Refused&&p.ids.empty(),"too few preferred CPUs refused");

    auto api=Fake::reset();auto line=applyPreferVCache(api);
    require(Fake::sets==1&&Fake::selected.size()==16&&Fake::selected.front()==256&&Fake::selected.back()==271,"successful apply uses larger CCD CPU-set IDs");
    require(line.find("unchanged")==std::string::npos,"successful apply log");
    api=Fake::reset();api.amd=false;unchanged(api,"non-AMD asymmetric topology unchanged");
    api=Fake::reset();api.query=nullptr;unchanged(api,"missing CPU-set API unchanged");
    api=Fake::reset();api.caches=nullptr;unchanged(api,"missing cache API unchanged");
    api=Fake::reset();api.affinity=nullptr;unchanged(api,"missing affinity API unchanged");
    api=Fake::reset();api.groups=nullptr;unchanged(api,"missing group API unchanged");
    api=Fake::reset();api.get=nullptr;unchanged(api,"missing defaults query API unchanged");
    api=Fake::reset();api.set=nullptr;unchanged(api,"missing defaults setter unchanged");
    api=Fake::reset();Fake::affinityOk=FALSE;unchanged(api,"failed affinity query unchanged");
    api=Fake::reset();Fake::processMask=Fake::systemMask=0;unchanged(api,"ambiguous zero affinity masks unchanged");
    api=Fake::reset();Fake::processMask=0xffff;unchanged(api,"existing hard affinity preserved");
    api=Fake::reset();Fake::defaultsOk=FALSE;unchanged(api,"failed default CPU-set query unchanged");
    api=Fake::reset();Fake::existing=2;unchanged(api,"existing process default CPU sets preserved");
    api=Fake::reset();Fake::groupCount=2;unchanged(api,"multiple processor groups unchanged");
    api=Fake::reset();Fake::groupCount=0;unchanged(api,"unknown processor group count unchanged");
    api=Fake::reset();Fake::changedPolicy=true;unchanged(api,"policy changed during topology query preserved");
    api=Fake::reset();Fake::setOk=FALSE;line=applyPreferVCache(api);
    require(Fake::sets==1&&line.find("failed")!=std::string::npos,"setter failure reported");
    for(unsigned kind=0;kind<8;++kind){
        api=Fake::reset();
        switch(kind){case 0:Fake::cacheProbeFail=true;break;case 1:Fake::cpuProbeFail=true;break;
        case 2:Fake::cacheReadFail=true;break;case 3:Fake::cpuReadFail=true;break;
        case 4:Fake::cacheOverrun=true;break;case 5:Fake::cpuOverrun=true;break;
        case 6:Fake::cacheBytes.push_back(0);break;case 7:Fake::cpuBytes.push_back(0);break;}
        unchanged(api,"query failure or malformed tail must never set defaults");
    }
    std::vector<Cpu> parsed;std::vector<L3> parsedCaches;
    api=Fake::reset();
    for(unsigned i=0;i<4;++i){BYTE flag=i==0?1:i==1?2:i==2?6:8;
        Fake::cpuBytes[i*sizeof(SYSTEM_CPU_SET_INFORMATION)+offsetof(SYSTEM_CPU_SET_INFORMATION,CpuSet)+offsetof(decltype(SYSTEM_CPU_SET_INFORMATION{}.CpuSet),AllFlags)]=flag;}
    require(queryCpus(api.query,parsed)&&parsed.size()==32,"valid CPU buffer parsed");
    require(!parsed[0].unavailable&&parsed[1].unavailable&&!parsed[2].unavailable&&parsed[3].unavailable,"parked, allocated, target and realtime flags");
    api=Fake::reset();Fake::cpuBytes.push_back(0);
    require(!queryCpus(api.query,parsed)&&parsed.empty(),"invalid CPU parse publishes no partial result");
    api=Fake::reset();Fake::cacheBytes.push_back(0);
    require(!queryCaches(parsedCaches,api.caches)&&parsedCaches.empty(),"invalid cache parse publishes no partial result");
    api=Fake::reset();WORD two=2;
    const auto groupOffset=offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX,Cache)+offsetof(CACHE_RELATIONSHIP,GroupMask)-sizeof(WORD);
    std::memcpy(Fake::cacheBytes.data()+groupOffset,&two,sizeof(two));
    require(!queryCaches(parsedCaches,api.caches),"truncated GroupMasks array refused");
    api=Fake::reset();WORD zero=0;
    std::memcpy(Fake::cacheBytes.data()+groupOffset,&zero,sizeof(zero));
    require(queryCaches(parsedCaches,api.caches)&&parsedCaches.size()==2,"legacy zero GroupCount supports one GroupMask");
    // Genuine variable-size cache record with two group masks.
    api=Fake::reset();DWORD firstSize=0;
    std::memcpy(&firstSize,Fake::cacheBytes.data()+sizeof(LOGICAL_PROCESSOR_RELATIONSHIP),sizeof(firstSize));
    Fake::cacheBytes.resize(firstSize);
    GROUP_AFFINITY secondMask{};secondMask.Group=1;secondMask.Mask=0xff;
    append(Fake::cacheBytes,secondMask);firstSize+=sizeof(secondMask);
    std::memcpy(Fake::cacheBytes.data()+sizeof(LOGICAL_PROCESSOR_RELATIONSHIP),&firstSize,sizeof(firstSize));
    std::memcpy(Fake::cacheBytes.data()+groupOffset,&two,sizeof(two));
    require(queryCaches(parsedCaches,api.caches)&&parsedCaches.size()==1&&parsedCaches[0].masks.size()==2&&
        parsedCaches[0].masks[1].first==1&&parsedCaches[0].masks[1].second==0xff,"variable GroupMasks record parsed completely");
    for(unsigned kind=0;kind<8;++kind){
        api=Fake::reset();
        if(kind<4){
            const DWORD badSize=kind==0?0:kind==1?8:kind==2?0xffffffff:DWORD(Fake::cacheBytes.size()+1);
            std::memcpy(Fake::cacheBytes.data()+sizeof(LOGICAL_PROCESSOR_RELATIONSHIP),&badSize,sizeof(badSize));
            require(!queryCaches(parsedCaches,api.caches)&&parsedCaches.empty(),"malformed cache size publishes no result");
        }else{
            const ULONG badSize=kind==4?0:kind==5?8:kind==6?0xffffffff:ULONG(Fake::cpuBytes.size()+1);
            std::memcpy(Fake::cpuBytes.data(),&badSize,sizeof(badSize));
            require(!queryCpus(api.query,parsed)&&parsed.empty(),"malformed CPU size publishes no result");
        }
    }
    api=Fake::reset();const CPU_SET_INFORMATION_TYPE unknown=static_cast<CPU_SET_INFORMATION_TYPE>(99);
    std::memcpy(Fake::cpuBytes.data()+offsetof(SYSTEM_CPU_SET_INFORMATION,Type),&unknown,sizeof(unknown));
    require(!queryCpus(api.query,parsed)&&parsed.empty(),"unknown CPU information type refused");
    api=Fake::reset();const LOGICAL_PROCESSOR_RELATIONSHIP wrongRelation=RelationProcessorCore;
    std::memcpy(Fake::cacheBytes.data(),&wrongRelation,sizeof(wrongRelation));
    require(!queryCaches(parsedCaches,api.caches)&&parsedCaches.empty(),"unexpected topology relationship refused");
    // Live access is read-only and limited to topology; applyPreferVCache() is
    // deliberately never invoked without the fake API argument.
    const bool liveOk=queryCaches(parsedCaches);
    if(liveOk)for(const auto& c:parsedCaches)require(c.size&&!c.masks.empty(),"read-only live cache data well-formed");
    std::cout<<checks<<" CPU cache checks; failures="<<failures<<"; read-only live L3 query="<<(liveOk?"ok":"unavailable")<<'\n';
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
