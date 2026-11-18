#pragma once
#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <openxr/openxr_platform.h>
#include "depthxr/openxr_layer.h"
#include "depthxr/openxr_loader_api_layer.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The layer's loader entry point (layer_entry.cpp).
extern "C" XrResult XRAPI_CALL xrCreateApiLayerInstance(const XrInstanceCreateInfo* info,
                                                        const depthxr::XrApiLayerCreateInfo* layer_info,
                                                        XrInstance* instance);
namespace {

using Microsoft::WRL::ComPtr;
using Color = std::array<std::uint8_t, 4>;

constexpr const char* kD3D11Extension = "XR_KHR_D3D11_enable";
constexpr const char* kD3D12Extension = "XR_KHR_D3D12_enable";
constexpr const char* kQuadViewsExtension = "XR_VARJO_quad_views";
constexpr const char* kFoveatedExtension = "XR_VARJO_foveated_rendering";
constexpr XrSystemId kSystem = 7;
constexpr XrDuration kPeriod = 11'111'111;
constexpr std::uint32_t kImages = 3;
constexpr std::uint32_t kWidth = 64;
constexpr std::uint32_t kHeight = 48;
// An application structure of no known type, chained after the ones the
// layer reads (it must reach the runtime as it was).
constexpr XrStructureType kApplicationStructType = static_cast<XrStructureType>(1000999000);

std::filesystem::path g_root; // the test's own directory for everything the layer writes
void Check(bool value, const std::string& message) {
    if (!value) {
        std::cerr << "FAILED: " << message << "\n(layer log: " << g_root.string() << ")" << std::endl;
        std::_Exit(1);
    }
}
bool SameLuid(const LUID& a, const LUID& b) {
    return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

template <typename T>
T Handle(std::uintptr_t value) {
    return reinterpret_cast<T>(value);
}

void SetEnv(const char* name, const std::string& value) {
    _putenv_s(name, value.c_str());
}

// --- The game ---------------------------------------------------------------

struct Game {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    LUID luid{};
    bool debug{false};
};

bool MakeGame(Game& game) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    const D3D_DRIVER_TYPE type = D3D_DRIVER_TYPE_WARP;
    // The Direct3D 11 debug layer when its SDK layers are installed.
    game.debug = SUCCEEDED(D3D11CreateDevice(nullptr, type, nullptr,
                                             D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG, levels, 2,
                                             D3D11_SDK_VERSION, &game.device, nullptr, &game.context));
    if (!game.debug && FAILED(D3D11CreateDevice(nullptr, type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                                levels, 2, D3D11_SDK_VERSION, &game.device, nullptr, &game.context))) {
        return false;
    }
    ComPtr<IDXGIDevice> dxgi_device;
    ComPtr<IDXGIAdapter> game_adapter;
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(game.device.As(&dxgi_device)) || FAILED(dxgi_device->GetAdapter(&game_adapter)) ||
        FAILED(game_adapter->GetDesc(&desc))) {
        return false;
    }
    game.luid = desc.AdapterLuid;
    return true;
}
void Clear(Game& game, ID3D11Texture2D* texture, const Color& color, std::uint32_t slice) {
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    D3D11_RENDER_TARGET_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (desc.SampleDesc.Count > 1) {
        view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
        view.Texture2DMSArray.FirstArraySlice = slice;
        view.Texture2DMSArray.ArraySize = 1;
    } else {
        view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        view.Texture2DArray.FirstArraySlice = slice;
        view.Texture2DArray.ArraySize = 1;
    }
    ComPtr<ID3D11RenderTargetView> target;
    Check(SUCCEEDED(game.device->CreateRenderTargetView(texture, &view, &target)), "a render-target view of an image");
    const float rgba[4] = {color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f, color[3] / 255.0f};
    game.context->ClearRenderTargetView(target.Get(), rgba);
}
struct StandInSwapchain {
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    std::vector<ComPtr<ID3D11Texture2D>> images11; // a Direct3D 11 session's
    std::deque<std::uint32_t> acquired;            // oldest first
    std::uint32_t next{0};
    std::optional<std::uint32_t> released; // the image released last
    int acquires{0};
    int waits{0};
    int releases{0};
};

