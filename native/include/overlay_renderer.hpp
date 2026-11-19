#pragma once
#include <algorithm>
#include <memory>
#include <openxr/openxr.h>
#include <string>
#include <vector>
struct ID3D11ShaderResourceView;
namespace og {
class SessionD3D11On12;
struct CockpitDepthFrame;
struct DashboardLayerOrder {
    const XrCompositionLayerBaseHeader *firstHelmetEye{};
    bool helmetAtLayerLimit{};

    void insert(std::vector<const XrCompositionLayerBaseHeader *> &layers, bool dashboard,
                const XrCompositionLayerBaseHeader *layer) const {
        auto position = layers.end();
        if (dashboard && firstHelmetEye)
            position = std::find(layers.begin(), layers.end(), firstHelmetEye);
        layers.insert(position, layer);
    }
    void insert(std::vector<const XrCompositionLayerBaseHeader *> &layers, bool dashboard,
                const XrCompositionLayerQuad &quad) const {
        insert(layers, dashboard, reinterpret_cast<const XrCompositionLayerBaseHeader *>(&quad));
    }
};
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
    void *focusContext{};
    bool (*focused)(void *){};
};
class OverlayRenderer {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    OverlayRenderer();
    ~OverlayRenderer();
    bool initialize(XrSession, const void *graphicsBinding, uint32_t maxLayers,
                    const OverlayDispatch &, const std::string &game,
                    std::shared_ptr<SessionD3D11On12> bridge = {});
    void reset();
    void setInputFocused(bool);
    bool wantsCockpitDepth() const;
    void append(const XrFrameEndInfo &, bool enabled, bool shouldRender,
                std::vector<const XrCompositionLayerBaseHeader *> &,
                const CockpitDepthFrame *depth = nullptr, DashboardLayerOrder order = {},
                ID3D11ShaderResourceView *sceneProbe = nullptr);
};
} // namespace og
