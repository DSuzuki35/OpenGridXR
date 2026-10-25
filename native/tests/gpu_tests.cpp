// Compile the production renderer into this isolated GPU fixture. No runtime
// registration or game configuration is needed to exercise shared textures.
#include "../src/output.cpp"
#include <iostream>
#include <stdexcept>
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() {
    try {
        RECT bounds{};
        require(parse_display(L"-1920,0,1920,1080", bounds) && bounds.left == -1920 &&
                    bounds.right == 0,
                "display parser accepts negative desktop coordinates");
        require(!parse_display(L"0,0,-1,1080", bounds) &&
                    !parse_display(L"0,0,1920,1080,junk", bounds),
                "display parser rejects invalid bounds");
        for (auto requestedFormat : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB}) {
            ComPtr<ID3D11Device> producer;
            ComPtr<ID3D11DeviceContext> ctx;
            D3D_FEATURE_LEVEL level{};
            require(
                SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr,
                                            0, D3D11_SDK_VERSION, &producer, &level, &ctx)),
                "hardware D3D11 device");
            ComPtr<IDXGIDevice> dxgi;
            ComPtr<IDXGIAdapter> adapter;
            require(SUCCEEDED(producer.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter)),
                    "producer adapter");
            DXGI_ADAPTER_DESC ad{};
            adapter->GetDesc(&ad);
            auto instance = GetModuleHandleW(nullptr);
            WNDCLASSW wc{};
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = instance;
            wc.lpszClassName = L"OpenGridXR.GpuTest";
            RegisterClassW(&wc);
            HWND window =
                CreateWindowW(wc.lpszClassName, L"Synthetic GPU fixture", WS_OVERLAPPEDWINDOW, 0, 0,
                              640, 360, nullptr, nullptr, instance, nullptr);
            require(window != nullptr, "hidden test window");
            Renderer renderer;
            require(renderer.init(window, ad.AdapterLuid, false),
                    "production renderer and shader initialization");
            std::vector<uint32_t> pixels(64 * 64, 0xff2080c0);
            for (unsigned y = 0; y < 64; ++y)
                for (unsigned x = 36; x < 64; ++x)
                    pixels[y * 64 + x] = 0xff208020;
            D3D11_TEXTURE2D_DESC d{};
            d.Width = d.Height = 64;
            d.ArraySize = d.MipLevels = 1;
            d.Format = requestedFormat;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            d.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
            D3D11_SUBRESOURCE_DATA data{pixels.data(), 64 * 4, 0};
            ComPtr<ID3D11Texture2D> source;
            require(SUCCEEDED(producer->CreateTexture2D(&d, &data, &source)),
                    "shared producer texture");
            ComPtr<IDXGIKeyedMutex> key;
            ComPtr<IDXGIResource> resource;
            require(SUCCEEDED(source.As(&key)) && SUCCEEDED(source.As(&resource)),
                    "shared resource interfaces");
            HANDLE handle{};
            require(SUCCEEDED(resource->GetSharedHandle(&handle)), "shared handle");
            require(key->AcquireSync(0, 0) == S_OK, "producer acquires key zero");
            ctx->Flush();
            require(SUCCEEDED(key->ReleaseSync(1)), "producer signals reader");
            og::SharedFrame frame{};
            frame.magic = og::frameMagic;
            frame.version = og::frameVersion;
            frame.width = frame.height = 64;
            frame.format = d.Format;
            frame.texture = reinterpret_cast<uint64_t>(handle);
            frame.producer = GetCurrentProcessId();
            frame.session = 1;
            frame.sequence = 1;
            frame.published = GetTickCount64();
            frame.adapter = ad.AdapterLuid;
            frame.ready = 1;
            require(renderer.consume(frame) && frame.ready == 0,
                    "production consumer copies shared GPU frame and acknowledges");
            og::Config config;
            config.width = 1920;
            config.height = 1080;
            require(renderer.render(window, config),
                    "production warp shader renders captured frame");
            require(renderer.canvas != nullptr,
                    "output allocates configured resolution canvas independent of window");
            D3D11_TEXTURE2D_DESC canvasDesc{};
            renderer.canvas->GetDesc(&canvasDesc);
            require(
                canvasDesc.Width == 1920 && canvasDesc.Height == 1080,
                "configured output canvas has exact requested resolution exceeding window size");
            ComPtr<ID3D11Texture2D> buffer, readback;
            require(SUCCEEDED(renderer.swap->GetBuffer(0, IID_PPV_ARGS(&buffer))),
                    "output backbuffer");
            buffer->GetDesc(&d);
            d.Usage = D3D11_USAGE_STAGING;
            d.BindFlags = 0;
            d.MiscFlags = 0;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            require(SUCCEEDED(renderer.device->CreateTexture2D(&d, nullptr, &readback)),
                    "GPU readback allocation");
            renderer.context->CopyResource(readback.Get(), buffer.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            require(SUCCEEDED(renderer.context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
                    "GPU result readback");
            auto *pixel = static_cast<unsigned char *>(mapped.pData) +
                          (d.Height / 2) * mapped.RowPitch + (d.Width / 2) * 4;
            bool correct = std::abs(int(pixel[0]) - 192) <= 1 &&
                           std::abs(int(pixel[1]) - 128) <= 1 &&
                           std::abs(int(pixel[2]) - 32) <= 1 && pixel[3] == 255;
            renderer.context->Unmap(readback.Get(), 0);
            require(correct, "clean output center pixel matches real source through GPU warp");
            config.crop.left = 30;
            require(renderer.render(window, config), "cropped frame rendered");
            renderer.context->CopyResource(readback.Get(), buffer.Get());
            require(SUCCEEDED(renderer.context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
                    "cropped GPU readback");
            pixel = static_cast<unsigned char *>(mapped.pData) + (d.Height / 2) * mapped.RowPitch +
                    (d.Width / 2) * 4;
            bool cropApplied = std::abs(int(pixel[0]) - 32) <= 1;
            renderer.context->Unmap(readback.Get(), 0);
            require(cropApplied,
                    "30 percent left crop samples shifted source region instead of whole texture");
            require(key->AcquireSync(0, 0) == S_OK, "consumer returned ownership to producer");
            key->ReleaseSync(0);
            RECT originalBounds{}, fullscreenBounds{}, restoredBounds{};
            GetWindowRect(window, &originalBounds);
            displayBounds = {-1280, 0, 0, 720};
            set_fullscreen(window, true);
            GetWindowRect(window, &fullscreenBounds);
            require(EqualRect(&displayBounds, &fullscreenBounds) &&
                        !(GetWindowLongPtrW(window, GWL_STYLE) & WS_CAPTION),
                    "borderless fullscreen uses exact chosen physical display bounds");
            set_fullscreen(window, false);
            GetWindowRect(window, &restoredBounds);
            require(EqualRect(&originalBounds, &restoredBounds) &&
                        (GetWindowLongPtrW(window, GWL_STYLE) & WS_CAPTION),
                    "fullscreen toggle restores window placement and decorations");
            DestroyWindow(window);
        }
        std::cout << "D3D11 shared-texture transport, shader compilation, render and pixel "
                     "readback passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
