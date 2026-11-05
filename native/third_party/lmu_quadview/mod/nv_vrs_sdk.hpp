#pragma once
#include <windows.h>
#include <d3d11_1.h>
#include <cstring>
#include <string>
#include <array>
#include <vector>
#include <memory>
// NVIDIA's nested SAL headers otherwise undefine annotations too early under MinGW.
// Preserve annotations during the official include, then use its own cleanup once.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#pragma GCC diagnostic ignored "-Wpedantic"
#define __NVAPI_EMPTY_SAL
#endif
#include "vendor/nvapi/nvapi.h"
#if defined(__GNUC__)
#undef __NVAPI_EMPTY_SAL
#include "vendor/nvapi/nvapi_lite_salend.h"
#pragma GCC diagnostic pop
#endif
namespace lmuqv::nv_vrs_detail {
struct Api {
    HMODULE module=nullptr;bool initialized=false;
    using Query=void* (__cdecl*)(NvU32);
    Query query=nullptr;
    decltype(&NvAPI_Initialize) initialize=nullptr;
    decltype(&NvAPI_Unload) unload=nullptr;
    decltype(&NvAPI_D3D1x_GetGraphicsCapabilities) caps=nullptr;
    decltype(&NvAPI_D3D11_RSSetViewportsPixelShadingRates) rates=nullptr;
    decltype(&NvAPI_D3D11_CreateShadingRateResourceView) createView=nullptr;
    decltype(&NvAPI_D3D11_RSSetShadingRateResourceView) setView=nullptr;
    NvAPI_Status status=NVAPI_API_NOT_INITIALIZED;
    Api()=default;~Api();Api(const Api&)=delete;Api& operator=(const Api&)=delete;
    bool open();void close();void* function(const char*)const;
    bool supported(ID3D11Device*);
    bool set_uniform(ID3D11DeviceContext*,NV_PIXEL_SHADING_RATE,bool enabled=true);
    bool disable(ID3D11DeviceContext*);
};
}
