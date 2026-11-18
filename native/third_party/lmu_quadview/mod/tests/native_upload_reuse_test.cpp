#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../native_upload_reuse.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
namespace {
template<class T>void drop(T*& p){if(p)p->Release();p=nullptr;}
struct Fixture {
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    std::array<std::byte,0x2660> cache{},other{};std::array<ID3D11Buffer*,7> buffers{};
    unsigned uploads=0,bindings=0;lmuqv::NativeUploadReuse reuse;
    Fixture(bool hardware){D3D_FEATURE_LEVEL level{};assert(SUCCEEDED(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
        for(size_t i=0;i<buffers.size();++i){const auto& r=lmuqv::native_upload_ranges[i];buffers[i]=create(UINT(r.bytes));std::memcpy(cache.data()+r.buffer,&buffers[i],8);for(size_t j=0;j<r.bytes;++j)cache[r.data+j]=std::byte((i*13+j)&255);}}
    ID3D11Buffer* create(UINT bytes){D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.Usage=D3D11_USAGE_DYNAMIC;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;ID3D11Buffer* b=nullptr;assert(SUCCEEDED(device->CreateBuffer(&d,nullptr,&b)));return b;}
    void dirty(){for(const auto& r:lmuqv::native_upload_ranges)cache[r.dirty]=std::byte(1);}
    void native(bool fail=false){
        // Models the verified routine's binding and complete writes, without
        // reading mapped WRITE memory. A failed upload leaves its dirty flag.
        for(size_t i=0;i<buffers.size();++i){const auto& r=lmuqv::native_upload_ranges[i];ID3D11Buffer* b=nullptr;std::memcpy(&b,cache.data()+r.buffer,8);context->VSSetConstantBuffers(UINT(i),1,&b);++bindings;
            if(b&&cache[r.dirty]!=std::byte(0)&&!fail){D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(context->Map(b,0,D3D11_MAP_WRITE_DISCARD,0,&map)));std::memcpy(map.pData,cache.data()+r.data,r.bytes);context->Unmap(b,0);cache[r.dirty]=std::byte(0);++uploads;}}
    }
    void run(bool fail=false){const auto token=reuse.before(cache.data(),context);native(fail);reuse.after(cache.data(),context,token);}
    void verify(unsigned i){const auto& r=lmuqv::native_upload_ranges[i];ID3D11Buffer* b=nullptr;std::memcpy(&b,cache.data()+r.buffer,8);D3D11_BUFFER_DESC desc{};b->GetDesc(&desc);desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Buffer* copy=nullptr;assert(SUCCEEDED(device->CreateBuffer(&desc,nullptr,&copy)));context->CopyResource(copy,b);D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(context->Map(copy,0,D3D11_MAP_READ,0,&map)));assert(!std::memcmp(map.pData,cache.data()+r.data,r.bytes));context->Unmap(copy,0);drop(copy);}
    ~Fixture(){reuse.reset();context->ClearState();for(auto*& b:buffers)drop(b);drop(context);drop(device);}
};
void topology_cache_test(bool hardware){
    Fixture f(hardware);
    const auto& second=lmuqv::native_upload_ranges[1];
    const auto& third=lmuqv::native_upload_ranges[2];
    f.reuse.begin(f.cache.data(),f.context,true);f.dirty();f.run();
    assert(f.uploads==7&&f.reuse.counters().topologyChecks==1);
    for(unsigned i=0;i<16;++i){f.dirty();f.run();}
    assert(f.uploads==7&&f.reuse.counters().topologyChecks==1);
    // A payload edit does not change resource ownership or need another scan.
    f.cache[second.data]^=std::byte(1);f.dirty();f.run();f.verify(1);
    assert(f.uploads==8&&f.reuse.counters().topologyChecks==1);
    // Introduce an alias after both independent resources were already learned.
    std::memcpy(f.cache.data()+third.buffer,&f.buffers[1],sizeof(void*));
    f.dirty();f.run();assert(f.uploads==10&&f.reuse.counters().topologyChecks==2);f.verify(2);
    f.dirty();f.run();assert(f.uploads==12&&f.reuse.counters().topologyChecks==2);f.verify(2);
    // Removing the alias must not revive stale GPU-content assumptions. The
    // unchanged formerly aliased resource stays conservative until scope reset.
    std::memcpy(f.cache.data()+third.buffer,&f.buffers[2],sizeof(void*));
    f.dirty();f.run();assert(f.uploads==14&&f.reuse.counters().topologyChecks==3);
    f.verify(1);f.verify(2);
    f.dirty();f.run();assert(f.uploads==15&&f.reuse.counters().topologyChecks==3);
    f.reuse.end();f.reuse.begin(f.cache.data(),f.context,true);f.dirty();f.run();
    assert(f.uploads==22&&f.reuse.counters().topologyChecks==4);
    f.dirty();f.run();assert(f.uploads==22&&f.reuse.counters().topologyChecks==4);
    // begin() must scan an unchanged identity layout again after clearing aliases.
    std::memcpy(f.cache.data()+third.buffer,&f.buffers[1],sizeof(void*));
    f.dirty();f.run();assert(f.reuse.counters().topologyChecks==5);
    f.reuse.end();f.reuse.begin(f.cache.data(),f.context,true);f.dirty();f.run();
    const auto uploads=f.uploads;f.dirty();f.run();
    assert(f.uploads==uploads+2&&f.reuse.counters().topologyChecks==6);f.verify(2);
    f.reuse.reset();f.reuse.begin(f.cache.data(),f.context,false);f.dirty();f.run();
    f.dirty();f.run();assert(f.reuse.counters().topologyChecks==0);
}
}
int main(int argc,char** argv){
    topology_cache_test(argc>1&&!std::strcmp(argv[1],"--hardware"));
    bool hardware=argc>1&&!std::strcmp(argv[1],"--hardware");Fixture f(hardware);f.reuse.begin(f.cache.data(),f.context,true);f.dirty();f.run();assert(f.uploads==7);f.dirty();f.run();assert(f.uploads==7&&f.bindings==14&&f.reuse.counters().skipped==7);for(unsigned i=0;i<7;++i)f.verify(i);
    // A changed byte anywhere in the payload forces the real upload.
    const auto& first=lmuqv::native_upload_ranges[0];f.cache[first.data+first.bytes-1]^=std::byte(1);f.dirty();f.run();assert(f.uploads==8);f.verify(0);
    // Same CPU bytes on a different GPU buffer must be uploaded.
    auto* replacement=f.create(UINT(first.bytes));std::memcpy(f.cache.data()+first.buffer,&replacement,8);f.dirty();f.run();assert(f.uploads==9);f.verify(0);std::memcpy(f.cache.data()+first.buffer,&f.buffers[0],8);drop(replacement);
    // A new render group invalidates prior GPU-content assumptions.
    f.reuse.end();f.reuse.begin(f.cache.data(),f.context,true);f.dirty();f.run();assert(f.uploads==16);
    f.dirty();auto t=f.reuse.before(f.other.data(),f.context);assert(!t.valid);f.native();f.reuse.after(f.other.data(),f.context,t);assert(f.uploads==23);
    // Invalidate before other native writers, as the integration does.
    f.reuse.end();f.reuse.begin(f.cache.data(),f.context,true);f.dirty();f.run();assert(f.uploads==30);
    f.reuse.end();f.reuse.begin(f.cache.data(),f.context,true);f.dirty();f.run(true);const auto old=f.uploads;f.run();assert(f.uploads==old+7);
    // Two fields writing the same resource never qualify as exclusive owners.
    const auto& second=lmuqv::native_upload_ranges[1];const auto& third=lmuqv::native_upload_ranges[2];std::memcpy(f.cache.data()+third.buffer,&f.buffers[1],8);f.reuse.end();f.reuse.begin(f.cache.data(),f.context,true);f.dirty();f.run();const auto aliased=f.uploads;f.dirty();f.run();assert(f.uploads==aliased+2&&f.reuse.counters().aliased);f.verify(2);std::memcpy(f.cache.data()+third.buffer,&f.buffers[2],8);
    // Diagnostics-off still performs the optimization without collecting stats.
    f.reuse.reset();f.reuse.begin(f.cache.data(),f.context,false);f.dirty();f.run();const auto quiet=f.uploads;f.dirty();f.run();assert(f.uploads==quiet&&f.reuse.counters().skipped==0&&f.reuse.counters().examined==0);
    (void)second;std::cout<<"Native upload reuse "<<(hardware?"hardware":"WARP")<<" passed: byte-exact GPU readback, skipped maps, all native bindings retained, data/buffer changes, scope invalidation, failed writes, aliases and diagnostics-off\n";
}
