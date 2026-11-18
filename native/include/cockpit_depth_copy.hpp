#pragma once

#include "cockpit_depth.hpp"
#include <d3d11_4.h>
#include <wrl/client.h>
#include <cmath>
#include <utility>

namespace og {
// Owned by a swapchain until the next release. A submitting frame pins its copy
// across the host's unlocked pacing wait, so a later release cannot overwrite it.
class CockpitDepthCopy {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
    Microsoft::WRL::ComPtr<ID3D11Multithread> multithread_;
    ID3D11DeviceContext* context_{};
    D3D11_TEXTURE2D_DESC description_{};

  public:
    ID3D11ShaderResourceView* view() const { return view_.Get(); }

    bool capture(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *source) {
        if (!device || !context || !source || device->GetFeatureLevel() < D3D_FEATURE_LEVEL_10_1) return false;
        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        if (desc.SampleDesc.Count != 1 || desc.SampleDesc.Quality || desc.MipLevels != 1 ||
            !(desc.BindFlags & D3D11_BIND_DEPTH_STENCIL)) return false;
        DXGI_FORMAT sampled{};
        switch (desc.Format) {
        case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_TYPELESS:
            desc.Format = DXGI_FORMAT_R16_TYPELESS; sampled = DXGI_FORMAT_R16_UNORM; break;
        case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24G8_TYPELESS:
            desc.Format = DXGI_FORMAT_R24G8_TYPELESS; sampled = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; break;
        case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_R32_TYPELESS:
            desc.Format = DXGI_FORMAT_R32_TYPELESS; sampled = DXGI_FORMAT_R32_FLOAT; break;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: case DXGI_FORMAT_R32G8X24_TYPELESS:
            desc.Format = DXGI_FORMAT_R32G8X24_TYPELESS; sampled = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; break;
        default: return false;
        }
        if (context_ != context) {
            context_ = nullptr;
            multithread_.Reset();
            if (FAILED(context->QueryInterface(IID_PPV_ARGS(&multithread_)))) return false;
            context_ = context;
            view_.Reset();
        }
        if (!view_ || description_.Format != desc.Format || description_.Width != desc.Width ||
            description_.Height != desc.Height || description_.ArraySize != desc.ArraySize) {
            Microsoft::WRL::ComPtr<ID3D11Device> owner;
            source->GetDevice(&owner);
            if (owner.Get() != device) return false;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags = desc.CPUAccessFlags = 0;
            desc.Usage = D3D11_USAGE_DEFAULT;
            D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = sampled;
            srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            srv.Texture2DArray.ArraySize = desc.ArraySize;
            srv.Texture2DArray.MipLevels = 1;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
            if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture)) ||
                FAILED(device->CreateShaderResourceView(texture.Get(), &srv, &view))) return false;
            texture_ = std::move(texture);
            view_ = std::move(view);
            description_ = desc;
        }
        multithread_->Enter();
        Microsoft::WRL::ComPtr<ID3D11Predicate> predicate;
        BOOL value{};
        context->GetPredication(&predicate, &value);
        if (predicate) context->SetPredication(nullptr, FALSE);
        context->CopyResource(texture_.Get(), source);
        if (predicate) context->SetPredication(predicate.Get(), value);
        multithread_->Leave();
        return SUCCEEDED(device->GetDeviceRemovedReason());
    }

    bool eye(const XrCompositionLayerProjectionView &projection, const XrCompositionLayerDepthInfoKHR &depth,
             CockpitDepthEye &out) const {
        const auto &rect = depth.subImage.imageRect;
        if (!view_ || depth.subImage.imageArrayIndex >= description_.ArraySize ||
            rect.offset.x < 0 || rect.offset.y < 0 || rect.extent.width <= 0 || rect.extent.height <= 0 ||
            int64_t(rect.offset.x) + rect.extent.width > description_.Width ||
            int64_t(rect.offset.y) + rect.extent.height > description_.Height ||
            !(depth.minDepth >= 0 && depth.minDepth < depth.maxDepth && depth.maxDepth <= 1) ||
            !(depth.nearZ > 0 && depth.farZ > 0) || depth.nearZ == depth.farZ) return false;
        const float invNear = 1.f / depth.nearZ, invFar = 1.f / depth.farZ;
        const auto &p = projection.pose.position;
        const auto &q = projection.pose.orientation;
        const float norm = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
        const auto &f = projection.fov;
        constexpr float halfPi = 1.57079632679f;
        if (!std::isfinite(invNear) || !std::isfinite(invFar) || !std::isfinite(p.x) ||
            !std::isfinite(p.y) || !std::isfinite(p.z) || !(std::abs(norm - 1.f) < 0.01f) ||
            !(f.angleLeft > -halfPi && f.angleLeft < f.angleRight && f.angleRight < halfPi &&
              f.angleDown > -halfPi && f.angleDown < f.angleUp && f.angleUp < halfPi)) return false;
        out = {view_.Get(), projection.pose, f, rect, depth.subImage.imageArrayIndex,
               depth.minDepth, depth.maxDepth, invNear, invFar};
        return true;
    }
};
}
