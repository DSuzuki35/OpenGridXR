#include "stream_helmet_renderer.hpp"
#include <array>
#include <d3d11.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
void check(bool v, const char *message) {
    if (!v)
        throw std::runtime_error(message);
}
void png(const std::filesystem::path &path, uint32_t rgba, UINT width = 16, UINT height = 9) {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&factory))),
          "WIC factory");
    check(SUCCEEDED(factory->CreateStream(&stream)) &&
              SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)),
          "PNG file");
    check(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
              SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)),
          "PNG encoder");
    check(SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
              SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(width, height)),
          "PNG frame");
    auto format = GUID_WICPixelFormat32bppBGRA;
    check(SUCCEEDED(frame->SetPixelFormat(&format)) && format == GUID_WICPixelFormat32bppBGRA,
          "PNG pixel format");
    uint32_t bgra = (rgba & 0xff00ff00u) | ((rgba & 255) << 16) | ((rgba >> 16) & 255);
    std::vector<uint32_t> pixels(size_t(width) * height, bgra);
    check(SUCCEEDED(frame->WritePixels(height, width * 4, UINT(pixels.size() * 4),
                                       reinterpret_cast<BYTE *>(pixels.data()))) &&
              SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()),
          "PNG commit");
}
struct Temp {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("opengrid-stream-helmet-test-" + std::to_string(GetCurrentProcessId()));
    Temp() { std::filesystem::create_directories(dir); }
    ~Temp() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};
int main() {
    try {
        check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "COM");
        Temp temp;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                                          D3D11_SDK_VERSION, &device, nullptr, &context)),
              "D3D11 WARP");
        og::StreamHelmetRenderer renderer;
        check(!renderer.initialize(device.Get(), context.Get(), "../iracing", temp.dir),
              "Reject unsupported game");
        check(renderer.initialize(device.Get(), context.Get(), "iracing", temp.dir),
              "Missing helmet is harmless");
        auto manifest = temp.dir / "OpenGridXR-stream-helmet-iracing.json",
             image = temp.dir / "OpenGridXR-stream-helmet-iracing-left.png";
        auto publish = [&](bool enabled, const char *generation, float opacity = 100,
                           const char *name = "OpenGridXR-stream-helmet-iracing-left.png") {
            std::ofstream out(manifest);
            out << "{\"version\":2,\"enabled\":" << (enabled ? "true" : "false")
                << ",\"generation\":\"" << generation << "\",\"leftImage\":\"" << name
                << "\",\"rightImage\":\"OpenGridXR-stream-helmet-iracing-right.png\","
                   "\"visorHotkey\":false,\"visorPosition\":\"half\",\"headMotion\":0,"
                   "\"vehicleMotion\":0,\"stereoOffset\":0,\"streamOpacity\":"
                << opacity << "}";
            out.close();
            renderer.refresh();
        };
        auto render = [&](unsigned width, unsigned height) {
            D3D11_TEXTURE2D_DESC d{};
            d.Width = width;
            d.Height = height;
            d.ArraySize = d.MipLevels = d.SampleDesc.Count = 1;
            d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            d.BindFlags = D3D11_BIND_RENDER_TARGET;
            ComPtr<ID3D11Texture2D> target, read;
            ComPtr<ID3D11RenderTargetView> view;
            check(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &target)) &&
                      SUCCEEDED(device->CreateRenderTargetView(target.Get(), nullptr, &view)),
                  "Canvas");
            d.BindFlags = 0;
            d.Usage = D3D11_USAGE_STAGING;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            check(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &read)), "Readback");
            const float blue[]{0, 0, 1, 1};
            context->ClearRenderTargetView(view.Get(), blue);
            D3D11_VIEWPORT callerViewport{3, 4, 12, 13, 0, 1};
            context->RSSetViewports(1, &callerViewport);
            renderer.draw(view.Get(), width, height);
            D3D11_VIEWPORT restored{};
            UINT count = 1;
            context->RSGetViewports(&count, &restored);
            check(count == 1 && restored.TopLeftX == 3 && restored.TopLeftY == 4 &&
                      restored.Width == 12 && restored.Height == 13,
                  "Caller viewport survives helmet pass");
            context->CopyResource(read.Get(), target.Get());
            D3D11_MAPPED_SUBRESOURCE m{};
            check(SUCCEEDED(context->Map(read.Get(), 0, D3D11_MAP_READ, 0, &m)), "Readback map");
            auto *p =
                static_cast<unsigned char *>(m.pData) + (height / 2) * m.RowPitch + (width / 2) * 4;
            std::array<int, 4> pixel{p[0], p[1], p[2], p[3]};
            context->Unmap(read.Get(), 0);
            return pixel;
        };
        check(render(64, 36) == std::array<int, 4>{0, 0, 255, 255},
              "Missing manifest leaves output untouched");
        check(renderer.status().uploads == 0 && renderer.status().draws == 0,
              "Disabled has zero uploads/draws");
        png(image, 0x800000ff);
        publish(true, "one");
        auto p = render(64, 36);
        check(abs(p[0] - 128) <= 1 && p[1] == 0 && abs(p[2] - 127) <= 1 && p[3] == 255,
              "Premultiplied red alpha blends over blue");
        check(renderer.status().uploads == 1, "One initial upload");
        publish(true, "one");
        render(192, 108);
        check(renderer.status().uploads == 1, "Resize and same config reuse texture");
        publish(true, "one", 50);
        p = render(64, 36);
        check(abs(p[0] - 64) <= 1 && abs(p[2] - 191) <= 1,
              "Global opacity affects complete premultiplied image");
        check(renderer.status().uploads == 1, "Opacity-only config reuses texture");
        publish(true, "one", 1e-7f);
        check(renderer.status().ready, "Finite JSON exponent opacity accepted");
        check(renderer.status().uploads == 1, "Tiny opacity retains cached texture");
        png(image, 0x8000ff00, 32, 18);
        publish(true, "two");
        p = render(80, 80);
        check(p[0] == 0 && abs(p[1] - 128) <= 1 && abs(p[2] - 127) <= 1,
              "New generation and source size reload");
        check(renderer.status().uploads == 2, "One upload per generation");
        publish(false, "three");
        auto draws = renderer.status().draws;
        check(render(64, 36) == std::array<int, 4>{0, 0, 255, 255}, "Disable clears stale image");
        check(renderer.status().draws == draws, "Disabled never draws");
        publish(true, "four", 100, "../OpenGridXR-helmet-iracing-left.png");
        check(!renderer.status().ready, "Reject paths outside fixed stream asset name");
        publish(true, "five");
        check(renderer.status().ready, "Valid manifest recovers");
        std::ofstream(manifest) << "{\"version\":2,\"enabled\":true,\"enabled\":false}";
        renderer.refresh();
        check(render(64, 36) == std::array<int, 4>{0, 0, 255, 255},
              "Malformed config removes stale image");
        publish(true, "six");
        std::filesystem::remove(manifest);
        renderer.refresh();
        check(!renderer.status().ready, "Deleted manifest removes stale image");
        std::ofstream(image, std::ios::binary) << "not a PNG";
        publish(true, "seven");
        check(!renderer.status().ready, "Corrupt image fails closed");
        renderer.reset();
        CoUninitialize();
        std::cout << "Stream helmet GPU tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
