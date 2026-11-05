#pragma once
#include "native_upload_reuse.hpp"
#include <d3d11_1.h>

namespace lmuqv {
// The pinned VS uploader writes seven ordinary CPU shadows into seven dynamic
// CBs. Combine its four small CBs into fixed, 256-byte-aligned windows in ONE
// dynamic CB. Keep the large bone/light buffers on their native dirty-only path.
// WRITE_DISCARD renames the whole combined buffer; offsets
// and buffer identity remain stable, so unchanged bindings need no setter.
// Native shaders, per-eye preparation and all draws remain unchanged.
class NativeUploadBatch {
public:
    using NativeFn=void(*)(void*,void*,ID3D11DeviceContext*,void*);
    struct Counters {
        uint64_t scopes=0,warmups=0,outerCalls=0,focusCalls=0,nativeMapsReplaced=0;
        uint64_t batchMaps=0,restoreMaps=0,nativeBytesReplaced=0,batchBytes=0;
        uint64_t largeMaps=0,invalidations=0,mapFailures=0;
    };
    struct Result {bool batched=false,mapped=false;unsigned maps=0;};
private:
    struct Range {ID3D11Buffer* original=nullptr;UINT slot=0,first=0,count=0;bool supported=false;};
    std::array<Range,7> ranges_{};
    ID3D11DeviceContext1* context_=nullptr;ID3D11DeviceContext* nativeContext_=nullptr;ID3D11Buffer* packed_=nullptr;
    void* cache_=nullptr;void* bindings_=nullptr;void* nativeDevice_=nullptr;NativeFn native_=nullptr;
    DWORD owner_=0;bool active_=false,learned_=false,used_=false,collect_=false,focus_=false;
    Counters stats_{};
    uint64_t failureSerial_=0;const char* failureReason_="none";size_t failureRange_=0;HRESULT failureHr_=S_OK;
    void failure(const char* reason,size_t range,HRESULT hr=S_OK){++failureSerial_;failureReason_=reason;failureRange_=range;failureHr_=hr;}
    static constexpr UINT aligned(UINT bytes){return (bytes+255u)&~255u;}
    static constexpr bool combined(size_t i){return i==0||i>=4;}
    template<class T>static T at(const void* pointer,size_t offset){T v{};std::memcpy(&v,static_cast<const std::byte*>(pointer)+offset,sizeof(v));return v;}
    template<class T>static void put(void* pointer,size_t offset,T v){std::memcpy(static_cast<std::byte*>(pointer)+offset,&v,sizeof(v));}
    static bool supported(ID3D11Buffer* b,size_t bytes){
        if(!b)return false;D3D11_BUFFER_DESC d{};b->GetDesc(&d);
        return d.ByteWidth==bytes&&d.Usage==D3D11_USAGE_DYNAMIC&&d.BindFlags==D3D11_BIND_CONSTANT_BUFFER&&d.CPUAccessFlags==D3D11_CPU_ACCESS_WRITE&&!d.MiscFlags;
    }
    bool learn(){
        unsigned usedSlots=0;
        for(size_t i=0;i<ranges_.size();++i){const auto& n=native_upload_ranges[i];auto& r=ranges_[i];const auto* buffer=at<ID3D11Buffer*>(cache_,n.buffer);
            if(buffer!=r.original){if(r.original)r.original->Release();r.original=const_cast<ID3D11Buffer*>(buffer);r.supported=false;if(r.original){r.original->AddRef();r.supported=supported(r.original,n.bytes);}}
            r.slot=at<UINT>(cache_,n.dirty-4);
            if(!r.supported){failure("native CB allocation size/type or missing buffer",i);return false;}
            if(r.slot>=14){failure("native VS CB slot outside supported range",i);return false;}
            if(usedSlots&(1u<<r.slot)){failure("duplicate native VS CB slot",i);return false;}usedSlots|=1u<<r.slot;
            for(size_t j=0;j<i;++j)if(r.original==ranges_[j].original){failure("aliased native VS CB resources",i);return false;}
        }return true;
    }
    bool unchanged(){
        for(size_t i=0;i<ranges_.size();++i){const auto& n=native_upload_ranges[i];if(at<ID3D11Buffer*>(cache_,n.buffer)!=ranges_[i].original||at<UINT>(cache_,n.dirty-4)!=ranges_[i].slot){failure("native CB resource or slot changed during scope",i);return false;}}return true;
    }
    void nativeBindings(){
        // Restore the exact native CB objects and ordinary (zero-offset) views.
        // This also repairs the weak native binding cache before its uploader
        // handles a changed buffer/slot layout. PS b5 aliases the first VS CB.
        for(size_t i=0;i<ranges_.size();++i)if(combined(i)){const auto& r=ranges_[i];context_->VSSetConstantBuffers(r.slot,1,&r.original);put(bindings_,0x78+size_t(r.slot)*8,r.original);}
        context_->PSSetConstantBuffers(5,1,&ranges_[0].original);
    }
    void restore(){
        if(!used_)return;nativeBindings();
        // The native GPU CBs did not receive our batched writes. Re-synchronize
        // them before ANY following legacy draw, not merely their bindings.
        for(size_t i=0;i<ranges_.size();++i)if(combined(i)){const auto& n=native_upload_ranges[i];auto* b=at<ID3D11Buffer*>(cache_,n.buffer);
            if(b&&(b==ranges_[i].original||supported(b,n.bytes)))put<uint8_t>(cache_,n.dirty,1);}
        if(collect_)for(const auto& n:native_upload_ranges)if(at<ID3D11Buffer*>(cache_,n.buffer)&&at<uint8_t>(cache_,n.dirty))++stats_.restoreMaps;
        native_(cache_,nativeDevice_,nativeContext_,bindings_);used_=false;
    }
public:
    static constexpr UINT packed_bytes(){UINT bytes=0;for(size_t i=0;i<native_upload_ranges.size();++i)if(combined(i))bytes+=aligned(UINT(native_upload_ranges[i].bytes));return bytes;}
    NativeUploadBatch()=default;~NativeUploadBatch(){reset();}
    NativeUploadBatch(const NativeUploadBatch&)=delete;NativeUploadBatch& operator=(const NativeUploadBatch&)=delete;
    bool configure(ID3D11DeviceContext* immediate){
        if(!reset()||!immediate||immediate->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
        ID3D11Device* device=nullptr;immediate->GetDevice(&device);D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
        const auto caps=device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,&options,sizeof(options));
        if(FAILED(caps)||!options.ConstantBufferOffsetting||FAILED(immediate->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(&context_)))){device->Release();return false;}
        D3D11_BUFFER_DESC d{};d.ByteWidth=packed_bytes();d.Usage=D3D11_USAGE_DYNAMIC;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        const auto hr=device->CreateBuffer(&d,nullptr,&packed_);device->Release();if(FAILED(hr)){reset();return false;}nativeContext_=immediate;
        UINT offset=0;for(size_t i=0;i<ranges_.size();++i)if(combined(i)){ranges_[i].first=offset/16;ranges_[i].count=aligned(UINT(native_upload_ranges[i].bytes))/16;offset+=aligned(UINT(native_upload_ranges[i].bytes));}return true;
    }
    bool begin(void* cache,ID3D11DeviceContext* context,bool collect,bool focus){
        if(active_||!packed_||context!=nativeContext_||!cache)return false;
        cache_=cache;bindings_=nativeDevice_=nullptr;native_=nullptr;owner_=GetCurrentThreadId();active_=true;learned_=used_=false;collect_=collect;focus_=focus;
        if(collect_)++stats_.scopes;return true;
    }
    Result run(void* cache,void* device,ID3D11DeviceContext* context,void* bindings,NativeFn native){
        if(!native)return {};
        if(!active_||GetCurrentThreadId()!=owner_){native(cache,device,context,bindings);return {};}
        if(cache!=cache_||context!=nativeContext_||!bindings||(bindings_&&bindings!=bindings_)){
            failure("unexpected upload cache/context/bindings",7);
            end();native(cache,device,context,bindings);return {};
        }
        bindings_=bindings;nativeDevice_=device;native_=native;
        if(!learned_){native(cache,device,context,bindings);if(collect_)++stats_.warmups;learned_=learn();if(!learned_){if(collect_)++stats_.invalidations;end();}return {};}
        if(!unchanged()){if(collect_)++stats_.invalidations;const bool forwarded=used_;end();if(!forwarded)native(cache,device,context,bindings);return {};}
        unsigned dirty=0;uint64_t nativeBytes=0;
        for(size_t i=0;i<ranges_.size();++i)if(combined(i)&&at<uint8_t>(cache_,native_upload_ranges[i].dirty)){++dirty;nativeBytes+=native_upload_ranges[i].bytes;}
        if(!used_&&dirty<2){native(cache,device,context,bindings);if(collect_)++stats_.warmups;return {};}
        unsigned maps=0;
        for(size_t i=0;i<ranges_.size();++i)if(!combined(i)){const auto& n=native_upload_ranges[i];if(!at<uint8_t>(cache_,n.dirty))continue;
            D3D11_MAPPED_SUBRESOURCE map{};const auto hr=context_->Map(ranges_[i].original,0,D3D11_MAP_WRITE_DISCARD,0,&map);
            if(FAILED(hr)||!map.pData){if(SUCCEEDED(hr))context_->Unmap(ranges_[i].original,0);failure("large native CB Map failed",i,hr);if(collect_)++stats_.mapFailures;const bool forwarded=used_;end();if(!forwarded)native(cache,device,context,bindings);return {false,false,maps};}
            std::memcpy(map.pData,static_cast<const std::byte*>(cache_)+n.data,n.bytes);context_->Unmap(ranges_[i].original,0);put<uint8_t>(cache_,n.dirty,0);++maps;if(collect_)++stats_.largeMaps;
        }
        bool mapped=false;
        if(dirty||!used_){
            D3D11_MAPPED_SUBRESOURCE map{};const auto hr=context_->Map(packed_,0,D3D11_MAP_WRITE_DISCARD,0,&map);
            if(FAILED(hr)||!map.pData){if(SUCCEEDED(hr))context_->Unmap(packed_,0);failure("combined CB Map failed",7,hr);if(collect_)++stats_.mapFailures;const bool forwarded=used_;end();if(!forwarded)native(cache,device,context,bindings);return {false,false,maps};}
            for(size_t i=0;i<ranges_.size();++i)if(combined(i)){const auto& n=native_upload_ranges[i];auto* to=static_cast<std::byte*>(map.pData)+ranges_[i].first*16;
                std::memcpy(to,static_cast<const std::byte*>(cache_)+n.data,n.bytes);std::memset(to+n.bytes,0,ranges_[i].count*16-n.bytes);}
            context_->Unmap(packed_,0);mapped=true;++maps;
            for(size_t i=0;i<ranges_.size();++i)if(combined(i))put<uint8_t>(cache_,native_upload_ranges[i].dirty,0);
        }
        bool firstChanged=!used_;
        for(size_t i=0;i<ranges_.size();++i){const auto& r=ranges_[i];auto* wanted=combined(i)?packed_:r.original;
            if((combined(i)&&!used_)||at<ID3D11Buffer*>(bindings_,0x78+size_t(r.slot)*8)!=wanted){
                if(combined(i))context_->VSSetConstantBuffers1(r.slot,1,&packed_,&r.first,&r.count);else context_->VSSetConstantBuffers(r.slot,1,&r.original);
                put(bindings_,0x78+size_t(r.slot)*8,wanted);if(i==0)firstChanged=true;}}
        if(firstChanged)context_->PSSetConstantBuffers1(5,1,&packed_,&ranges_[0].first,&ranges_[0].count);
        used_=true;
        if(collect_){if(focus_)++stats_.focusCalls;else ++stats_.outerCalls;stats_.nativeMapsReplaced+=dirty;stats_.nativeBytesReplaced+=nativeBytes;if(mapped){++stats_.batchMaps;stats_.batchBytes+=packed_bytes();}}
        return {true,mapped,maps};
    }
    bool end(){
        if(active_&&GetCurrentThreadId()!=owner_)return false;
        if(active_)restore();active_=learned_=used_=false;cache_=bindings_=nativeDevice_=nullptr;native_=nullptr;owner_=0;return true;
    }
    bool reset(){
        if(!end())return false;for(auto& r:ranges_){if(r.original)r.original->Release();r={};}if(packed_)packed_->Release();packed_=nullptr;if(context_)context_->Release();context_=nullptr;nativeContext_=nullptr;stats_={};failureSerial_=0;failureReason_="none";failureRange_=0;failureHr_=S_OK;return true;
    }
    const Counters& counters()const{return stats_;}
    bool ready()const{return packed_!=nullptr;}
    uint64_t failure_serial()const{return failureSerial_;}
    const char* failure_reason()const{return failureReason_;}
    size_t failure_range()const{return failureRange_;}
    HRESULT failure_hr()const{return failureHr_;}
};
}
