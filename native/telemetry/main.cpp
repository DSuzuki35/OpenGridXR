#include "windows_memory.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>
using namespace og::telemetry;
namespace {
HANDLE stopEvent{};
BOOL WINAPI console_stop(DWORD){if(stopEvent)SetEvent(stopEvent);return TRUE;}
std::uint64_t utc_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();}
unsigned positive(const std::string&s,unsigned maximum){
    if(s.empty()||s.size()>8)throw std::runtime_error("invalid numeric option");
    for(char c:s)if(c<'0'||c>'9')throw std::runtime_error("invalid numeric option");
    auto n=std::stoul(s);if(n==0||n>maximum)throw std::runtime_error("numeric option out of range");return unsigned(n);
}
bool stdout_line(const std::string&s){
    if(s.size()>8192)return false;DWORD wrote=0;
    return WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s.data(),DWORD(s.size()),&wrote,nullptr)&&wrote==s.size();
}
bool stdin_stop(){
    auto input=GetStdHandle(STD_INPUT_HANDLE);
    if(!input||input==INVALID_HANDLE_VALUE||GetFileType(input)!=FILE_TYPE_PIPE)return false;
    DWORD available=0;if(!PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr))return true;
    if(!available)return false;
    char bytes[64];DWORD count=0;
    if(!ReadFile(input,bytes,std::min<DWORD>(available,sizeof(bytes)),&count,nullptr))return true;
    static std::string line;
    for(DWORD n=0;n<count;++n){if(bytes[n]=='\n'){
        bool stop=line=="quit"||line=="stop";line.clear();if(stop)return true;
    }else if(bytes[n]!='\r'){if(line.size()<64)line+=bytes[n];else line.clear();}}
    return false;
}
struct Source {
    Game game;MappedMemory memory,lock;IRacingDecoder iracing;LmuDecoder lmu;Tracker tracker;
    std::uint64_t due{},openedAt{},lastEmit{};std::string previousStatus;
    explicit Source(Game g):game(g){}
    Record poll(std::uint64_t now,const std::string&testNamespace,unsigned rate){
        Sample sample;
        if(!memory.opened()){
            if(memory.open(mapping_name(game,testNamespace))){openedAt=now;
                if(game==Game::Lmu)lock.open(lock_name(testNamespace));}
        }
        if(memory.opened())sample=game==Game::IRacing?iracing.read(memory):lmu.read(memory,lock.opened()?&lock:nullptr);
        auto record=tracker.observe(game,sample,now,utc_ms(),!testNamespace.empty());
        bool live=std::strcmp(record.status,"live")==0;
        due=now+(live?(1000+rate-1)/rate:record.connected?250:1000);
        if(!record.connected||sample.status==ReadStatus::Unsupported||sample.status==ReadStatus::Invalid||
            (record.stale&&now-openedAt>=5000&&(!record.ageMs||*record.ageMs>=5000))){
            memory.close();lock.close();tracker.reset();due=now+1000;
        }
        return record;
    }
};
}
int main(int argc,char**argv){try{
    std::string game="auto",testNamespace;unsigned rate=20,duration=0;bool once=false;
    for(int n=1;n<argc;++n){std::string option=argv[n];
        auto argument=[&](){if(n+1>=argc)throw std::runtime_error("missing option value");return std::string(argv[++n]);};
        if(option=="--game")game=argument();else if(option=="--rate")rate=positive(argument(),30);
        else if(option=="--duration-ms")duration=positive(argument(),60000);
        else if(option=="--once")once=true;
        else if(option=="--test-namespace"){testNamespace=argument();if(!valid_test_namespace(testNamespace))throw std::runtime_error("invalid test namespace");}
        else if(option=="--help"){std::cout<<"OpenGridXR-Telemetry --game iracing|lmu|auto --rate 1..30 [--once]\n";return 0;}
        else throw std::runtime_error("unknown option");
    }
    if(game!="auto"&&game!="iracing"&&game!="lmu")throw std::runtime_error("invalid game");
    stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stopEvent)throw std::runtime_error("cannot create shutdown event");
    SetConsoleCtrlHandler(console_stop,TRUE);
    std::vector<std::unique_ptr<Source>> sources;
    if(game!="lmu")sources.emplace_back(new Source(Game::IRacing));
    if(game!="iracing")sources.emplace_back(new Source(Game::Lmu));
    const auto start=GetTickCount64();bool stopping=false;
    while(!stopping){
        const auto now=GetTickCount64();
        if((duration&&now-start>=duration)||stdin_stop())break;
        std::uint64_t due=now+250;
        for(auto&source:sources){
            if(now>=source->due){
                auto record=source->poll(now,testNamespace,rate);
                const bool changed=source->previousStatus!=record.status;
                if(once||changed||std::strcmp(record.status,"live")==0||now-source->lastEmit>=1000){
                    if(!stdout_line(json_line(record))){stopping=true;break;}
                    source->lastEmit=now;source->previousStatus=record.status;
                }
            }
            due=std::min(due,source->due);
        }
        if(once||stopping)break;
        DWORD delay=DWORD(due>now?std::min<std::uint64_t>(250,due-now):1);
        if(WaitForSingleObject(stopEvent,delay)==WAIT_OBJECT_0)break;
    }
    SetConsoleCtrlHandler(console_stop,FALSE);CloseHandle(stopEvent);stopEvent=nullptr;
    return 0;
}catch(const std::exception&e){std::cerr<<"Telemetry helper: "<<e.what()<<'\n';return 2;}}
