#include <cstring>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
namespace {
PFN_xrGetInstanceProcAddr downstream{};
PFN_xrEnumerateViewConfigurationViews sizes{};
XrResult XRAPI_CALL enumerate(XrInstance instance, XrSystemId system, XrViewConfigurationType type,
                              uint32_t capacity, uint32_t *count, XrViewConfigurationView *views) {
    auto result = sizes(instance, system, type, capacity, count, views);
    if (XR_SUCCEEDED(result) && type == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO && views &&
        capacity >= *count)
        for (uint32_t n = 0; n < *count; ++n)
            views[n].recommendedImageRectWidth += 200;
    return result;
}
XrResult XRAPI_CALL get(XrInstance instance, const char *name, PFN_xrVoidFunction *out) {
    if (!std::strcmp(name, "xrEnumerateViewConfigurationViews")) {
        *out = reinterpret_cast<PFN_xrVoidFunction>(enumerate);
        return XR_SUCCESS;
    }
    return downstream(instance, name, out);
}
XrResult XRAPI_CALL create(const XrInstanceCreateInfo *info, const XrApiLayerCreateInfo *layer,
                           XrInstance *instance) {
    if (!layer || !layer->nextInfo ||
        std::strcmp(layer->nextInfo->layerName, "XR_APILAYER_OPENGRIDXR_ENHANCEMENTS"))
        return XR_ERROR_INITIALIZATION_FAILED;
    downstream = layer->nextInfo->nextGetInstanceProcAddr;
    auto next = *layer;
    next.nextInfo = layer->nextInfo->next;
    auto result = layer->nextInfo->nextCreateApiLayerInstance(info, &next, instance);
    if (XR_SUCCEEDED(result))
        downstream(*instance, "xrEnumerateViewConfigurationViews",
                   reinterpret_cast<PFN_xrVoidFunction *>(&sizes));
    return result;
}
} // namespace
extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo *, const char *name, XrNegotiateApiLayerRequest *request) {
    if (std::strcmp(name, "XR_APILAYER_OPENGRIDXR_ENHANCEMENTS"))
        return XR_ERROR_INITIALIZATION_FAILED;
    request->layerInterfaceVersion = 1;
    request->layerApiVersion = XR_CURRENT_API_VERSION;
    request->getInstanceProcAddr = get;
    request->createApiLayerInstance = create;
    return XR_SUCCESS;
}
