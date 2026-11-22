#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#include "cockpit_depth.hpp"
#include "dashboard_placement.hpp"
#include "desktop_interaction.hpp"
#include "overlay_renderer.hpp"
#include "overlay_transport.hpp"
#include "session_d3d11on12.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <future>
#include <filesystem>
#include <fstream>
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
uint32_t pixel(const RuntimeImage &image, int sampleX = -1) {
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
        auto value = *reinterpret_cast<const uint32_t *>(
            static_cast<const char *>(mapped.pData) + (image.height / 2) * mapped.RowPitch +
            (sampleX < 0 ? image.width / 2 : sampleX) * 4);
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
        image.height / 2 * footprint.Footprint.RowPitch + (sampleX < 0 ? image.width / 2 : sampleX) * 4);
    read->Unmap(0, nullptr);
    return value;
}
// Optional visual evidence from the production renderer in the synthetic runtime.
// Never used by normal tests or the application; no desktop/window capture.
void captureWheel(const RuntimeImage& image, const std::filesystem::path& path) {
    check(!d12 && image.texture11, "wheel capture requires D3D11");
    D3D11_TEXTURE2D_DESC desc{};
    image.texture11->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> read;
    check(SUCCEEDED(device11->CreateTexture2D(&desc, nullptr, &read)), "wheel capture allocation");
    context11->CopyResource(read.Get(), image.texture11.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(SUCCEEDED(context11->Map(read.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "wheel capture map");
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (unsigned y = 0; y < image.height; ++y) {
        const auto* row = static_cast<const char*>(mapped.pData) + y * mapped.RowPitch;
        for (unsigned x = 0; x < image.width; ++x) file.write(row + x * 4, 3);
    }
    context11->Unmap(read.Get(), 0);
    check(file.good(), "wheel capture write");
}
int main(int argc, char **argv) {
    try {
        bool listen = false;
        unsigned seconds = 60;
        std::string wanted;
        std::filesystem::path wheelCaptureDirectory;
        unsigned arturaCaptureFrames = 0;
        bool arturaCaptured = false;
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
            else if (arg == "--capture-wheel-dir" && i + 1 < argc)
                wheelCaptureDirectory = argv[++i];
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
        og::OverlayChannel channel;
        check(channel.open("test"), "fixture channel");
        auto sharedBridge=std::make_shared<og::SessionD3D11On12>();
        uint32_t layerLimit=listen ? 32 : 4;
        auto initialize = [&]{return renderer.initialize(reinterpret_cast<XrSession>(10),
            d12 ? static_cast<void *>(&b12) : static_cast<void *>(&b11),layerLimit,api,"test",sharedBridge);};
        if (!listen) {
            check(channel.lock(), "hold overlay startup handshake");
            auto starting=std::async(std::launch::async,initialize);
            check(starting.get(), "contended renderer retains its session binding");
            channel.unlock();
        } else check(initialize(),"renderer initialized");
        renderer.setInputFocused(true);
        inspectChannel = &channel;
        XrCompositionLayerProjection app{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        XrCompositionLayerProjectionView views[2]{};
        views[0].pose={{0,0,0,1},{-.032f,0,0}};views[1].pose={{0,0,0,1},{.032f,0,0}};
        app.views=views;app.viewCount=2;app.space=reinterpret_cast<XrSpace>(1);
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
            check(channel.data()->consumerPid!=GetCurrentProcessId(), "disabled renderer leaves pending handshake idle");
            Sleep(275);
            std::vector<const XrCompositionLayerBaseHeader *> layers{base[0],base[1]};
            renderer.append(info,true,true,layers);
            check(channel.data()->consumerPid==GetCurrentProcessId() && channel.data()->inputFocused,
                  "enabled frame recovers the channel and the current input focus after startup contention");
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
            if (listen && !wheelCaptureDirectory.empty() && !d12 && channel.lock()) {
                const auto& selected = channel.data()->slots[0];
                const bool artura = std::string_view(selected.wheelModel.data()) == "ascher-artura";
                channel.unlock();
                arturaCaptureFrames = artura ? arturaCaptureFrames + 1 : 0;
                if (!artura) arturaCaptured = false;
                if (!arturaCaptured && arturaCaptureFrames >= 45 && layers.size() > 2 && layers[2]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
                    std::filesystem::create_directories(wheelCaptureDirectory);
                    const auto* wheel = reinterpret_cast<const XrCompositionLayerProjection*>(layers[2]);
                    for (unsigned eye = 0; eye < wheel->viewCount; ++eye) {
                        captureWheel(images.at(wheel->views[eye].subImage.swapchain),
                            wheelCaptureDirectory / (eye ? "ascher-artura-right.ppm" : "ascher-artura-left.ppm"));
                        const auto& fov = wheel->views[eye].fov;
                        std::ofstream metadata(wheelCaptureDirectory / (eye ? "ascher-artura-right.json" : "ascher-artura-left.json"));
                        metadata << "{\"projection_aspect\":" <<
                            (std::tan(fov.angleRight)-std::tan(fov.angleLeft)) /
                            (std::tan(fov.angleUp)-std::tan(fov.angleDown)) << "}\n";
                    }
                    arturaCaptured = true;
                }
            }
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
                    auto image = layers[2]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION
                        ? reinterpret_cast<const XrCompositionLayerProjection *>(layers[2])->views[0].subImage
                        : reinterpret_cast<const XrCompositionLayerQuad *>(layers[2])->subImage;
                    std::cout << ",\"pixel\":" << pixel(images.at(image.swapchain));
                }
                std::cout << "}" << std::endl;
            }
            if (listen)
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        if (!listen && !d12) {
            check(!renderer.wantsCockpitDepth(), "inactive dashboard has no capture demand");
            if (keys[0]->AcquireSync(0, 0) != S_OK)
                check(keys[0]->AcquireSync(1, 0) == S_OK, "fixture reacquires web frame");
            keys[0]->ReleaseSync(1);
            check(channel.lock(), "configure depth dashboard");
            for (auto &s : channel.data()->slots)
                s = {};
            auto &dash = channel.data()->slots[0];
            dash.enabled = dash.webDashboard = dash.cockpitOcclusion = 1;
            dash.frames[0] = {
                72, reinterpret_cast<uint64_t>(handles[0]), 83, 32, 16, DXGI_FORMAT_R8G8B8A8_UNORM};
            channel.data()->producerBeat = GetTickCount64();
            channel.unlock();
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = desc.Height = desc.MipLevels = desc.SampleDesc.Count = 1;
            desc.ArraySize = 2;
            desc.Format = DXGI_FORMAT_R32_FLOAT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            const float samples[2]{0, 1};
            D3D11_SUBRESOURCE_DATA data[2]{{samples, 4, 4}, {samples + 1, 4, 4}};
            ComPtr<ID3D11Texture2D> texture;
            ComPtr<ID3D11ShaderResourceView> depthView;
            check(SUCCEEDED(device11->CreateTexture2D(&desc, data, &texture)) &&
                      SUCCEEDED(
                          device11->CreateShaderResourceView(texture.Get(), nullptr, &depthView)),
                  "two-eye depth fixture");
            og::CockpitDepthFrame depth{};
            depth.space = reinterpret_cast<XrSpace>(1);
            for (unsigned i = 0; i < 2; ++i) {
                auto &eye = depth.eyes[i];
                eye.view = depthView.Get();
                eye.pose.orientation.w = 1;
                eye.fov = {-.7f, .7f, .7f, -.7f};
                eye.rect.extent = {1, 1};
                eye.slice = i;
                eye.invNear = 2;
                eye.invFar = .1f;
            }
            auto render = [&](bool current) {
                ++info.displayTime;
                depth.time = current ? info.displayTime : info.displayTime - 1;
                std::vector<const XrCompositionLayerBaseHeader *> layers{base[0], base[1]};
                renderer.append(info, true, true, layers, &depth);
                return layers;
            };
            auto masked = render(true);
            check(masked.size() == 4 && renderer.wantsCockpitDepth(),
                  "dashboard produces a stereo mask pair");
            {
                XrCompositionLayerQuad firstHelmetEye{XR_TYPE_COMPOSITION_LAYER_QUAD};
                auto *helmet = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&firstHelmetEye);
                std::vector<const XrCompositionLayerBaseHeader *> stack{base[0], helmet};
                renderer.append(info, true, true, stack, &depth, {helmet});
                check(stack.size() == 4 && stack.front() == base[0] && stack.back() == helmet,
                      "both masked dashboard eyes must compose beneath the visor");
                stack = {base[0], helmet, base[1]};
                renderer.append(info, true, true, stack, &depth, {helmet});
                check(stack.size() == 4 && stack[0] == base[0] && stack[2] == helmet &&
                          stack[3] == base[1] &&
                          reinterpret_cast<const XrCompositionLayerQuad *>(stack[1])->eyeVisibility == XR_EYE_VISIBILITY_BOTH,
                      "single-slot dashboard fallback stays beneath the visor and original HUD");
                XrCompositionLayerQuad gameMenu{XR_TYPE_COMPOSITION_LAYER_QUAD};
                stack = {base[0], base[1], reinterpret_cast<const XrCompositionLayerBaseHeader *>(&gameMenu)};
                renderer.append(info, true, true, stack, &depth, {nullptr, true});
                check(stack.size() == 3 && dash.state == og::OverlayState::LayerLimit,
                      "dashboard cannot take the last free slot when the helmet pair cannot fit");
                check(!renderer.wantsCockpitDepth(), "layer-limited dashboard has no headset depth demand");
                check(channel.lock(), "enable depth for active stream");
                dash.showInStream = 1;
                channel.data()->streamConsumerPid = GetCurrentProcessId();
                channel.data()->streamConsumerBeat = GetTickCount64();
                channel.unlock();
                renderer.append(info, true, true, stack, &depth, {nullptr, true});
                check(stack.size() == 3 && renderer.wantsCockpitDepth(), "full XR layer budget suppressed stream depth");
                check(channel.lock(), "disconnect depth stream");
                channel.data()->streamConsumerPid = 0;
                channel.unlock();
                renderer.append(info, true, true, stack, &depth, {nullptr, true});
                check(!renderer.wantsCockpitDepth(), "disconnected stream retained depth demand");
            }
            auto *left = reinterpret_cast<const XrCompositionLayerQuad *>(masked[2]);
            auto *right = reinterpret_cast<const XrCompositionLayerQuad *>(masked[3]);
            auto &target = images.at(left->subImage.swapchain);
            check(left->eyeVisibility == XR_EYE_VISIBILITY_LEFT &&
                      right->eyeVisibility == XR_EYE_VISIBILITY_RIGHT &&
                      right->subImage.imageRect.offset.x == 32 && pixel(target, 16) == 0 &&
                      (pixel(target, 48) >> 24) == 255,
                  "only the foreground eye clears its premultiplied RGBA pixels");
            std::swap(depth.eyes[0].slice, depth.eyes[1].slice);
            masked = render(true);
            check(masked.size() == 4 && (pixel(target, 16) >> 24) == 255 && pixel(target, 48) == 0,
                  "current game depth changes the mask of an unchanged web frame");
            check(channel.lock(), "publish next dashboard texture");
            dash.frames[1] = {73, reinterpret_cast<uint64_t>(handles[1]), 84, 32, 16,
                              DXGI_FORMAT_R8G8B8A8_UNORM};
            dash.current = 1;
            channel.unlock();
            check(render(true).size() == 4 && dash.consumedFrame == 83,
                  "busy alternate source buffer keeps the previous image with current stereo depth");
            check(channel.lock(), "hold publisher metadata across a VR frame");
            std::swap(depth.eyes[0].slice, depth.eyes[1].slice);
            auto contended = std::async(std::launch::async, [&] { return render(true); }).get();
            channel.unlock();
            check(contended.size() == 4 && pixel(target, 16) == 0 && (pixel(target, 48) >> 24) == 255,
                  "metadata contention preserves the dashboard and recomputes both eyes");
            check(channel.lock(), "hold publisher while changing dashboard controls");
            auto stale = std::async(std::launch::async, [&] { return render(false); }).get();
            channel.data()->invalidatePresentation();
            dash.enabled = 0;
            auto hidden = std::async(std::launch::async, [&] { return render(true); }).get();
            dash.enabled = 1;
            channel.unlock();
            check(stale.size() == 3 &&
                      reinterpret_cast<const XrCompositionLayerQuad *>(stale[2])->eyeVisibility ==
                          XR_EYE_VISIBILITY_BOTH && hidden.size() == 2,
                  "contended renderer rejects old stereo depth and a dashboard invalidated by a hide");
            check(keys[1]->AcquireSync(0, 0) == S_OK, "producer finishes alternate frame");
            keys[1]->ReleaseSync(1);
            check(render(true).size() == 4 && dash.consumedFrame == 84,
                  "next available dashboard texture replaces the retained image");
            check(channel.lock(), "hold publisher across suspended rendering");
            std::vector<const XrCompositionLayerBaseHeader *> suspended{base[0], base[1]};
            std::async(std::launch::async, [&] {
                renderer.append(info, true, false, suspended, &depth);
            }).get();
            auto resumed = std::async(std::launch::async, [&] { return render(true); }).get();
            channel.unlock();
            check(suspended.size() == 2 && resumed.size() == 2,
                  "suspending invalidates a retained dashboard until metadata is revalidated");
            auto fallback = render(false);
            check(
                fallback.size() == 3 &&
                    reinterpret_cast<const XrCompositionLayerQuad *>(fallback[2])->eyeVisibility ==
                        XR_EYE_VISIBILITY_BOTH &&
                    !dash.cockpitDepthActive,
                "stale depth restores the plain dashboard");
            check(channel.lock(), "disable dashboard depth");
            dash.cockpitOcclusion = 0;
            channel.unlock();
            check(render(true).size() == 3 && !renderer.wantsCockpitDepth() && images.size() == 1,
                  "off releases masked output and withdraws capture demand");
            auto closeTo = [](float a, float b) { return std::abs(a - b) < .001f; };
            constexpr float halfRoot = .70710678f;
            head = {{0, 0, 0, 1}, {2, 0, 0}};
            check(channel.lock(), "set cockpit dashboard placement");
            dash.anchor = 1;
            ++dash.recenter;
            dash.x = .35f;
            dash.y = .10f;
            dash.z = -.8f;
            dash.roll = 20;
            dash.widthMeters = .45f;
            channel.unlock();
            render(false);
            head.position.x = 2.2f;
            check(channel.lock(), "start headset placement");
            dash.placementMode = 1;
            ++dash.placementToken;
            channel.unlock();
            render(false);
            check(dash.placementAhead.tick && closeTo(dash.placementAhead.x, .35f) &&
                      closeTo(dash.placementAhead.y, .1f) && closeTo(dash.placementAhead.z, -.8f) &&
                      closeTo(dash.placementAhead.roll, 20) && closeTo(dash.placementAhead.widthMeters, .45f),
                  "grabbing preserves the configured cockpit position, tilt, and width");
            head = {{0, 0, halfRoot, halfRoot}, {2.4f, .2f, .1f}};
            check(channel.lock(), "freeze headset placement");
            dash.placementMode = 2;
            channel.unlock();
            render(false);
            check(dash.placementMode == 3 && closeTo(dash.placementAhead.x, .3f) &&
                      closeTo(dash.placementAhead.y, .35f) && closeTo(dash.placementAhead.z, -.7f) &&
                      closeTo(dash.placementAhead.roll, 110),
                  "second press freezes the rigid head movement since the grab");
            head = {{0, 0, 0, 1}, {2.6f, 0, 0}};
            render(false);
            check(closeTo(dash.placementAhead.x, .3f) && closeTo(dash.placementAhead.roll, 110),
                  "frozen dashboard stays at the committed point");
            check(channel.lock(), "start another correction from the saved pose");
            dash.x = dash.placementAhead.x;
            dash.y = dash.placementAhead.y;
            dash.z = dash.placementAhead.z;
            dash.roll = dash.placementAhead.roll;
            dash.placementMode = 1;
            ++dash.placementToken;
            channel.unlock();
            render(false);
            check(closeTo(dash.placementAhead.x, .3f) && closeTo(dash.placementAhead.roll, 110),
                  "re-grabbing after moving the head preserves the saved pose");
            head.position.z += .2f;
            render(false);
            check(closeTo(dash.placementAhead.z, -.5f), "second correction accumulates on the first");
            check(channel.lock(), "start a grab of a head-locked dashboard");
            dash.anchor = 0;
            dash.x = .2f;
            dash.y = .1f;
            dash.z = -.7f;
            dash.roll = 15;
            ++dash.placementToken;
            channel.unlock();
            head = {{0, 0, halfRoot, halfRoot}, {2.6f, 0, 0}};
            render(false);
            check(closeTo(dash.placementAhead.x, .5f) && closeTo(dash.placementAhead.y, .2f) &&
                      closeTo(dash.placementAhead.z, -.7f) && closeTo(dash.placementAhead.roll, 105),
                  "grabbing a head-locked dashboard resolves its view-space pose in the cockpit");
            check(channel.lock(), "reset dashboard while grabbed");
            dash.placementMode = 4;
            ++dash.placementToken;
            channel.unlock();
            render(false);
            check(dash.placementMode == 3 && closeTo(dash.placementAhead.x, .6f) &&
                      closeTo(dash.placementAhead.y, 0) && closeTo(dash.placementAhead.z, -1) &&
                      closeTo(dash.placementAhead.roll, 0) && closeTo(dash.placementAhead.widthMeters, .45f),
                  "reset freezes a centered dashboard a metre ahead with its current width");
            check(channel.lock(), "preview place in front of me");
            dash.placementMode = 0;
            dash.previewUntil = GetTickCount64() + 3000;
            channel.unlock();
            render(false);
            check(closeTo(dash.placementAhead.x, .6f) && closeTo(dash.placementAhead.z, -.7f) &&
                      closeTo(dash.pose.position[0], .2f),
                  "preview reports the headset target without moving the submitted dashboard");
            const XrPosef neutral{{0, 0, 0, 1}, {0, .2f, -.6f}};
            const XrVector3f hub{0, -.2f, .03f};
            auto placed = og::steered_dashboard(neutral, hub, 23.f, 1.2f);
            auto moved = og::spatial_rotate(placed.orientation, hub);
            check(std::abs(placed.position.x + moved.x - neutral.position.x - hub.x) < .00001f &&
                      std::abs(placed.position.y + moved.y - neutral.position.y - hub.y) <
                          .00001f &&
                      std::abs(placed.position.z + moved.z - neutral.position.z - hub.z) < .00001f,
                  "tilted steering motion preserves the hub in space");
            og::PlacementAdjustment adjust;
            adjust.start(1, -.6f, .4f);
            adjust.nudge(adjust.Up | adjust.Control, 1200);
            adjust.nudge(adjust.Up | adjust.Control, 1210);
            adjust.nudge(adjust.Up | adjust.Alt | adjust.Shift, 1211);
            check(std::abs(adjust.distance - .625f) < .0001f &&
                      std::abs(adjust.width - .401f) < .0001f,
                  "placement keys respect repeat delay, depth and fine size increments");
            adjust.nudge(adjust.Right | adjust.Control, 1300);
            adjust.nudge(adjust.Left | adjust.Alt | adjust.Shift, 1301);
            adjust.nudge(adjust.Up | adjust.Control | adjust.Alt, 1302);
            auto oriented = adjust.orient({0,0,-1,180,12,0,.4f,1});
            check(oriented.yaw == -179 && oriented.pitch == 13 && std::abs(oriented.roll+.1f)<.0001f &&
                  std::abs(adjust.distance-.625f)<.0001f && std::abs(adjust.width-.401f)<.0001f,
                  "rotation chords wrap yaw, tilt pitch, and fine-tune roll independently of size and depth");
            adjust.start(2,-1,.5f);
            check(adjust.yaw == 0 && adjust.pitch == 0 && adjust.roll == 0,
                  "starting a new placement resets the previous orientation adjustments");
        }
        if(!listen) {
            // Use a private cache so this fixture cannot replace a user's wheel.
            auto root=std::filesystem::temp_directory_path()/(L"OpenGridXR-wheel-"+std::to_wstring(GetCurrentProcessId()));
            auto directory=root/"OpenGridXR"/"wheels";
            std::filesystem::create_directories(directory);
            struct Cleanup {std::filesystem::path root; ~Cleanup(){std::error_code ec;std::filesystem::remove_all(root,ec);}} cleanup{root};
            SetEnvironmentVariableW(L"LOCALAPPDATA",root.c_str());
            const std::string id(64,'b');
            std::ofstream file(directory/(id+".agm"),std::ios::binary);
            auto write=[&](const auto& data){file.write(reinterpret_cast<const char*>(&data),sizeof(data));};
            file.write("AGMESH1\0",8);
            const uint32_t header[]={5,16,24,2,1,0};write(header);
            constexpr float materials[]{0,200,201,0}; // casing, display, SimHub LED, casing
            uint32_t indices[24]{};
            for(unsigned panel=0;panel<4;++panel) {
                for(unsigned corner=0;corner<4;++corner) {
                    const float x=float(corner&1), y=float(corner>>1);
                    const float u=materials[panel]==200?x:float(materials[panel]==201);
                    const float vertex[]={-.5f+(panel+x)*.25f,(.5f-y)*.45f,.05f*panel,
                        0,0,1,u,y,materials[panel]};
                    write(vertex);
                }
                constexpr unsigned quad[]={0,1,2,1,3,2};
                for(unsigned i=0;i<6;++i) indices[panel*6+i]=panel*4+quad[i];
            }
            const uint32_t atlas[]={0x80808080,0xffffffff};
            write(indices);write(atlas);file.close();
            renderer.reset();check(initialize(),"restart wheel fixture");renderer.setInputFocused(true);
            check(keys[0]->AcquireSync(0,100)==S_OK,"wheel screen producer");
            std::array<uint32_t,32*16> red;red.fill(0xff000080);
            context11->UpdateSubresource(source[0].Get(),0,nullptr,red.data(),32*4,0);
            context11->Flush();keys[0]->ReleaseSync(1);
            check(channel.lock(),"configure wheel fixture");
            for(auto& item:channel.data()->slots)item={};
            auto& wheel=channel.data()->slots[0];
            wheel.enabled=wheel.webDashboard=wheel.wheelLights=wheel.wheelDevice=wheel.wheelScreen=1;
            wheel.widthMeters=.3f;wheel.z=-.45f;wheel.wheelRgb[1]=128;
            std::copy(id.begin(),id.end(),wheel.wheelModel.begin());
            wheel.frames[0]={900,reinterpret_cast<uint64_t>(handles[0]),910,32,16,DXGI_FORMAT_R8G8B8A8_UNORM};
            channel.data()->producerPid=GetCurrentProcessId();channel.unlock();
            auto renderWheel=[&](bool fresh,ID3D11ShaderResourceView* probe=nullptr,float ambientLight=0.f,
                                 bool tightBudget=false,og::CockpitDepthFrame* depth=nullptr) {
                check(channel.lock(),"wheel heartbeat");
                channel.data()->producerBeat=GetTickCount64();
                wheel.wheelLightTick=fresh?GetTickCount64():0;
                wheel.wheelAmbientLight=ambientLight;
                channel.unlock();
                std::vector<const XrCompositionLayerBaseHeader*> stack{base[0],base[1]};
                if(tightBudget) stack.push_back(base[1]);
                ++info.displayTime;
                if(depth) depth->time=info.displayTime;
                renderer.append(info,true,true,stack,depth,{},probe);
                return stack;
            };
            auto stack=renderWheel(true);
            for(unsigned i=0;stack.size()!=3 && i<300;i++){Sleep(10);stack=renderWheel(true);}
            check(stack.size()==3 && stack[2]->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION,
                  "native wheel publishes one stereo projection layer without eyeVisibility quads");
            check(!renderer.wantsCockpitDepth(), "3D wheel without occlusion never requests cockpit depth");
            auto tight=renderWheel(true,nullptr,0,true);
            check(tight.size()==4 && tight[3]->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION,
                  "stereo wheel fits a single remaining composition layer");
            check(channel.lock(),"enable wheel depth");wheel.cockpitOcclusion=1;channel.unlock();
            renderWheel(true,nullptr,0,true);
            check(d12 || renderer.wantsCockpitDepth(),"one-layer stereo wheel can request occlusion depth");
            check(channel.lock(),"disable wheel depth");wheel.cockpitOcclusion=0;channel.unlock();
            renderWheel(true);
            check(!renderer.wantsCockpitDepth(),"disabling wheel occlusion clears capture demand immediately");
            auto wheelLayer=reinterpret_cast<const XrCompositionLayerProjection*>(stack[2]);
            check(wheelLayer->viewCount==2 && wheelLayer->views &&
                  (wheelLayer->layerFlags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT) &&
                  (wheelLayer->layerFlags & XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT),
                  "stereo wheel retains both views and straight-alpha blending");
            auto left=&wheelLayer->views[0];
            auto right=&wheelLayer->views[1];
            check(left->subImage.swapchain!=right->subImage.swapchain && left->pose.position.x!=right->pose.position.x,
                  "wheel uses distinct viewpoints with geometric parallax");
            const auto& leftImage=images.at(left->subImage.swapchain);
            constexpr uint32_t screenColor=0xff000080, ledColor=0xff008000;
            for(auto eye:{left,right}) {
                auto& image=images.at(eye->subImage.swapchain);
                check(pixel(image,image.width*3/8)==screenColor,"live red dashboard maps to screen faces");
                check(pixel(image,image.width*5/8)==ledColor,"physical green LED maps to the correct faces");
            }
            // A warm-left/cool-right probe must tint the two translucent casing
            // panels while leaving the live page and the physical LED alone.
            {
                auto binding=d12?sharedBridge->acquire(device12.Get(),queue12.Get()):nullptr;
                check(!d12 || binding,"wheel lighting fixture shares the session bridge");
                auto* probeDevice=d12?binding->device.Get():device11.Get();
                D3D11_TEXTURE2D_DESC desc{};
                desc.Width=desc.Height=2;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
                desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                constexpr uint32_t colors[]={0xff0000ff,0xffff0000,0xff0000ff,0xffff0000};
                D3D11_SUBRESOURCE_DATA data{colors,8,0};
                ComPtr<ID3D11Texture2D> texture;
                ComPtr<ID3D11ShaderResourceView> probe;
                check(SUCCEEDED(probeDevice->CreateTexture2D(&desc,&data,&texture)) &&
                      SUCCEEDED(probeDevice->CreateShaderResourceView(texture.Get(),nullptr,&probe)),
                      "wheel lighting fixture probe");
                const auto baseCase=pixel(leftImage,leftImage.width/8);
                check((baseCase>>24)==128 && (baseCase&255)==((baseCase>>16)&255),
                      "unlit casing retains its neutral atlas and straight alpha");
                renderWheel(true,probe.Get(),0);
                check(pixel(leftImage,leftImage.width/8)==baseCase,"zero wheel strength retains the original casing");
                renderWheel(true,nullptr,100);
                check(pixel(leftImage,leftImage.width/8)==baseCase,"missing scene probe retains the original casing");
                if(d12) {
                    ComPtr<ID3D11Texture2D> foreignTexture;
                    ComPtr<ID3D11ShaderResourceView> foreign;
                    check(SUCCEEDED(device11->CreateTexture2D(&desc,&data,&foreignTexture)) &&
                          SUCCEEDED(device11->CreateShaderResourceView(foreignTexture.Get(),nullptr,&foreign)),
                          "foreign-device wheel probe");
                    renderWheel(true,foreign.Get(),100);
                    check(pixel(leftImage,leftImage.width/8)==baseCase,"foreign scene probe retains the original casing");
                }
                renderWheel(true,probe.Get(),50);
                const auto halfCase=pixel(leftImage,leftImage.width/8);
                stack=renderWheel(true,probe.Get(),100);
                for(auto eye:{left,right}) {
                    const auto& image=images.at(eye->subImage.swapchain);
                    auto warm=pixel(image,image.width/8), cool=pixel(image,image.width*7/8);
                    check((warm&255)>(baseCase&255) && (warm&255)>((warm>>16)&255)+20 &&
                          ((cool>>16)&255)>(baseCase&255) && ((cool>>16)&255)>(cool&255)+20 &&
                          (warm>>24)==128 && (cool>>24)==128, "scene probe tints casing in both eyes and preserves alpha");
                    check((baseCase&255)<(halfCase&255) && (halfCase&255)<(warm&255),
                          "intermediate wheel strength blends the casing response");
                    check(pixel(image,image.width*3/8)==screenColor && pixel(image,image.width*5/8)==ledColor,
                          "wheel lighting preserves dashboard and SimHub LED colors");
                }
                stack=renderWheel(false,probe.Get(),100);
                const auto& staleImage=images.at(right->subImage.swapchain);
                check(stack.size()==3 && pixel(staleImage,staleImage.width*5/8)==0xff000000,
                      "stale LED frame stays dark on a scene-lit wheel");
                renderWheel(true,nullptr,100);
                check(pixel(leftImage,leftImage.width/8)==baseCase,"losing the probe restores the original casing");
            }
            check(channel.lock(),"select Artura physical device");wheel.wheelDevice=6;channel.unlock();
            stack=renderWheel(true);
            check(stack.size()==3 && pixel(leftImage,leftImage.width*5/8)==ledColor,
                  "Artura device six renders direct physical LED groups without GSI ring indexing");
            std::array<XrCompositionLayerProjectionView,4> quadViews{views[0],views[1],views[0],views[1]};
            app.viewCount=4;app.views=quadViews.data();
            stack=renderWheel(true);
            check(stack.size()==4 && stack[2]->type==XR_TYPE_COMPOSITION_LAYER_QUAD &&
                  stack[3]->type==XR_TYPE_COMPOSITION_LAYER_QUAD,
                  "four-view sessions retain the existing pair of eye-specific quads");
            check(renderWheel(true,nullptr,0,true).size()==3,
                  "four-view wheel does not exceed a single remaining composition layer");
            app.viewCount=2;app.views=views;
            stack=renderWheel(true);
            check(stack.size()==3 && stack[2]->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION,
                  "returning to stereo restores the single projection layer");
            if(!d12) {
                renderer.reset();layerLimit=6;check(initialize(),"restart mixed wheel/dashboard fixture");
                for(auto& key:keys) {
                    if(key->AcquireSync(0,0)!=S_OK) check(key->AcquireSync(1,0)==S_OK,"reclaim mixed source");
                    key->ReleaseSync(1);
                }
                check(channel.lock(),"configure independent dashboard depth");
                auto& dashboard=channel.data()->slots[1];
                dashboard=wheel;dashboard.wheelModel.fill(0);dashboard.cockpitOcclusion=1;
                dashboard.frames[0]={901,reinterpret_cast<uint64_t>(handles[1]),911,32,16,DXGI_FORMAT_R8G8B8A8_UNORM};
                channel.unlock();
                D3D11_TEXTURE2D_DESC desc{};
                desc.Width=desc.Height=desc.MipLevels=desc.SampleDesc.Count=1;desc.ArraySize=2;
                desc.Format=DXGI_FORMAT_R32_FLOAT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                const float nearDepth=0;
                D3D11_SUBRESOURCE_DATA samples[2]{{&nearDepth,4,4},{&nearDepth,4,4}};
                ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> depthView;
                check(SUCCEEDED(device11->CreateTexture2D(&desc,samples,&texture)) &&
                      SUCCEEDED(device11->CreateShaderResourceView(texture.Get(),nullptr,&depthView)),"mixed depth texture");
                og::CockpitDepthFrame depth{};depth.space=app.space;
                for(unsigned i=0;i<2;++i) {
                    auto& eye=depth.eyes[i];eye.view=depthView.Get();eye.pose=views[i].pose;
                    eye.fov={-.7f,.7f,.7f,-.7f};eye.rect.extent={1,1};eye.slice=i;eye.invNear=2;eye.invFar=.1f;
                }
                stack=renderWheel(true,nullptr,0,false,&depth);
                for(unsigned i=0;stack.size()!=5 && i<300;++i) {Sleep(10);stack=renderWheel(true,nullptr,0,false,&depth);}
                check(stack.size()==5 && renderer.wantsCockpitDepth() && dashboard.cockpitDepthActive && !wheel.cockpitDepthActive,
                      "dashboard keeps its own depth capture and masking beside an unoccluded 3D wheel");
                auto* wheelLayer=reinterpret_cast<const XrCompositionLayerProjection*>(stack[2]);
                auto& wheelImage=images.at(wheelLayer->views[0].subImage.swapchain);
                check(pixel(wheelImage,wheelImage.width*3/8)==screenColor,
                      "another dashboard's foreground depth cannot mask a wheel with occlusion off");
                check(channel.lock(),"disable final depth consumer");dashboard.cockpitOcclusion=0;channel.unlock();
                renderWheel(true,nullptr,0,false,&depth);
                check(!renderer.wantsCockpitDepth(),"last occlusion consumer releases capture demand while 3D rendering continues");
            }
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
