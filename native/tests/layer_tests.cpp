#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include "channel.hpp"
#include "overlay_transport.hpp"
#include "settings.hpp"
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <openxr/openxr_platform.h>
#include <stdexcept>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
ComPtr<ID3D11Device> captureDevice;
ComPtr<ID3D11DeviceContext> captureContext;
ComPtr<ID3D11Texture2D> captureTexture;
ComPtr<ID3D11RenderTargetView> captureRtv;
struct FixtureImage {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> target;
};
std::map<XrSwapchain, FixtureImage> fixtureImages;
uint64_t nextSwapchain = 3;
uint32_t submittedLayers{};
XrSwapchain submittedOverlay{};
XrResult XRAPI_CALL fake_formats(XrSession, uint32_t capacity, uint32_t *count, int64_t *formats) {
    *count = 2;
    if (capacity >= 2) {
        formats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        formats[1] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL fake_swap_create(XrSession, const XrSwapchainCreateInfo *info,
                                     XrSwapchain *out) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = info->width;
    d.Height = info->height;
    d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT(info->format);
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    FixtureImage image;
    if (!captureDevice || FAILED(captureDevice->CreateTexture2D(&d, nullptr, &image.texture)) ||
        FAILED(captureDevice->CreateRenderTargetView(image.texture.Get(), nullptr, &image.target)))
        return XR_ERROR_RUNTIME_FAILURE;
    *out = reinterpret_cast<XrSwapchain>(nextSwapchain++);
    if (fixtureImages.empty()) {
        captureTexture = image.texture;
        captureRtv = image.target;
    }
    fixtureImages.emplace(*out, std::move(image));
    return XR_SUCCESS;
}
XrResult XRAPI_CALL fake_swap_destroy(XrSwapchain swap) {
    fixtureImages.erase(swap);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL fake_images(XrSwapchain swap, uint32_t capacity, uint32_t *count,
                                XrSwapchainImageBaseHeader *images) {
    *count = 1;
    if (capacity)
        reinterpret_cast<XrSwapchainImageD3D11KHR *>(images)[0].texture =
            fixtureImages.at(swap).texture.Get();
    return XR_SUCCESS;
}
XrResult XRAPI_CALL fake_acquire(XrSwapchain, const XrSwapchainImageAcquireInfo *,
                                 uint32_t *index) {
    *index = 0;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL fake_wait(XrSwapchain, const XrSwapchainImageWaitInfo *) { return XR_SUCCESS; }
XrResult XRAPI_CALL fake_release(XrSwapchain swap, const XrSwapchainImageReleaseInfo *) {
    float blue[]{0, 0, 1, 1};
    if (fixtureImages.at(swap).texture == captureTexture)
        captureContext->ClearRenderTargetView(captureRtv.Get(), blue);
    return XR_SUCCESS;
}
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
bool mutableFov = true;
bool realEngine = false;
XrResult XRAPI_CALL fake_unsupported() { return XR_ERROR_FUNCTION_UNSUPPORTED; }
XrResult XRAPI_CALL fake_create(const XrInstanceCreateInfo *, const XrApiLayerCreateInfo *,
                                XrInstance *out) {
    *out = reinterpret_cast<XrInstance>(1);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL fake_destroy(XrInstance) { return XR_SUCCESS; }
XrResult XRAPI_CALL fake_session(XrInstance, const XrSessionCreateInfo *, XrSession *out) {
    *out = reinterpret_cast<XrSession>(2);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL fake_session_destroy(XrSession) { return XR_SUCCESS; }
XrResult XRAPI_CALL fake_props(XrInstance, XrSystemId, XrViewConfigurationType,
                               XrViewConfigurationProperties *p) {
    p->fovMutable = mutableFov;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL fake_sizes(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t cap,
                               uint32_t *count, XrViewConfigurationView *v) {
    *count = 2;
    if (cap) {
        if (cap < 2)
            return XR_ERROR_SIZE_INSUFFICIENT;
        for (int i = 0; i < 2; ++i) {
            v[i].recommendedImageRectWidth = 2000;
            v[i].recommendedImageRectHeight = 1600;
            v[i].maxImageRectWidth = v[i].maxImageRectHeight = 4096;
        }
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL fake_locate(XrSession, const XrViewLocateInfo *, XrViewState *state,
                                uint32_t cap, uint32_t *count, XrView *v) {
    *count = 2;
    if (cap < 2)
        return XR_ERROR_SIZE_INSUFFICIENT;
    state->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
    for (int i = 0; i < 2; ++i) {
        v[i].fov = {-.7853981634f, .7853981634f, .7853981634f, -.7853981634f};
        v[i].pose.orientation.w = 1;
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL fake_mask(XrSession, XrViewConfigurationType, uint32_t, XrVisibilityMaskTypeKHR,
                              XrVisibilityMaskKHR *m) {
    if (!m || m->type != XR_TYPE_VISIBILITY_MASK_KHR)
        return XR_ERROR_VALIDATION_FAILURE;
    m->vertexCountOutput = m->indexCountOutput = 3;
    if (!m->vertexCapacityInput || !m->indexCapacityInput)
        return XR_SUCCESS;
    if (m->vertexCapacityInput < 3 || m->indexCapacityInput < 3)
        return XR_ERROR_SIZE_INSUFFICIENT;
    m->vertices[0] = {-1, -1};
    m->vertices[1] = {-.9f, -1};
    m->vertices[2] = {-1, -.9f};
    m->indices[0] = 0;
    m->indices[1] = 1;
    m->indices[2] = 2;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL fake_poll(XrInstance, XrEventDataBuffer *) { return XR_EVENT_UNAVAILABLE; }
XrResult XRAPI_CALL fake_system_props(XrInstance, XrSystemId, XrSystemProperties *props) {
    props->graphicsProperties.maxLayerCount = 4;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL fake_space(XrSession, const XrReferenceSpaceCreateInfo *info, XrSpace *out) {
    *out = reinterpret_cast<XrSpace>(uintptr_t(10 + info->referenceSpaceType));
    return XR_SUCCESS;
}
XrResult XRAPI_CALL fake_space_destroy(XrSpace) { return XR_SUCCESS; }
XrResult XRAPI_CALL fake_space_locate(XrSpace, XrSpace, XrTime, XrSpaceLocation *out) {
    out->pose.orientation.w = 1;
    out->locationFlags =
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL fake_end(XrSession, const XrFrameEndInfo *info) {
    submittedLayers = info->layerCount;
    submittedOverlay = XR_NULL_HANDLE;
    for (uint32_t i = 0; i < info->layerCount; ++i)
        if (info->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_QUAD)
            submittedOverlay = reinterpret_cast<const XrCompositionLayerQuad *>(info->layers[i])
                                   ->subImage.swapchain;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL fake_gipa(XrInstance, const char *n, PFN_xrVoidFunction *out) {
#define F(name, fn)                                                                                \
    if (!std::strcmp(n, "xr" #name)) {                                                             \
        *out = reinterpret_cast<PFN_xrVoidFunction>(fn);                                           \
        return XR_SUCCESS;                                                                         \
    }
    F(DestroyInstance, fake_destroy)
    F(CreateSession, fake_session)
    F(DestroySession, fake_session_destroy)
    F(GetViewConfigurationProperties, fake_props)
    F(EnumerateViewConfigurationViews, fake_sizes)
    F(LocateViews, fake_locate)
    F(GetVisibilityMaskKHR, fake_mask)
    F(PollEvent, fake_poll)
    F(EndFrame, fake_end)
    F(GetSystemProperties, fake_system_props)
    F(CreateReferenceSpace, fake_space)
    F(DestroySpace, fake_space_destroy)
    F(LocateSpace, fake_space_locate)
    F(EnumerateSwapchainFormats, fake_formats)
    F(CreateSwapchain, fake_swap_create)
    F(DestroySwapchain, fake_swap_destroy)
    F(EnumerateSwapchainImages, fake_images)
    F(AcquireSwapchainImage, fake_acquire)
    F(WaitSwapchainImage, fake_wait)
    F(ReleaseSwapchainImage, fake_release)
#undef F
    *out = realEngine ? reinterpret_cast<PFN_xrVoidFunction>(fake_unsupported) : nullptr;
    return realEngine ? XR_SUCCESS : XR_ERROR_FUNCTION_UNSUPPORTED;
}
template <class T> T get(const XrNegotiateApiLayerRequest &r, XrInstance i, const char *name) {
    T fn{};
    check(r.getInstanceProcAddr(i, name, reinterpret_cast<PFN_xrVoidFunction *>(&fn)) == XR_SUCCESS,
          "intercepted proc resolves");
    return fn;
}
int wmain(int argc, wchar_t **argv) {
    try {
        wchar_t exe[32768];
        GetModuleFileNameW(nullptr, exe, 32768);
        auto dir = std::filesystem::path(exe).parent_path();
        auto folder = dir / (L"test-state-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(folder);
        auto config = folder / L"OpenGridXR.ini";
        bool withEngine = argc == 3;
        bool engineEnabled = withEngine && std::wstring(argv[1]) != L"--engine-disabled";
        realEngine = withEngine && std::wstring(argv[1]) == L"--real-engine";
        bool fixtureActive = engineEnabled && !realEngine;
        std::filesystem::copy_file(dir / L"OpenGridXR.dll", folder / L"OpenGridXR.dll",
                                   std::filesystem::copy_options::overwrite_existing);
        if (withEngine)
            std::filesystem::copy_file(argv[2], folder / L"OpenGridXR-Enhancements.dll",
                                       std::filesystem::copy_options::overwrite_existing);
        {
            std::ofstream out(config);
            out << "[iracing]\nEnabled=1\nEnhancementsEnabled="
                << ((!withEngine || engineEnabled) ? 1 : 0)
                << "\nMode=driving\nCropLeft=10\nCropRight="
                   "20\nCropTop=5\nCropBottom=15\nMaskEnabled=1\nMaskLeft=0.2,0.2;0."
                   "8,0.2;0.8,0.8;0.2,0.8\nMaskRight=0.2,0.2;0.8,0.2;0.8,0.8;0.2,0."
                   "8\n";
        }
        if (realEngine) {
            std::ifstream input(std::filesystem::path(__FILE__).parent_path() /
                                "../third_party/vectorxr/layer/tests/opengrid-config-fixture.json");
            std::string body((std::istreambuf_iterator<char>(input)), {});
            const std::string enabled = "\"enabled\": true";
            size_t at = 0;
            while ((at = body.find(enabled, at)) != std::string::npos) {
                body.replace(at, enabled.size(), "\"enabled\": false");
                at += 16;
            }
            auto core = body.find("\"core\": {");
            check(core != std::string::npos, "fixture core exists");
            auto coreEnabled = body.find("\"enabled\": false", core);
            body.replace(coreEnabled, 16, "\"enabled\": true");
            body.insert(core + 9, "\n    \"overlays\": {\"enabled\": true},");
            std::ofstream output(folder / L"OpenGridXR-enhancements-iracing.json");
            output << body;
        }
        WritePrivateProfileStringW(L"iracing", L"SharpeningMode", L"luma", config.c_str());
        WritePrivateProfileStringW(L"iracing", L"SharpeningStrength", L"120", config.c_str());
        WritePrivateProfileStringW(L"iracing", L"LumaRadius", L"NaN", config.c_str());
        WritePrivateProfileStringW(L"iracing", L"HelmetVisorEnabled", L"1", config.c_str());
        WritePrivateProfileStringW(L"iracing", L"HelmetVisorContrast", L"999", config.c_str());
        WritePrivateProfileStringW(L"iracing", L"HelmetVisorBrightness", L"NaN", config.c_str());
        WritePrivateProfileStringW(L"iracing", L"HelmetVisorSaturation", L"-1", config.c_str());
        auto parsed = og::read_config(config.wstring(), "iracing");
        check(parsed.sharpening.mode == og::SharpeningMode::Luma &&
                  parsed.sharpening.strength == 100 && parsed.sharpening.radius == 1,
              "sharpen settings clamp finite bounds and default invalid values");
        check(parsed.visorColor.enabled&&parsed.visorColor.contrast==150&&parsed.visorColor.brightness==100&&parsed.visorColor.saturation==0,
              "visor settings clamp finite bounds and default invalid values");
        SetEnvironmentVariableW(L"OPENGRIDXR_CONFIG", config.c_str());
        auto module = LoadLibraryW((folder / L"OpenGridXR.dll").c_str());
        check(module != nullptr, "built DLL loads");
        auto negotiate = reinterpret_cast<PFN_xrNegotiateLoaderApiLayerInterface>(
            GetProcAddress(module, "xrNegotiateLoaderApiLayerInterface"));
        check(negotiate != nullptr, "loader negotiation exported");
        XrNegotiateLoaderInfo loader{XR_LOADER_INTERFACE_STRUCT_LOADER_INFO,
                                     XR_LOADER_INFO_STRUCT_VERSION,
                                     sizeof(XrNegotiateLoaderInfo),
                                     1,
                                     1,
                                     XR_MAKE_VERSION(1, 0, 0),
                                     XR_CURRENT_API_VERSION};
        XrNegotiateApiLayerRequest request{XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST,
                                           XR_API_LAYER_INFO_STRUCT_VERSION,
                                           sizeof(XrNegotiateApiLayerRequest)};
        check(negotiate(&loader, "wrong_name", &request) == XR_ERROR_INITIALIZATION_FAILED,
              "reject wrong layer name");
        check(negotiate(&loader, "XR_APILAYER_OPENGRIDXR", &request) == XR_SUCCESS,
              "loader negotiation succeeds");
        XrApiLayerNextInfo next{XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO,
                                XR_API_LAYER_NEXT_INFO_STRUCT_VERSION, sizeof(XrApiLayerNextInfo)};
        next.nextGetInstanceProcAddr = fake_gipa;
        next.nextCreateApiLayerInstance = fake_create;
        XrApiLayerCreateInfo layer{XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO,
                                   XR_API_LAYER_CREATE_INFO_STRUCT_VERSION,
                                   sizeof(XrApiLayerCreateInfo)};
        layer.nextInfo = &next;
        const char *extension = "XR_KHR_visibility_mask";
        XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
        create.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
        create.enabledExtensionCount = 1;
        create.enabledExtensionNames = &extension;
        for (int iteration = 0; iteration < 2; ++iteration) {
            mutableFov = iteration == 0;
            XrInstance instance{};
            check(request.createApiLayerInstance(&create, &layer, &instance) == XR_SUCCESS,
                  "create via real layer");
            auto sizes = get<PFN_xrEnumerateViewConfigurationViews>(
                request, instance, "xrEnumerateViewConfigurationViews");
            XrViewConfigurationView views[2]{{XR_TYPE_VIEW_CONFIGURATION_VIEW},
                                             {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
            uint32_t count{};
            check(sizes(instance, 1, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &count, views) ==
                      XR_SUCCESS,
                  "recommendation succeeds");
            check(views[0].recommendedImageRectWidth ==
                      (fixtureActive ? (mutableFov ? 1540u : 2200u) : (mutableFov ? 1400u : 2000u)),
                  "real layer crop respects mutable FOV");
            check(sizes(instance, 1, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO, 2, &count, views) ==
                          XR_SUCCESS &&
                      views[0].recommendedImageRectWidth == 2000,
                  "unsupported view mode passes through");
            XrSession session{};
            XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
            sci.systemId = 1;
            check(get<PFN_xrCreateSession>(request, instance, "xrCreateSession")(
                      instance, &sci, &session) == XR_SUCCESS,
                  "session creation");
            auto mask =
                get<PFN_xrGetVisibilityMaskKHR>(request, instance, "xrGetVisibilityMaskKHR");
            XrVisibilityMaskKHR m{XR_TYPE_VISIBILITY_MASK_KHR};
            check(mask(session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                       XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR, &m) == XR_SUCCESS &&
                      m.vertexCountOutput == 3,
                  "mask before first locate preserves runtime");
            XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
            li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            XrViewState state{XR_TYPE_VIEW_STATE};
            XrView located[2]{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
            check(get<PFN_xrLocateViews>(request, instance, "xrLocateViews")(
                      session, &li, &state, 2, &count, located) == XR_SUCCESS,
                  "locate succeeds");
            check(std::abs(std::tan(located[0].fov.angleLeft) - (mutableFov ? -.8 : -1)) < 1e-5,
                  "actual layer tangent crop");
            m = {XR_TYPE_VISIBILITY_MASK_KHR};
            check(mask(session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                       XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR, &m) == XR_SUCCESS &&
                      m.vertexCountOutput > 3,
                  "merged mask reports larger buffers");
            std::vector<XrVector2f> vertices(m.vertexCountOutput);
            std::vector<uint32_t> indices(m.indexCountOutput);
            m.vertexCapacityInput = uint32_t(vertices.size());
            m.indexCapacityInput = uint32_t(indices.size());
            m.vertices = vertices.data();
            m.indices = indices.data();
            check(mask(session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                       XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR, &m) == XR_SUCCESS,
                  "merged mask fills");
            check(vertices[0].x == -1 && vertices[1].x == -.9f && indices[2] == 2,
                  "native hidden triangles preserved exactly");
            m.vertexCapacityInput = 1;
            check(mask(session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                       XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR,
                       &m) == XR_ERROR_SIZE_INSUFFICIENT,
                  "insufficient custom mask buffer rejected");
            m = {XR_TYPE_VISIBILITY_MASK_KHR};
            check(mask(session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                       XR_VISIBILITY_MASK_TYPE_LINE_LOOP_KHR, &m) == XR_SUCCESS &&
                      m.vertexCountOutput == 3,
                  "unsupported mask kind passes through");
            m.type = XR_TYPE_UNKNOWN;
            check(mask(session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                       XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR,
                       &m) == XR_ERROR_VALIDATION_FAILURE,
                  "invalid mask type remains rejected");
            check(get<PFN_xrDestroySession>(request, instance, "xrDestroySession")(session) ==
                      XR_SUCCESS,
                  "destroy session");
            check(get<PFN_xrDestroyInstance>(request, instance, "xrDestroyInstance")(instance) ==
                      XR_SUCCESS,
                  "destroy instance");
        }
        if (realEngine) {
            WritePrivateProfileStringW(L"iracing", L"Mode", L"streaming", config.c_str());
            WritePrivateProfileStringW(L"iracing", L"SharpeningMode", L"off", config.c_str());
            check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                              nullptr, 0, D3D11_SDK_VERSION, &captureDevice,
                                              nullptr, &captureContext)),
                  "private chain capture device");
            XrInstance instance{};
            check(request.createApiLayerInstance(&create, &layer, &instance) == XR_SUCCESS,
                  "private capture instance");
            XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
            binding.device = captureDevice.Get();
            XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
            sci.next = &binding;
            sci.systemId = 1;
            XrSession session{};
            check(get<PFN_xrCreateSession>(request, instance, "xrCreateSession")(
                      instance, &sci, &session) == XR_SUCCESS,
                  "private capture session");
            og::Channel channel;
            check(channel.open("iracing") && channel.lock(), "private capture channel");
            channel.data()->consumer = GetCurrentProcessId();
            channel.data()->consumerBeat = GetTickCount64();
            channel.unlock();
            XrSwapchainCreateInfo swapInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            swapInfo.width = swapInfo.height = 64;
            swapInfo.arraySize = swapInfo.faceCount = swapInfo.mipCount = swapInfo.sampleCount = 1;
            swapInfo.format = DXGI_FORMAT_R8G8B8A8_UNORM;
            swapInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
            XrSwapchain swap{};
            check(get<PFN_xrCreateSwapchain>(request, instance, "xrCreateSwapchain")(
                      session, &swapInfo, &swap) == XR_SUCCESS,
                  "private capture swapchain");
            uint32_t index{};
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wait.timeout = XR_INFINITE_DURATION;
            check(get<PFN_xrAcquireSwapchainImage>(request, instance, "xrAcquireSwapchainImage")(
                      swap, &acquire, &index) == XR_SUCCESS,
                  "private acquired image");
            check(get<PFN_xrWaitSwapchainImage>(request, instance,
                                                "xrWaitSwapchainImage")(swap, &wait) == XR_SUCCESS,
                  "private waited image");
            float red[]{1, 0, 0, 1};
            captureContext->ClearRenderTargetView(captureRtv.Get(), red);
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            check(get<PFN_xrReleaseSwapchainImage>(request, instance, "xrReleaseSwapchainImage")(
                      swap, &release) == XR_SUCCESS,
                  "private release snapshots before runtime reuse");
            XrCompositionLayerProjectionView views[2]{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                                      {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
            for (auto &view : views) {
                view.subImage.swapchain = swap;
                view.subImage.imageRect = {{0, 0}, {64, 64}};
                view.pose.orientation.w = 1;
                view.fov = {-.7f, .7f, .7f, -.7f};
            }
            XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
            projection.viewCount = 2;
            projection.views = views;
            auto *header = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&projection);
            XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
            end.displayTime = 1;
            end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            end.layerCount = 1;
            end.layers = &header;
            check(get<PFN_xrEndFrame>(request, instance, "xrEndFrame")(session, &end) == XR_SUCCESS,
                  "private capture submit");
            check(channel.lock(), "private capture metadata");
            auto frame = *channel.data();
            channel.unlock();
            check(frame.ready && frame.width == 64, "real engine chain produced source frame");
            ComPtr<ID3D11Texture2D> shared, staging;
            ComPtr<IDXGIKeyedMutex> key;
            check(SUCCEEDED(captureDevice->OpenSharedResource(
                      reinterpret_cast<HANDLE>(frame.texture), IID_PPV_ARGS(&shared))),
                  "private shared image");
            shared.As(&key);
            check(key->AcquireSync(1, 1000) == S_OK, "private shared ownership");
            D3D11_TEXTURE2D_DESC desc{};
            shared->GetDesc(&desc);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = desc.MiscFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            check(SUCCEEDED(captureDevice->CreateTexture2D(&desc, nullptr, &staging)),
                  "private readback texture");
            captureContext->CopyResource(staging.Get(), shared.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            check(SUCCEEDED(captureContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
                  "private readback");
            auto pixel = *static_cast<const uint32_t *>(mapped.pData);
            captureContext->Unmap(staging.Get(), 0);
            key->ReleaseSync(0);
            check(pixel == 0xff0000ff,
                  "raw red pixels survive private engine runtime release blue overwrite");
            // Exercise the production private engine's downstream submission hook, not just
            // the standalone renderer. The game projection and raw capture remain intact.
            og::OverlayChannel overlays;
            check(overlays.open("iracing") && overlays.lock(1000), "private overlay channel");
            check(overlays.data()->consumerPid == GetCurrentProcessId() &&
                      overlays.data()->capacity == 3,
                  "private engine registered actual GPU receiver and layer budget");
            overlays.unlock();
            ComPtr<ID3D11Texture2D> overlaySource;
            ComPtr<IDXGIResource1> overlayResource;
            ComPtr<IDXGIKeyedMutex> overlayKey;
            HANDLE overlayHandle{};
            D3D11_TEXTURE2D_DESC overlayDesc{};
            overlayDesc.Width = 32;
            overlayDesc.Height = 16;
            overlayDesc.ArraySize = overlayDesc.MipLevels = overlayDesc.SampleDesc.Count = 1;
            overlayDesc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
            overlayDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            overlayDesc.MiscFlags =
                D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
            std::vector<uint32_t> green(32 * 16, 0xff00ff00);
            D3D11_SUBRESOURCE_DATA overlayPixels{green.data(), 32 * 4, 0};
            check(SUCCEEDED(captureDevice->CreateTexture2D(&overlayDesc, &overlayPixels,
                                                           &overlaySource)) &&
                      SUCCEEDED(overlaySource.As(&overlayResource)) &&
                      SUCCEEDED(overlaySource.As(&overlayKey)) &&
                      SUCCEEDED(overlayResource->CreateSharedHandle(
                          nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                          &overlayHandle)),
                  "private GPU overlay source");
            check(overlayKey->AcquireSync(0, 1000) == S_OK && SUCCEEDED(overlayKey->ReleaseSync(1)),
                  "private overlay producer ownership");
            check(overlays.lock(1000), "private overlay publish");
            overlays.data()->producerPid = GetCurrentProcessId();
            overlays.data()->producerBeat = GetTickCount64();
            overlays.data()->slots = {};
            auto &panel = overlays.data()->slots[0];
            panel.enabled = 1;
            panel.showInStream = 1;
            panel.sourceRevision = 1;
            panel.frames[0] = {1,  reinterpret_cast<uint64_t>(overlayHandle),
                               1,  32,
                               16, DXGI_FORMAT_R8G8B8A8_UNORM};
            overlays.unlock();
            for (unsigned frameIndex = 0; frameIndex < 3; ++frameIndex) {
                ++end.displayTime;
                check(get<PFN_xrEndFrame>(request, instance, "xrEndFrame")(session, &end) ==
                          XR_SUCCESS,
                      "private overlay runtime submission");
                check(submittedLayers == 2 && submittedOverlay != XR_NULL_HANDLE,
                      "private engine appends quad after application projection");
            }
            auto overlayOutput = fixtureImages.at(submittedOverlay).texture;
            D3D11_TEXTURE2D_DESC readDesc{};
            overlayOutput->GetDesc(&readDesc);
            readDesc.Usage = D3D11_USAGE_STAGING;
            readDesc.BindFlags = readDesc.MiscFlags = 0;
            readDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> overlayRead;
            check(SUCCEEDED(captureDevice->CreateTexture2D(&readDesc, nullptr, &overlayRead)),
                  "private quad readback allocation");
            captureContext->CopyResource(overlayRead.Get(), overlayOutput.Get());
            check(SUCCEEDED(captureContext->Map(overlayRead.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
                  "private quad readback");
            pixel = *static_cast<const uint32_t *>(mapped.pData);
            captureContext->Unmap(overlayRead.Get(), 0);
            check(pixel == 0xff00ff00, "private engine quad contains actual GPU source pixels");
            check(channel.lock(), "private captured geometry request");
            channel.data()->ready = 0;
            channel.unlock();
            Sleep(20);
            ++end.displayTime;
            check(get<PFN_xrEndFrame>(request, instance, "xrEndFrame")(session, &end) == XR_SUCCESS,
                  "private capture of resolved VR placement");
            check(channel.lock(), "private captured geometry snapshot");
            const auto spatialFrame = *channel.data();
            channel.unlock();
            check(
                spatialFrame.overlays[0].valid && spatialFrame.overlays[0].sourceRevision == 1 &&
                    spatialFrame.overlays[0].pixelWidth == 32 && spatialFrame.overlaySession != 0 &&
                    spatialFrame.overlayConsumerPid == GetCurrentProcessId() &&
                    spatialFrame.overlayProducerPid == GetCurrentProcessId() &&
                    spatialFrame.displayTime == end.displayTime &&
                    std::abs(spatialFrame.overlays[0].corners[0][0] + .5f) < 1e-5 &&
                    std::abs(spatialFrame.overlays[0].corners[0][1] - .25f) < 1e-5 &&
                    std::abs(spatialFrame.overlays[0].corners[0][2] + 1.5f) < 1e-5,
                "private chain atomically captures real resolved VR quad geometry with raw image");
            check(overlays.lock(1000), "private overlay stop");
            overlays.data()->producerPid = 0;
            overlays.unlock();
            ++end.displayTime;
            check(get<PFN_xrEndFrame>(request, instance, "xrEndFrame")(session, &end) ==
                          XR_SUCCESS &&
                      submittedLayers == 1 && fixtureImages.size() == 1,
                  "private producer stop retires overlay resources and preserves game projection");
            CloseHandle(overlayHandle);
            get<PFN_xrDestroySwapchain>(request, instance, "xrDestroySwapchain")(swap);
            get<PFN_xrDestroySession>(request, instance, "xrDestroySession")(session);
            get<PFN_xrDestroyInstance>(request, instance, "xrDestroyInstance")(instance);
        }
        FreeLibrary(module);
        std::ifstream status(folder / L"OpenGridXR-status-iracing.json");
        std::string text((std::istreambuf_iterator<char>(status)),
                         std::istreambuf_iterator<char>());
        check(text.find(engineEnabled ? "\"engineLoaded\":true" : "\"engineLoaded\":false") !=
                  std::string::npos,
              "private engine loaded state reported honestly");
        std::cout << "Built DLL synthetic OpenXR negotiation, crop, mask merge, "
                     "and pass-through passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
