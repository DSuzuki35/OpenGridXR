#pragma once
// Prefer the larger L3 on AMD CPUs with exactly two asymmetric cache domains.
// Process-default CPU sets are a soft scheduling policy: thread-selected CPU
// sets and restrictive thread affinity retain Windows' documented precedence.
// This does not inspect every policy a scheduling utility may apply later.
#include <windows.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#elif defined(__i386__) || defined(__x86_64__)
#include <cpuid.h>
#endif

namespace cpu_cache {
struct L3 {
    std::uint64_t size=0;
    std::vector<std::pair<std::uint16_t,std::uint64_t>> masks;
};
struct Cpu {
    std::uint32_t id=0;std::uint16_t group=0;std::uint8_t logical=0;
    bool unavailable=false;
};
enum class Status { Apply, NotApplicable, Refused };
struct Plan {
    Status status=Status::NotApplicable;
    const char* reason="";
    std::vector<std::uint32_t> ids;
    std::uint64_t largeSize=0,otherSize=0;
    unsigned largeLogical=0,totalLogical=0;
};
inline unsigned popcount64(std::uint64_t value){unsigned n=0;while(value){value&=value-1;++n;}return n;}
inline bool inCache(const L3& cache,std::uint16_t group,std::uint8_t logical){
    if(logical>=64)return false;
    for(const auto& m:cache.masks)if(m.first==group&&((m.second>>logical)&1u))return true;
    return false;
}
inline bool sameCache(const L3& a,const L3& b){return a.size==b.size&&a.masks==b.masks;}
// Pure topology decision. The apply entry point separately requires AMD vendor.
inline Plan plan(std::vector<L3> caches,const std::vector<Cpu>& cpus){
    Plan p;
    auto refuse=[&](const char* reason){p.status=Status::Refused;p.reason=reason;p.ids.clear();return p;};
    std::vector<L3> unique;
    for(auto& c:caches){
        if(!c.size||c.masks.empty())return refuse("invalid L3 domain");
        std::sort(c.masks.begin(),c.masks.end());
        for(std::size_t i=0;i<c.masks.size();++i)
            if(!c.masks[i].second||(i&&c.masks[i-1].first==c.masks[i].first))return refuse("invalid L3 group masks");
        if(std::none_of(unique.begin(),unique.end(),[&](const L3& u){return sameCache(u,c);}))unique.push_back(c);
    }
    if(unique.empty())return refuse("no L3 domains");
    if(unique.size()==1){p.reason="single L3 domain: all cores already share one cache";return p;}
    if(unique.size()!=2)return refuse("expected exactly two L3 domains");
    for(const auto& a:unique[0].masks)for(const auto& b:unique[1].masks)
        if(a.first==b.first&&(a.second&b.second))return refuse("overlapping L3 masks");
    for(std::size_t i=0;i<cpus.size();++i){
        const auto& c=cpus[i];
        if(c.logical>=64||(!inCache(unique[0],c.group,c.logical)&&!inCache(unique[1],c.group,c.logical)))return refuse("CPU outside L3 domains");
        for(std::size_t j=0;j<i;++j)
            if(c.id==cpus[j].id||(c.group==cpus[j].group&&c.logical==cpus[j].logical))return refuse("duplicate CPU-set ID or processor");
        if(!c.unavailable)++p.totalLogical;
    }
    unsigned expected=0;
    for(const auto& c:unique)for(const auto& m:c.masks)expected+=popcount64(m.second);
    if(cpus.size()!=expected)return refuse("incomplete CPU-set topology");
    std::sort(unique.begin(),unique.end(),[](const L3& a,const L3& b){return a.size>b.size;});
    p.largeSize=unique[0].size;p.otherSize=unique[1].size;
    // Subtraction avoids overflow for synthetic or corrupt large sizes.
    if(p.largeSize-p.otherSize<p.otherSize/2||p.largeSize==p.otherSize){p.reason="symmetric L3 domains: no V-Cache CCD to prefer";return p;}
    for(const auto& c:cpus)if(!c.unavailable&&inCache(unique[0],c.group,c.logical))p.ids.push_back(c.id);
    p.largeLogical=unsigned(p.ids.size());
    if(p.largeLogical<6)return refuse("too few logical processors on the V-Cache CCD");
    if(p.largeLogical>=p.totalLogical){p.reason="V-Cache domain already covers every available processor";p.ids.clear();return p;}
    p.status=Status::Apply;p.reason="asymmetric AMD L3: preferring the larger cache domain";
    return p;
}

using GetSystemCpuSetInformationFn=BOOL(WINAPI*)(PSYSTEM_CPU_SET_INFORMATION,ULONG,PULONG,HANDLE,ULONG);
using SetProcessDefaultCpuSetsFn=BOOL(WINAPI*)(HANDLE,const ULONG*,ULONG);
using GetProcessDefaultCpuSetsFn=BOOL(WINAPI*)(HANDLE,PULONG,ULONG,PULONG);
using GetCachesFn=BOOL(WINAPI*)(LOGICAL_PROCESSOR_RELATIONSHIP,PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX,PDWORD);
struct Api {
    GetSystemCpuSetInformationFn query=nullptr;
    SetProcessDefaultCpuSetsFn set=nullptr;
    GetProcessDefaultCpuSetsFn get=nullptr;
    GetCachesFn caches=nullptr;
    decltype(&GetProcessAffinityMask) affinity=nullptr;
    decltype(&GetActiveProcessorGroupCount) groups=nullptr;
    bool amd=false;
};
inline bool queryCaches(std::vector<L3>& out,GetCachesFn query=&GetLogicalProcessorInformationEx){
    out.clear();if(!query)return false;DWORD bytes=0;SetLastError(ERROR_SUCCESS);
    if(query(RelationCache,nullptr,&bytes)||GetLastError()!=ERROR_INSUFFICIENT_BUFFER||!bytes||bytes>(1u<<24))return false;
    std::vector<unsigned char> buffer(bytes);const DWORD capacity=bytes;
    if(!query(RelationCache,reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),&bytes)||!bytes||bytes>capacity)return false;
    std::vector<L3> parsed;
    constexpr auto cacheOffset=offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX,Cache);
    constexpr auto maskOffset=cacheOffset+offsetof(CACHE_RELATIONSHIP,GroupMask);
    // The WORD immediately before GroupMask is GroupCount in new Windows SDKs,
    // and the last two bytes of Reserved[20] in older SDKs. Read the wire bytes,
    // not beyond the bounds of new SDKs' Reserved[18] array.
    constexpr auto groupOffset=maskOffset-sizeof(WORD);
    for(std::size_t offset=0;offset<bytes;){
        if(bytes-offset<cacheOffset)return false;
        LOGICAL_PROCESSOR_RELATIONSHIP relation{};DWORD size=0;
        std::memcpy(&relation,buffer.data()+offset,sizeof(relation));
        std::memcpy(&size,buffer.data()+offset+sizeof(relation),sizeof(size));
        if(size<maskOffset+sizeof(GROUP_AFFINITY)||size>bytes-offset||relation!=RelationCache)return false;
        CACHE_RELATIONSHIP cache{};std::memcpy(&cache,buffer.data()+offset+cacheOffset,sizeof(cache));
        WORD groups=0;std::memcpy(&groups,buffer.data()+offset+groupOffset,sizeof(groups));
        if(!groups)groups=1; // Pre-Windows 11 records have exactly one GroupMask.
        if(groups>(size-maskOffset)/sizeof(GROUP_AFFINITY))return false;
        if(cache.Level==3&&(cache.Type==CacheUnified||cache.Type==CacheData)){
            L3 l3;l3.size=cache.CacheSize;
            for(WORD g=0;g<groups;++g){
                GROUP_AFFINITY mask{};std::memcpy(&mask,buffer.data()+offset+maskOffset+g*sizeof(mask),sizeof(mask));
                if(!mask.Mask||!l3.size)return false;
                l3.masks.push_back({mask.Group,std::uint64_t(mask.Mask)});
            }
            parsed.push_back(std::move(l3));
        }
        offset+=size;
    }
    if(parsed.empty())return false;
    out=std::move(parsed);return true;
}
inline bool queryCpus(GetSystemCpuSetInformationFn query,std::vector<Cpu>& out){
    out.clear();if(!query)return false;ULONG bytes=0;HANDLE process=GetCurrentProcess();SetLastError(ERROR_SUCCESS);
    if(query(nullptr,0,&bytes,process,0)||GetLastError()!=ERROR_INSUFFICIENT_BUFFER||!bytes||bytes>(1u<<24))return false;
    std::vector<unsigned char> buffer(bytes);const ULONG capacity=bytes;
    if(!query(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()),capacity,&bytes,process,0)||!bytes||bytes>capacity)return false;
    std::vector<Cpu> parsed;
    for(std::size_t offset=0;offset<bytes;){
        if(bytes-offset<sizeof(ULONG))return false;
        ULONG size=0;std::memcpy(&size,buffer.data()+offset,sizeof(size));
        if(size<sizeof(SYSTEM_CPU_SET_INFORMATION)||size>bytes-offset)return false;
        SYSTEM_CPU_SET_INFORMATION entry{};std::memcpy(&entry,buffer.data()+offset,sizeof(entry));
        if(entry.Type!=CpuSetInformation||entry.CpuSet.LogicalProcessorIndex>=64)return false;
        const BYTE flags=entry.CpuSet.AllFlags;
        parsed.push_back({entry.CpuSet.Id,entry.CpuSet.Group,entry.CpuSet.LogicalProcessorIndex,
            bool(((flags&0x2)&&!(flags&0x4))||(flags&0x8))});
        offset+=size;
    }
    if(parsed.empty())return false;
    out=std::move(parsed);return true;
}
inline const char* existingPolicy(const Api& api){
    DWORD_PTR processMask=0,systemMask=0;
    if(!api.affinity(GetCurrentProcess(),&processMask,&systemMask)||!processMask||!systemMask)
        return "cpu cache affinity: process affinity query failed or ambiguous; unchanged";
    if(processMask!=systemMask)return "cpu cache affinity: process affinity already chosen; unchanged";
    ULONG existing=0;
    if(!api.get(GetCurrentProcess(),nullptr,0,&existing))
        return "cpu cache affinity: default CPU sets already chosen or query failed; unchanged";
    if(existing)return "cpu cache affinity: process already has default CPU sets; unchanged";
    return nullptr;
}
inline std::string applyPreferVCache(const Api& api){
    if(!api.amd)return "cpu cache affinity: processor vendor is not AMD; unchanged";
    if(!api.query||!api.set||!api.get||!api.caches||!api.affinity||!api.groups)
        return "cpu cache affinity: required CPU APIs unavailable; unchanged";
    // GetProcessAffinityMask cannot prove unrestricted state across groups.
    if(api.groups()!=1)return "cpu cache affinity: multiple or unknown processor groups; unchanged";
    if(const char* reason=existingPolicy(api))return reason;
    std::vector<L3> caches;std::vector<Cpu> cpus;
    if(!queryCaches(caches,api.caches)||!queryCpus(api.query,cpus))return "cpu cache affinity: processor topology unavailable; unchanged";
    // A single-group query and the topology must agree.
    for(const auto& c:cpus)if(c.group!=0)return "cpu cache affinity: inconsistent processor group; unchanged";
    const Plan p=plan(caches,cpus);char line[320]{};
    if(p.status!=Status::Apply){
        std::snprintf(line,sizeof(line),"cpu cache affinity: %s (L3 %llu/%llu MB); unchanged",p.reason,
            static_cast<unsigned long long>(p.largeSize>>20),static_cast<unsigned long long>(p.otherSize>>20));return line;
    }
    std::vector<ULONG> ids(p.ids.begin(),p.ids.end());
    // Recheck immediately before setting. Windows offers no atomic compare/set;
    // this preserves policies observed before/during the topology query, but a
    // concurrent scheduler utility can still race the final call.
    if(const char* reason=existingPolicy(api))return reason;
    if(!api.set(GetCurrentProcess(),ids.data(),ULONG(ids.size()))){
        std::snprintf(line,sizeof(line),"cpu cache affinity: SetProcessDefaultCpuSets failed (%lu); unchanged",GetLastError());return line;
    }
    std::snprintf(line,sizeof(line),"cpu cache affinity: %s; %u of %u logical processors (L3 %llu MB vs %llu MB)",p.reason,
        p.largeLogical,p.totalLogical,static_cast<unsigned long long>(p.largeSize>>20),static_cast<unsigned long long>(p.otherSize>>20));return line;
}
inline bool isAmdProcessor(){
    char vendor[13]{};
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int registers[4]{};__cpuid(registers,0);
    std::memcpy(vendor,&registers[1],4);std::memcpy(vendor+4,&registers[3],4);std::memcpy(vendor+8,&registers[2],4);
#elif defined(__i386__) || defined(__x86_64__)
    unsigned a=0,b=0,c=0,d=0;
    if(!__get_cpuid(0,&a,&b,&c,&d))return false;
    std::memcpy(vendor,&b,4);std::memcpy(vendor+4,&d,4);std::memcpy(vendor+8,&c,4);
#else
    return false;
#endif
    return std::strcmp(vendor,"AuthenticAMD")==0;
}
inline std::string applyPreferVCache(){
    try {
        HMODULE kernel=GetModuleHandleW(L"kernel32.dll");Api api;
        api.amd=isAmdProcessor();
        api.query=kernel?reinterpret_cast<GetSystemCpuSetInformationFn>(reinterpret_cast<void*>(GetProcAddress(kernel,"GetSystemCpuSetInformation"))):nullptr;
        api.set=kernel?reinterpret_cast<SetProcessDefaultCpuSetsFn>(reinterpret_cast<void*>(GetProcAddress(kernel,"SetProcessDefaultCpuSets"))):nullptr;
        api.get=kernel?reinterpret_cast<GetProcessDefaultCpuSetsFn>(reinterpret_cast<void*>(GetProcAddress(kernel,"GetProcessDefaultCpuSets"))):nullptr;
        api.caches=&GetLogicalProcessorInformationEx;api.affinity=&GetProcessAffinityMask;api.groups=&GetActiveProcessorGroupCount;
        return applyPreferVCache(api);
    }catch(...){return "cpu cache affinity: query allocation failed; unchanged";}
}
} // namespace cpu_cache
