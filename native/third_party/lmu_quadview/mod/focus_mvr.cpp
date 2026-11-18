#define NOMINMAX
#include "focus_mvr.hpp"
#include "bridge.hpp"
#include "diagnostics.hpp"
#include "caller_address.hpp"
#include "nv_multiview_backend.hpp"
#include "vendor/sim_qvfr/dxbc_patch.h"
#include "vendor/minhook-1.3.4/include/MinHook.h"
#include <wrl/client.h>
#include <atomic>
#include <utility>
#include <d3dcompiler.h>
#include <d3d11_3.h>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#ifdef LMU_MVR_SHADOW_PROBE
#include "focus_mvr_shadow_probe.hpp"
#endif

namespace lmuqv::focus_mvr {
namespace {
using Microsoft::WRL::ComPtr;
using C=ID3D11DeviceContext;
using VsFn=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11VertexShader**);
using PsFn=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11PixelShader**);
using ResolveFn=void(STDMETHODCALLTYPE*)(C*,ID3D11Resource*,UINT,ID3D11Resource*,UINT,DXGI_FORMAT);
using DrawFn=void(STDMETHODCALLTYPE*)(C*,UINT,UINT);
using IndexedFn=void(STDMETHODCALLTYPE*)(C*,UINT,UINT,INT);
using InstancedFn=void(STDMETHODCALLTYPE*)(C*,UINT,UINT,UINT,UINT);
using IndexedInstancedFn=void(STDMETHODCALLTYPE*)(C*,UINT,UINT,UINT,INT,UINT);
using IndirectFn=void(STDMETHODCALLTYPE*)(C*,ID3D11Buffer*,UINT);
using AutoFn=void(STDMETHODCALLTYPE*)(C*);
using ListFn=void(STDMETHODCALLTYPE*)(C*,ID3D11CommandList*,BOOL);
ResolveFn resolve=nullptr;DrawFn draw=nullptr;IndexedFn indexed=nullptr;
InstancedFn instanced=nullptr;IndexedInstancedFn indexedInstanced=nullptr;
IndirectFn indirect=nullptr,indexedIndirect=nullptr;AutoFn automatic=nullptr;ListFn execute=nullptr;
constexpr GUID twinId={0x7468456a,0xf4af,0x49b6,{0x88,0x11,0x42,0x93,0xe7,0x0d,0x81,0x22}};
constexpr GUID instancedTwinId={0x9d8abcd1,0xff16,0x4aa7,{0xaa,0x70,0x0c,0x27,0xac,0xae,0x21,0xe1}};
constexpr GUID vertexOnlyId={0x54940d87,0xcc07,0x4981,{0x83,0x17,0x2f,0x48,0x96,0xeb,0x15,0xb1}};
constexpr GUID doubledLayoutId={0x29c1e63e,0xdf86,0x41aa,{0x94,0x26,0x4a,0x04,0xd3,0x74,0xa0,0x13}};
std::atomic<bool> instancedBackend{false};
const GUID& shaderTwinKey(){return instancedBackend.load()?instancedTwinId:twinId;}
constexpr GUID pixelScreenId={0x88fccc2f,0x5019,0x4132,{0xb9,0xaf,0xf2,0xec,0xf1,0x31,0x2e,0xa1}};
constexpr GUID pixelId={0xade5c22f,0x3880,0x4852,{0xb9,0x75,0xd2,0xea,0xcf,0x22,0x4c,0x51}};
struct PixelInputs { unsigned textures=0, irradiance=0; };
GUID binding_name_key(UINT slot){
    return {0x93378330u+slot,0x1f12,0x4e31,{0x84,0xc2,0x0c,0x19,0x15,0x90,0x08,0x0f}};
}
struct State {
    ComPtr<ID3D11Device> device;
    ComPtr<C> ctx;
    ComPtr<ID3D11Buffer> cb;
    ComPtr<ID3D11Texture2D> outerImage,focusImage,hdr,resolved,focusHdr,focusResolved;
    ComPtr<ID3D11RenderTargetView> color;
    ComPtr<ID3D11DepthStencilView> depth;
    ComPtr<ID3D11Texture2D> auxiliary[2][3];
    bool bindMode=false, compareFocus=false;
    unsigned checked[2]{}, accepted[2]{}, observedResolves{};
    struct ValidationKey {
        ComPtr<ID3D11Texture2D> atlas, nativeHdr, nativeResolve;
        unsigned w{},h{},fw{},fh{};
    } validated[2];
    ComPtr<ID3D11Texture2D> atlasResolve;
    const void* focusSite[2]{};
    bool validatedNativeDestination[2]{};
    unsigned focusResolves{};
    bool staleInputs=false;
    const char* refusal=nullptr;
    unsigned w=0,h=0,fw=0,fh=0;
    UINT uavSlots=D3D11_PS_CS_UAV_REGISTER_COUNT;
    bool complete=false,haveFocus=false,replaced=false,broken=false,clearedColor=false,clearedDepth=false;
    unsigned paired=0,eye=0,misses[2]{};
    FocusRetry retry[2];
    unsigned long long outerPasses=0,focusSkips=0;
    char refusalDetail[320]{};
    char fallbackReason[513]{};
    unsigned long long fallbackCount[2]{},missingValidation[2]{};
    std::array<unsigned,4> rejectedAtlas[2]{};
} s;
std::atomic<bool> fallbackPending{false};
void refuse(const char* reason){
    if(!s.refusal)s.refusal=reason;
    s.complete=false;s.haveFocus=false;
}
void report_stopped(unsigned eye,const char* reason){
    std::snprintf(s.fallbackReason,sizeof(s.fallbackReason),"%s eye: %s",eye?"Right":"Left",reason);
    fallbackPending.store(true,std::memory_order_release);
    char line[768]{};
    std::snprintf(line,sizeof(line),
        "focus_mvr stopped eye=%u reason=%s outer_passes=%llu focus_reused=%llu fallbacks=%llu/%llu missing_validation=%llu/%llu validated=%u/%u outer=%ux%u focus=%ux%u",
        eye,reason,s.outerPasses,s.focusSkips,s.fallbackCount[0],s.fallbackCount[1],
        s.missingValidation[0],s.missingValidation[1],s.accepted[0],s.accepted[1],s.w,s.h,s.fw,s.fh);
    log_line(line);
}
void stop_reuse(const char* reason){
    if(s.broken)return;
    s.broken=true;
    refuse(reason);
    report_stopped(s.eye,reason);
}
thread_local bool outer=false,focus=false,lens=false;
std::atomic<ID3D11Device*> shaderDevice{nullptr};
thread_local unsigned geometryDepth=0;
using ClearColorFn=void(STDMETHODCALLTYPE*)(C*,ID3D11RenderTargetView*,const FLOAT*);
using ClearDepthFn=void(STDMETHODCALLTYPE*)(C*,ID3D11DepthStencilView*,UINT,FLOAT,UINT8);
ClearColorFn clearColor=nullptr;ClearDepthFn clearDepth=nullptr;
std::recursive_mutex lifecycle;
// Device and shader creation can outlive an XR session. Disable these detours
// immediately at teardown, but keep their originals callable until every
// creation already inside has returned. Draw/bind hooks never take this lock.
std::atomic<uint64_t> creationGeneration{1};
struct CreationHook {
    void *entry{}, *trampoline{};
    unsigned callers{};
    bool observing{}, disabled{};

