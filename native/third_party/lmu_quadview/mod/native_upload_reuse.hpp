#pragma once
#include <d3d11.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lmuqv {
// Fixed layout verified in the pinned native VS constant-upload routine. These
// are ordinary CPU shadows, never mapped GPU WRITE memory. The native binding
// routine does not allocate/replace buffers or modify these payloads/dirty flags.
struct NativeUploadRange {size_t buffer,data,bytes,dirty;};
inline constexpr std::array<NativeUploadRange,7> native_upload_ranges{{
    {0,8,272,0x11c},{0x120,0x128,2624,0xb6c},{0xb70,0xb78,2624,0x15bc},
    {0x15c0,0x15c8,2688,0x204c},{0x2050,0x2058,640,0x22dc},
    {0x22e0,0x22e8,544,0x250c},{0x2510,0x2518,192,0x25dc}
}};
class NativeUploadReuse {
    struct Last {ID3D11Buffer* buffer=nullptr;bool known=false,aliased=false,supported=false;std::array<std::byte,2688> data{};};
    std::array<Last,7> last_{};void* cache_=nullptr;ID3D11DeviceContext* context_=nullptr;DWORD owner_=0;bool collect_=false,topologyDirty_=true;
    template<class T>static T at(const void* base,size_t offset){T value{};std::memcpy(&value,static_cast<const std::byte*>(base)+offset,sizeof(value));return value;}
public:
    struct Counters {uint64_t examined=0,skipped=0,bytesSkipped=0,learned=0,aliased=0,invalidated=0,topologyChecks=0;};
    struct Token {unsigned dirty=0,skipped=0;bool valid=false;};
private:
    Counters stats_{};
public:
    NativeUploadReuse()=default;~NativeUploadReuse(){reset();}
    NativeUploadReuse(const NativeUploadReuse&)=delete;NativeUploadReuse& operator=(const NativeUploadReuse&)=delete;
    void begin(void* cache,ID3D11DeviceContext* context,bool collect){
        cache_=cache;context_=context;owner_=GetCurrentThreadId();collect_=collect;topologyDirty_=true;
        for(auto& last:last_){last.known=false;last.aliased=false;}
    }
    void end(){cache_=nullptr;context_=nullptr;owner_=0;for(auto& last:last_)last.known=false;}
    void reset(){end();for(auto& last:last_){if(last.buffer)last.buffer->Release();last.buffer=nullptr;last.aliased=false;}stats_={};}
    const Counters& counters()const{return stats_;}
    Token before(void* cache,ID3D11DeviceContext* context){
        if(!cache||cache!=cache_||context!=context_||GetCurrentThreadId()!=owner_)return {};
        Token token;token.valid=true;
        std::array<ID3D11Buffer*,7> current{};
        for(size_t i=0;i<current.size();++i){const auto& r=native_upload_ranges[i];current[i]=at<ID3D11Buffer*>(cache,r.buffer);auto& last=last_[i];
            if(current[i]!=last.buffer){topologyDirty_=true;if(last.buffer)last.buffer->Release();last.buffer=current[i];last.supported=false;if(last.buffer){last.buffer->AddRef();D3D11_BUFFER_DESC d{};last.buffer->GetDesc(&d);last.supported=d.ByteWidth==r.bytes&&d.BindFlags==D3D11_BIND_CONSTANT_BUFFER&&d.Usage==D3D11_USAGE_DYNAMIC&&d.CPUAccessFlags==D3D11_CPU_ACCESS_WRITE&&!d.MiscFlags;}last.known=false;last.aliased=false;if(collect_)++stats_.invalidated;}}
        // Two cache fields sharing a GPU buffer invalidate byte-only ownership.
        // Keep both native uploads in that case, even if one payload is unchanged.
        // Resource identity was checked above on every call. Retained COM refs
        // prevent address reuse, so unchanged identities need no repeated 21-pair
        // ownership scan. begin() forces a scan after clearing scope alias state.
        if(topologyDirty_){
            if(collect_)++stats_.topologyChecks;
            for(size_t i=0;i<current.size();++i)for(size_t j=0;j<i;++j)if(current[i]&&current[i]==current[j]){
                if(collect_&&!last_[i].aliased)++stats_.aliased;
                last_[i].aliased=last_[j].aliased=true;last_[i].known=last_[j].known=false;
            }
            topologyDirty_=false;
        }
        for(size_t i=0;i<current.size();++i){const auto& r=native_upload_ranges[i];auto& last=last_[i];
            if(!current[i]||!at<uint8_t>(cache,r.dirty))continue;
            token.dirty|=1u<<i;if(collect_)++stats_.examined;
            if(last.supported&&last.known&&!last.aliased&&!std::memcmp(last.data.data(),static_cast<const std::byte*>(cache)+r.data,r.bytes)){
                *reinterpret_cast<uint8_t*>(static_cast<std::byte*>(cache)+r.dirty)=0;token.skipped|=1u<<i;if(collect_){++stats_.skipped;stats_.bytesSkipped+=r.bytes;}
            }
        }return token;
    }
    void after(void* cache,ID3D11DeviceContext* context,const Token& token){
        if(!token.valid||cache!=cache_||context!=context_||GetCurrentThreadId()!=owner_)return;
        for(size_t i=0;i<last_.size();++i)if((token.dirty&(1u<<i))&&!(token.skipped&(1u<<i))){const auto& r=native_upload_ranges[i];auto& last=last_[i];
            if(last.supported&&!last.aliased&&last.buffer&&at<ID3D11Buffer*>(cache,r.buffer)==last.buffer&&!at<uint8_t>(cache,r.dirty)){
                std::memcpy(last.data.data(),static_cast<const std::byte*>(cache)+r.data,r.bytes);last.known=true;if(collect_)++stats_.learned;
            }else last.known=false;
        }
    }
};
}
