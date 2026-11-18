#pragma once
#include "overlay_renderer.hpp"
#include <filesystem>
#include <memory>
#include <string>

struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D12Resource;
namespace Microsoft::WRL { template<class T> class ComPtr; }

namespace og {
class SessionD3D11On12;
enum class HelmetVisorPosition : int32_t { Closed = 0, Half = 1, Open = 2 };
struct HelmetShortcutSample { bool control{}, alt{}, v{}, foreground{}; };
// Update even while inactive, so holding the shortcut while switching focus
// cannot cycle the visor. Returns true only for an allowed rising edge.
bool helmet_visor_shortcut(bool &held, bool allowed, HelmetShortcutSample);
struct HelmetConfig {
    bool enabled{};
    std::string leftImage, rightImage, generation;
    // Percent of image width per eye; positive separates the artwork outward.
    float stereoOffset{}, ambientLight{}, wheelAmbientLight{};
    bool visorHotkey{};
    HelmetVisorPosition visorPosition{HelmetVisorPosition::Closed};
    std::string halfImage, openImage;
};
bool parse_helmet_config(const std::string &, const std::string &game, HelmetConfig &);
bool helmet_quad_geometry(const XrView &, XrPosef &, XrExtent2Df &);
// A common head-space image envelope prevents asymmetric/canted eye frusta
// from mapping the same flat image pixel to two different viewing directions.
bool helmet_shared_fov(const XrView &left, const XrView &right, XrFovf &);
struct HelmetStatus {
    bool ready{};
    bool cycleAvailable{};
    bool imageReduced{}; // Artwork was fitted once to the runtime's padded image limit.
    unsigned uploads{};
    std::string error;
};
class HelmetRenderer {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    HelmetRenderer();
    ~HelmetRenderer();
    bool initialize(XrSession, const void *graphicsBinding, uint32_t maxLayers,
                    const OverlayDispatch &, PFN_xrLocateViews, const std::string &game,
                    const std::filesystem::path &directory = {},
                    uint32_t maxSwapchainWidth = 0, uint32_t maxSwapchainHeight = 0,
                    bool allowSceneLight = true, std::shared_ptr<SessionD3D11On12> bridge = {});
    void reset();
    void viewConfiguration(XrViewConfigurationType);
    DashboardLayerOrder append(const XrFrameEndInfo &, bool shouldRender,
                std::vector<const XrCompositionLayerBaseHeader *> &, int32_t *visorPosition = nullptr);
    // The runtime fixes usage at creation. An enabled helmet can start sampling
    // later when the driver changes lighting during a session.
    bool needsReadableScene() const;
    bool wantsSceneLight() const;
    // Retain the last fresh GPU sample while drawing. An empty view selects
    // the wheel's original material, including when only helmet lighting is on.
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> wheelLightProbe() const;
    // Observe application projections before composition.
    // Only capture from a successfully waited image before forwarding its release.
    void sceneProjection(const XrFrameEndInfo&);
    void forgetScene(XrSwapchain);
    void captureScene(XrSwapchain, int64_t format, ID3D11Texture2D*, ID3D12Resource*);
    // Explicit input seam for deterministic tests/hosts; no OS key injection.
    void shortcutSample(HelmetShortcutSample);
    HelmetStatus status() const;
    bool cycleVisor(bool focused);
    void cancelVisorCycle();
};
} // namespace og
