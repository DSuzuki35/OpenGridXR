#pragma once
#include "nv_vrs_sdk.hpp"

// Standalone eligibility reporting only. This header is not part of the game
// layer and exposes no mode setters, shader replacement or draw suppression.
namespace lmuqv::nv_multiview {
struct Api {
    decltype(&NvAPI_Initialize) initialize=nullptr;
    decltype(&NvAPI_D3D_QuerySinglePassStereoSupport) sps=nullptr;
    decltype(&NvAPI_D3D_QueryMultiViewSupport) multiview=nullptr;
};
struct Capabilities {
    NvAPI_Status initialization=NVAPI_LIBRARY_NOT_FOUND;
    NvAPI_Status spsStatus=NVAPI_NO_IMPLEMENTATION,multiviewStatus=NVAPI_NO_IMPLEMENTATION;
    bool singlePassStereo=false,singlePassStereoXYZW=false;
    bool multiView=false,multiviewStereo=false,multiviewStereoXYZW=false;
    unsigned maximumViews=0;
};
inline Capabilities query_capabilities(IUnknown* device,const Api& api){
    Capabilities result;
    if(!device){result.initialization=NVAPI_INVALID_ARGUMENT;return result;}
    if(!api.initialize)return result;
    result.initialization=api.initialize();
    if(result.initialization!=NVAPI_OK)return result;
    if(api.sps){
        NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS support{};
        support.version=NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS_VER2;
        result.spsStatus=api.sps(device,&support);
        if(result.spsStatus==NVAPI_OK){
            result.singlePassStereo=support.bSinglePassStereoSupported!=0;
            result.singlePassStereoXYZW=support.bSinglePassStereoXYZWSupported!=0;
        }
    }
    if(api.multiview){
        NV_QUERY_MULTIVIEW_SUPPORT_PARAMS support{};
        support.version=NV_QUERY_MULTIVIEW_SUPPORT_PARAMS_VER1;
        result.multiviewStatus=api.multiview(device,&support);
        if(result.multiviewStatus==NVAPI_OK){
            result.multiView=support.bMultiViewSupported!=0;
            result.multiviewStereo=support.bSinglePassStereoSupported!=0;
            result.multiviewStereoXYZW=support.bSinglePassStereoXYZWSupported!=0;
        }
    }
    result.maximumViews=result.multiView?NV_MULTIVIEW_MAX_SUPPORTED_VIEWS:
        (result.singlePassStereo||result.singlePassStereoXYZW||result.multiviewStereo||result.multiviewStereoXYZW?2u:0u);
    return result;
}
inline Capabilities query_capabilities(IUnknown* device){
    struct Driver {
        HMODULE module=nullptr;
        Api api;
        Driver(){
            module=LoadLibraryExW(L"nvapi64.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            if(!module)return;
            using Query=void* (__cdecl*)(NvU32);
            Query query=nullptr;auto raw=GetProcAddress(module,"nvapi_QueryInterface");
            std::memcpy(&query,&raw,sizeof(query));
            if(!query)return;
            // IDs from the pinned vendor/nvapi/nvapi_interface.h.
            api.initialize=reinterpret_cast<decltype(api.initialize)>(query(0x0150e828));
            api.sps=reinterpret_cast<decltype(api.sps)>(query(0x6f5f0a6d));
            api.multiview=reinterpret_cast<decltype(api.multiview)>(query(0xb6e0a41c));
        }
        // Keep NVAPI resident until process exit, matching nv_vrs.cpp's lifetime
        // rule. Never unload driver integration while a D3D device may exist.
    };
    static const Driver resident;
    return query_capabilities(device,resident.api);
}
}
