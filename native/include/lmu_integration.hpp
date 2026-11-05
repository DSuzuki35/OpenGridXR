#pragma once
// Private, statically linked boundary. No exported API or registered layer.
#include "settings.hpp"
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <cstdlib>
#include <cstring>
#include <utility>
namespace og::lmu {
struct Frame {
    uint64_t id{};
    XrTime time{};
    XrSpace space{};
    XrView views[4]{};
    XrFovf fullOuterFov[2]{};
    uint32_t widths[4]{}, heights[4]{};
};
struct HostCallbacks {
    // Texture is waited and owned by the adapter only for this call. Copy it
    // now if needed later. Slots 0/1 are cropped outers, 2/3 final focus eyes.
    // Native eye identity proves these are complete single-slice final eye
    // textures. Region is exactly {0,0,0,width,height}; no atlas or array views.
    void (*image)(XrSession, const Frame&, unsigned, ID3D11Texture2D*, DXGI_FORMAT,
                  const ImageRegion&){};
    // Called after successful downstream submission. Only owned copies may be
    // consumed here: runtime images are released.
    void (*submit)(XrSession, const Frame&, const XrCompositionLayerProjection&, XrTime){};
};
struct Settings {
    bool enabled{}, quad{}, expansionUnsupported{};
    Edges crop;
    std::string provider;
};
inline Settings settings;
inline HostCallbacks host;
inline std::vector<std::pair<std::string, std::string>> savedEnvironment;
inline std::string environment(const char* name) {
    char value[128]{};const DWORD length=GetEnvironmentVariableA(name,value,sizeof(value));
    return length<sizeof(value)?std::string(value,length):std::string("invalid");
}
inline void restore_environment() {
    for (const auto& entry : savedEnvironment)
        SetEnvironmentVariableA(entry.first.c_str(), entry.second.empty() ? nullptr : entry.second.c_str());
    savedEnvironment.clear();
}
// Only the ordinary implicit-layer path uses saved settings. Explicit offline
// capture/launch environments remain immutable and keep their existing guards.
inline bool prepare_saved(const Config& config, const std::string& game) {
    if (!config.enabled || game != "lmu" || config.lmu.mode == "off" ||
        !environment("OPENGRIDXR_LMU_INTEGRATED").empty()) return true;
    auto set = [](const char* key, const std::string& value) {
        DWORD length = GetEnvironmentVariableA(key, nullptr, 0);
        std::string old(length, '\0');
        if (length) { GetEnvironmentVariableA(key, old.data(), length); old.resize(length - 1); }
        savedEnvironment.emplace_back(key, std::move(old));
        if (!SetEnvironmentVariableA(key, value.c_str())) { restore_environment(); return false; }
        return true;
    };
    const auto& q = config.lmu;
    const bool vrs = q.mode == "vrs";
    for (const char* suffix : {"BENCHMARK", "DYNAMIC_FOCUS", "PIXEL_STATS", "PROFILE", "STEREO_AUDIT",
         "STEREO_SHADER_CAPTURE", "STEREO_DRAW_PROOF", "CPU_UPLOAD_REUSE", "CPU_UPLOAD_BATCH", "CPU_OUTER_REUSE", "OUTER_SSR"})
        if (!set((std::string("LMU_QV_") + suffix).c_str(), "0")) return false;
    for (const char* suffix : {"CAPTURE_DIR", "CAPTURE_TRIGGER", "GPU_TIMING_TRIGGER", "BENCH_CONTROL", "LOG"})
        if (!set((std::string("LMU_QV_") + suffix).c_str(), "")) return false;
    return set("OPENGRIDXR_LMU_INTEGRATED", "1") && set("OPENGRIDXR_LMU_PROVIDER", q.provider) &&
        set("LMU_QV_MODE", q.mode) && set("LMU_QV_USE_RUNTIME_SETTINGS", vrs ? "0" : "1") &&
        set("LMU_QV_FOVEATION_MODE", q.gaze) && set("LMU_QV_DIAGNOSTICS", "1") &&
        set("LMU_QV_NATIVE_OUTER", !vrs && q.nativeOuter ? "1" : "0") &&
        set("LMU_QV_VRS_EXCLUSIVE", vrs ? "1" : "0") &&
        set("LMU_QV_VECTORXR_DIRECT_INPUTS", !vrs && q.provider == "vectorxr" ? "1" : "0") &&
        set("LMU_QV_OUTER_SHADING_RATE", std::to_string(vrs ? q.shadingRate : 2)) &&
        set("LMU_QV_DEBUG_FOCUS", q.debugFocus ? "1" : "0") &&
        set("LMU_QV_OUTER_BLUR", std::to_string(vrs ? 0 : q.smoothing)) &&
        set("LMU_QV_CROP_WIDTH_PERCENT", "100") && set("LMU_QV_CROP_HEIGHT_PERCENT", "100") &&
        set("LMU_QV_FOCUS_OFFSET_X_PERCENT", std::to_string(q.offsetX)) &&
        set("LMU_QV_FOCUS_OFFSET_Y_PERCENT", std::to_string(q.offsetY)) &&
        set("LMU_QV_FOCUS_WIDTH_PERCENT", std::to_string(vrs ? q.focusWidth : 100)) &&
        set("LMU_QV_FOCUS_HEIGHT_PERCENT", std::to_string(vrs ? q.focusHeight : 100)) &&
        set("LMU_QV_OUTER_PIXELS_PERCENT", "100") && set("LMU_QV_FOCUS_PIXELS_PERCENT", "100") &&
        set("LMU_QV_FOCUS_BOX_PERCENT", "100");
}
inline bool configure(const Config& config, const std::string& game, HostCallbacks callbacks) {
    const std::string mode=environment("LMU_QV_MODE"), provider=environment("OPENGRIDXR_LMU_PROVIDER");
    const bool requested=config.enabled && game=="lmu" && environment("OPENGRIDXR_LMU_INTEGRATED")=="1";
    if (!requested) return false;
    if (settings.enabled || (mode!="quad" && mode!="vrs") ||
        (provider!="native" && provider!="qvfr" && provider!="vectorxr")) return false;
    settings.enabled=true; settings.quad=mode=="quad"; settings.provider=provider;
    settings.crop=config.streaming?Edges{}:config.crop;
    settings.expansionUnsupported=config.streaming && (config.extraLeft>0 || config.extraRight>0);
    host=callbacks;
    return true;
}
inline void reset() { settings={}; host={}; restore_environment(); }
XrResult XRAPI_CALL get_proc(XrInstance, const char*, PFN_xrVoidFunction*);
XrResult XRAPI_CALL create_instance(const XrInstanceCreateInfo*, const XrApiLayerCreateInfo*, XrInstance*);
}
