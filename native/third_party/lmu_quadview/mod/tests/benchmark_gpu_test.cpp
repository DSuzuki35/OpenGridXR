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
struct FakeContext { FakeDevice* device{};unsigned refs=1;bool immediate=true;uint64_t ticks=0;unsigned endCalls=0; };
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
    static void release(Query q){--q->device->live;++q->device->released;delete q;}
    static void begin(Context,Query q){assert(q->type==D3D11_QUERY_TIMESTAMP_DISJOINT);assert(!q->begun||q->ended);q->begun=true;q->ended=false;}
    static void end(Context c,Query q){++c->endCalls;if(q->type==D3D11_QUERY_TIMESTAMP_DISJOINT){assert(q->begun&&!q->ended);q->ended=true;}else q->value=c->ticks;}
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
}
#include "../benchmark_gpu.hpp"
int main(){
 using Span=lmuqv::benchmark::GpuSpan<FakeBackend>;FakeDevice device;FakeContext context{&device};Span span;double ms=0;uint64_t source=0;
 assert(!span.start(&context,1)&&device.created==0);assert(span.start(&context,8));assert(!span.poll(ms,source));context.ticks+=7;span.stop();device.pending=true;assert(!span.poll(ms,source)&&std::isnan(ms)&&source==0);device.pending=false;
 assert(span.poll(ms,source)&&ms==7&&source==8);assert(!span.poll(ms,source)&&std::isnan(ms)&&source==0);
 assert(span.start(&context,16));context.ticks+=200;span.stop(false);assert(!span.poll(ms,source)&&source==0&&std::isnan(ms));
 assert(span.start(&context,16));span.stop();device.disjoint=true;assert(!span.poll(ms,source)&&span.failures==1);device.disjoint=false;
 assert(span.start(&context,24));span.stop();device.readFailure=true;assert(!span.poll(ms,source)&&span.failures==2);device.readFailure=false;
 assert(span.start(&context,32));auto ends=context.endCalls;span.reset();assert(context.endCalls==ends);assert(device.live==0&&context.refs==1&&device.refs==1);
 device.createFailAt=device.created+2;assert(!span.start(&context,40)&&span.failures==1);assert(device.live==0);span.reset();device.createFailAt=0;
 device.pending=true;for(unsigned frame=8;frame<=64;frame+=8){assert(span.start(&context,frame));span.stop();}assert(!span.start(&context,72));for(int i=0;i<240;++i)assert(!span.poll(ms,source));assert(span.failures==8&&device.live==0);
 span.reset();device.pending=false;assert(span.start(&context,80));FakeDevice other;FakeContext otherContext{&other};span.stop();assert(!span.start(&otherContext,88));span.reset();assert(device.live==0&&context.refs==1);
 std::cout<<"benchmark sparse GPU lifecycle PASS\n";
}
