#include "channel.hpp"
#include "core.hpp"
#include "settings.hpp"
#include "stabilization.hpp"
#include "stream_overlay_renderer.hpp"
#include <algorithm>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <filesystem>
#include <shellapi.h>
#include <sstream>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace {
struct Constants {
    float input[4], output[4], quat[4], uvBounds[4], flags[4];
};
const char *shader = R"(
Texture2D source:register(t0);SamplerState linearSampler:register(s0);
cbuffer Parameters:register(b0){float4 inputFov;float4 outputFov;float4 q;float4 uvBounds;float4 flags;}
struct V{float4 pos:SV_Position;float2 uv:TEXCOORD0;};
V vs(uint id:SV_VertexID){V o;o.uv=float2((id<<1)&2,id&2);o.pos=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}
float4 ps(V v):SV_Target{float3 ray=float3(lerp(outputFov.x,outputFov.y,v.uv.x),lerp(outputFov.z,outputFov.w,v.uv.y),-1);ray+=2*cross(q.xyz,cross(q.xyz,ray)+q.w*ray);if(ray.z>=-.0001)return float4(0,0,0,1);float2 uv=float2((ray.x/-ray.z-inputFov.x)/(inputFov.y-inputFov.x),(inputFov.z-ray.y/-ray.z)/(inputFov.z-inputFov.w));uv=lerp(uvBounds.xy,uvBounds.zw,uv);float3 color=source.Sample(linearSampler,uv).rgb;if(flags.x>.5)color=lerp(12.92*color,1.055*pow(max(color,0),1.0/2.4)-.055,step(.0031308,color));return float4(color,1);}
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
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Texture2D> shared, local;
    ComPtr<IDXGIKeyedMutex> key;
    ComPtr<ID3D11ShaderResourceView> view;
    uint64_t handle{}, sequence{}, session{};
    LUID adapter{};
    vrst::PoseHistory poses;
    og::UprightReference upright;
    og::SharedFrame frame{};
    og::StreamOverlayRenderer overlays;
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
        ComPtr<ID3DBlob> v, p, err;
        if (FAILED(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "vs",
                              "vs_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &v, &err)) ||
            FAILED(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "ps",
                              "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &p, &err)))
            return false;
        if (FAILED(device->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), nullptr,
                                              &vs)) ||
            FAILED(
                device->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), nullptr, &ps)))
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
            f.reference != frame.reference) {
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
        poses.set_smoothing(c.smoothing);
        vrst::Pose pose;
        pose.session = frame.session;
        pose.sequence = frame.sequence;
        pose.time_ns = frame.published * 1000000;
        pose.orientation = q;
        pose.projection = projection;
        pose.flags = vrst::TrackingValid | vrst::FramePose;
        poses.push(pose);
        auto sample = poses.sample(pose.time_ns);
        auto correction = sample.valid ? upright.correction(q, sample.target_orientation, c.horizon)
                                       : vrst::Quat{};
        vrst::Settings settings;
        settings.automatic_fov = true;
        settings.output_16_9 = false;
        settings.input_left = c.crop.left;
        settings.input_right = c.crop.right;
        settings.input_top = c.crop.top;
        settings.input_bottom = c.crop.bottom;
        double pw = projection.right - projection.left, ph = projection.up - projection.down;
        projection.left += pw * c.crop.left / 100;
        projection.right -= pw * c.crop.right / 100;
        projection.up -= ph * c.crop.top / 100;
        projection.down += ph * c.crop.bottom / 100;
        settings.stabilization_crop = 0;
        settings.framing_x = c.offsetX;
        settings.framing_y = c.offsetY;
        settings.mirror_edges = c.mirror;
        settings.edge_reserve = {true, c.reserve.left, c.reserve.right, c.reserve.top,
                                 c.reserve.bottom};
        auto warp = vrst::make_warp(frame.width, frame.height, settings, correction, projection);
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
        vrst::limit_warp_correction(warp, correction, c.mirror);
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
             0, 0, 0}};
        context->UpdateSubresource(constants.Get(), 0, nullptr, &values, 0, 0);
        D3D11_TEXTURE2D_DESC canvasDesc{};
        if (canvas)
            canvas->GetDesc(&canvasDesc);
        if (!canvas || canvasDesc.Width != c.width || canvasDesc.Height != c.height) {
            canvasView.Reset();
            canvasTarget.Reset();
            canvas.Reset();
            canvasDesc = {};
            canvasDesc.Width = c.width;
            canvasDesc.Height = c.height;
            canvasDesc.MipLevels = canvasDesc.ArraySize = 1;
            canvasDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            canvasDesc.SampleDesc.Count = 1;
            canvasDesc.Usage = D3D11_USAGE_DEFAULT;
            canvasDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(device->CreateTexture2D(&canvasDesc, nullptr, &canvas)) ||
                FAILED(device->CreateRenderTargetView(canvas.Get(), nullptr, &canvasTarget)) ||
                FAILED(device->CreateShaderResourceView(canvas.Get(), nullptr, &canvasView)))
                return false;
        }
        ID3D11ShaderResourceView *empty = nullptr;
        context->PSSetShaderResources(0, 1, &empty);
        float clear[4]{0, 0, 0, 1};
        context->ClearRenderTargetView(canvasTarget.Get(), clear);
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
        overlays.draw(canvasTarget.Get(), c.width, c.height, frame, warp);
        // Present the configured-resolution canvas without stretching it to
        // the window or capture-card display's aspect ratio.
        Constants identity{
            {-1, 1, 1, -1}, {-1, 1, 1, -1}, {0, 0, 0, 1}, {0, 0, 1, 1}, {0, 0, 0, 0}};
        context->UpdateSubresource(constants.Get(), 0, nullptr, &identity, 0, 0);
        context->PSSetShaderResources(0, 1, &empty);
        RECT rect{};
        GetClientRect(window, &rect);
        context->ClearRenderTargetView(target.Get(), clear);
        float width = float(rect.right), height = float(rect.bottom);
        float ow = std::min(width, height * float(wanted)), oh = ow / float(wanted);
        D3D11_VIEWPORT viewport{(width - ow) / 2, (height - oh) / 2, ow, oh, 0, 1};
        context->RSSetViewports(1, &viewport);
        context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(), nullptr, 0);
        context->PSSetShader(ps.Get(), nullptr, 0);
        context->PSSetConstantBuffers(0, 1, constants.GetAddressOf());
        context->PSSetSamplers(0, 1, sampler.GetAddressOf());
        context->PSSetShaderResources(0, 1, canvasView.GetAddressOf());
        context->Draw(3, 0);
        return true;
    }
};
Renderer *active{};
bool fullscreen{};
RECT displayBounds{}, windowedBounds{};
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
                       L"Mans Ultimate in OpenXR streaming mode.";
