#pragma once
#include <d3d11.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace lmuqv {
using PipelineStatsLog=void(*)(void*,const char*);
struct PipelineStatsResult {
    unsigned samples=0;
    uint64_t issued=0,dropped=0,failures=0;
    uint64_t ps_sum=0,vs_sum=0,ia_sum=0,cs_sum=0;
    double ps_mean=0,vs_mean=0,ia_mean=0,cs_mean=0;
    bool done=false;
};
namespace pipeline_detail {
struct NativeBackend {
    using Context=ID3D11DeviceContext*;
    using Device=ID3D11Device*;
    using Query=ID3D11Query*;
    static bool immediate(Context c){return c&&c->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE;}
    static Device retain_device(Context c){Device d=nullptr;c->GetDevice(&d);return d;}
    static void release_device(Device d){d->Release();}
    static void retain_context(Context c){c->AddRef();}
    static void release_context(Context c){c->Release();}
    static HRESULT create(Device d,Query& q){D3D11_QUERY_DESC desc{D3D11_QUERY_PIPELINE_STATISTICS,0};return d->CreateQuery(&desc,&q);}
    static void release(Query q){q->Release();}
    static void begin(Context c,Query q){c->Begin(q);}
    static void end(Context c,Query q){c->End(q);}
    static HRESULT data(Context c,Query q,D3D11_QUERY_DATA_PIPELINE_STATISTICS& out){return c->GetData(q,&out,sizeof(out),D3D11_ASYNC_GETDATA_DONOTFLUSH);}
};
}
// Render-thread owned, including reset/destruction. Callback must not re-enter or throw.
// Distinct query objects may surround the same draw interval as the GPU timer.
// Disabled by default; never waits, flushes, maps or forces GPU completion.
template<class Backend> class BasicPipelineStats {
public:
    using Context=typename Backend::Context;
    BasicPipelineStats()=default;
    ~BasicPipelineStats(){reset();}
    BasicPipelineStats(const BasicPipelineStats&)=delete;
    BasicPipelineStats& operator=(const BasicPipelineStats&)=delete;
    void configure(bool enable,unsigned mask=0xf,PipelineStatsLog callback=nullptr,void* user=nullptr){
        reset();enabled_=enable;mask_=mask;callback_=callback;user_=user;
        if(mask&~0xfu){fault("invalid_slot_mask");}
        else if(enable&&!mask)done_=true;
    }
    bool start(Context context,unsigned slot){
        if(!enabled_||done_||faulted_||slot>=4||!(mask_&(1u<<slot))||stats_[slot].done)return false;
        if(active_>=0){entries_[active_].invalid=true;++stats_[slot].failures;++stats_[slot].dropped;return false;}
        if(!context||!Backend::immediate(context)){++stats_[slot].failures;return false;}
        if(!bind(context))return false;
        auto& stats=stats_[slot];
        if(stats.samples+outstanding_[slot]>=TargetSamples||stats.issued>=MaxAttempts)return false;
        int free=-1;for(unsigned i=0;i<RingSize;++i)if(entries_[i].state==State::free){free=int(i);break;}
        if(free<0){++stats.dropped;return false;}
        auto& entry=entries_[free];
        if(!entry.query&&(FAILED(Backend::create(device_,entry.query))||!entry.query)){
            ++stats.failures;fault("create_query_failed");return false;
        }
        entry.slot=slot;entry.polls=0;entry.invalid=false;entry.state=State::active;
        Backend::begin(context_,entry.query);active_=free;++stats.issued;++outstanding_[slot];return true;
    }
    bool stop(Context context){
        if(!enabled_||done_||faulted_||active_<0)return false;
        auto& entry=entries_[active_];
        if(!context||!Backend::immediate(context)){
            Backend::end(context_,entry.query);active_=-1;discard(entry,true);return false;
        }
        if(!bind(context))return false;
        Backend::end(context_,entry.query);active_=-1;
        if(entry.invalid){discard(entry,true);return false;}
        entry.state=State::pending;return true;
    }
    void poll(Context context){
        if(!enabled_||done_||faulted_||!context_)return;
        if(!context||!Backend::immediate(context)){fault("invalid_poll_context");return;}
        if(!bind(context))return;
        for(auto& entry:entries_){
            if(entry.state!=State::pending)continue;
            ++entry.polls;D3D11_QUERY_DATA_PIPELINE_STATISTICS data{};
            const HRESULT result=Backend::data(context_,entry.query,data);
            if(result==S_FALSE){if(entry.polls>=MaxPolls){discard(entry,true);release_query(entry);}continue;}
            if(result!=S_OK){discard(entry,true);release_query(entry);continue;}
            auto& stats=stats_[entry.slot];
            const uint64_t maximum=(std::numeric_limits<uint64_t>::max)();
            if(data.PSInvocations>maximum-stats.ps_sum||data.VSInvocations>maximum-stats.vs_sum||
               data.IAPrimitives>maximum-stats.ia_sum||data.CSInvocations>maximum-stats.cs_sum){
                discard(entry,true);fault("counter_sum_overflow");return;
            }
            stats.ps_sum+=data.PSInvocations;stats.vs_sum+=data.VSInvocations;
            stats.ia_sum+=data.IAPrimitives;stats.cs_sum+=data.CSInvocations;++stats.samples;
            unsigned slot=entry.slot;discard(entry,false);
            if(stats.samples==TargetSamples)report(slot);
        }
        bool complete=true;
        for(unsigned slot=0;slot<4;++slot){
            if(!(mask_&(1u<<slot)))continue;
            auto& stats=stats_[slot];
            if(!stats.done&&stats.issued>=MaxAttempts&&!outstanding_[slot]){
                stats.done=true;char line[384]{};
                std::snprintf(line,sizeof(line),"pipeline_stats unavailable slot=%s reason=attempt_budget samples=%u issued=%llu dropped=%llu failures=%llu",names_[slot],stats.samples,static_cast<unsigned long long>(stats.issued),static_cast<unsigned long long>(stats.dropped),static_cast<unsigned long long>(stats.failures));emit(line);
            }
            complete=complete&&stats.done;
        }
        if(complete){done_=true;free_resources();}
    }
    void reset(){free_resources();stats_={};outstanding_={};mask_=0xf;enabled_=done_=faulted_=false;callback_=nullptr;user_=nullptr;}
    bool done()const{return done_;}
    bool enabled()const{return enabled_;}
    bool faulted()const{return faulted_;}
    PipelineStatsResult stats(unsigned slot)const{return slot<4?stats_[slot]:PipelineStatsResult{};}
private:
    static constexpr unsigned RingSize=16,TargetSamples=60,MaxAttempts=240,MaxPolls=120;
    static constexpr const char* names_[4]={"outer0","outer1","focus0","focus1"};
    using Device=typename Backend::Device;
    using Query=typename Backend::Query;
    enum class State{free,active,pending};
    struct Entry{Query query=nullptr;State state=State::free;unsigned slot=0,polls=0;bool invalid=false;};
    std::array<Entry,RingSize> entries_{};
    std::array<PipelineStatsResult,4> stats_{};
    std::array<unsigned,4> outstanding_{};
    Context context_=nullptr;Device device_=nullptr;
    unsigned mask_=0xf;int active_=-1;
    bool enabled_=false,done_=false,faulted_=false;
    PipelineStatsLog callback_=nullptr;void* user_=nullptr;
    void emit(const char* line){if(callback_)callback_(user_,line);}
    bool bind(Context context){
        if(context_){if(context!=context_){fault("context_or_device_changed");return false;}return true;}
        device_=Backend::retain_device(context);if(!device_){fault("missing_device");return false;}
        context_=context;Backend::retain_context(context_);return true;
    }
    void release_query(Entry& entry){if(entry.query){Backend::release(entry.query);entry.query=nullptr;}}
    void discard(Entry& entry,bool failure){
        if(failure){++stats_[entry.slot].failures;++stats_[entry.slot].dropped;}
        if(entry.state!=State::free)--outstanding_[entry.slot];
        entry.state=State::free;entry.invalid=false;
    }
    void free_resources(){
        if(active_>=0&&context_)Backend::end(context_,entries_[active_].query);
        active_=-1;for(auto& entry:entries_){release_query(entry);entry=Entry{};}
        outstanding_={};
        if(context_){Backend::release_context(context_);context_=nullptr;}
        if(device_){Backend::release_device(device_);device_=nullptr;}
    }
    void fault(const char* reason){
        for(auto& entry:entries_)if(entry.state!=State::free){++stats_[entry.slot].failures;++stats_[entry.slot].dropped;}
        free_resources();faulted_=true;enabled_=false;
        char line[256]{};std::snprintf(line,sizeof(line),"pipeline_stats unavailable reason=%s",reason);emit(line);
    }
    void report(unsigned slot){
        auto& stats=stats_[slot];stats.done=true;
        stats.ps_mean=double(stats.ps_sum)/stats.samples;stats.vs_mean=double(stats.vs_sum)/stats.samples;
        stats.ia_mean=double(stats.ia_sum)/stats.samples;stats.cs_mean=double(stats.cs_sum)/stats.samples;
        char line[768]{};
        std::snprintf(line,sizeof(line),"pipeline_stats slot=%s samples=%u PSInvocations_sum=%llu PSInvocations_mean=%.3f VSInvocations_sum=%llu VSInvocations_mean=%.3f IAPrimitives_sum=%llu IAPrimitives_mean=%.3f CSInvocations_sum=%llu CSInvocations_mean=%.3f issued=%llu dropped=%llu failures=%llu",names_[slot],stats.samples,static_cast<unsigned long long>(stats.ps_sum),stats.ps_mean,static_cast<unsigned long long>(stats.vs_sum),stats.vs_mean,static_cast<unsigned long long>(stats.ia_sum),stats.ia_mean,static_cast<unsigned long long>(stats.cs_sum),stats.cs_mean,static_cast<unsigned long long>(stats.issued),static_cast<unsigned long long>(stats.dropped),static_cast<unsigned long long>(stats.failures));emit(line);
    }
};
using PipelineStats=BasicPipelineStats<pipeline_detail::NativeBackend>;
}
