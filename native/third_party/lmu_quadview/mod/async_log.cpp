#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "async_log.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <new>

namespace lmuqv {
bool AsyncLogQueue::enqueue(const char* bytes,std::size_t size){
    if(!enabled_)return false;
    if(!accepting_.load(std::memory_order_acquire)){stopped_.fetch_add(1,std::memory_order_relaxed);return false;}
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock()){note_contention();return false;}
    if(!accepting_.load(std::memory_order_acquire)){stopped_.fetch_add(1,std::memory_order_relaxed);return false;}
    if(count_==capacity){overflow_.fetch_add(1,std::memory_order_relaxed);return false;}
    if(!bytes)size=0;
    auto& entry=entries_[(begin_+count_)%capacity];entry.size=(std::min)(size,maximum_line_bytes);
    if(entry.size)std::memcpy(entry.bytes.data(),bytes,entry.size);
    if(size>maximum_line_bytes){
        constexpr char marker[]=" [truncated]\r\n";
        std::memcpy(entry.bytes.data()+maximum_line_bytes-(sizeof(marker)-1),marker,sizeof(marker)-1);
        truncated_.fetch_add(1,std::memory_order_relaxed);
    }
    entry.sequence=accepted_.load(std::memory_order_relaxed)+1;++count_;
    accepted_.store(entry.sequence,std::memory_order_release);return true;
}
void AsyncLogQueue::request_flush(){
    if(!enabled_)return;
    const auto mark=accepted_.load(std::memory_order_acquire);
    auto previous=flushThrough_.load(std::memory_order_relaxed);
    while(previous<mark&&!flushThrough_.compare_exchange_weak(previous,mark,std::memory_order_release,std::memory_order_relaxed)){}
    flushSerial_.fetch_add(1,std::memory_order_release);
}
void AsyncLogQueue::request_stop(){
    if(!enabled_)return;
    accepting_.store(false,std::memory_order_release);stopping_.store(true,std::memory_order_release);
}
AsyncLogDrain AsyncLogQueue::drain_once(const AsyncLogSink& sink){
    if(!enabled_)return {false,true};
    std::array<Entry,batch_size> batch;std::size_t size=0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto serial=flushSerial_.load(std::memory_order_acquire);
        if(serial!=pendingSerial_){pendingSerial_=serial;pendingThrough_=(std::max)(pendingThrough_,flushThrough_.load(std::memory_order_acquire));}
        size=(std::min)(count_,batch_size);
        for(std::size_t i=0;i<size;++i){
            const auto& source=entries_[begin_];batch[i].sequence=source.sequence;batch[i].size=source.size;
            if(source.size)std::memcpy(batch[i].bytes.data(),source.bytes.data(),source.size);
            begin_=(begin_+1)%capacity;--count_;
        }
    }
    bool progress=size!=0;
    for(std::size_t i=0;i<size;++i){
        const auto& entry=batch[i];
        if(sink.write&&sink.write(sink.context,entry.bytes.data(),entry.size))written_.fetch_add(1,std::memory_order_relaxed);
        else writeFailures_.fetch_add(1,std::memory_order_relaxed);
        processed_=entry.sequence;
    }
    const bool requested=pendingSerial_!=flushedSerial_&&processed_>=pendingThrough_;
    // Producers could have filled the queue while this batch's sink was busy.
    // Recheck after accepting is disabled; the earlier queue length is stale.
    bool done=false;
    if(stopping_.load(std::memory_order_acquire)){
        std::lock_guard<std::mutex> lock(mutex_);done=count_==0;
    }
    if(requested||(done&&(!everFlushed_||flushedThrough_!=processed_))){
        if(!sink.flush||!sink.flush(sink.context))flushFailures_.fetch_add(1,std::memory_order_relaxed);
        everFlushed_=true;flushedSerial_=pendingSerial_;flushedThrough_=processed_;progress=true;
    }
    return {progress,done};
}
AsyncLogStats AsyncLogQueue::stats()const{
    return {accepted_.load(),written_.load(),overflow_.load(),contention_.load(),stopped_.load(),
        truncated_.load(),writeFailures_.load(),flushFailures_.load()};
}
void AsyncLogQueue::restart(){
    std::lock_guard<std::mutex> lock(mutex_);
    begin_=count_=0;processed_=pendingThrough_=pendingSerial_=flushedSerial_=flushedThrough_=0;everFlushed_=false;
    accepted_=0;written_=0;overflow_=0;contention_=0;stopped_=0;truncated_=0;writeFailures_=0;flushFailures_=0;
    flushThrough_=0;flushSerial_=0;stopping_=false;accepting_=enabled_;
}