    void retire() {
        if (!trampoline || observing) return;
        if (!disabled) {
            const auto status=MH_DisableHook(entry);
            disabled=status==MH_OK || status==MH_ERROR_DISABLED;
        }
        if (disabled && !callers && MH_RemoveHook(entry)==MH_OK) trampoline=nullptr;
    }
    bool install(void* target,void* detour) {
        if (!target || trampoline || callers) return false;
        // A thread can be suspended at the very first detour instruction, before
        // acquiring a lease. Its forwarding code must survive a loader unload.
        static const bool resident=[]{
            HMODULE owner{};
            return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&creationGeneration),&owner)!=FALSE;
        }();
        if (!resident || MH_CreateHook(target,detour,&trampoline)!=MH_OK) return false;
        entry=target;
        disabled=true;
        if (MH_EnableHook(entry)!=MH_OK) { retire(); return false; }
        observing=true;
        disabled=false;
        return true;
    }
} deviceFactory, chainFactory, vertexCreation, pixelCreation, layoutCreation;
struct CreationCall {
    CreationHook& hook;
    const uint64_t generation=creationGeneration.load();
    void* original{};
    bool leased{};
    CreationCall(CreationHook& h,ID3D11Device* device=nullptr,unsigned slot=0):hook(h) {
        std::lock_guard<std::recursive_mutex> lock(lifecycle);
        // Factory exports are fixed. A stale COM detour can instead belong to
        // a retired device implementation, so forward through its own vtable.
        const auto target=device?(*reinterpret_cast<void***>(device))[slot]:h.entry;
        leased=h.trampoline && h.entry==target;
        original=leased?h.trampoline:target;
        // Also reserve a direct call whose detour was removed before this lock:
        // re-hooking the same native entry before it is called would send the
        // expired operation through a newly armed session.
        ++h.callers;
    }
    ~CreationCall() {
        std::lock_guard<std::recursive_mutex> lock(lifecycle);
        --hook.callers;
        hook.retire();
    }
    bool current() const { return leased && generation==creationGeneration.load(); }
    template<class F> void publish(F write) const {
        std::lock_guard<std::recursive_mutex> lock(lifecycle);
        if (current() && hook.observing) write();
    }
};
bool retiringCreationHooks() {
    for (auto* hook:{&deviceFactory,&chainFactory,&vertexCreation,&pixelCreation,&layoutCreation})
        if ((hook->trampoline || hook->callers) && !hook->observing) return true;
    return false;
}
using DispatchFn=void(STDMETHODCALLTYPE*)(C*,UINT,UINT,UINT);
DispatchFn dispatch=nullptr;IndirectFn dispatchIndirect=nullptr;
using QueryFn=void(STDMETHODCALLTYPE*)(C*,ID3D11Asynchronous*);
QueryFn beginQuery=nullptr,endQuery=nullptr;
std::atomic<unsigned> countingQueries{0};
void* hooks[32]{};unsigned hookCount=0;
const auto& api(){return nv_multiview::driver_render_api();}
// LMU often recreates the same shader during loading. Reuse the driver object
// for identical DXBC, but never across devices. Confirm the bytes as well as the
// DXBC checksum prefix so a collision cannot substitute another game's shader.
struct CachedTwin {
    std::vector<uint8_t> dxbc;
    ComPtr<ID3D11VertexShader> shader;
};
std::mutex twinCacheMutex;
std::unordered_map<uint64_t,CachedTwin> twinCache;
size_t cachedByteCount=0;

ComPtr<ID3D11VertexShader> make_twin(ID3D11Device* dev,const void* code,SIZE_T size){
    if (instancedBackend.load()) {
        const auto variant = simqvfr::dxbc::add_instanced_view(static_cast<const uint8_t*>(code), size);
        ComPtr<ID3D11VertexShader> twin;
        if (!variant.bytecode.empty())
            dev->CreateVertexShader(variant.bytecode.data(), variant.bytecode.size(), nullptr, &twin);
        return twin;
    }
        auto variant=simqvfr::dxbc::add_second_view(static_cast<const uint8_t*>(code),size);
        if(!variant.bytecode.empty()){
            NV_CUSTOM_SEMANTIC semantics[2]{};
            for(auto& sem:semantics){sem.version=NV_CUSTOM_SEMANTIC_VERSION;sem.RegisterSpecified=TRUE;}
            semantics[0].NVCustomSemanticType=NV_XYZW_RIGHT_SEMANTIC;
            semantics[0].RegisterNum=variant.rightRegister;semantics[0].RegisterMask=15;
            std::strcpy(semantics[0].NVCustomSemanticNameString,simqvfr::dxbc::kRightSemantic);
            semantics[1].NVCustomSemanticType=NV_VIEWPORT_MASK_SEMANTIC;
            semantics[1].RegisterNum=variant.maskRegister;semantics[1].RegisterMask=1;
            std::strcpy(semantics[1].NVCustomSemanticNameString,simqvfr::dxbc::kMaskSemantic);
            NvAPI_D3D11_CREATE_VERTEX_SHADER_EX ex{};
            ex.version=NVAPI_D3D11_CREATEVERTEXSHADEREX_VERSION;ex.NumCustomSemantics=2;ex.pCustomSemantics=semantics;
            ComPtr<ID3D11VertexShader> twin;
            if(api().vertex(dev,variant.bytecode.data(),variant.bytecode.size(),nullptr,&ex,&twin)==NVAPI_OK&&twin)
                return twin;
        }
    return {};
}

