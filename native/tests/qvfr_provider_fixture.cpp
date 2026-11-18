#include <windows.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <cstring>
namespace {
PFN_xrGetInstanceProcAddr downstream;
unsigned calls;
XrResult XRAPI_CALL proc(XrInstance instance,const char* name,PFN_xrVoidFunction* out) {
    return downstream(instance,name,out);
}
XrResult XRAPI_CALL create(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* layer,XrInstance* instance) {
    if(!layer||!layer->nextInfo||std::strcmp(layer->nextInfo->layerName,"XR_APILAYER_MBUCCHIA_quad_views_foveated"))return XR_ERROR_VALIDATION_FAILURE;
    bool quad=false;
    for(uint32_t i=0;i<info->enabledExtensionCount;++i)if(!std::strcmp(info->enabledExtensionNames[i],"XR_VARJO_quad_views"))quad=true;
    if(!quad)return XR_ERROR_EXTENSION_NOT_PRESENT;
    auto next=layer->nextInfo;auto forwarded=*layer;forwarded.nextInfo=next->next;
    downstream=next->nextGetInstanceProcAddr;++calls;
    return next->nextCreateApiLayerInstance(info,&forwarded,instance);
}
}
extern "C" __declspec(dllexport) unsigned fixtureCalls(){return calls;}
extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo*,const char* name,XrNegotiateApiLayerRequest* api) {
    if(std::strcmp(name,"XR_APILAYER_MBUCCHIA_quad_views_foveated"))return XR_ERROR_INITIALIZATION_FAILED;
    api->layerInterfaceVersion=1;api->layerApiVersion=XR_CURRENT_API_VERSION;
    api->getInstanceProcAddr=proc;api->createApiLayerInstance=create;return XR_SUCCESS;
}
