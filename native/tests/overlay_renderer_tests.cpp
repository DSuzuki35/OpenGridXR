#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#include "desktop_interaction.hpp"
#include "overlay_renderer.hpp"
#include "overlay_transport.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <future>
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
bool inputFocused = true;
og::OverlayChannel *inspectChannel{};
bool inspectPresentedHit{};
unsigned presentedHitInspections{};
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
unsigned spaceCreations{}, spaceDestructions{};
XrResult XRAPI_CALL createSpace(XrSession, const XrReferenceSpaceCreateInfo *info, XrSpace *out) {
    ++spaceCreations;
    *out = reinterpret_cast<XrSpace>(
        uintptr_t(info->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_VIEW ? 1 : 2));
    return XR_SUCCESS;
}
XrResult XRAPI_CALL destroySpace(XrSpace) {
    ++spaceDestructions;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL locate(XrSpace, XrSpace, XrTime, XrSpaceLocation *out) {
    if (inspectPresentedHit && inspectChannel) {
        check(inspectChannel->lock(), "midframe presented hit lock");
        const bool fresh = og::desktop_target_fresh(*inspectChannel->data(), inspectChannel->data()->slots[0], GetTickCount64());
        inspectChannel->unlock();
        check(fresh, "preparing next frame preserves last presented precision hit");
        ++presentedHitInspections;
    }
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
        api.focused = [](void *) { return inputFocused; };
        check(renderer.initialize(reinterpret_cast<XrSession>(10),
                                  d12 ? static_cast<void *>(&b12) : static_cast<void *>(&b11),
                                  listen ? 32 : 4, api, "test"),
              "renderer initialized");
        renderer.setInputFocused(true);
        og::OverlayChannel channel;
        check(channel.open("test"), "fixture channel");
        inspectChannel = &channel;
        XrCompositionLayerProjection app{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        XrCompositionLayerQuad osd{XR_TYPE_COMPOSITION_LAYER_QUAD};
        const XrCompositionLayerBaseHeader *base[]{
            reinterpret_cast<const XrCompositionLayerBaseHeader *>(&app),
            reinterpret_cast<const XrCompositionLayerBaseHeader *>(&osd)};
        XrFrameEndInfo info{XR_TYPE_FRAME_END_INFO};
        info.displayTime = 1;
        info.layerCount = 2;
        info.layers = base;
        if (!listen) {
            for (unsigned frame = 0; frame < 90; ++frame) {
                std::vector<const XrCompositionLayerBaseHeader *> layers{base[0], base[1]};
                renderer.append(info, false, true, layers);
                check(layers.size() == 2, "disabled overlays preserve submitted layers");
            }
            check(spaceCreations == 0 && images.empty(),
                  "disabled spatial overlays create no reference spaces or swapchains");
        }
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
                d->slots[i].desktopMode = 1;
                d->slots[i].desktopWidth = 1920;
                d->slots[i].desktopHeight = 1080;
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
                    channel.data()->slots[1].y = 1;
                    channel.unlock();
                }
                if (f == 4) {
                    check(channel.lock(), "opacity lock");
                    channel.data()->slots[0].opacity = 1;
                    channel.data()->slots[0].desktopMode = 2;
                    channel.unlock();
                }
                if (f == 8) {
                    check(channel.lock(), "source replacement lock");
                    ++channel.data()->slots[0].sourceRevision;
                    ++channel.data()->slots[0].frames[0].generation;
                    channel.data()->slots[0].enabled = 1;
                    channel.unlock();
                }
                if (f == 7) {
                    check(channel.lock(), "restore desktop for precision coverage");
                    channel.data()->slots[0].desktopMode = 1;
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
                check(spaceCreations == 2,
                      "hot enable initializes spatial graphics and spaces once");
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
                if (f == 2)
                    check(!channel.data()->slots[0].pointer.valid,
                          "later composed panel occludes lower desktop ray");
                else
                    check(channel.data()->slots[0].pointer.valid &&
                              channel.data()->slots[0].pointer.source == 1 &&
                              std::abs(channel.data()->slots[0].pointer.u - .5f) < .001f,
                          "fresh head hit matches rendered quad");
                check(!channel.data()->slots[2].pointer.valid,
                      "layer-limited unrendered panel cannot point");
                channel.unlock();
            }
            if (!listen && (f == 5 || f == 6 || f == 9 || f >= 10)) {
                check(channel.lock(), "inactive pointer lock");
                check(!channel.data()->slots[0].pointer.valid,
                      "suspended or stale render invalidates pointer immediately");
                channel.unlock();
            }
            if (!listen && f == 7) {
                auto *normal = reinterpret_cast<const XrCompositionLayerQuad *>(layers[2]);
                const auto normalPose = normal->pose;
                const auto normalSize = normal->size;
                check(channel.lock(), "precision transition lock");
                auto &zoom = channel.data()->slots[0];
                zoom.precisionScale = 2; zoom.precisionU = .6f; zoom.precisionV = .3f;
                zoom.precisionActive = 1; ++zoom.revision;
                channel.unlock();
                std::vector<const XrCompositionLayerBaseHeader *> enlarged{base[0], base[1]};
                renderer.append(info, true, true, enlarged);
                const auto *magnified = reinterpret_cast<const XrCompositionLayerQuad *>(enlarged[2]);
                check(std::abs(magnified->size.width - normalSize.width * 2) < .0001f &&
                      std::abs(magnified->pose.position.x + .1f * magnified->size.width - normalPose.position.x - .1f * normalSize.width) < .0001f &&
                      std::abs(magnified->pose.position.y + .2f * magnified->size.height - normalPose.position.y - .2f * normalSize.height) < .0001f,
                      "rendered precision quad grows around fixed prepress world point");
                check(channel.lock(), "matching streaming precision pose lock");
                check(channel.data()->slots[0].pose.valid &&
                      channel.data()->slots[0].pose.width == magnified->size.width &&
                      channel.data()->slots[0].pose.height == magnified->size.height &&
                      channel.data()->slots[0].pose.position[0] == magnified->pose.position.x &&
                      std::abs(channel.data()->slots[0].pointer.u - .55f) < .001f,
                      "stream pose and head hit use exact enlarged rendering geometry");
                channel.unlock();
                inspectPresentedHit = true;
                std::vector<const XrCompositionLayerBaseHeader *> continuous{base[0], base[1]};
                renderer.append(info, true, true, continuous);
                inspectPresentedHit = false;
                check(presentedHitInspections > 0, "precision hold checked between frames");
                renderer.setInputFocused(false);
                check(channel.lock(), "focus cancels precision lock");
                check(!channel.data()->slots[0].precisionActive, "focus loss restores precision immediately");
                channel.unlock();
                renderer.setInputFocused(true);
                std::vector<const XrCompositionLayerBaseHeader *> restored{base[0], base[1]};
                renderer.append(info, true, true, restored);
                const auto *restoredQuad = reinterpret_cast<const XrCompositionLayerQuad *>(restored[2]);
                check(restoredQuad->size.width == normalSize.width && restoredQuad->pose.position.x == normalPose.position.x,
                      "cancelled precision restores original panel geometry");
                check(channel.lock(), "capture displayed pointer before focus round trip");
                auto beforeFocus = channel.data()->slots[0].pointer;
                channel.unlock();
                std::promise<void> focusReturned;
                auto ready = focusReturned.get_future();
                std::thread delayedPublisher([&] {
                    ready.wait();
                    check(channel.lock(1000), "late pointer publication lock");
                    channel.data()->slots[0].pointer = beforeFocus;
                    channel.unlock();
                });
                renderer.setInputFocused(false);
                renderer.setInputFocused(true);
                focusReturned.set_value();
                delayedPublisher.join();
                check(channel.lock(), "late pre-loss pointer freshness lock");
                check(!og::desktop_target_fresh(*channel.data(), channel.data()->slots[0],
                                                GetTickCount64()),
                      "late pre-loss publication cannot reactivate after focus return");
                channel.unlock();
                renderer.setInputFocused(false);
                inputFocused = false;
                check(channel.lock(), "focus loss sample lock");
                check(!og::desktop_target_fresh(*channel.data(), channel.data()->slots[0],
                                                GetTickCount64()),
                      "focus loss invalidates last pointer before another frame");
                channel.data()->slots[0].desktopMode = 1;
                channel.unlock();
                for (int mode : {1, 2}) {
                    check(channel.lock(), "unfocused interaction mode lock");
                    channel.data()->slots[0].desktopMode = mode;
                    channel.unlock();
                    std::vector<const XrCompositionLayerBaseHeader *> visible{base[0], base[1]};
                    renderer.append(info, true, true, visible);
                    check(visible.size() == 4, "unfocused overlays continue rendering");
                    check(channel.lock(), "unfocused pointer lock");
                    check(!channel.data()->slots[0].pointer.valid,
                          "visible unfocused desktop and internal UI cannot point");
                    channel.unlock();
                }
                check(channel.lock(), "restore head mode lock");
                channel.data()->slots[0].desktopMode = 1;
                channel.unlock();
                inputFocused = true;
                renderer.setInputFocused(true);
                std::vector<const XrCompositionLayerBaseHeader *> resumed{base[0], base[1]};
                renderer.append(info, true, true, resumed);
                check(channel.lock(), "focus return pointer lock");
                check(channel.data()->slots[0].pointer.valid,
                      "focus return publishes a new head hit");
                channel.data()->slots[0].desktopMode = 2;
                channel.unlock();
                std::vector<const XrCompositionLayerBaseHeader *> menu{base[0], base[1]};
                renderer.append(info, true, true, menu);
                check(channel.lock(), "internal menu rendered hit lock");
                check(og::pointer_target_fresh(*channel.data(), channel.data()->slots[0], GetTickCount64()) &&
                      !og::desktop_target_fresh(*channel.data(), channel.data()->slots[0], GetTickCount64()),
                      "rendered internal UI supports fresh head target without OS desktop permission");
                // Return the second, later-composed quad over the same head ray.
                channel.data()->slots[1].y = 0;
                ++channel.data()->slots[1].revision;
                channel.unlock();
                std::vector<const XrCompositionLayerBaseHeader *> coveredMenu{base[0], base[1]};
                renderer.append(info, true, true, coveredMenu);
                check(channel.lock(), "internal menu occlusion lock");
                check(!channel.data()->slots[0].pointer.valid, "later rendered quad occludes internal menu head target");
                channel.data()->slots[1].y = 1;
                ++channel.data()->slots[1].revision;
                channel.unlock();
            }
            if (!listen && f == 8)
                check(layers.size() == 3, "clear and replacement between frames cannot reveal "
                                          "previous source while new pixels are busy");
            if (!listen && f == 9)
                check(layers.size() == 2 && images.size() == 1,
                      "paused source removal releases that panel while preserving unchanged "
                      "resources");
            if (!listen && f >= 10) {
                check(layers.size() == 2 && images.empty(), "stale publisher retires GPU panels");
                check(spaceDestructions == 2,
                      "inactive spatial overlays release their reference spaces");
            }
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
