#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include "channel.hpp"
#include "overlay_geometry.hpp"
#include "settings.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <d3d11_4.h>
#include <deque>
#include <dxgi.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <openxr/openxr_platform.h>
#include <unordered_map>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace {
constexpr char layerName[] = "XR_APILAYER_OPENGRIDXR";
struct Instance {
    XrInstance handle{};
    PFN_xrGetInstanceProcAddr gipa{};
    og::Config config;
    std::wstring path;
    std::string game;
    bool engineLoaded{};
    std::string engineError;
    bool visibilityEnabled{};
#define FN(n) PFN_xr##n n{};
    FN(DestroyInstance)
    FN(CreateSession)
    FN(DestroySession)
    FN(EnumerateViewConfigurationViews)
    FN(GetViewConfigurationProperties)
    FN(LocateViews)
    FN(LocateSpace)
    FN(GetVisibilityMaskKHR)
    FN(PollEvent)
    FN(CreateSwapchain)
    FN(DestroySwapchain)
    FN(EnumerateSwapchainImages)
    FN(AcquireSwapchainImage)
    FN(WaitSwapchainImage)
    FN(ReleaseSwapchainImage)
    FN(EndFrame)
#undef FN
};
uint64_t nextSessionId = GetTickCount64() << 16;
struct Session {
    Instance *instance{};
    XrSession handle{};
    uint64_t identity{++nextSessionId};
    bool mutableFov{}, haveFov{};
    uint32_t maskCalls{}, maskAppliedCalls{};
    uint64_t sharpenAppliedCalls{};
    og::ImageProcessStatus sharpenStatus{og::ImageProcessStatus::Skipped};
    std::unique_ptr<og::ImageProcessor> processor;
    unsigned pendingMaskEvents{};
    uint64_t frames{}, capturedFrames{}, lastStatus{}, lastCapture{}, generation{};
    uint32_t captureWidth{}, captureHeight{};
    og::Projection fov[2];
    og::Projection originalFov[2];
    og::Mesh masks[2];
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> sharedTexture;
    ComPtr<IDXGIKeyedMutex> sharedKey;
    HANDLE sharedHandle{};
    LUID adapter{};
    og::Channel channel;
    og::OverlayChannel overlays;
    bool overlaysAvailable{};
};
struct Swapchain {
    Session *session{};
    XrSwapchainCreateInfo info{};
    std::vector<XrSwapchainImageD3D11KHR> images;
    std::deque<std::pair<uint32_t, bool>> acquired;
    ComPtr<ID3D11Texture2D> snapshot;
    std::vector<og::ImageRegion> projectionRegions;
    bool valid{};
};
std::recursive_mutex guard;
std::unordered_map<XrInstance, std::unique_ptr<Instance>> instances;
std::unordered_map<XrSession, std::unique_ptr<Session>> sessions;
std::unordered_map<XrSwapchain, std::unique_ptr<Swapchain>> chains;
PFN_xrGetInstanceProcAddr fallback{};
#define LOCK std::lock_guard<std::recursive_mutex> lock(guard)
bool mutable_fov(Instance &i, XrSystemId sys) {
    XrViewConfigurationProperties props{XR_TYPE_VIEW_CONFIGURATION_PROPERTIES};
    return i.GetViewConfigurationProperties &&
           XR_SUCCEEDED(i.GetViewConfigurationProperties(
               i.handle, sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, &props)) &&
           props.fovMutable;
}
og::Projection projection(XrFovf f) {
    return {std::tan(f.angleLeft), std::tan(f.angleRight), std::tan(f.angleUp),
            std::tan(f.angleDown)};
}
XrFovf angles(og::Projection p) {
    return {float(std::atan(p.left)), float(std::atan(p.right)), float(std::atan(p.up)),
            float(std::atan(p.down))};
}
void status(Session &s, bool active) {
    if (s.instance->game.empty())
        return;
    auto now = GetTickCount64();
    if (active && now - s.lastStatus < 2000)
        return;
    s.lastStatus = now;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t ms =
        ((uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime) / 10000 - 11644473600000ULL;
    auto path = std::filesystem::path(s.instance->path).parent_path() /
                (L"OpenGridXR-status-" +
                 std::wstring(s.instance->game.begin(), s.instance->game.end()) + L".json");
    auto temp = path;
    temp += L".tmp";
    std::ofstream out(temp);
    if (!out)
        return;
    out << "{\"pid\":" << GetCurrentProcessId() << ",\"updatedUnixMs\":" << ms
        << ",\"active\":" << (active ? "true" : "false") << ",\"game\":\"" << s.instance->game
        << "\",\"mode\":\"" << (s.instance->config.streaming ? "streaming" : "driving")
        << "\",\"mutableFov\":" << (s.mutableFov ? "true" : "false")
        << ",\"captureSupported\":" << (s.device ? "true" : "false")
        << ",\"engineLoaded\":" << (s.instance->engineLoaded ? "true" : "false")
        << ",\"engineError\":\"" << s.instance->engineError << "\""
        << ",\"sharpenAppliedCalls\":" << s.sharpenAppliedCalls
        << ",\"sharpenStatus\":" << int(s.sharpenStatus) << ",\"maskCalls\":" << s.maskCalls
        << ",\"frames\":" << s.frames << ",\"maskAppliedCalls\":" << s.maskAppliedCalls
        << ",\"capturedFrames\":" << s.capturedFrames << ",\"captureWidth\":" << s.captureWidth
        << ",\"captureHeight\":" << s.captureHeight
        << ",\"configuredFps\":" << s.instance->config.fps
        << ",\"config\":{\"enabled\":" << (s.instance->config.enabled ? "true" : "false")
        << ",\"renderScale\":" << s.instance->config.renderScale
        << ",\"maskEnabled\":" << (s.instance->config.mask ? "true" : "false")
        << ",\"streamEye\":" << s.instance->config.eye
        << ",\"cropLeft\":" << s.instance->config.crop.left
        << ",\"cropRight\":" << s.instance->config.crop.right
        << ",\"cropTop\":" << s.instance->config.crop.top
        << ",\"cropBottom\":" << s.instance->config.crop.bottom
        << ",\"extraLeft\":" << s.instance->config.extraLeft
        << ",\"extraRight\":" << s.instance->config.extraRight << "}"
        << ",\"message\":\""
        << (active ? "OpenXR frames observed; custom aperture supports hidden "
                     "triangle masks. Headset validation required."
                   : "Session ended.")
        << "\"}";
    out.close();
    MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}
bool viewer(Session &s) {
    if (!s.instance->config.streaming || !s.channel.lock())
        return false;
    auto *f = s.channel.data();
    bool active = f->consumer && GetTickCount64() - f->consumerBeat < 3000;
    s.channel.unlock();
    return active;
}
void snapshot(Swapchain &c, uint32_t index) {
    c.valid = false;
    auto &s = *c.session;
    if (!s.device || index >= c.images.size() || !viewer(s) ||
        (c.info.createFlags & XR_SWAPCHAIN_CREATE_PROTECTED_CONTENT_BIT))
        return;
    auto *source = c.images[index].texture;
    if (!source)
        return;
    D3D11_TEXTURE2D_DESC d{};
    source->GetDesc(&d);
    const auto typedFormat = static_cast<DXGI_FORMAT>(c.info.format);
    if (!c.snapshot) {
        auto sd = d;
        sd.Format = typedFormat;
        sd.SampleDesc = {1, 0};
        sd.Usage = D3D11_USAGE_DEFAULT;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = 0;
        sd.MiscFlags = 0;
        sd.MipLevels = 1;
        if (FAILED(s.device->CreateTexture2D(&sd, nullptr, &c.snapshot)))
            return;
    }
    if (d.SampleDesc.Count > 1) {
        UINT support{};
        if (FAILED(s.device->CheckFormatSupport(typedFormat, &support)) ||
            !(support & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE))
            return;
        for (UINT slice = 0; slice < d.ArraySize; ++slice)
            s.context->ResolveSubresource(c.snapshot.Get(), slice, source,
                                          D3D11CalcSubresource(0, slice, d.MipLevels), typedFormat);
    } else {
        for (UINT slice = 0; slice < d.ArraySize; ++slice)
            s.context->CopySubresourceRegion(c.snapshot.Get(), slice, 0, 0, 0, source,
                                             D3D11CalcSubresource(0, slice, d.MipLevels), nullptr);
    }
    c.valid = true;
}
void publish(Session &s, const XrFrameEndInfo *end) {
    if (!s.device || !viewer(s))
        return;
    auto now = GetTickCount64();
    if (now - s.lastCapture < 1000 / std::max(30u, s.instance->config.fps))
        return;
    const XrCompositionLayerProjection *p = nullptr;
    for (uint32_t i = 0; i < end->layerCount; ++i)
        if (end->layers[i] && end->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            p = reinterpret_cast<const XrCompositionLayerProjection *>(end->layers[i]);
            break;
        }
    if (!p || p->viewCount != 2 || !p->views)
        return;
    unsigned eye = s.instance->config.eye;
    auto &v = p->views[eye];
    auto it = chains.find(v.subImage.swapchain);
    if (it == chains.end() || it->second->session != &s || !it->second->valid)
        return;
    auto &c = *it->second;
    auto r = v.subImage.imageRect;
    if (r.offset.x < 0 || r.offset.y < 0 || r.extent.width <= 0 || r.extent.height <= 0 ||
        uint64_t(r.offset.x) + r.extent.width > c.info.width ||
        uint64_t(r.offset.y) + r.extent.height > c.info.height ||
        v.subImage.imageArrayIndex >= c.info.arraySize)
        return;
    if (!s.channel.lock())
        return;
    auto *f = s.channel.data();
    if (f->ready && now - f->published < 1000) {
        s.channel.unlock();
        return;
    }
    if (f->ready) {
        s.sharedTexture.Reset();
        s.sharedKey.Reset();
        f->ready = 0;
    }
    D3D11_TEXTURE2D_DESC d{};
    c.snapshot->GetDesc(&d);
    D3D11_TEXTURE2D_DESC old{};
    if (s.sharedTexture)
        s.sharedTexture->GetDesc(&old);
    if (!s.sharedTexture || old.Width != UINT(r.extent.width) ||
        old.Height != UINT(r.extent.height) || old.Format != d.Format) {
        s.sharedKey.Reset();
        s.sharedTexture.Reset();
        s.sharedHandle = nullptr;
        ++s.generation;
        d.Width = r.extent.width;
        d.Height = r.extent.height;
        d.ArraySize = 1;
        d.MipLevels = 1;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        d.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        if (FAILED(s.device->CreateTexture2D(&d, nullptr, &s.sharedTexture)) ||
            FAILED(s.sharedTexture.As(&s.sharedKey))) {
            s.channel.unlock();
            return;
        }
        ComPtr<IDXGIResource> resource;
        if (FAILED(s.sharedTexture.As(&resource)) ||
            FAILED(resource->GetSharedHandle(&s.sharedHandle))) {
            s.channel.unlock();
            return;
        }
    }
    if (s.sharedKey->AcquireSync(0, 0) != S_OK) {
        s.channel.unlock();
        return;
    }
    D3D11_BOX box{UINT(r.offset.x),
                  UINT(r.offset.y),
                  0,
                  UINT(r.offset.x + r.extent.width),
                  UINT(r.offset.y + r.extent.height),
                  1};
    s.context->CopySubresourceRegion(s.sharedTexture.Get(), 0, 0, 0, 0, c.snapshot.Get(),
                                     v.subImage.imageArrayIndex, &box);
    s.context->Flush();
    s.sharedKey->ReleaseSync(1);
    f->producer = GetCurrentProcessId();
    f->published = now;
    f->sequence = ++s.capturedFrames;
    f->generation = s.generation;
    f->texture = reinterpret_cast<uint64_t>(s.sharedHandle);
    f->session = s.identity;
    f->reference = reinterpret_cast<uint64_t>(p->space);
    f->displayTime = end->displayTime;
    f->captureEye = eye;
    f->position[0] = v.pose.position.x;
    f->position[1] = v.pose.position.y;
    f->position[2] = v.pose.position.z;
    f->overlays = {};
    f->overlaySession = 0;
    f->overlayConsumerPid = 0;
    f->overlayProducerPid = 0;
    if (s.overlaysAvailable && s.instance->LocateSpace && s.overlays.lock()) {
        auto state = *s.overlays.data();
        s.overlays.unlock();
        if (state.consumerPid == GetCurrentProcessId() && now - state.consumerBeat < 3000 &&
            state.producerPid && now - state.producerBeat < 3000) {
            f->overlaySession = state.session;
            f->overlayConsumerPid = state.consumerPid;
            f->overlayProducerPid = state.producerPid;
            for (unsigned index = 0; index < og::overlay_count; ++index) {
                const auto &slot = state.slots[index];
                const auto &pose = slot.pose;
                const auto &pixels = slot.frames[slot.current < 2 ? slot.current : 0];
                // Each texture in one source's ring has its own resource generation.
                // Geometry survives normal ring rotation; source/config identity and
                // dimensions determine whether the resolved placement still applies.
                if (!slot.enabled || !slot.showInStream || !pose.valid ||
                    pose.sourceRevision != slot.sourceRevision ||
                    pose.configRevision != slot.revision || pose.recenter != slot.recenter ||
                    !pixels.generation || !pixels.width || !pixels.height ||
                    pose.pixelWidth != pixels.width || pose.pixelHeight != pixels.height)
                    continue;
                XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
                const auto flags =
                    XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
                if (XR_SUCCEEDED(s.instance->LocateSpace(reinterpret_cast<XrSpace>(pose.space),
                                                         p->space, end->displayTime, &location)) &&
                    (location.locationFlags & flags) == flags)
                    og::capture_overlay_corners(pose, location.pose, v.pose, f->overlays[index]);
            }
        }
    }
    f->width = r.extent.width;
    f->height = r.extent.height;
    s.captureWidth = f->width;
    s.captureHeight = f->height;
    f->format = d.Format;
    f->adapter = s.adapter;
    auto q = v.pose.orientation;
    f->orientation[0] = q.x;
    f->orientation[1] = q.y;
    f->orientation[2] = q.z;
    f->orientation[3] = q.w;
    auto proj = projection(v.fov);
    f->fov[0] = float(proj.left);
    f->fov[1] = float(proj.right);
    f->fov[2] = float(proj.up);
    f->fov[3] = float(proj.down);
    f->ready = 1;
    s.lastCapture = now;
    s.channel.unlock();
}
XrResult XRAPI_CALL destroy_instance(XrInstance h) {
    LOCK;
    auto it = instances.find(h);
    if (it == instances.end())
        return XR_ERROR_HANDLE_INVALID;
    auto result = it->second->DestroyInstance(h);
    if (XR_SUCCEEDED(result)) {
        for (auto s = sessions.begin(); s != sessions.end();)
            if (s->second->instance == it->second.get()) {
                status(*s->second, false);
                for (auto c = chains.begin(); c != chains.end();)
                    if (c->second->session == s->second.get())
                        c = chains.erase(c);
                    else
                        ++c;
                s = sessions.erase(s);
            } else
                ++s;
        instances.erase(it);
    }
    return result;
}
XrResult XRAPI_CALL create_session(XrInstance h, const XrSessionCreateInfo *info, XrSession *out) {
    LOCK;
    auto it = instances.find(h);
    if (it == instances.end())
        return XR_ERROR_HANDLE_INVALID;
    auto &i = *it->second;
    auto result = i.CreateSession(h, info, out);
    if (XR_FAILED(result))
        return result;
    auto s = std::make_unique<Session>();
    s->instance = &i;
    s->handle = *out;
    s->mutableFov = mutable_fov(i, info->systemId);
    if (i.config.enabled) {
        for (auto *n = reinterpret_cast<const XrBaseInStructure *>(info->next); n; n = n->next)
            if (n->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
                s->device = reinterpret_cast<const XrGraphicsBindingD3D11KHR *>(n)->device;
                if (s->device) {
                    s->device->GetImmediateContext(&s->context);
                    if (i.config.streaming || i.config.sharpening.mode != og::SharpeningMode::Off) {
                        // The downstream engine may present from a separate thread.
                        ComPtr<ID3D11Multithread> multithread;
                        if (SUCCEEDED(s->context.As(&multithread)))
                            multithread->SetMultithreadProtected(TRUE);
                    }
                    if (i.config.sharpening.mode != og::SharpeningMode::Off)
                        s->processor =
                            std::make_unique<og::ImageProcessor>(s->device.Get(), s->context.Get());
                    ComPtr<IDXGIDevice> dxgi;
                    ComPtr<IDXGIAdapter> adapter;
                    if (SUCCEEDED(s->device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter))) {
                        DXGI_ADAPTER_DESC desc{};
                        adapter->GetDesc(&desc);
                        s->adapter = desc.AdapterLuid;
                    }
                }
            }
        s->channel.open(i.game);
        if (i.engineLoaded)
            s->overlaysAvailable = s->overlays.open(i.game);
    }
    sessions[*out] = std::move(s);
    return result;
}
XrResult XRAPI_CALL destroy_session(XrSession h) {
    LOCK;
    auto it = sessions.find(h);
    if (it == sessions.end())
        return XR_ERROR_HANDLE_INVALID;
    auto r = it->second->instance->DestroySession(h);
    if (XR_SUCCEEDED(r)) {
        status(*it->second, false);
        for (auto c = chains.begin(); c != chains.end();)
            if (c->second->session == it->second.get())
                c = chains.erase(c);
            else
                ++c;
        sessions.erase(it);
    }
    return r;
}
XrResult XRAPI_CALL view_sizes(XrInstance h, XrSystemId sys, XrViewConfigurationType type,
                               uint32_t cap, uint32_t *count, XrViewConfigurationView *views) {
    LOCK;
    auto it = instances.find(h);
    if (it == instances.end())
        return XR_ERROR_HANDLE_INVALID;
    auto &i = *it->second;
    auto r = i.EnumerateViewConfigurationViews(h, sys, type, cap, count, views);
    if (XR_SUCCEEDED(r) && type == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO && count &&
        *count == 2 && cap >= 2 && views) {
        auto f = og::dimension_factors(i.config, mutable_fov(i, sys));
        for (unsigned e = 0; e < 2; ++e) {
            views[e].recommendedImageRectWidth =
                uint32_t(std::clamp(std::round(views[e].recommendedImageRectWidth * f[0]), 1.,
                                    double(views[e].maxImageRectWidth)));
            views[e].recommendedImageRectHeight =
                uint32_t(std::clamp(std::round(views[e].recommendedImageRectHeight * f[1]), 1.,
                                    double(views[e].maxImageRectHeight)));
        }
    }
    return r;
}
XrResult XRAPI_CALL locate_views(XrSession h, const XrViewLocateInfo *info, XrViewState *state,
                                 uint32_t cap, uint32_t *count, XrView *views) {
    LOCK;
    auto it = sessions.find(h);
    if (it == sessions.end())
        return XR_ERROR_HANDLE_INVALID;
    auto &s = *it->second;
    auto r = s.instance->LocateViews(h, info, state, cap, count, views);
    if (XR_SUCCEEDED(r) && info &&
        info->viewConfigurationType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO && count &&
        *count == 2 && cap >= 2 && views && s.instance->config.enabled) {
        for (unsigned eye = 0; eye < 2; ++eye) {
            auto p = projection(views[eye].fov);
            if (!std::isfinite(p.left) || !std::isfinite(p.right) || !std::isfinite(p.up) ||
                !std::isfinite(p.down) || p.right <= p.left || p.up <= p.down ||
                std::abs(p.left) > 100 || std::abs(p.right) > 100 || std::abs(p.up) > 100 ||
                std::abs(p.down) > 100)
                return r;
        }
        bool changed = !s.haveFov;
        for (unsigned e = 0; e < 2; ++e) {
            auto original = projection(views[e].fov);
            auto p = og::adjust_projection(original, s.instance->config, s.mutableFov);
            views[e].fov = angles(p);
            const auto &previous = s.originalFov[e];
            if (!s.haveFov || std::abs(original.left - previous.left) > 1e-5 ||
                std::abs(original.right - previous.right) > 1e-5 ||
                std::abs(original.up - previous.up) > 1e-5 ||
                std::abs(original.down - previous.down) > 1e-5) {
                changed = true;
                s.originalFov[e] = original;
                s.fov[e] = p;
                s.masks[e] = og::hidden_mesh(s.instance->config.contours[e],
                                             s.instance->config.padding, original);
            }
        }
        if (changed) {
            s.haveFov = true;
            if (s.instance->config.mask && s.instance->visibilityEnabled)
                s.pendingMaskEvents = 2;
        }
    }
    return r;
}
XrResult XRAPI_CALL visibility_mask(XrSession h, XrViewConfigurationType type, uint32_t eye,
                                    XrVisibilityMaskTypeKHR kind, XrVisibilityMaskKHR *mask) {
    LOCK;
    auto it = sessions.find(h);
    if (it == sessions.end())
        return XR_ERROR_HANDLE_INVALID;
    auto &s = *it->second;
    auto &i = *s.instance;
    if (!i.GetVisibilityMaskKHR)
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    ++s.maskCalls;
    if (mask && mask->type != XR_TYPE_VISIBILITY_MASK_KHR)
        return XR_ERROR_VALIDATION_FAILURE;
    bool expanded = i.config.enabled && i.config.streaming && s.mutableFov &&
                    (i.config.extraLeft > 0 || i.config.extraRight > 0);
    bool custom = i.config.enabled && i.config.mask && s.haveFov && eye < 2 &&
                  !s.masks[eye].indices.empty() &&
                  kind == XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR;
    if (!mask || type != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO || eye >= 2 ||
        (!expanded && !custom))
        return i.GetVisibilityMaskKHR(h, type, eye, kind, mask);
    XrVisibilityMaskKHR query{XR_TYPE_VISIBILITY_MASK_KHR};
    auto result = i.GetVisibilityMaskKHR(h, type, eye, kind, &query);
    if (XR_FAILED(result))
        return result;
    if (expanded) {
        mask->vertexCountOutput = mask->indexCountOutput = 0;
        return XR_SUCCESS;
    }
    if (query.vertexCountOutput > 100000 || query.indexCountOutput > 300000)
        return i.GetVisibilityMaskKHR(h, type, eye, kind, mask);
    og::Mesh mesh;
    mesh.vertices.resize(query.vertexCountOutput);
    mesh.indices.resize(query.indexCountOutput);
    query.vertexCapacityInput = query.vertexCountOutput;
    query.indexCapacityInput = query.indexCountOutput;
    query.vertices = reinterpret_cast<XrVector2f *>(mesh.vertices.data());
    query.indices = mesh.indices.data();
    if (query.vertexCountOutput && query.indexCountOutput) {
        result = i.GetVisibilityMaskKHR(h, type, eye, kind, &query);
        if (XR_FAILED(result))
            return result;
        mesh.vertices.resize(query.vertexCountOutput);
        mesh.indices.resize(query.indexCountOutput);
    }
    auto base = uint32_t(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), s.masks[eye].vertices.begin(),
                         s.masks[eye].vertices.end());
    for (auto index : s.masks[eye].indices)
        mesh.indices.push_back(base + index);
    mask->vertexCountOutput = uint32_t(mesh.vertices.size());
    mask->indexCountOutput = uint32_t(mesh.indices.size());
    if (!mask->vertexCapacityInput || !mask->indexCapacityInput)
        return XR_SUCCESS;
    if (mask->vertexCapacityInput < mesh.vertices.size() ||
        mask->indexCapacityInput < mesh.indices.size())
        return XR_ERROR_SIZE_INSUFFICIENT;
    if (!mask->vertices || !mask->indices)
        return XR_ERROR_VALIDATION_FAILURE;
    std::memcpy(mask->vertices, mesh.vertices.data(), mesh.vertices.size() * sizeof(og::Point));
    std::copy(mesh.indices.begin(), mesh.indices.end(), mask->indices);
    ++s.maskAppliedCalls;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL poll_event(XrInstance h, XrEventDataBuffer *event) {
    LOCK;
    auto it = instances.find(h);
    if (it == instances.end())
        return XR_ERROR_HANDLE_INVALID;
    auto r = it->second->PollEvent(h, event);
    if (r == XR_EVENT_UNAVAILABLE && event)
        for (auto &entry : sessions) {
            auto &s = *entry.second;
            if (s.instance == it->second.get() && s.pendingMaskEvents) {
                XrEventDataVisibilityMaskChangedKHR changed{
                    XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR};
                changed.session = s.handle;
                changed.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                changed.viewIndex = 2 - s.pendingMaskEvents--;
                std::memcpy(event, &changed, sizeof(changed));
                return XR_SUCCESS;
            }
        }
    return r;
}
XrResult XRAPI_CALL create_swapchain(XrSession h, const XrSwapchainCreateInfo *info,
                                     XrSwapchain *out) {
    LOCK;
    auto it = sessions.find(h);
    if (it == sessions.end())
        return XR_ERROR_HANDLE_INVALID;
    auto &i = *it->second->instance;
    auto r = i.CreateSwapchain(h, info, out);
    if (XR_FAILED(r))
        return r;
    auto c = std::make_unique<Swapchain>();
    c->session = it->second.get();
    c->info = *info;
    if (c->session->device && (info->usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT)) {
        uint32_t count{};
        if (XR_SUCCEEDED(i.EnumerateSwapchainImages(*out, 0, &count, nullptr)) && count < 64) {
            c->images.resize(count);
            for (auto &image : c->images)
                image.type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
            if (XR_FAILED(i.EnumerateSwapchainImages(
                    *out, count, &count,
                    reinterpret_cast<XrSwapchainImageBaseHeader *>(c->images.data()))))
                c->images.clear();
        }
    }
    chains[*out] = std::move(c);
    return r;
}
XrResult XRAPI_CALL destroy_swapchain(XrSwapchain h) {
    LOCK;
    auto it = chains.find(h);
    if (it == chains.end())
        return XR_ERROR_HANDLE_INVALID;
    auto r = it->second->session->instance->DestroySwapchain(h);
    if (XR_SUCCEEDED(r))
        chains.erase(it);
    return r;
}
XrResult XRAPI_CALL acquire_image(XrSwapchain h, const XrSwapchainImageAcquireInfo *info,
                                  uint32_t *index) {
    LOCK;
    auto it = chains.find(h);
    if (it == chains.end())
        return XR_ERROR_HANDLE_INVALID;
    auto r = it->second->session->instance->AcquireSwapchainImage(h, info, index);
    if (XR_SUCCEEDED(r))
        it->second->acquired.emplace_back(*index, false);
    return r;
}
XrResult XRAPI_CALL wait_image(XrSwapchain h, const XrSwapchainImageWaitInfo *info) {
    LOCK;
    auto it = chains.find(h);
    if (it == chains.end())
        return XR_ERROR_HANDLE_INVALID;
    auto r = it->second->session->instance->WaitSwapchainImage(h, info);
    if (r == XR_SUCCESS)
        for (auto &item : it->second->acquired)
            if (!item.second) {
                item.second = true;
                break;
            }
    return r;
}
XrResult XRAPI_CALL release_image(XrSwapchain h, const XrSwapchainImageReleaseInfo *info) {
    LOCK;
    auto it = chains.find(h);
    if (it == chains.end())
        return XR_ERROR_HANDLE_INVALID;
    auto &c = *it->second;
    if (!c.acquired.empty() && c.acquired.front().second) {
        snapshot(c, c.acquired.front().first);
        auto index = c.acquired.front().first;
        auto &s = *c.session;
        if (s.processor && !(c.info.createFlags & XR_SWAPCHAIN_CREATE_PROTECTED_CONTENT_BIT) &&
            index < c.images.size() && !c.projectionRegions.empty()) {
            const auto result =
                s.processor->process(c.images[index].texture, DXGI_FORMAT(c.info.format),
                                     c.projectionRegions, s.instance->config.sharpening);
            s.sharpenStatus = result.status;
            if (result.status == og::ImageProcessStatus::Applied)
                ++s.sharpenAppliedCalls;
        }
    }
    auto r = c.session->instance->ReleaseSwapchainImage(h, info);
    if (XR_SUCCEEDED(r) && !c.acquired.empty())
        c.acquired.pop_front();
    if (XR_FAILED(r))
        c.valid = false;
    return r;
}
void remember_projection_regions(Session &session, const XrFrameEndInfo *info) {
    for (auto &[handle, chain] : chains)
        if (chain->session == &session)
            chain->projectionRegions.clear();
    if (!info || !info->layers)
        return;
    std::vector<XrSwapchain> excluded;
    for (uint32_t n = 0; n < info->layerCount; ++n) {
        auto *layer = info->layers[n];
        if (!layer)
            continue;
        if (layer->type == XR_TYPE_COMPOSITION_LAYER_QUAD)
            excluded.push_back(
                reinterpret_cast<const XrCompositionLayerQuad *>(layer)->subImage.swapchain);
        else if (layer->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION)
            return; // Unknown composition: skip conservatively.
    }
    for (uint32_t n = 0; n < info->layerCount; ++n) {
        auto *layer = info->layers[n];
        if (!layer || layer->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION)
            continue;
        auto *projection = reinterpret_cast<const XrCompositionLayerProjection *>(layer);
        if (projection->viewCount != 2 || !projection->views)
            continue;
        for (uint32_t eye = 0; eye < 2; ++eye) {
            const auto &image = projection->views[eye].subImage;
            auto it = chains.find(image.swapchain);
            if (it == chains.end() || it->second->session != &session ||
                std::find(excluded.begin(), excluded.end(), image.swapchain) != excluded.end())
                continue;
            auto &chain = *it->second;
            auto r = image.imageRect;
            if ((chain.info.createFlags & XR_SWAPCHAIN_CREATE_PROTECTED_CONTENT_BIT) ||
                !(chain.info.usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) ||
                r.offset.x < 0 || r.offset.y < 0 || r.extent.width <= 0 || r.extent.height <= 0 ||
                image.imageArrayIndex >= chain.info.arraySize ||
                uint64_t(r.offset.x) + r.extent.width > chain.info.width ||
                uint64_t(r.offset.y) + r.extent.height > chain.info.height)
                continue;
            og::ImageRegion region{image.imageArrayIndex, uint32_t(r.offset.x),
                                   uint32_t(r.offset.y), uint32_t(r.extent.width),
                                   uint32_t(r.extent.height)};
            bool duplicate = std::any_of(chain.projectionRegions.begin(),
                                         chain.projectionRegions.end(), [&](const auto &old) {
                                             return old.arraySlice == region.arraySlice &&
                                                    old.x == region.x && old.y == region.y &&
                                                    old.width == region.width &&
                                                    old.height == region.height;
                                         });
            if (!duplicate)
                chain.projectionRegions.push_back(region);
        }
    }
}
XrResult XRAPI_CALL end_frame(XrSession h, const XrFrameEndInfo *info) {
    LOCK;
    auto it = sessions.find(h);
    if (it == sessions.end())
        return XR_ERROR_HANDLE_INVALID;
    auto &s = *it->second;
    if (info && s.instance->config.enabled)
        publish(s, info);
    auto r = s.instance->EndFrame(h, info);
    if (XR_SUCCEEDED(r)) {
        remember_projection_regions(s, info);
        ++s.frames;
        status(s, true);
    }
    return r;
}
template <auto Function> struct Boundary;
template <class... Args, XrResult(XRAPI_PTR *Function)(Args...)> struct Boundary<Function> {
    static XrResult XRAPI_CALL call(Args... args) noexcept {
        try {
            return Function(args...);
        } catch (const std::bad_alloc &) {
            return XR_ERROR_OUT_OF_MEMORY;
        } catch (...) {
            return XR_ERROR_RUNTIME_FAILURE;
        }
    }
};
XrResult XRAPI_CALL gipa(XrInstance h, const char *name, PFN_xrVoidFunction *out) {
    if (!name || !out)
        return XR_ERROR_VALIDATION_FAILURE;
    LOCK;
    auto it = instances.find(h);
    auto next = it == instances.end() ? fallback : it->second->gipa;
    if (!next)
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    auto r = next(h, name, out);
    if (XR_FAILED(r))
        return r;
#define HOOK(n, f)                                                                                 \
    if (!std::strcmp(name, "xr" #n)) {                                                             \
        *out = reinterpret_cast<PFN_xrVoidFunction>(Boundary<f>::call);                            \
        return XR_SUCCESS;                                                                         \
    }
    HOOK(GetInstanceProcAddr, gipa)
    HOOK(DestroyInstance, destroy_instance)
    HOOK(CreateSession, create_session)
    HOOK(DestroySession, destroy_session)
    HOOK(EnumerateViewConfigurationViews, view_sizes)
    HOOK(LocateViews, locate_views)
    HOOK(GetVisibilityMaskKHR, visibility_mask)
    HOOK(PollEvent, poll_event)
    HOOK(CreateSwapchain, create_swapchain)
    HOOK(DestroySwapchain, destroy_swapchain)
    HOOK(AcquireSwapchainImage, acquire_image)
    HOOK(WaitSwapchainImage, wait_image)
    HOOK(ReleaseSwapchainImage, release_image)
    HOOK(EndFrame, end_frame)
#undef HOOK
    return r;
}
XrResult XRAPI_CALL create_instance(const XrInstanceCreateInfo *info,
                                    const XrApiLayerCreateInfo *layerInfo, XrInstance *out) {
    if (!info || !layerInfo || !layerInfo->nextInfo || !out)
        return XR_ERROR_VALIDATION_FAILURE;
    auto *next = layerInfo->nextInfo;
    LOCK;
    auto i = std::make_unique<Instance>();
    i->gipa = next->nextGetInstanceProcAddr;
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, 32768);
    i->game = og::game_from_executable(exe);
    wchar_t env[32768]{};
    DWORD n = GetEnvironmentVariableW(L"OPENGRIDXR_CONFIG", env, 32768);
    if (n && n < 32768)
        i->path = env;
    else {
        HMODULE module{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(layerName), &module);
        GetModuleFileNameW(module, exe, 32768);
        i->path = (std::filesystem::path(exe).parent_path() / L"OpenGridXR.ini").wstring();
    }
    i->config = og::read_config(i->path, i->game);
    auto forwarded = *layerInfo;
    forwarded.nextInfo = next->next;
    PFN_xrCreateApiLayerInstance create = next->nextCreateApiLayerInstance;
    XrApiLayerNextInfo privateNext = *next;
    if (i->config.enabled && i->config.enhancements) {
        HMODULE owner{};
        wchar_t ownerPath[32768]{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(layerName), &owner);
        GetModuleFileNameW(owner, ownerPath, 32768);
        auto enginePath =
            std::filesystem::path(ownerPath).parent_path() / L"OpenGridXR-Enhancements.dll";
        // Keep the private engine loaded for the process lifetime. Its presenter
        // can legitimately finish teardown after an application destroys a session.
        HMODULE engine =
            LoadLibraryExW(enginePath.c_str(), nullptr,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!engine)
            i->engineError =
                "Enhancements DLL unavailable (Win32 " + std::to_string(GetLastError()) + ")";
        else {
            auto negotiate = reinterpret_cast<PFN_xrNegotiateLoaderApiLayerInterface>(
                GetProcAddress(engine, "xrNegotiateLoaderApiLayerInterface"));
            XrNegotiateLoaderInfo loader{XR_LOADER_INTERFACE_STRUCT_LOADER_INFO,
                                         XR_LOADER_INFO_STRUCT_VERSION,
                                         sizeof(XrNegotiateLoaderInfo),
                                         1,
                                         1,
                                         XR_MAKE_VERSION(1, 0, 0),
                                         XR_CURRENT_API_VERSION};
            XrNegotiateApiLayerRequest api{XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST,
                                           XR_API_LAYER_INFO_STRUCT_VERSION,
                                           sizeof(XrNegotiateApiLayerRequest)};
            if (!negotiate ||
                XR_FAILED(negotiate(&loader, "XR_APILAYER_OPENGRIDXR_ENHANCEMENTS", &api)) ||
                !api.getInstanceProcAddr || !api.createApiLayerInstance) {
                i->engineError = "Enhancements loader negotiation failed";
                FreeLibrary(engine);
            } else {
                strcpy_s(privateNext.layerName, "XR_APILAYER_OPENGRIDXR_ENHANCEMENTS");
                forwarded.nextInfo = &privateNext;
                create = api.createApiLayerInstance;
                i->gipa = api.getInstanceProcAddr;
                i->engineLoaded = true;
            }
        }
    }
    auto r = create(info, &forwarded, out);
    if (XR_FAILED(r))
        return r;
    i->handle = *out;
    for (uint32_t e = 0; e < info->enabledExtensionCount && info->enabledExtensionNames; ++e)
        if (!std::strcmp(info->enabledExtensionNames[e], XR_KHR_VISIBILITY_MASK_EXTENSION_NAME))
            i->visibilityEnabled = true;
#define LOAD(n) i->gipa(*out, "xr" #n, reinterpret_cast<PFN_xrVoidFunction *>(&i->n));
    LOAD(DestroyInstance)
    LOAD(CreateSession)
    LOAD(DestroySession)
    LOAD(GetViewConfigurationProperties)
    LOAD(EnumerateViewConfigurationViews)
    LOAD(LocateViews)
    LOAD(LocateSpace)
    LOAD(GetVisibilityMaskKHR)
    LOAD(PollEvent)
    LOAD(CreateSwapchain)
    LOAD(DestroySwapchain)
    LOAD(EnumerateSwapchainImages)
    LOAD(AcquireSwapchainImage)
    LOAD(WaitSwapchainImage)
    LOAD(ReleaseSwapchainImage)
    LOAD(EndFrame)
#undef LOAD
    fallback = i->gipa;
    instances[*out] = std::move(i);
    return r;
}
} // namespace
extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo *loader, const char *name, XrNegotiateApiLayerRequest *request) {
    if (!loader || !request || !name || std::strcmp(name, layerName) ||
        loader->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        loader->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
        loader->structSize != sizeof(*loader) ||
        request->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
        request->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION ||
        request->structSize != sizeof(*request) || loader->minInterfaceVersion > 1 ||
        loader->maxInterfaceVersion < 1 || loader->maxApiVersion < XR_MAKE_VERSION(1, 0, 0) ||
        loader->minApiVersion > XR_CURRENT_API_VERSION)
        return XR_ERROR_INITIALIZATION_FAILED;
    request->layerInterfaceVersion = 1;
    request->layerApiVersion = std::min(loader->maxApiVersion, XR_CURRENT_API_VERSION);
    request->getInstanceProcAddr = Boundary<gipa>::call;
    request->createApiLayerInstance = Boundary<create_instance>::call;
    return XR_SUCCESS;
}
