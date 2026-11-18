#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../async_log.hpp"
#include <windows.h>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace lmuqv;
static void require(bool okay,const char* why){if(!okay){std::cerr<<why<<'\n';std::exit(1);}}
struct Sink {
    std::vector<std::string> events;
    std::mutex mutex;std::condition_variable condition;
    bool block=false,entered=false,released=false,failWrite=false,failFlush=false;
    static bool write(void* pointer,const char* bytes,std::size_t count){
        auto& s=*static_cast<Sink*>(pointer);
        {std::unique_lock<std::mutex> lock(s.mutex);if(s.block){s.entered=true;s.condition.notify_all();s.condition.wait(lock,[&]{return s.released;});}}
        s.events.emplace_back(bytes,count);return !s.failWrite;
    }
    static bool flush(void* pointer){auto& s=*static_cast<Sink*>(pointer);s.events.emplace_back("FLUSH");return !s.failFlush;}
    AsyncLogSink callbacks(){return {this,write,flush};}
};
static void drain(AsyncLogQueue& q,Sink& s){
    q.request_stop();for(unsigned n=0;n<AsyncLogQueue::capacity+4;++n)if(q.drain_once(s.callbacks()).stopped)return;
    require(false,"stop did not drain");
}
int main(int argc,char** argv){
    auto q=std::make_unique<AsyncLogQueue>();Sink sink;
    for(unsigned n=0;n<32;++n){const auto text=std::to_string(n);require(q->enqueue(text.data(),text.size()),"ordered enqueue failed");if(n==15)q->request_flush();}
    require(sink.events.empty(),"producer performed I/O");
    auto result=q->drain_once(sink.callbacks());require(result.progress&&!result.stopped,"first batch failed");
    require(sink.events.size()==17&&sink.events.back()=="FLUSH","flush did not follow accepted-sequence mark");
    for(unsigned n=0;n<16;++n)require(sink.events[n]==std::to_string(n),"first batch out of order");
    drain(*q,sink);require(sink.events.back()=="FLUSH","stop did not flush final data");
    for(unsigned n=16;n<32;++n)require(sink.events[n+1]==std::to_string(n),"second batch out of order");
    require(q->stats().accepted==32&&q->stats().written==32,"write counters incorrect");
    require(!q->enqueue("late",4)&&q->stats().droppedStopped==1,"enqueue after stop accepted");
    q->restart();sink.events.clear();
    for(unsigned n=0;n<AsyncLogQueue::capacity;++n)require(q->enqueue("x",1),"queue filled early");
    require(!q->enqueue("overflow",8),"overflow accepted");
    require(q->stats().droppedOverflow==1,"overflow not counted");drain(*q,sink);
    require(q->stats().written==AsyncLogQueue::capacity,"overflow damaged queued lines");
    q->restart();sink.events.clear();std::string tooLong(AsyncLogQueue::maximum_line_bytes+32,'z');
    require(q->enqueue(tooLong.data(),tooLong.size()),"long message dropped");drain(*q,sink);
    require(q->stats().truncated==1&&sink.events[0].find("[truncated]")!=std::string::npos,"truncation not marked");
    q->restart();sink.events.clear();sink.block=true;sink.released=false;
    require(q->enqueue("blocked",7),"blocked setup enqueue failed");
    auto worker=std::async(std::launch::async,[&]{return q->drain_once(sink.callbacks());});
    {std::unique_lock<std::mutex> lock(sink.mutex);require(sink.condition.wait_for(lock,std::chrono::seconds(2),[&]{return sink.entered;}),"writer did not enter injected stall");}
    auto producer=std::async(std::launch::async,[&]{for(unsigned n=0;n<AsyncLogQueue::capacity*2;++n)q->enqueue("while stalled",13);q->request_flush();return true;});
    const bool producerCompleted=producer.wait_for(std::chrono::milliseconds(200))==std::future_status::ready;
    q->request_stop(); // Data accepted while the sink was stalled must still drain.
    {std::lock_guard<std::mutex> lock(sink.mutex);sink.released=true;sink.condition.notify_all();}
    require(producerCompleted&&producer.get(),"producer or flush waited for stalled disk writer");
    require(!worker.get().stopped,"stop lost data accepted during a stalled write");
    require(q->stats().droppedOverflow>=AsyncLogQueue::capacity,"stalled writer overflow missing");sink.block=false;drain(*q,sink);
    q->restart();sink.events.clear();sink.failWrite=sink.failFlush=true;
    require(q->enqueue("failure",7),"failure setup enqueue");drain(*q,sink);
    require(q->stats().writeFailures==1&&q->stats().flushFailures==1,"I/O failure not counted");
    auto disabled=std::make_unique<AsyncLogQueue>(false);Sink noIo;
    require(!disabled->enqueue("off",3),"disabled queue accepted");disabled->request_flush();disabled->request_stop();
    require(disabled->drain_once(noIo.callbacks()).stopped&&noIo.events.empty(),"diagnostics-off performed I/O");
    require(disabled->stats().accepted==0&&disabled->stats().droppedOverflow==0,"disabled queue recorded activity");
    wchar_t temporary[MAX_PATH]{};require(GetTempPathW(MAX_PATH,temporary)!=0,"temporary path unavailable");
    const auto path=std::filesystem::path(temporary)/(L"lmu-async-log-"+std::to_wstring(GetCurrentProcessId())+L".txt");
    DeleteFileW(path.c_str());require(SetEnvironmentVariableW(L"LMU_QV_LOG",path.c_str())!=FALSE,"test log setting failed");
    async_log_enqueue("first instance\r\n",16);async_log_request_flush();async_log_stop();
    require(async_log_stats().written==1,"service did not drain first instance");
    async_log_enqueue("second instance\r\n",17);async_log_stop();
    require(async_log_stats().written==1,"service did not restart for next instance");
    std::ifstream file(path,std::ios::binary);const std::string contents((std::istreambuf_iterator<char>(file)),{});file.close();
    require(contents.find("first instance\r\n")<contents.find("second instance\r\n"),"service restart changed order");
    require(contents.find("async_log stopped accepted=1 written=1")!=std::string::npos,"service dropped-count summary missing");
    DeleteFileW(path.c_str());
    if(argc>1&&std::string(argv[1])=="--dll-lifetime"){
        wchar_t executable[32768]{};require(GetModuleFileNameW(nullptr,executable,32768)!=0,"test executable path missing");
        const auto fixturePath=std::filesystem::path(executable).parent_path()/L"async_log_lifetime_fixture.dll";
        const auto original=LoadLibraryW(fixturePath.c_str());require(original!=nullptr,"lifetime fixture load failed");
        using Call=void(*)();
        const auto start=reinterpret_cast<Call>(GetProcAddress(original,"fixture_start_log"));
        const auto stop=reinterpret_cast<Call>(GetProcAddress(original,"fixture_stop_log"));
        require(start&&stop,"lifetime fixture exports missing");start();
        require(FreeLibrary(original)!=FALSE,"initial loader reference release failed");
        HMODULE observed=nullptr;
        require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(start),&observed)!=FALSE,"early negotiation worker failed to retain its module");
        // As with the OpenXR loader, hold a caller reference while executing a
        // DLL export. The worker may release its own reference during this call.
        HMODULE callerReference=nullptr;
        require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(stop),&callerReference)!=FALSE,"caller module retention failed");
        stop();require(FreeLibrary(callerReference)!=FALSE,"caller module release failed");
        observed=nullptr;
        require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(start),&observed)==FALSE,"stopped worker leaked a DLL reference");
        require(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES,"fixture stop did not drain its log");DeleteFileW(path.c_str());
    }
    std::cout<<"async_log_test passed\n";
}
