#pragma once
#include <d3d11.h>
#include <array>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lmuqv {
using GpuTimingLog=void(*)(void* user,const char* message);
struct GpuTimingStats {
    unsigned samples=0;
    uint64_t issued=0,disjoint=0,dropped=0,failures=0,pending=0;
    bool done=false;
    double mean_ms=0,p50_ms=0,p90_ms=0,p95_ms=0,p99_ms=0,min_ms=0,max_ms=0;
};
namespace gpu_detail {
struct NativeTimingBackend {
    using Context=ID3D11DeviceContext*;
    using Device=ID3D11Device*;
    using Query=ID3D11Query*;
    static bool immediate(Context c){return c&&c->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE;}
    static Device retain_device(Context c){Device d=nullptr;c->GetDevice(&d);return d;}
    static void release_device(Device d){d->Release();}
    static void retain_context(Context c){c->AddRef();}
    static void release_context(Context c){c->Release();}
    static HRESULT create(Device d,D3D11_QUERY type,Query& q){D3D11_QUERY_DESC desc{};desc.Query=type;return d->CreateQuery(&desc,&q);}
    static void release(Query q){q->Release();}
    static void begin(Context c,Query q){c->Begin(q);}
    static void end(Context c,Query q){c->End(q);}
    static HRESULT data(Context c,Query q,void* result,UINT bytes,UINT flags){return c->GetData(q,result,bytes,flags);}
};
}
// All methods, including reset/destruction, belong to the owning render thread.
// Callback must not throw or re-enter this object. No GPU waits or Flush occur here.
template<class Backend,unsigned TargetSamples=60> class BasicGpuTiming {
    static_assert(TargetSamples>0,"GPU timing sample budget must be positive");
public:
    using Context=typename Backend::Context;
    BasicGpuTiming()=default;
    explicit BasicGpuTiming(bool enable,GpuTimingLog callback=nullptr,void* user=nullptr,uint32_t mask=0xf){configure(enable,mask,callback,user);}
    ~BasicGpuTiming(){reset();}
    BasicGpuTiming(const BasicGpuTiming&)=delete;
    BasicGpuTiming& operator=(const BasicGpuTiming&)=delete;
    void configure(bool enable,uint32_t mask=0xf,GpuTimingLog callback=nullptr,void* user=nullptr){
        reset();callback_=callback;user_=user;mask_=mask;enabled_=enable;
        if(mask&~0xfu){enabled_=false;faulted_=true;emit("gpu_timing unavailable reason=invalid_slot_mask");}
        else if(enable&&!mask)done_=true;
    }
    bool start(Context context,unsigned slot){
        if(!enabled_||done_||faulted_||slot>=4||!(mask_&(1u<<slot))||stats_[slot].done)return false;
        if(active_>=0){
            entries_[active_].invalid=true;++stats_[slot].dropped;++stats_[slot].failures;return false;
        }
        if(!context||!Backend::immediate(context)){++stats_[slot].failures;return false;}
        if(!bind(context))return false;
        auto& stats=stats_[slot];
        if(stats.samples+outstanding_[slot]>=TargetSamples||stats.issued>=MaxAttempts)return false;
        int free=-1;
        for(unsigned i=0;i<RingSize;++i)if(entries_[i].state==State::free){free=int(i);break;}
        if(free<0){++stats.dropped;return false;}
        auto& entry=entries_[free];
        if(!ensure_queries(entry)){
            ++stats.failures;fault("create_query_failed");return false;
        }
        entry.slot=slot;entry.polls=0;entry.invalid=false;entry.state=State::active;
        Backend::begin(context_,entry.query[0]);Backend::end(context_,entry.query[1]);
        active_=free;++stats.issued;++outstanding_[slot];return true;
    }
    bool stop(Context context){
        if(!enabled_||done_||faulted_||active_<0)return false;
        auto& entry=entries_[active_];
        if(!context||!Backend::immediate(context)){
            end_active();discard(entry,true,true);active_=-1;return false;
        }
        if(!bind(context))return false;
        end_active();active_=-1;
        if(entry.invalid){discard(entry,true,true);return false;}
        entry.state=State::pending;return true;
    }
    void poll(Context context){
        if(!enabled_||done_||faulted_||!context_)return;
        if(!context||!Backend::immediate(context)){fault("invalid_poll_context");return;}
        if(!bind(context))return;
        for(auto& entry:entries_){
            if(entry.state!=State::pending)continue;
            ++entry.polls;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
            HRESULT result=Backend::data(context_,entry.query[0],&disjoint,sizeof(disjoint),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(result==S_FALSE){pending(entry);continue;}
            if(result!=S_OK){discard(entry,true,true);release_queries(entry);continue;}
            if(disjoint.Disjoint){++stats_[entry.slot].disjoint;discard(entry,false,false);continue;}
            if(!disjoint.Frequency){discard(entry,true,true);continue;}
            uint64_t begin=0,end=0;
            result=Backend::data(context_,entry.query[1],&begin,sizeof(begin),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(result==S_FALSE){pending(entry);continue;}
            if(result!=S_OK){discard(entry,true,true);release_queries(entry);continue;}
            result=Backend::data(context_,entry.query[2],&end,sizeof(end),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(result==S_FALSE){pending(entry);continue;}
            if(result!=S_OK){discard(entry,true,true);release_queries(entry);continue;}
            double ms=double(end-begin)*1000.0/double(disjoint.Frequency);
            if(end<begin||!std::isfinite(ms)||ms<0){discard(entry,true,true);continue;}
            auto& stats=stats_[entry.slot];values_[entry.slot][stats.samples++]=ms;
            unsigned slot=entry.slot;discard(entry,false,false);
            if(stats.samples==TargetSamples)report(slot);
        }
        for(unsigned slot=0;slot<4;++slot){
            auto& stats=stats_[slot];
            if((mask_&(1u<<slot))&&!stats.done&&stats.issued>=MaxAttempts&&!outstanding_[slot]){
                stats.done=true;char line[384]{};
                std::snprintf(line,sizeof(line),"gpu_timing unavailable slot=%s reason=attempt_budget samples=%u issued=%llu disjoint=%llu dropped=%llu failures=%llu",names_[slot],stats.samples,static_cast<unsigned long long>(stats.issued),static_cast<unsigned long long>(stats.disjoint),static_cast<unsigned long long>(stats.dropped),static_cast<unsigned long long>(stats.failures));emit(line);
            }
        }
        bool complete=true;for(unsigned slot=0;slot<4;++slot)if((mask_&(1u<<slot))&&!stats_[slot].done)complete=false;
        if(complete){done_=true;free_resources();}
    }
    // Finish a timed capture without waiting for pending queries. Report only
    // completed samples; explicitly count the uncollected tail as dropped.
    void finish(){
        if(!enabled_||done_)return;
        if(active_>=0){end_active();active_=-1;}
        for(auto& entry:entries_)if(entry.state!=State::free)discard(entry,false,true);
        for(unsigned slot=0;slot<4;++slot)if((mask_&(1u<<slot))&&!stats_[slot].done){
            if(stats_[slot].samples)report(slot);
            else stats_[slot].done=true;
        }
        done_=true;free_resources();
    }
    void reset(){
        free_resources();stats_={};values_={};outstanding_={};enabled_=false;done_=false;faulted_=false;mask_=0xf;callback_=nullptr;user_=nullptr;
    }
    bool done() const{return done_;}
    bool enabled() const{return enabled_;}
    bool faulted() const{return faulted_;}
    GpuTimingStats stats(unsigned slot) const{return slot<4?stats_[slot]:GpuTimingStats{};}
    // CPU-only snapshot of completed values; safe under the profiler's mutex
    // during teardown on another thread. Does not call the D3D11 context.
    GpuTimingStats snapshot(unsigned slot)const{
        if(slot>=4)return {};
        auto result=stats_[slot];result.pending=outstanding_[slot];
        if(result.samples)distribution(slot,result);
        return result;
    }
private:
    static constexpr unsigned RingSize=16,MaxAttempts=TargetSamples*4,MaxPolls=120;
    static constexpr const char* names_[4]={"outer0","outer1","focus0","focus1"};
    using Device=typename Backend::Device;
    using Query=typename Backend::Query;
    enum class State {free,active,pending};
    struct Entry {std::array<Query,3> query{};State state=State::free;unsigned slot=0,polls=0;bool invalid=false;};
    std::array<Entry,RingSize> entries_{};
    std::array<GpuTimingStats,4> stats_{};
    std::array<std::array<double,TargetSamples>,4> values_{};
    std::array<unsigned,4> outstanding_{};
    Context context_=nullptr;Device device_=nullptr;
    bool enabled_=false,done_=false,faulted_=false;
    uint32_t mask_=0xf;
    int active_=-1;
    GpuTimingLog callback_=nullptr;void* user_=nullptr;
    void emit(const char* message){if(callback_)callback_(user_,message);}
    bool bind(Context context){
        if(context_){
            if(context!=context_){fault("context_or_device_changed");return false;}
            return true;
        }
        device_=Backend::retain_device(context);
        if(!device_){fault("missing_device");return false;}
        context_=context;Backend::retain_context(context_);return true;
    }
    bool ensure_queries(Entry& entry){
        constexpr D3D11_QUERY types[]={D3D11_QUERY_TIMESTAMP_DISJOINT,D3D11_QUERY_TIMESTAMP,D3D11_QUERY_TIMESTAMP};
        for(unsigned i=0;i<3;++i)if(!entry.query[i]){
            if(FAILED(Backend::create(device_,types[i],entry.query[i]))||!entry.query[i])return false;
        }
        return true;
    }
    void end_active(){
        auto& entry=entries_[active_];
        Backend::end(context_,entry.query[2]);Backend::end(context_,entry.query[0]);
    }
    void release_queries(Entry& entry){for(auto& query:entry.query)if(query){Backend::release(query);query=nullptr;}}
    void discard(Entry& entry,bool failure,bool dropped){
        if(failure)++stats_[entry.slot].failures;
        if(dropped)++stats_[entry.slot].dropped;
        if(entry.state!=State::free)--outstanding_[entry.slot];
        entry.state=State::free;entry.invalid=false;
    }
    void pending(Entry& entry){if(entry.polls>=MaxPolls){discard(entry,true,true);release_queries(entry);}}
    void free_resources(){
        if(active_>=0&&context_)end_active();
        active_=-1;
        for(auto& entry:entries_){release_queries(entry);entry=Entry{};}
        outstanding_={};
        if(context_){Backend::release_context(context_);context_=nullptr;}
        if(device_){Backend::release_device(device_);device_=nullptr;}
    }
    void fault(const char* reason){
        for(auto& entry:entries_)if(entry.state!=State::free){++stats_[entry.slot].failures;++stats_[entry.slot].dropped;}
        free_resources();faulted_=true;enabled_=false;
        char line[256]{};std::snprintf(line,sizeof(line),"gpu_timing unavailable reason=%s",reason);emit(line);
    }
    void distribution(unsigned slot,GpuTimingStats& stats)const{
        auto sorted=values_[slot];const auto n=stats.samples;
        std::sort(sorted.begin(),sorted.begin()+n);
        double sum=0;for(unsigned i=0;i<n;++i)sum+=sorted[i];
        auto percentile=[&](unsigned percent){return sorted[(n*percent+99)/100-1];};
        stats.mean_ms=sum/n;stats.p50_ms=percentile(50);stats.p90_ms=percentile(90);
        stats.p95_ms=percentile(95);stats.p99_ms=percentile(99);
        stats.min_ms=sorted[0];stats.max_ms=sorted[n-1];
    }
    void report(unsigned slot){
        auto& stats=stats_[slot];distribution(slot,stats);stats.done=true;
        char line[640]{};
        std::snprintf(line,sizeof(line),"gpu_timing slot=%s samples=%u mean_ms=%.6f p50_ms=%.6f p90_ms=%.6f p95_ms=%.6f p99_ms=%.6f min_ms=%.6f max_ms=%.6f disjoint=%llu dropped=%llu failures=%llu",names_[slot],stats.samples,stats.mean_ms,stats.p50_ms,stats.p90_ms,stats.p95_ms,stats.p99_ms,stats.min_ms,stats.max_ms,static_cast<unsigned long long>(stats.disjoint),static_cast<unsigned long long>(stats.dropped),static_cast<unsigned long long>(stats.failures));emit(line);
    }
};
using GpuTiming=BasicGpuTiming<gpu_detail::NativeTimingBackend>;
}
