#pragma once
#include "focus_mvr_policy.hpp"
#include <d3d11.h>
namespace lmuqv::focus_mvr {
bool early();
bool start(ID3D11DeviceContext*,bool bindMode=false,const char** failure=nullptr);
bool fallback_pending();
const char* take_fallback_reason();
void stop();
bool available();
bool uses_instancing();
void discard();
bool candidate(unsigned eye);
void miss(unsigned eye,const char* reason);
void native_outer_stopped(unsigned eye,const char* reason);
void atlas_refusal(unsigned eye,unsigned w,unsigned h,unsigned fw,unsigned fh);
struct GeometryScope {
    explicit GeometryScope(bool eligible);
    ~GeometryScope();
    bool active=false;
};
bool prepare(ID3D11Texture2D* finalAtlas,ID3D11Texture2D* hdr,ID3D11Texture2D* resolve,
             ID3D11RenderTargetView* color,ID3D11DepthStencilView* depth,ID3D11Texture2D* focusHdr,
             ID3D11Texture2D* focusResolve,unsigned w,unsigned h,unsigned fw,unsigned fh,const std::array<float,4>&);
void auxiliary(ID3D11Texture2D* depth,ID3D11Texture2D* depthResolve,ID3D11Texture2D* motion,
               ID3D11Texture2D* nativeDepth,ID3D11Texture2D* nativeDepthResolve,ID3D11Texture2D* nativeMotion);
void begin_outer();
ID3D11Texture2D* end_outer(ID3D11Texture2D* finalAtlas);
void mask(bool active);
void nested();
bool begin_focus();
bool skipping();
bool end_focus();
#ifdef LMU_MVR_SHADOW_PROBE
void finish_shadow_probe();
#endif
}
