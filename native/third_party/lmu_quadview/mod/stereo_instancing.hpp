#pragma once
#include <d3d11.h>
#include <cstdint>
#include <limits>

namespace lmuqv {
enum class StereoRoute { unavailable, vertex_array_index, geometry_array_index };
struct StereoCapabilities {
    D3D_FEATURE_LEVEL featureLevel{};
    HRESULT optionsResult=E_FAIL;
    bool vertexArrayIndex=false;
    StereoRoute route=StereoRoute::unavailable;
};
// Device capabilities, never GPU vendor identity, select the portable route.
inline StereoCapabilities query_stereo_capabilities(ID3D11Device* device){
    StereoCapabilities result;
    if(!device)return result;
    result.featureLevel=device->GetFeatureLevel();
    D3D11_FEATURE_DATA_D3D11_OPTIONS3 options{};
    result.optionsResult=device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS3,&options,sizeof(options));
    result.vertexArrayIndex=SUCCEEDED(result.optionsResult)&&options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer;
    if(result.featureLevel>=D3D_FEATURE_LEVEL_11_0)
        result.route=result.vertexArrayIndex?StereoRoute::vertex_array_index:StereoRoute::geometry_array_index;
    return result;
}
inline const char* stereo_route_name(StereoRoute route){
    return route==StereoRoute::vertex_array_index?"instanced_vprt":
        route==StereoRoute::geometry_array_index?"instanced_geometry":"legacy";
}
struct StereoInstances {UINT count=0,start=0;};
// The shader must decode eye/original-instance separately. IA per-instance input
// step rates must also be adapted; this is not a drop-in change to arbitrary draws.
inline bool stereo_instances(UINT count,UINT start,StereoInstances& result){
    result={};
    const uint64_t limit=uint64_t((std::numeric_limits<UINT>::max)())+1;
    if(uint64_t(count)*2>=limit)return false;
    // D3D11's StartInstanceLocation offsets IA instance data, independently of
    // the zero-based shader instance counter. Preserve it; scale the step rate.
    result={count*2,start};return true;
}
inline bool stereo_step_rate(UINT original,UINT& result){
    if(original>(std::numeric_limits<UINT>::max)()/2){result=0;return false;}
    result=original*2;return true;
}
// Caller owns the stereo shader variants, eye constants, array targets and IA
// step-rate adaptation. Never apply this to an unconverted native game pipeline.
inline bool submit_stereo_indexed(ID3D11DeviceContext* context,UINT indexCount,UINT instanceCount,
                                 UINT startIndex,INT baseVertex,UINT startInstance){
    StereoInstances instances;
    if(!context||!stereo_instances(instanceCount,startInstance,instances))return false;
    context->DrawIndexedInstanced(indexCount,instances.count,startIndex,baseVertex,instances.start);
    return true;
}
}
