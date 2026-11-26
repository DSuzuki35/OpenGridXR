#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace og::artura {
inline constexpr double pi=3.14159265358979323846;
inline bool device_name(std::string_view name) { return name=="Ascher Racing Artura ULTIMATE"; }
// Zero-based DirectInput buttons, measured on the USB Artura in pulsed mode.
// A state (rather than accumulated +/- steps) prevents drift when pulses skip.
struct Positions {
    std::array<unsigned,2> position{};
    void reset(){position={};}
    void sample(const std::array<bool,128>& buttons) {
        for(unsigned side=0;side<2;++side) {
            unsigned value=0,count=0;
            for(unsigned i=0;i<12;++i)if(buttons[30+12*side+i]){value=i+1;++count;}
            // Retain the last absolute position between pulses. Ambiguous
            // simultaneous inputs do not choose an arbitrary new position.
            if(count==1)position[side]=value;
        }
    }
    unsigned valid()const{return (position[0]?1u:0u)|(position[1]?2u:0u);}
    float angle(unsigned side)const {
        if(side>1 || !position[side] || position[side]>12)return 0;
        const double offset=side?pi/6:-pi/2;
        return float(std::remainder(-double(position[side]-1)*pi/6+offset,2*pi));
    }
};
}

#ifdef _WIN32
#include <windows.h>
#include <string>
namespace og::artura {
inline constexpr wchar_t MappingName[]=L"Local\\OpenGridXR.ArturaDials.v1";
inline constexpr wchar_t MutexName[]=L"Local\\OpenGridXR.ArturaDials.Writer.v1";
struct alignas(8) Sample {
    volatile LONG sequence;
    std::uint32_t version;
    std::uint64_t tick;
    float left,right;
    std::uint32_t valid,reserved;
};
static_assert(sizeof(Sample)==32);
class Writer {
    HANDLE mutex{},mapping{};Sample* sample{};bool owned{};
    std::uint64_t nextOpen{};
    std::wstring mappingName,mutexName;
public:
    explicit Writer(const wchar_t* name=MappingName,const wchar_t* lock=MutexName):mappingName(name),mutexName(lock){}
    Writer(const Writer&)=delete;Writer& operator=(const Writer&)=delete;
    bool acquire(std::uint64_t now=GetTickCount64()) {
        if(sample)return true;
        if(now<nextOpen)return false;nextOpen=now+1000;
        if(!mutex)mutex=CreateMutexW(nullptr,FALSE,mutexName.c_str());
        if(!mutex)return false;
        const auto result=WaitForSingleObject(mutex,0);
        if(result!=WAIT_OBJECT_0 && result!=WAIT_ABANDONED)return false;
        owned=true;
        mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Sample),mappingName.c_str());
        if(mapping)sample=static_cast<Sample*>(MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,sizeof(Sample)));
        if(!sample){close();return false;}
        InterlockedExchange(&sample->sequence,1);
        sample->version=1;sample->tick=now;sample->left=sample->right=0;sample->valid=sample->reserved=0;
        InterlockedExchange(&sample->sequence,2);return true;
    }
    void publish(const Positions& positions,std::uint64_t now=GetTickCount64()) {
        if(!sample)return;
        InterlockedIncrement(&sample->sequence);
        sample->tick=now;sample->left=positions.angle(0);sample->right=positions.angle(1);sample->valid=positions.valid();
        InterlockedIncrement(&sample->sequence);
    }
    void close() {
        if(sample){publish({});UnmapViewOfFile(sample);sample=nullptr;}
        if(mapping){CloseHandle(mapping);mapping=nullptr;}
        if(mutex){if(owned)ReleaseMutex(mutex);CloseHandle(mutex);mutex=nullptr;}owned=false;
    }
    ~Writer(){close();}
};
class Reader {
    HANDLE mapping{};const volatile Sample* sample{};std::uint64_t nextOpen{};
    std::wstring mappingName;
public:
    float angles[4]{};
    explicit Reader(const wchar_t* name=MappingName):mappingName(name){}
    Reader(const Reader&)=delete;Reader& operator=(const Reader&)=delete;
    void update(std::uint64_t now=GetTickCount64()) {
        if(!sample && now>=nextOpen) {
            nextOpen=now+1000;mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,mappingName.c_str());
            if(mapping){sample=static_cast<const volatile Sample*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(Sample)));if(!sample){CloseHandle(mapping);mapping=nullptr;}}
        }
        for(auto& value:angles)value=0;
        if(!sample)return;
        const LONG before=sample->sequence;if(before&1)return;
        MemoryBarrier();
        const auto version=sample->version,valid=sample->valid;const auto tick=sample->tick;
        const float left=sample->left,right=sample->right;
        MemoryBarrier();
        if(before!=sample->sequence || version!=1 || valid>3 || tick>now || now-tick>1000 ||
           !std::isfinite(left)||!std::isfinite(right)||std::abs(left)>3.142f||std::abs(right)>3.142f)return;
        angles[0]=(valid&1)?left:0;angles[1]=(valid&2)?right:0;angles[2]=1;
    }
    ~Reader(){if(sample)UnmapViewOfFile(const_cast<const Sample*>(sample));if(mapping)CloseHandle(mapping);}
};
}
#endif
