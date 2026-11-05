#pragma once
#include "gpu_timing.hpp"
#include "benchmark_native.hpp"
namespace lmuqv { namespace benchmark {
// One query triple every eighth frame. Bounded work, no Flush, no synchronous readback.
template<class Backend=gpu_detail::NativeTimingBackend> class GpuSpan {
 using Context=typename Backend::Context;using Device=typename Backend::Device;using Query=typename Backend::Query;
 struct Entry {Query queries[3]{};uint64_t frame=0;unsigned polls=0;bool pending=false;};
 std::array<Entry,8> entries_{};Context context_=nullptr;Device device_=nullptr;int active_=-1;bool faulted_=false;
 void release(Entry& e){for(auto& q:e.queries)if(q){Backend::release(q);q=nullptr;}e={};}
public:
 uint32_t failures=0;
 ~GpuSpan(){reset();}
 void reset(){/* Teardown may run on another thread: release queries without context calls. */for(auto& e:entries_)release(e);if(context_)Backend::release_context(context_);if(device_)Backend::release_device(device_);context_=nullptr;device_=nullptr;active_=-1;faulted_=false;failures=0;}
 bool start(Context context,uint64_t frame){
 if(faulted_||frame%8||active_>=0||!Backend::immediate(context))return false;
 if(context_&&context_!=context){++failures;faulted_=true;return false;}
 if(!context_){device_=Backend::retain_device(context);if(!device_){++failures;faulted_=true;return false;}context_=context;Backend::retain_context(context_);}
 for(unsigned i=0;i<entries_.size();++i){auto& e=entries_[i];if(e.pending)continue;
 const D3D11_QUERY types[]={D3D11_QUERY_TIMESTAMP_DISJOINT,D3D11_QUERY_TIMESTAMP,D3D11_QUERY_TIMESTAMP};
 for(unsigned j=0;j<3;++j)if(!e.queries[j]&&(FAILED(Backend::create(device_,types[j],e.queries[j]))||!e.queries[j])){release(e);++failures;faulted_=true;return false;}
 e.frame=frame;e.polls=0;Backend::begin(context_,e.queries[0]);Backend::end(context_,e.queries[1]);active_=int(i);return true;}
 return false;
 }
 void stop(bool scene=true){if(active_<0)return;auto& e=entries_[active_];Backend::end(context_,e.queries[2]);Backend::end(context_,e.queries[0]);e.pending=scene;active_=-1;}
 bool poll(double& ms,uint64_t& frame){
 ms=missing();frame=0;if(!context_)return false;
 for(auto& e:entries_){if(!e.pending)continue;D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d{};uint64_t first=0,last=0;++e.polls;
 HRESULT r=Backend::data(context_,e.queries[0],&d,sizeof(d),D3D11_ASYNC_GETDATA_DONOTFLUSH);
 if(r==S_OK&&!d.Disjoint&&d.Frequency)r=Backend::data(context_,e.queries[1],&first,sizeof(first),D3D11_ASYNC_GETDATA_DONOTFLUSH);
 if(r==S_OK&&!d.Disjoint&&d.Frequency)r=Backend::data(context_,e.queries[2],&last,sizeof(last),D3D11_ASYNC_GETDATA_DONOTFLUSH);
 if(r==S_FALSE&&e.polls<240)continue;
 e.pending=false;if(r!=S_OK||d.Disjoint||!d.Frequency||last<first){++failures;release(e);continue;}
 ms=double(last-first)*1000/double(d.Frequency);frame=e.frame;return true;
 }return false;
 }
};
} }