namespace {
std::atomic<std::uint64_t> unavailable{0};
struct Service;
DWORD WINAPI writer_entry(void*);
struct Service {
    AsyncLogQueue queue;
    SRWLOCK lifecycle=SRWLOCK_INIT;
    HANDLE wake=CreateEventW(nullptr,FALSE,FALSE,nullptr),thread=nullptr;
    std::atomic<bool> running{false};
    HMODULE module=nullptr;
    // Only the writer touches these fields, including creation and CloseHandle.
    HANDLE file=INVALID_HANDLE_VALUE;bool attempted=false;
    bool ensure_running(){
        if(running.load(std::memory_order_acquire))return true;
        if(!TryAcquireSRWLockExclusive(&lifecycle))return false;
        bool okay=running.load(std::memory_order_relaxed);
        if(!okay&&wake){
            // This increments the DLL reference count, unlike UNCHANGED_REFCOUNT.
            // If instance destruction never occurs, the bounded service lives
            // until process exit; no destructor waits under the loader lock.
            HMODULE retained=nullptr;
            if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&writer_entry),&retained)){
                queue.restart();module=retained;file=INVALID_HANDLE_VALUE;attempted=false;
                thread=CreateThread(nullptr,0,writer_entry,this,0,nullptr);
                if(thread){running.store(true,std::memory_order_release);okay=true;}
                else {module=nullptr;FreeLibrary(retained);}
            }
        }
        ReleaseSRWLockExclusive(&lifecycle);return okay;
    }
    HANDLE open_file(){
        if(attempted)return file;
        attempted=true;
        try{
            wchar_t path[32768]{};const auto n=GetEnvironmentVariableW(L"LMU_QV_LOG",path,32768);
            std::filesystem::path target;
            if(n&&n<32768&&std::filesystem::path(path).is_absolute())target=path;
            else{
                if(!GetModuleFileNameW(module,path,32768))return INVALID_HANDLE_VALUE;
                auto directory=std::filesystem::path(path).parent_path();
                if(directory.filename()==L"build")directory=directory.parent_path();
                target=directory/L"logs"/L"xr-layer.log";
            }
            std::error_code error;std::filesystem::create_directories(target.parent_path(),error);
            file=CreateFileW(target.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        }catch(...){file=INVALID_HANDLE_VALUE;}
        return file;
    }
    static bool write(void* pointer,const char* bytes,std::size_t count){
        auto& service=*static_cast<Service*>(pointer);const auto target=service.open_file();
        if(target==INVALID_HANDLE_VALUE)return false;
        while(count){DWORD written=0;if(!WriteFile(target,bytes,static_cast<DWORD>(count),&written,nullptr)||!written)return false;bytes+=written;count-=written;}
        return true;
    }
    static bool flush(void* pointer){
        auto& service=*static_cast<Service*>(pointer);const auto target=service.open_file();
        return target!=INVALID_HANDLE_VALUE&&FlushFileBuffers(target)!=FALSE;
    }
    void stop(){
        AcquireSRWLockExclusive(&lifecycle);
        if(running.load(std::memory_order_relaxed)){
            queue.request_stop();SetEvent(wake);
            WaitForSingleObject(thread,INFINITE);CloseHandle(thread);thread=nullptr;
            running.store(false,std::memory_order_release);
        }
        ReleaseSRWLockExclusive(&lifecycle);
    }
};
// Intentionally process-lifetime storage: neither std::mutex destruction nor a
// std::thread join is registered with CRT/DLL detach. Only explicit stop waits.
std::atomic<Service*> published{nullptr};
Service* service(){
    static Service* const selected=[]()noexcept->Service*{try{return new(std::nothrow) Service;}catch(...){return nullptr;}}();
    published.store(selected,std::memory_order_release);return selected;
}
DWORD WINAPI writer_entry(void* pointer){
    auto& service=*static_cast<Service*>(pointer);
    const auto retained=service.module;const AsyncLogSink sink{&service,Service::write,Service::flush};
    bool done=false;
    while(!done){
        WaitForSingleObject(service.wake,1000);
        for(;;){const auto result=service.queue.drain_once(sink);if(result.stopped){done=true;break;}if(!result.progress)break;}
    }
    const auto stats=service.queue.stats();char summary[512]{};
    const int size=std::snprintf(summary,sizeof(summary),
        "async_log stopped accepted=%llu written=%llu dropped_overflow=%llu dropped_contention=%llu dropped_stopped=%llu dropped_unavailable=%llu truncated=%llu write_failures=%llu flush_failures=%llu\r\n",
        static_cast<unsigned long long>(stats.accepted),static_cast<unsigned long long>(stats.written),
        static_cast<unsigned long long>(stats.droppedOverflow),static_cast<unsigned long long>(stats.droppedContention),
        static_cast<unsigned long long>(stats.droppedStopped),static_cast<unsigned long long>(unavailable.load()),static_cast<unsigned long long>(stats.truncated),
        static_cast<unsigned long long>(stats.writeFailures),static_cast<unsigned long long>(stats.flushFailures));
    if(size>0)Service::write(&service,summary,(std::min)(static_cast<std::size_t>(size),sizeof(summary)-1));
    Service::flush(&service);
    if(service.file!=INVALID_HANDLE_VALUE){CloseHandle(service.file);service.file=INVALID_HANDLE_VALUE;}
    // The release and thread exit are atomic with respect to module unloading.
    // Never return into this DLL after releasing its final counted reference.
    FreeLibraryAndExitThread(retained,0);
}
}
void async_log_enqueue(const char* bytes,std::size_t size){
    try{
        if(auto* selected=service())if(selected->ensure_running()){
            if(selected->queue.enqueue(bytes,size))SetEvent(selected->wake);
            return;
        }
    }catch(...){} // Diagnostics must never throw into native rendering.
    unavailable.fetch_add(1,std::memory_order_relaxed);
}
void async_log_request_flush(){
    if(auto* selected=published.load(std::memory_order_acquire))if(selected->running.load(std::memory_order_acquire)){
        selected->queue.request_flush();SetEvent(selected->wake);
    }
}
void async_log_stop(){if(auto* selected=published.load(std::memory_order_acquire))selected->stop();}
AsyncLogStats async_log_stats(){
    AsyncLogStats stats;
    if(auto* selected=published.load(std::memory_order_acquire))stats=selected->queue.stats();
    stats.droppedUnavailable=unavailable.load();return stats;
}
}
