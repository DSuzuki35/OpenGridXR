#pragma once
#include <d3d12.h>
#include <d3d11on12.h>
#include <memory>
#include <mutex>
#include <wrl/client.h>

namespace og {
struct D3D11On12Binding {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<ID3D11On12Device> on12;
};

// Views cannot cross D3D11 devices, even when both wrap the same D3D12
// queue. Active consumers share one bridge for this session. Disabling the
// last consumer releases it, and an idle session never creates a GPU device.
class SessionD3D11On12 {
    std::mutex mutex_;
    std::weak_ptr<D3D11On12Binding> active_;
    ID3D12Device* source_{};
    ID3D12CommandQueue* queue_{};
public:
    std::shared_ptr<D3D11On12Binding> acquire(ID3D12Device* source, ID3D12CommandQueue* queue) {
        if (!source || !queue) return {};
        std::lock_guard lock(mutex_);
        if (auto binding = active_.lock())
            return source == source_ && queue == queue_ ? binding : nullptr;
        auto binding = std::make_shared<D3D11On12Binding>();
        IUnknown* queues[]{queue};
        if (FAILED(D3D11On12CreateDevice(source, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                    queues, 1, 0, &binding->device, &binding->context, nullptr)) ||
            FAILED(binding->device.As(&binding->on12))) return {};
        source_=source;
        queue_=queue;
        active_=binding;
        return binding;
    }
};
} // namespace og
