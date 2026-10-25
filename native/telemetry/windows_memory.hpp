#pragma once
#include "telemetry.hpp"
#include <windows.h>
#include <cstring>
#include <string>
namespace og::telemetry {
class MappedMemory final:public Memory{
    HANDLE handle_{};const unsigned char *view_{};std::size_t length_{};
public:
    ~MappedMemory(){close();}
    MappedMemory()=default;
    MappedMemory(const MappedMemory&)=delete;
    MappedMemory&operator=(const MappedMemory&)=delete;
    bool open(const std::wstring&name){
        close();handle_=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());if(!handle_)return false;
        view_=static_cast<const unsigned char*>(MapViewOfFile(handle_,FILE_MAP_READ,0,0,0));
        if(!view_){close();return false;}
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(view_,&info,sizeof(info))||info.RegionSize>128*1024*1024||info.State!=MEM_COMMIT){close();return false;}
        length_=info.RegionSize;return true;
    }
    void close(){if(view_)UnmapViewOfFile(view_);if(handle_)CloseHandle(handle_);view_=nullptr;handle_=nullptr;length_=0;}
    bool opened()const{return view_!=nullptr;}
    std::size_t size()const override{return length_;}
    bool copy(std::size_t offset,void*destination,std::size_t count)const override{
        if(!view_||offset>length_||count>length_-offset)return false;
        MemoryBarrier();std::memcpy(destination,view_+offset,count);MemoryBarrier();return true;
    }
};
inline bool valid_test_namespace(const std::string&s){
    if(s.empty()||s.size()>64)return false;
    for(unsigned char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'))return false;
    return true;
}
inline std::wstring mapping_name(Game game,const std::string&testNamespace){
    if(testNamespace.empty())return game==Game::IRacing?L"Local\\IRSDKMemMapFileName":L"LMU_Data";
    return L"Local\\OpenGridXR-Test-"+std::wstring(testNamespace.begin(),testNamespace.end())+
        (game==Game::IRacing?L"-iracing":L"-lmu");
}
inline std::wstring lock_name(const std::string&testNamespace){
    if(testNamespace.empty())return L"LMU_SharedMemoryLockData";
    return L"Local\\OpenGridXR-Test-"+std::wstring(testNamespace.begin(),testNamespace.end())+L"-lock";
}
}
