#include "fixtures.hpp"
#include "windows_memory.hpp"
#include <windows.h>
#include <iostream>
using namespace og::telemetry;
struct Mapping {
    HANDLE handle{};unsigned char *data{};
    Mapping(const std::wstring&name,std::size_t size){
        handle=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,DWORD(size),name.c_str());
        if(!handle||GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("test namespace already exists");
        data=static_cast<unsigned char*>(MapViewOfFile(handle,FILE_MAP_WRITE,0,0,size));
        if(!data)throw std::runtime_error("test fixture mapping failed");
    }
    ~Mapping(){if(data)UnmapViewOfFile(data);if(handle)CloseHandle(handle);}
};
int main(int argc,char**argv){try{
    std::string prefix;unsigned duration=5000,freeze=0;
    for(int n=1;n<argc;++n){std::string arg=argv[n];if(n+1>=argc)throw std::runtime_error("missing value");
        std::string v=argv[++n];if(arg=="--namespace")prefix=v;else if(arg=="--duration-ms")duration=std::stoul(v);
        else if(arg=="--freeze-after-ms")freeze=std::stoul(v);else throw std::runtime_error("unknown option");}
    if(!valid_test_namespace(prefix)||duration>60000||duration<1)throw std::runtime_error("invalid test options");
    auto ir=fixture::iracing(),lmu=fixture::lmu();
    Mapping im(mapping_name(Game::IRacing,prefix),ir.size()),lm(mapping_name(Game::Lmu,prefix),lmu.size()),lock(lock_name(prefix),8);
    std::memcpy(im.data,ir.data.data(),ir.size());std::memcpy(lm.data,lmu.data.data(),lmu.size());
    std::memset(lock.data,0,8);
    std::cout<<"{\"ready\":true,\"testData\":true}\n"<<std::flush;
    auto start=GetTickCount64();unsigned tick=100;
    while(GetTickCount64()-start<duration){
        auto elapsed=GetTickCount64()-start;
        if(!freeze||elapsed<freeze){
            ++tick;auto slot=tick%2;auto offset=4096+slot*256;
            float lap=32.5f+float(elapsed)/1000;std::memcpy(im.data+offset+36,&lap,4);MemoryBarrier();
            InterlockedExchange(reinterpret_cast<volatile LONG*>(im.data+48+slot*16),LONG(tick));
            InterlockedExchange(reinterpret_cast<volatile LONG*>(lock.data+4),1);
            double time=123.5+double(elapsed)/1000;
            std::memcpy(lm.data+130356+12,&time,8);std::memcpy(lm.data+1700,&time,8);
            MemoryBarrier();InterlockedExchange(reinterpret_cast<volatile LONG*>(lock.data+4),0);
        }
        Sleep(25);
    }return 0;
}catch(const std::exception&e){std::cerr<<"Fixture: "<<e.what()<<'\n';return 1;}}