// The stand-in runtime: what it does, and what it saw.
struct StandIn {
    std::vector<std::string> extensions{kD3D11Extension, kD3D12Extension}; // listed before the instance
    LUID luid{};                      // the adapter it composes on
    std::uint32_t recommended_width{kWidth}; // each stereo view's recommended size
    std::uint32_t recommended_height{kHeight};
    std::array<XrFovf, 2> fovs{{{-0.8f, 0.8f, 0.8f, -0.8f}, {-0.8f, 0.8f, 0.8f, -0.8f}}};
    std::vector<std::vector<std::string>> instance_attempts; // each attempt's extensions
    XrInstance instance{XR_NULL_HANDLE};
    XrSession session{XR_NULL_HANDLE};
    ComPtr<ID3D11Device> device11;
    std::map<XrSwapchain, StandInSwapchain> swapchains;
    std::vector<XrSwapchainCreateInfo> swapchain_requests; // as received
    std::function<void(const XrFrameEndInfo&)> on_end_frame;
    XrTime time{1'000'000'000};
    std::uintptr_t handles{0x1000};
};

StandIn g_runtime;
std::recursive_mutex g_runtime_mutex;
StandInSwapchain* FindSwapchain(XrSwapchain swapchain) {
    const auto it = g_runtime.swapchains.find(swapchain);
    return it == g_runtime.swapchains.end() ? nullptr : &it->second;
}
bool ReadBack11(const std::vector<std::pair<ID3D11Texture2D*, std::uint32_t>>& sources, std::vector<Color>& texels) {
    ComPtr<ID3D11DeviceContext> context;
    g_runtime.device11->GetImmediateContext(&context);
    texels.clear();
    for (const auto& [image, slice] : sources) {
        D3D11_TEXTURE2D_DESC desc{};
        image->GetDesc(&desc);
        D3D11_TEXTURE2D_DESC staging_desc{};
        staging_desc.Width = 1;
        staging_desc.Height = 1;
        staging_desc.MipLevels = 1;
        staging_desc.ArraySize = 1;
        staging_desc.Format = desc.Format;
        staging_desc.SampleDesc.Count = 1;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(g_runtime.device11->CreateTexture2D(&staging_desc, nullptr, &staging))) {
            return false;
        }
        const D3D11_BOX box{desc.Width / 2, desc.Height / 2, 0, desc.Width / 2 + 1, desc.Height / 2 + 1, 1};
        context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, image, D3D11CalcSubresource(0, slice, desc.MipLevels),
                                       &box);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            return false;
        }
        const auto* texel = static_cast<const std::uint8_t*>(mapped.pData);
        texels.push_back({texel[0], texel[1], texel[2], texel[3]});
        context->Unmap(staging.Get(), 0);
    }
    return true;
}

XrResult XRAPI_CALL RtEnumerateInstanceExtensions(const char*, std::uint32_t capacity, std::uint32_t* count,
                                                   XrExtensionProperties* properties) {
    std::scoped_lock lock(g_runtime_mutex);
    const std::vector<std::string>& names = g_runtime.extensions;
    *count = static_cast<std::uint32_t>(names.size());
    if (capacity == 0) return XR_SUCCESS;
    if (capacity < names.size()) return XR_ERROR_SIZE_INSUFFICIENT;
    for (std::size_t i = 0; i < names.size(); ++i) {
        std::strncpy(properties[i].extensionName, names[i].c_str(), XR_MAX_EXTENSION_NAME_SIZE - 1);
        properties[i].extensionVersion = 1;
    }
    return XR_SUCCESS;
}
depthxr::XrApiLayerNextInfo* g_layer_below = nullptr;

XrResult XRAPI_CALL RtCreateApiLayerInstance(const XrInstanceCreateInfo* info,
                                             const depthxr::XrApiLayerCreateInfo* layer_info, XrInstance* instance) {
    std::scoped_lock lock(g_runtime_mutex);
    StandIn& rt = g_runtime;
    std::vector<std::string> extensions;
    for (std::uint32_t i = 0; i < info->enabledExtensionCount; ++i) {
        extensions.emplace_back(info->enabledExtensionNames[i]);
    }
    rt.instance_attempts.push_back(extensions);
    // The stand-in is the last of the chain: nothing follows it.
    Check(layer_info && layer_info->nextInfo == g_layer_below, "the layer hands the rest of the chain on");
    *instance = rt.instance = Handle<XrInstance>(++rt.handles);
    return XR_SUCCESS;
}

// The session's swapchains (their images) and everything of its device go.
void DropSession(StandIn& rt) {
    rt.swapchains.clear();
    rt.device11.Reset();
    rt.session = XR_NULL_HANDLE;
}

