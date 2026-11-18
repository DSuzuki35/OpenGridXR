#include "channel.hpp"
#include "core.hpp"
#include "settings.hpp"
#include "output_framing.hpp"
#include "output_framing_preview.hpp"
#include "output_activity.hpp"
#include "output_monitor_clone.hpp"
#include "../telemetry/windows_memory.hpp"
#include "stabilization.hpp"
#include "stream_overlay_renderer.hpp"
#include "stream_depth.hpp"
#include "stream_cadence.hpp"
#include "stream_helmet_renderer.hpp"
#include <algorithm>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <filesystem>
#include <shellapi.h>
#include <sstream>
#include <utility>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace {
struct Constants {
    float input[4], output[4], quat[4], uvBounds[4], flags[4], mirrorStyle[4], grade[4];
};
const char *shader = R"(
Texture2D source:register(t0);SamplerState linearSampler:register(s0);
cbuffer Parameters:register(b0){float4 inputFov;float4 outputFov;float4 q;float4 uvBounds;float4 flags;float4 mirrorStyle;float4 grade;}
struct V{float4 pos:SV_Position;float2 uv:TEXCOORD0;};
V vs(uint id:SV_VertexID){V o;o.uv=float2((id<<1)&2,id&2);o.pos=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}
float3 imageSample(float2 uv) {
 return source.Sample(linearSampler,lerp(uvBounds.xy,uvBounds.zw,
   clamp(uv,mirrorStyle.yz*.5,1-mirrorStyle.yz*.5))).rgb;
}
float3 finiteMirror(float2 originalUV) {
 float2 over=max(max(-originalUV,originalUV-1),0);
 float distance=max(over.x,over.y);
 float2 uv=1-abs(1-2*frac(originalUV*.5));
 [branch] if(mirrorStyle.w>0 && distance>0)
   uv=lerp(uv,saturate(originalUV),mirrorStyle.w*smoothstep(0,max(.000001,flags.z),distance));
 float3 color=imageSample(uv);
 [branch] if(mirrorStyle.x<=0 || distance<=0) return color;
 float seam=smoothstep(0,max(.0001,min(.03,flags.z*.5)),distance);
 float amount=mirrorStyle.x*seam;
 float2 radius=mirrorStyle.yz*(18*amount);
 float3 blurred=color*.4;
 blurred+=imageSample(uv+radius)*.15;
 blurred+=imageSample(uv-radius)*.15;
 blurred+=imageSample(uv+float2(radius.x,-radius.y))*.15;
 blurred+=imageSample(uv+float2(-radius.x,radius.y))*.15;
 color=lerp(color,blurred,amount)*(1-.18*amount);
 return color*(1-smoothstep(flags.z*(1-.12*mirrorStyle.x),flags.z,distance));
}
float4 ps(V v):SV_Target {
 float3 ray=float3(lerp(outputFov.x,outputFov.y,v.uv.x),lerp(outputFov.z,outputFov.w,v.uv.y),-1);
 ray+=2*cross(q.xyz,cross(q.xyz,ray)+q.w*ray);
 if(ray.z>=-.0001)return float4(0,0,0,1);
 float2 uv=float2((ray.x/-ray.z-inputFov.x)/(inputFov.y-inputFov.x),
                 (inputFov.z-ray.y/-ray.z)/(inputFov.z-inputFov.w));
 float3 color;
 [branch] if(flags.y>.5 && flags.z>=0) {
   if(ray.z>=-.05 || any(uv < -flags.z) || any(uv > 1+flags.z))return float4(0,0,0,1);
   color=finiteMirror(uv);
 } else color=source.Sample(linearSampler,lerp(uvBounds.xy,uvBounds.zw,uv)).rgb;
 if(flags.x>.5)color=lerp(12.92*color,1.055*pow(max(color,0),1.0/2.4)-.055,step(.0031308,color));
 [branch] if(flags.w>0) {
   // Match headset grading in display-encoded RGB before compositing dashboards and helmet art.
   float3 adjusted=pow(saturate((color*grade.y-.5)*grade.x+.5),grade.w);
   float gray=dot(adjusted,float3(.2126,.7152,.0722));
   color=lerp(color,saturate(lerp(gray.xxx,adjusted,grade.z)),flags.w);
 }
 return float4(color,1);
}
float4 present(V v):SV_Target {
 return float4(source.Sample(linearSampler,v.uv).rgb,1);
}
)";
class Renderer {
  public:
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11Texture2D> canvas;
    ComPtr<ID3D11RenderTargetView> canvasTarget;
    ComPtr<ID3D11ShaderResourceView> canvasView;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps, presentShader;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Texture2D> shared, local;
    ComPtr<IDXGIKeyedMutex> key;
    ComPtr<ID3D11ShaderResourceView> view;
    uint64_t handle{}, sequence{}, session{};
    LUID adapter{};
    vrst::PoseHistory poses;
    og::UprightReference upright;
    og::CameraComparison comparison;
    og::SharedFrame frame{};
    og::StreamOverlayRenderer overlays;
    og::StreamDepthReader streamDepth;
    og::StreamHelmetRenderer streamHelmet;
    bool init(HWND window, LUID luid, bool mirror, const std::string& game = "") {
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
            return false;
        ComPtr<IDXGIAdapter1> chosen;
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> a;
            if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND)
                break;
            DXGI_ADAPTER_DESC1 d{};
            a->GetDesc1(&d);
            if (d.AdapterLuid.HighPart == luid.HighPart && d.AdapterLuid.LowPart == luid.LowPart) {
                chosen = a;
                break;
            }
        }
        if (!chosen)
            return false;
        adapter = luid;
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = window;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        D3D_FEATURE_LEVEL level{};
        if (FAILED(D3D11CreateDeviceAndSwapChain(
                chosen.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr, 0, D3D11_SDK_VERSION, &sd, &swap, &device, &level, &context)))
            return false;
        factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
        if (!resize())
            return false;
        ComPtr<ID3DBlob> v, p, present, err;
        if (FAILED(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "vs",
                              "vs_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &v, &err)) ||
            FAILED(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "ps",
                              "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &p, &err)) ||
            FAILED(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "present",
                              "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &present, &err)))
            return false;
        if (FAILED(device->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), nullptr,
                                              &vs)) ||
            FAILED(
                device->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), nullptr, &ps)) ||
            FAILED(device->CreatePixelShader(present->GetBufferPointer(), present->GetBufferSize(),
                                             nullptr, &presentShader)))
            return false;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(Constants);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&bd, nullptr, &constants)))
            return false;
        D3D11_SAMPLER_DESC ss{};
        ss.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        ss.AddressU = ss.AddressV = ss.AddressW =
            mirror ? D3D11_TEXTURE_ADDRESS_MIRROR : D3D11_TEXTURE_ADDRESS_BORDER;
        ss.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&ss, &sampler)))
            return false;
        // Overlay failure does not prevent the clean game image from rendering.
        if (!game.empty()) overlays.initialize(device.Get(), context.Get(), game);
        if (!game.empty()) streamHelmet.initialize(device.Get(), context.Get(), game);
        return true;
    }
    bool resize() {
        if (!swap)
            return false;
        context->OMSetRenderTargets(0, nullptr, nullptr);
        target.Reset();
        if (FAILED(swap->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0)))
            return false;
        ComPtr<ID3D11Texture2D> buffer;
        if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&buffer))))
            return false;
        return SUCCEEDED(device->CreateRenderTargetView(buffer.Get(), nullptr, &target));
    }
    bool consume(og::SharedFrame &f) {
        if (!device || !f.ready || !f.width || !f.height || f.width > 16384 || f.height > 16384 ||
            !f.texture)
            return false;
        if (f.session != frame.session || f.producer != frame.producer ||
            f.reference != frame.reference || f.captureEye != frame.captureEye ||
            f.sequence < frame.sequence ||
            f.published < frame.published) {
            poses.clear();
            upright.reset();
        }
        if (f.texture != handle || f.session != session || f.generation != frame.generation ||
            f.producer != frame.producer) {
            shared.Reset();
            key.Reset();
            local.Reset();
            view.Reset();
            handle = 0;
            if (FAILED(device->OpenSharedResource(reinterpret_cast<HANDLE>(f.texture),
                                                  IID_PPV_ARGS(&shared))) ||
                FAILED(shared.As(&key)))
                return false;
            D3D11_TEXTURE2D_DESC d{};
            shared->GetDesc(&d);
            if (d.Width != f.width || d.Height != f.height || d.Format != DXGI_FORMAT(f.format) ||
                d.ArraySize != 1 || d.SampleDesc.Count != 1)
                return false;
            d.MiscFlags = 0;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(device->CreateTexture2D(&d, nullptr, &local)) ||
                FAILED(device->CreateShaderResourceView(local.Get(), nullptr, &view)))
                return false;
            handle = f.texture;
            session = f.session;
            poses.clear();
        }
        if (key->AcquireSync(1, 0) != S_OK)
            return false;
        context->CopyResource(local.Get(), shared.Get());
        context->Flush();
        key->ReleaseSync(0);
        streamDepth.consume(device.Get(), context.Get(), f.depth,
                            f.session != frame.session || f.producer != frame.producer);
        frame = f;
        f.ready = 0;
        sequence = f.sequence;
        return true;
    }
    bool render(HWND window, const og::Config &c) {
        if (!view || !target)
            return false;
        auto q = vrst::Quat{frame.orientation[0], frame.orientation[1], frame.orientation[2],
                            frame.orientation[3]};
        auto projection = vrst::Projection{frame.fov[0], frame.fov[1], frame.fov[2], frame.fov[3]};
        vrst::MotionSettings motion;
        motion.amount = motion.roll = c.smoothing;
        motion.separate_axes = c.separateSmoothing;
        motion.yaw = c.horizontalSmoothing;
        motion.pitch = c.verticalSmoothing;
        poses.set_smoothing(motion);
        vrst::Pose pose;
        pose.session = frame.session;
        pose.sequence = frame.sequence;
        pose.time_ns = frame.published * 1000000;
        pose.orientation = q;
        pose.projection = projection;
        pose.flags = vrst::TrackingValid | vrst::FramePose;
        poses.push(pose);
        auto sample = poses.sample(pose.time_ns);
        auto correction = sample.valid ? upright.correction(q, sample.target_orientation, c.horizon,
                                                             c.adaptiveHorizon, c.pitchLock)
                                       : vrst::Quat{};
        vrst::Settings settings;
        settings.automatic_fov = true;
        settings.output_16_9 = false;
        // Driving crop is already part of the submitted eye projection.
        // Only Streaming retains the full image for output-side cropping.
        const auto crop = c.streaming ? c.crop : og::Edges{};
        settings.input_left = crop.left;
        settings.input_right = crop.right;
        settings.input_top = crop.top;
        settings.input_bottom = crop.bottom;
        double pw = projection.right - projection.left, ph = projection.up - projection.down;
        projection.left += pw * crop.left / 100;
        projection.right -= pw * crop.right / 100;
        projection.up -= ph * crop.top / 100;
        projection.down += ph * crop.bottom / 100;
        settings.stabilization_crop = 0;
        settings.framing_x = c.offsetX;
        settings.framing_y = c.offsetY;
        settings.mirror_edges = c.mirror;
        settings.edge_reserve = {true, c.reserve.left, c.reserve.right, c.reserve.top,
                                 c.reserve.bottom};
        // Limit only after final aspect fitting; an intermediate limit would be
        // discarded below and repeat the same CPU corner search unnecessarily.
        auto warp = vrst::make_warp(frame.width, frame.height, settings, {}, projection);
        double wanted = double(c.width) / c.height;
        double source =
            (frame.width * (warp.uv_right - warp.uv_left) * (warp.output.right - warp.output.left) /
             (warp.input.right - warp.input.left)) /
            (frame.height * (warp.uv_bottom - warp.uv_top) * (warp.output.up - warp.output.down) /
             (warp.input.up - warp.input.down));
        double cx = (warp.output.left + warp.output.right) / 2,
               cy = (warp.output.up + warp.output.down) / 2,
               sx = (warp.output.right - warp.output.left),
               sy = (warp.output.up - warp.output.down);
        if (wanted < source)
            sx *= wanted / source;
        else
            sy *= source / wanted;
        warp.output = {cx - sx / 2, cx + sx / 2, cy + sy / 2, cy - sy / 2};
        // Keep feeding pose history during bypass so returning to the tuned
        // camera uses tracking from the same uninterrupted source.
        og::limit_stream_correction(warp, comparison.correction(correction, c.stabilize, GetTickCount64()),
                                    c.mirror, c.mirrorLimit);
        const auto helmet = streamHelmet.prepare(frame.visorPosition);
        const auto &color = frame.color;
        const float openOpacity = helmet.ready && helmet.visorPosition == 2 ? helmet.opacity : 0.f;
        const float gradeStrength = og::scene_color_active(color) ? 1.f - openOpacity * .5f : 0.f;
        Constants values{
            {float(warp.input.left), float(warp.input.right), float(warp.input.up),
             float(warp.input.down)},
            {float(warp.output.left), float(warp.output.right), float(warp.output.up),
             float(warp.output.down)},
            {float(warp.correction.x), float(warp.correction.y), float(warp.correction.z),
             float(warp.correction.w)},
            {float(warp.uv_left), float(warp.uv_top), float(warp.uv_right), float(warp.uv_bottom)},
            {(frame.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
              frame.format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
              frame.format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)
                 ? 1.f
                 : 0.f,
             c.mirror ? 1.f : 0.f, float(c.mirrorLimit / 100), gradeStrength},
            {float(c.mirrorSoftness / 100),
             1.f / float(frame.width * (warp.uv_right - warp.uv_left)),
             1.f / float(frame.height * (warp.uv_bottom - warp.uv_top)),
             float(c.mirrorStretch / 100)},
            {color.contrast * .01f, color.brightness * .01f, color.saturation * .01f, 100.f / color.gamma}};
        context->UpdateSubresource(constants.Get(), 0, nullptr, &values, 0, 0);
        D3D11_TEXTURE2D_DESC canvasDesc{};
        if (canvas)
            canvas->GetDesc(&canvasDesc);
        if (!canvas || canvasDesc.Width != c.width || canvasDesc.Height != c.height) {
            ComPtr<ID3D11Texture2D> newCanvas;
            ComPtr<ID3D11RenderTargetView> newTarget;
            ComPtr<ID3D11ShaderResourceView> newView;
            canvasDesc = {};
            canvasDesc.Width = c.width;
            canvasDesc.Height = c.height;
            canvasDesc.MipLevels = canvasDesc.ArraySize = 1;
            canvasDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            canvasDesc.SampleDesc.Count = 1;
            canvasDesc.Usage = D3D11_USAGE_DEFAULT;
            canvasDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(device->CreateTexture2D(&canvasDesc, nullptr, &newCanvas)) ||
                FAILED(device->CreateRenderTargetView(newCanvas.Get(), nullptr, &newTarget)) ||
                FAILED(device->CreateShaderResourceView(newCanvas.Get(), nullptr, &newView)))
                return false;
            canvas = std::move(newCanvas);
            canvasTarget = std::move(newTarget);
            canvasView = std::move(newView);
        }
        ID3D11ShaderResourceView *empty = nullptr;
        context->PSSetShaderResources(0, 1, &empty);
        // The warp triangle writes every canvas pixel, including black outside the eye.
        D3D11_VIEWPORT canvasViewport{0, 0, float(c.width), float(c.height), 0, 1};
        context->RSSetViewports(1, &canvasViewport);
        context->OMSetRenderTargets(1, canvasTarget.GetAddressOf(), nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(), nullptr, 0);
        context->PSSetShader(ps.Get(), nullptr, 0);
        context->PSSetConstantBuffers(0, 1, constants.GetAddressOf());
        context->PSSetSamplers(0, 1, sampler.GetAddressOf());
        context->PSSetShaderResources(0, 1, view.GetAddressOf());
        context->Draw(3, 0);
        // Captured VR quad geometry follows the exact same crop/stabilization
        // transform as the raw eye image, before display letterboxing.
        overlays.draw(canvasTarget.Get(), c.width, c.height, frame, warp, streamDepth.view());
        // The visor covers dashboard pixels after the dashboard follows the eye warp.
        streamHelmet.drawPrepared(canvasTarget.Get(), c.width, c.height, local.Get());
        return presentCanvas(window);
    }
    bool presentCanvas(HWND window) {
        if (!target || !canvasView) return false;
        // Present the configured-resolution canvas without stretching it to
        // the window or capture-card display's aspect ratio.
        ID3D11ShaderResourceView *empty = nullptr;
        context->PSSetShaderResources(0, 1, &empty);
        RECT rect{};
        GetClientRect(window, &rect);
        if (rect.right <= 0 || rect.bottom <= 0) return false;
        float width = float(rect.right), height = float(rect.bottom);
        D3D11_TEXTURE2D_DESC desc{};
        canvas->GetDesc(&desc);
        if (uint64_t(rect.right) * desc.Height != uint64_t(rect.bottom) * desc.Width) {
            const float black[4]{0, 0, 0, 1};
            context->ClearRenderTargetView(target.Get(), black);
        }
        const float aspect = float(desc.Width) / desc.Height;
        float ow = std::min(width, height * aspect), oh = ow / aspect;
        D3D11_VIEWPORT viewport{(width - ow) / 2, (height - oh) / 2, ow, oh, 0, 1};
        context->RSSetViewports(1, &viewport);
        context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(), nullptr, 0);
        context->PSSetShader(presentShader.Get(), nullptr, 0);
        context->PSSetSamplers(0, 1, sampler.GetAddressOf());
        context->PSSetShaderResources(0, 1, canvasView.GetAddressOf());
        context->Draw(3, 0);
        return true;
    }
};
Renderer *active{};
bool fullscreen{};
bool paused{};
RECT displayBounds{}, windowedBounds{};
RECT cloneBounds{};
std::wstring cloneIdentity;
og::OutputActivity currentOutput=og::OutputActivity::Wait;
class PlayerActivity {
    og::telemetry::MappedMemory memory,lock;
    og::telemetry::IRacingDecoder ir;
    og::telemetry::LmuDecoder lmu;
    og::telemetry::Tracker tracker;
    uint64_t nextPoll{},lastReopen{};
    bool driving{};
public:
    bool read(og::telemetry::Game game,uint64_t now) {
        if(now<nextPoll)return driving;
        nextPoll=now+200;
        using namespace og::telemetry;
        if(!memory.opened()){
            if(!memory.open(mapping_name(game,{}))){nextPoll=now+500;return driving=false;}
            if(game==Game::Lmu)lock.open(lock_name({}));
            lastReopen=now;
        }
        const auto sample=game==Game::Lmu?lmu.read(memory,lock.opened()?&lock:nullptr):ir.read(memory);
        const auto record=tracker.observe(game,sample,now,now);
        driving=record.connected&&!record.stale&&record.data.inCar.value_or(false);
        if(!record.connected || (record.stale && now-lastReopen>=5000)){
            tracker.reset(); memory.close(); lock.close(); driving=false;
        }
        return driving;
    }
};
LONG_PTR windowedStyle{};
bool parse_display(const std::wstring &text, RECT &bounds) {
    std::wistringstream in(text);
    long x, y, w, h;
    wchar_t a, b, c;
    if (!(in >> x >> a >> y >> b >> w >> c >> h) || a != L',' || b != L',' || c != L',')
        return false;
    in >> std::ws;
    if (!in.eof() || x < -131072 || x > 131072 || y < -131072 || y > 131072 || w < 64 ||
        w > 32768 || h < 64 || h > 32768)
        return false;
    bounds = {x, y, x + w, y + h};
    return true;
}
void set_fullscreen(HWND window, bool enabled) {
    if (enabled == fullscreen)
        return;
    if (enabled) {
        GetWindowRect(window, &windowedBounds);
        windowedStyle = GetWindowLongPtrW(window, GWL_STYLE);
        RECT target = displayBounds;
        if (target.right <= target.left) {
            MONITORINFO monitor{sizeof(MONITORINFO)};
            if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
                return;
            target = monitor.rcMonitor;
        }
        SetWindowLongPtrW(window, GWL_STYLE, windowedStyle & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(window, HWND_TOP, target.left, target.top, target.right - target.left,
                     target.bottom - target.top, SWP_FRAMECHANGED | SWP_NOACTIVATE);
    } else {
        SetWindowLongPtrW(window, GWL_STYLE, windowedStyle);
        SetWindowPos(window, nullptr, windowedBounds.left, windowedBounds.top,
                     windowedBounds.right - windowedBounds.left,
                     windowedBounds.bottom - windowedBounds.top,
                     SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    fullscreen = enabled;
}
std::wstring waiting = L"Waiting for OpenGrid XR frames\nStart iRacing or Le "
                       L"Mans Ultimate in OpenXR mode.";
std::optional<og::MonitorClone> monitorClone;
void transitionOutput(HWND window,og::OutputActivity mode) {
    if(mode==currentOutput)return;
    const auto style=GetWindowLongPtrW(window,GWL_EXSTYLE);
    if(mode==og::OutputActivity::Transparent){
        SetWindowLongPtrW(window,GWL_EXSTYLE,style|WS_EX_LAYERED|WS_EX_TRANSPARENT);
        SetLayeredWindowAttributes(window,0,0,LWA_ALPHA);
    }else if(currentOutput==og::OutputActivity::Transparent){
        SetWindowLongPtrW(window,GWL_EXSTYLE,style&~(WS_EX_LAYERED|WS_EX_TRANSPARENT));
    }
    og::selectMonitorCapture(monitorClone,mode);
    currentOutput=mode;
    InvalidateRect(window,nullptr,TRUE);
}
LRESULT CALLBACK wndproc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_SETCURSOR && fullscreen && LOWORD(lp) == HTCLIENT) {
        SetCursor(nullptr);
        return TRUE;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    if(msg==WM_DISPLAYCHANGE && monitorClone){
        monitorClone.emplace();
        InvalidateRect(w,nullptr,FALSE);
    }
    if (msg == WM_SIZE) {
        if (active && active->swap && wp != SIZE_MINIMIZED)
            active->resize();
        return 0;
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(w, &paint);
        RECT r;
        GetClientRect(w, &r);
        if (monitorClone && monitorClone->paint(dc,r,cloneBounds,w,cloneIdentity)) {
            EndPaint(w,&paint);return 0;
        }
        if (currentOutput==og::OutputActivity::Scene && active && active->presentCanvas(w)) {
            active->swap->Present(0, 0);
            EndPaint(w, &paint);
            return 0;
        }
        HBRUSH brush = CreateSolidBrush(RGB(19, 19, 24));
        FillRect(dc, &r, brush);
        DeleteObject(brush);
        SetTextColor(dc, RGB(206, 199, 235));
        SetBkMode(dc, TRANSPARENT);
        r.top = r.bottom / 2 - 24;
        DrawTextW(dc, currentOutput==og::OutputActivity::Clone ? L"Waiting for monitor capture. Move Clean Output off the monitor being cloned." : waiting.c_str(), -1, &r, DT_CENTER | DT_WORDBREAK);
        EndPaint(w, &paint);
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        if (fullscreen)
            set_fullscreen(w, false);
        else
            DestroyWindow(w);
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_F11) {
        set_fullscreen(w, !fullscreen);
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == 'R' && !(lp & (1LL << 30))) {
        if (active) active->upright.reset();
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == 'B' && !(lp & (1LL << 30))) {
        if (active) active->comparison.toggle(GetTickCount64());
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == 'P' && !(lp & (1LL << 30))) {
        paused = !paused;
        SetWindowTextW(w, paused ? L"OpenGrid XR - Clean Output (paused)" : L"OpenGrid XR - Clean Output");
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc{};
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string game = "iracing";
    wchar_t module[32768];
    GetModuleFileNameW(nullptr, module, 32768);
    std::wstring path = (std::filesystem::path(module).parent_path() / L"OpenGridXR.ini").wstring();
    bool selftest = false, startFullscreen = false, framingStdin = false;
    auto idleOutput=og::OutputActivity::Wait,outsideOutput=og::OutputActivity::Wait;
    bool cockpitOnly=false;
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--config" && i + 1 < argc)
            path = argv[++i];
        else if (arg == L"--game" && i + 1 < argc) {
            std::wstring g = argv[++i];
            game = g == L"iracing" ? "iracing" : g == L"lmu" ? "lmu" : "";
        } else if (arg == L"--self-test")
            selftest = true;
        else if (arg == L"--framing-stdin")
            framingStdin = true;
        else if (arg == L"--fullscreen")
            startFullscreen = true;
        else if ((arg==L"--idle-output" || arg==L"--outside-car") && i+1<argc) {
            const auto mode=og::outputActivity(argv[++i]);
            if(!mode){LocalFree(argv);return 2;}
            if(arg==L"--outside-car"){outsideOutput=*mode;cockpitOnly=true;}else idleOutput=*mode;
        }else if(arg==L"--clone-display" && i+1<argc){
            if(!parse_display(argv[++i],cloneBounds)){LocalFree(argv);return 2;}
        }
        else if (arg == L"--display" && i + 1 < argc) {
            if (!parse_display(argv[++i], displayBounds)) {
                LocalFree(argv);
                return 2;
            }
        } else {
            LocalFree(argv);
            return 2;
        }
    }
    LocalFree(argv);
    if(cloneBounds.right>cloneBounds.left)cloneIdentity=og::monitorIdentity(cloneBounds);
    if (game != "iracing" && game != "lmu")
        return 2;
    auto config = og::read_config(path, game);
    auto savedConfig = config;
    og::OutputFramingReload framingReload(path, game);
    og::OutputFramingPreview framingPreview(framingStdin ? GetStdHandle(STD_INPUT_HANDLE) : nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = instance;
    wc.lpszClassName = L"OpenGridXR.CleanOutput";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    RECT r{0, 0, LONG(std::min(config.width, 1280u)), LONG(std::min(config.height, 720u))};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window = CreateWindowW(wc.lpszClassName, L"OpenGrid XR - Clean Output",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                                r.bottom - r.top, nullptr, nullptr, instance, nullptr);
    if (!window)
        return 3;
    ShowWindow(window, selftest ? SW_HIDE : show);
    if (startFullscreen && !selftest)
        set_fullscreen(window, true);
    og::Channel channel;
    if (!channel.open(game, 0) && !channel.pending())
        return 4;
    Renderer renderer;
    active = &renderer;
    uint64_t start = GetTickCount64(), lastFrame{};
    og::StreamCadence drawCadence;
    uint64_t lastNotification{};
    bool wasLive = false, ownsChannel = false;
    PlayerActivity playerActivity;
    const auto telemetryGame=game=="lmu"?og::telemetry::Game::Lmu:og::telemetry::Game::IRacing;
    uint64_t nextClonePaint{}, lastSourceBeat{};
    uint64_t outsideSince{};
    int result = 0;
    while (true) {
        MSG msg{};
        bool quit = false;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (quit)
            break;
        auto now = GetTickCount64();
        if (paused && !cockpitOnly && idleOutput==og::OutputActivity::Wait) {
            // Leave the last composed canvas on screen. Releasing this viewer
            // stops on-demand eye copies and lets another consumer attach.
            if (ownsChannel && channel.lock()) {
                auto *f = channel.data();
                if (f->consumer == GetCurrentProcessId()) {
                    f->consumer = 0;
                    f->consumerBeat = 0;
                }
                ownsChannel = false;
                channel.unlock();
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
            continue;
        }
        // Disk checks belong to this output process, outside the game channel
        // lock. Keep session geometry and the renderer's pose reference intact.
        const bool restoreSavedFraming = framingPreview.poll(now);
        framingReload.poll(savedConfig, now, restoreSavedFraming);
        framingPreview.apply(savedConfig, config);
        const bool driving=cockpitOnly && playerActivity.read(telemetryGame,now);
        const bool requestFrames=!cockpitOnly || driving;
        bool newFrame = false;
        if (channel.ready(now) && channel.lock()) {
            auto *f = channel.data();
            lastSourceBeat=f->sourceBeat;
            if(!requestFrames){
                if(f->consumer==GetCurrentProcessId()){f->consumer=0;f->consumerBeat=0;}
                ownsChannel=false;
            }else if (!f->consumer || f->consumer == GetCurrentProcessId() ||
                now - f->consumerBeat > 3000) {
                f->consumer = GetCurrentProcessId();
                ownsChannel = true;
                f->consumerBeat = now;
                f->eye = config.eye;
                f->fps = config.fps;
                if (f->ready && now - f->published < 3000) {
                    if (!renderer.device ||
                        std::memcmp(&renderer.adapter, &f->adapter, sizeof(LUID))) {
                        renderer = Renderer{};
                        if (!renderer.init(window, f->adapter, config.mirror, game))
                            waiting = L"Graphics initialization failed. Check that the game "
                                      L"and output use the same adapter.";
                    }
                    if (renderer.consume(*f)) {
                        lastFrame = now;
                        newFrame = true;
                    }
                }
            } else {
                ownsChannel = false;
                waiting = L"Another OpenGrid XR output window owns this game channel.";
            }
            channel.unlock();
        }
        bool live = lastFrame && now - lastFrame < 3000;
        if(!requestFrames){lastFrame=0;live=lastSourceBeat && now-lastSourceBeat<3000;}
        if(live && cockpitOnly && !driving){ if(!outsideSince)outsideSince=now; }
        else outsideSince=0;
        const auto mode=og::chooseOutput(live,idleOutput,cockpitOnly,driving,outsideOutput,now-start,
                                         outsideSince ? now-outsideSince : 0);
        if(mode==og::OutputActivity::Close)break;
        if(mode==og::OutputActivity::Wait && live && cockpitOnly) waiting=L"Waiting for the player to enter the car.";
        else if(!live && wasLive) waiting=L"Waiting for OpenGrid XR frames.";
        transitionOutput(window,mode);
        if(mode==og::OutputActivity::Clone && now>=nextClonePaint){
            InvalidateRect(window,nullptr,FALSE);
            nextClonePaint=now+1000/config.fps;
        }
        const auto drawTime = og::streamSeconds();
        // Capture is already rate-limited by the producer. Present a new frame on
        // arrival. Keep preview edits and overlays responsive when capture stalls.
        const auto fps = std::max(30u, config.fps);
        const bool stalled = now - lastFrame >= (2000u + fps - 1) / fps;
        if (mode==og::OutputActivity::Scene && !paused && (newFrame || (stalled && drawCadence.due(drawTime)))) {
            if (renderer.render(window, config))
                renderer.swap->Present(0, 0);
            drawCadence.next = drawTime + 1.0 / fps;
        }
        if (wasLive && !live) {
            renderer = Renderer{};
            InvalidateRect(window, nullptr, TRUE);
        }
        wasLive = live;
        if (selftest && now - start > 250 && (!channel.pending() || now - start >= 3000)) {
            result = channel.data() ? 0 : 5;
            break;
        }
        HANDLE wake = ownsChannel ? channel.frameEvent() : nullptr;
        // Older layers do not signal publication. Keep their polling fallback
        // until we observe a notification from the active producer.
        const DWORD timeout = !requestFrames ? (mode==og::OutputActivity::Clone ? std::max(1u,1000u/fps) : 100u)
            : lastNotification && now - lastNotification < 500
            ? std::max(1u, 1000u / fps) : 2;
        if (MsgWaitForMultipleObjectsEx(wake ? 1 : 0, wake ? &wake : nullptr, timeout,
                                        QS_ALLINPUT, MWMO_INPUTAVAILABLE) == WAIT_OBJECT_0 && wake)
            lastNotification = GetTickCount64();
    }
    if (channel.lock()) {
        auto *f = channel.data();
        if (f->consumer == GetCurrentProcessId()) {
            f->consumer = 0;
            f->consumerBeat = 0;
        }
        channel.unlock();
    }
    active = nullptr;
    if (IsWindow(window))
        DestroyWindow(window);
    return result;
}
