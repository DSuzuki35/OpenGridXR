#include "channel.hpp"
#include "core.hpp"
#include "overlay_geometry.hpp"
#include "overlay_transport.hpp"
#include "stream_overlay_renderer.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Texture {
    ComPtr<ID3D11Texture2D> image;
    ComPtr<IDXGIKeyedMutex> key;
    HANDLE handle{};
    ~Texture() {
        if (handle)
            CloseHandle(handle);
    }
};
og::CapturedOverlay panel() {
    og::CapturedOverlay p{};
    p.valid = 1;
    p.sourceRevision = 1;
    p.pixelWidth = 32;
    p.pixelHeight = 16;
    for (unsigned i = 0; i < 4; ++i) {
        p.corners[i][0] = (i & 1) ? .4f : -.4f;
        p.corners[i][1] = (i & 2) ? -.2f : .2f;
        p.corners[i][2] = -1;
    }
    return p;
}
int listen(ID3D11Device *device, ID3D11DeviceContext *context, unsigned seconds) {
    og::StreamOverlayRenderer renderer;
    check(renderer.initialize(device, context, "test"), "listener initializes");
    og::OverlayChannel poses;
    check(poses.open("test"), "listener pose mapping");
    og::SharedFrame captured{};
    vrst::Warp warp{};
    D3D11_TEXTURE2D_DESC d{};
    d.Width = d.Height = 100;
    d.ArraySize = d.MipLevels = d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target, read;
    ComPtr<ID3D11RenderTargetView> rtv;
    check(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &target)) &&
              SUCCEEDED(device->CreateRenderTargetView(target.Get(), nullptr, &rtv)),
          "listener canvas");
    d.BindFlags = 0;
    d.Usage = D3D11_USAGE_STAGING;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &read)), "listener readback");
    std::cout << "{\"op\":\"stream-fixture\",\"ready\":true}" << std::endl;
    auto end = GetTickCount64() + seconds * 1000;
    unsigned frame = 0;
    while (GetTickCount64() < end) {
        float bg[]{0, 0, 0, 1};
        context->ClearRenderTargetView(rtv.Get(), bg);
        captured.overlays = {};
        // The listener's synthetic raw camera and receiver spaces are identity.
        // Production instead gets these corners atomically with captured pixels.
        if (poses.lock()) {
            auto snapshot = *poses.data();
            poses.unlock();
            captured.producer = snapshot.consumerPid;
            captured.overlaySession = snapshot.session;
            captured.overlayConsumerPid = snapshot.consumerPid;
            captured.overlayProducerPid = snapshot.producerPid;
            XrPosef identity{{0, 0, 0, 1}, {}};
            for (unsigned i = 0; i < 15; ++i)
                if (snapshot.slots[i].pose.valid)
                    og::capture_overlay_corners(snapshot.slots[i].pose, identity, identity,
                                                captured.overlays[i]);
        }
        renderer.draw(rtv.Get(), 100, 100, captured, warp);
        if ((frame++ % 30) == 0) {
            context->CopyResource(read.Get(), target.Get());
            D3D11_MAPPED_SUBRESOURCE m{};
            check(SUCCEEDED(context->Map(read.Get(), 0, D3D11_MAP_READ, 0, &m)),
                  "listener readback maps");
            auto at = [&](unsigned x, unsigned y) {
                return reinterpret_cast<uint32_t *>(static_cast<char *>(m.pData) +
                                                    y * m.RowPitch)[x];
            };
            std::cout << "{\"op\":\"stream-frame\",\"pixelCenter\":" << at(50, 50)
                      << ",\"pixelDefault\":" << at(50, 75) << ",\"pixelCorner\":" << at(0, 0)
                      << "}" << std::endl;
            context->Unmap(read.Get(), 0);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    renderer.reset();
    return 0;
}
int main(int argc, char **argv) {
    try {
        std::string wanted;
        bool listener = false;
        unsigned seconds = 30;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--listen")
                listener = true;
            else if (arg == "--seconds" && i + 1 < argc) {
                auto value = std::stoul(argv[++i]);
                check(value >= 1 && value <= 300, "listener duration1..300s");
                seconds = unsigned(value);
            } else if (arg == "--adapter" && i + 1 < argc)
                wanted = argv[++i];
            else if (arg == "NVIDIA")
                wanted = arg;
            else
                throw std::runtime_error("invalid test argument");
        }
        ComPtr<IDXGIFactory1> factory;
        check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))), "DXGI factory");
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> a;
            if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND)
                break;
            DXGI_ADAPTER_DESC1 d{};
            a->GetDesc1(&d);
            std::string name;
            for (auto c : d.Description) {
                if (!c)
                    break;
                name += c < 128 ? char(c) : '?';
            }
            if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                (wanted.empty() || name.find(wanted) != std::string::npos)) {
                adapter = a;
                std::cout << "Adapter: " << name << '\n';
                break;
            }
        }
        check(bool(adapter), "requested hardware adapter available");
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        check(SUCCEEDED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                          D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                          D3D11_SDK_VERSION, &device, nullptr, &context)),
              "D3D11 device");
        if (listener)
            return listen(device.Get(), context.Get(), seconds);
        og::OverlayChannel channel;
        check(channel.open("test") && channel.lock(1000), "isolated test mapping");
        *channel.data() = og::OverlayShared{};
        channel.data()->producerPid = GetCurrentProcessId();
        channel.data()->producerBeat = GetTickCount64();
        channel.data()->session = 1;
        channel.unlock();
        std::array<std::array<Texture, 2>, 2> textures;
        for (unsigned slot = 0; slot < 2; ++slot)
            for (unsigned index = 0; index < 2; ++index) {
                auto &t = textures[slot][index];
                D3D11_TEXTURE2D_DESC d{};
                d.Width = 32;
                d.Height = 16;
                d.ArraySize = d.MipLevels = d.SampleDesc.Count = 1;
                d.Format = slot ? DXGI_FORMAT_B8G8R8A8_TYPELESS : DXGI_FORMAT_R8G8B8A8_TYPELESS;
                d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                d.MiscFlags =
                    D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
                check(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &t.image)) &&
                          SUCCEEDED(t.image.As(&t.key)),
                      "shared source allocation");
                ComPtr<IDXGIResource1> r;
                t.image.As(&r);
                check(SUCCEEDED(r->CreateSharedHandle(
                          nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                          &t.handle)),
                      "shared source handle");
            }
        auto publish = [&](unsigned slot, unsigned index, uint64_t frame, uint32_t color,
                           bool ramp = false) {
            auto &t = textures[slot][index];
            check(t.key->AcquireSync(0, 1000) == S_OK, "producer owns independent stream key");
            std::vector<uint32_t> pixels(32 * 16, color);
            if (ramp)
                for (unsigned y = 0; y < 16; ++y)
                    for (unsigned x = 0; x < 32; ++x)
                        pixels[y * 32 + x] = 0xff000000 | uint32_t(std::lround(x * 255. / 31));
            context->UpdateSubresource(t.image.Get(), 0, nullptr, pixels.data(), 32 * 4, 0);
            context->Flush();
            t.key->ReleaseSync(1);
            check(channel.lock(1000), "publish lock");
            auto &s = channel.data()->slots[slot];
            s.streamCurrent = index;
            s.streamFrames[index] = {
                uint64_t(slot * 2 + index + 1),
                reinterpret_cast<uint64_t>(t.handle),
                frame,
                32,
                16,
                uint32_t(slot ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM)};
            channel.data()->producerBeat = GetTickCount64();
            channel.unlock();
        };
        auto edit = [&](auto fn) {
            check(channel.lock(1000), "configure lock");
            fn(*channel.data());
            channel.unlock();
        };
        edit([](auto &d) {
            for (unsigned i = 0; i < 2; ++i) {
                auto &s = d.slots[i];
                s.enabled = 1;
                s.opacity = 1;
                s.sourceRevision = 1;
            }
        });
        D3D11_TEXTURE2D_DESC d{};
        d.Width = d.Height = 100;
        d.ArraySize = d.MipLevels = d.SampleDesc.Count = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target, read;
        ComPtr<ID3D11RenderTargetView> rtv;
        check(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &target)) &&
                  SUCCEEDED(device->CreateRenderTargetView(target.Get(), nullptr, &rtv)),
              "canvas allocation");
        d.BindFlags = 0;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &read)), "test readback allocation");
        og::StreamOverlayRenderer renderer;
        check(renderer.initialize(device.Get(), context.Get(), "test"),
              "stream renderer initializes");
        og::SharedFrame captured{};
        captured.overlays[0] = panel();
        captured.overlays[1] = panel();
        vrst::Warp warp{};
        captured.overlaySession = 1;
        captured.overlayProducerPid = GetCurrentProcessId();
        auto render = [&](bool blue = false) {
            float bg[]{0, 0, blue ? 1.f : 0.f, 1};
            context->ClearRenderTargetView(rtv.Get(), bg);
            renderer.draw(rtv.Get(), 100, 100, captured, warp);
            context->CopyResource(read.Get(), target.Get());
            D3D11_MAPPED_SUBRESOURCE m{};
            check(SUCCEEDED(context->Map(read.Get(), 0, D3D11_MAP_READ, 0, &m)), "readback maps");
            std::vector<uint32_t> result(10000);
            for (unsigned y = 0; y < 100; ++y)
                memcpy(result.data() + y * 100, static_cast<char *>(m.pData) + y * m.RowPitch, 400);
            context->Unmap(read.Get(), 0);
            return result;
        };
        auto pixel = [](const auto &p, unsigned x = 50, unsigned y = 50) { return p[y * 100 + x]; };
        publish(0, 0, 1, 0xff0000ff);
        publish(1, 0, 1, 0xff00ff00);
        check(pixel(render()) == 0xff000000, "private overlays are never composed");
        edit([](auto &d) { d.slots[0].showInStream = 1; });
        auto pixels = render();
        check(pixel(pixels) == 0xff0000ff, "selected RGBA source appears");
        check(pixel(pixels, 29, 50) == 0xff000000 && pixel(pixels, 30, 40) == 0xff0000ff &&
                  pixel(pixels, 69, 59) == 0xff0000ff && pixel(pixels, 70, 50) == 0xff000000 &&
                  pixel(pixels, 50, 60) == 0xff000000,
              "captured VR geometry retains physical aspect and exact projected edges");
        auto original = panel();
        captured.overlays[0].corners[0][2] = captured.overlays[0].corners[2][2] = -.5f;
        captured.overlays[0].corners[1][2] = captured.overlays[0].corners[3][2] = -1.5f;
        pixels = render();
        check(pixel(pixels, 11, 32) == 0xff0000ff && pixel(pixels, 62, 45) == 0xff0000ff &&
                  pixel(pixels, 62, 35) == 0xff000000 && pixel(pixels, 64, 50) == 0xff000000,
              "slanted VR panel uses depth-dependent perspective silhouette");
        publish(0, 1, 2, 0, true);
        auto perspectivePixel = pixel(render());
        check((perspectivePixel & 255) >= 128 && (perspectivePixel & 255) <= 135,
              "slanted panel texture coordinates interpolate in perspective, not screen space");
        publish(0, 0, 3, 0xff0000ff);
        captured.overlays[0] = original;
        for (auto &c : captured.overlays[0].corners) {
            auto v = vrst::rotate(vrst::axis_angle({0, 0, 1}, vrst::pi / 2), {c[0], c[1], c[2]});
            c[0] = float(v.x);
            c[1] = float(v.y);
            c[2] = float(v.z);
        }
        pixels = render();
        check(pixel(pixels, 50, 31) == 0xff0000ff && pixel(pixels, 31, 50) == 0xff000000,
              "VR panel roll is preserved");
        captured.overlays[0] = original;
        for (auto &c : captured.overlays[0].corners)
            c[0] += .4f;
        warp.correction = vrst::axis_angle({0, 0, 1}, vrst::pi / 2);
        pixels = render();
        check(pixel(pixels, 50, 85) == 0xff0000ff && pixel(pixels, 50, 15) == 0xff000000 &&
                  pixel(pixels, 85, 50) == 0xff000000,
              "quad follows inverse of the game shader's stabilization correction");
        warp = {};
        captured.overlays[0] = original;
        captured.fov[0] = -2;
        captured.fov[1] = 1;
        captured.fov[2] = 1.5f;
        captured.fov[3] = -.5f;
        warp.input = {-1.4, .7, 1.3, -.1};
        warp.output = warp.input;
        warp.uv_left = .2;
        warp.uv_right = .9;
        warp.uv_top = .1;
        warp.uv_bottom = .8;
        for (unsigned i = 0; i < 4; ++i) {
            auto &c = captured.overlays[0].corners[i];
            c[0] = (i & 1) ? .21f : -.21f;
            c[1] = .3f + ((i & 2) ? -.14f : .14f);
        }
        pixels = render();
        check(pixel(pixels, 67, 71) == 0xff0000ff && pixel(pixels, 55, 71) == 0xff000000 &&
                  pixel(pixels, 78, 71) == 0xff000000 && pixel(pixels, 67, 59) == 0xff000000,
              "expanded asymmetric eye FOV and nonuniform crop match final game framing");
        captured.fov[0] = captured.fov[3] = -1;
        captured.fov[1] = captured.fov[2] = 1;
        warp = {};
        captured.overlays[0] = original;
        for (auto &c : captured.overlays[0].corners)
            c[2] = 1;
        check(pixel(render()) == 0xff000000, "panel entirely behind eye is clipped");
        captured.overlays[0] = original;
        for (auto &c : captured.overlays[0].corners)
            c[0] = -c[0];
        check(pixel(render()) == 0xff000000, "back face is hidden like the VR quad");
        captured.overlays[0] = original;
        for (unsigned i : {0u, 2u}) {
            captured.overlays[0].corners[i][0] = -.2f;
            captured.overlays[0].corners[i][2] = .1f;
        }
        pixels = render();
        check(pixel(pixels, 10, 50) == 0xff0000ff && pixel(pixels, 90, 50) == 0xff000000,
              "quad crossing near plane clips with homogeneous coordinates");
        captured.overlays[0] = original;
        captured.overlays[0].pixelWidth = 64;
        check(pixel(render()) == 0xff000000,
              "geometry with a mismatched source aspect is rejected");
        captured.overlays[0] = original;
        D3D11_VIEWPORT previousViewport{7, 8, 17, 19, .2f, .8f};
        context->RSSetViewports(1, &previousViewport);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
        context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
        check(pixel(render()) == 0xff0000ff, "overlay draw ignores caller viewport and topology");
        UINT viewportCount = 1;
        D3D11_VIEWPORT restored{};
        context->RSGetViewports(&viewportCount, &restored);
        D3D11_PRIMITIVE_TOPOLOGY topology{};
        context->IAGetPrimitiveTopology(&topology);
        ComPtr<ID3D11RenderTargetView> restoredTarget;
        context->OMGetRenderTargets(1, &restoredTarget, nullptr);
        check(viewportCount == 1 && memcmp(&restored, &previousViewport, sizeof(restored)) == 0 &&
                  topology == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP &&
                  restoredTarget.Get() == rtv.Get(),
              "caller pipeline state restored for the display pass");
        context->OMSetRenderTargets(0, nullptr, nullptr);
        edit([](auto &d) {
            d.streamConsumerPid = GetCurrentProcessId() + 1;
            d.streamConsumerBeat = GetTickCount64();
        });
        check(pixel(render()) == 0xff000000, "another live output consumer owns stream ring");
        edit([](auto &d) { d.streamConsumerPid = 0; });
        check(pixel(render()) == 0xff0000ff, "consumer ownership recovery reads static pixels");
        std::promise<void> locked, release;
        auto unlock = release.get_future();
        std::thread blocker([&] {
            check(channel.lock(1000), "fixture holds metadata lock");
            locked.set_value();
            unlock.wait();
            channel.unlock();
        });
        locked.get_future().wait();
        auto start = std::chrono::steady_clock::now();
        auto busyPixels = render();
        auto elapsed = std::chrono::steady_clock::now() - start;
        release.set_value();
        blocker.join();
        check(pixel(busyPixels) == 0xff000000 && elapsed < std::chrono::milliseconds(200),
              "metadata contention skips without waiting on producer");
        check(pixel(render()) == 0xff0000ff, "panel resumes after metadata contention");
        edit([](auto &d) { d.slots[0].opacity = .5f; });
        auto color = pixel(render(true));
        check((color & 255) >= 127 && (color & 255) <= 128 && ((color >> 16) & 255) >= 127 &&
                  ((color >> 16) & 255) <= 128 && (color >> 24) == 255,
              "opacity blends premultiplied encoded color over game canvas");
        publish(0, 1, 4, 0x80000080);
        color = pixel(render(true));
        check((color & 255) >= 63 && (color & 255) <= 65 && ((color >> 16) & 255) >= 190 &&
                  ((color >> 16) & 255) <= 192 && (color >> 24) == 255,
              "source alpha is not multiplied twice");
        edit([](auto &d) {
            d.slots[0].opacity = 1;
            d.slots[1].showInStream = 1;
            d.slots[1].opacity = .5f;
        });
        color = pixel(render());
        check(((color >> 8) & 255) >= 127 && (color & 255) >= 63 && (color & 255) <= 65,
              "later BGRA slot stacks above earlier transparent slot");
        edit([](auto &d) { d.slots[1].showInStream = 0; });
        for (auto &corner : captured.overlays[0].corners)
            corner[0] -= 1;
        pixels = render();
        check((pixel(pixels, 0, 50) & 255) == 128 && (pixel(pixels, 19, 50) & 255) == 128 &&
                  pixel(pixels, 20, 50) == 0xff000000,
              "partly outside canvas clips without changing aspect");
        captured.overlays[0] = panel();
        publish(0, 0, 5, 0xff00ff00);
        D3D11_QUERY_DESC predicateDesc{D3D11_QUERY_OCCLUSION_PREDICATE, 0};
        ComPtr<ID3D11Predicate> predicate;
        check(SUCCEEDED(device->CreatePredicate(&predicateDesc, &predicate)),
              "predicate allocation");
        context->Begin(predicate.Get());
        context->End(predicate.Get());
        context->Flush();
        BOOL predicateResult = TRUE;
        auto predicateDeadline = GetTickCount64() + 2000;
        HRESULT predicateStatus = S_FALSE;
        while ((predicateStatus = context->GetData(predicate.Get(), &predicateResult,
                                                   sizeof(predicateResult), 0)) == S_FALSE &&
               GetTickCount64() < predicateDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(predicateStatus == S_OK && !predicateResult,
              "empty occlusion predicate suppresses commands");
        context->SetPredication(predicate.Get(), FALSE);
        renderer.draw(rtv.Get(), 100, 100, captured, warp);
        ComPtr<ID3D11Predicate> restoredPredicate;
        BOOL restoredValue = TRUE;
        context->GetPredication(&restoredPredicate, &restoredValue);
        check(restoredPredicate.Get() == predicate.Get() && !restoredValue,
              "caller predicate restored");
        context->SetPredication(nullptr, FALSE);
        check(pixel(render()) == 0xff00ff00,
              "new ring copy is not suppressed by caller predication");
        check(pixel(render()) == 0xff00ff00,
              "static local frame persists without reacquiring producer key");
        renderer.reset();
        edit([](auto &d) {
            check(d.streamConsumerPid == 0 && d.streamConsumerBeat == 0,
                  "reset relinquishes consumer heartbeat");
        });
        check(renderer.initialize(device.Get(), context.Get(), "test"),
              "same process renderer restart");
        check(pixel(render()) == 0xff00ff00,
              "restart reads already-consumed current static frame on key zero");
        edit([](auto &d) {
            ++d.slots[0].sourceRevision;
            d.slots[0].streamFrames = {};
        });
        check(pixel(render()) == 0xff000000, "cleared source never shows cached pixels");
        publish(0, 1, 6, 0xff0000ff);
        check(pixel(render()) == 0xff000000,
              "old captured source epoch cannot show replacement content");
        captured.overlays[0].sourceRevision = 2;
        check(pixel(render()) == 0xff0000ff, "replacement source recovers after clear");
        edit([](auto &d) { d.producerPid = 0; });
        check(pixel(render()) == 0xff000000, "producer disappearance clears cached content");
        edit([](auto &d) {
            d.producerPid = GetCurrentProcessId();
            ++d.session;
        });
        check(pixel(render()) == 0xff000000,
              "old captured receiver session cannot use a new session's texture");
        ++captured.overlaySession;
        check(pixel(render()) == 0xff0000ff,
              "producer/session restart can reuse static source safely");
        edit([](auto &d) { d.slots[0].showInStream = 0; });
        check(pixel(render()) == 0xff000000, "turning stream selection off clears immediately");
        edit([](auto &d) { d.slots[0].showInStream = 1; });
        captured.overlays[0].corners[0][0] = std::numeric_limits<float>::quiet_NaN();
        check(pixel(render()) == 0xff000000, "invalid metadata cannot enter GPU placement");
        edit([](auto &d) { d.producerBeat = 0; });
        check(pixel(render()) == 0xff000000,
              "expired producer cannot retain apparently live content");
        renderer.reset();
        edit([](auto &d) {
            check(!d.streamConsumerPid && !d.streamConsumerBeat,
                  "shutdown removes consumer ownership");
            d = og::OverlayShared{};
        });
        std::cout
            << "Stream selection, placement, clipping, alpha, update, restart and cleanup passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
