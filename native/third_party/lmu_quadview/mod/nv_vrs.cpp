#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "nv_vrs.hpp"
#include "nv_vrs_sdk.hpp"
#include <cstdio>
namespace {
#include "vendor/nvapi/nvapi_interface.h"
// NVAPI can install device-owned driver integration. The helper does not own
// the application's last D3D reference, so it must not unload that integration
// on reset. One lazy initialization is retained for the process lifetime.
struct NvDriverLifetime {
    HMODULE module=nullptr;
    using Query=void* (__cdecl*)(NvU32);
    Query query=nullptr;
    NvAPI_Status status=NVAPI_API_NOT_INITIALIZED;
    NvDriverLifetime(){
        module=LoadLibraryExW(L"nvapi64.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)return;
        auto raw=GetProcAddress(module,"nvapi_QueryInterface");std::memcpy(&query,&raw,sizeof(query));
        if(query)for(const auto& entry:nvapi_interface_table)if(!std::strcmp(entry.func,"NvAPI_Initialize")){
            auto init=reinterpret_cast<decltype(&NvAPI_Initialize)>(query(entry.id));if(init)status=init();break;
        }
        if(status!=NVAPI_OK){FreeLibrary(module);module=nullptr;query=nullptr;}
    }
};
const NvDriverLifetime& driverLifetime(){static const NvDriverLifetime resident;return resident;}
}
namespace lmuqv::nv_vrs_detail {
Api::~Api(){close();}
void* Api::function(const char* name)const{
    if(!query)return nullptr;
    for(const auto& entry:nvapi_interface_table)if(!std::strcmp(entry.func,name))return query(entry.id);
    return nullptr;
}
bool Api::open(){
    if(initialized)return true;
    const auto& resident=driverLifetime();module=resident.module;query=resident.query;status=resident.status;
    if(!module||!query||status!=NVAPI_OK)return false;
    initialize=reinterpret_cast<decltype(initialize)>(function("NvAPI_Initialize"));unload=reinterpret_cast<decltype(unload)>(function("NvAPI_Unload"));
    caps=reinterpret_cast<decltype(caps)>(function("NvAPI_D3D1x_GetGraphicsCapabilities"));
    rates=reinterpret_cast<decltype(rates)>(function("NvAPI_D3D11_RSSetViewportsPixelShadingRates"));
    createView=reinterpret_cast<decltype(createView)>(function("NvAPI_D3D11_CreateShadingRateResourceView"));
    setView=reinterpret_cast<decltype(setView)>(function("NvAPI_D3D11_RSSetShadingRateResourceView"));
    if(!initialize||!unload||!caps||!rates||!createView||!setView){close();return false;}
    initialized=true;return true;
}
void Api::close(){
    initialized=false;
    module=nullptr;query=nullptr;
}
bool Api::supported(ID3D11Device* device){
    if(!device||!open())return false;
    NV_D3D1x_GRAPHICS_CAPS value{};status=caps(device,NV_D3D1x_GRAPHICS_CAPS_VER,&value);
    return status==NVAPI_OK&&value.bVariablePixelRateShadingSupported;
}
bool Api::set_uniform(ID3D11DeviceContext* context,NV_PIXEL_SHADING_RATE rate,bool enabled){
    std::array<NV_D3D11_VIEWPORT_SHADING_RATE_DESC,NV_MAX_NUM_VIEWPORTS> viewports{};
    for(auto& view:viewports){view.enableVariablePixelShadingRate=enabled;for(auto& value:view.shadingRateTable)value=rate;}
    NV_D3D11_VIEWPORTS_SHADING_RATE_DESC desc{};desc.version=NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER;desc.numViewports=NvU32(viewports.size());desc.pViewports=viewports.data();
    status=rates(context,&desc);return status==NVAPI_OK;
}
bool Api::disable(ID3D11DeviceContext* context){
    bool okay=set_uniform(context,NV_PIXEL_X1_PER_RASTER_PIXEL,false);
    NV_D3D11_VIEWPORTS_SHADING_RATE_DESC desc{};desc.version=NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER;
    status=rates(context,&desc);okay=status==NVAPI_OK&&okay;
    status=setView(context,nullptr);return status==NVAPI_OK&&okay;
}
}
namespace lmuqv {
struct NvVrs::Impl {
    nv_vrs_detail::Api api;
    ID3D11DeviceContext* context=nullptr;ID3D11Device* device=nullptr;
    ID3D11Texture2D* resource=nullptr;ID3D11NvShadingRateResourceView* view=nullptr;
    NvVrsLog callback=nullptr;void* user=nullptr;
    unsigned width=0,height=0,rate=2;DWORD ownerThread=0;
    bool ready=false,active=false,fault=false,needsRestore=false,foveated=false;
    std::vector<unsigned char> map;
    ~Impl(){if(view)view->Release();if(resource)resource->Release();if(context)context->Release();if(device)device->Release();}
    void log(const char* message){if(callback)callback(user,message);}
    bool restore(){
        if(!context||!api.initialized)return false;
        if(GetCurrentThreadId()!=ownerThread){log("nv_vrs reset_failed reason=wrong_render_thread; baseline restoration not proven");return false;}
        bool okay=api.disable(context);active=false;needsRestore=!okay;
        if(!okay){char text[192]{};std::snprintf(text,sizeof(text),"nv_vrs reset_failed status=%d; VRS disabled for this helper; discard/restart context",int(api.status));log(text);}
        return okay;
    }
};
NvVrs::NvVrs()=default;NvVrs::~NvVrs(){reset();}
bool NvVrs::configure(ID3D11DeviceContext* context,unsigned width,unsigned height,unsigned rate,bool enable,NvVrsOwnership ownership,NvVrsLog callback,void* user){
    reset();
    auto message=[&](const char* text){if(callback)callback(user,text);};
    if(!enable)return false;
    if(resetFault_){message("nv_vrs refused: prior reset failure is sticky; construct a new helper only with a new controlled context");return false;}
    if(ownership!=NvVrsOwnership::ExclusiveKnownDisabled){message("nv_vrs refused: no public VRS state getters; explicit ExclusiveKnownDisabled ownership required");return false;}
    if(!context||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||!width||!height||width>16384||height>16384||(rate!=1&&rate!=2&&rate!=4)){
        message("nv_vrs refused: invalid context, dimensions or rate (expected1/2/4)");return false;
    }
    impl_=std::make_unique<Impl>();auto& state=*impl_;
    state.callback=callback;state.user=user;state.width=width;state.height=height;state.rate=rate;state.ownerThread=GetCurrentThreadId();
    state.context=context;context->AddRef();context->GetDevice(&state.device);
    if(!state.api.supported(state.device)){
        char text[160]{};std::snprintf(text,sizeof(text),"nv_vrs unsupported status=%d; no VRS setter called",int(state.api.status));state.log(text);state.fault=true;return false;
    }
    D3D11_TEXTURE2D_DESC texture{};texture.Width=(width+NV_VARIABLE_PIXEL_SHADING_TILE_WIDTH-1)/NV_VARIABLE_PIXEL_SHADING_TILE_WIDTH;
    texture.Height=(height+NV_VARIABLE_PIXEL_SHADING_TILE_HEIGHT-1)/NV_VARIABLE_PIXEL_SHADING_TILE_HEIGHT;
    texture.MipLevels=texture.ArraySize=1;texture.Format=DXGI_FORMAT_R8_UINT;texture.SampleDesc.Count=1;
    texture.Usage=D3D11_USAGE_DYNAMIC;texture.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    std::vector<unsigned char> indices(size_t(texture.Width)*texture.Height,0);
    D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=indices.data();initial.SysMemPitch=texture.Width;
    HRESULT hr=state.device->CreateTexture2D(&texture,&initial,&state.resource);
    if(FAILED(hr)){state.log("nv_vrs resource creation failed; no VRS setter called");state.fault=true;return false;}
    NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC desc{};desc.version=NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC_VER;
    desc.Format=DXGI_FORMAT_R8_UINT;desc.ViewDimension=NV_SRRV_DIMENSION_TEXTURE2D;
    state.api.status=state.api.createView(state.device,state.resource,&desc,&state.view);
    if(state.api.status!=NVAPI_OK){state.log("nv_vrs resource view creation failed; no VRS setter called");state.fault=true;return false;}
    state.needsRestore=true;
    if(!state.restore()){resetFault_=true;state.fault=true;return false;}
    state.ready=true;
    char text[256]{};std::snprintf(text,sizeof(text),"nv_vrs ready rate=%ux%u viewport=%ux%u ownership=exclusive_known_disabled; restores disabled/1x1 and null rate view",rate,rate,width,height);state.log(text);
    return true;
}
bool NvVrs::begin_outer(ID3D11DeviceContext* context){
    if(!impl_||!impl_->ready||impl_->fault||resetFault_)return false;
    auto& state=*impl_;
    if(context!=state.context||GetCurrentThreadId()!=state.ownerThread||state.active){
        state.log("nv_vrs invalid/overlapping begin; scope discarded and baseline restoration requested");
        if(state.needsRestore&&!state.restore())resetFault_=true;
        state.active=false;state.ready=false;state.fault=true;return false;
    }
    // Configure established the exclusive disabled baseline. The rate1 control
    // measures the real optimization cost: no per-draw NVAPI setters in baseline.
    if(state.rate==1){state.active=true;return true;}
    state.needsRestore=true;state.api.status=state.api.setView(context,state.view);
    NV_PIXEL_SHADING_RATE rate=state.rate==2?NV_PIXEL_X1_PER_2X2_RASTER_PIXELS:NV_PIXEL_X1_PER_4X4_RASTER_PIXELS;
    bool ratesOkay=false;
    if(state.api.status==NVAPI_OK&&state.foveated){
        std::array<NV_D3D11_VIEWPORT_SHADING_RATE_DESC,NV_MAX_NUM_VIEWPORTS> viewports{};
        for(auto& viewport:viewports){
            viewport.enableVariablePixelShadingRate=true;
            for(auto& entry:viewport.shadingRateTable)entry=NV_PIXEL_X1_PER_RASTER_PIXEL;
            viewport.shadingRateTable[1]=rate;
        }
        NV_D3D11_VIEWPORTS_SHADING_RATE_DESC desc{};desc.version=NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER;
        desc.numViewports=NvU32(viewports.size());desc.pViewports=viewports.data();
        state.api.status=state.api.rates(context,&desc);ratesOkay=state.api.status==NVAPI_OK;
    }else if(state.api.status==NVAPI_OK)ratesOkay=state.api.set_uniform(context,rate);
    if(!ratesOkay){
        state.log("nv_vrs begin failed; no applied scope claimed");if(!state.restore())resetFault_=true;
        state.ready=false;state.fault=true;return false;
    }
    state.active=true;return true;
}
bool NvVrs::update_focus(ID3D11DeviceContext* context,const VrsFocus& focus){
    if(!ready()||impl_->active||context!=impl_->context||GetCurrentThreadId()!=impl_->ownerThread)return false;
    auto& state=*impl_;
    auto map=vrs_map(state.width,state.height,focus);
    if(map.empty())return false;
    if(!state.foveated||map!=state.map){
        // Updating an SRRV-backed DEFAULT texture with UpdateSubresource caused
        // intermittent NVIDIA device-teardown faults in the real-driver probe.
        // Dynamic discard uploads preserve both moving maps and clean teardown.
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if(FAILED(context->Map(state.resource,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
        const unsigned pitch=(state.width+15)/16,rows=(state.height+15)/16;
        if(!mapped.pData||mapped.RowPitch<pitch){context->Unmap(state.resource,0);return false;}
        for(unsigned y=0;y<rows;++y)std::memcpy(static_cast<unsigned char*>(mapped.pData)+size_t(y)*mapped.RowPitch,map.data()+size_t(y)*pitch,pitch);
        context->Unmap(state.resource,0);
        state.map=std::move(map);
    }
    state.foveated=true;return true;
}
bool NvVrs::end_outer(ID3D11DeviceContext* context){
    if(!impl_||!impl_->active)return false;
    auto& state=*impl_;bool same=context==state.context&&GetCurrentThreadId()==state.ownerThread;
    if(state.rate==1){
        state.active=false;
        if(!same){state.fault=true;state.ready=false;state.log("nv_vrs rate1 end failed/context mismatch; baseline unchanged; no applied scope claimed");}
        return same;
    }
    bool restored=state.restore();if(!restored)resetFault_=true;
    if(!same||!restored){state.fault=true;state.ready=false;state.log("nv_vrs end failed/context mismatch; no applied scope claimed");return false;}
    return true;
}
void NvVrs::reset(){
    if(impl_&&impl_->needsRestore&&!impl_->restore()){resetFault_=true;impl_->fault=true;}
    impl_.reset(); // A failed baseline restore intentionally remains observable through resetFault_.
}
bool NvVrs::ready()const{return impl_&&impl_->ready&&!impl_->fault&&!resetFault_;}
bool NvVrs::active()const{return impl_&&impl_->active;}
bool NvVrs::faulted()const{return resetFault_||(impl_&&impl_->fault);}
}
