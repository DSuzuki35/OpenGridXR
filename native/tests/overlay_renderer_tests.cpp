#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#include "overlay_renderer.hpp"
#include "overlay_transport.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <iostream>
#include <map>
#include <openxr/openxr_platform.h>
#include <stdexcept>
#include <thread>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
ComPtr<ID3D11Device> device11;
ComPtr<ID3D11DeviceContext> context11;
ComPtr<ID3D12Device> device12;
ComPtr<ID3D12CommandQueue> queue12;
struct RuntimeImage {
    ComPtr<ID3D11Texture2D> texture11;
    ComPtr<ID3D12Resource> texture12;
    uint32_t width{}, height{};
};
std::map<XrSwapchain, RuntimeImage> images;
uint64_t nextSwap = 20;
bool d12{}, timeout{};
XrPosef head{{0, 0, 0, 1}, {0, 0, 0}};
XrResult XRAPI_CALL formats(XrSession, uint32_t cap, uint32_t *count, int64_t *out) {
    *count = 1;
    if (cap)
        out[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL create(XrSession, const XrSwapchainCreateInfo *info, XrSwapchain *out) {
    RuntimeImage image;
    image.width = info->width;
    image.height = info->height;
    if (d12) {
        D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = info->width;
        desc.Height = info->height;
        desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (FAILED(device12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                     D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                     IID_PPV_ARGS(&image.texture12))))
            return XR_ERROR_RUNTIME_FAILURE;
    } else {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = info->width;
        desc.Height = info->height;
        desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        desc.Usage = D3D11_USAGE_DEFAULT;
        if (FAILED(device11->CreateTexture2D(&desc, nullptr, &image.texture11)))
            return XR_ERROR_RUNTIME_FAILURE;
    }
    *out = reinterpret_cast<XrSwapchain>(++nextSwap);
    images.emplace(*out, std::move(image));
    return XR_SUCCESS;
}
XrResult XRAPI_CALL enumerate(XrSwapchain swap, uint32_t cap, uint32_t *count,
                              XrSwapchainImageBaseHeader *out) {
    *count = 1;
    if (cap) {
        if (d12)
            reinterpret_cast<XrSwapchainImageD3D12KHR *>(out)->texture =
                images.at(swap).texture12.Get();
        else
            reinterpret_cast<XrSwapchainImageD3D11KHR *>(out)->texture =
                images.at(swap).texture11.Get();
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL acquire(XrSwapchain, const XrSwapchainImageAcquireInfo *, uint32_t *out) {
    *out = 0;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL waitImage(XrSwapchain, const XrSwapchainImageWaitInfo *) {
    return timeout ? XR_TIMEOUT_EXPIRED : XR_SUCCESS;
}
XrResult XRAPI_CALL releaseImage(XrSwapchain, const XrSwapchainImageReleaseInfo *) {
    return XR_SUCCESS;
}
XrResult XRAPI_CALL destroy(XrSwapchain s) {
    images.erase(s);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL createSpace(XrSession, const XrReferenceSpaceCreateInfo *info, XrSpace *out) {
    *out = reinterpret_cast<XrSpace>(
        uintptr_t(info->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_VIEW ? 1 : 2));
    return XR_SUCCESS;
}
XrResult XRAPI_CALL destroySpace(XrSpace) { return XR_SUCCESS; }
XrResult XRAPI_CALL locate(XrSpace, XrSpace, XrTime, XrSpaceLocation *out) {
    out->pose = head;
    out->locationFlags =
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    return XR_SUCCESS;
}
uint32_t pixel(const RuntimeImage &image) {
    if (!d12) {
        D3D11_TEXTURE2D_DESC desc{};
        image.texture11->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> read;
        check(SUCCEEDED(device11->CreateTexture2D(&desc, nullptr, &read)), "readback allocation");
        context11->CopyResource(read.Get(), image.texture11.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(SUCCEEDED(context11->Map(read.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "readback map");
        auto value = *reinterpret_cast<const uint32_t *>(static_cast<const char *>(mapped.pData) +
                                                         (image.height / 2) * mapped.RowPitch +
                                                         (image.width / 2) * 4);
        context11->Unmap(read.Get(), 0);
        return value;
    }
    auto desc = image.texture12->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes{};
    device12->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = bytes;
    bd.Height = bd.DepthOrArraySize = bd.MipLevels = bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> read;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> command;
    ComPtr<ID3D12Fence> fence;
    check(SUCCEEDED(device12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd,
                                                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                      IID_PPV_ARGS(&read))),
          "D3D12 readback");
    device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                IID_PPV_ARGS(&command));
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {image.texture12.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                          D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE};
    command->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION to{}, from{};
    to.pResource = read.Get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint = footprint;
    from.pResource = image.texture12.Get();
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    command->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command->ResourceBarrier(1, &barrier);
    command->Close();
    ID3D12CommandList *list = command.Get();
    queue12->ExecuteCommandLists(1, &list);
    device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    queue12->Signal(fence.Get(), 1);
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    fence->SetEventOnCompletion(1, event);
    check(WaitForSingleObject(event, 3000) == WAIT_OBJECT_0, "D3D12 copy fence");
    CloseHandle(event);
    void *mapped{};
    read->Map(0, nullptr, &mapped);
    uint32_t value = *reinterpret_cast<const uint32_t *>(
        static_cast<const char *>(mapped) + footprint.Offset +
        image.height / 2 * footprint.Footprint.RowPitch + image.width / 2 * 4);
    read->Unmap(0, nullptr);
    return value;
}
int main(int argc, char **argv) {
    try {
        bool listen = false;
        unsigned seconds = 60;
        std::string wanted;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--listen")
                listen = true;
            else if (arg == "--d3d12")
                d12 = true;
            else if (arg == "--adapter" && i + 1 < argc)
                wanted = argv[++i];
            else if (arg == "--seconds" && i + 1 < argc)
                seconds = std::clamp(unsigned(std::stoul(argv[++i])), 1u, 600u);
        }
        ComPtr<IDXGIFactory1> factory;
        CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        ComPtr<IDXGIAdapter1> adapter;
        std::string adapterName;
        for (UINT n = 0;; ++n) {
            ComPtr<IDXGIAdapter1> a;
            if (factory->EnumAdapters1(n, &a) == DXGI_ERROR_NOT_FOUND)
                break;
            DXGI_ADAPTER_DESC1 desc{};
            a->GetDesc1(&desc);
            std::wstring w = desc.Description;
            std::string name;
            for (auto character : w)
                name += character < 128 ? char(character) : '?';
            if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                (wanted.empty() || name.find(wanted) != std::string::npos)) {
                adapter = a;
                adapterName = name;
                break;
            }
        }
        check(bool(adapter), "test adapter available");
        check(
            SUCCEEDED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr,
                                        0, D3D11_SDK_VERSION, &device11, nullptr, &context11)),
            "D3D11 test device");
        XrGraphicsBindingD3D11KHR b11{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
        b11.device = device11.Get();
        XrGraphicsBindingD3D12KHR b12{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};
        if (d12) {
            check(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                              IID_PPV_ARGS(&device12))),
                  "D3D12 test device");
            D3D12_COMMAND_QUEUE_DESC q{};
            q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            check(SUCCEEDED(device12->CreateCommandQueue(&q, IID_PPV_ARGS(&queue12))),
                  "D3D12 test queue");
            b12.device = device12.Get();
            b12.queue = queue12.Get();
        }
        og::OverlayRenderer renderer;
        og::OverlayDispatch api{formats,      create,  enumerate,   acquire,      waitImage,
                                releaseImage, destroy, createSpace, destroySpace, locate};
        check(renderer.initialize(reinterpret_cast<XrSession>(10),
                                  d12 ? static_cast<void *>(&b12) : static_cast<void *>(&b11),
                                  listen ? 32 : 4, api, "test"),
              "renderer initialized");
        og::OverlayChannel channel;
        check(channel.open("test"), "fixture channel");
        XrCompositionLayerProjection app{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        XrCompositionLayerQuad osd{XR_TYPE_COMPOSITION_LAYER_QUAD};
        const XrCompositionLayerBaseHeader *base[]{
            reinterpret_cast<const XrCompositionLayerBaseHeader *>(&app),
            reinterpret_cast<const XrCompositionLayerBaseHeader *>(&osd)};
        XrFrameEndInfo info{XR_TYPE_FRAME_END_INFO};
        info.displayTime = 1;
        info.layerCount = 2;
        info.layers = base;
        std::array<ComPtr<ID3D11Texture2D>, 2> source;
        std::array<ComPtr<IDXGIKeyedMutex>, 2> keys;
        std::array<HANDLE, 2> handles{};
        if (!listen) {
            for (unsigned i = 0; i < 2; ++i) {
                D3D11_TEXTURE2D_DESC desc{};
                desc.Width = 32;
                desc.Height = 16;
                desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
                desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                desc.MiscFlags =
                    D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
                check(SUCCEEDED(device11->CreateTexture2D(&desc, nullptr, &source[i])),
                      "source allocation");
                source[i].As(&keys[i]);
                ComPtr<IDXGIResource1> shared;
                source[i].As(&shared);
                check(SUCCEEDED(shared->CreateSharedHandle(
                          nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                          &handles[i])),
                      "source NT handle");
                check(keys[i]->AcquireSync(0, 0) == S_OK, "producer source ownership");
                std::array<uint32_t, 32 * 16> red;
                red.fill(0xff0000ff);
                context11->UpdateSubresource(source[i].Get(), 0, nullptr, red.data(), 32 * 4, 0);
                context11->Flush();
                keys[i]->ReleaseSync(1);
            }
            check(channel.lock(), "configure fixture");
            auto *d = channel.data();
            d->producerPid = GetCurrentProcessId();
            d->producerBeat = GetTickCount64();
            for (unsigned i = 0; i < 15; ++i) {
                d->slots[i] = {};
                d->slots[i].enabled = 1;
                d->slots[i].opacity = .5f;
                d->slots[i].anchor = i == 1;
                d->slots[i].widthMeters = 1;
                if (i < 3)
                    d->slots[i].frames[0] = {uint64_t(i + 1),
                                             reinterpret_cast<uint64_t>(handles[i % 2]),
                                             1,
                                             32,
                                             16,
                                             DXGI_FORMAT_R8G8B8A8_UNORM};
            }
            channel.unlock();
        }
        std::cout << "{\"op\":\"fixture\",\"ready\":true,\"adapter\":\"" << adapterName
                  << "\",\"d3d12\":" << (d12 ? "true" : "false") << "}" << std::endl;
        unsigned frames = listen ? seconds * 60 : 12;
        for (unsigned f = 0; f < frames; ++f) {
            info.displayTime++;
            if (!listen) {
                timeout = f == 0;
                if (f == 3) {
                    head.position.x = 2;
                    check(channel.lock(), "recenter lock");
                    ++channel.data()->slots[1].recenter;
                    channel.unlock();
                }
                if (f == 4) {
                    check(channel.lock(), "opacity lock");
                    channel.data()->slots[0].opacity = 1;
                    channel.unlock();
                }
                if (f == 8) {
                    check(channel.lock(), "source replacement lock");
                    ++channel.data()->slots[0].sourceRevision;
                    ++channel.data()->slots[0].frames[0].generation;
                    channel.data()->slots[0].enabled = 1;
                    channel.unlock();
                }
                if (f == 9) {
                    check(channel.lock(), "paused source removal lock");
                    channel.data()->slots[1].enabled = 0;
                    channel.unlock();
                }
                if (f == 10) {
                    check(channel.lock(), "stale lock");
                    channel.data()->producerBeat = 0;
                    channel.unlock();
                }
            }
            std::vector<const XrCompositionLayerBaseHeader *> layers{base[0], base[1]};
            const auto normalLayerCount = info.layerCount;
            if (!listen && f == 6)
                info.layerCount = 0;
            renderer.append(info, true, listen || (f != 5 && f != 9), layers);
            info.layerCount = normalLayerCount;
            if (!listen && (f == 5 || f == 6)) {
                check(layers.size() == 2 && images.size() == 2,
                      "temporary headset inactivity retains static GPU panels without submitting "
                      "them");
            }
            if (!listen && ((f >= 2 && f < 5) || f == 7)) {
                check(layers.size() == 4, "runtime layer budget preserves app and OSD");
                auto *q = reinterpret_cast<const XrCompositionLayerQuad *>(layers[2]);
                check(q->size.height == .5f && q->eyeVisibility == XR_EYE_VISIBILITY_BOTH,
                      "aspect-derived stereo quad");
                uint32_t value = pixel(images.at(q->subImage.swapchain));
                if (f < 4)
                    check((value & 255) > 180 && (value & 255) < 196 &&
                              ((value >> 24) & 255) > 120 && ((value >> 24) & 255) < 136,
                          "GPU shader preserves color encoding and applies opacity");
                else
                    check(value == 0xff0000ff,
                          "opacity changes redraw last shared frame without new producer pixels");
                auto *localQuad = reinterpret_cast<const XrCompositionLayerQuad *>(layers[3]);
                check(localQuad->space == reinterpret_cast<XrSpace>(2) &&
                          q->space == reinterpret_cast<XrSpace>(1),
                      "head and local spaces differ");
                check(localQuad->pose.position.x == (f >= 3 ? 2.f : 0.f),
                      "overlay recenter uses current local head pose");
                check(channel.lock(), "capacity status lock");
                check(channel.data()->capacity == 2 && channel.data()->active == 2 &&
                          channel.data()->slots[2].state == og::OverlayState::LayerLimit &&
                          channel.data()->slots[2].pose.valid,
                      "excess of fifteen configured slots reports runtime limit");
                channel.unlock();
            }
            if (!listen && f == 8)
                check(layers.size() == 3, "clear and replacement between frames cannot reveal "
                                          "previous source while new pixels are busy");
            if (!listen && f == 9)
                check(layers.size() == 2 && images.size() == 1,
                      "paused source removal releases that panel while preserving unchanged "
                      "resources");
            if (!listen && f >= 10)
                check(layers.size() == 2 && images.empty(), "stale publisher retires GPU panels");
            if (listen && f % 30 == 0) {
                std::cout << "{\"op\":\"fixture-frame\",\"active\":" << layers.size() - 2;
                if (layers.size() > 2) {
                    auto *q = reinterpret_cast<const XrCompositionLayerQuad *>(layers[2]);
                    std::cout << ",\"pixel\":" << pixel(images.at(q->subImage.swapchain));
                }
                std::cout << "}" << std::endl;
            }
            if (listen)
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        renderer.reset();
        for (auto h : handles)
            if (h)
                CloseHandle(h);
        std::cout << "Overlay GPU copy, quad layer budget and opacity tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
