#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "stereo_audit.hpp"
#include "stereo_instancing.hpp"
#include "stereo_draw_proof.hpp"
#include "diagnostics.hpp"
#include "caller_address.hpp"
#include "vendor/minhook-1.3.4/include/MinHook.h"
#include <d3dcompiler.h>
#include <wincrypt.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <limits>
#include <locale>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lmuqv { namespace {
constexpr unsigned framesWanted=2,maxDrawsPerPass=4096;
constexpr size_t maxShaderAliases=32768,maxShaderPrograms=8192;
constexpr size_t maxReflectionBytes=16*1024*1024,maxBytecodeBytes=64*1024*1024;
const GUID resourceGuid={0x8d9afcd1,0x561d,0x4724,{0xb2,0x13,0x28,0xfa,0x73,0x65,0x22,0x11}};
const GUID layoutGuid={0x8d9afcd2,0x561d,0x4724,{0xb2,0x13,0x28,0xfa,0x73,0x65,0x22,0x11}};
struct Tag {uint32_t id=0,version=0;};
struct Layout {uint32_t known=1,instanceSlots=0;uint64_t steps=0;};
struct Record {
    uint32_t slot=0,kind=0,args[5]{},bufferEpoch=0;int group=-1;uint64_t apiTicks=0,snapshotTicks=0;
    uint32_t shaders[5]{},layout=0,layoutKnown=0,instanceSlots=0;uint64_t layoutSteps=0,vertexSignature=0;
    Tag index{};uint32_t indexFormat=0,indexOffset=0,topology=0;
    std::array<Tag,28> constants{};uint64_t targetSignature=0;
    uint64_t pixelResources=0,pixelSamplers=0;uint32_t predicate=0,predicateValue=0;
    uint32_t rtWidth=0,rtHeight=0,rtFormat=0,rtSamples=0,rtCount=0,blend=0,classes=0;
    uint32_t nativeMesh=0,activeQueries=0,activeQueryMask=0;uint64_t nativeBindTicks=0,nativePrepareTicks=0,proofTicks=0;
};
struct Pass {uint64_t calls=0,dropped=0,wallTicks=0,snapshotTicks=0,apiTicks=0,maps=0,unmaps=0,updates=0,copies=0,dispatches=0,commandLists=0,nativeBinds=0,nativeBindTicks=0;};
// LMU creates many shader objects containing identical DXBC. Keep reflection
// and optional local bytecode once per program, with cheap object-ID aliases.
struct Program {std::string stage,sha,reflection;std::vector<BYTE> bytecode;};
struct LayoutDefinition {uint32_t id=0;std::string elements;};
std::recursive_mutex mutex;
std::atomic<bool> enabled{false};
ID3D11DeviceContext* selected=nullptr;ID3D11Device* device=nullptr;
std::atomic<DWORD> owner{0};unsigned installed=0,openScopes=0,nextId=1,completedFrames=0;
unsigned bufferEpoch=0,shaderDefinitionsDropped=0,layoutDefinitionsDropped=0;
size_t reflectionBytes=0,bytecodeBytes=0;unsigned bytecodeProgramsDropped=0;
bool captureBytecode=false,earlyDevice=false,proofRequested=false;
StereoDrawProof pairProof;
// Only timestamp-disjoint scopes are safe for extra private draws. Occlusion,
// statistics, counters and unknown types must retain their original results.
constexpr uint32_t timingQueryMask=1u<<D3D11_QUERY_TIMESTAMP_DISJOINT;
std::unordered_map<ID3D11Asynchronous*,uint32_t> activeQueries;
uint64_t proofCandidates=0,proofShaderRejected=0;std::array<uint64_t,2> proofEligible{};
uint32_t active_query_mask(){uint32_t mask=0;for(const auto& q:activeQueries)mask|=q.second;return mask;}
uint32_t query_mask(ID3D11Asynchronous* asynchronous){
    ID3D11Query* query=nullptr;if(!asynchronous||FAILED(asynchronous->QueryInterface(__uuidof(ID3D11Query),reinterpret_cast<void**>(&query))))return 1u<<31;
    D3D11_QUERY_DESC desc{};query->GetDesc(&desc);query->Release();return unsigned(desc.Query)<31?1u<<unsigned(desc.Query):1u<<31;
}
std::unordered_map<const void*,uint32_t> nativeMeshes;
std::atomic<ID3D11DeviceContext*> watchedContext{nullptr};std::atomic<ID3D11Device*> watchedDevice{nullptr};
std::atomic<bool> capturing{false};bool keyWasDown=false,sawScene=false,deferredStop=false;
long long started=0,frequency=0;std::filesystem::path directory;
void(*logger)(const char*)=nullptr;
StereoCapabilities capabilities;
std::array<Pass,framesWanted*4> passes{};std::vector<Record> records;
std::unordered_map<uint32_t,std::shared_ptr<Program>> shaders;
std::unordered_map<std::string,std::shared_ptr<Program>> programs;
std::vector<LayoutDefinition> layouts;
thread_local int activeSlot=-1,group=-1;thread_local bool suspended=false;
thread_local uint32_t nativeMesh=0;thread_local long long nativeStarted=0;
thread_local uint64_t nativeBindTicks=0;thread_local bool nativeFirstDraw=false;
// Context methods followed by device shader/input-layout creation methods.
constexpr unsigned contextCount=14,totalHooks=24;
constexpr auto indices=stereo_audit_vtable;
std::array<void*,totalHooks> targets{},originals{};
std::array<void*,2> factoryTargets{},factoryOriginals{};
unsigned factoryInstalled=0;bool factoryReady=false;
std::filesystem::path earlyDirectory;void(*earlyLogger)(const char*)=nullptr;
thread_local bool insideFactory=false;
long long ticks(){LARGE_INTEGER t{};QueryPerformanceCounter(&t);return t.QuadPart;}
void emit(const char* s){if(logger)logger(s);}
struct Suspend {bool prior=suspended;Suspend(){suspended=true;}~Suspend(){suspended=prior;}};
template<class Fn,unsigned Index> Fn original(){return reinterpret_cast<Fn>(originals[Index]);}
bool watched(ID3D11DeviceContext* c){return !suspended&&activeSlot>=0&&enabled.load(std::memory_order_relaxed)&&c==watchedContext.load(std::memory_order_relaxed);}
uint64_t mix(uint64_t h,uint64_t v){for(unsigned i=0;i<8;++i){h^=v&255;h*=1099511628211ull;v>>=8;}return h;}
Tag tag(ID3D11DeviceChild* child,bool write=false){
    if(!child)return {};
    Tag value;UINT bytes=sizeof(value);
    if(FAILED(child->GetPrivateData(resourceGuid,&bytes,&value))||bytes!=sizeof(value)){if(nextId==(std::numeric_limits<unsigned>::max)())return {};value={nextId++,0};child->SetPrivateData(resourceGuid,sizeof(value),&value);}
    if(write){++value.version;child->SetPrivateData(resourceGuid,sizeof(value),&value);}return value;
}
template<class T> void drop(T*& p){if(p){p->Release();p=nullptr;}}
std::string quoted(const char* value){
    std::string s="\"";for(const unsigned char* p=reinterpret_cast<const unsigned char*>(value?value:"");*p;++p){
        if(*p=='"'||*p=='\\'){s+='\\';s+=char(*p);}else if(*p<32){char b[7]{};std::snprintf(b,sizeof(b),"\\u%04x",unsigned(*p));s+=b;}else s+=char(*p);
    }return s+'"';
}
std::string sha256(const void* data,SIZE_T size){
    if(!data||!size||size>16*1024*1024)return {};
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;std::string result;
    if(CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)&&CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)&&CryptHashData(hash,static_cast<const BYTE*>(data),DWORD(size),0)){
        BYTE digest[32]{};DWORD bytes=sizeof(digest);if(CryptGetHashParam(hash,HP_HASHVAL,digest,&bytes,0)){const char* digits="0123456789abcdef";for(auto b:digest){result+=digits[b>>4];result+=digits[b&15];}}
    }
    if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);return result;
}
void remember_shader(ID3D11DeviceChild* child,const void* data,SIZE_T size,const char* stage){
    std::lock_guard<std::recursive_mutex> lock(mutex);if(!enabled)return;const auto id=tag(child).id;
    if(shaders.find(id)!=shaders.end())return;
    if(!id||shaders.size()>=maxShaderAliases){++shaderDefinitionsDropped;return;}
    const auto hash=sha256(data,size);if(hash.empty()){++shaderDefinitionsDropped;return;}
    const auto key=std::string(stage)+hash;const auto existing=programs.find(key);
    if(existing!=programs.end()){shaders.emplace(id,existing->second);return;}
    if(programs.size()>=maxShaderPrograms||reflectionBytes>maxReflectionBytes-32768){++shaderDefinitionsDropped;return;}
    auto program=std::make_shared<Program>();auto& s=*program;s.stage=stage;s.sha=hash;s.reflection="null";
    ID3D11ShaderReflection* reflection=nullptr;
    if(SUCCEEDED(D3DReflect(data,size,IID_ID3D11ShaderReflection,reinterpret_cast<void**>(&reflection)))){
        const std::unique_ptr<ID3D11ShaderReflection,void(*)(ID3D11ShaderReflection*)> release(reflection,[](ID3D11ShaderReflection* p){p->Release();});
        D3D11_SHADER_DESC d{};reflection->GetDesc(&d);std::ostringstream out;out.imbue(std::locale::classic());out<<"{\"inputs\":"<<d.InputParameters<<",\"outputs\":"<<d.OutputParameters<<",\"resources_total\":"<<d.BoundResources<<",\"constant_buffers\":[";bool first=true;
        for(UINT i=0;i<(std::min)(d.BoundResources,128u);++i){D3D11_SHADER_INPUT_BIND_DESC binding{};if(FAILED(reflection->GetResourceBindingDesc(i,&binding))||binding.Type!=D3D_SIT_CBUFFER)continue;
            if(!first)out<<',';first=false;D3D11_SHADER_BUFFER_DESC b{};auto* cb=reflection->GetConstantBufferByName(binding.Name);cb->GetDesc(&b);
            out<<"{\"slot\":"<<binding.BindPoint<<",\"name\":"<<quoted(binding.Name)<<",\"bytes\":"<<b.Size<<",\"variables_total\":"<<b.Variables<<",\"variables\":[";
            for(UINT j=0;j<(std::min)(b.Variables,64u);++j){if(j)out<<',';auto* variable=cb->GetVariableByIndex(j);D3D11_SHADER_VARIABLE_DESC v{};D3D11_SHADER_TYPE_DESC type{};variable->GetDesc(&v);variable->GetType()->GetDesc(&type);
                out<<"{\"name\":"<<quoted(v.Name)<<",\"offset\":"<<v.StartOffset<<",\"bytes\":"<<v.Size<<",\"class\":"<<unsigned(type.Class)<<",\"rows\":"<<type.Rows<<",\"columns\":"<<type.Columns<<'}';}
            out<<"]}";
        }out<<"],\"resources\":[";
        for(UINT i=0;i<(std::min)(d.BoundResources,128u);++i){D3D11_SHADER_INPUT_BIND_DESC binding{};reflection->GetResourceBindingDesc(i,&binding);if(i)out<<',';out<<"{\"name\":"<<quoted(binding.Name)<<",\"type\":"<<unsigned(binding.Type)<<",\"slot\":"<<binding.BindPoint<<",\"count\":"<<binding.BindCount<<'}';}
        out<<"]}";s.reflection=out.str();if(s.reflection.size()>32768||s.reflection.size()>maxReflectionBytes-reflectionBytes){s.reflection="null";++shaderDefinitionsDropped;}
    }
    reflectionBytes+=s.reflection.size();
    if(captureBytecode||proofRequested){
        if(size<=maxBytecodeBytes-bytecodeBytes){const auto* begin=static_cast<const BYTE*>(data);s.bytecode.assign(begin,begin+size);bytecodeBytes+=size;}
        else ++bytecodeProgramsDropped;
    }
    programs.emplace(key,program);shaders.emplace(id,std::move(program));
}
Record snapshot(ID3D11DeviceContext* c,unsigned kind,const std::array<UINT,5>& args){
    Suspend pause;Record r;r.slot=unsigned(activeSlot);r.kind=kind;r.group=group;r.bufferEpoch=bufferEpoch;std::copy(args.begin(),args.end(),r.args);
    r.nativeMesh=nativeMesh;r.nativeBindTicks=nativeBindTicks;r.activeQueries=unsigned(activeQueries.size());r.activeQueryMask=active_query_mask();
    ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;ID3D11GeometryShader* gs=nullptr;ID3D11HullShader* hs=nullptr;ID3D11DomainShader* ds=nullptr;
    UINT count=0;c->VSGetShader(&vs,nullptr,&count);r.classes+=count;count=0;c->PSGetShader(&ps,nullptr,&count);r.classes+=count;
    count=0;c->GSGetShader(&gs,nullptr,&count);r.classes+=count;count=0;c->HSGetShader(&hs,nullptr,&count);r.classes+=count;count=0;c->DSGetShader(&ds,nullptr,&count);r.classes+=count;
    r.shaders[0]=tag(vs).id;r.shaders[1]=tag(ps).id;r.shaders[2]=tag(gs).id;r.shaders[3]=tag(hs).id;r.shaders[4]=tag(ds).id;drop(vs);drop(ps);drop(gs);drop(hs);drop(ds);
    ID3D11InputLayout* layout=nullptr;c->IAGetInputLayout(&layout);r.layout=tag(layout).id;
    if(layout){Layout l{};UINT bytes=sizeof(l);if(SUCCEEDED(layout->GetPrivateData(layoutGuid,&bytes,&l))&&bytes==sizeof(l)){r.layoutKnown=l.known;r.instanceSlots=l.instanceSlots;r.layoutSteps=l.steps;}}drop(layout);
    D3D11_PRIMITIVE_TOPOLOGY topology{};c->IAGetPrimitiveTopology(&topology);r.topology=unsigned(topology);
    ID3D11Buffer* vb[32]{};UINT strides[32]{},offsets[32]{};c->IAGetVertexBuffers(0,32,vb,strides,offsets);r.vertexSignature=1469598103934665603ull;
    for(unsigned i=0;i<32;++i){auto t=tag(vb[i]);r.vertexSignature=mix(mix(mix(mix(r.vertexSignature,t.id),t.version),strides[i]),offsets[i]);drop(vb[i]);}
    ID3D11Buffer* ib=nullptr;DXGI_FORMAT format{};c->IAGetIndexBuffer(&ib,&format,&r.indexOffset);r.index=tag(ib);r.indexFormat=unsigned(format);drop(ib);
    ID3D11Buffer* cb[14]{};c->VSGetConstantBuffers(0,14,cb);for(unsigned i=0;i<14;++i){r.constants[i]=tag(cb[i]);drop(cb[i]);}
    c->PSGetConstantBuffers(0,14,cb);for(unsigned i=0;i<14;++i){r.constants[i+14]=tag(cb[i]);drop(cb[i]);}
    ID3D11ShaderResourceView* srvs[16]{};c->PSGetShaderResources(0,16,srvs);r.pixelResources=1469598103934665603ull;
    for(auto*& srv:srvs){ID3D11Resource* resource=nullptr;if(srv)srv->GetResource(&resource);const auto t=tag(resource);r.pixelResources=mix(mix(mix(r.pixelResources,tag(srv).id),t.id),t.version);drop(resource);drop(srv);}
    ID3D11SamplerState* samplers[16]{};c->PSGetSamplers(0,16,samplers);r.pixelSamplers=1469598103934665603ull;for(auto*& sampler:samplers){r.pixelSamplers=mix(r.pixelSamplers,tag(sampler).id);drop(sampler);}
    ID3D11Predicate* predicate=nullptr;BOOL predicateValue=FALSE;c->GetPredication(&predicate,&predicateValue);r.predicate=tag(predicate).id;r.predicateValue=unsigned(predicateValue);drop(predicate);
    ID3D11RenderTargetView* rt[8]{};ID3D11DepthStencilView* depth=nullptr;c->OMGetRenderTargets(8,rt,&depth);r.targetSignature=1469598103934665603ull;
    for(unsigned i=0;i<8;++i){r.targetSignature=mix(r.targetSignature,tag(rt[i]).id);if(rt[i])++r.rtCount;
        if(i==0&&rt[i]){ID3D11Resource* resource=nullptr;rt[i]->GetResource(&resource);ID3D11Texture2D* texture=nullptr;
            if(resource&&SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture)))){D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);r.rtWidth=d.Width;r.rtHeight=d.Height;r.rtFormat=unsigned(d.Format);r.rtSamples=d.SampleDesc.Count;drop(texture);}drop(resource);}drop(rt[i]);}
    r.targetSignature=mix(r.targetSignature,tag(depth).id);drop(depth);
    ID3D11BlendState* blend=nullptr;FLOAT factor[4]{};UINT mask=0;c->OMGetBlendState(&blend,factor,&mask);if(blend){D3D11_BLEND_DESC d{};blend->GetDesc(&d);r.blend=d.RenderTarget[0].BlendEnable?1:0;}drop(blend);return r;
}
void inspect_pair(ID3D11DeviceContext* c,const Record& r){
    if(!proofRequested||r.slot%4<2||r.group!=31||r.kind!=0||!r.nativeMesh||!r.layoutKnown||r.instanceSlots||r.blend||r.classes||r.predicate||r.rtSamples!=1||r.rtCount!=1||r.args[0]<128||r.shaders[2]||r.shaders[3]||r.shaders[4])return;
    ++proofCandidates;
    const auto vs=shaders.find(r.shaders[0]),ps=shaders.find(r.shaders[1]);if(vs==shaders.end()||ps==shaders.end())return;
    // Initial opaque PBR family only. Do not infer eligibility for SSR, mirrors,
    // transparency or other programs merely because their conversion succeeds.
    if(vs->second->sha!="a99b2cd689aa55f5f705b6ddd01d39db16d1dc1e83405446281d8d0f2f583d5d"||(ps->second->sha!="79bed2230b50db6c1f4db928a55f421f72f5ca19828aa2901d32583685554f56"&&ps->second->sha!="d41d95fc9c8fed235e1336ef9749f7aefc7ce6eb45c35b10e6fbd68b19eef76d")||vs->second->bytecode.empty()||ps->second->bytecode.empty()){++proofShaderRejected;return;}
    ++proofEligible[r.slot%4-2];
    // Keep frame identity in the key: a left-only object in one scene frame
    // must not pair with a right-only object from the next frame.
    Suspend pause;StereoProofDraw d;d.key=mix(mix(mix(mix(r.nativeMesh,r.vertexSignature),r.index.id),r.index.version),r.slot/4);d.indexCount=r.args[0];d.startIndex=r.args[1];d.baseVertex=INT(r.args[2]);d.vertexCode=vs->second->bytecode.data();d.vertexBytes=vs->second->bytecode.size();d.pixelCode=ps->second->bytecode.data();d.pixelBytes=ps->second->bytecode.size();d.vertexConstants=1|(1u<<4);d.pixelConstants=1|(1u<<3);
    pairProof.observe(c,r.slot%4==3,d,earlyDevice&&!(r.activeQueryMask&~timingQueryMask),bool(r.activeQueryMask&timingQueryMask));
}
template<class Call> void draw(ID3D11DeviceContext* c,unsigned kind,std::array<UINT,5> args,Call call){
    if(!watched(c)){call();return;}
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(!capturing||!enabled){call();return;}
    auto& p=passes[unsigned(activeSlot)];++p.calls;
    if(p.calls>maxDrawsPerPass){++p.dropped;Suspend pause;call();return;}
    const auto before=ticks();auto record=snapshot(c,kind,args);
    if(nativeFirstDraw&&nativeStarted){record.nativePrepareTicks=uint64_t(before-nativeStarted);nativeFirstDraw=false;}
    const auto proofStart=ticks();inspect_pair(c,record);const auto ready=ticks();record.proofTicks=uint64_t(ready-proofStart);
    {Suspend pause;call();}const auto after=ticks();record.snapshotTicks=uint64_t(ready-before);record.apiTicks=uint64_t(after-ready);p.snapshotTicks+=record.snapshotTicks;p.apiTicks+=record.apiTicks;records.push_back(record);
}
void STDMETHODCALLTYPE indexed(ID3D11DeviceContext* c,UINT n,UINT start,INT base){using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);draw(c,0,{n,start,UINT(base),0,0},[&]{original<Fn,0>()(c,n,start,base);});}
void STDMETHODCALLTYPE plain(ID3D11DeviceContext* c,UINT n,UINT start){using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);draw(c,1,{n,start,0,0,0},[&]{original<Fn,1>()(c,n,start);});}
void STDMETHODCALLTYPE indexed_instanced(ID3D11DeviceContext* c,UINT n,UINT instances,UINT start,INT base,UINT first){using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);draw(c,2,{n,instances,start,UINT(base),first},[&]{original<Fn,2>()(c,n,instances,start,base,first);});}
void STDMETHODCALLTYPE instanced(ID3D11DeviceContext* c,UINT n,UINT instances,UINT start,UINT first){using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);draw(c,3,{n,instances,start,first,0},[&]{original<Fn,3>()(c,n,instances,start,first);});}
void STDMETHODCALLTYPE automatic(ID3D11DeviceContext* c){using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);draw(c,4,{},[&]{original<Fn,4>()(c);});}
template<unsigned I> void STDMETHODCALLTYPE indirect(ID3D11DeviceContext* c,ID3D11Buffer* args,UINT offset){using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Buffer*,UINT);draw(c,I,{offset,0,0,0,0},[&]{original<Fn,I>()(c,args,offset);});}
void note(ID3D11DeviceContext* c,ID3D11Resource* resource,unsigned kind){
    if(suspended||!enabled||GetCurrentThreadId()!=owner||c!=watchedContext.load(std::memory_order_relaxed))return;
    std::lock_guard<std::recursive_mutex> lock(mutex);if(!capturing)return;
    if(resource)tag(resource,true);if(activeSlot>=0){auto& p=passes[unsigned(activeSlot)];if(kind==0)++p.maps;else if(kind==1)++p.unmaps;else if(kind==2)++p.updates;else if(kind==3)++p.copies;else ++p.dispatches;}
}
void STDMETHODCALLTYPE dispatch(ID3D11DeviceContext* c,UINT x,UINT y,UINT z){using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT);note(c,nullptr,4);original<Fn,7>()(c,x,y,z);}
void STDMETHODCALLTYPE dispatch_indirect(ID3D11DeviceContext* c,ID3D11Buffer* args,UINT offset){using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Buffer*,UINT);note(c,nullptr,4);original<Fn,8>()(c,args,offset);}
HRESULT STDMETHODCALLTYPE map(ID3D11DeviceContext* c,ID3D11Resource* resource,UINT sub,D3D11_MAP type,UINT flags,D3D11_MAPPED_SUBRESOURCE* result){
    using Fn=HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,D3D11_MAP,UINT,D3D11_MAPPED_SUBRESOURCE*);
    const auto hr=original<Fn,9>()(c,resource,sub,type,flags,result);if(SUCCEEDED(hr)&&type!=D3D11_MAP_READ)note(c,resource,0);return hr;
}
void STDMETHODCALLTYPE unmap(ID3D11DeviceContext* c,ID3D11Resource* resource,UINT sub){using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT);note(c,nullptr,1);original<Fn,10>()(c,resource,sub);}
void STDMETHODCALLTYPE update(ID3D11DeviceContext* c,ID3D11Resource* resource,UINT sub,const D3D11_BOX* box,const void* data,UINT row,UINT depth){using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,const D3D11_BOX*,const void*,UINT,UINT);original<Fn,11>()(c,resource,sub,box,data,row,depth);note(c,resource,2);}
void STDMETHODCALLTYPE copy(ID3D11DeviceContext* c,ID3D11Resource* dst,ID3D11Resource* src){using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,ID3D11Resource*);original<Fn,12>()(c,dst,src);note(c,dst,3);}
void STDMETHODCALLTYPE copy_region(ID3D11DeviceContext* c,ID3D11Resource* dst,UINT sub,UINT x,UINT y,UINT z,ID3D11Resource* src,UINT srcSub,const D3D11_BOX* box){using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,UINT,UINT,UINT,ID3D11Resource*,UINT,const D3D11_BOX*);original<Fn,13>()(c,dst,sub,x,y,z,src,srcSub,box);note(c,dst,3);}
template<class T,unsigned I> HRESULT STDMETHODCALLTYPE create_shader(ID3D11Device* d,const void* data,SIZE_T bytes,ID3D11ClassLinkage* linkage,T** result){
    using Fn=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,T**);
    const auto hr=original<Fn,I>()(d,data,bytes,linkage,result);
    if(!suspended&&enabled&&d==watchedDevice.load(std::memory_order_relaxed)&&SUCCEEDED(hr)&&result&&*result){try{Suspend pause;constexpr const char* names[]={"vs","ps","gs","hs","ds","cs"};remember_shader(*result,data,bytes,names[I-contextCount]);}catch(...){enabled=false;emit("stereo_audit disabled: shader metadata allocation failed; rendering remains legacy");}}return hr;
}
HRESULT STDMETHODCALLTYPE create_layout(ID3D11Device* d,const D3D11_INPUT_ELEMENT_DESC* elements,UINT count,const void* code,SIZE_T bytes,ID3D11InputLayout** result){
    using Fn=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const D3D11_INPUT_ELEMENT_DESC*,UINT,const void*,SIZE_T,ID3D11InputLayout**);
    const auto hr=original<Fn,20>()(d,elements,count,code,bytes,result);
    if(!suspended&&enabled&&d==watchedDevice.load(std::memory_order_relaxed)&&SUCCEEDED(hr)&&result&&*result)try{
        std::lock_guard<std::recursive_mutex> lock(mutex);if(!enabled)return hr;
        Layout l;l.steps=1469598103934665603ull;std::ostringstream definition;definition.imbue(std::locale::classic());definition<<'[';
        for(UINT i=0;i<count;++i){if(elements[i].InputSlotClass==D3D11_INPUT_PER_INSTANCE_DATA&&elements[i].InputSlot<32){l.instanceSlots|=1u<<elements[i].InputSlot;l.steps=mix(mix(l.steps,elements[i].InputSlot),elements[i].InstanceDataStepRate);}
            if(i)definition<<',';const auto& e=elements[i];definition<<"{\"semantic\":"<<quoted(e.SemanticName)<<",\"semantic_index\":"<<e.SemanticIndex<<",\"format\":"<<unsigned(e.Format)<<",\"slot\":"<<e.InputSlot<<",\"offset\":"<<e.AlignedByteOffset<<",\"classification\":"<<unsigned(e.InputSlotClass)<<",\"step_rate\":"<<e.InstanceDataStepRate<<'}';}
        definition<<']';(*result)->SetPrivateData(layoutGuid,sizeof(l),&l);
        const auto id=tag(*result).id;const auto known=std::find_if(layouts.begin(),layouts.end(),[&](const LayoutDefinition& d){return d.id==id;});
        if(known==layouts.end()){if(layouts.size()<8192)layouts.push_back({id,definition.str()});else ++layoutDefinitionsDropped;}
    }catch(...){enabled=false;emit("stereo_audit disabled: layout metadata allocation failed; rendering remains legacy");}
    return hr;
}
void STDMETHODCALLTYPE execute(ID3D11DeviceContext* c,ID3D11CommandList* list,BOOL restore){
    using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11CommandList*,BOOL);
    {Suspend pause;original<Fn,21>()(c,list,restore);}
    if(!suspended&&enabled&&GetCurrentThreadId()==owner&&c==watchedContext.load(std::memory_order_relaxed)){
        std::lock_guard<std::recursive_mutex> lock(mutex);if(capturing){++bufferEpoch;if(activeSlot>=0)++passes[unsigned(activeSlot)].commandLists;}}
}
void STDMETHODCALLTYPE begin_query(ID3D11DeviceContext* c,ID3D11Asynchronous* query){
    using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Asynchronous*);original<Fn,22>()(c,query);
    if(!suspended&&enabled&&c==watchedContext.load(std::memory_order_relaxed)){
        std::lock_guard<std::recursive_mutex> lock(mutex);if(enabled&&query)try{activeQueries[query]=query_mask(query);}catch(...){proofRequested=false;emit("stereo_pair_proof disabled: query tracking allocation failed");}}
}
void STDMETHODCALLTYPE end_query(ID3D11DeviceContext* c,ID3D11Asynchronous* query){
    using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Asynchronous*);original<Fn,23>()(c,query);
    if(!suspended&&enabled&&c==watchedContext.load(std::memory_order_relaxed)){std::lock_guard<std::recursive_mutex> lock(mutex);activeQueries.erase(query);}
}
const std::array<void*,totalHooks> detours{
    reinterpret_cast<void*>(&indexed),reinterpret_cast<void*>(&plain),reinterpret_cast<void*>(&indexed_instanced),reinterpret_cast<void*>(&instanced),reinterpret_cast<void*>(&automatic),reinterpret_cast<void*>(&indirect<5>),reinterpret_cast<void*>(&indirect<6>),reinterpret_cast<void*>(&dispatch),reinterpret_cast<void*>(&dispatch_indirect),reinterpret_cast<void*>(&map),reinterpret_cast<void*>(&unmap),reinterpret_cast<void*>(&update),reinterpret_cast<void*>(&copy),reinterpret_cast<void*>(&copy_region),
    reinterpret_cast<void*>(&create_shader<ID3D11VertexShader,14>),reinterpret_cast<void*>(&create_shader<ID3D11PixelShader,15>),reinterpret_cast<void*>(&create_shader<ID3D11GeometryShader,16>),reinterpret_cast<void*>(&create_shader<ID3D11HullShader,17>),reinterpret_cast<void*>(&create_shader<ID3D11DomainShader,18>),reinterpret_cast<void*>(&create_shader<ID3D11ComputeShader,19>),reinterpret_cast<void*>(&create_layout),reinterpret_cast<void*>(&execute),reinterpret_cast<void*>(&begin_query),reinterpret_cast<void*>(&end_query)};