XrResult XRAPI_CALL RtDestroyInstance(XrInstance instance) {
    std::scoped_lock lock(g_runtime_mutex);
    StandIn& rt = g_runtime;
    Check(instance == rt.instance, "the runtime's instance is destroyed");
    DropSession(rt);
    rt.instance = XR_NULL_HANDLE;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtGetInstanceProperties(XrInstance, XrInstanceProperties* properties) {
    properties->runtimeVersion = XR_MAKE_VERSION(1, 0, 0);
    std::strncpy(properties->runtimeName, "VectorXR stand-in runtime", XR_MAX_RUNTIME_NAME_SIZE - 1);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtGetSystem(XrInstance, const XrSystemGetInfo*, XrSystemId* system) {
    *system = kSystem;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtGetSystemProperties(XrInstance, XrSystemId, XrSystemProperties* properties) {
    properties->systemId = kSystem;
    std::strncpy(properties->systemName, "Stand-in headset", XR_MAX_SYSTEM_NAME_SIZE - 1);
    properties->graphicsProperties.maxLayerCount = 16;
    properties->graphicsProperties.maxSwapchainImageWidth = 4096;
    properties->graphicsProperties.maxSwapchainImageHeight = 4096;
    properties->trackingProperties.orientationTracking = XR_TRUE;
    properties->trackingProperties.positionTracking = XR_TRUE;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtGetD3D11Requirements(XrInstance, XrSystemId, XrGraphicsRequirementsD3D11KHR* requirements) {
    std::scoped_lock lock(g_runtime_mutex);
    requirements->adapterLuid = g_runtime.luid;
    requirements->minFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtGetD3D12Requirements(XrInstance, XrSystemId, XrGraphicsRequirementsD3D12KHR* requirements) {
    std::scoped_lock lock(g_runtime_mutex);
    requirements->adapterLuid = g_runtime.luid;
    requirements->minFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtEnumerateViewConfigurations(XrInstance, XrSystemId, std::uint32_t capacity,
                                                   std::uint32_t* count, XrViewConfigurationType* types) {
    *count = 1;
    if (capacity) types[0] = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtGetViewConfigurationProperties(XrInstance, XrSystemId, XrViewConfigurationType type,
                                                      XrViewConfigurationProperties* properties) {
    properties->viewConfigurationType = type;
    properties->fovMutable = XR_TRUE;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtEnumerateViewConfigurationViews(XrInstance, XrSystemId, XrViewConfigurationType,
                                                       std::uint32_t capacity, std::uint32_t* count,
                                                       XrViewConfigurationView* views) {
    std::scoped_lock lock(g_runtime_mutex);
    *count = 2;
    for (std::uint32_t i = 0; i < std::min(capacity, 2u); ++i) {
        views[i].recommendedImageRectWidth = g_runtime.recommended_width;
        views[i].recommendedImageRectHeight = g_runtime.recommended_height;
        views[i].maxImageRectWidth = 4096;
        views[i].maxImageRectHeight = 4096;
        views[i].recommendedSwapchainSampleCount = 1;
        views[i].maxSwapchainSampleCount = 4;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtEnumerateBlendModes(XrInstance, XrSystemId, XrViewConfigurationType, std::uint32_t capacity,
                                          std::uint32_t* count, XrEnvironmentBlendMode* modes) {
    *count = 1;
    if (capacity) modes[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtPollEvent(XrInstance, XrEventDataBuffer*) {
    return XR_EVENT_UNAVAILABLE;
}

XrResult XRAPI_CALL RtCreateSession(XrInstance, const XrSessionCreateInfo* info, XrSession* session) {
    std::scoped_lock lock(g_runtime_mutex);
    StandIn& rt = g_runtime;
    const auto* first = info ? static_cast<const XrBaseInStructure*>(info->next) : nullptr;
    if (first && first->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
        const auto& binding = *reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(first);
        if (!binding.device) return XR_ERROR_GRAPHICS_DEVICE_INVALID;
        rt.device11 = binding.device;
    } else {
        return XR_ERROR_GRAPHICS_DEVICE_INVALID;
    }
    *session = rt.session = Handle<XrSession>(++rt.handles);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtDestroySession(XrSession session) {
    std::scoped_lock lock(g_runtime_mutex);
    StandIn& rt = g_runtime;
    Check(session == rt.session, "the runtime's session is destroyed");
    DropSession(rt);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtBeginSession(XrSession, const XrSessionBeginInfo* info) {
    std::scoped_lock lock(g_runtime_mutex);
    // Quad views are the layer's: the stand-in has the stereo configuration only.
    Check(info && info->primaryViewConfigurationType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
          "the runtime's session begins with the stereo view configuration");
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtEndSession(XrSession) {
    std::scoped_lock lock(g_runtime_mutex);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtAttachActionSets(XrSession, const XrSessionActionSetsAttachInfo*) {
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtSyncActions(XrSession, const XrActionsSyncInfo*) {
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtEnumerateReferenceSpaces(XrSession, std::uint32_t capacity, std::uint32_t* count,
                                                XrReferenceSpaceType* spaces) {
    constexpr XrReferenceSpaceType kSpaces[] = {XR_REFERENCE_SPACE_TYPE_VIEW, XR_REFERENCE_SPACE_TYPE_LOCAL,
                                                XR_REFERENCE_SPACE_TYPE_STAGE};
    *count = 3;
    if (capacity == 0) return XR_SUCCESS;
    if (capacity < 3) return XR_ERROR_SIZE_INSUFFICIENT;
    std::copy(std::begin(kSpaces), std::end(kSpaces), spaces);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtGetReferenceSpaceBoundsRect(XrSession, XrReferenceSpaceType, XrExtent2Df* bounds) {
    *bounds = {};
    return XR_SPACE_BOUNDS_UNAVAILABLE;
}

XrResult XRAPI_CALL RtCreateReferenceSpace(XrSession, const XrReferenceSpaceCreateInfo*, XrSpace* space) {
    std::scoped_lock lock(g_runtime_mutex);
    *space = Handle<XrSpace>(++g_runtime.handles);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtCreateActionSpace(XrSession, const XrActionSpaceCreateInfo*, XrSpace* space) {
    std::scoped_lock lock(g_runtime_mutex);
    *space = Handle<XrSpace>(++g_runtime.handles);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtDestroySpace(XrSpace) {
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtLocateSpace(XrSpace, XrSpace, XrTime, XrSpaceLocation* location) {
    location->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT |
                              XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
    location->pose = {{0, 0, 0, 1}, {0, 0, 0}};
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtLocateViews(XrSession, const XrViewLocateInfo*, XrViewState* state, std::uint32_t capacity,
                                  std::uint32_t* count, XrView* views) {
    std::scoped_lock lock(g_runtime_mutex);
    *count = 2;
    state->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT |
                            XR_VIEW_STATE_ORIENTATION_TRACKED_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT;
    for (std::uint32_t i = 0; i < std::min(capacity, 2u); ++i) {
        views[i].pose = {{0, 0, 0, 1}, {i ? 0.032f : -0.032f, 0, 0}};
        views[i].fov = g_runtime.fovs[i];
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtWaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState* state) {
    state->predictedDisplayTime = g_runtime.time += kPeriod;
    state->predictedDisplayPeriod = kPeriod;
    state->shouldRender = XR_TRUE;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL RtBeginFrame(XrSession, const XrFrameBeginInfo*) { return XR_SUCCESS; }
XrResult XRAPI_CALL RtEndFrame(XrSession, const XrFrameEndInfo* frame) {
    if (g_runtime.on_end_frame) g_runtime.on_end_frame(*frame);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL RtEnumerateSwapchainFormats(XrSession, std::uint32_t capacity, std::uint32_t* count,
                                                 std::int64_t* formats) {
    constexpr std::int64_t kFormats[] = {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM,
                                         DXGI_FORMAT_D32_FLOAT};
    *count = 3;
    if (capacity == 0) return XR_SUCCESS;
    if (capacity < 3) return XR_ERROR_SIZE_INSUFFICIENT;
    std::copy(std::begin(kFormats), std::end(kFormats), formats);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtCreateSwapchain(XrSession session, const XrSwapchainCreateInfo* info, XrSwapchain* swapchain) {
    std::scoped_lock lock(g_runtime_mutex);
    StandIn& rt = g_runtime;
    rt.swapchain_requests.push_back(*info);
    if (session != rt.session) return XR_ERROR_HANDLE_INVALID;
    const bool depth = (info->usageFlags & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
    StandInSwapchain chain;
    chain.info = *info;
    chain.info.next = nullptr;
    const std::uint32_t array_size = std::max(1u, info->arraySize) * std::max(1u, info->faceCount);
    for (std::uint32_t i = 0; i < kImages; ++i) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = info->width;
            desc.Height = info->height;
            desc.MipLevels = std::max(1u, info->mipCount);
            desc.ArraySize = array_size;
            desc.Format = depth ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R8G8B8A8_TYPELESS;
            desc.SampleDesc.Count = std::max(1u, info->sampleCount);
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = depth ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            ComPtr<ID3D11Texture2D> image;
            if (FAILED(rt.device11->CreateTexture2D(&desc, nullptr, &image))) {
                return XR_ERROR_RUNTIME_FAILURE;
            }
            chain.images11.push_back(image);
    }
    *swapchain = Handle<XrSwapchain>(++rt.handles);
    rt.swapchains[*swapchain] = std::move(chain);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtDestroySwapchain(XrSwapchain swapchain) {
    std::scoped_lock lock(g_runtime_mutex);
    StandIn& rt = g_runtime;
    if (!FindSwapchain(swapchain)) return XR_ERROR_HANDLE_INVALID;
    rt.swapchains.erase(swapchain);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtEnumerateSwapchainImages(XrSwapchain swapchain, std::uint32_t capacity, std::uint32_t* count,
                                                XrSwapchainImageBaseHeader* images) {
    std::scoped_lock lock(g_runtime_mutex);
    const StandInSwapchain* chain = FindSwapchain(swapchain);
    if (!chain) return XR_ERROR_HANDLE_INVALID;
    *count = static_cast<std::uint32_t>(chain->images11.size());
    if (capacity == 0) return XR_SUCCESS;
    if (capacity < chain->images11.size()) return XR_ERROR_SIZE_INSUFFICIENT;
        auto* out = reinterpret_cast<XrSwapchainImageD3D11KHR*>(images);
        for (std::size_t i = 0; i < chain->images11.size(); ++i) {
            if (out[i].type != XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR) return XR_ERROR_VALIDATION_FAILURE;
            out[i].texture = chain->images11[i].Get();
        }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtAcquire(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo*, std::uint32_t* index) {
    std::scoped_lock lock(g_runtime_mutex);
    StandInSwapchain* chain = FindSwapchain(swapchain);
    if (!chain) return XR_ERROR_HANDLE_INVALID;
    if (chain->acquired.size() >= chain->images11.size()) return XR_ERROR_CALL_ORDER_INVALID;
    // Pimax's runtime hands the image back released last, every frame.
    *index = chain->next;
    chain->next = (*index + 1) % static_cast<std::uint32_t>(chain->images11.size());
    chain->acquired.push_back(*index);
    ++chain->acquires;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtWait(XrSwapchain swapchain, const XrSwapchainImageWaitInfo*) {
    std::scoped_lock lock(g_runtime_mutex);
    StandInSwapchain* chain = FindSwapchain(swapchain);
    if (!chain) return XR_ERROR_HANDLE_INVALID;
    // An image acquired and not waited for, and none waited for and not
    // released (OpenXR's order: a wait only after the image waited before it
    // was released).
    if (chain->waits >= chain->acquires || chain->waits > chain->releases) return XR_ERROR_CALL_ORDER_INVALID;
    ++chain->waits;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL RtRelease(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo*) {
    std::scoped_lock lock(g_runtime_mutex);
    StandIn& rt = g_runtime;
    StandInSwapchain* chain = FindSwapchain(swapchain);
    if (!chain) return XR_ERROR_HANDLE_INVALID;
    if (chain->acquired.empty() || chain->releases >= chain->waits) return XR_ERROR_CALL_ORDER_INVALID;
    chain->released = chain->acquired.front();
    chain->acquired.pop_front();
    ++chain->releases;
    return XR_SUCCESS;
}

template <typename F>
PFN_xrVoidFunction Function(F* function) {
    return reinterpret_cast<PFN_xrVoidFunction>(function);
}

XrResult XRAPI_CALL RtGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
    static const std::map<std::string, PFN_xrVoidFunction> kFunctions = {
        {"xrDestroyInstance", Function(RtDestroyInstance)},
        {"xrGetInstanceProperties", Function(RtGetInstanceProperties)},
        {"xrGetSystem", Function(RtGetSystem)},
        {"xrGetSystemProperties", Function(RtGetSystemProperties)},
        {"xrGetD3D11GraphicsRequirementsKHR", Function(RtGetD3D11Requirements)},
        {"xrEnumerateViewConfigurations", Function(RtEnumerateViewConfigurations)},
        {"xrGetViewConfigurationProperties", Function(RtGetViewConfigurationProperties)},
        {"xrEnumerateViewConfigurationViews", Function(RtEnumerateViewConfigurationViews)},
        {"xrEnumerateEnvironmentBlendModes", Function(RtEnumerateBlendModes)},
        {"xrPollEvent", Function(RtPollEvent)},
        {"xrCreateSession", Function(RtCreateSession)},
        {"xrDestroySession", Function(RtDestroySession)},
        {"xrBeginSession", Function(RtBeginSession)},
        {"xrEndSession", Function(RtEndSession)},
        {"xrAttachSessionActionSets", Function(RtAttachActionSets)},
        {"xrSyncActions", Function(RtSyncActions)},
        {"xrEnumerateReferenceSpaces", Function(RtEnumerateReferenceSpaces)},
        {"xrGetReferenceSpaceBoundsRect", Function(RtGetReferenceSpaceBoundsRect)},
        {"xrCreateReferenceSpace", Function(RtCreateReferenceSpace)},
        {"xrCreateActionSpace", Function(RtCreateActionSpace)},
        {"xrDestroySpace", Function(RtDestroySpace)},
        {"xrLocateSpace", Function(RtLocateSpace)},
        {"xrLocateViews", Function(RtLocateViews)},
        {"xrWaitFrame", Function(RtWaitFrame)},
        {"xrBeginFrame", Function(RtBeginFrame)},
        {"xrEndFrame", Function(RtEndFrame)},
        {"xrEnumerateSwapchainFormats", Function(RtEnumerateSwapchainFormats)},
        {"xrCreateSwapchain", Function(RtCreateSwapchain)},
        {"xrDestroySwapchain", Function(RtDestroySwapchain)},
        {"xrEnumerateSwapchainImages", Function(RtEnumerateSwapchainImages)},
        {"xrAcquireSwapchainImage", Function(RtAcquire)},
        {"xrWaitSwapchainImage", Function(RtWait)},
        {"xrReleaseSwapchainImage", Function(RtRelease)},
    };
    *function = nullptr;
    if (instance == XR_NULL_HANDLE) {
        // Before the instance: the loader's extension scan.
        std::scoped_lock lock(g_runtime_mutex);
        if (std::strcmp(name, "xrEnumerateInstanceExtensionProperties") == 0) {
            *function = Function(RtEnumerateInstanceExtensions);
            return XR_SUCCESS;
        }
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }
    const auto it = kFunctions.find(name);
    if (it == kFunctions.end()) return XR_ERROR_FUNCTION_UNSUPPORTED;
    *function = it->second;
    return XR_SUCCESS;
}
struct Api {
    XrInstance instance{XR_NULL_HANDLE};
    PFN_xrDestroyInstance destroy_instance{};
    PFN_xrGetSystem get_system{};
    PFN_xrGetD3D11GraphicsRequirementsKHR d3d11_requirements{};
    PFN_xrEnumerateViewConfigurationViews enumerate_views{};
    PFN_xrPollEvent poll_event{};
    PFN_xrCreateSession create_session{};
    PFN_xrDestroySession destroy_session{};
    PFN_xrBeginSession begin_session{};
    PFN_xrEndSession end_session{};
    PFN_xrCreateReferenceSpace create_space{};
    PFN_xrDestroySpace destroy_space{};
    PFN_xrCreateSwapchain create_swapchain{};
    PFN_xrDestroySwapchain destroy_swapchain{};
    PFN_xrEnumerateSwapchainImages enumerate_images{};
    PFN_xrAcquireSwapchainImage acquire{};
    PFN_xrWaitSwapchainImage wait{};
    PFN_xrReleaseSwapchainImage release{};
    PFN_xrWaitFrame wait_frame{};
    PFN_xrBeginFrame begin_frame{};
    PFN_xrEndFrame end_frame{};
    PFN_xrLocateViews locate_views{};
};

template <typename T>
T LayerFunction(XrInstance instance, const char* name) {
    PFN_xrVoidFunction function = nullptr;
    Check(XR_SUCCEEDED(xrGetInstanceProcAddr(instance, name, &function)) && function,
          std::string("the layer hands out ") + name);
    return reinterpret_cast<T>(function);
}

Api Load(XrInstance instance) {
    Api api;
    api.instance = instance;
    api.destroy_instance = LayerFunction<PFN_xrDestroyInstance>(instance, "xrDestroyInstance");
    api.get_system = LayerFunction<PFN_xrGetSystem>(instance, "xrGetSystem");
    api.d3d11_requirements =
        LayerFunction<PFN_xrGetD3D11GraphicsRequirementsKHR>(instance, "xrGetD3D11GraphicsRequirementsKHR");
    api.enumerate_views =
        LayerFunction<PFN_xrEnumerateViewConfigurationViews>(instance, "xrEnumerateViewConfigurationViews");
    api.poll_event = LayerFunction<PFN_xrPollEvent>(instance, "xrPollEvent");
    api.create_session = LayerFunction<PFN_xrCreateSession>(instance, "xrCreateSession");
    api.destroy_session = LayerFunction<PFN_xrDestroySession>(instance, "xrDestroySession");
    api.begin_session = LayerFunction<PFN_xrBeginSession>(instance, "xrBeginSession");
    api.end_session = LayerFunction<PFN_xrEndSession>(instance, "xrEndSession");
    api.create_space = LayerFunction<PFN_xrCreateReferenceSpace>(instance, "xrCreateReferenceSpace");
    api.destroy_space = LayerFunction<PFN_xrDestroySpace>(instance, "xrDestroySpace");
    api.create_swapchain = LayerFunction<PFN_xrCreateSwapchain>(instance, "xrCreateSwapchain");
    api.destroy_swapchain = LayerFunction<PFN_xrDestroySwapchain>(instance, "xrDestroySwapchain");
    api.enumerate_images = LayerFunction<PFN_xrEnumerateSwapchainImages>(instance, "xrEnumerateSwapchainImages");
    api.acquire = LayerFunction<PFN_xrAcquireSwapchainImage>(instance, "xrAcquireSwapchainImage");
    api.wait = LayerFunction<PFN_xrWaitSwapchainImage>(instance, "xrWaitSwapchainImage");
    api.release = LayerFunction<PFN_xrReleaseSwapchainImage>(instance, "xrReleaseSwapchainImage");
    api.wait_frame = LayerFunction<PFN_xrWaitFrame>(instance, "xrWaitFrame");
    api.begin_frame = LayerFunction<PFN_xrBeginFrame>(instance, "xrBeginFrame");
    api.end_frame = LayerFunction<PFN_xrEndFrame>(instance, "xrEndFrame");
    api.locate_views = LayerFunction<PFN_xrLocateViews>(instance, "xrLocateViews");
    return api;
}

// xrCreateInstance as the loader makes it with the layer enabled: the
// stand-in runtime is the next (and last) of the chain.
XrResult CreateInstance(const std::vector<const char*>& extensions, XrInstance& instance) {
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strncpy(info.applicationInfo.applicationName, "LMU external quad test", XR_MAX_APPLICATION_NAME_SIZE - 1);
    std::strncpy(info.applicationInfo.engineName, "Direct3D 11", XR_MAX_ENGINE_NAME_SIZE - 1);
    info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.enabledExtensionNames = extensions.empty() ? nullptr : extensions.data();
    depthxr::XrApiLayerNextInfo next{};
    next.structType = depthxr::XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO;
    next.structVersion = depthxr::XR_API_LAYER_NEXT_INFO_STRUCT_VERSION;
    next.structSize = sizeof(next);
    std::strncpy(next.layerName, "XR_APILAYER_TEST_stand_in_runtime", XR_MAX_API_LAYER_NAME_SIZE - 1);
    next.nextGetInstanceProcAddr = RtGetInstanceProcAddr;
    next.nextCreateApiLayerInstance = RtCreateApiLayerInstance;
    next.next = g_layer_below;
    depthxr::XrApiLayerCreateInfo create{};
    create.structType = depthxr::XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO;
    create.structVersion = depthxr::XR_API_LAYER_CREATE_INFO_STRUCT_VERSION;
    create.structSize = sizeof(create);
    create.nextInfo = &next;
    instance = XR_NULL_HANDLE;
    return xrCreateApiLayerInstance(&info, &create, &instance);
}

// The application's session create info: its Direct3D 11 binding first, a
// structure of its own after it.
struct SessionCreate {
    XrBaseInStructure application_struct{kApplicationStructType};
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};
    SessionCreate(ID3D11Device* device, XrSystemId system) {
        binding.next = &application_struct;
        binding.device = device;
        info.next = &binding;
        info.systemId = system;
    }
    SessionCreate(const SessionCreate&) = delete;
    SessionCreate& operator=(const SessionCreate&) = delete;
};

struct App {
    Api api;
    XrSystemId system{XR_NULL_SYSTEM_ID};
    XrSession session{XR_NULL_HANDLE};
    XrSpace space{XR_NULL_HANDLE};
};

// A Direct3D 11 game's start: its instance, system and graphics requirements.
App StartApp(const std::vector<const char*>& extensions = {kD3D11Extension}) {
    XrInstance instance = XR_NULL_HANDLE;
    const XrResult result = CreateInstance(extensions, instance);
    Check(result == XR_SUCCESS && instance != XR_NULL_HANDLE,
          "xrCreateInstance through the layer: " + std::to_string(result));
    App app;
    app.api = Load(instance);
    XrSystemGetInfo get{XR_TYPE_SYSTEM_GET_INFO};
    get.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    Check(app.api.get_system(instance, &get, &app.system) == XR_SUCCESS, "xrGetSystem");
    XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    Check(app.api.d3d11_requirements(instance, app.system, &requirements) == XR_SUCCESS &&
              SameLuid(requirements.adapterLuid, g_runtime.luid),
          "xrGetD3D11GraphicsRequirementsKHR");
    return app;
}

void BeginSession(App& app, XrViewConfigurationType configuration = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
    XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    space.poseInReferenceSpace.orientation.w = 1;
    Check(app.api.create_space(app.session, &space, &app.space) == XR_SUCCESS, "xrCreateReferenceSpace");
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    Check(app.api.poll_event(app.api.instance, &event) == XR_EVENT_UNAVAILABLE, "xrPollEvent");
    XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
    begin.primaryViewConfigurationType = configuration;
    Check(app.api.begin_session(app.session, &begin) == XR_SUCCESS, "xrBeginSession");
}

// Its session (when it has one) and instance destroyed.
void StopApp(App& app) {
    if (app.space != XR_NULL_HANDLE) {
        Check(app.api.destroy_space(app.space) == XR_SUCCESS, "xrDestroySpace");
        app.space = XR_NULL_HANDLE;
    }
    if (app.session != XR_NULL_HANDLE) {
        Check(app.api.destroy_session(app.session) == XR_SUCCESS, "xrDestroySession");
        app.session = XR_NULL_HANDLE;
    }
    Check(app.api.destroy_instance(app.api.instance) == XR_SUCCESS, "xrDestroyInstance");
}

XrSwapchainCreateInfo ColorInfo(std::uint32_t samples = 1, std::uint32_t width = kWidth,
                                std::uint32_t height = kHeight) {
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    info.sampleCount = samples;
    info.width = width;
    info.height = height;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    return info;
}

struct Target {
    XrSwapchain swapchain{XR_NULL_HANDLE};
    std::vector<ID3D11Texture2D*> images;
};

std::vector<ID3D11Texture2D*> Images(App& app, XrSwapchain swapchain) {
    std::uint32_t count = 0;
    const XrResult result = app.api.enumerate_images(swapchain, 0, &count, nullptr);
    Check(result == XR_SUCCESS && count == kImages,
          "xrEnumerateSwapchainImages counts the images: " + std::to_string(count));
    std::vector<XrSwapchainImageD3D11KHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    Check(app.api.enumerate_images(swapchain, count, &count,
                                   reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())) == XR_SUCCESS,
          "xrEnumerateSwapchainImages");
    std::vector<ID3D11Texture2D*> textures;
    for (const XrSwapchainImageD3D11KHR& image : images) {
        Check(image.texture != nullptr, "every image is a texture");
        textures.push_back(image.texture);
    }
    return textures;
}

Target MakeTarget(App& app, const XrSwapchainCreateInfo& info, const std::string& what) {
    Target target;
    const XrResult result = app.api.create_swapchain(app.session, &info, &target.swapchain);
    Check(result == XR_SUCCESS && target.swapchain != XR_NULL_HANDLE,
          what + ": xrCreateSwapchain " + std::to_string(result));
    target.images = Images(app, target.swapchain);
    return target;
}

std::uint32_t Acquire(App& app, const Target& target) {
    std::uint32_t index = 0;
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    Check(app.api.acquire(target.swapchain, &acquire, &index) == XR_SUCCESS && index < target.images.size(),
          "xrAcquireSwapchainImage");
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    Check(app.api.wait(target.swapchain, &wait) == XR_SUCCESS, "xrWaitSwapchainImage");
    return index;
}

void Release(App& app, const Target& target) {
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    Check(app.api.release(target.swapchain, &release) == XR_SUCCESS, "xrReleaseSwapchainImage");
}

void Draw(Game& game, ID3D11Texture2D* image, const Color& color) {
    D3D11_TEXTURE2D_DESC desc{};
    image->GetDesc(&desc);
    for (std::uint32_t slice = 0; slice < desc.ArraySize; ++slice) {
        Clear(game, image, color, slice);
    }
}
struct Eye {
    const Target* target{nullptr};
    std::uint32_t slice{0};
};
XrResult Frame(App& app, Game& game, const std::vector<const Target*>& targets, const std::vector<Color>& colors,
               const std::array<Eye, 2>& eyes) {
    XrFrameWaitInfo wait_info{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState state{XR_TYPE_FRAME_STATE};
    Check(app.api.wait_frame(app.session, &wait_info, &state) == XR_SUCCESS, "xrWaitFrame");
    XrFrameBeginInfo begin_info{XR_TYPE_FRAME_BEGIN_INFO};
    Check(app.api.begin_frame(app.session, &begin_info) == XR_SUCCESS, "xrBeginFrame");
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime = state.predictedDisplayTime;
    locate.space = app.space;
    XrViewState view_state{XR_TYPE_VIEW_STATE};
    std::array<XrView, 2> located{};
    for (XrView& view : located) view.type = XR_TYPE_VIEW;
    std::uint32_t located_count = 0;
    Check(app.api.locate_views(app.session, &locate, &view_state, 2, &located_count, located.data()) == XR_SUCCESS &&
              located_count == 2,
          "xrLocateViews");

    std::vector<std::uint32_t> indices;
    for (const Target* target : targets) {
        indices.push_back(Acquire(app, *target));
    }
    for (std::size_t i = 0; i < targets.size(); ++i) {
        Draw(game, targets[i]->images[indices[i]], colors[i]);
    }
    for (std::size_t i = 0; i < targets.size(); ++i) {
        Release(app, *targets[i]);
    }
    std::array<XrCompositionLayerProjectionView, 2> views{};
    for (std::uint32_t eye = 0; eye < 2; ++eye) {
        views[eye] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        views[eye].pose = located[eye].pose;
        views[eye].fov = located[eye].fov;
        views[eye].subImage.swapchain = eyes[eye].target->swapchain;
        views[eye].subImage.imageRect = {{0, 0},
                                         {static_cast<std::int32_t>(kWidth), static_cast<std::int32_t>(kHeight)}};
        views[eye].subImage.imageArrayIndex = eyes[eye].slice;
    }
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    projection.space = app.space;
    projection.viewCount = 2;
    projection.views = views.data();
    const XrCompositionLayerBaseHeader* layers[] = {reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection)};
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = state.predictedDisplayTime;
    end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount = 1;
    end.layers = layers;
    return app.api.end_frame(app.session, &end);
}
void IsolateHost() {
    wchar_t temp[MAX_PATH + 1]{};
    GetTempPathW(MAX_PATH, temp);
    g_root = std::filesystem::path(temp) /
             ("opengrid-lmu-external-tests-" + std::to_string(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(g_root, error);
    std::filesystem::create_directories(g_root);
    SetEnv("OPENGRIDXR_ENHANCEMENTS_CONFIG", (g_root / "settings.json").string());
    SetEnv("OPENGRIDXR_ENHANCEMENTS_LOG_PATH", (g_root / "logs" / "vectorxr.log").string());
    SetEnv("OPENGRIDXR_ENHANCEMENTS_SEEN_APPS_PATH", (g_root / "seen-apps.json").string());
    SetEnv("OPENGRIDXR_ENHANCEMENTS_RUNTIME_RELAY_PATH", (g_root / "runtime").string());
    SetEnv("OPENGRIDXR_ENHANCEMENTS_EXTERNAL_STATUS_PATH", (g_root / "api" / "status.json").string());
    SetEnv("OPENGRIDXR_ENHANCEMENTS_RUNTIME_PACING_PATH", (g_root / "runtime-pacing.json").string());
    SetEnv("OPENGRIDXR_ENHANCEMENTS_TURBO_METRICS_PATH", (g_root / "turbo-metrics.json").string());
    SetEnv("XR_RUNTIME_JSON", (g_root / "no-runtime.json").string());
    SetEnv("LOCALAPPDATA", (g_root / "appdata").string());
}

void BeginCase(const std::string& name, const Game& game) {
    g_runtime = StandIn{};
    g_runtime.luid = game.luid;
    SetEnv("OPENGRIDXR_ENHANCEMENTS_LOG_PATH", (g_root / "logs" / name / "vectorxr.log").string());
    std::ofstream settings(g_root / "settings.json");
    settings << R"({"version":3,"core":{"enabled":true,"trackSeenApps":false},"applications":[],"modules":{"depthxr":{"enabled":false},"pivotxr":{"enabled":false},"quadviews":{"enabled":false},"turbo":{"enabled":false}}})";
}
} // namespace
