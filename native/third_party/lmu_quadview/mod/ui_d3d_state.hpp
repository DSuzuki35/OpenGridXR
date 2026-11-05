#pragma once
#include <d3d11.h>
#include <array>

namespace lmuqv {
// Native UI eye draw reads dimensions at object+0xbc/+0xc0 independently of its RTV.
// Override both together, then return the original borrowed pointers and integer sizes.
class UiTargetScope {
    ID3D11RenderTargetView*& slot_;
    ID3D11RenderTargetView* saved_;
    UINT* dimensions_;
    std::array<UINT,2> savedDimensions_;
public:
    UiTargetScope(ID3D11RenderTargetView*& slot,UINT* dimensions,ID3D11RenderTargetView* target,UINT width,UINT height)
        :slot_(slot),saved_(slot),dimensions_(dimensions),savedDimensions_{dimensions[0],dimensions[1]} {
        slot_=target;dimensions_[0]=width;dimensions_[1]=height;
    }
    UiTargetScope(const UiTargetScope&)=delete;
    UiTargetScope& operator=(const UiTargetScope&)=delete;
    ~UiTargetScope() {slot_=saved_;dimensions_[0]=savedDimensions_[0];dimensions_[1]=savedDimensions_[1];}
};
// State changed by LMU 617fb0, captured after the native UI wrapper has finished.
class UiD3dState {
    ID3D11DeviceContext* context;
    std::array<ID3D11RenderTargetView*,8> targets{};
    ID3D11DepthStencilView* depth{};
    ID3D11VertexShader* vs{};
    ID3D11PixelShader* ps{};
    std::array<ID3D11ClassInstance*,256> vsClasses{},psClasses{};
    UINT vsCount=256,psCount=256;
    ID3D11Buffer *vertex{},*constant{};
    UINT stride{},offset{};
    ID3D11InputLayout* layout{};
    ID3D11ShaderResourceView* resource{};
    ID3D11BlendState* blend{};
    FLOAT blendFactor[4]{};
    UINT sampleMask{};
    ID3D11RasterizerState* raster{};
    std::array<D3D11_VIEWPORT,16> viewports{};
    std::array<D3D11_RECT,16> scissors{};
    UINT viewportCount=16,scissorCount=16;
    D3D11_PRIMITIVE_TOPOLOGY topology{};
    template<class T> static void release(T* pointer) {if(pointer)pointer->Release();}
public:
    explicit UiD3dState(ID3D11DeviceContext* ctx):context(ctx) {
        context->OMGetRenderTargets(UINT(targets.size()),targets.data(),&depth);
        context->VSGetShader(&vs,vsClasses.data(),&vsCount);
        context->PSGetShader(&ps,psClasses.data(),&psCount);
        context->IAGetVertexBuffers(0,1,&vertex,&stride,&offset);
        context->IAGetInputLayout(&layout);
        context->IAGetPrimitiveTopology(&topology);
        context->VSGetConstantBuffers(0,1,&constant);
        context->PSGetShaderResources(0,1,&resource);
        context->OMGetBlendState(&blend,blendFactor,&sampleMask);
        context->RSGetState(&raster);
        context->RSGetViewports(&viewportCount,viewports.data());
        context->RSGetScissorRects(&scissorCount,scissors.data());
    }
    UiD3dState(const UiD3dState&)=delete;
    UiD3dState& operator=(const UiD3dState&)=delete;
    ~UiD3dState() {
        context->OMSetRenderTargets(UINT(targets.size()),targets.data(),depth);
        context->VSSetShader(vs,vsClasses.data(),vsCount);
        context->PSSetShader(ps,psClasses.data(),psCount);
        context->IASetVertexBuffers(0,1,&vertex,&stride,&offset);
        context->IASetInputLayout(layout);
        context->IASetPrimitiveTopology(topology);
        context->VSSetConstantBuffers(0,1,&constant);
        context->PSSetShaderResources(0,1,&resource);
        context->OMSetBlendState(blend,blendFactor,sampleMask);
        context->RSSetState(raster);
        context->RSSetViewports(viewportCount,viewports.data());
        context->RSSetScissorRects(scissorCount,scissors.data());
        for(auto* p:targets)release(p);
        for(UINT i=0;i<vsCount;++i)release(vsClasses[i]);
        for(UINT i=0;i<psCount;++i)release(psClasses[i]);
        release(depth);release(vs);release(ps);release(vertex);release(constant);
        release(layout);release(resource);release(blend);release(raster);
    }
};
}
