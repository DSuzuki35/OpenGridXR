#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../native_upload_batch.hpp"
#include <d3dcompiler.h>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
namespace {
template<class T>void drop(T*& p){if(p)p->Release();p=nullptr;}
template<class T>void put(void* p,size_t offset,T value){std::memcpy(static_cast<std::byte*>(p)+offset,&value,sizeof(value));}
template<class T>T get(const void* p,size_t offset){T value{};std::memcpy(&value,static_cast<const std::byte*>(p)+offset,sizeof(value));return value;}
ID3DBlob* compile(const char* text,const char* profile){ID3DBlob *code=nullptr,*error=nullptr;const auto hr=D3DCompile(text,std::strlen(text),nullptr,nullptr,nullptr,"main",profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);if(error)std::cerr<<static_cast<char*>(error->GetBufferPointer());drop(error);assert(SUCCEEDED(hr));return code;}
struct Fixture {
    ID3D11Device* device=nullptr;ID3D11DeviceContext *context=nullptr;ID3D11DeviceContext1* context1=nullptr;
    ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;ID3D11Texture2D *color=nullptr,*readback=nullptr;ID3D11RenderTargetView* rtv=nullptr;ID3D11Buffer* sentinel=nullptr;
    std::array<std::byte,0x2660> cache{};std::array<std::byte,0x200> bindings{};std::vector<ID3D11Buffer*> owned;
    lmuqv::NativeUploadBatch batch;uint64_t nativeMaps=0;
    static constexpr UINT slots[7]{0,7,8,9,4,6,5};
    Fixture(bool hardware){
        D3D_FEATURE_LEVEL level{};assert(SUCCEEDED(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
        assert(SUCCEEDED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(&context1))));assert(batch.configure(context));
        for(size_t i=0;i<slotsCount();++i){const auto& r=lmuqv::native_upload_ranges[i];auto* b=buffer(UINT(r.bytes));put(cache.data(),r.buffer,b);put(cache.data(),r.dirty-4,slots[i]);}
        auto* code=compile("cbuffer A:register(b0){float4 a[17];};cbuffer B:register(b7){float4 b[164];};cbuffer C:register(b8){float4 c[164];};cbuffer D:register(b9){float4 d[168];};cbuffer E:register(b4){float4 e[40];};cbuffer F:register(b6){float4 f[34];};cbuffer G:register(b5){float4 g[12];};cbuffer H:register(b12){float4 h;};struct Out{float4 p:SV_Position;float4 v:TEXCOORD0;};Out main(uint id:SV_VertexID){Out o;o.p=float4(id==2?3:-1,id==1?3:-1,0,1);o.v=(a[16]+b[163]+c[163]+d[167]+e[39]+f[33]+g[11])*0.125+h;return o;}","vs_5_0");
        assert(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vs)));drop(code);
        code=compile("cbuffer Alias:register(b5){float4 m[17];};cbuffer S:register(b6){float4 s;};float4 main(float4 p:SV_Position,float4 v:TEXCOORD0):SV_Target{return v+m[16]*.25+s;}","ps_5_0");assert(SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&ps)));drop(code);
        sentinel=buffer(16);const float v[4]{.125f,.25f,.375f,.5f};D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(context->Map(sentinel,0,D3D11_MAP_WRITE_DISCARD,0,&map)));std::memcpy(map.pData,v,16);context->Unmap(sentinel,0);
        context->VSSetConstantBuffers(12,1,&sentinel);context->PSSetConstantBuffers(6,1,&sentinel);
        D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;assert(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&color)));assert(SUCCEEDED(device->CreateRenderTargetView(color,nullptr,&rtv)));td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;assert(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&readback)));
        context->VSSetShader(vs,nullptr,0);context->PSSetShader(ps,nullptr,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);D3D11_VIEWPORT vp{0,0,1,1,0,1};context->RSSetViewports(1,&vp);context->OMSetRenderTargets(1,&rtv,nullptr);
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ID3D11RasterizerState* rs=nullptr;assert(SUCCEEDED(device->CreateRasterizerState(&rd,&rs)));context->RSSetState(rs);drop(rs);fill(0x7f,1);
    }
    static constexpr size_t slotsCount(){return lmuqv::native_upload_ranges.size();}
    ID3D11Buffer* buffer(UINT bytes){D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.Usage=D3D11_USAGE_DYNAMIC;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;ID3D11Buffer* b=nullptr;assert(SUCCEEDED(device->CreateBuffer(&d,nullptr,&b)));owned.push_back(b);return b;}
    void fill(unsigned mask,unsigned seed){
        for(size_t i=0;i<slotsCount();++i)if(mask&(1u<<i)){const auto& r=lmuqv::native_upload_ranges[i];for(size_t j=0;j<r.bytes/4;++j){const float value=float(seed*3+i*7+j%11)/128;put(cache.data(),r.data+j*4,value);}cache[r.dirty]=std::byte(1);}
    }
    static void native(void* raw,void* tag,ID3D11DeviceContext* c,void* binding){
        auto& f=*static_cast<Fixture*>(tag);
        for(size_t i=0;i<slotsCount();++i){const auto& r=lmuqv::native_upload_ranges[i];auto* b=get<ID3D11Buffer*>(raw,r.buffer);const auto slot=get<UINT>(raw,r.dirty-4);
            if(get<ID3D11Buffer*>(binding,0x78+slot*8)!=b){c->VSSetConstantBuffers(slot,1,&b);put(binding,0x78+slot*8,b);if(i==0)c->PSSetConstantBuffers(5,1,&b);}
            if(b&&get<uint8_t>(raw,r.dirty)){D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(c->Map(b,0,D3D11_MAP_WRITE_DISCARD,0,&map)));std::memcpy(map.pData,static_cast<const std::byte*>(raw)+r.data,r.bytes);c->Unmap(b,0);put<uint8_t>(raw,r.dirty,0);++f.nativeMaps;}}
    }
    lmuqv::NativeUploadBatch::Result run(){const auto r=batch.run(cache.data(),this,context,bindings.data(),native);nativeMaps+=r.maps;return r;}
    std::array<uint8_t,16> image(){
        const float zero[4]{};context->ClearRenderTargetView(rtv,zero);context->Draw(3,0);context->CopyResource(readback,color);D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(context->Map(readback,0,D3D11_MAP_READ,0,&map)));std::array<uint8_t,16> data{};std::memcpy(data.data(),map.pData,16);context->Unmap(readback,0);return data;
    }
    void verifyNative(){
        for(size_t i=0;i<slotsCount();++i){const auto& r=lmuqv::native_upload_ranges[i];auto* expected=get<ID3D11Buffer*>(cache.data(),r.buffer);const auto slot=get<UINT>(cache.data(),r.dirty-4);ID3D11Buffer* actual=nullptr;UINT first=99,count=0;context1->VSGetConstantBuffers1(slot,1,&actual,&first,&count);assert(actual==expected&&first==0);drop(actual);
            if(!expected)continue;D3D11_BUFFER_DESC d{};expected->GetDesc(&d);d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Buffer* read=nullptr;assert(SUCCEEDED(device->CreateBuffer(&d,nullptr,&read)));context->CopyResource(read,expected);D3D11_MAPPED_SUBRESOURCE m{};assert(SUCCEEDED(context->Map(read,0,D3D11_MAP_READ,0,&m)));assert(std::memcmp(m.pData,cache.data()+r.data,r.bytes)==0);context->Unmap(read,0);drop(read);}
        ID3D11Buffer *a=nullptr,*b=nullptr,*s=nullptr;context->PSGetConstantBuffers(5,1,&a);context->VSGetConstantBuffers(12,1,&b);context->PSGetConstantBuffers(6,1,&s);assert(a==get<ID3D11Buffer*>(cache.data(),0)&&b==sentinel&&s==sentinel);drop(a);drop(b);drop(s);
    }
    ~Fixture(){assert(batch.reset());context->ClearState();for(auto*& b:owned)drop(b);drop(rtv);drop(readback);drop(color);drop(vs);drop(ps);drop(context1);drop(context);drop(device);}
};
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);const bool hardware=argc>1&&!std::strcmp(argv[1],"--hardware");
    Fixture reference(hardware),test(hardware);assert(!test.batch.begin(nullptr,test.context,true,true));assert(test.batch.begin(test.cache.data(),test.context,true,true));
    for(unsigned frame=1;frame<=32;++frame){const unsigned mask=frame<8?0x7f:frame%4==0?0:frame%2?1u:0x24u;reference.fill(mask,frame);test.fill(mask,frame);Fixture::native(reference.cache.data(),&reference,reference.context,reference.bindings.data());const auto result=test.run();assert(result.batched==(frame>1));const auto expected=reference.image();float red=0;std::memcpy(&red,expected.data(),4);assert(red>.25f);assert(expected==test.image());}
    const auto before=test.nativeMaps;assert(test.batch.end());assert(test.nativeMaps==before+4);test.verifyNative();assert(reference.image()==test.image());
    const auto stats=test.batch.counters();assert(stats.focusCalls==31&&stats.batchMaps<stats.nativeMapsReplaced&&stats.restoreMaps==4&&stats.batchBytes>0);
    // An ordinary caller changes one binding through the native weak cache.
    assert(test.batch.begin(test.cache.data(),test.context,true,false));test.fill(0x7f,40);test.run();test.fill(0x7f,41);assert(test.run().batched);
    auto* original=get<ID3D11Buffer*>(test.cache.data(),lmuqv::native_upload_ranges[2].buffer);test.context->VSSetConstantBuffers(Fixture::slots[2],1,&original);put(test.bindings.data(),0x78+Fixture::slots[2]*8,original);assert(test.run().batched);assert(test.batch.end());test.verifyNative();
    // Mid-scope replacement restores native buffers before the replacement draw.
    assert(test.batch.begin(test.cache.data(),test.context,true,true));test.run();test.fill(0x7f,50);assert(test.run().batched);const auto& replaced=lmuqv::native_upload_ranges[1];auto* replacement=test.buffer(UINT(replaced.bytes));put(test.cache.data(),replaced.buffer,replacement);test.fill(2,51);assert(!test.run().batched);test.verifyNative();assert(test.batch.end());
    // Slot changes, aliases and nulls must retain native behavior rather than
    // continuing with stale range bindings. Unsupported scopes never batch.
    put(test.cache.data(),replaced.dirty-4,UINT(11));assert(test.batch.begin(test.cache.data(),test.context,true,true));test.fill(0x7f,60);test.run();test.fill(0x7f,61);assert(test.run().batched);put(test.cache.data(),replaced.dirty-4,UINT(7));test.fill(0x7f,62);assert(!test.run().batched);test.verifyNative();test.batch.end();
    auto* saved=get<ID3D11Buffer*>(test.cache.data(),lmuqv::native_upload_ranges[2].buffer);put(test.cache.data(),lmuqv::native_upload_ranges[2].buffer,replacement);assert(test.batch.begin(test.cache.data(),test.context,true,true));test.fill(0x7f,70);assert(!test.run().batched);assert(!test.run().batched);test.batch.end();put(test.cache.data(),lmuqv::native_upload_ranges[2].buffer,saved);
    assert(test.batch.begin(test.cache.data(),test.context,true,true));test.fill(0x7f,80);test.run();test.fill(0x7f,81);assert(test.run().batched);bool crossThread=true;std::thread close([&]{crossThread=test.batch.end();});close.join();assert(!crossThread);assert(test.batch.end());test.verifyNative();
    assert(test.batch.reset()&&test.batch.configure(test.context));assert(test.batch.begin(test.cache.data(),test.context,false,true));test.fill(0x7f,90);test.run();test.fill(0x7f,91);assert(test.run().batched);test.batch.end();test.verifyNative();assert(test.batch.counters().batchMaps==0&&test.batch.counters().scopes==0);
    {
        Fixture combinedReference(hardware),combined(hardware);lmuqv::NativeUploadReuse filter;
        assert(combined.batch.begin(combined.cache.data(),combined.context,true,false));filter.begin(combined.cache.data(),combined.context,true);
        for(unsigned frame=1;frame<=48;++frame){
            const unsigned changed=frame<3?0x7f:1u|(frame%5==0?8u:0u)|(frame%7==0?0x40u:0u);combinedReference.fill(changed,frame);combined.fill(changed,frame);
            for(const auto& n:lmuqv::native_upload_ranges){combinedReference.cache[n.dirty]=std::byte(1);combined.cache[n.dirty]=std::byte(1);}
            Fixture::native(combinedReference.cache.data(),&combinedReference,combinedReference.context,combinedReference.bindings.data());
            const auto token=filter.before(combined.cache.data(),combined.context);combined.run();filter.after(combined.cache.data(),combined.context,token);
            assert(combinedReference.image()==combined.image());
        }
        assert(combined.batch.end());filter.end();combined.verifyNative();assert(filter.counters().skipped>100&&combined.batch.counters().outerCalls>0);
        assert(combined.nativeMaps<combinedReference.nativeMaps);
        std::cout<<"Combined filter + batch: native Map calls "<<combinedReference.nativeMaps<<" -> "<<combined.nativeMaps<<" including restoration; shader output byte-identical PASS\n";
    }
    std::cout<<"Native upload batch "<<(hardware?"hardware":"WARP")<<" PASS: byte-identical rendered shader output, all seven CB windows, PS b5 alias, unrelated bindings, native GPU data/offset restoration, buffer/slot changes, aliases, owner thread and diagnostics-off; packed bytes="<<lmuqv::NativeUploadBatch::packed_bytes()<<'\n';
}
