#pragma once
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_NO_PROTOTYPES
#include <windows.h>
#include <d3d11.h>
#include "openxr/openxr.h"
#include "vrs_foveation.hpp"

namespace lmuqv {
// XR side: frame cache valid only from successful locate to end-frame on rendering thread.
bool xr_quad_active();
bool xr_vrs_active();
bool xr_vrs_focus(unsigned eye,VrsFocus& focus);
bool xr_vrs_render_focus(unsigned eye,const float* projection,VrsFocus& focus);
void xr_vrs_scene_complete(unsigned eye);
// Runtime-only selected dimensions. Available before hooks start; exact per eye.
// False in legacy mode or before validated runtime recommendations are available.
bool xr_runtime_sizes(unsigned eye,uint32_t& outerWidth,uint32_t& outerHeight,uint32_t& focusWidth,uint32_t& focusHeight);
bool xr_frame_views(XrView (&views)[4]);
bool xr_frame_full_outer_fov(XrFovf (&fov)[2]);
// Paired-pass integration infrastructure, not an enable switch for stereo.
// Prepare before application swapchains are created. A held, waited image stays
// runtime-owned by this layer if the app releases it early. The rendering owner
// must retire the lease before frame advancement or another acquire on its chain.
// This is the FINAL focus image; it is not the native opaque HDR/depth target.
struct FocusImageLease {
    uint64_t token=0,frame=0;unsigned eye=2,index=0;
    ID3D11Texture2D* texture=nullptr; // Borrowed; valid only while lease_valid().
};
bool xr_prepare_focus_image_ownership();
bool xr_hold_focus_image(unsigned eye,FocusImageLease&);
bool xr_focus_lease_valid(const FocusImageLease&);
bool xr_retire_focus_image(FocusImageLease&);
// Copies source (resolved single-sample color) to acquired outer image for eye 0/1.
// XR side owns acquisition/release; returns false if not active or incompatible.
bool xr_copy_outer(unsigned eye, ID3D11Texture2D* source);
// Borrowed RTV for the captured outer image, still acquired until xrEndFrame.
ID3D11RenderTargetView* xr_outer_rtv(unsigned eye);
// Cached dimensions are validated during outer capture. Same borrowed-image lifetime
// as xr_outer_rtv; unavailable targets return an empty descriptor.
struct OuterTarget {
    ID3D11RenderTargetView* rtv = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
};
OuterTarget xr_outer_target(unsigned eye);
void xr_scene_focus_complete(unsigned eye);
void log_line(const char* message);
void flush_log();

// Engine side, called after successful xrCreateSession and before xrDestroySession.
// No hook in trace mode. Never patch on unrecognized executable/build.
bool engine_start(ID3D11DeviceContext* context=nullptr);
void engine_stop();
// Identity from the pinned game's native XR eye-handle vector, not dimensions
// or creation order. Returns 2 for unproven/auxiliary handles.
unsigned engine_game_eye(XrSwapchain chain);
}
