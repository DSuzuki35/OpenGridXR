#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include "gpu_timing.hpp"
#include "diagnostics.hpp"

namespace lmuqv {
enum class ProfileSlot : unsigned {
    outer0,outer1,focus_hook0,focus_hook1,focus_scene0,focus_scene1,
    focus_prepare0,focus_prepare1,focus_restore0,focus_restore1,
    outer_copy0,outer_copy1,outer_wait0,outer_wait1,native_prepare0,native_prepare1,
    native_restore0,native_restore1,sort_outer0,sort_outer1,sort_focus0,sort_focus1,
    ui,provider_submit,frame_interval,non_scene_ui,non_scene_provider_submit,
    non_scene_outer_wait0,non_scene_outer_wait1,count
};
inline ProfileSlot profile_eye(ProfileSlot left,unsigned eye){return ProfileSlot(unsigned(left)+eye);}
struct NativeProfileClock {
    static uint64_t now(){LARGE_INTEGER n{};QueryPerformanceCounter(&n);return uint64_t(n.QuadPart);}
    static uint64_t frequency(){LARGE_INTEGER n{};QueryPerformanceFrequency(&n);return uint64_t(n.QuadPart);}
    static DWORD thread(){return GetCurrentThreadId();}
};
using ProfileGpuTiming=BasicGpuTiming<gpu_detail::NativeTimingBackend,512>;

// Collection belongs to the first verified eye-render thread. Other threads
// bypass it before touching mutable state. Short mutex sections protect state
// snapshots at teardown; no lock is held across a game/runtime call. No sample
// allocation or GPU waits occur. CPU numbers are elapsed wall time,
// including any driver/runtime waits. Nested scopes are inclusive.
template<class Clock=NativeProfileClock,class Gpu=ProfileGpuTiming,unsigned Capacity=65536>
class BasicRenderProfile {
public:
    using Context=typename Gpu::Context;
    using Log=void(*)(const char*);
    struct Token {uint64_t begin=0,generation=0;DWORD thread=0;ProfileSlot slot=ProfileSlot::count;uint64_t frame=0;};
    void configure(bool enable,Log callback,unsigned durationSeconds=120,void(*flush)()=nullptr){
        enabled_.store(false);std::lock_guard<std::mutex> lock(mutex_);
        gpu_.reset();active_=false;owner_.store(0);
        log_=callback;keyWasDown_=false;generation_=0;lastFrame_=0;
        xrBoundaries_=false;xrOpen_=false;sceneOpen_=false;intervalValid_=false;frameSerial_=0;
        durationSeconds_=durationSeconds>=30&&durationSeconds<=300?durationSeconds:120;flush_=flush;
        frequency_=enable&&diagnostics_enabled()?Clock::frequency():0;enabled_.store(frequency_!=0);
        if(enabled()){
            char line[256]{};std::snprintf(line,sizeof(line),"render_profile armed hotkey=Ctrl+Alt+F9 duration_s=%u checkpoint_s=15 save_on_session_end=1; press on track",durationSeconds_);emit(line);
        }
    }
    bool enabled()const{return enabled_.load(std::memory_order_relaxed);}
    bool collecting()const{if(!owns_thread())return false;std::lock_guard<std::mutex> lock(mutex_);return active_;}
    // At the left-eye scene boundary, with no outstanding CPU/GPU scope.
    // Key-down is injected to allow deterministic tests without global hotkeys.
    void frame(Context context,bool keyDown){
        if(!enabled())return;
        DWORD empty=0;owner_.compare_exchange_strong(empty,Clock::thread());
        if(!owns_thread())return;
        std::lock_guard<std::mutex> lock(mutex_);if(!enabled())return;
        const bool pressed=keyDown&&!keyWasDown_;keyWasDown_=keyDown;
        const auto now=Clock::now();
        if(active_){
            gpu_.poll(context);
            if(intervalValid_)record(ProfileSlot::frame_interval,now-lastFrame_);
            if(!xrBoundaries_)flush_frame();
            if(pressed||now-started_>=durationSeconds_*frequency_){finish_locked(pressed?"hotkey":"duration",true);return;}
            if(now-lastCheckpoint_>=15*frequency_){checkpoint(now);lastCheckpoint_=now;}
        }else if(pressed){
            for(auto& bucket:buckets_){bucket.count=0;bucket.dropped=0;bucket.calls=0;bucket.pendingTicks=0;bucket.pendingCalls=0;bucket.sumTicks=0;}
            ++generation_;started_=lastCheckpoint_=nextGpu_=now;frames_=0;xrFrames_=nonSceneFrames_=0;lastFrame_=0;active_=true;
            gpu_.configure(true,0xf,nullptr,nullptr);
            char line[384]{};std::snprintf(line,sizeof(line),
                "render_profile start capture=%llu thread=%lu cpu=elapsed_wall_ms inclusive_scopes=1 duration_s=%u gpu_sample_period_ms=%.3f capacity=%u",
                static_cast<unsigned long long>(generation_),static_cast<unsigned long>(owner_.load()),durationSeconds_,durationSeconds_*1000.0/480,Capacity);emit(line);
        }
        if(!active_)return;
        lastFrame_=now;intervalValid_=true;sceneOpen_=true;++frames_;
        if(!xrBoundaries_)++frameSerial_;
        sampleGpu_=now>=nextGpu_;
        if(sampleGpu_)nextGpu_=now+(std::max)(uint64_t(1),durationSeconds_*frequency_/480);
    }
    Token begin(ProfileSlot slot){
        if(!owns_thread()||unsigned(slot)>=SlotCount)return {};
        std::lock_guard<std::mutex> lock(mutex_);if(!enabled()||!active_)return {};
        if(xrBoundaries_&&!sceneOpen_){
            if(slot==ProfileSlot::ui)slot=ProfileSlot::non_scene_ui;
            else if(slot==ProfileSlot::provider_submit)slot=ProfileSlot::non_scene_provider_submit;
            else if(slot==ProfileSlot::outer_wait0)slot=ProfileSlot::non_scene_outer_wait0;
            else if(slot==ProfileSlot::outer_wait1)slot=ProfileSlot::non_scene_outer_wait1;
        }
        return {Clock::now(),generation_,Clock::thread(),slot,frameSerial_};
    }
    void end(Token& token){
        if(token.slot==ProfileSlot::count)return;
        std::lock_guard<std::mutex> lock(mutex_);
        if(enabled()&&active_&&token.thread==Clock::thread()&&token.generation==generation_&&token.frame==frameSerial_){
            const auto now=Clock::now();if(now>=token.begin)record(token.slot,now-token.begin);
        }
        token.slot=ProfileSlot::count;
    }
    void xr_frame_begin(){
        if(!owns_thread())return;
        std::lock_guard<std::mutex> lock(mutex_);if(!enabled())return;
        // Also close an abandoned frame; never merge it into the next one.
        if(active_&&xrOpen_)flush_frame();
        xrBoundaries_=true;xrOpen_=true;sceneOpen_=false;++frameSerial_;
    }
    void xr_frame_end(Context context){
        if(!owns_thread())return;
        std::lock_guard<std::mutex> lock(mutex_);if(!enabled())return;
        if(active_){
            gpu_.poll(context);flush_frame();++xrFrames_;
            if(!sceneOpen_){++nonSceneFrames_;intervalValid_=false;}
            const auto now=Clock::now();
            if(now-started_>=durationSeconds_*frequency_)finish_locked("duration",true);
            else if(now-lastCheckpoint_>=15*frequency_){checkpoint(now);lastCheckpoint_=now;}
        }
        xrBoundaries_=true;xrOpen_=false;sceneOpen_=false;
    }
    bool gpu_start(Context context,unsigned slot){
        if(!owns_thread())return false;
        std::lock_guard<std::mutex> lock(mutex_);return enabled()&&active_&&sampleGpu_&&gpu_.start(context,slot);
    }
    void gpu_stop(Context context){
        if(owner_.load()!=Clock::thread())return;
        std::lock_guard<std::mutex> lock(mutex_);gpu_.stop(context);
        if(!enabled())gpu_.reset();
    }
    // Called at an unmeasured boundary or session shutdown on the render thread.
    void finish(const char* reason){
        std::lock_guard<std::mutex> lock(mutex_);finish_locked(reason,owner_.load()==Clock::thread());
    }
    void shutdown(){
        enabled_.store(false);std::lock_guard<std::mutex> lock(mutex_);
        const bool owning=owner_.load()==0||owner_.load()==Clock::thread();
        finish_locked("session_end",owning);
        if(owning){gpu_.reset();owner_.store(0);}
        if(flush_)flush_();
    }
    uint64_t samples(ProfileSlot slot)const{std::lock_guard<std::mutex> lock(mutex_);return buckets_[unsigned(slot)].count;}
private:
    void finish_locked(const char* reason,bool owning){
        if(!active_)return;
        active_=false;flush_frame();if(owning)gpu_.finish();
        char line[768]{};
        std::snprintf(line,sizeof(line),"render_profile summary capture=%llu reason=%s elapsed_s=%.3f frames=%llu xr_frames=%llu non_scene_frames=%llu; CPU wall includes waits; nested scopes overlap; GPU slots exclude copy/UI/provider",
            static_cast<unsigned long long>(generation_),reason,double(Clock::now()-started_)/frequency_,static_cast<unsigned long long>(frames_),static_cast<unsigned long long>(xrFrames_),static_cast<unsigned long long>(nonSceneFrames_));emit(line);
        for(unsigned slot=0;slot<SlotCount;++slot){
            auto& b=buckets_[slot];if(!b.count)continue;
            std::sort(b.ticks.begin(),b.ticks.begin()+b.count);
            double sum=0;for(unsigned i=0;i<b.count;++i)sum+=double(b.ticks[i]);
            auto ms=[&](uint64_t ticks){return double(ticks)*1000.0/frequency_;};
            auto percentile=[&](unsigned p){return ms(b.ticks[(b.count*p+99)/100-1]);};
            std::snprintf(line,sizeof(line),"render_profile cpu slot=%s samples=%u calls=%llu dropped=%u unit=ms_per_frame_with_scope mean_ms=%.6f p50_ms=%.6f p95_ms=%.6f p99_ms=%.6f max_ms=%.6f",
                names_[slot],b.count,static_cast<unsigned long long>(b.calls),b.dropped,sum*1000.0/frequency_/b.count,percentile(50),percentile(95),percentile(99),ms(b.ticks[b.count-1]));emit(line);
        }
        constexpr const char* gpuNames[]={"outer0","outer1","focus0","focus1"};
        for(unsigned slot=0;slot<4;++slot){
            const auto gpuStats=owning?gpu_.stats(slot):gpu_.snapshot(slot);
            std::snprintf(line,sizeof(line),"render_profile gpu slot=%s samples=%u mean_ms=%.6f p95_ms=%.6f p99_ms=%.6f max_ms=%.6f issued=%llu disjoint=%llu dropped=%llu failures=%llu pending_at_save=%llu faulted=%u",
                gpuNames[slot],gpuStats.samples,gpuStats.mean_ms,gpuStats.p95_ms,gpuStats.p99_ms,gpuStats.max_ms,
                static_cast<unsigned long long>(gpuStats.issued),static_cast<unsigned long long>(gpuStats.disjoint),static_cast<unsigned long long>(gpuStats.dropped),static_cast<unsigned long long>(gpuStats.failures),static_cast<unsigned long long>(gpuStats.pending),unsigned(gpu_.faulted()));emit(line);
        }
        if(!owning)emit("render_profile partial saved on teardown thread; completed GPU results saved without context calls; in-flight CPU scopes omitted");
        emit("render_profile complete; repeat Ctrl+Alt+F9 for another capture");if(owning)gpu_.reset();
        if(flush_)flush_();
    }
    static constexpr unsigned SlotCount=unsigned(ProfileSlot::count);
    struct Bucket {std::array<uint64_t,Capacity> ticks{};unsigned count=0,dropped=0,pendingCalls=0;uint64_t pendingTicks=0,calls=0;double sumTicks=0;};
    std::array<Bucket,SlotCount> buckets_{};
    static constexpr const char* names_[SlotCount]={
        "outer0","outer1","focus_hook0","focus_hook1","focus_scene0","focus_scene1",
        "focus_prepare0","focus_prepare1","focus_restore0","focus_restore1",
        "outer_copy0","outer_copy1","outer_wait0","outer_wait1","native_prepare0","native_prepare1",
        "native_restore0","native_restore1","sort_outer0","sort_outer1","sort_focus0","sort_focus1",
        "ui","provider_submit","frame_interval","non_scene_ui","non_scene_provider_submit",
        "non_scene_outer_wait0","non_scene_outer_wait1"};
    std::atomic<bool> enabled_{false};std::atomic<DWORD> owner_{0};
    mutable std::mutex mutex_;
    bool active_=false,keyWasDown_=false,sampleGpu_=false;
    bool xrBoundaries_=false,xrOpen_=false,sceneOpen_=false,intervalValid_=false;
    uint64_t frameSerial_=0,xrFrames_=0,nonSceneFrames_=0;
    uint64_t frequency_=0,started_=0,frames_=0,generation_=0,lastFrame_=0,lastCheckpoint_=0,nextGpu_=0;
    unsigned durationSeconds_=120;
    Log log_=nullptr;void(*flush_)()=nullptr;Gpu gpu_;
    bool owns_thread()const{return enabled()&&owner_.load(std::memory_order_relaxed)==Clock::thread();}
    void emit(const char* line){if(log_)log_(line);}
    void record(ProfileSlot slot,uint64_t ticks){
        auto& b=buckets_[unsigned(slot)];
        b.pendingTicks+=ticks;++b.pendingCalls;
    }
    void flush_frame(){
        for(auto& b:buckets_)if(b.pendingCalls){
            if(b.count<Capacity)b.ticks[b.count++]=b.pendingTicks;else ++b.dropped;
            b.sumTicks+=double(b.pendingTicks);
            b.calls+=b.pendingCalls;b.pendingTicks=0;b.pendingCalls=0;
        }
    }
    void checkpoint(uint64_t now){
        auto mean=[&](ProfileSlot slot){const auto& b=buckets_[unsigned(slot)];return b.count+b.dropped?b.sumTicks*1000.0/frequency_/(b.count+b.dropped):0;};
        auto pair=[&](ProfileSlot slot){return mean(slot)+mean(profile_eye(slot,1));};
        char line[1536]{};
        std::snprintf(line,sizeof(line),"render_profile checkpoint capture=%llu elapsed_s=%.3f frames=%llu cpu_wall_mean_ms outer_both=%.6f focus_hook_both=%.6f focus_scene_both=%.6f focus_prepare_both=%.6f focus_restore_both=%.6f outer_copy_both=%.6f outer_wait_both=%.6f native_prepare_both=%.6f native_restore_both=%.6f sort_outer_both=%.6f sort_focus_both=%.6f ui=%.6f provider_submit=%.6f frame_interval=%.6f; cumulative completed scopes; final percentiles saved at capture/session end",
            static_cast<unsigned long long>(generation_),double(now-started_)/frequency_,static_cast<unsigned long long>(frames_),
            pair(ProfileSlot::outer0),pair(ProfileSlot::focus_hook0),pair(ProfileSlot::focus_scene0),pair(ProfileSlot::focus_prepare0),pair(ProfileSlot::focus_restore0),pair(ProfileSlot::outer_copy0),pair(ProfileSlot::outer_wait0),pair(ProfileSlot::native_prepare0),pair(ProfileSlot::native_restore0),pair(ProfileSlot::sort_outer0),pair(ProfileSlot::sort_focus0),mean(ProfileSlot::ui),mean(ProfileSlot::provider_submit),mean(ProfileSlot::frame_interval));emit(line);
    }
};
using RenderProfile=BasicRenderProfile<>;
inline RenderProfile renderProfile;
class ProfileScope {
public:
    explicit ProfileScope(ProfileSlot slot,bool allow=true):token_(allow?renderProfile.begin(slot):RenderProfile::Token{}){}
    ~ProfileScope(){end();}
    void end(){renderProfile.end(token_);}
    ProfileScope(const ProfileScope&)=delete;
    ProfileScope& operator=(const ProfileScope&)=delete;
private:
    RenderProfile::Token token_;
};
}