bool main_caller(const void* caller){
    HMODULE module=nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(caller),&module)&&module==GetModuleHandleW(nullptr);
}
struct FactoryScope {bool previous=insideFactory;FactoryScope(){insideFactory=true;}~FactoryScope(){insideFactory=previous;}};
void factory_device(ID3D11Device* created,ID3D11DeviceContext* context){
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(!factoryReady||selected||!created)return;
    ID3D11DeviceContext* immediate=context;if(immediate)immediate->AddRef();else created->GetImmediateContext(&immediate);
    if(stereo_audit_configure(immediate,true,earlyDirectory.c_str(),earlyLogger)){
        earlyDevice=true;emit("stereo_audit early device interception active; observing shader and layout creation before xrCreateSession");
    }
    drop(immediate);
}
HRESULT WINAPI create_device(IDXGIAdapter* adapter,D3D_DRIVER_TYPE driver,HMODULE software,UINT flags,const D3D_FEATURE_LEVEL* levels,UINT count,UINT sdk,ID3D11Device** result,D3D_FEATURE_LEVEL* level,ID3D11DeviceContext** context){
    const bool observe=!insideFactory&&main_caller(LMU_CALLER_ADDRESS);FactoryScope scope;
    const auto fn=reinterpret_cast<decltype(&D3D11CreateDevice)>(factoryOriginals[0]);
    const auto hr=fn(adapter,driver,software,flags,levels,count,sdk,result,level,context);
    if(observe&&SUCCEEDED(hr)&&result&&*result)try{factory_device(*result,context?*context:nullptr);}catch(...){if(earlyLogger)earlyLogger("stereo_audit early device metadata unavailable; rendering remains legacy");}
    return hr;
}
HRESULT WINAPI create_device_swapchain(IDXGIAdapter* adapter,D3D_DRIVER_TYPE driver,HMODULE software,UINT flags,const D3D_FEATURE_LEVEL* levels,UINT count,UINT sdk,const DXGI_SWAP_CHAIN_DESC* desc,IDXGISwapChain** swapchain,ID3D11Device** result,D3D_FEATURE_LEVEL* level,ID3D11DeviceContext** context){
    const bool observe=!insideFactory&&main_caller(LMU_CALLER_ADDRESS);FactoryScope scope;
    const auto fn=reinterpret_cast<decltype(&D3D11CreateDeviceAndSwapChain)>(factoryOriginals[1]);
    const auto hr=fn(adapter,driver,software,flags,levels,count,sdk,desc,swapchain,result,level,context);
    if(observe&&SUCCEEDED(hr)&&result&&*result)try{factory_device(*result,context?*context:nullptr);}catch(...){if(earlyLogger)earlyLogger("stereo_audit early device metadata unavailable; rendering remains legacy");}
    return hr;
}
// Export only programs used in the sampled focus views. Never export the full
// startup shader cache, mapped memory, textures, or game code. Opt-in local files
// are named by stage/SHA and cannot contain supplied paths or machine identity.
bool export_program(const Program& p,std::string& relative){
    if(p.bytecode.empty())return false;
    relative="stereo-shaders/"+p.stage+"-"+p.sha+".dxbc";
    std::error_code error;std::filesystem::create_directory(directory/L"stereo-shaders",error);if(error)return false;
    const auto path=directory/std::filesystem::path(relative);
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){
        if(GetLastError()!=ERROR_FILE_EXISTS)return false;
        // Reuse an existing file only after verifying size and SHA, so an old or
        // partial file is never silently accepted or overwritten.
        file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
        LARGE_INTEGER size{};bool okay=GetFileSizeEx(file,&size)&&size.QuadPart==static_cast<long long>(p.bytecode.size());
        std::vector<BYTE> data;if(okay){try{data.resize(p.bytecode.size());}catch(...){CloseHandle(file);throw;}DWORD read=0;okay=ReadFile(file,data.data(),DWORD(data.size()),&read,nullptr)&&read==data.size();}
        CloseHandle(file);return okay&&sha256(data.data(),data.size())==p.sha;
    }
    DWORD written=0;const bool okay=WriteFile(file,p.bytecode.data(),DWORD(p.bytecode.size()),&written,nullptr)&&written==p.bytecode.size()&&FlushFileBuffers(file);CloseHandle(file);return okay;
}
void save_impl(const char* reason){
    if(!capturing)return;capturing=false;pairProof.finish();
    SYSTEMTIME time{};GetSystemTime(&time);wchar_t name[128]{};std::swprintf(name,std::size(name),L"stereo-audit-%04u%02u%02u-%02u%02u%02u-%llx.json",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,static_cast<unsigned long long>(started));
    const auto path=directory/name;
    std::set<uint32_t> usedShaders,usedLayouts,focusShaders;
    for(const auto& r:records){usedLayouts.insert(r.layout);for(auto id:r.shaders)if(id){usedShaders.insert(id);if(r.slot%4>=2)focusShaders.insert(id);}}
    std::unordered_map<const Program*,std::string> exported;unsigned exportFailures=0;
    if(captureBytecode)for(auto id:focusShaders){const auto found=shaders.find(id);if(found==shaders.end())continue;const auto* p=found->second.get();if(exported.find(p)!=exported.end())continue;
        std::string relative;if(!export_program(*p,relative)){relative.clear();++exportFailures;}exported.emplace(p,std::move(relative));}
    std::ostringstream out;out.imbue(std::locale::classic());out<<"{\"schema\":1,\"rendering\":\"legacy\",\"reason\":"<<quoted(reason)<<",\"frequency\":"<<frequency<<",\"scene_frames\":"<<completedFrames<<",\"open_scopes_at_save\":"<<openScopes<<",\"capacity_per_pass\":"<<maxDrawsPerPass<<",\"shader_definitions_dropped\":"<<shaderDefinitionsDropped<<",\"layout_definitions_dropped\":"<<layoutDefinitionsDropped<<",\"metadata_cache\":{\"shader_aliases\":"<<shaders.size()<<",\"unique_programs\":"<<programs.size()<<",\"reflection_bytes\":"<<reflectionBytes<<",\"early_device\":"<<unsigned(earlyDevice)<<"},\"shader_export\":{\"enabled\":"<<unsigned(captureBytecode)<<",\"cached_bytes\":"<<bytecodeBytes<<",\"programs_dropped\":"<<bytecodeProgramsDropped<<",\"write_failures\":"<<exportFailures<<"},\"capabilities\":{\"feature_level\":"<<unsigned(capabilities.featureLevel)<<",\"vprt\":"<<unsigned(capabilities.vertexArrayIndex)<<",\"route\":"<<quoted(stereo_route_name(capabilities.route))<<"},\"tracking\":\"owner-thread observed writes; CB signatures are resource identities and versions, not content hashes; existing shaders may lack definitions; indirect arguments are not read; PS SRVs/samplers cover slots 0-15; other-stage SRVs, UAV writes and mapped contents are not observed\",\"layouts\":[";
    bool firstLayout=true;for(const auto& l:layouts){if(!usedLayouts.count(l.id))continue;if(!firstLayout)out<<',';firstLayout=false;out<<"{\"id\":"<<l.id<<",\"elements\":"<<l.elements<<'}';}out<<"],\"shaders\":[";
    bool first=true;for(auto id:usedShaders){const auto found=shaders.find(id);if(found==shaders.end())continue;const auto& s=*found->second;if(!first)out<<',';first=false;out<<"{\"id\":"<<id<<",\"stage\":"<<quoted(s.stage.c_str())<<",\"sha256\":"<<quoted(s.sha.c_str())<<",\"reflection\":"<<s.reflection;
        const auto file=exported.find(&s);if(focusShaders.count(id)&&file!=exported.end()&&!file->second.empty())out<<",\"bytecode_file\":"<<quoted(file->second.c_str());out<<'}';}out<<"],\"passes\":[";
    for(unsigned i=0;i<passes.size();++i){if(i)out<<',';const auto& p=passes[i];out<<"{\"slot\":"<<i<<",\"draw_calls\":"<<p.calls<<",\"dropped\":"<<p.dropped<<",\"wall_ticks\":"<<p.wallTicks<<",\"snapshot_ticks\":"<<p.snapshotTicks<<",\"api_ticks\":"<<p.apiTicks<<",\"maps\":"<<p.maps<<",\"unmaps\":"<<p.unmaps<<",\"updates\":"<<p.updates<<",\"copies\":"<<p.copies<<",\"dispatches\":"<<p.dispatches<<",\"command_lists\":"<<p.commandLists<<",\"native_mesh_preparations\":"<<p.nativeBinds<<",\"native_prepare_ticks\":"<<p.nativeBindTicks<<'}';}
    out<<"],\"draws\":[";first=true;
    for(const auto& r:records){if(!first)out<<',';first=false;out<<"{\"slot\":"<<r.slot<<",\"kind\":"<<r.kind<<",\"group\":"<<r.group<<",\"args\":[";for(unsigned i=0;i<5;++i){if(i)out<<',';out<<r.args[i];}
        out<<"],\"shaders\":[";for(unsigned i=0;i<5;++i){if(i)out<<',';out<<r.shaders[i];}
        out<<"],\"layout\":"<<r.layout<<",\"layout_known\":"<<r.layoutKnown<<",\"instance_slots\":"<<r.instanceSlots<<",\"layout_steps\":"<<r.layoutSteps<<",\"vertex_signature\":"<<r.vertexSignature<<",\"index\":["<<r.index.id<<','<<r.index.version<<','<<r.indexFormat<<','<<r.indexOffset<<"],\"topology\":"<<r.topology<<",\"buffer_epoch\":"<<r.bufferEpoch<<",\"ps_srv_signature\":"<<r.pixelResources<<",\"ps_sampler_signature\":"<<r.pixelSamplers<<",\"predicate\":["<<r.predicate<<','<<r.predicateValue<<"],\"cb\":[";
        for(unsigned i=0;i<r.constants.size();++i){if(i)out<<',';out<<'['<<r.constants[i].id<<','<<r.constants[i].version<<']';}
        out<<"],\"target_signature\":"<<r.targetSignature<<",\"rt\":["<<r.rtWidth<<','<<r.rtHeight<<','<<r.rtFormat<<','<<r.rtSamples<<','<<r.rtCount<<"],\"blend\":"<<r.blend<<",\"class_instances\":"<<r.classes<<",\"api_ticks\":"<<r.apiTicks<<",\"snapshot_ticks\":"<<r.snapshotTicks<<",\"native_mesh\":"<<r.nativeMesh<<",\"native_prepare_ticks\":"<<r.nativeBindTicks<<",\"native_to_draw_ticks\":"<<r.nativePrepareTicks<<",\"active_queries\":"<<r.activeQueries<<",\"active_query_mask\":"<<r.activeQueryMask<<",\"proof_ticks\":"<<r.proofTicks<<'}';}
    const auto& proof=pairProof.result();
    out<<"],\"stereo_pair_proof\":{\"requested\":"<<unsigned(proofRequested)<<",\"status\":"<<quoted(proof.status.c_str())<<",\"reason\":"<<quoted(proof.reason.c_str())<<",\"route\":"<<quoted(proof.route.c_str());
    out<<",\"key\":"<<proof.key<<",\"observed\":"<<proof.observed<<",\"query_skips\":"<<proof.querySkips<<",\"timing_query_overlaps\":"<<proof.timingQueryOverlaps<<",\"opaque_candidates\":"<<proofCandidates<<",\"shader_rejections\":"<<proofShaderRejected;
    out<<",\"eligible_draws\":["<<proofEligible[0]<<','<<proofEligible[1]<<"],\"used_sampler_masks\":["<<proof.samplerSlots[0]<<','<<proof.samplerSlots[1]<<"],\"width\":"<<proof.width<<",\"height\":"<<proof.height<<",\"reference_depth_format\":"<<proof.depthFormat<<",\"native_cpu_preparation_shared\":false";
    out<<",\"depth_snapshots_queued\":["<<(proof.depthCopied[0]?"true":"false")<<','<<(proof.depthCopied[1]?"true":"false")<<"],\"native_depth\":[";
    for(unsigned eye=0;eye<2;++eye){if(eye)out<<',';const auto& d=proof.nativeDepth[eye];
        out<<"{\"enabled\":"<<d[0]<<",\"write_mask\":"<<d[1]<<",\"comparison\":"<<d[2]<<",\"stencil_enabled\":"<<d[3]<<",\"view_flags\":"<<d[4]<<'}';}
    out<<"],\"coverage_method\":\"occlusion_passed_samples\",\"paired_samples\":"<<proof.pairedSamples<<",\"covered\":["<<proof.covered[0]<<','<<proof.covered[1]<<"],\"color_different\":["<<proof.colorDifferent[0]<<','<<proof.colorDifferent[1];
    out<<"],\"depth_different\":["<<proof.depthDifferent[0]<<','<<proof.depthDifferent[1]<<"],\"stencil_different\":["<<proof.stencilDifferent[0]<<','<<proof.stencilDifferent[1]<<"],\"maximum_color_error\":"<<proof.maximumColorError<<",\"maximum_depth_error\":"<<proof.maximumDepthError<<"}}\n";
    const auto data=out.str();HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){emit("stereo_audit save failed; rendering remains legacy");return;}
    DWORD written=0;const bool okay=data.size()<(std::numeric_limits<DWORD>::max)()&&WriteFile(file,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(file);CloseHandle(file);
    char line[640]{};std::snprintf(line,sizeof(line),"stereo_audit %s reason=%s frames=%u records=%zu file=%s; diagnostic overhead is included; game rendering remains legacy",okay?"complete":"save_failed",reason,completedFrames,records.size(),path.filename().u8string().c_str());emit(line);
    if(proofRequested){std::snprintf(line,sizeof(line),"stereo_pair_proof status=%s route=%s covered=%llu/%llu color_different=%llu/%llu depth_different=%llu/%llu reason=%s; private targets only; native CPU sharing disabled",proof.status.c_str(),proof.route.c_str(),static_cast<unsigned long long>(proof.covered[0]),static_cast<unsigned long long>(proof.covered[1]),static_cast<unsigned long long>(proof.colorDifferent[0]),static_cast<unsigned long long>(proof.colorDifferent[1]),static_cast<unsigned long long>(proof.depthDifferent[0]),static_cast<unsigned long long>(proof.depthDifferent[1]),proof.reason.c_str());emit(line);}
}
void save(const char* reason){try{save_impl(reason);}catch(...){capturing=false;emit("stereo_audit save failed: serialization allocation error; rendering remains legacy");}}
void cleanup_methods(){
    watchedContext=nullptr;watchedDevice=nullptr;for(unsigned i=0;i<installed;++i)MH_DisableHook(targets[i]);for(unsigned i=0;i<installed;++i)MH_RemoveHook(targets[i]);installed=0;drop(selected);drop(device);deferredStop=false;
    shaders.clear();programs.clear();layouts.clear();records.clear();earlyDevice=false;
    pairProof.reset();activeQueries.clear();nativeMeshes.clear();
}
void cleanup_factories(){
    factoryReady=false;for(unsigned i=0;i<factoryInstalled;++i)MH_DisableHook(factoryTargets[i]);for(unsigned i=0;i<factoryInstalled;++i)MH_RemoveHook(factoryTargets[i]);factoryInstalled=0;
    earlyDirectory.clear();earlyLogger=nullptr;
}
void cleanup(){cleanup_methods();cleanup_factories();}
} // namespace
bool stereo_audit_enabled(){return enabled.load(std::memory_order_relaxed);}
bool stereo_audit_prepare(bool enable,const wchar_t* output,void(*log)(const char*)){
    stereo_audit_stop();std::lock_guard<std::recursive_mutex> lock(mutex);logger=log;
    if(installed||factoryInstalled||!enable||!diagnostics_enabled())return false;
    std::error_code error;earlyDirectory=output?output:L"";
    if(!earlyDirectory.is_absolute()||!std::filesystem::is_directory(earlyDirectory,error)||error){emit("stereo_audit early interception unavailable: existing absolute output directory required");return false;}
    const auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)return false;
    const auto module=GetModuleHandleW(L"d3d11.dll");if(!module)return false;
    factoryTargets={reinterpret_cast<void*>(GetProcAddress(module,"D3D11CreateDevice")),reinterpret_cast<void*>(GetProcAddress(module,"D3D11CreateDeviceAndSwapChain"))};
    const std::array<void*,2> factoryDetours{reinterpret_cast<void*>(&create_device),reinterpret_cast<void*>(&create_device_swapchain)};
    for(unsigned i=0;i<2;++i){if(!factoryTargets[i]||MH_CreateHook(factoryTargets[i],factoryDetours[i],&factoryOriginals[i])!=MH_OK){cleanup_factories();emit("stereo_audit early factory hook creation failed; late session capture remains available");return false;}++factoryInstalled;}
    earlyLogger=log;factoryReady=true;
    for(unsigned i=0;i<2;++i)if(MH_EnableHook(factoryTargets[i])!=MH_OK){cleanup_factories();emit("stereo_audit early factory hook activation failed; late session capture remains available");return false;}
    emit("stereo_audit early factory interception armed; only main-executable D3D11 device creation qualifies");return true;
}
bool stereo_audit_configure(ID3D11DeviceContext* context,bool enable,const wchar_t* output,void(*log)(const char*)){
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(!enable||!diagnostics_enabled()){stereo_audit_stop();logger=log;return false;}
    if(enabled&&context==selected&&output&&directory==std::filesystem::path(output)){
        logger=log;emit("stereo_audit session confirmed existing device; preserving early shader/layout definitions");return true;
    }
    enabled=false;save("reconfigure");logger=log;
    if(openScopes){deferredStop=true;return false;}cleanup_methods();
    std::error_code error;directory=output?output:L"";
    if(!context||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||!directory.is_absolute()||!std::filesystem::is_directory(directory,error)||error){emit("stereo_audit unavailable: immediate context and existing absolute output directory required");return false;}
    selected=context;selected->AddRef();selected->GetDevice(&device);owner=0;passes={};keyWasDown=false;openScopes=0;shaderDefinitionsDropped=layoutDefinitionsDropped=0;bufferEpoch=0;
    reflectionBytes=bytecodeBytes=0;bytecodeProgramsDropped=0;
    char shaderSetting[8]{};captureBytecode=GetEnvironmentVariableA("LMU_QV_STEREO_SHADER_CAPTURE",shaderSetting,sizeof(shaderSetting))==1&&shaderSetting[0]=='1';
    char proofSetting[8]{};proofRequested=GetEnvironmentVariableA("LMU_QV_STEREO_DRAW_PROOF",proofSetting,sizeof(proofSetting))==1&&proofSetting[0]=='1';
    LARGE_INTEGER f{};QueryPerformanceFrequency(&f);frequency=f.QuadPart;if(!frequency){cleanup_methods();return false;}
    capabilities=query_stereo_capabilities(device);char line[256]{};std::snprintf(line,sizeof(line),"portable_stereo capabilities feature_level=0x%x vprt=%u route=%s; synthetic backend available, LMU rendering remains legacy",unsigned(capabilities.featureLevel),unsigned(capabilities.vertexArrayIndex),stereo_route_name(capabilities.route));emit(line);
    const auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED){cleanup_methods();return false;}
    auto** contextTable=*reinterpret_cast<void***>(selected);auto** deviceTable=*reinterpret_cast<void***>(device);
    for(unsigned i=0;i<totalHooks;++i){targets[i]=(i<contextCount||i>=21?contextTable:deviceTable)[indices[i]];for(unsigned j=0;j<i;++j)if(targets[j]==targets[i]){emit("stereo_audit unavailable: aliased method targets");cleanup_methods();return false;}}
    for(unsigned i=0;i<totalHooks;++i){if(MH_CreateHook(targets[i],detours[i],&originals[i])!=MH_OK){emit("stereo_audit unavailable: method hook creation failed");cleanup_methods();return false;}++installed;}
    for(unsigned i=0;i<totalHooks;++i)if(MH_EnableHook(targets[i])!=MH_OK){emit("stereo_audit unavailable: method hook activation failed");cleanup_methods();return false;}
    watchedContext=selected;watchedDevice=device;enabled=true;emit("stereo_audit armed hotkey=Ctrl+Alt+F8 scene_frames=2 max_draws_per_pass=4096; heavy diagnostic capture, not a performance benchmark; rendering remains legacy");return true;
}
void stereo_audit_frame(bool keyDown){
    if(!enabled)return;std::lock_guard<std::recursive_mutex> lock(mutex);if(!enabled)return;
    if(!owner)owner=GetCurrentThreadId();if(owner!=GetCurrentThreadId()||openScopes)return;
    const bool pressed=keyDown&&!keyWasDown;keyWasDown=keyDown;if(!pressed)return;
    if(capturing){save("hotkey");return;}
    try{records.clear();records.reserve(framesWanted*4*maxDrawsPerPass);}catch(...){enabled=false;emit("stereo_audit disabled: capture allocation failed; rendering remains legacy");return;}
    passes={};completedFrames=0;sawScene=false;started=ticks();nativeMeshes.clear();proofCandidates=proofShaderRejected=0;proofEligible={};capturing=true;
    if(proofRequested)try{pairProof.begin();}catch(...){proofRequested=false;emit("stereo_pair_proof unavailable: allocation failed; ordinary audit retained");}
    emit("stereo_audit start; collecting two scene frames, no game draw suppression or stereo conversion");
}
StereoAuditToken stereo_audit_begin(StereoAuditPass pass){
    if(!enabled||unsigned(pass)>=4||suspended)return {};
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(!capturing||GetCurrentThreadId()!=owner||completedFrames>=framesWanted||activeSlot>=0)return {};
    activeSlot=int(completedFrames*4+unsigned(pass));group=-1;nativeMesh=0;nativeStarted=0;nativeBindTicks=0;nativeFirstDraw=false;++openScopes;sawScene=true;
    return {true,unsigned(activeSlot),ticks()};
}
void stereo_audit_end(StereoAuditToken& token){
    if(!token.entered)return;std::lock_guard<std::recursive_mutex> lock(mutex);
    if(capturing)passes[token.slot].wallTicks+=uint64_t(ticks()-token.started);
    activeSlot=-1;group=-1;if(openScopes)--openScopes;token.entered=false;
    if(deferredStop&&!openScopes)cleanup();
}
int stereo_audit_group(int value){if(!enabled)return -1;const auto previous=group;group=value;nativeMesh=0;nativeStarted=0;nativeBindTicks=0;nativeFirstDraw=false;return previous;}
StereoNativePrepareToken stereo_audit_native_prepare_begin(const void* mesh){
    if(!capturing.load(std::memory_order_relaxed)||activeSlot<0||suspended||!mesh||GetCurrentThreadId()!=owner)return {};
    std::lock_guard<std::recursive_mutex> lock(mutex);if(!enabled||!capturing)return {};
    try{const auto known=nativeMeshes.find(mesh);if(known!=nativeMeshes.end())nativeMesh=known->second;else{if(nativeMeshes.size()>=32768)return {};nativeMesh=uint32_t(nativeMeshes.size()+1);nativeMeshes.emplace(mesh,nativeMesh);}}
    catch(...){nativeMesh=0;return {};}
    nativeStarted=ticks();nativeBindTicks=0;nativeFirstDraw=true;return {true,unsigned(activeSlot),nativeStarted};
}
void stereo_audit_native_prepare_end(const StereoNativePrepareToken& token){
    if(!token.entered)return;std::lock_guard<std::recursive_mutex> lock(mutex);if(!enabled||!capturing)return;
    nativeBindTicks=uint64_t(ticks()-token.started);auto& p=passes[token.slot];++p.nativeBinds;p.nativeBindTicks+=nativeBindTicks;
}
void stereo_audit_frame_end(){
    if(!enabled)return;std::lock_guard<std::recursive_mutex> lock(mutex);if(!capturing||GetCurrentThreadId()!=owner)return;
    if(sawScene&&!openScopes){++completedFrames;sawScene=false;if(completedFrames==framesWanted){save("frames");return;}}
    if(ticks()-started>10*frequency)save("timeout");
}
void stereo_audit_stop(){
    std::lock_guard<std::recursive_mutex> lock(mutex);enabled=false;save("session_end");
    if(openScopes){deferredStop=true;emit("stereo_audit cleanup deferred until active render scope exits");return;}cleanup();
}
}
