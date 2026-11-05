#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include "channel.hpp"
#include "overlay_geometry.hpp"
#include "settings.hpp"
#include "runtime_paths.hpp"
#include "lmu_integration.hpp"
#include "lmu_mask.hpp"
#include "lmu_stream.hpp"
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
#include <sstream>
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
    bool lmuIntegrated{}, lmuQuad{}, lmuExpansionUnsupported{};
    std::string lmuProvider;
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
    FN(BeginFrame)
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
    uint64_t visorColorAppliedCalls{};
    og::ImageProcessStatus visorColorStatus{og::ImageProcessStatus::Skipped};
    std::unique_ptr<og::ImageProcessor> processor;
    std::unique_ptr<og::ColorRuntime> colorRuntime;
    og::ColorChannel colorChannel;
    std::recursive_mutex colorGuard;
    std::vector<WORD> colorKeys;
    uint64_t colorFrame{UINT64_MAX}, colorSequence{};
    bool colorFrameBegun{};
    og::ColorPreset colorEffectPreset{og::ColorPreset::Neutral};
    bool colorFocused{}, colorRunning{}, colorSupported{}, colorEffect{};
    unsigned pendingMaskEvents{};
    uint64_t frames{}, capturedFrames{}, lastStatus{}, lastCapture{}, generation{};
    uint64_t nextViewerProbe{};
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
    og::lmu::Frame lmuFrame;
    uint64_t lmuProcessed[4]{}, lmuSnapshotFrame[4]{}, lmuCaptureFrame{};
    ComPtr<ID3D11Texture2D> lmuSnapshots[4];
    std::unique_ptr<og::LmuStream> lmuStream;
    std::unique_ptr<og::LmuMask> lmuMask;
    std::unique_ptr<og::ImageProcessor> lmuProcessors[4];
    std::string lmuCaptureState{"inactive"};
    bool lmuSubmitted{};
};
struct Swapchain {
    Session *session{};
    XrSwapchainCreateInfo info{};
    std::vector<XrSwapchainImageD3D11KHR> images;
    std::deque<std::pair<uint32_t, bool>> acquired;
    ComPtr<ID3D11Texture2D> snapshot;
    std::vector<og::ImageRegion> projectionRegions;
    std::unique_ptr<og::ImageProcessor> processor;
    bool valid{};
};
std::recursive_mutex guard;
std::unordered_map<XrInstance, std::unique_ptr<Instance>> instances;
std::unordered_map<XrSession, std::unique_ptr<Session>> sessions;
std::unordered_map<XrSwapchain, std::unique_ptr<Swapchain>> chains;
PFN_xrGetInstanceProcAddr fallback{};
#define LOCK std::lock_guard<std::recursive_mutex> lock(guard)
bool color_shortcut_down(const Session &s) {
    return !s.colorKeys.empty() &&
           std::all_of(s.colorKeys.begin(), s.colorKeys.end(),
                       [](WORD key) { return (GetAsyncKeyState(key) & 0x8000) != 0; });
}
void color_publish(Session &s, bool commands = false) {
    if (!s.colorRuntime)
        return;
    std::lock_guard<std::recursive_mutex> lock(s.colorGuard);
    if (s.colorRuntime)
        s.colorChannel.publish(*s.colorRuntime, s.colorRunning, s.colorFocused, s.colorSupported,
                               s.instance->config.color.enabled,
                               s.colorEffect && s.colorEffectPreset == s.colorRuntime->preset(),
                               GetTickCount64(), commands);
}
void color_begin(Session &s, uint64_t frame) {
    if (!s.colorRuntime)
        return;
    std::lock_guard<std::recursive_mutex> lock(s.colorGuard);
    if (s.colorFrame == frame)
        return;
    s.colorFrame = frame;
    s.colorRunning = true;
    color_publish(s, true);
    s.colorRuntime->frame(frame, s.colorFocused && s.colorSupported, color_shortcut_down(s));
    if (!og::scene_color_active(s.colorRuntime->frameColor()))
        s.colorEffect = false;
    color_publish(s);
}
const og::SceneColorSettings &frame_color(const Session &s) {
    // New independent correction takes precedence over legacy visor settings,
    // including its exact-neutral bypass. Legacy INIs remain readable.
    if ((s.instance->config.color.configured || s.instance->config.color.enabled) && s.colorRuntime)
        return s.colorRuntime->frameColor();
    if (s.instance->config.color.configured) {
        static const og::SceneColorSettings disabled;
        return disabled;
    }
    return s.instance->config.visorColor;
}
void color_processed(Session &s, const og::ImageProcessResult &result) {
    if (!s.colorRuntime || !s.instance->config.color.enabled)
        return;
    std::lock_guard<std::recursive_mutex> lock(s.colorGuard);
    if (result.status == og::ImageProcessStatus::Applied) {
        s.colorSupported = true;
        s.colorEffect = og::scene_color_active(frame_color(s));
        s.colorEffectPreset = s.colorRuntime->preset();
    } else if (result.status == og::ImageProcessStatus::Unsupported ||
               result.status == og::ImageProcessStatus::Failed) {
        s.colorSupported = false;
        s.colorEffect = false;
    }
    color_publish(s);
}
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
std::string lmu_status(const Session& s) {
    if(!s.instance->lmuIntegrated)return "null";
    std::ostringstream out;
    constexpr double degrees=180/3.14159265358979323846;
    const auto& frame=s.lmuFrame;
    out<<"{\"mode\":\""<<(s.instance->lmuQuad?"quad":"stereoVrs")<<"\",\"provider\":\""<<s.instance->lmuProvider
       <<"\",\"hookStatus\":\"ready\",\"frameId\":"<<frame.id<<",\"focusFovValid\":"<<(s.lmuSubmitted?"true":"false")
       <<",\"focusLeftHorizontal\":"<<(frame.views[2].fov.angleRight-frame.views[2].fov.angleLeft)*degrees
       <<",\"focusLeftVertical\":"<<(frame.views[2].fov.angleUp-frame.views[2].fov.angleDown)*degrees
       <<",\"focusRightHorizontal\":"<<(frame.views[3].fov.angleRight-frame.views[3].fov.angleLeft)*degrees
       <<",\"focusRightVertical\":"<<(frame.views[3].fov.angleUp-frame.views[3].fov.angleDown)*degrees
       <<",\"captureState\":\""<<s.lmuCaptureState<<"\",\"streamingExpansionUnsupported\":"<<(s.instance->lmuExpansionUnsupported?"true":"false");
    for(unsigned group=0;group<2;++group){out<<(group?",\"focusSizes\":[":",\"outerSizes\":[");
        for(unsigned eye=0;eye<2;++eye){if(eye)out<<',';const unsigned slot=group*2+eye;
            out<<"{\"width\":"<<frame.widths[slot]<<",\"height\":"<<frame.heights[slot]<<'}';}out<<']';}
    out<<",\"renderedFov\":[";
    if(s.lmuSubmitted)for(unsigned slot=0;slot<4;++slot){if(slot)out<<',';const auto& f=frame.views[slot].fov;
        out<<"{\"angleLeft\":"<<f.angleLeft<<",\"angleRight\":"<<f.angleRight<<",\"angleUp\":"<<f.angleUp<<",\"angleDown\":"<<f.angleDown<<'}';}
    out<<"]}";return out.str();
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
    auto directory=std::filesystem::path(s.instance->path).parent_path();
    wchar_t statusDirectory[32768]{};
    const DWORD statusLength=GetEnvironmentVariableW(L"OPENGRIDXR_STATUS_DIR",statusDirectory,32768);
    if(statusLength&&statusLength<32768)directory=statusDirectory;
    auto path = directory /
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
        << ",\"visorColorAppliedCalls\":" << s.visorColorAppliedCalls
        << ",\"visorColorStatus\":" << int(s.visorColorStatus)
        << ",\"visorColorSupported\":" << (s.device ? "true" : "false")
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
        << ",\"lmuRendering\":" << lmu_status(s)
        << ",\"message\":\""
        << (active ? "OpenXR frames observed; custom aperture supports hidden "
                     "triangle masks. Headset validation required."
                   : "Session ended.")
        << "\"}";
    out.close();
    MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}
