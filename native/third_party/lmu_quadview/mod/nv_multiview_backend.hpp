#pragma once
#include "nv_multiview_caps.hpp"

namespace lmuqv::nv_multiview {
// Research building block: capability bits and successful asynchronous mode
// setters do not establish render correctness. Validate private output images
// before a caller uses this to replace any visible native rendering.
enum class Route { sps2, mvr2, mvr4 };
struct RenderApi {
    Api capabilities;
    decltype(&NvAPI_D3D11_CreateVertexShaderEx) vertex=nullptr;
    decltype(&NvAPI_D3D11_CreatePixelShaderEx_2) pixel=nullptr;
    decltype(&NvAPI_D3D_SetSinglePassStereoMode) stereoMode=nullptr;
    decltype(&NvAPI_D3D_SetMultiViewMode) multiViewMode=nullptr;
};
const RenderApi& driver_render_api();
struct Bytecode { const void* data=nullptr;size_t size=0; };
struct ShaderSet {
    ID3D11VertexShader* vertex=nullptr;
    ID3D11PixelShader* pixel=nullptr;
    ShaderSet()=default;~ShaderSet();
    ShaderSet(const ShaderSet&)=delete;ShaderSet& operator=(const ShaderSet&)=delete;
};
struct Submission {
    bool submitted=false,disabled=false;
    NvAPI_Status enableStatus=NVAPI_NO_IMPLEMENTATION,disableStatus=NVAPI_NO_IMPLEMENTATION;
};
// Explicit ownership contract: this context starts with NVIDIA stereo/MVR disabled,
// and no other owner changes these modes while this object is in use. NVAPI has
// no mode getter; this cannot save/restore an arbitrary application's NV state.
enum class ContextContract { exclusively_owned_and_known_disabled };
class Backend {
    RenderApi api_{};
    Capabilities capabilities_{};
    ID3D11Device* device_=nullptr;
    ID3D11DeviceContext* context_=nullptr;
    bool poisoned_=false;
public:
    Backend()=default;~Backend();
    Backend(const Backend&)=delete;Backend& operator=(const Backend&)=delete;
    bool initialize(ID3D11Device*,ID3D11DeviceContext*,ContextContract,const RenderApi&);
    bool initialize(ID3D11Device* d,ID3D11DeviceContext* c,ContextContract k){return initialize(d,c,k,driver_render_api());}
    const Capabilities& capabilities()const{return capabilities_;}
    bool supports(Route)const;
    bool poisoned()const{return poisoned_;}
    bool create_shaders(Route,Bytecode vs,Bytecode ps,
        NV_CUSTOM_SEMANTIC* vsSemantics,unsigned vsCount,
        NV_CUSTOM_SEMANTIC* psSemantics,unsigned psCount,ShaderSet&,std::string& error);
    // Caller binds converted shaders, complete per-view constants, compatible
    // array RTV/DSV and IA state. sps2 is the documented X-only/shared-attribute
    // shader contract; independently projected generic attributes use mvr2/mvr4.
    // A single submission preserves original instance
    // count/start; it never multiplies instances or installs game hooks.
    Submission submit_indexed(Route,UINT indexCount,UINT startIndex,INT baseVertex,
        UINT instanceCount=1,UINT startInstance=0,bool instanced=false);
};
}
