#pragma once
#include <memory>
#include <openxr/openxr.h>
#include <string>
#include <vector>
namespace og {
struct OverlayDispatch {
    PFN_xrEnumerateSwapchainFormats formats{};
    PFN_xrCreateSwapchain create{};
    PFN_xrEnumerateSwapchainImages images{};
    PFN_xrAcquireSwapchainImage acquire{};
    PFN_xrWaitSwapchainImage wait{};
    PFN_xrReleaseSwapchainImage release{};
    PFN_xrDestroySwapchain destroy{};
    PFN_xrCreateReferenceSpace createSpace{};
    PFN_xrDestroySpace destroySpace{};
    PFN_xrLocateSpace locate{};
};
class OverlayRenderer {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    OverlayRenderer();
    ~OverlayRenderer();
    bool initialize(XrSession, const void *graphicsBinding, uint32_t maxLayers,
                    const OverlayDispatch &, const std::string &game);
    void reset();
    void append(const XrFrameEndInfo &, bool enabled, bool shouldRender,
                std::vector<const XrCompositionLayerBaseHeader *> &);
};
} // namespace og
