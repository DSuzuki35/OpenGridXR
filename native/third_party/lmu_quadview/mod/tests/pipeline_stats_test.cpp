#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../pipeline_stats.hpp"
#include "../gpu_timing.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {
struct Device{unsigned refs=1,created=0,released=0,live=0,reads=0;bool createFail=false,pending=false,readFail=false;};
struct Context{Device* device;unsigned refs=1;bool immediate=true;D3D11_QUERY_DATA_PIPELINE_STATISTICS value{};};
struct Query{Device* device;bool active=false,ended=false;D3D11_QUERY_DATA_PIPELINE_STATISTICS value{};};
struct Backend{
    using Context=::Context*;using Device=::Device*;using Query=::Query*;
    static bool immediate(Context c){return c&&c->immediate;}
    static Device retain_device(Context c){++c->device->refs;return c->device;}
    static void release_device(Device d){--d->refs;}
    static void retain_context(Context c){++c->refs;}
    static void release_context(Context c){--c->refs;}
    static HRESULT create(Device d,Query& q){++d->created;if(d->createFail)return E_FAIL;q=new ::Query{d};++d->live;return S_OK;}
    static void release(Query q){assert(!q->active);--q->device->live;++q->device->released;delete q;}
    static void begin(Context,Query q){assert(!q->active);q->active=true;q->ended=false;}
    static void end(Context c,Query q){assert(q->active);q->active=false;q->ended=true;q->value=c->value;}
    static HRESULT data(Context,Query q,D3D11_QUERY_DATA_PIPELINE_STATISTICS& out){
        assert(q->ended&&!q->active);++q->device->reads;
        if(q->device->readFail)return E_FAIL;
        if(q->device->pending)return S_FALSE;
        out=q->value;return S_OK;
    }
};
using Stats=lmuqv::BasicPipelineStats<Backend>;
struct Log{std::vector<std::string> lines;static void append(void* p,const char* s){static_cast<Log*>(p)->lines.emplace_back(s);}};
void sample(Stats& s,Context& c,unsigned slot,uint64_t count=1){
    c.value={};c.value.PSInvocations=count;c.value.VSInvocations=count*2;c.value.IAPrimitives=count*3;c.value.CSInvocations=count*4;
    assert(s.start(&c,slot)&&s.stop(&c));s.poll(&c);
}
void aggregation(){
    Device d;Context c{&d};Log log;Stats s;
    assert(!s.start(&c,0));s.poll(&c);assert(!s.stop(&c)&&d.created==0&&d.reads==0);
    s.configure(true,0xf,Log::append,&log);
    for(unsigned n=1;n<=60;++n)for(unsigned slot=0;slot<4;++slot)sample(s,c,slot,n);
    assert(s.done()&&!s.faulted()&&log.lines.size()==4&&d.live==0);
    for(unsigned slot=0;slot<4;++slot){auto r=s.stats(slot);
        assert(r.samples==60&&r.issued==60&&r.done&&!r.failures&&!r.dropped);
        assert(r.ps_sum==1830&&r.vs_sum==3660&&r.ia_sum==5490&&r.cs_sum==7320);
        assert(r.ps_mean==30.5&&r.vs_mean==61&&r.ia_mean==91.5&&r.cs_mean==122);
        assert(log.lines[slot].find("PSInvocations_sum=1830")!=std::string::npos);
    }
    auto reads=d.reads,created=d.created;s.poll(&c);assert(!s.start(&c,0)&&!s.stop(&c)&&d.reads==reads&&d.created==created);
    s.reset();s.reset();assert(d.refs==1&&c.refs==1&&!s.done());
    s.configure(true,3);assert(!s.start(&c,2));
    for(unsigned i=0;i<60;++i){sample(s,c,0);sample(s,c,1);}
    assert(s.done()&&s.stats(2).samples==0);s.reset();
    s.configure(true,0);assert(s.done()&&!s.start(&c,0));
    s.configure(true,16);assert(s.faulted()&&!s.enabled());
}
void lifetime_and_errors(){
    Device d;Context c{&d};Stats s;s.configure(true);d.pending=true;
    for(unsigned i=0;i<16;++i)assert(s.start(&c,i%4)&&s.stop(&c));
    assert(d.live==16&&!s.start(&c,0)&&s.stats(0).dropped==1);
    s.poll(&c);assert(s.stats(0).samples==0&&s.stats(0).failures==0);
    d.pending=false;s.poll(&c);for(unsigned i=0;i<4;++i)assert(s.stats(i).samples==4);
    assert(s.start(&c,0));auto reads=d.reads;s.poll(&c);assert(d.reads==reads);
    s.reset();s.reset();assert(d.live==0&&d.refs==1&&c.refs==1);
    s.configure(true);assert(s.start(&c,0));assert(!s.start(&c,1)&&!s.stop(&c));
    assert(s.stats(0).failures==1&&s.stats(1).failures==1);s.reset();
    s.configure(true);assert(s.start(&c,0)&&!s.stop(nullptr));s.reset();assert(!d.live);
    Context deferred{&d};deferred.immediate=false;s.configure(true);assert(!s.start(&deferred,0));
    assert(!s.start(&c,4));s.reset();
    d.readFail=true;s.configure(true);sample(s,c,0);assert(!s.stats(0).samples&&s.stats(0).failures==1&&!d.live);s.reset();d.readFail=false;
    d.createFail=true;s.configure(true);assert(!s.start(&c,0)&&s.faulted()&&!d.live);s.reset();d.createFail=false;
    Device d2;Context c2{&d2};s.configure(true);assert(s.start(&c,0)&&!s.stop(&c2));assert(s.faulted()&&!d.live);s.reset();
    s.configure(true);assert(s.start(&c,0)&&s.stop(&c));s.poll(&c2);assert(s.faulted()&&!d.live);s.reset();
    assert(d.refs==1&&c.refs==1&&d2.refs==1&&c2.refs==1);
}
void budgets_and_overflow(){
    Device d;Context c{&d};Stats s;s.configure(true,1);d.pending=true;
    assert(s.start(&c,0)&&s.stop(&c));for(unsigned i=0;i<120;++i)s.poll(&c);
    assert(!d.live&&s.stats(0).failures==1&&s.stats(0).samples==0);
    d.pending=false;d.readFail=true;
    for(unsigned i=1;i<240;++i){assert(s.start(&c,0)&&s.stop(&c));s.poll(&c);}
    assert(s.done()&&s.stats(0).issued==240&&s.stats(0).failures==240&&!d.live);
    s.reset();d.readFail=false;s.configure(true,1);
    c.value={};c.value.PSInvocations=(std::numeric_limits<uint64_t>::max)();
    assert(s.start(&c,0)&&s.stop(&c));s.poll(&c);assert(s.stats(0).samples==1);
    c.value.PSInvocations=1;assert(s.start(&c,0)&&s.stop(&c));s.poll(&c);
    assert(s.faulted()&&s.stats(0).samples==1&&!d.live);s.reset();
}
void native_warp(){
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    D3D_FEATURE_LEVEL level{};
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context);
    assert(SUCCEEDED(hr)&&device&&context);
    lmuqv::PipelineStats stats,nested;lmuqv::GpuTiming timer;
    stats.configure(true,1);nested.configure(true,1);timer.configure(true,1);
    assert(timer.start(context,0)&&stats.start(context,0));
    assert(nested.start(context,0)&&nested.stop(context));
    assert(stats.stop(context)&&timer.stop(context));stats.poll(context);timer.poll(context);
    nested.poll(context);assert(!nested.faulted()&&nested.stats(0).failures==0);nested.reset();
    assert(!stats.faulted()&&!timer.faulted()&&SUCCEEDED(device->GetDeviceRemovedReason()));
    // Either S_FALSE or a ready zero-work query is valid; never force completion.
    assert(stats.stats(0).samples<=1&&stats.stats(0).failures==0);
    assert(stats.start(context,0));stats.reset();stats.reset();timer.reset();
    ID3D11DeviceContext* deferred=nullptr;assert(SUCCEEDED(device->CreateDeferredContext(0,&deferred)));
    stats.configure(true);assert(!stats.start(deferred,0));stats.reset();deferred->Release();
    context->Release();device->Release();
}
}
int main(){aggregation();lifetime_and_errors();budgets_and_overflow();native_warp();std::cout<<"PipelineStats tests passed: sums/means, masks, ring, lifecycle, errors, limits, overflow, nested native WARP queries\n";}