ComPtr<ID3D11VertexShader> find_or_create_twin(ID3D11Device* dev,const void* code,SIZE_T size){
    const auto* bytes=static_cast<const uint8_t*>(code);
    if(size<20||std::memcmp(code,"DXBC",4)!=0)return {};
    uint64_t key{};
    std::memcpy(&key,bytes+4,sizeof(key));
    {
        std::lock_guard<std::mutex> lock(twinCacheMutex);
        const auto found=twinCache.find(key);
        if(shaderDevice.load()!=dev)return {};
        if(found!=twinCache.end()&&found->second.dxbc.size()==size&&
           std::memcmp(bytes,found->second.dxbc.data(),size)==0)return found->second.shader;
    }
    // Keep driver work outside the cache lock. Concurrent first-time requests
    // can convert twice, and subsequent requests reuse the stored result.
    auto twin=make_twin(dev,code,size);
    if(twin){
        std::lock_guard<std::mutex> lock(twinCacheMutex);
        if(shaderDevice.load()==dev&&twinCache.size()<256&&size<=4*1024*1024-cachedByteCount&&
           twinCache.find(key)==twinCache.end()){
            twinCache.emplace(key,CachedTwin{{bytes,bytes+size},twin});
            cachedByteCount+=size;
        }
    }
    return twin;
}
HRESULT STDMETHODCALLTYPE on_create(ID3D11Device* dev,const void* code,SIZE_T size,ID3D11ClassLinkage* linkage,ID3D11VertexShader** vs){
    CreationCall call(vertexCreation,dev,12);
    const HRESULT hr=reinterpret_cast<VsFn>(call.original)(dev,code,size,linkage,vs);
    static thread_local bool creating=false;
    if(FAILED(hr)||creating||!call.current()||shaderDevice.load()!=dev||linkage||!vs||!*vs||!code)return hr;
    creating=true;
    try {
#ifdef LMU_MVR_SHADOW_PROBE
        call.publish([&]{shadow_probe::tag(*vs,code,size);});
#endif
        auto twin=find_or_create_twin(dev,code,size);
        if(twin)call.publish([&]{(*vs)->SetPrivateDataInterface(shaderTwinKey(),twin.Get());});
    } catch(...) { /* Native shader creation already succeeded. */ }
    creating=false;
    return hr;
}
// A displaced viewport changes SV_Position.xy in the pixel shader. The same-eye
// twin shares every other varying. Reject screen-position consumers and dynamic
// linkage. Render-target/depth SRVs are checked at draw time.
HRESULT STDMETHODCALLTYPE on_create_ps(ID3D11Device* dev,const void* code,SIZE_T size,ID3D11ClassLinkage* linkage,ID3D11PixelShader** ps){
    CreationCall call(pixelCreation,dev,15);
    const HRESULT hr=reinterpret_cast<PsFn>(call.original)(dev,code,size,linkage,ps);
    if(FAILED(hr)||!call.current()||shaderDevice.load()!=dev||linkage||!ps||!*ps||!code)return hr;
#ifdef LMU_MVR_SHADOW_PROBE
    call.publish([&]{shadow_probe::tag(*ps,code,size);});
#endif
    ComPtr<ID3D11ShaderReflection> reflect;
    D3D11_SHADER_DESC desc{};
    if(FAILED(D3DReflect(code,size,IID_ID3D11ShaderReflection,reinterpret_cast<void**>(reflect.GetAddressOf())))||
       FAILED(reflect->GetDesc(&desc)))return hr;
    for(UINT n=0;n<desc.InputParameters;++n){
        D3D11_SIGNATURE_PARAMETER_DESC input{};
        if(FAILED(reflect->GetInputParameterDesc(n,&input)))return hr;
        if(input.SystemValueType==D3D_NAME_POSITION&&(input.ReadWriteMask&3)){
            const UINT tag=1;call.publish([&]{(*ps)->SetPrivateData(pixelScreenId,sizeof(tag),&tag);});return hr;
        }
    }
    // Only hardware sampling from ordinary material textures. Reject unknown
    // resource kinds and queryable screen/depth inputs at the caller.
    PixelInputs inputs{};
    for(UINT n=0;n<desc.BoundResources;++n){
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        if(FAILED(reflect->GetResourceBindingDesc(n,&binding)))return hr;
        if(binding.Type==D3D_SIT_TEXTURE){
            if(binding.BindPoint>=32||binding.BindCount>32-binding.BindPoint)return hr;
            for(UINT i=0;i<binding.BindCount;++i){
                const UINT slot=binding.BindPoint+i;
                inputs.textures|=1u<<slot;
                if(binding.Dimension==D3D_SRV_DIMENSION_TEXTURECUBE&&binding.Name&&
                   std::strcmp(binding.Name,"irradDiffuse")==0)inputs.irradiance|=1u<<slot;
                if(diagnostics_enabled()&&binding.Name)
                    call.publish([&]{(*ps)->SetPrivateData(binding_name_key(slot),UINT(strnlen(binding.Name,63)),binding.Name);});
            }
        }else if(binding.Type!=D3D_SIT_SAMPLER&&binding.Type!=D3D_SIT_CBUFFER)return hr;
    }
    call.publish([&]{(*ps)->SetPrivateData(pixelId,sizeof(inputs),&inputs);});return hr;
}
// Cache immutable properties only. Every draw still reads the current shader and
// SRV bindings, including changes made by a command list or a context state swap.
// Retained COM references prevent pointer reuse until this outer pass ends.
struct DrawMetadata {
    ComPtr<ID3D11PixelShader> pixel;
    PixelInputs inputs;
    ComPtr<ID3D11ShaderResourceView> materialViews[32];
    ComPtr<ID3D11VertexShader> vertex, twin;
    ComPtr<ID3D11InputLayout> layout, doubledLayout;
} drawMetadata;

void clear_draw_cache(){ drawMetadata = DrawMetadata{}; }

const char* scene_input(ID3D11Resource* resource){
    if(resource==s.hdr.Get()||resource==s.focusHdr.Get())return "scene-hdr";
    if(resource==s.resolved.Get()||resource==s.focusResolved.Get())return "scene-resolve";
    constexpr const char* roles[]={"scene-depth","scene-depth-resolve","scene-motion"};
    for(auto& bank:s.auxiliary)for(unsigned i=0;i<3;++i)
        if(resource==bank[i].Get())return roles[i];
    return "external";
}

bool material_texture(ID3D11ShaderResourceView* view,ID3D11PixelShader* ps,UINT slot,bool irradiance){
    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture;
    D3D11_TEXTURE2D_DESC desc{};
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    if(SUCCEEDED(resource.As(&texture))){
        texture->GetDesc(&desc);
        if(!(desc.BindFlags&(D3D11_BIND_RENDER_TARGET|D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_UNORDERED_ACCESS)))return true;
        // LMU generates its diffuse lighting probe as six renderable faces, then
        // samples it by direction in both projections of the same eye.
        if(irradiance && desc.BindFlags==(D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET) &&
           (desc.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE) && desc.ArraySize==6 &&
           desc.SampleDesc.Count==1 && desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT){
            view->GetDesc(&srv);
            if(srv.ViewDimension==D3D11_SRV_DIMENSION_TEXTURECUBE && srv.Format==desc.Format)return true;
        }
    }
    if(diagnostics_enabled()){
        char name[64]{};
        UINT length=sizeof(name)-1;
        if(FAILED(ps->GetPrivateData(binding_name_key(slot),&length,name)))name[0]=0;
        view->GetDesc(&srv);
        std::snprintf(s.refusalDetail,sizeof(s.refusalDetail),
            "PS texture t%u name=%s role=%s view=%u format=%u resource_format=%u size=%ux%u layers=%u mips=%u samples=%u bind=0x%x misc=0x%x paired=%u",
            slot,name[0]?name:"unknown",scene_input(resource.Get()),unsigned(srv.ViewDimension),unsigned(srv.Format),
            unsigned(desc.Format),desc.Width,desc.Height,desc.ArraySize,desc.MipLevels,desc.SampleDesc.Count,
            desc.BindFlags,desc.MiscFlags,s.paired);
        refuse(s.refusalDetail);
    }
    return false;
}

bool pixel_compatible(C* c){
    ComPtr<ID3D11PixelShader> ps;
    UINT instances=0;
    c->PSGetShader(&ps,nullptr,&instances);
    if(!ps)return true;
    if(instances){refuse("pixel shader class instances");return false;}
    if(ps.Get()!=drawMetadata.pixel.Get()){
        PixelInputs inputs{};
        UINT size=sizeof(inputs);
        if(FAILED(ps->GetPrivateData(pixelId,&size,&inputs))){
            UINT tag=0;size=sizeof(tag);
            refuse(SUCCEEDED(ps->GetPrivateData(pixelScreenId,&size,&tag))?"pixel shader reads screen-space SV_Position.xy":"unknown or unsupported pixel shader");
            return false;
        }
        drawMetadata.pixel=ps;
        // A cube accepted as irradDiffuse must be checked again if the next
        // shader binds it to an input with different semantics at the same slot.
        unsigned changed=drawMetadata.inputs.irradiance^inputs.irradiance;
        for(UINT i=0;changed;++i,changed>>=1)if(changed&1u)drawMetadata.materialViews[i].Reset();
        drawMetadata.inputs=inputs;
    }
    unsigned slots=drawMetadata.inputs.textures;
    UINT slot=0;
    while(slots){
        while(!(slots&1u)){slots>>=1;++slot;}
        const UINT first=slot;
        do{slots>>=1;++slot;}while(slots&1u);
        // One D3D getter per consecutive range. Sparse masks never acquire
        // references to unrelated material slots in the gaps.
        ID3D11ShaderResourceView* views[32];
        c->PSGetShaderResources(first,slot-first,views);
        bool compatible=true;
        for(UINT i=first;i<slot;++i){
            auto* view=views[i-first];
            if(!view)continue;
            auto& cached=drawMetadata.materialViews[i];
            if(view!=cached.Get()&&compatible){
                compatible=material_texture(view,ps.Get(),i,(drawMetadata.inputs.irradiance&(1u<<i))!=0);
                if(compatible){
                    cached.Attach(view); // transfer the getter's reference
                    continue;
                }
            }
            view->Release();
        }
        if(!compatible){refuse("pixel shader samples render target, depth or unsupported texture");return false;}
    }
    return true;
}