LRESULT CALLBACK wndproc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_SETCURSOR && fullscreen && LOWORD(lp) == HTCLIENT) {
        SetCursor(nullptr);
        return TRUE;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
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
        HBRUSH brush = CreateSolidBrush(RGB(19, 19, 24));
        FillRect(dc, &r, brush);
        DeleteObject(brush);
        SetTextColor(dc, RGB(206, 199, 235));
        SetBkMode(dc, TRANSPARENT);
        r.top = r.bottom / 2 - 24;
        DrawTextW(dc, waiting.c_str(), -1, &r, DT_CENTER | DT_WORDBREAK);
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
    bool selftest = false, startFullscreen = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--config" && i + 1 < argc)
            path = argv[++i];
        else if (arg == L"--game" && i + 1 < argc) {
            std::wstring g = argv[++i];
            game = g == L"iracing" ? "iracing" : g == L"lmu" ? "lmu" : "";
        } else if (arg == L"--self-test")
            selftest = true;
        else if (arg == L"--fullscreen")
            startFullscreen = true;
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
    if (game != "iracing" && game != "lmu")
        return 2;
    auto config = og::read_config(path, game);
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
    if (!channel.open(game))
        return 4;
    Renderer renderer;
    active = &renderer;
    uint64_t start = GetTickCount64(), lastFrame{}, lastDraw{};
    bool wasLive = false;
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
        if (channel.lock()) {
            auto *f = channel.data();
            if (!f->consumer || f->consumer == GetCurrentProcessId() ||
                now - f->consumerBeat > 3000) {
                f->consumer = GetCurrentProcessId();
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
                    if (renderer.consume(*f))
                        lastFrame = now;
                }
            } else
                waiting = L"Another OpenGrid XR output window owns this game channel.";
            channel.unlock();
        }
        bool live = lastFrame && now - lastFrame < 3000;
        if (live && now - lastDraw >= 1000 / std::max(30u, config.fps)) {
            if (renderer.render(window, config))
                renderer.swap->Present(0, 0);
            lastDraw = now;
        }
        if (wasLive && !live) {
            renderer = Renderer{};
            InvalidateRect(window, nullptr, TRUE);
        }
        wasLive = live;
        if (selftest && now - start > 250) {
            result = channel.data() ? 0 : 5;
            break;
        }
        Sleep(2);
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
