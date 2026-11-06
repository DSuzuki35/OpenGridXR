#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <windows.h>
#include <d3d11.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstring>
using Microsoft::WRL::ComPtr;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
unsigned instancesCreated=0,sessionsCreated=0,sessionsDestroyed=0;
XrResult XRAPI_CALL unused(){return XR_ERROR_FUNCTION_UNSUPPORTED;}
XrResult XRAPI_CALL destroyInstance(XrInstance){return XR_SUCCESS;}
XrResult XRAPI_CALL systemProps(XrInstance,XrSystemId,XrSystemProperties* properties){
    properties->graphicsProperties.maxLayerCount=16;
    for(auto* next=static_cast<XrBaseOutStructure*>(properties->next);next;next=next->next)
        if(next->type==XR_TYPE_SYSTEM_FOVEATED_RENDERING_PROPERTIES_VARJO)
            reinterpret_cast<XrSystemFoveatedRenderingPropertiesVARJO*>(next)->supportsFoveatedRendering=XR_TRUE;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL sizes(XrInstance,XrSystemId,XrViewConfigurationType type,uint32_t capacity,uint32_t* count,XrViewConfigurationView* views){
    require(type==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO,"adapter requests quad recommendations downstream");*count=4;
    if(!capacity)return XR_SUCCESS;if(capacity<4)return XR_ERROR_SIZE_INSUFFICIENT;
    for(unsigned i=0;i<4;++i){views[i].recommendedImageRectWidth=i<2?512:1024;views[i].recommendedImageRectHeight=i<2?500:1000;
        views[i].maxImageRectWidth=views[i].maxImageRectHeight=4096;views[i].recommendedSwapchainSampleCount=views[i].maxSwapchainSampleCount=1;}
    return XR_SUCCESS;
}
XrResult XRAPI_CALL properties(XrInstance,XrSystemId,XrViewConfigurationType,XrViewConfigurationProperties* out){out->fovMutable=XR_TRUE;return XR_SUCCESS;}
XrResult XRAPI_CALL createSession(XrInstance,const XrSessionCreateInfo*,XrSession* session){++sessionsCreated;*session=reinterpret_cast<XrSession>(12);return XR_SUCCESS;}
XrResult XRAPI_CALL destroySession(XrSession){++sessionsDestroyed;return XR_SUCCESS;}
XrResult XRAPI_CALL proc(XrInstance,const char* name,PFN_xrVoidFunction* out){
#define ENTRY(x,f) if(!std::strcmp(name,"xr" #x)){*out=reinterpret_cast<PFN_xrVoidFunction>(f);return XR_SUCCESS;}
    ENTRY(DestroyInstance,destroyInstance) ENTRY(GetSystemProperties,systemProps)
    ENTRY(EnumerateViewConfigurationViews,sizes) ENTRY(GetViewConfigurationProperties,properties)
    ENTRY(CreateSession,createSession) ENTRY(DestroySession,destroySession)
#undef ENTRY
    *out=reinterpret_cast<PFN_xrVoidFunction>(unused);return XR_SUCCESS;
}
XrResult XRAPI_CALL createInstance(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* layer,XrInstance* instance){
    require(!layer->nextInfo,"private adapter consumes only its own chain node");
    bool quad=false;for(uint32_t i=0;i<info->enabledExtensionCount;++i)if(!std::strcmp(info->enabledExtensionNames[i],XR_VARJO_QUAD_VIEWS_EXTENSION_NAME))quad=true;
    require(quad,"internal adapter enables quad contract");++instancesCreated;*instance=reinterpret_cast<XrInstance>(11);return XR_SUCCESS;
}
template<class T>T get(const XrNegotiateApiLayerRequest& request,XrInstance instance,const char* name){T result{};
    require(request.getInstanceProcAddr(instance,name,reinterpret_cast<PFN_xrVoidFunction*>(&result))==XR_SUCCESS,"proc resolves");return result;}
int wmain(int argc,wchar_t** argv){try{
    require(argc==2,"built DLL argument");
    const auto directory=std::filesystem::path(argv[1]).parent_path()/(L"lmu-layer-test-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);const auto config=directory/L"OpenGridXR.ini";
    {std::ofstream file(config);file<<"[lmu]\nEnabled=1\nEnhancementsEnabled=0\nMode=driving\nCropTop=8\nCropBottom=21\nRenderScale=50\n";}
    SetEnvironmentVariableW(L"OPENGRIDXR_CONFIG",config.c_str());
    SetEnvironmentVariableW(L"OPENGRIDXR_STATUS_DIR",directory.c_str());
    SetEnvironmentVariableA("OPENGRIDXR_LMU_INTEGRATED","1");SetEnvironmentVariableA("OPENGRIDXR_LMU_PROVIDER","native");
    SetEnvironmentVariableA("LMU_QV_MODE","quad");SetEnvironmentVariableA("LMU_QV_DIAGNOSTICS","0");
    SetEnvironmentVariableA("LMU_QV_DYNAMIC_FOCUS","1");SetEnvironmentVariableA("LMU_QV_USE_RUNTIME_SETTINGS",nullptr);
    const auto module=LoadLibraryW(argv[1]);require(module!=nullptr,"built OpenGrid DLL loads");
    auto negotiate=reinterpret_cast<PFN_xrNegotiateLoaderApiLayerInterface>(GetProcAddress(module,"xrNegotiateLoaderApiLayerInterface"));
    require(negotiate!=nullptr,"single public negotiation export");
    XrNegotiateLoaderInfo loader{XR_LOADER_INTERFACE_STRUCT_LOADER_INFO,XR_LOADER_INFO_STRUCT_VERSION,sizeof(XrNegotiateLoaderInfo),1,1,XR_MAKE_VERSION(1,0,0),XR_CURRENT_API_VERSION};
    XrNegotiateApiLayerRequest request{XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST,XR_API_LAYER_INFO_STRUCT_VERSION,sizeof(XrNegotiateApiLayerRequest)};
    require(negotiate(&loader,"XR_APILAYER_OPENGRIDXR",&request)==XR_SUCCESS,"public layer negotiates");
    XrApiLayerNextInfo next{XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO,XR_API_LAYER_NEXT_INFO_STRUCT_VERSION,sizeof(XrApiLayerNextInfo)};
    next.nextGetInstanceProcAddr=proc;next.nextCreateApiLayerInstance=createInstance;
    XrApiLayerCreateInfo layer{XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO,XR_API_LAYER_CREATE_INFO_STRUCT_VERSION,sizeof(XrApiLayerCreateInfo)};layer.nextInfo=&next;
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};info.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);XrInstance instance{};
    require(request.createApiLayerInstance(&info,&layer,&instance)==XR_SUCCESS&&instancesCreated==1,"built DLL activates internal adapter");
    XrViewConfigurationView views[2]{{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}};uint32_t count{};
    require(get<PFN_xrEnumerateViewConfigurationViews>(request,instance,"xrEnumerateViewConfigurationViews")(instance,1,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,views)==XR_SUCCESS,"game enumerates stereo");
    require(count==2&&views[0].recommendedImageRectWidth==1024&&views[0].recommendedImageRectHeight==1000,"base does not independently crop or scale focus swapchains");
    auto sessionCreate=get<PFN_xrCreateSession>(request,instance,"xrCreateSession");XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};sessionInfo.systemId=1;XrSession session{};
    require(sessionCreate(instance,&sessionInfo,&session)==XR_ERROR_INITIALIZATION_FAILED&&sessionsCreated==0,"non-D3D session cannot bypass game guard");
    ComPtr<ID3D11Device> device;require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr)),"WARP");
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};binding.device=device.Get();sessionInfo.next=&binding;
    require(sessionCreate(instance,&sessionInfo,&session)==XR_ERROR_INITIALIZATION_FAILED&&session==XR_NULL_HANDLE,"fixture executable fails game resource/code-anchor guard");
    require(sessionsCreated==1&&sessionsDestroyed==1,"hook rejection cleans downstream session");
    require(get<PFN_xrDestroyInstance>(request,instance,"xrDestroyInstance")(instance)==XR_SUCCESS,"destroy resets private adapter");
    SetEnvironmentVariableA("OPENGRIDXR_LMU_PROVIDER","invalid");
    require(request.createApiLayerInstance(&info,&layer,&instance)==XR_ERROR_VALIDATION_FAILURE&&instancesCreated==1,"invalid provider fails before downstream instance creation");
    // Ordinary Steam startup has no launcher environment. Saved settings must
    // activate the same adapter, still enforcing its executable guard.
    SetEnvironmentVariableA("OPENGRIDXR_LMU_INTEGRATED",nullptr);
    SetEnvironmentVariableA("OPENGRIDXR_LMU_PROVIDER",nullptr);
    SetEnvironmentVariableA("LMU_QV_MODE",nullptr);
    WritePrivateProfileStringW(L"lmu",L"LmuMode",L"quad",config.c_str());
    WritePrivateProfileStringW(L"lmu",L"LmuProvider",L"native",config.c_str());
    require(request.createApiLayerInstance(&info,&layer,&instance)==XR_SUCCESS&&instancesCreated==2,"saved profile activates without Steam wrapper");
    XrInstance duplicate{};
    require(XR_FAILED(request.createApiLayerInstance(&info,&layer,&duplicate)),"second integrated instance rejected");
    char mode[16]{};GetEnvironmentVariableA("LMU_QV_MODE",mode,sizeof(mode));
    require(!std::strcmp(mode,"quad"),"rejected duplicate does not clear the active instance environment");
    count=0;
    require(get<PFN_xrEnumerateViewConfigurationViews>(request,instance,"xrEnumerateViewConfigurationViews")(instance,1,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,views)==XR_SUCCESS&&count==2&&views[0].recommendedImageRectWidth==1024,"automatic mode uses focus recommendations");
    require(get<PFN_xrCreateSession>(request,instance,"xrCreateSession")(instance,&sessionInfo,&session)==XR_ERROR_INITIALIZATION_FAILED,"automatic start retains executable guard");
    require(get<PFN_xrDestroyInstance>(request,instance,"xrDestroyInstance")(instance)==XR_SUCCESS,"automatic destroy");
    WritePrivateProfileStringW(L"lmu",L"LmuProvider",L"qvfr",config.c_str());
    require(request.createApiLayerInstance(&info,&layer,&instance)==XR_SUCCESS&&instancesCreated==3,"automatic QVFR is inserted below adapter when absent downstream");
    const auto providerPath=std::filesystem::path(argv[1]).parent_path()/L"providers"/L"qvfr"/L"OpenGridXR-QVFR.dll";
    auto provider=GetModuleHandleW(providerPath.c_str());require(provider!=nullptr,"isolated provider loaded");
    auto calls=reinterpret_cast<unsigned(*)()>(GetProcAddress(provider,"fixtureCalls"));
    require(calls&&calls()==1,"private provider negotiated with exactly one quad instance");
    require(get<PFN_xrDestroyInstance>(request,instance,"xrDestroyInstance")(instance)==XR_SUCCESS,"private provider destroy");
    // Existing downstream provider is reused, never replaced or loaded twice.
    XrApiLayerNextInfo existing=next;strcpy_s(existing.layerName,"XR_APILAYER_MBUCCHIA_quad_views_foveated");
    auto negotiateProvider=reinterpret_cast<PFN_xrNegotiateLoaderApiLayerInterface>(GetProcAddress(provider,"xrNegotiateLoaderApiLayerInterface"));
    XrNegotiateApiLayerRequest providerApi{XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST,XR_API_LAYER_INFO_STRUCT_VERSION,sizeof(XrNegotiateApiLayerRequest)};
    require(negotiateProvider(&loader,existing.layerName,&providerApi)==XR_SUCCESS,"mock registered provider negotiates");
    next.next=&existing;next.nextGetInstanceProcAddr=providerApi.getInstanceProcAddr;next.nextCreateApiLayerInstance=providerApi.createApiLayerInstance;
    require(request.createApiLayerInstance(&info,&layer,&instance)==XR_SUCCESS&&instancesCreated==4&&calls()==2,"downstream QVFR is used exactly once");
    require(get<PFN_xrDestroyInstance>(request,instance,"xrDestroyInstance")(instance)==XR_SUCCESS,"existing provider destroy");
    FreeLibrary(module);std::cout<<"Built DLL internal routing, automatic startup, uncropped focus sizing and executable guard PASS\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
