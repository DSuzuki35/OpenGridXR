#pragma once
#include "stereo_shader.hpp"

namespace lmuqv {
// One entry per family, not per view. Translate to NV_CUSTOM_SEMANTIC with
// RegisterSpecified=FALSE and all optional/reserved fields zero.
struct NvStereoCustomSemantic { uint32_t type=0; std::string name; };
struct NvStereoShaderVariant : StereoShaderVariant {
    unsigned views=0;
    std::vector<NvStereoCustomSemantic> customSemantics;
};
// Bounded SM4 adapter for NVIDIA MVR2/MVR4 custom semantics (not Pascal SPS).
// Requires MultiView support and SetMultiViewMode, array offsets 0..views-1,
// independentViewportMaskEnable=false, and one shared viewport. The VS reruns its body
// for each view, preserves native instance IDs, and exports every varying per
// view. PS inputs refer to view zero; NVAPI selects the rasterized view.
// Selected constants occupy views banks of eyeStride float4 registers at b13:
// bank(view)[range.offset + sourceRegister]. Caller owns immutable snapshots.
// Does not bind shaders, modify native buffers, combine/suppress native draws,
// or assert that a device supports MVR. Unsupported programs fail closed.
bool convert_nv_stereo_shader(const void*,size_t,StereoShaderStage,uint32_t constantSlots,
                              unsigned views,NvStereoShaderVariant&,std::string& error);
}
