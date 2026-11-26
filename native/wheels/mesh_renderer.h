#pragma once
#include <d3d11.h>
#include "wheel_resources.hpp"
#include "driver_fit.h"
#include <array>
#include <cstdint>
#include <future>
#include <memory>
#include <string>

namespace ovrlay {
constexpr unsigned kWheelModels=5, kLightCount=88, kModelLightCount=96, kRingCount=17;
struct ModelLightsFrame { uint64_t sampled_tick_ms{}; uint32_t count{},ring_rotation{}; uint8_t rgba[96][4]{}; };
template<class T> struct Com {
    T* value{};
    Com()=default; ~Com(){reset();}
    Com(const Com&)=delete; Com& operator=(const Com&)=delete;
    void reset(){if(value)value->Release();value=nullptr;}
    T** put(){reset();return &value;}
    T* get() const{return value;}
    T* operator->() const{return value;}
    explicit operator bool() const{return value!=nullptr;}
};
struct MeshVertex { float position[3], normal[3], uv[2], lightGroup; };
static_assert(sizeof(MeshVertex)==36);
struct MeshEyeParameters {
    float modelToEye[12]{};
    float eyeLocal[4]{}; // xyz metres in shaft coordinates, w scene lighting strength (0..1)
    float aperture[4]{}; // centre x,y and full width,height at local z=0
    float fov[4]{}, rectangle[4]{}, depth[4]{};
    float model[4]{}; // model width metres, alpha, depth bias, depth array slice
    float clip[4]{}; // near, far, game depth enabled, reserved
    float effects[4]{1,0,0,1}; // Ring gain, glow strength, radius px, dot gain.
    float body[4]{1,1,1,1}; // brightness, contrast, saturation, gamma.
    float target[4]{}; // full width/height, quarter width/height.
    float probeUv[4]{}; // scale.xy and offset.zw from model XY to probe UV.
};
static_assert(sizeof(MeshEyeParameters)==224);
struct MeshLightsParameters {
    float colors[kModelLightCount][4]{};
    float centers[kRingCount][4]{};
    float options[4]{}; // enabled, quarter-turn offset, physical dot start, use control mean.
};
static_assert(sizeof(MeshLightsParameters)==1824);
struct MeshTargets {
    Com<ID3D11Texture2D> depth,color,emission,blur[2],sampleColor,sampleEmission,sampleOcclusion;
    Com<ID3D11DepthStencilView> depthView;
    Com<ID3D11ShaderResourceView> colorSource,emissionSource,blurSource[2];
    Com<ID3D11RenderTargetView> colorTarget,emissionTarget,blurTarget[2];
    Com<ID3D11RenderTargetView> sampleColorTarget,sampleEmissionTarget,sampleOcclusionTarget;
    Com<ID3D11Texture2D> occlusion[2];
    Com<ID3D11ShaderResourceView> occlusionSource[2];
    Com<ID3D11RenderTargetView> occlusionTarget[2];
    uint32_t width{},height{},samples{};
    bool prepare(ID3D11Device*,uint32_t,uint32_t,bool bloom,bool masks,uint32_t sampleCount) noexcept;
};

struct MeshAsset {
    Com<ID3D11Buffer> vertices,indices;
    Com<ID3D11Texture2D> atlas;
    Com<ID3D11ShaderResourceView> atlasView;
    uint32_t indexCount{},bodyIndexCount{};
    bool taggedLights{};
    float minimum[3]{},maximum[3]{},centers[kRingCount][4]{},probeUv[4]{};
    bool initialize(ID3D11Device*,const wchar_t*,uint32_t) noexcept;
};

// Shaders and per-slot targets are shared. Only requested immutable models get
// GPU resources; one background load may be pending, never waited on a frame.
struct DriverAsset;
class MeshRenderer {
    Com<ID3D11DeviceContext> deferred;
    Com<ID3D11VertexShader> vertex,postVertex;
    Com<ID3D11PixelShader> pixel,emissionPixel,downsamplePixel,horizontalPixel,verticalPixel,compositePixel,copyPixel;
    Com<ID3D11PixelShader> bodyPixel,bodyEmissionPixel;
    Com<ID3D11InputLayout> layout;
    Com<ID3D11Buffer> constants,lightConstants;
    Com<ID3D11SamplerState> sampler;
    Com<ID3D11RasterizerState> rasterizer;
    Com<ID3D11DepthStencilState> depthState,noDepthState;
    uint32_t samples{1};
    MeshTargets targets;
    std::unique_ptr<MeshTargets> driverTargets;
    bool coloredLights{},pipelineReady{},lightsNeedUpload{};
    MeshLightsParameters lights{};
    WheelMeshCache<MeshAsset> models;
    MeshAsset* active{};
    WheelMeshCache<DriverAsset> drivers;
    DriverAsset* driver{};
    bool fitLimitedState{};
public:
    bool initializePipeline(ID3D11Device*) noexcept;
    bool selectModel(ID3D11Device*,unsigned slot,const std::wstring& path,unsigned kind,unsigned deviceKind) noexcept;
    bool selectDriver(ID3D11Device*,unsigned slot,const std::wstring& path,float radians,
                      const float (&neutralToWheel)[12],unsigned glove,unsigned suit,bool torso,bool legs,
                      const DriverFit& fit={},float wheelWidth=.3f,const DriverMount& mountSettings={}) noexcept;
    bool driverFitLimited() const noexcept;
    void forgetDriver(unsigned slot) {
        drivers.forget(slot);driver=nullptr;fitLimitedState=false;drivers.collect();
        if(!drivers.hasAssets())driverTargets.reset();
    }
    bool driverFailed(unsigned slot) const {return drivers.failed(slot);}
    bool failed(unsigned slot) const {return models.failed(slot);}
    void forgetModel(unsigned slot) {models.forget(slot);forgetDriver(slot);active=nullptr;}
    bool ready() const noexcept { return pipelineReady && active && active->indexCount && active->atlasView; }
    bool idle() const {
        return models.idle() && drivers.idle();
    }
    // Called per selected model/stereo pair. Both eyes use the same snapshot,
    // including when IPC is contended and the previous sample has expired.
    void setLights(bool enabled,const ModelLightsFrame&,uint64_t now) noexcept;
    // Derive the exact aperture for this eye from all eight model box corners.
    // Eye position, size and model transform must already be populated.
    bool aperture(MeshEyeParameters&,const float* visiblePlane=nullptr) const noexcept;
    bool render(ID3D11Device*,ID3D11DeviceContext*,ID3D11Texture2D*,DXGI_FORMAT,
                ID3D11ShaderResourceView* depth,const MeshEyeParameters&,ID3D11RenderTargetView*,
                ID3D11ShaderResourceView* screen,uint32_t eye,
                ID3D11ShaderResourceView* sceneProbe=nullptr,float ambientLight=0) noexcept;
};
}
