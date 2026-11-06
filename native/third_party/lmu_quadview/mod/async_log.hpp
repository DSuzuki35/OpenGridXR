#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace lmuqv {
struct AsyncLogSink {
    void* context=nullptr;
    bool (*write)(void*,const char*,std::size_t)=nullptr;
    bool (*flush)(void*)=nullptr;
};
struct AsyncLogStats {
    std::uint64_t accepted=0,written=0,droppedOverflow=0,droppedContention=0,droppedStopped=0;
    std::uint64_t truncated=0,writeFailures=0,flushFailures=0;
    std::uint64_t droppedUnavailable=0;
};
struct AsyncLogDrain {bool progress=false,stopped=false;};
// Fixed storage. Producers never wait for its mutex and never invoke the sink.
// One consumer owns drain_once. The sink runs after the queue lock is released.
class AsyncLogQueue {
public:
    static constexpr std::size_t capacity=512,maximum_line_bytes=2304,batch_size=16;
private:
    struct Entry {std::uint64_t sequence=0;std::size_t size=0;std::array<char,maximum_line_bytes> bytes;};
    std::array<Entry,capacity> entries_;
    std::mutex mutex_;
    std::size_t begin_=0,count_=0;
    const bool enabled_;
    std::atomic<bool> accepting_,stopping_{false};
    std::atomic<std::uint64_t> accepted_{0},written_{0},overflow_{0},contention_{0},stopped_{0};
    std::atomic<std::uint64_t> truncated_{0},writeFailures_{0},flushFailures_{0};
    std::atomic<std::uint64_t> flushThrough_{0},flushSerial_{0};
    // Consumer-only state; restart requires the previous consumer to be joined.
    std::uint64_t processed_=0,pendingThrough_=0,pendingSerial_=0,flushedSerial_=0,flushedThrough_=0;
    bool everFlushed_=false;
public:
    explicit AsyncLogQueue(bool enabled=true):enabled_(enabled),accepting_(enabled){}
    AsyncLogQueue(const AsyncLogQueue&)=delete;
    AsyncLogQueue& operator=(const AsyncLogQueue&)=delete;
    bool enqueue(const char*,std::size_t);
    void note_contention(){if(enabled_)contention_.fetch_add(1,std::memory_order_relaxed);}
    void request_flush();
    void request_stop();
    AsyncLogDrain drain_once(const AsyncLogSink&);
    AsyncLogStats stats()const;
    // Lifecycle owner only, after the old writer has exited; no I/O here.
    void restart();
};

// Heap-owned process service, explicitly stopped outside the XR state lock.
// No static worker destructor or DllMain join. Each Win32 worker holds a counted
// module reference until FreeLibraryAndExitThread. A later instance can restart.
void async_log_enqueue(const char*,std::size_t);
void async_log_request_flush();
void async_log_stop();
AsyncLogStats async_log_stats();
}