bool viewer(Session &s) {
    const auto now = GetTickCount64();
    // Output can attach during a session. Poll an absent consumer at 4 Hz,
    // while an active consumer retains the immediate disconnect/stale check.
    if (now < s.nextViewerProbe)
        return false;
    if (!s.channel.lock())
        return false;
    auto *f = s.channel.data();
    bool active = f->consumer && now - f->consumerBeat < 3000;
    s.channel.unlock();
    s.nextViewerProbe = active ? 0 : now + 250;
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
void publish_owned(Session &s, const XrFrameEndInfo *end, const XrCompositionLayerProjection* p,
                   unsigned eye, Swapchain& c) {
    if (!s.device || !viewer(s))
        return;
    auto now = GetTickCount64();
    if (now - s.lastCapture < 1000 / std::max(30u, s.instance->config.fps))
        return;
    auto &v = p->views[eye];
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
void publish(Session& s,const XrFrameEndInfo* end) {
    if(!end||!end->layers)return;
    const XrCompositionLayerProjection* projection=nullptr;
    for(uint32_t i=0;i<end->layerCount;++i)if(end->layers[i]&&end->layers[i]->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION){
        projection=reinterpret_cast<const XrCompositionLayerProjection*>(end->layers[i]);break;}
    if(!projection||projection->viewCount!=2||!projection->views)return;
    const auto eye=s.instance->config.eye;
    if(eye>=2)return;
    const auto found=chains.find(projection->views[eye].subImage.swapchain);
    if(found==chains.end()||found->second->session!=&s||!found->second->valid)return;
    publish_owned(s,end,projection,eye,*found->second);
}
// Called under the adapter's frame/session lock on the render thread. Do not
// take guard here: public OpenXR calls acquire guard before the adapter lock.
void lmu_image(XrSession handle,const og::lmu::Frame& frame,unsigned slot,ID3D11Texture2D* texture,
               DXGI_FORMAT format,const og::ImageRegion& region) noexcept {
    auto found=sessions.find(handle);if(found==sessions.end()||slot>=4||!texture)return;
    auto& s=*found->second;
    if(!s.instance->lmuQuad||!frame.id||s.lmuProcessed[slot]==frame.id)return;
    if(frame.id!=s.lmuFrame.id){
        color_begin(s,frame.id);
        s.lmuSubmitted=false;
        s.lmuCaptureFrame=0;
        const auto now=GetTickCount64();
        if(now-s.lastCapture>=1000/std::max(30u,s.instance->config.fps)&&viewer(s)&&s.channel.lock()){
            // Admit both eye copies together only when the consumer can take
            // another frame. Keep publish_owned's stale-frame recovery policy.
            const auto* pending=s.channel.data();
            if(!pending->ready||now-pending->published>=1000)s.lmuCaptureFrame=frame.id;
            s.channel.unlock();
        }
    }
    s.lmuProcessed[slot]=frame.id;s.lmuFrame=frame;
    try {
        if(slot%2==s.instance->config.eye){
            s.lmuSnapshotFrame[slot]=0;
            if(s.lmuCaptureFrame==frame.id){
                D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
                D3D11_TEXTURE2D_DESC old{};if(s.lmuSnapshots[slot])s.lmuSnapshots[slot]->GetDesc(&old);
                if(!s.lmuSnapshots[slot]||old.Width!=desc.Width||old.Height!=desc.Height||old.Format!=format){
                    s.lmuSnapshots[slot].Reset();desc.Format=format;desc.Usage=D3D11_USAGE_DEFAULT;
                    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=desc.MiscFlags=0;
                    s.device->CreateTexture2D(&desc,nullptr,&s.lmuSnapshots[slot]);
                }
                if(s.lmuSnapshots[slot]){s.context->CopyResource(s.lmuSnapshots[slot].Get(),texture);s.lmuSnapshotFrame[slot]=frame.id;}
            }
        }
        if(s.processor && (og::sharpening_active(s.instance->config.sharpening)||og::scene_color_active(frame_color(s)))){
            // Each view keeps its own scratch size. Alternating focus/outer
            // through the stereo processor would reallocate large textures twice
            // every frame (and asymmetric providers can have four sizes).
            if(!s.lmuProcessors[slot])s.lmuProcessors[slot]=std::make_unique<og::ImageProcessor>(s.device.Get(),s.context.Get());
            const auto result=s.lmuProcessors[slot]->process(texture,format,{region},s.instance->config.sharpening,frame_color(s));
            color_processed(s,result);
            if(og::sharpening_active(s.instance->config.sharpening)){s.sharpenStatus=result.status;if(result.status==og::ImageProcessStatus::Applied)++s.sharpenAppliedCalls;}
            if(og::scene_color_active(frame_color(s))){s.visorColorStatus=result.status;if(result.status==og::ImageProcessStatus::Applied)++s.visorColorAppliedCalls;}
        }
        if(s.instance->config.mask){
            if(!s.lmuMask)s.lmuMask=std::make_unique<og::LmuMask>(s.device.Get(),s.context.Get());
            ++s.maskCalls;
            if(s.lmuMask->draw(texture,format,s.instance->config,slot%2,frame.fullOuterFov[slot%2],frame.views[slot].fov)==og::ImageProcessStatus::Applied)++s.maskAppliedCalls;
        }
    }catch(...){s.lmuCaptureState="skipped";}
}
void lmu_submit(XrSession handle,const og::lmu::Frame& frame,const XrCompositionLayerProjection& submitted,XrTime time) noexcept {
    auto found=sessions.find(handle);if(found==sessions.end())return;
    auto& s=*found->second;s.lmuFrame=frame;
    s.lmuSubmitted=s.instance->lmuQuad&&frame.id&&submitted.viewCount==4&&submitted.views&&time==frame.time;
    const auto eye=s.instance->config.eye;
    if(!s.instance->lmuQuad||eye>=2||submitted.viewCount!=4||!submitted.views||time!=frame.time||s.lmuSnapshotFrame[eye]!=frame.id){s.lmuCaptureState="skipped";return;}
    try {
        // Compose the selected eye's raw outer/focus from one completed frame,
        // before output stabilization. Never downsample focus into the tiny outer.
        if(!viewer(s)||GetTickCount64()-s.lastCapture<1000/std::max(30u,s.instance->config.fps))return;
        if(!s.channel.lock())return;
        const bool busy=s.channel.data()->ready&&GetTickCount64()-s.channel.data()->published<1000;
        s.channel.unlock();if(busy)return;
        ComPtr<ID3D11Texture2D> image=s.lmuSnapshots[eye];
        bool composed=false;
        if(s.lmuSnapshotFrame[eye+2]==frame.id&&submitted.views[eye+2].subImage.imageArrayIndex==0){
            if(!s.lmuStream)s.lmuStream=std::make_unique<og::LmuStream>(s.device.Get(),s.context.Get());
            if(auto result=s.lmuStream->compose(image.Get(),s.lmuSnapshots[eye+2].Get(),
                frame.views[eye].fov,frame.views[eye+2].fov,submitted.views[eye].pose,submitted.views[eye+2].pose,
                submitted.views[eye+2].subImage.imageRect,s.instance->config.width,s.instance->config.height)){image=result;composed=true;}
        }
        D3D11_TEXTURE2D_DESC desc{};image->GetDesc(&desc);
        // The owned image contains cropped outer rays, not provider padding.
        XrCompositionLayerProjectionView views[2]{submitted.views[0],submitted.views[1]};
        for(unsigned e=0;e<2;++e){views[e].fov=frame.views[e].fov;views[e].subImage.imageArrayIndex=0;
            views[e].subImage.imageRect={{0,0},{int32_t(frame.widths[e]),int32_t(frame.heights[e])}};}
        auto projection=submitted;projection.viewCount=2;projection.views=views;
        views[eye].subImage.imageRect.extent={int32_t(desc.Width),int32_t(desc.Height)};
        Swapchain owned;owned.session=&s;owned.valid=true;owned.snapshot=image;
        owned.info.width=desc.Width;owned.info.height=desc.Height;owned.info.arraySize=1;
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=time;
        const auto previous=s.capturedFrames;
        publish_owned(s,&end,&projection,eye,owned);
        if(s.capturedFrames!=previous)s.lmuCaptureState=composed?"ready-focus":"ready-outer-only";
    }catch(...){s.lmuCaptureState="skipped";}
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
        if(it->second->lmuIntegrated)og::lmu::reset();
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
    if (i.config.color.enabled) {
        s->colorRuntime = std::make_unique<og::ColorRuntime>(i.config.color);
        og::color_shortcut_keys(i.config.color.shortcut, s->colorKeys);
        s->colorRuntime->suppressHeld(color_shortcut_down(*s));
        s->colorChannel.open(i.game, true, s->identity);
    }
    if (i.config.enabled) {
        for (auto *n = reinterpret_cast<const XrBaseInStructure *>(info->next); n; n = n->next)
            if (n->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
                s->device = reinterpret_cast<const XrGraphicsBindingD3D11KHR *>(n)->device;
                if (s->device) {
                    s->device->GetImmediateContext(&s->context);
                    // Output can start in either mode after session creation.
                    // The downstream engine may present from a separate thread.
                    ComPtr<ID3D11Multithread> multithread;
                    if (SUCCEEDED(s->context.As(&multithread)))
                        multithread->SetMultithreadProtected(TRUE);
                    if (og::sharpening_active(i.config.sharpening) || (!i.config.color.configured&&og::scene_color_active(i.config.visorColor)) || i.config.color.enabled)
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
        if (!i.config.color.configured && og::scene_color_active(i.config.visorColor) && !s->device)
            s->visorColorStatus = og::ImageProcessStatus::Unsupported;
        s->channel.open(i.game);
        if (i.engineLoaded)
            s->overlaysAvailable = s->overlays.open(i.game);
    }
    s->colorSupported=s->device && s->device->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_0;
    color_publish(*s);
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
    if (XR_SUCCEEDED(r) && !i.lmuQuad && type == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO && count &&
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
        *count == 2 && cap >= 2 && views && s.instance->config.enabled && !s.instance->lmuQuad) {
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
    if (r == XR_SUCCESS && event && event->type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
        const auto *changed = reinterpret_cast<const XrEventDataSessionStateChanged *>(event);
        const auto found = sessions.find(changed->session);
        if (found != sessions.end() && found->second->colorRuntime) {
            auto &s = *found->second;
            std::lock_guard<std::recursive_mutex> colorLock(s.colorGuard);
            const bool focused = changed->state == XR_SESSION_STATE_FOCUSED;
            if (focused != s.colorFocused && s.colorRuntime)
                s.colorRuntime->suppressHeld(color_shortcut_down(s));
            s.colorFocused = focused;
            s.colorRunning = changed->state == XR_SESSION_STATE_SYNCHRONIZED ||
                             changed->state == XR_SESSION_STATE_VISIBLE || focused;
            if (!s.colorRunning)
                s.colorEffect = false;
            color_publish(s);
        }
    }
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
    if (!c.session->instance->lmuQuad && !c.acquired.empty() && c.acquired.front().second) {
        snapshot(c, c.acquired.front().first);
        auto index = c.acquired.front().first;
        auto &s = *c.session;
        if (s.colorRuntime && !s.colorFrameBegun) {
            s.colorFrameBegun = true;
            color_begin(s, ++s.colorSequence);
        }
        if (s.processor &&
            (og::sharpening_active(s.instance->config.sharpening) ||
             og::scene_color_active(frame_color(s))) &&
            !(c.info.createFlags & XR_SWAPCHAIN_CREATE_PROTECTED_CONTENT_BIT) &&
            index < c.images.size() && !c.projectionRegions.empty()) {
            if (!c.processor)
                c.processor = std::make_unique<og::ImageProcessor>(s.device.Get(), s.context.Get());
            const auto result = c.processor->process(
                c.images[index].texture, DXGI_FORMAT(c.info.format), c.projectionRegions,
                s.instance->config.sharpening, frame_color(s));
            color_processed(s, result);
            if (og::sharpening_active(s.instance->config.sharpening)) {
                s.sharpenStatus = result.status;
                if (result.status == og::ImageProcessStatus::Applied)
                    ++s.sharpenAppliedCalls;
            }
            if (og::scene_color_active(frame_color(s))) {
                s.visorColorStatus = result.status;
                if (result.status == og::ImageProcessStatus::Applied)
                    ++s.visorColorAppliedCalls;
            }
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
    // Settings are fixed for the session. Only image processing consumes these
    // regions, so the base crop/capture path needs no scans or vector storage.
    if (!session.processor || session.instance->lmuQuad)
        return;
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
        if ((projection->viewCount != 2 && projection->viewCount != 4) || !projection->views)
            continue;
        for (uint32_t eye = 0; eye < projection->viewCount; ++eye) {
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
XrResult XRAPI_CALL begin_frame(XrSession h, const XrFrameBeginInfo *info) {
    LOCK;
    const auto it = sessions.find(h);
    if (it == sessions.end())
        return XR_ERROR_HANDLE_INVALID;
    auto &s = *it->second;
    if (!s.instance->BeginFrame)
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    const auto result = s.instance->BeginFrame(h, info);
    if (XR_SUCCEEDED(result) && s.colorRuntime) {
        if (!s.instance->lmuQuad) {
            s.colorFrameBegun = true;
            color_begin(s, ++s.colorSequence);
        } else {
            std::lock_guard<std::recursive_mutex> colorLock(s.colorGuard);
            s.colorRunning = true;
            color_publish(s);
        }
    }
    return result;
}
XrResult XRAPI_CALL end_frame(XrSession h, const XrFrameEndInfo *info) {
    LOCK;
    auto it = sessions.find(h);
    if (it == sessions.end())
        return XR_ERROR_HANDLE_INVALID;
    auto &s = *it->second;
    s.colorFrameBegun=false;
    if(s.instance->lmuQuad)s.lmuSubmitted=false;
    if (info && s.instance->config.enabled && !s.instance->lmuQuad)
        publish(s, info);
    if(info&&info->layers&&s.instance->lmuIntegrated&&!s.instance->lmuQuad){
        for(uint32_t i=0;i<info->layerCount;++i){
            const auto* layer=info->layers[i];if(!layer||layer->type!=XR_TYPE_COMPOSITION_LAYER_PROJECTION)continue;
            const auto* projection=reinterpret_cast<const XrCompositionLayerProjection*>(layer);
            if(projection->viewCount!=2||!projection->views)continue;
            s.lmuFrame={};s.lmuFrame.id=s.frames+1;s.lmuFrame.time=info->displayTime;s.lmuFrame.space=projection->space;
            for(unsigned eye=0;eye<2;++eye){const auto& view=projection->views[eye];
                s.lmuFrame.views[eye].fov=view.fov;s.lmuFrame.views[eye].pose=view.pose;
                s.lmuFrame.widths[eye]=std::max(0,view.subImage.imageRect.extent.width);
                s.lmuFrame.heights[eye]=std::max(0,view.subImage.imageRect.extent.height);}
            break;
        }
    }
    auto r = s.instance->EndFrame(h, info);
    if (XR_SUCCEEDED(r)) {
        remember_projection_regions(s, info);
        ++s.frames;
        status(s, true);
    } else if(s.instance->lmuQuad) {
        s.lmuSubmitted=false;s.lastStatus=0;status(s,true);
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
    HOOK(BeginFrame, begin_frame)
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
        i->path = (og::runtime_data_directory(std::filesystem::path(exe).parent_path()) / L"OpenGridXR.ini").wstring();
    }
    i->config = og::read_config(i->path, i->game);
    // The integrated adapter owns one instance. A duplicate must not undo the
    // first instance's environment before its deferred engine/session startup.
    if (og::lmu::settings.enabled) return XR_ERROR_LIMIT_REACHED;
    if (!og::lmu::prepare_saved(i->config, i->game)) return XR_ERROR_INITIALIZATION_FAILED;
    const bool integrationRequested=i->config.enabled&&i->game=="lmu"&&og::lmu::environment("OPENGRIDXR_LMU_INTEGRATED")=="1";
    if(integrationRequested){
        if(!og::lmu::configure(i->config,i->game,{lmu_image,lmu_submit})){og::lmu::restore_environment();return XR_ERROR_VALIDATION_FAILURE;}
        i->lmuIntegrated=true;i->lmuQuad=og::lmu::settings.quad;
        i->lmuProvider=og::lmu::settings.provider;i->lmuExpansionUnsupported=og::lmu::settings.expansionUnsupported;
    }
    auto forwarded = *layerInfo;
    forwarded.nextInfo = next->next;
    PFN_xrCreateApiLayerInstance create = next->nextCreateApiLayerInstance;
    // An implicitly loaded QVFR above us has already seen LMU's stereo request.
    // Use an isolated local copy below the adapter when no downstream QVFR is
    // available. Never negotiate the same module twice (QVFR owns singleton state).
    XrApiLayerNextInfo providerNext = *next;
    bool downstreamQvfr = false;
    for (auto* entry = next->next; entry; entry = entry->next)
        if (!std::strcmp(entry->layerName, "XR_APILAYER_MBUCCHIA_quad_views_foveated")) downstreamQvfr = true;
    if (i->lmuQuad && i->lmuProvider == "qvfr" && !downstreamQvfr) {
        HMODULE owner{}; wchar_t ownerPath[32768]{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(layerName), &owner);
        GetModuleFileNameW(owner, ownerPath, 32768);
        auto providerPath = std::filesystem::path(ownerPath).parent_path() / L"providers" / L"qvfr" / L"OpenGridXR-QVFR.dll";
        HMODULE provider = LoadLibraryExW(providerPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto negotiate = provider ? reinterpret_cast<PFN_xrNegotiateLoaderApiLayerInterface>(GetProcAddress(provider, "xrNegotiateLoaderApiLayerInterface")) : nullptr;
        XrNegotiateLoaderInfo loader{XR_LOADER_INTERFACE_STRUCT_LOADER_INFO, XR_LOADER_INFO_STRUCT_VERSION, sizeof(XrNegotiateLoaderInfo), 1, 1, XR_MAKE_VERSION(1,0,0), XR_CURRENT_API_VERSION};
        XrNegotiateApiLayerRequest api{XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST, XR_API_LAYER_INFO_STRUCT_VERSION, sizeof(XrNegotiateApiLayerRequest)};
        if (!negotiate || XR_FAILED(negotiate(&loader, "XR_APILAYER_MBUCCHIA_quad_views_foveated", &api)) || !api.getInstanceProcAddr || !api.createApiLayerInstance) {
            og::lmu::reset(); return XR_ERROR_INITIALIZATION_FAILED;
        }
        strcpy_s(providerNext.layerName, "XR_APILAYER_MBUCCHIA_quad_views_foveated");
        forwarded.nextInfo = &providerNext;
        create = api.createApiLayerInstance; i->gipa = api.getInstanceProcAddr;
    }
    XrApiLayerNextInfo privateNext = *next;
    privateNext.next = forwarded.nextInfo;
    privateNext.nextGetInstanceProcAddr = i->gipa;
    privateNext.nextCreateApiLayerInstance = create;
    if (i->config.enabled && (i->config.enhancements || (i->lmuQuad&&i->lmuProvider=="vectorxr"))) {
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
    XrApiLayerNextInfo adapterNext=*next;
    if(i->lmuIntegrated){
        if(i->lmuQuad&&i->lmuProvider=="vectorxr"&&!i->engineLoaded){og::lmu::reset();return XR_ERROR_INITIALIZATION_FAILED;}
        strcpy_s(adapterNext.layerName,"XR_APILAYER_LMU_quadview");
        adapterNext.nextGetInstanceProcAddr=i->gipa;
        adapterNext.nextCreateApiLayerInstance=create;
        adapterNext.next=forwarded.nextInfo;
        forwarded.nextInfo=&adapterNext;
        create=og::lmu::create_instance;i->gipa=og::lmu::get_proc;
    }
    auto r = create(info, &forwarded, out);
    if (XR_FAILED(r)) {
        if(i->lmuIntegrated)og::lmu::reset();
        return r;
    }
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
    LOAD(BeginFrame)
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