ID3D11VertexShader* vertex_twin(ID3D11VertexShader* shader){
    if(shader!=drawMetadata.vertex.Get()){
        UINT size=sizeof(ID3D11VertexShader*);
        ComPtr<ID3D11VertexShader> twin;
        if(FAILED(shader->GetPrivateData(shaderTwinKey(),&size,twin.GetAddressOf()))||!twin)return nullptr;
        drawMetadata.vertex=shader;
        drawMetadata.twin=std::move(twin);
    }
    return drawMetadata.twin.Get();
}
template<class T> bool hook(void* entry,T detour,T& original){
    if(MH_CreateHook(entry,reinterpret_cast<void*>(detour),reinterpret_cast<void**>(&original))!=MH_OK)return false;
    hooks[hookCount++]=entry;
    return MH_EnableHook(entry)==MH_OK;
}
HRESULT STDMETHODCALLTYPE on_create_layout(ID3D11Device* dev, const D3D11_INPUT_ELEMENT_DESC* elements,
    UINT count, const void* code, SIZE_T size, ID3D11InputLayout** result) {
    using CreateLayout = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_INPUT_ELEMENT_DESC*, UINT,
        const void*, SIZE_T, ID3D11InputLayout**);
    CreationCall call(layoutCreation, dev, 11);
    const auto original = reinterpret_cast<CreateLayout>(call.original);
    const HRESULT hr = original(dev, elements, count, code, size, result);
    if (FAILED(hr) || !result || !*result || !elements || !call.current() || shaderDevice.load() != dev ||
        count > D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT) return hr;

    D3D11_INPUT_ELEMENT_DESC doubled[D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT];
    std::copy_n(elements, count, doubled);
    bool changed = false;
    for (UINT i = 0; i < count; ++i) {
        if (doubled[i].InputSlotClass != D3D11_INPUT_PER_INSTANCE_DATA || !doubled[i].InstanceDataStepRate) continue;
        if (doubled[i].InstanceDataStepRate > UINT_MAX / 2) return hr;
        doubled[i].InstanceDataStepRate *= 2;
        changed = true;
    }
    if (!changed) {
        const UINT valid = 1;
        call.publish([&]{ (*result)->SetPrivateData(vertexOnlyId, sizeof(valid), &valid); });
    } else {
        ComPtr<ID3D11InputLayout> twin;
        if (SUCCEEDED(original(dev, doubled, count, code, size, &twin)) && twin && twin.Get() != *result)
            call.publish([&]{ (*result)->SetPrivateDataInterface(doubledLayoutId, twin.Get()); });
    }
    return hr;
}

