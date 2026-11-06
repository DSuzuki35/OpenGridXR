#include "nv_multiview_backend.hpp"

namespace lmuqv::nv_multiview {
namespace {
template<class T>void release(T*& p){if(p){p->Release();p=nullptr;}}
bool semantics_valid(NV_CUSTOM_SEMANTIC* s,unsigned n){
    if(!n)return !s;
    if(!s||n>NV_CUSTOM_SEMANTIC_MAX)return false;
    for(unsigned i=0;i<n;++i)if(s[i].version!=NV_CUSTOM_SEMANTIC_VERSION||s[i].Reserved||
        !std::memchr(s[i].NVCustomSemanticNameString,0,sizeof(s[i].NVCustomSemanticNameString))||
        !s[i].NVCustomSemanticNameString[0])return false;
    return true;
}
bool has(NV_CUSTOM_SEMANTIC* s,unsigned n,NV_CUSTOM_SEMANTIC_TYPE type){
    for(unsigned i=0;i<n;++i)if(s[i].NVCustomSemanticType==type)return true;
    return false;
}
}
const RenderApi& driver_render_api(){
    struct Driver {
        RenderApi api{};HMODULE module=nullptr;
        Driver(){
            module=LoadLibraryExW(L"nvapi64.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            if(!module)return;
            using Query=void* (__cdecl*)(NvU32);Query query=nullptr;
            auto proc=GetProcAddress(module,"nvapi_QueryInterface");std::memcpy(&query,&proc,sizeof(query));
            if(!query)return;
            // Pinned official vendor/nvapi/nvapi_interface.h IDs.
            api.capabilities.initialize=reinterpret_cast<decltype(api.capabilities.initialize)>(query(0x0150e828));
            api.capabilities.sps=reinterpret_cast<decltype(api.capabilities.sps)>(query(0x6f5f0a6d));
            api.capabilities.multiview=reinterpret_cast<decltype(api.capabilities.multiview)>(query(0xb6e0a41c));
            api.vertex=reinterpret_cast<decltype(api.vertex)>(query(0x0beaa0b2));
            api.pixel=reinterpret_cast<decltype(api.pixel)>(query(0x4162822b));
            api.stereoMode=reinterpret_cast<decltype(api.stereoMode)>(query(0xa39e6e6e));
            api.multiViewMode=reinterpret_cast<decltype(api.multiViewMode)>(query(0x8285c8da));
        }
        // Keep driver integration resident for D3D resource lifetime.
    };
    static const Driver driver;return driver.api;
}
ShaderSet::~ShaderSet(){release(vertex);release(pixel);}
Backend::~Backend(){release(context_);release(device_);}
bool Backend::initialize(ID3D11Device* d,ID3D11DeviceContext* c,ContextContract,const RenderApi& api){
    if(device_||!d||!c||c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
    ID3D11Device* owner=nullptr;c->GetDevice(&owner);const bool matches=owner==d;release(owner);
    if(!matches)return false;
    api_=api;capabilities_=query_capabilities(d,api.capabilities);
    if(capabilities_.initialization!=NVAPI_OK)return false;
    device_=d;context_=c;d->AddRef();c->AddRef();return true;
}
bool Backend::supports(Route route)const{
    if(!device_||poisoned_||!api_.vertex||!api_.pixel)return false;
    return route!=Route::sps2?capabilities_.multiView&&api_.multiViewMode:
        (capabilities_.singlePassStereo||capabilities_.multiviewStereo)&&api_.stereoMode;
}
bool Backend::create_shaders(Route route,Bytecode vs,Bytecode ps,NV_CUSTOM_SEMANTIC* v,unsigned vn,
                            NV_CUSTOM_SEMANTIC* p,unsigned pn,ShaderSet& output,std::string& error){
    error.clear();
    if(!supports(route)){error="unsupported NVIDIA rendering route";return false;}
    if(output.vertex||output.pixel||!vs.data||!vs.size||!ps.data||!ps.size||
       !semantics_valid(v,vn)||!semantics_valid(p,pn)){error="invalid shader or custom semantic table";return false;}
    if(!has(v,vn,NV_VIEWPORT_MASK_SEMANTIC)){
        error="array-target shader requires explicit NVIDIA viewport mask";return false;
    }
    if(route==Route::sps2){
        if(!has(v,vn,NV_X_RIGHT_SEMANTIC)||has(v,vn,NV_POSITION_SEMANTIC)||
           has(v,vn,NV_GENERIC_ATTRIBUTE_SEMANTIC)||pn){
            error="SPS2 requires X-only position and shared generic attributes";return false;
        }
    }else if(!has(v,vn,NV_POSITION_SEMANTIC)||!has(v,vn,NV_VIEWPORT_MASK_2_SEMANTIC)){
        error="MVR requires position family and both viewport masks";return false;
    }
    NvAPI_D3D11_CREATE_VERTEX_SHADER_EX va{};va.version=NVAPI_D3D11_CREATEVERTEXSHADEREX_VERSION;
    va.NumCustomSemantics=vn;va.pCustomSemantics=v;
    ID3D11VertexShader* vertex=nullptr;ID3D11PixelShader* pixel=nullptr;
    auto status=api_.vertex(device_,vs.data,vs.size,nullptr,&va,&vertex);
    if(status!=NVAPI_OK||!vertex){release(vertex);error="NVIDIA vertex shader creation failed: "+std::to_string(status);return false;}
    NvAPI_D3D11_CREATE_PIXEL_SHADER_EX pa{};pa.version=NVAPI_D3D11_CREATEPIXELSHADEREX_VERSION;
    pa.NumCustomSemantics=pn;pa.pCustomSemantics=p;
    status=api_.pixel(device_,ps.data,ps.size,nullptr,&pa,&pixel);
    if(status!=NVAPI_OK||!pixel){release(vertex);release(pixel);error="NVIDIA pixel shader creation failed: "+std::to_string(status);return false;}
    output.vertex=vertex;output.pixel=pixel;return true;
}
Submission Backend::submit_indexed(Route route,UINT indices,UINT start,INT base,UINT instances,UINT first,bool instanced){
    Submission result;
    if(!supports(route)||!indices||!instances)return result;
    auto set=[&](unsigned views){
        if(route==Route::sps2)return api_.stereoMode(context_,views,1,0);
        NV_MULTIVIEW_PARAMS p{};p.version=NV_MULTIVIEW_PARAMS_VER;p.numViews=views;
        for(unsigned i=0;i<views;++i)p.renderTargetIndexOffset[i]=i;
        return api_.multiViewMode(context_,&p);
    };
    result.enableStatus=set(route==Route::mvr4?4:2);
    if(result.enableStatus==NVAPI_OK){
        if(instanced)context_->DrawIndexedInstanced(indices,instances,start,base,first);
        else context_->DrawIndexed(indices,start,base);
        result.submitted=true;
    }
    // Set calls are asynchronous; even failure gets an explicit reset attempt.
    result.disableStatus=set(1);result.disabled=result.disableStatus==NVAPI_OK;
    if(!result.disabled)poisoned_=true;
    return result;
}
}
