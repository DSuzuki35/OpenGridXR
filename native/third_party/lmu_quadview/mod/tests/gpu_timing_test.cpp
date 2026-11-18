#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../gpu_timing.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
struct FakeDevice {
    unsigned refs=1,created=0,released=0,live=0,createFailAt=0,reads=0;
    unsigned pendingAt=0,readFailureAt=0;
    bool pending=false,readFailure=false,disjoint=false;
    uint64_t frequency=1000;
};
struct FakeContext { FakeDevice* device{};unsigned refs=1;bool immediate=true;uint64_t ticks=0; };
struct FakeQuery { FakeDevice* device;D3D11_QUERY type;unsigned ordinal;bool begun=false,ended=false;uint64_t value=0; };
struct FakeBackend {
    using Context=FakeContext*;using Device=FakeDevice*;using Query=FakeQuery*;
    static bool immediate(Context c){return c&&c->immediate;}
    static Device retain_device(Context c){++c->device->refs;return c->device;}
    static void release_device(Device d){--d->refs;}
    static void retain_context(Context c){++c->refs;}
    static void release_context(Context c){--c->refs;}
    static HRESULT create(Device d,D3D11_QUERY type,Query& q){
        ++d->created;if(d->createFailAt==d->created)return E_FAIL;
        q=new FakeQuery{d,type,d->created};++d->live;return S_OK;
    }
    static void release(Query q){assert(!q->begun||q->ended);--q->device->live;++q->device->released;delete q;}
    static void begin(Context,Query q){assert(q->type==D3D11_QUERY_TIMESTAMP_DISJOINT);assert(!q->begun||q->ended);q->begun=true;q->ended=false;}
    static void end(Context c,Query q){if(q->type==D3D11_QUERY_TIMESTAMP_DISJOINT){assert(q->begun&&!q->ended);q->ended=true;}else q->value=c->ticks;}
    static HRESULT data(Context,Query q,void* out,UINT bytes,UINT flags){
        assert(flags==D3D11_ASYNC_GETDATA_DONOTFLUSH);++q->device->reads;
        if(q->device->readFailure||q->device->readFailureAt==q->ordinal)return E_FAIL;
        if(q->device->pending||q->device->pendingAt==q->ordinal)return S_FALSE;
        if(q->type==D3D11_QUERY_TIMESTAMP_DISJOINT){
            assert(bytes==sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT)&&q->ended);
            *static_cast<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT*>(out)={q->device->frequency,q->device->disjoint?TRUE:FALSE};
        }else{assert(bytes==sizeof(uint64_t));*static_cast<uint64_t*>(out)=q->value;}
        return S_OK;
    }
};
using Timer=lmuqv::BasicGpuTiming<FakeBackend>;
struct Log {std::vector<std::string> lines;static void append(void* opaque,const char* line){static_cast<Log*>(opaque)->lines.emplace_back(line);} };
void sample(Timer& timer,FakeContext& context,unsigned slot,unsigned ticks=1){
    assert(timer.start(&context,slot));context.ticks+=ticks;assert(timer.stop(&context));timer.poll(&context);
}
void disabled_and_mask(){
    FakeDevice device;FakeContext context{&device};Timer timer;
    assert(!timer.enabled()&&!timer.start(&context,0));timer.poll(&context);assert(!timer.stop(&context));
    assert(device.created==0&&device.reads==0);
    timer.configure(true,3);
    assert(!timer.start(&context,2)&&device.created==0);
    timer.reset();timer.reset();assert(!timer.enabled()&&device.refs==1&&context.refs==1);
    timer.configure(true,0);assert(timer.done()&&!timer.start(&context,0)&&device.created==0);
    timer.configure(true,0x10);assert(timer.faulted()&&!timer.enabled()&&device.created==0);
}
void distribution_and_completion(){
    FakeDevice device;FakeContext context{&device};Log log;Timer timer;
    timer.configure(true,0xf,Log::append,&log);
    for(unsigned i=1;i<=60;++i)for(unsigned slot=0;slot<4;++slot)sample(timer,context,slot,i);
    assert(timer.done()&&!timer.faulted()&&log.lines.size()==4);
    for(unsigned slot=0;slot<4;++slot){
        auto stats=timer.stats(slot);assert(stats.samples==60&&stats.done&&stats.issued==60);
        assert(stats.mean_ms==30.5&&stats.p50_ms==30&&stats.p90_ms==54&&stats.min_ms==1&&stats.max_ms==60);
        assert(stats.disjoint==0&&stats.dropped==0&&stats.failures==0);
    }
    auto created=device.created,reads=device.reads;
    for(unsigned i=0;i<20;++i){assert(!timer.start(&context,i%4));timer.poll(&context);assert(!timer.stop(&context));}
    assert(device.created==created&&device.reads==reads&&log.lines.size()==4&&device.live==0);
    assert(device.refs==1&&context.refs==1);
    timer.reset();assert(!timer.enabled()&&!timer.done()&&timer.stats(0).samples==0);
}
void stereo_only(){
    FakeDevice device;FakeContext context{&device};Log log;Timer timer;
    timer.configure(true,3,Log::append,&log);
    for(unsigned i=0;i<60;++i){sample(timer,context,0);sample(timer,context,1);}
    assert(timer.done()&&log.lines.size()==2&&timer.stats(2).samples==0);
    assert(!timer.start(&context,2)&&!timer.start(&context,3));
}
void pending_ring_and_partial_reset(){
    FakeDevice device;device.pending=true;FakeContext context{&device};Timer timer(true);
    for(unsigned i=0;i<16;++i){assert(timer.start(&context,i%4));++context.ticks;assert(timer.stop(&context));}
    assert(!timer.start(&context,0));assert(timer.stats(0).dropped==1);
    assert(device.live<=48);auto reads=device.reads;timer.poll(&context);assert(device.reads>reads);
    assert(timer.stats(0).samples==0);
    device.pending=false;timer.poll(&context);for(unsigned slot=0;slot<4;++slot)assert(timer.stats(slot).samples==4);
    assert(timer.start(&context,0));timer.reset();timer.reset();
    assert(device.live==0&&device.refs==1&&context.refs==1);
}
void overlap_and_invalid_context(){
    FakeDevice device;FakeContext context{&device};Timer timer(true);
    assert(timer.start(&context,0));assert(!timer.start(&context,1));context.ticks+=4;assert(!timer.stop(&context));timer.poll(&context);
    assert(timer.stats(0).samples==0&&timer.stats(0).failures>0&&timer.stats(1).dropped>0);
    assert(timer.start(&context,0));assert(!timer.stop(nullptr));timer.poll(&context);assert(timer.stats(0).samples==0);
    FakeContext deferred{&device};deferred.immediate=false;assert(!timer.start(&deferred,0));
    assert(!timer.start(&context,4));timer.reset();assert(device.live==0);
}
void device_change(){
    FakeDevice first,second;FakeContext context{&first},other{&second};Timer timer(true);
    assert(timer.start(&context,0));assert(!timer.stop(&other));
    assert(timer.faulted()&&!timer.enabled()&&first.live==0&&second.created==0);
    assert(first.refs==1&&context.refs==1&&second.refs==1);
    timer.reset();timer.configure(true,1);sample(timer,other,0);timer.reset();assert(second.live==0);
    timer.configure(true,1);assert(timer.start(&context,0)&&timer.stop(&context));
    timer.poll(&other);assert(timer.faulted()&&first.live==0&&second.live==0);
}
void timestamp_readiness_and_errors(){
    for(unsigned queryOrdinal:{2u,3u}){
        FakeDevice device;FakeContext context{&device};Timer timer(true);
        assert(timer.start(&context,0));timer.poll(&context);assert(device.reads==0); // Never poll an active query.
        context.ticks=5;assert(timer.stop(&context));device.pendingAt=queryOrdinal;timer.poll(&context);
        assert(timer.stats(0).samples==0&&timer.stats(0).failures==0&&device.reads==queryOrdinal);
        device.pendingAt=0;timer.poll(&context);assert(timer.stats(0).samples==1);
        timer.reset();timer.configure(true,1);
        assert(timer.start(&context,0));context.ticks+=5;assert(timer.stop(&context));
        device.readFailureAt=device.created-3+queryOrdinal;timer.poll(&context);
        assert(timer.stats(0).samples==0&&timer.stats(0).failures==1&&device.live==0);
    }
    FakeDevice device;FakeContext context{&device};Timer timer(true);
    context.ticks=10;assert(timer.start(&context,0));context.ticks=9;assert(timer.stop(&context));timer.poll(&context);
    assert(timer.stats(0).samples==0&&timer.stats(0).failures==1);
    timer.reset();timer.configure(true,1);device.frequency=1000000;
    for(unsigned i=0;i<60;++i)sample(timer,context,0,1000);
    assert(timer.done()&&timer.stats(0).mean_ms==1.0);
}
void failures_and_limits(){
    FakeDevice device;FakeContext context{&device};Timer timer(true);
    device.disjoint=true;sample(timer,context,0);assert(timer.stats(0).disjoint==1&&timer.stats(0).samples==0);
    device.disjoint=false;device.frequency=0;sample(timer,context,0);assert(timer.stats(0).failures==1);
    device.frequency=1000;device.readFailure=true;sample(timer,context,0);assert(timer.stats(0).failures==2);
    device.readFailure=false;assert(timer.start(&context,0));++context.ticks;assert(timer.stop(&context));
    device.pending=true;for(unsigned i=0;i<120;++i)timer.poll(&context);assert(timer.stats(0).dropped>=1&&timer.stats(0).samples==0);
    timer.reset();assert(device.live==0);
    FakeDevice broken;broken.createFailAt=2;FakeContext bad{&broken};timer.configure(true);
    assert(!timer.start(&bad,0)&&timer.faulted()&&broken.live==0&&broken.refs==1&&bad.refs==1);
    timer.reset();device.pending=false;device.disjoint=true;timer.configure(true,1);
    for(unsigned i=0;i<240;++i)sample(timer,context,0);
    assert(timer.done()&&timer.stats(0).samples==0&&timer.stats(0).issued==240&&device.live==0);
}
void warp_smoke(){
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;D3D_FEATURE_LEVEL feature{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&feature,&context)));
    lmuqv::GpuTiming timer;assert(!timer.start(context,0));timer.configure(true,3);
    assert(timer.start(context,0));assert(timer.stop(context));timer.poll(context);
    assert(timer.stats(0).failures==0); // S_FALSE is a valid asynchronous result on an unflushed context.
    assert(timer.start(context,1));timer.reset();timer.reset();
    ID3D11DeviceContext* deferred=nullptr;assert(SUCCEEDED(device->CreateDeferredContext(0,&deferred)));
    timer.configure(true,1);assert(!timer.start(deferred,0));timer.reset();deferred->Release();
    ID3D11Device* secondDevice=nullptr;ID3D11DeviceContext* secondContext=nullptr;
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&secondDevice,&feature,&secondContext)));
    timer.configure(true,1);assert(timer.start(context,0)&&timer.stop(context));timer.poll(secondContext);
    assert(timer.faulted()&&!timer.enabled());timer.reset();secondContext->Release();secondDevice->Release();
    context->Release();device->Release();
}
void timed_partial_finish(){
    FakeDevice device;FakeContext context{&device};Timer timer(true);
    sample(timer,context,0,2);sample(timer,context,0,8);
    device.pending=true;assert(timer.start(&context,0));++context.ticks;assert(timer.stop(&context));
    const auto reads=device.reads;
    const auto saved=timer.snapshot(0);
    assert(saved.samples==2&&saved.mean_ms==5&&saved.p99_ms==8&&saved.pending==1&&device.reads==reads);
    timer.finish();
    const auto s=timer.stats(0);
    assert(timer.done()&&s.done&&s.samples==2&&s.mean_ms==5&&s.p95_ms==8&&s.p99_ms==8&&s.dropped==1);
    assert(device.reads==reads&&device.live==0&&context.refs==1&&device.refs==1); // no draining/waiting
    timer.finish();timer.reset();timer.configure(true);timer.finish();
    assert(timer.done()&&timer.stats(0).samples==0&&device.live==0);
    lmuqv::BasicGpuTiming<FakeBackend,2> shortTimer(true);
    device.pending=false;
    for(unsigned i=0;i<2;++i){assert(shortTimer.start(&context,0));++context.ticks;assert(shortTimer.stop(&context));shortTimer.poll(&context);}
    assert(shortTimer.stats(0).done&&shortTimer.stats(0).samples==2&&shortTimer.stats(0).mean_ms==1);
    shortTimer.finish();assert(device.live==0);
}
}
int main(){
    disabled_and_mask();distribution_and_completion();stereo_only();pending_ring_and_partial_reset();
    overlap_and_invalid_context();device_change();timestamp_readiness_and_errors();failures_and_limits();warp_smoke();timed_partial_finish();
    std::cout<<"gpu_timing tests passed: disabled, masks, distribution, ring, lifetime, overlap, device change, readiness/errors, limits, native WARP\n";
}
