#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lmuqv {
// Research compiler only: this does not install hooks, replace a game shader,
// combine visibility lists or suppress native draws. The caller must provide
// correct per-eye constants/resources and a compatible array-target pipeline.
enum class StereoShaderStage {vertex,pixel};
struct StereoConstantRange {unsigned sourceSlot=0,registers=0,offset=0;};
struct StereoShaderVariant {
    std::vector<uint8_t> bytecode;
    std::vector<StereoConstantRange> constants;
    unsigned packedSlot=13,eyeStride=0;
    // Exact SM4 sampler declarations; body operands must reference a declared
    // immediate slot. Allows ignoring stale, unused sampler bindings safely.
    uint32_t samplerSlots=0;
    std::string geometrySource;
};
// The first supported family is legacy SM4 VS/PS with ordinary signatures and
// immediate-indexed eye-dependent CBs. Unsupported instructions/layouts fail
// closed. Other CBs/resources keep their original bindings. Each packed CB has
// two eyeStride-register banks; the chosen CB ranges occupy the listed offsets.
bool convert_stereo_shader(const void*,size_t,StereoShaderStage,uint32_t constantSlots,
                           bool vertexArrayRouting,StereoShaderVariant&,std::string& error);
}
