#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../autoload_policy.hpp"
#include <cassert>
#include <iostream>
extern "C" XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo*,const char*,XrNegotiateApiLayerRequest*);
static int creates=0,destroys=0,gets=0;
static XrApiLayerNextInfo* expected=nullptr;
static XrResult XRAPI_CALL downstreamDestroy(XrInstance){++destroys;return XR_SUCCESS;}
static XrResult XRAPI_CALL downstreamGet(XrInstance,const char* name,PFN_xrVoidFunction* function) {
    ++gets;*function=!strcmp(name,"xrDestroyInstance")?reinterpret_cast<PFN_xrVoidFunction>(downstreamDestroy):nullptr;
    return *function?XR_SUCCESS:XR_ERROR_FUNCTION_UNSUPPORTED;
}
static XrResult XRAPI_CALL downstreamCreate(const XrInstanceCreateInfo*,const XrApiLayerCreateInfo* layer,XrInstance* instance) {
    assert(layer->nextInfo==expected);*instance=reinterpret_cast<XrInstance>(uintptr_t(++creates));return XR_SUCCESS;
}
int main(){
    XrNegotiateLoaderInfo loader{};loader.structType=XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;loader.structVersion=XR_LOADER_INFO_STRUCT_VERSION;loader.structSize=sizeof(loader);
    loader.minInterfaceVersion=loader.maxInterfaceVersion=1;loader.minApiVersion=XR_MAKE_VERSION(1,0,0);loader.maxApiVersion=XR_CURRENT_API_VERSION;
    XrNegotiateApiLayerRequest request{};request.structType=XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST;request.structVersion=XR_API_LAYER_INFO_STRUCT_VERSION;request.structSize=sizeof(request);
    assert(xrNegotiateLoaderApiLayerInterface(&loader,"wrong",&request)==XR_ERROR_INITIALIZATION_FAILED);
    assert(xrNegotiateLoaderApiLayerInterface(&loader,"XR_APILAYER_LMU_autostart",&request)==XR_SUCCESS);
    XrApiLayerNextInfo head{};head.structType=XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO;head.structVersion=XR_API_LAYER_NEXT_INFO_STRUCT_VERSION;head.structSize=sizeof(head);
    strcpy_s(head.layerName,"XR_APILAYER_LMU_autostart");head.nextCreateApiLayerInstance=downstreamCreate;head.nextGetInstanceProcAddr=downstreamGet;
    XrApiLayerNextInfo sentinel{};head.next=&sentinel;expected=&sentinel;
    XrApiLayerCreateInfo layer{};layer.structType=XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO;layer.structVersion=XR_API_LAYER_CREATE_INFO_STRUCT_VERSION;layer.structSize=sizeof(layer);layer.nextInfo=&head;
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};XrInstance first{},second{};
    SetEnvironmentVariableW(L"LMU_QV_MODE",nullptr);
    assert(request.createApiLayerInstance(&info,&layer,&first)==XR_SUCCESS);
    assert(request.createApiLayerInstance(&info,&layer,&second)==XR_SUCCESS&&creates==2);
    assert(GetEnvironmentVariableW(L"LMU_QV_MODE",nullptr,0)==0); // Foreign host does not configure LMU or start a helper.
    PFN_xrVoidFunction fn=nullptr;
    assert(request.getInstanceProcAddr(first,"xrDestroyInstance",&fn)==XR_SUCCESS&&fn);
    assert(reinterpret_cast<PFN_xrDestroyInstance>(fn)(first)==XR_SUCCESS&&destroys==1);
    assert(request.getInstanceProcAddr(first,"xrDestroyInstance",&fn)==XR_ERROR_FUNCTION_UNSUPPORTED);
    assert(request.getInstanceProcAddr(second,"xrDestroyInstance",&fn)==XR_SUCCESS);
    assert(reinterpret_cast<PFN_xrDestroyInstance>(fn)(second)==XR_SUCCESS&&destroys==2&&gets==2);
    head.structSize=0;assert(request.createApiLayerInstance(&info,&layer,&first)==XR_ERROR_INITIALIZATION_FAILED);
    std::cout<<"Autoload foreign-host passthrough, instance lifecycle and invalid-chain tests passed\n";
}
