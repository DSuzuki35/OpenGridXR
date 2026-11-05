#include <windows.h>
static bool shortcutHeld{};
static unsigned shortcutSamples{};
static SHORT colorTestKeyState(int) { ++shortcutSamples; return shortcutHeld ? SHORT(0x8000) : 0; }
#define GetAsyncKeyState colorTestKeyState
#include "../src/layer.cpp"
#undef GetAsyncKeyState
#include <iostream>
#include <stdexcept>
void check(bool value, const char *text) {
    if (!value)
        throw std::runtime_error(text);
}
XrResult XRAPI_CALL createOkay(XrInstance, const XrSessionCreateInfo *, XrSession *out) {
    *out = reinterpret_cast<XrSession>(71);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL properties(XrInstance, XrSystemId, XrViewConfigurationType,
                               XrViewConfigurationProperties *out) {
    out->fovMutable = XR_TRUE;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL beginOkay(XrSession, const XrFrameBeginInfo *) { return XR_SUCCESS; }
static XrSessionState nextState = XR_SESSION_STATE_FOCUSED;
XrResult XRAPI_CALL eventOkay(XrInstance, XrEventDataBuffer *buffer) {
    XrEventDataSessionStateChanged event{XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};
    event.session = reinterpret_cast<XrSession>(71);
    event.state = nextState;
    std::memcpy(buffer, &event, sizeof(event));
    return XR_SUCCESS;
}
XrResult XRAPI_CALL releaseOkay(XrSwapchain, const XrSwapchainImageReleaseInfo *) {
    return XR_SUCCESS;
}
uint32_t pixel(Session &s, ID3D11Texture2D *texture) {
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> read;
    check(SUCCEEDED(s.device->CreateTexture2D(&desc, nullptr, &read)), "read fixture");
    s.context->CopyResource(read.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE map{};
    check(SUCCEEDED(s.context->Map(read.Get(), 0, D3D11_MAP_READ, 0, &map)), "read mapping");
    auto value = *static_cast<uint32_t *>(map.pData);
    s.context->Unmap(read.Get(), 0);
    return value;
}
int main() {
    try {
        const auto handle = reinterpret_cast<XrInstance>(70);
        instances[handle] = std::make_unique<Instance>();
        auto &i = *instances[handle];
        i.game = "color-layer-test-" + std::to_string(GetCurrentProcessId());
        i.config.enabled = true;
        i.config.color.enabled = true;
        i.CreateSession = createOkay;
        i.GetViewConfigurationProperties = properties;
        i.BeginFrame = beginOkay;
        i.PollEvent = eventOkay;
        i.ReleaseSwapchainImage = releaseOkay;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                                          D3D11_SDK_VERSION, &device, nullptr, &context)),
              "WARP fixture");
        XrGraphicsBindingD3D11KHR graphics{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
        graphics.device = device.Get();
        XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};
        info.next = &graphics;
        XrSession sessionHandle{};
        check(create_session(handle, &info, &sessionHandle) == XR_SUCCESS,
              "neutral color session created");
        auto &s = *sessions.at(sessionHandle);
        check(s.processor != nullptr,
              "enabled neutral creates processor availability despite sharpening off");
        const auto chainHandle = reinterpret_cast<XrSwapchain>(72);
        chains[chainHandle] = std::make_unique<Swapchain>();
        auto &c = *chains.at(chainHandle);
        c.session = &s;
        c.info.width = c.info.height = 8;
        c.info.arraySize = 1;
        c.info.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        c.info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = 8;
        desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> image;
        check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &image)), "color image");
        ComPtr<ID3D11RenderTargetView> target;
        check(SUCCEEDED(device->CreateRenderTargetView(image.Get(), nullptr, &target)),
              "color target");
        c.images.push_back({XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR, nullptr, image.Get()});
        XrCompositionLayerProjectionView views[2]{};
        for (auto &view : views)
            view.subImage = {chainHandle, {{0, 0}, {8, 8}}, 0};
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        projection.viewCount = 2;
        projection.views = views;
        const auto base = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&projection);
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
        end.layerCount = 1;
        end.layers = &base;
        remember_projection_regions(s, &end);
        check(c.projectionRegions.size() == 1, "neutral startup records future processing regions");
        s.colorFocused = true;
        const float gray[4] = {.2f, .2f, .2f, .4f};
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        begin_frame(sessionHandle, &begin);
        context->ClearRenderTargetView(target.Get(), gray);
        c.acquired.push_back({0, true});
        release_image(chainHandle, &release);
        check((pixel(s, image.Get()) & 255) == 51 && s.visorColorAppliedCalls == 0,
              "neutral remains pixel exact without an extra pass");
        check(!c.processor, "neutral startup creates no per-swapchain GPU processor resources");
        s.colorRuntime->cycle();
        ++s.frames;
        begin_frame(sessionHandle, &begin);
        context->ClearRenderTargetView(target.Get(), gray);
        c.acquired.push_back({0, true});
        release_image(chainHandle, &release);
        check((pixel(s, image.Get()) & 255) < 51 && s.visorColorAppliedCalls == 1,
              "neutral to Sun applies live with helmet absent and sharpening off");
        const auto cachedProcessor = c.processor.get();
        check(cachedProcessor != nullptr, "active view caches its processor");
        s.colorRuntime->cycle();
        context->ClearRenderTargetView(target.Get(), gray);
        c.acquired.push_back({0, true});
        release_image(chainHandle, &release);
        check((pixel(s, image.Get()) & 255) < 51, "second eye retains Sun after midframe cycle");
        ++s.frames;
        begin_frame(sessionHandle, &begin);
        context->ClearRenderTargetView(target.Get(), gray);
        c.acquired.push_back({0, true});
        release_image(chainHandle, &release);
        check((pixel(s, image.Get()) & 255) > 51, "next frame uses Night and lifts dark detail");
        check(c.processor.get() == cachedProcessor, "preset cycles reuse the same view processor");
        check(s.colorChannel.lock(), "color status lock");
        check(s.colorChannel.data()->effectActive == 1,
              "status reports a real successfully applied night effect");
        s.colorChannel.unlock();
        ++s.frames;
        begin_frame(sessionHandle, &begin);
        check(s.colorChannel.lock(), "stable active status lock");
        check(s.colorChannel.data()->effectActive == 1,
              "a continuing preset remains active while the next frame is being rendered");
        s.colorChannel.unlock();
        nextState = XR_SESSION_STATE_VISIBLE;
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        poll_event(handle, &event);
        shortcutHeld = true;
        ++s.frames;
        begin_frame(sessionHandle, &begin);
        nextState = XR_SESSION_STATE_FOCUSED;
        poll_event(handle, &event);
        ++s.frames;
        begin_frame(sessionHandle, &begin);
        check(s.colorRuntime->preset() == og::ColorPreset::Night,
              "keyboard held while XR focus returns cannot cycle");
        shortcutHeld = false;
        ++s.frames;
        begin_frame(sessionHandle, &begin);
        shortcutHeld = true;
        ++s.frames;
        begin_frame(sessionHandle, &begin);
        check(s.colorRuntime->preset() == og::ColorPreset::Custom,
              "fresh keyboard edge after XR focus return cycles exactly once");
        ++s.frames;
        begin_frame(sessionHandle, &begin);
        check(s.colorRuntime->preset() == og::ColorPreset::Custom,
              "held focused shortcut cannot repeat");
        shortcutHeld = false;
        s.colorRuntime->cycle();
        s.colorRuntime->cycle();
        begin_frame(sessionHandle, &begin);
        check(s.colorRuntime->frameColor().brightness == 75,
              "a newly begun XR frame snapshots even if the preceding end frame failed or was "
              "discarded");
        chains.clear();
        sessions.clear();
        i.config.color.enabled = false;
        i.config.color.configured = true;
        i.config.visorColor = {true, 100, 50, 0};
        shortcutSamples = 0;
        check(create_session(handle, &info, &sessionHandle) == XR_SUCCESS,
              "explicit OFF session created");
        auto &disabled = *sessions.at(sessionHandle);
        check(!disabled.processor && !og::scene_color_active(frame_color(disabled)),
              "explicit independent OFF suppresses a conflicting enabled legacy visor without "
              "allocating a processor");
        check(!disabled.colorRuntime && !disabled.colorChannel.data() && disabled.colorKeys.empty(),
              "disabled color allocates no runtime, control channel, or keyboard shortcut");
        begin_frame(sessionHandle, &begin);
        nextState = XR_SESSION_STATE_VISIBLE;
        poll_event(handle, &event);
        nextState = XR_SESSION_STATE_FOCUSED;
        poll_event(handle, &event);
        begin_frame(sessionHandle, &begin);
        color_publish(disabled, true);
        chains[chainHandle] = std::make_unique<Swapchain>();
        auto &disabledChain = *chains.at(chainHandle);
        disabledChain.session = &disabled;
        disabledChain.acquired.push_back({0, true});
        release_image(chainHandle, &release);
        chains.clear();
        i.lmuQuad = true;
        begin_frame(sessionHandle, &begin);
        i.lmuQuad = false;
        check(shortcutSamples == 0 && disabled.colorFrame == UINT64_MAX && disabled.colorSequence == 0 &&
                  !disabled.colorRunning && !disabled.colorChannel.data(),
              "disabled creation, frame updates, focus events and publication do no color control work");
        sessions.clear();
        i.config.color.configured = false;
        check(create_session(handle, &info, &sessionHandle) == XR_SUCCESS, "legacy-only session created");
        auto &legacy = *sessions.at(sessionHandle);
        check(legacy.processor && og::scene_color_active(frame_color(legacy)) && frame_color(legacy).brightness == 50,
              "legacy INI grading remains available without independent color control");
        check(!legacy.colorRuntime && !legacy.colorChannel.data() && shortcutSamples == 0,
              "legacy grading does not create independent color controls");
        sessions.clear();
        instances.clear();
        std::cout << "Neutral startup and live stereo color integration passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
