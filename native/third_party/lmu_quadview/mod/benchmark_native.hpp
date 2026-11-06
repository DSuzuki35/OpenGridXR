#pragma once
#include <windows.h>
#include <sddl.h>
#ifdef _MSC_VER
#pragma comment(lib,"advapi32.lib")
#endif
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <cmath>
#include <limits>
namespace lmuqv { namespace benchmark {
constexpr uint32_t Magic=0x51424d4c,Version=1,Slots=4096,MaxWidth=768,MaxHeight=512;
inline double missing(){return std::numeric_limits<double>::quiet_NaN();}
struct Header {
 uint32_t magic,version,size,pid;int64_t frequency,counter,heartbeat;
 int32_t configSequence,visible,layout,recording;float x,y,distance,width;
 int32_t bitmapSequence,bitmapWidth,bitmapHeight,status,running,result,projections;
 uint32_t gpuFailures,reserved104,outer0Width,outer0Height,outer1Width,outer1Height,focusWidth,focusHeight;
 int32_t reason;
 // Optional ABI-v1 extension in reserved space. Own seqlock, old readers ignore it.
 int32_t focusSequence;uint32_t focusFlags;
 float focusScale,leftHorizontal,leftVertical,rightHorizontal,rightVertical;
 uint32_t focusPadding;uint64_t focusFrame;uint8_t reserved[80];
};
struct Sample {int64_t commit,qpc;uint64_t frame;double intervalMs,appMs,providerMs,gpuMs;uint64_t gpuFrame;uint32_t flags;int32_t result;uint64_t reserved;};
struct Shared {Header header;Sample ring[Slots];uint8_t bitmap[MaxWidth*MaxHeight*4];};
static_assert(sizeof(Header)==256&&sizeof(Sample)==80&&sizeof(Shared)==1900800,"benchmark ABI size");
static_assert(offsetof(Header,reason)==132&&offsetof(Header,bitmapSequence)==72&&offsetof(Header,gpuFailures)==100,"benchmark header ABI");
static_assert(offsetof(Shared,ring)==256&&offsetof(Shared,bitmap)==327936&&offsetof(Sample,gpuFrame)==56,"benchmark sample ABI");
#define BENCH_OFFSET(type,field,offset) static_assert(offsetof(type,field)==offset,"benchmark ABI " #field)
BENCH_OFFSET(Header,magic,0);BENCH_OFFSET(Header,version,4);BENCH_OFFSET(Header,size,8);BENCH_OFFSET(Header,pid,12);
BENCH_OFFSET(Header,frequency,16);BENCH_OFFSET(Header,counter,24);BENCH_OFFSET(Header,heartbeat,32);
BENCH_OFFSET(Header,configSequence,40);BENCH_OFFSET(Header,visible,44);BENCH_OFFSET(Header,layout,48);BENCH_OFFSET(Header,recording,52);
BENCH_OFFSET(Header,x,56);BENCH_OFFSET(Header,y,60);BENCH_OFFSET(Header,distance,64);BENCH_OFFSET(Header,width,68);
BENCH_OFFSET(Header,bitmapWidth,76);BENCH_OFFSET(Header,bitmapHeight,80);BENCH_OFFSET(Header,status,84);BENCH_OFFSET(Header,running,88);
BENCH_OFFSET(Header,result,92);BENCH_OFFSET(Header,projections,96);BENCH_OFFSET(Header,outer0Width,108);BENCH_OFFSET(Header,outer0Height,112);
BENCH_OFFSET(Header,outer1Width,116);BENCH_OFFSET(Header,outer1Height,120);BENCH_OFFSET(Header,focusWidth,124);BENCH_OFFSET(Header,focusHeight,128);
BENCH_OFFSET(Header,focusSequence,136);BENCH_OFFSET(Header,focusFlags,140);BENCH_OFFSET(Header,focusScale,144);
BENCH_OFFSET(Header,leftHorizontal,148);BENCH_OFFSET(Header,leftVertical,152);BENCH_OFFSET(Header,rightHorizontal,156);BENCH_OFFSET(Header,rightVertical,160);BENCH_OFFSET(Header,focusFrame,168);
BENCH_OFFSET(Sample,commit,0);BENCH_OFFSET(Sample,qpc,8);BENCH_OFFSET(Sample,frame,16);BENCH_OFFSET(Sample,intervalMs,24);
BENCH_OFFSET(Sample,appMs,32);BENCH_OFFSET(Sample,providerMs,40);BENCH_OFFSET(Sample,gpuMs,48);BENCH_OFFSET(Sample,flags,64);BENCH_OFFSET(Sample,result,68);BENCH_OFFSET(Sample,reserved,72);
#undef BENCH_OFFSET
inline int64_t counter_load(const int64_t& v){return InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(const_cast<int64_t*>(&v)),0,0);}
inline int32_t sequence_load(const int32_t& v){return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(const_cast<int32_t*>(&v)),0,0);}
inline int64_t ticks(){LARGE_INTEGER value{};QueryPerformanceCounter(&value);return value.QuadPart;}
struct FocusTelemetry {
 // flags: 1=valid rendered quad scene. Bits 2/4 and scale are legacy ABI slots;
 // corrected 0.2.1 emits no dynamic flags and always publishes scale=100.
 uint32_t flags=0;float scale=100,leftHorizontal=0,leftVertical=0,rightHorizontal=0,rightVertical=0;
 uint64_t frame=0;
};
inline void publish_focus(Shared& data,const FocusTelemetry& value){
 auto& h=data.header;InterlockedIncrement(reinterpret_cast<volatile LONG*>(&h.focusSequence));
 h.focusFlags=value.flags;h.focusScale=value.scale;h.leftHorizontal=value.leftHorizontal;h.leftVertical=value.leftVertical;
 h.rightHorizontal=value.rightHorizontal;h.rightVertical=value.rightVertical;h.focusFrame=value.frame;
 InterlockedIncrement(reinterpret_cast<volatile LONG*>(&h.focusSequence));
}
inline void initialize(Shared& data,uint32_t pid,int64_t frequency){
 std::memset(&data,0,sizeof(data));auto& h=data.header;h.magic=Magic;h.version=Version;h.size=sizeof(data);h.pid=pid;h.frequency=frequency;
 h.y=-.25f;h.distance=1.2f;h.width=.65f;h.status=1;h.heartbeat=ticks();
}
inline void publish(Shared& data,Sample sample){
 const int64_t number=counter_load(data.header.counter)+1;auto& slot=data.ring[(number-1)%Slots];
 InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&slot.commit),-number);
 std::memcpy(reinterpret_cast<uint8_t*>(&slot)+8,reinterpret_cast<const uint8_t*>(&sample)+8,sizeof(Sample)-8);
 InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&slot.commit),number);
 InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&data.header.heartbeat),sample.qpc);
 InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&data.header.counter),number);
}
inline bool read_sample(const Shared& data,int64_t number,Sample& sample){
 if(number<1)return false;const auto& slot=data.ring[(number-1)%Slots];if(counter_load(slot.commit)!=number)return false;
 std::memcpy(&sample,&slot,sizeof(sample));MemoryBarrier();return counter_load(slot.commit)==number;
}
struct Config {int32_t visible=0,layout=0,recording=0;float x=0,y=-.25f,distance=1.2f,width=.65f;};
inline bool read_config(const Shared& data,Config& config){
 const auto& h=data.header;int32_t sequence=sequence_load(h.configSequence);if(sequence&1)return false;
 Config c{h.visible,h.layout,h.recording,h.x,h.y,h.distance,h.width};MemoryBarrier();if(sequence_load(h.configSequence)!=sequence)return false;
 if(!std::isfinite(c.x)||!std::isfinite(c.y)||!std::isfinite(c.distance)||!std::isfinite(c.width)||std::abs(c.x)>5||std::abs(c.y)>5||c.distance<.2f||c.distance>10||c.width<.1f||c.width>3)return false;
 config=c;return true;
}
// 0.3.6: the channel is created with an explicit DACL (current user and SYSTEM
// only) instead of the token default. Failure disables the optional transport.
inline PSECURITY_DESCRIPTOR owner_only_descriptor(){
 HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return nullptr;
 DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);
 PSECURITY_DESCRIPTOR descriptor=nullptr;
 if(size&&size<=4096){
  alignas(TOKEN_USER) unsigned char buffer[4096];
  if(GetTokenInformation(token,TokenUser,buffer,size,&size)){
   LPWSTR sid=nullptr;
   if(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid,&sid)){
    wchar_t sddl[256]{};
    if(std::swprintf(sddl,256,L"D:P(A;;GA;;;%ls)(A;;GA;;;SY)",sid)>0)
     ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,SDDL_REVISION_1,&descriptor,nullptr);
    LocalFree(sid);
   }
  }
 }
 CloseHandle(token);return descriptor;
}
// Pin the exact section created by this DLL until unload. A boolean recording a
// past creation is insufficient: after the last handle closes, another process
// could replace the name between sessions. The pin retains sizeof(Shared) of
// section commit, but no view; later sessions duplicate this trusted handle.
// As with Transport itself, callers serialize open/reset (the XR state mutex).
struct ChannelIdentity {
 HANDLE mapping=nullptr;
 ~ChannelIdentity(){if(mapping)CloseHandle(mapping);}
};
inline ChannelIdentity& channel_identity(){static ChannelIdentity identity;return identity;}
class Transport {
 HANDLE mapping_=nullptr;Shared* data_=nullptr;
public:
 ~Transport(){reset();}Transport()=default;Transport(const Transport&)=delete;Transport& operator=(const Transport&)=delete;
 bool open(){reset();wchar_t name[96]{};std::swprintf(name,96,L"Local\\LMUQuadView.Benchmark.%lu",GetCurrentProcessId());
 auto& identity=channel_identity();
 if(identity.mapping){
  if(!DuplicateHandle(GetCurrentProcess(),identity.mapping,GetCurrentProcess(),&mapping_,0,FALSE,DUPLICATE_SAME_ACCESS))return false;
 }else{
  SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,FALSE};PSECURITY_DESCRIPTOR descriptor=owner_only_descriptor();attributes.lpSecurityDescriptor=descriptor;
  if(!descriptor){if(GetLastError()==ERROR_SUCCESS)SetLastError(ERROR_ACCESS_DENIED);return false;}
  SetLastError(ERROR_SUCCESS);
  mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,&attributes,PAGE_READWRITE,0,sizeof(Shared),name);
  const DWORD created=GetLastError();LocalFree(descriptor);
  if(!mapping_){SetLastError(created);return false;}
  if(created==ERROR_ALREADY_EXISTS){reset();SetLastError(ERROR_ALREADY_EXISTS);return false;}
 }
 data_=static_cast<Shared*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));
 if(!data_){const DWORD error=GetLastError();reset();SetLastError(error);return false;}
 if(!identity.mapping&&!DuplicateHandle(GetCurrentProcess(),mapping_,GetCurrentProcess(),&identity.mapping,0,FALSE,DUPLICATE_SAME_ACCESS)){
  const DWORD error=GetLastError();reset();SetLastError(error);return false;
 }
 LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);initialize(*data_,GetCurrentProcessId(),frequency.QuadPart);return true;}
 void reset(){if(data_){UnmapViewOfFile(data_);data_=nullptr;}if(mapping_){CloseHandle(mapping_);mapping_=nullptr;}}
 Shared* data(){return data_;}
};
} }
