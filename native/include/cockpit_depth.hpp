#pragma once
#include <d3d11.h>
#include <openxr/openxr.h>

namespace og {
// Each view is a private single-sample Texture2DArray<float> SRV. Slice indices
// are relative to that SRV. Pose is eye-in-space at the producer's display time.
struct CockpitDepthEye {
    ID3D11ShaderResourceView *view{};
    XrPosef pose{};
    XrFovf fov{};
    XrRect2Di rect{};
    uint32_t slice{};
    float minDepth{}, maxDepth{1}, invNear{}, invFar{};
};
struct CockpitDepthFrame {
    XrSpace space{};
    XrTime time{};
    CockpitDepthEye eyes[2]{};
};
// On success the caller owns one reference per eye (also for a shared SRV).
// A null output updates demand without retrieving a frame.
using LmuCockpitDepthFn = bool(__cdecl *)(ID3D11Device *, XrSession, XrTime, bool,
                                          CockpitDepthFrame *);
// Read the released depth for the pending app submission while the host
// serializes BeginFrame, EndFrame and application swapchain releases.
using IRacingCockpitDepthFn = bool(__cdecl *)(XrSession, const XrFrameEndInfo *, CockpitDepthFrame *);
} // namespace og
