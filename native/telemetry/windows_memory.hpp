#pragma once
#include "telemetry.hpp"
#include <windows.h>
#include <cstring>
#include <string>
namespace og::telemetry {
class MappedMemory final:public Memory{
    HANDLE handle_{};const unsigned char *view_{};std::size_t length_{};DWORD error_{};
public:
    ~MappedMemory(){close();}
    MappedMemory()=default;
    MappedMemory(const MappedMemory&)=delete;
    MappedMemory&operator=(const MappedMemory&)=delete;
    bool open(const std::wstring&name){
        close();error_=ERROR_SUCCESS;
        handle_=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());if(!handle_){error_=GetLastError();return false;}
        view_=static_cast<const unsigned char*>(MapViewOfFile(handle_,FILE_MAP_READ,0,0,0));
        if(!view_){error_=GetLastError();close();return false;}
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(view_,&info,sizeof(info))){error_=GetLastError();close();return false;}
        if(info.RegionSize>128*1024*1024||info.State!=MEM_COMMIT){error_=ERROR_INVALID_DATA;close();return false;}
        length_=info.RegionSize;return true;
    }
    void close(){if(view_)UnmapViewOfFile(view_);if(handle_)CloseHandle(handle_);view_=nullptr;handle_=nullptr;length_=0;}
    bool opened()const{return view_!=nullptr;}
    const char* failure_reason()const{
        if(error_==ERROR_FILE_NOT_FOUND)return "mapping-not-found";
        if(error_==ERROR_ACCESS_DENIED)return "mapping-access-denied";
        return "mapping-open-failed";
    }
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

class SteeringInput {
    MappedMemory memory_,lock_;
    SteeringDecoder decoder_;
    Game game_{Game::IRacing};
    bool seeded_{},advanced_{};
    std::uint64_t token_{},lastChange_{},nextOpen_{};
    std::optional<double> angle_;
public:
    void reset(){
        memory_.close();lock_.close();decoder_={};seeded_=advanced_=false;angle_.reset();nextOpen_=0;
    }
    std::optional<double> read(Game game,std::uint64_t nowMs) {
        if(game!=game_){reset();game_=game;}
        if(!memory_.opened()){
            if(nowMs<nextOpen_)return {};
            nextOpen_=nowMs+1000;
            if(!memory_.open(mapping_name(game,{})))return {};
            lastChange_=nowMs;
            if(game==Game::Lmu)lock_.open(lock_name({}));
        }
        const auto sample=decoder_.read(game,memory_,lock_.opened()?&lock_:nullptr);
        if(sample.status==ReadStatus::Disconnected || sample.status==ReadStatus::Invalid){
            reset();nextOpen_=nowMs+500;return {};
        }
        if(sample.status==ReadStatus::Unsupported){angle_.reset();}
        if(sample.status==ReadStatus::Valid){
            if(!seeded_||token_!=sample.token){
                advanced_=seeded_;seeded_=true;token_=sample.token;lastChange_=nowMs;
            }
            angle_=sample.radians;
        }
        // A dead producer's mapping can remain alive because this reader owns
        // a handle. Release it periodically so a restarted producer can replace it.
        if(nowMs-lastChange_>5000){reset();nextOpen_=nowMs+500;return {};}
        if(!advanced_||nowMs-lastChange_>250)return {};
        return angle_;
    }
};
}