bool shaders(ID3D11Device* dev, bool allowInstancing = true){
    if(!dev)return false;
    if(shaderDevice.load()==dev)return true;
    if(shaderDevice.load())return false;
    const auto caps=nv_multiview::query_capabilities(dev,api().capabilities);
    const bool useInstancing = !api().vertex || !api().stereoMode || !caps.singlePassStereoXYZW;
    if (useInstancing) {
        D3D11_FEATURE_DATA_D3D11_OPTIONS3 options{};
        if (!allowInstancing || FAILED(dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS3,&options,sizeof(options))) ||
            !options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer) return false;
    }
    instancedBackend.store(useInstancing);
    auto** dv=*reinterpret_cast<void***>(dev);
    if(!vertexCreation.install(dv[12],reinterpret_cast<void*>(on_create)))return false;
    if(!pixelCreation.install(dv[15],reinterpret_cast<void*>(on_create_ps))){
        vertexCreation.observing=false;vertexCreation.retire();return false;
    }
    if (useInstancing && !layoutCreation.install(dv[11],reinterpret_cast<void*>(on_create_layout))) {
        pixelCreation.observing=false;pixelCreation.retire();
        vertexCreation.observing=false;vertexCreation.retire();
        return false;
    }
    s.device=dev;shaderDevice.store(dev);return true;
}
bool main_module(void* pc){
    HMODULE m=nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(pc),&m)&&m==GetModuleHandleW(nullptr);
}
int rendering_query_type(ID3D11Asynchronous*);
bool has_output_uavs(C*, UINT);
void STDMETHODCALLTYPE on_resolve(C*,ID3D11Resource*,UINT,ID3D11Resource*,UINT,DXGI_FORMAT);
void STDMETHODCALLTYPE on_clear_color(C*,ID3D11RenderTargetView*,const FLOAT*);
void STDMETHODCALLTYPE on_clear_depth(C*,ID3D11DepthStencilView*,UINT,FLOAT,UINT8);
#include "focus_mvr_bindings.inl"
void early_device(const CreationCall& call,ID3D11Device* dev){
    call.publish([&]{
        if(!shaderDevice.load()&&shaders(dev))log_line("focus_mvr observing game shader creation before xrCreateSession");
    });
}
HRESULT WINAPI game_factory(IDXGIAdapter* a,D3D_DRIVER_TYPE driver,HMODULE sw,UINT flags,const D3D_FEATURE_LEVEL* lv,UINT n,UINT sdk,ID3D11Device** dev,D3D_FEATURE_LEVEL* fl,C** ctx){
    CreationCall call(deviceFactory);
    const bool game=main_module(LMU_CALLER_ADDRESS);
    const HRESULT hr=reinterpret_cast<decltype(&D3D11CreateDevice)>(call.original)(a,driver,sw,flags,lv,n,sdk,dev,fl,ctx);
    if(game&&SUCCEEDED(hr)&&dev)early_device(call,*dev);
    return hr;
}
HRESULT WINAPI game_chain_factory(IDXGIAdapter* a,D3D_DRIVER_TYPE driver,HMODULE sw,UINT flags,const D3D_FEATURE_LEVEL* lv,UINT n,UINT sdk,const DXGI_SWAP_CHAIN_DESC* desc,IDXGISwapChain** chain,ID3D11Device** dev,D3D_FEATURE_LEVEL* fl,C** ctx){
    CreationCall call(chainFactory);
    const bool game=main_module(LMU_CALLER_ADDRESS);
    const HRESULT hr=reinterpret_cast<decltype(&D3D11CreateDeviceAndSwapChain)>(call.original)(a,driver,sw,flags,lv,n,sdk,desc,chain,dev,fl,ctx);
    if(game&&SUCCEEDED(hr)&&dev)early_device(call,*dev);
    return hr;
}
void STDMETHODCALLTYPE on_clear_color(C* c,ID3D11RenderTargetView* rtv,const FLOAT* rgba){
    clearColor(c,rtv,rgba);
    if(outer&&c==s.ctx.Get()&&rtv==s.color.Get())s.clearedColor=true;
}
void STDMETHODCALLTYPE on_clear_depth(C* c,ID3D11DepthStencilView* dsv,UINT flags,FLOAT depth,UINT8 stencil){
    clearDepth(c,dsv,flags,depth,stencil);
    if(outer&&c==s.ctx.Get()&&dsv==s.depth.Get()&&(flags&D3D11_CLEAR_DEPTH))s.clearedDepth=true;
}
bool output(ComPtr<ID3D11Texture2D>& image,ID3D11Texture2D* model,unsigned w,unsigned h,unsigned bind){
    D3D11_TEXTURE2D_DESC desc{};model->GetDesc(&desc);
    if(desc.ArraySize!=1||desc.MipLevels!=1||desc.SampleDesc.Count!=1)return false;
    if(image){D3D11_TEXTURE2D_DESC d{};image->GetDesc(&d);if(d.Width==w&&d.Height==h&&d.Format==desc.Format)return true;}
    desc.Width=w;desc.Height=h;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=bind;desc.MiscFlags=0;desc.CPUAccessFlags=0;
    return SUCCEEDED(s.device->CreateTexture2D(&desc,nullptr,image.ReleaseAndGetAddressOf()));
}
void region(ID3D11Resource* dst,ID3D11Resource* src,unsigned x,unsigned w,unsigned h){
    const D3D11_BOX box{x,0,0,x+w,h,1};
    s.ctx->CopySubresourceRegion(dst,0,0,0,0,src,0,&box);
}
#include "focus_mvr_image_check.inl"
void STDMETHODCALLTYPE on_resolve(C* c,ID3D11Resource* dst,UINT ds,ID3D11Resource* src,UINT ss,DXGI_FORMAT fmt){
    const void* site=LMU_CALLER_ADDRESS;
    const bool ours=c==s.ctx.Get()&&!ds&&!ss;
    const bool outerSource=ours&&outer&&src==s.hdr.Get();
    const bool focusSource=ours&&(focus||s.compareFocus)&&src==s.focusHdr.Get();
    ComPtr<ID3D11Texture2D> target;
    D3D11_TEXTURE2D_DESC desc{};
    if((outerSource||focusSource)&&dst&&SUCCEEDED(dst->QueryInterface(IID_PPV_ARGS(&target))))target->GetDesc(&desc);
    if(s.bindMode&&ours&&(outer||focus||s.compareFocus)&&s.observedResolves<12){
        ++s.observedResolves;
        char line[256];std::snprintf(line,sizeof(line),"nvidia_mvr resolve outer=%d focus=%d comparing=%d src_atlas=%d src_native=%d dst_known=%d fmt=%u target=%ux%u",int(outer),int(focus),int(s.compareFocus),int(outerSource),int(focusSource),int(dst==s.resolved.Get()||dst==s.focusResolved.Get()),unsigned(fmt),desc.Width,desc.Height);log_line(line);
    }
    const bool nativeTarget=focusSource&&target&&desc.SampleDesc.Count==1&&desc.Width>=s.fw&&desc.Height>=s.fh&&desc.Format==fmt;
    if(focus&&ours)++s.focusResolves;
    const bool destinationMatches=dst==s.focusResolved.Get();
    const bool validatedResolve=s.bindMode?(site==s.focusSite[s.eye]||(s.validatedNativeDestination[s.eye]&&destinationMatches)):destinationMatches;
    if(focus&&s.haveFocus&&nativeTarget&&validatedResolve){
        region(dst,s.focusImage.Get(),0,s.fw,s.fh);s.replaced=true;return;
    }
    resolve(c,dst,ds,src,ss,fmt);
    if(s.compareFocus&&nativeTarget){
        s.retry[s.eye].missingResolves=0;
        s.compareFocus=false;++s.checked[s.eye];s.focusSite[s.eye]=site;s.validatedNativeDestination[s.eye]=destinationMatches;
        const auto& cv=bind_path::coverage;
        char audit[256];std::snprintf(audit,sizeof(audit),"nvidia_mvr coverage eye=%u paired=%u rejected_vs_binds target=%u query=%u stage=%u viewport=%u mask=%u no_twin=%u",
            s.eye,s.paired,cv.target,cv.query,cv.stage,cv.viewport,cv.masked,cv.shader);log_line(audit);
        bool matched=false;
        try{matched=image_check::matches(target.Get());}catch(const std::bad_alloc&){}
        if(matched)++s.accepted[s.eye];
        else {
            s.accepted[s.eye]=0;
            miss(s.eye,"paired focus differs from native focus");
        }
    }
    if(outerSource&&target&&desc.Width>=s.w+s.fw&&desc.Height>=s.fh&&desc.Format==fmt&&
       (s.bindMode||dst==s.resolved.Get())&&s.complete&&s.paired&&s.clearedColor&&s.clearedDepth){
        region(s.focusImage.Get(),dst,s.w,s.fw,s.fh);s.haveFocus=true;
    }
}
// Shared color alone cannot feed an effect that also consumes native depth or
// motion from the skipped scene. Inspect actual bound SRVs on post-process work.
bool stale_post_input(C* c,bool compute){
    const auto& inputs=s.auxiliary[focus?1:0];
    if(!inputs[0]&&!inputs[1]&&!inputs[2])return false;
    ID3D11ShaderResourceView* views[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
    auto referencesInput=[&](){
        bool stale=false;
        for(auto*& v:views)if(v){
            ComPtr<ID3D11Resource> resource;v->GetResource(&resource);
            for(const auto& input:inputs)if(resource.Get()==input.Get())stale=true;
            v->Release();v=nullptr;
        }
        return stale;
    };
    if(compute){c->CSGetShaderResources(0,UINT(std::size(views)),views);return referencesInput();}
    c->PSGetShaderResources(0,UINT(std::size(views)),views);if(referencesInput())return true;
    c->VSGetShaderResources(0,UINT(std::size(views)),views);return referencesInput();
}
void check_post_inputs(C* c,bool compute){
    if(c!=s.ctx.Get())return;
    if((outer&&s.haveFocus)||(focus&&s.replaced)){
        if(stale_post_input(c,compute)){s.staleInputs=true;refuse("post-process samples scene depth or motion");}
    }
}
template<typename T>bool bound(T* p){if(!p)return false;p->Release();return true;}
bool has_output_uavs(C* c, UINT slots){
    ID3D11UnorderedAccessView* views[D3D11_1_UAV_SLOT_COUNT];
    c->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,slots,views);
    bool found=false;
    for(UINT i=0;i<slots;++i)found|=bound(views[i]);
    return found;
}
template<typename F>void scene_draw(C* c,F native,[[maybe_unused]] std::array<UINT,6> signature){
#ifdef LMU_MVR_SHADOW_PROBE
    if(c==s.ctx.Get()){
        try{shadow_probe::inspect(c,signature);}catch(...){shadow_probe::finish();}
    }
#endif
    if(outer||focus)check_post_inputs(c,false);
    if(!outer||lens||c!=s.ctx.Get()||!s.complete){native();return;}
    ID3D11RenderTargetView* rt[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* depth=nullptr;
    c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,rt,&depth);
    const bool color=rt[0]==s.color.Get(),depthMatch=depth==s.depth.Get();
    bool other=false;
    for(unsigned i=0;i<D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;++i){
        if(rt[i]&&(i||!color))other=true;
        if(rt[i])rt[i]->Release();
    }
    if(depth)depth->Release();
    if(!color&&!depthMatch){if(geometryDepth)refuse("unexpected geometry render target");native();return;}
    if(s.haveFocus){refuse("scene draw after HDR resolve");native();return;}
    // Reject extra attachments, UAV writes, tessellation, geometry and stream output.
    UINT count=0;
    ComPtr<ID3D11VertexShader> vs;
    c->VSGetShader(&vs,nullptr,&count);
    ID3D11GeometryShader* gs=nullptr;ID3D11HullShader* hs=nullptr;ID3D11DomainShader* ds=nullptr;
    c->GSGetShader(&gs,nullptr,nullptr);c->HSGetShader(&hs,nullptr,nullptr);c->DSGetShader(&ds,nullptr,nullptr);
    const bool geometry=bound(gs),hull=bound(hs),domain=bound(ds);
    ID3D11Buffer* streams[D3D11_SO_BUFFER_SLOT_COUNT]{};c->SOGetTargets(D3D11_SO_BUFFER_SLOT_COUNT,streams);
    for(auto* stream:streams)other|=bound(stream);
    other|=has_output_uavs(c,s.uavSlots);
    D3D11_VIEWPORT vp[2]{};UINT vps=2;c->RSGetViewports(&vps,vp);
    D3D11_RECT sc[2]{};UINT scs=2;c->RSGetScissorRects(&scs,sc);
    const char* reason=nullptr;
    if(other||(depth&&!depthMatch))reason="additional render, stream-output, depth or UAV target";
    else if(geometry||hull||domain)reason="geometry or tessellation shader";
    else if(count||!vs)reason="missing vertex shader or class instances";
    else if(vps!=1||vp[0].TopLeftX!=0||vp[0].TopLeftY!=0||vp[0].Width!=float(s.w)||vp[0].Height!=float(s.h))reason="unexpected scene viewport";
    else if(scs>1)reason="multiple scissor rectangles";
    else if(scs&&(sc[0].left>0||sc[0].top>0||sc[0].right<LONG(s.w)||sc[0].bottom<LONG(s.h))){
        ComPtr<ID3D11RasterizerState> rs;c->RSGetState(&rs);
        D3D11_RASTERIZER_DESC rd{};if(rs)rs->GetDesc(&rd);
        if(rd.ScissorEnable)reason="partial scene scissor";
    }
    if(reason){refuse(reason);native();return;}
    if(!pixel_compatible(c)){native();return;}
    auto* pairedShader=vertex_twin(vs.Get());
    if(!pairedShader){refuse("vertex shader has no converted XYZW twin");native();return;}
    vp[1]={float(s.w),0,float(s.fw),float(s.fh),vp[0].MinDepth,vp[0].MaxDepth};
    if(scs){
        const auto x=[&](LONG n){return LONG(s.w)+LONG(std::lround(double(n)*s.fw/s.w));};
        const auto y=[&](LONG n){return LONG(std::lround(double(n)*s.fh/s.h));};
        sc[1]={x(sc[0].left),y(sc[0].top),x(sc[0].right),y(sc[0].bottom)};
    }
    ComPtr<ID3D11Buffer> previous;
    c->VSGetConstantBuffers(13,1,&previous);
    // The native b13 may have a ranged D3D11.1 binding. Leave it untouched.
    if(previous){refuse("game uses vertex cbuffer b13");native();return;}
    if(api().stereoMode(c,2,0,TRUE)!=NVAPI_OK){stop_reuse("driver refused dual viewport mode");native();return;}
    c->VSSetConstantBuffers(13,1,s.cb.GetAddressOf());c->VSSetShader(pairedShader,nullptr,0);
    c->RSSetViewports(2,vp);if(scs)c->RSSetScissorRects(2,sc);
    native();
    if(api().stereoMode(c,1,0,FALSE)!=NVAPI_OK){
        stop_reuse("driver refused single-view restore");api().stereoMode(c,1,0,FALSE);
        log_line("nvidia_mvr disabled: driver refused single-view restoration");
    }
    c->RSSetViewports(1,vp);if(scs)c->RSSetScissorRects(1,sc);
    c->VSSetShader(vs.Get(),nullptr,0);ID3D11Buffer* empty=nullptr;c->VSSetConstantBuffers(13,1,&empty);
    ++s.paired;
}
void STDMETHODCALLTYPE on_draw(C* c,UINT a,UINT b){scene_draw(c,[&]{draw(c,a,b);},{1,a,b});}
void STDMETHODCALLTYPE on_indexed(C* c,UINT a,UINT b,INT d){scene_draw(c,[&]{indexed(c,a,b,d);},{2,a,b,UINT(d)});}
void STDMETHODCALLTYPE on_instanced(C* c,UINT a,UINT b,UINT d,UINT e){scene_draw(c,[&]{instanced(c,a,b,d,e);},{3,a,b,d,e});}
void STDMETHODCALLTYPE on_indexed_instanced(C* c,UINT a,UINT b,UINT d,INT e,UINT f){scene_draw(c,[&]{indexedInstanced(c,a,b,d,e,f);},{4,a,b,d,UINT(e),f});}
void STDMETHODCALLTYPE on_indirect(C* c,ID3D11Buffer* b,UINT off){scene_draw(c,[&]{indirect(c,b,off);},{5,off});}
void STDMETHODCALLTYPE on_indexed_indirect(C* c,ID3D11Buffer* b,UINT off){scene_draw(c,[&]{indexedIndirect(c,b,off);},{6,off});}
void STDMETHODCALLTYPE on_auto(C* c){scene_draw(c,[&]{automatic(c);},{7});}
int rendering_query_type(ID3D11Asynchronous* async){
    if(!async)return -1;
    ComPtr<ID3D11Query> query;
    if(FAILED(async->QueryInterface(IID_PPV_ARGS(&query))))return 16;
    D3D11_QUERY_DESC d{};query->GetDesc(&d);
    if(d.Query==D3D11_QUERY_TIMESTAMP_DISJOINT||d.Query==D3D11_QUERY_TIMESTAMP||d.Query==D3D11_QUERY_EVENT)return -1;
    return (std::min)(unsigned(d.Query),16u);
}
#ifdef LMU_MVR_SHADOW_PROBE
std::atomic<unsigned> openByType[17]{};
void query_refusal(const char* where,int type){
    if(s.refusal)return;
    if(type<0){for(int i=0;i<17;++i)if(openByType[i].load()){type=i;break;}}
    const char* name=type==D3D11_QUERY_OCCLUSION?"occlusion":type==D3D11_QUERY_OCCLUSION_PREDICATE?"occlusion predicate":
        type==D3D11_QUERY_PIPELINE_STATISTICS?"pipeline statistics":"other";
    std::snprintf(s.refusalDetail,sizeof(s.refusalDetail),"native rendering query type=%d (%s) phase=%s paired=%u",type,name,where,s.paired);
    refuse(s.refusalDetail);
}
#endif
void STDMETHODCALLTYPE on_begin_query(C* c,ID3D11Asynchronous* async){
    beginQuery(c,async);
    if(c!=s.ctx.Get())return;
    const int kind=rendering_query_type(async);if(kind<0)return;
    countingQueries.fetch_add(1,std::memory_order_relaxed);
#ifdef LMU_MVR_SHADOW_PROBE
    ++openByType[kind];
    if(outer)query_refusal("during outer",kind);
#else
    if(outer)refuse("active native rendering query");
#endif
    if(focus)s.staleInputs=true;
}
void STDMETHODCALLTYPE on_end_query(C* c,ID3D11Asynchronous* async){
    endQuery(c,async);
    if(c!=s.ctx.Get())return;
    const int kind=rendering_query_type(async);if(kind<0)return;
#ifdef LMU_MVR_SHADOW_PROBE
    auto& tracked=openByType[kind];auto open=tracked.load();
    while(open&&!tracked.compare_exchange_weak(open,open-1)){}
#endif
    unsigned n=countingQueries.load(std::memory_order_relaxed);
    while(n&&!countingQueries.compare_exchange_weak(n,n-1,std::memory_order_relaxed)){}
}
void check_dispatch(C* c){
    if(outer&&geometryDepth&&c==s.ctx.Get())refuse("compute dispatch in geometry pass");
    check_post_inputs(c,true);
}
void STDMETHODCALLTYPE on_dispatch(C* c,UINT x,UINT y,UINT z){if(outer||focus)check_dispatch(c);dispatch(c,x,y,z);}
void STDMETHODCALLTYPE on_dispatch_indirect(C* c,ID3D11Buffer* b,UINT off){if(outer||focus)check_dispatch(c);dispatchIndirect(c,b,off);}
void STDMETHODCALLTYPE on_list(C* c,ID3D11CommandList* l,BOOL restore){if(outer&&c==s.ctx.Get())refuse("native command list");if(focus&&c==s.ctx.Get())s.staleInputs=true;execute(c,l,restore);}
}
bool install_early_factories(decltype(&D3D11CreateDevice) device,decltype(&D3D11CreateDeviceAndSwapChain) chain){
    std::lock_guard<std::recursive_mutex> lock(lifecycle);
    if(hookCount||shaderDevice.load()||deviceFactory.observing||retiringCreationHooks())return false;
    const auto mh=MH_Initialize();if(mh!=MH_OK&&mh!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(!deviceFactory.install(reinterpret_cast<void*>(device),reinterpret_cast<void*>(game_factory))||
       !chainFactory.install(reinterpret_cast<void*>(chain),reinterpret_cast<void*>(game_chain_factory))){stop();return false;}
    return true;
}
bool early(){
    const auto dll=GetModuleHandleW(L"d3d11.dll");
    return dll && install_early_factories(reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(dll,"D3D11CreateDevice")),
        reinterpret_cast<decltype(&D3D11CreateDeviceAndSwapChain)>(GetProcAddress(dll,"D3D11CreateDeviceAndSwapChain")));
}
bool start(C* c,bool bindMode,const char** failure){
#ifdef LMU_MVR_SHADOW_PROBE
    bindMode=false;
#endif
    std::lock_guard<std::recursive_mutex> lock(lifecycle);
    if(retiringCreationHooks()){if(failure)*failure="Previous device hooks are still retiring";return false;}
    auto failed=[&](const char* reason){if(failure)*failure=reason;stop();return false;};
    if(!c||c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return failed("Immediate D3D11 device context unavailable");
    ComPtr<ID3D11Device> device;c->GetDevice(&device);
    // A launcher/video plug-in can create a different D3D device first.
    if(shaderDevice.load()&&shaderDevice.load()!=device.Get())stop();
    if(retiringCreationHooks()){if(failure)*failure="Previous device hooks are still retiring";return false;}
    if(!shaders(device.Get(),bindMode) || (instancedBackend.load() && !bindMode))return failed("Viewport instancing or NVIDIA stereo shader setup unavailable");
    s.ctx=c;s.bindMode=bindMode;countingQueries.store(0,std::memory_order_relaxed);
    s.uavSlots=device->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1 ? D3D11_1_UAV_SLOT_COUNT : D3D11_PS_CS_UAV_REGISTER_COUNT;
    D3D11_BUFFER_DESC cb{16,D3D11_USAGE_DEFAULT,D3D11_BIND_CONSTANT_BUFFER,0,0,0};
    auto** cv=*reinterpret_cast<void***>(c);
    if(FAILED(s.device->CreateBuffer(&cb,nullptr,&s.cb))||
       (s.bindMode ? !bind_path::install(cv) : ( !hook(cv[27],on_begin_query,beginQuery)||!hook(cv[28],on_end_query,endQuery)||
       !hook(cv[50],on_clear_color,clearColor)||!hook(cv[53],on_clear_depth,clearDepth)||!hook(cv[12],on_indexed,indexed)||!hook(cv[13],on_draw,draw)||
       !hook(cv[20],on_indexed_instanced,indexedInstanced)||!hook(cv[21],on_instanced,instanced)||
       !hook(cv[38],on_auto,automatic)||!hook(cv[39],on_indexed_indirect,indexedIndirect)||
       !hook(cv[40],on_indirect,indirect)||!hook(cv[41],on_dispatch,dispatch)||!hook(cv[42],on_dispatch_indirect,dispatchIndirect)||!hook(cv[57],on_resolve,resolve)||!hook(cv[58],on_list,execute)))){
        return failed("MVR constant buffer or D3D11 context hooks unavailable");
    }
    shaderDevice.store(s.device.Get());return true;
}
void stop(){
    std::lock_guard<std::recursive_mutex> lock(lifecycle);
    ++creationGeneration;
    shaderDevice.store(nullptr);
    for(auto* hook:{&deviceFactory,&chainFactory,&vertexCreation,&pixelCreation,&layoutCreation}){
        hook->observing=false;
        hook->retire();
    }
    {
        std::lock_guard<std::mutex> cacheLock(twinCacheMutex);
        twinCache.clear();cachedByteCount=0;
    }
    while(hookCount){auto* p=hooks[--hookCount];MH_DisableHook(p);MH_RemoveHook(p);}
    fallbackPending.store(false,std::memory_order_release);
    bind_path::reset(); clear_draw_cache();outer=focus=lens=false;s=State{};
#ifdef LMU_MVR_SHADOW_PROBE
    shadow_probe::probe={};
    for(auto& count:openByType)count.store(0);
#endif
}
GeometryScope::GeometryScope(bool eligible):active(eligible&&outer){if(active)++geometryDepth;}
GeometryScope::~GeometryScope(){if(active)--geometryDepth;}
bool candidate(unsigned eye){
    if(eye>=2)return false;
    auto& retry=s.retry[eye];
    if(retry.retryAt!=FocusRetry::Clock::time_point{} && !retry.ready(FocusRetry::Clock::now()))return false;
    s.eye=eye;return true;
}
void native_outer_stopped(unsigned eye,const char* reason){
    if(available())report_stopped(eye,reason);
}
void atlas_refusal(unsigned eye,unsigned w,unsigned h,unsigned fw,unsigned fh){
    // This case is retried every pass as LMU's render targets settle.
    const std::array<unsigned,4> dimensions{w,h,fw,fh};
    if(eye>=2||dimensions==s.rejectedAtlas[eye])return;
    s.rejectedAtlas[eye]=dimensions;
    ++s.fallbackCount[eye];
    std::snprintf(s.fallbackReason,sizeof(s.fallbackReason),"%s eye: outer %ux%u and focus %ux%u do not fit the MVR atlas",
                  eye?"Right":"Left",w,h,fw,fh);
    fallbackPending.store(true,std::memory_order_release);
    log_line(s.fallbackReason);
}
void miss(unsigned eye,const char* reason){
    if(eye>=2)return;
    ++s.fallbackCount[eye];
    if(!s.broken){
        std::snprintf(s.fallbackReason,sizeof(s.fallbackReason),"%s eye: %s",eye?"Right":"Left",reason);
        fallbackPending.store(true,std::memory_order_release);
    }
    if (instancedBackend.load()) {
        s.accepted[eye]=0;
        s.focusSite[eye]=nullptr;
        s.validatedNativeDestination[eye]=false;
    }
    const auto failures=++s.misses[eye];
    auto& retry=s.retry[eye];
    retry.failed(FocusRetry::Clock::now());
    if(failures<=4||failures%10==0){
        char line[640];std::snprintf(line,sizeof(line),
            "nvidia_mvr fallback eye=%u reason=%s; ordinary focus, retry after %u seconds; outer_passes=%llu focus_reused=%llu fallbacks=%llu/%llu",
            eye,reason,retry.waitSeconds,s.outerPasses,s.focusSkips,s.fallbackCount[0],s.fallbackCount[1]);
        log_line(line);
    }
}
bool fallback_pending(){return fallbackPending.load(std::memory_order_acquire);}
const char* take_fallback_reason(){
    if(!fallbackPending.exchange(false,std::memory_order_acq_rel))return nullptr;
    return s.fallbackReason;
}
bool available(){return s.ctx&&!s.broken;}
bool uses_instancing(){return available() && instancedBackend.load();}
void discard(){
    // Observe the whole interval, including a late resolve after the focus scene.
    // Give late resolves a chance before backing off on an unobservable pass.
    if(s.compareFocus&&!s.broken){
        const auto count=++s.missingValidation[s.eye];
        if(++s.retry[s.eye].missingResolves>=45){
            s.accepted[s.eye]=0;
            s.focusSite[s.eye]=nullptr;
            s.validatedNativeDestination[s.eye]=false;
            miss(s.eye,"no matching native focus resolve for validation");
        }else if(count==1||count%120==0){
            std::snprintf(s.fallbackReason,sizeof(s.fallbackReason),"%s eye: no matching native focus resolve for validation (%llu passes)",
                          s.eye?"Right":"Left",count);
            fallbackPending.store(true,std::memory_order_release);
            log_line(s.fallbackReason);
        }
    }
    outer=focus=lens=false;s.compareFocus=false;s.haveFocus=false;s.replaced=false;
#ifdef LMU_MVR_SHADOW_PROBE
    shadow_probe::finish();
#endif
}
bool prepare(ID3D11Texture2D* finalAtlas,ID3D11Texture2D* hdr,ID3D11Texture2D* resolved,
             ID3D11RenderTargetView* color,ID3D11DepthStencilView* depth,ID3D11Texture2D* focusHdr,
             ID3D11Texture2D* focusResolved,unsigned w,unsigned h,unsigned fw,unsigned fh,const std::array<float,4>& r){
    if(!available()||!finalAtlas||!hdr||!resolved||!color||!depth||!focusHdr||!focusResolved)return false;
    constexpr unsigned maxExtent=D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    if(!w||!h||!fw||!fh||w>maxExtent||fw>maxExtent-w||h>maxExtent||fh>maxExtent)return false;
    D3D11_TEXTURE2D_DESC a{},b{},c{},d{},f{};
    finalAtlas->GetDesc(&f);hdr->GetDesc(&a);resolved->GetDesc(&b);focusHdr->GetDesc(&c);focusResolved->GetDesc(&d);
    if(a.ArraySize!=1||b.ArraySize!=1||c.ArraySize!=1||d.ArraySize!=1||a.MipLevels!=1||b.MipLevels!=1||c.MipLevels!=1||d.MipLevels!=1||
       a.SampleDesc.Count<=1||c.SampleDesc.Count!=a.SampleDesc.Count||b.SampleDesc.Count!=1||d.SampleDesc.Count!=1||
       b.Format!=d.Format||a.Width!=b.Width||a.Height!=b.Height||b.Width<w+fw||b.Height<std::max(h,fh)||
       d.Width!=fw||d.Height!=fh||c.Width!=fw||c.Height!=fh||f.Width<w||f.Height<h||
       !output(s.outerImage,finalAtlas,w,h,D3D11_BIND_SHADER_RESOURCE)||!output(s.focusImage,resolved,fw,fh,0))return false;
    for(auto& inputs:s.auxiliary)for(auto& input:inputs)input.Reset();
    s.hdr=hdr;s.resolved=resolved;s.focusHdr=focusHdr;s.focusResolved=focusResolved;s.color=color;s.depth=depth;
    auto& key=s.validated[s.eye];
    if(key.atlas.Get()!=hdr||key.nativeHdr.Get()!=focusHdr||key.nativeResolve.Get()!=focusResolved||
       key.w!=w||key.h!=h||key.fw!=fw||key.fh!=fh){
        s.accepted[s.eye]=s.checked[s.eye]=s.misses[s.eye]=0;s.focusSite[s.eye]=nullptr;
        s.retry[s.eye]={};
        key.atlas=hdr;key.nativeHdr=focusHdr;key.nativeResolve=focusResolved;key.w=w;key.h=h;key.fw=fw;key.fh=fh;
    }
    s.rejectedAtlas[s.eye]={};
    s.w=w;s.h=h;s.fw=fw;s.fh=fh;s.ctx->UpdateSubresource(s.cb.Get(),0,nullptr,r.data(),0,0);
    return true;
}
void auxiliary(ID3D11Texture2D* depth,ID3D11Texture2D* depthResolve,ID3D11Texture2D* motion,
               ID3D11Texture2D* nativeDepth,ID3D11Texture2D* nativeDepthResolve,ID3D11Texture2D* nativeMotion){
    s.auxiliary[0][0]=depth;s.auxiliary[0][1]=depthResolve;s.auxiliary[0][2]=motion;
    s.auxiliary[1][0]=nativeDepth;s.auxiliary[1][1]=nativeDepthResolve;s.auxiliary[1][2]=nativeMotion;
}
void begin_outer(){outer=true;s.refusal=nullptr;s.compareFocus=false;s.staleInputs=false;s.complete=true;s.haveFocus=false;s.paired=0;s.clearedColor=false;s.clearedDepth=false;
    if(s.bindMode){bind_path::begin();return;}
#ifdef LMU_MVR_SHADOW_PROBE
    shadow_probe::begin(s.eye);
    if(countingQueries.load())query_refusal("outer entry",-1);
#else
    if(countingQueries.load(std::memory_order_relaxed))refuse("active native rendering query");
#endif
}
ID3D11Texture2D* end_outer(ID3D11Texture2D* atlas){
#ifdef LMU_MVR_SHADOW_PROBE
    shadow_probe::probe.samplingOuter=false;
#endif
    if(s.bindMode){
        bind_path::end();
        if(!s.haveFocus&&s.complete&&s.paired&&s.clearedColor&&s.clearedDepth&&!s.broken){
            D3D11_TEXTURE2D_DESC d{};s.hdr->GetDesc(&d);
            if(output(s.atlasResolve,s.resolved.Get(),d.Width,d.Height,0)){
                resolve(s.ctx.Get(),s.atlasResolve.Get(),0,s.hdr.Get(),0,d.Format);
                region(s.focusImage.Get(),s.atlasResolve.Get(),s.w,s.fw,s.fh);s.haveFocus=true;
            }
        }
    }
    clear_draw_cache();outer=false;s.haveFocus&=s.complete&&!s.broken;
    region(s.outerImage.Get(),atlas,0,s.w,s.h);
    if(!s.haveFocus){
        const char* reason=s.refusal?s.refusal:!s.clearedColor?"no full scene color clear":
            !s.clearedDepth?"no full scene depth clear":!s.paired?"no compatible scene draws":"missing matching HDR resolve";
        miss(s.eye,reason);
    }
    if(++s.outerPasses<=4||s.outerPasses%600==0){
        char line[192];std::snprintf(line,sizeof(line),"nvidia_mvr outer=%llu eye=%u %s=%u focus_ready=%d focus_passes_reused=%llu",
            s.outerPasses,s.eye,s.bindMode?"paired_binds":"paired_draws",s.paired,int(s.haveFocus),s.focusSkips);log_line(line);
    }
    return s.outerImage.Get();
}
void mask(bool active){lens=active;if(s.bindMode&&outer)bind_path::update();}
void nested(){if(outer&&!s.bindMode)refuse("nested scene view");}
bool begin_focus(){focus=false;s.replaced=false;s.focusResolves=0;
    if(s.haveFocus&&!s.broken){
        if(s.bindMode&&s.accepted[s.eye]<2)s.compareFocus=true;
        else focus=!countingQueries.load(std::memory_order_relaxed);
    }
#ifdef LMU_MVR_SHADOW_PROBE
    if(!focus)shadow_probe::focus();
#endif
    return focus;}
#ifdef LMU_MVR_SHADOW_PROBE
void finish_shadow_probe(){shadow_probe::finish();}
#endif
bool skipping(){return focus&&!s.replaced;}
bool end_focus(){
    const bool ok=s.replaced&&!s.staleInputs;
    if(ok){
        s.retry[s.eye].reused();
        ++s.focusSkips;
        if(s.focusSkips<=4)log_line("nvidia_mvr focus pass reused");
    }else if(focus){
        const char* reason=s.replaced?"native post-process input required":
            s.focusResolves?"validated focus resolve not observed":"no focus resolve issued";
        if(s.bindMode){
            s.accepted[s.eye]=0;s.focusSite[s.eye]=nullptr;
            miss(s.eye,reason);
        }else stop_reuse(reason);
    }
    discard();return ok;
}
}
