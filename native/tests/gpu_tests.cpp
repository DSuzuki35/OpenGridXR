// Compile the production renderer into this isolated GPU fixture. No runtime
// registration or game configuration is needed to exercise shared textures.
#include "../src/output.cpp"
#include <iostream>
#include <fstream>
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
            require(renderer.init(window, ad.AdapterLuid, true),
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
            config.streaming = true;
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
            config.streaming = false;
            require(renderer.render(window, config), "driving frame rendered");
            renderer.context->CopyResource(readback.Get(), buffer.Get());
            require(SUCCEEDED(renderer.context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
                    "driving GPU readback");
            pixel = static_cast<unsigned char *>(mapped.pData) + (d.Height / 2) * mapped.RowPitch +
                    (d.Width / 2) * 4;
            bool drivingNotRecropped = std::abs(int(pixel[0]) - 192) <= 1;
            renderer.context->Unmap(readback.Get(), 0);
            require(drivingNotRecropped, "driving output does not crop its already cropped eye image again");
            // Exercise the production shader directly at selected projected UVs,
            // independently of the CPU corner limiter and display letterboxing.
            config.width = config.height = 64;
            config.crop = {};
            require(renderer.render(window, config), "small mirror test canvas");
            for (unsigned y = 0; y < 64; ++y)
                for (unsigned x = 0; x < 64; ++x)
                    pixels[y * 64 + x] = 0xff808000u | (x * 4);
            renderer.context->UpdateSubresource(renderer.local.Get(), 0, nullptr, pixels.data(), 64 * 4, 0);
            ComPtr<ID3D11Texture2D> mirrorReadback;
            renderer.canvas->GetDesc(&d);
            d.Usage = D3D11_USAGE_STAGING;
            d.BindFlags = 0;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            require(SUCCEEDED(renderer.device->CreateTexture2D(&d, nullptr, &mirrorReadback)), "mirror readback");
            const auto sampleMirror = [&](float u, float limit, float softness, float stretch,
                                          float left = 0, float right = 1) {
                Constants parameters{};
                const float fov[4]{-1, 1, 1, -1};
                std::copy(fov, fov + 4, parameters.input);
                parameters.output[0] = parameters.output[1] = 2 * u - 1;
                parameters.quat[3] = 1;
                parameters.uvBounds[0] = left;
                parameters.uvBounds[2] = right;
                parameters.uvBounds[3] = 1;
                parameters.flags[0] = requestedFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ? 1.f : 0.f;
                parameters.flags[1] = 1;
                parameters.flags[2] = limit;
                parameters.mirrorStyle[0] = softness;
                parameters.mirrorStyle[1] = parameters.mirrorStyle[2] = 1.f / 64;
                parameters.mirrorStyle[3] = stretch;
                renderer.context->UpdateSubresource(renderer.constants.Get(), 0, nullptr, &parameters, 0, 0);
                ID3D11ShaderResourceView *empty = nullptr;
                renderer.context->PSSetShaderResources(0, 1, &empty);
                renderer.context->OMSetRenderTargets(1, renderer.canvasTarget.GetAddressOf(), nullptr);
                D3D11_VIEWPORT viewport{0, 0, 64, 64, 0, 1};
                renderer.context->RSSetViewports(1, &viewport);
                renderer.context->PSSetShaderResources(0, 1, renderer.view.GetAddressOf());
                renderer.context->Draw(3, 0);
                renderer.context->CopyResource(mirrorReadback.Get(), renderer.canvas.Get());
                D3D11_MAPPED_SUBRESOURCE mappedMirror{};
                require(SUCCEEDED(renderer.context->Map(mirrorReadback.Get(), 0, D3D11_MAP_READ, 0,
                                                       &mappedMirror)), "mirror pixel readback");
                const auto at = static_cast<unsigned char *>(mappedMirror.pData) +
                                32 * mappedMirror.RowPitch + 32 * 4;
                std::array<int, 3> result{at[0], at[1], at[2]};
                renderer.context->Unmap(mirrorReadback.Get(), 0);
                return result;
            };
            const auto reflected = sampleMirror(1.2f, .25f, 0, 0);
            require(std::abs(reflected[0] - 203) <= 2 && reflected[1] == 128,
                    "finite border reflects image within allowance");
            require(sampleMirror(1.3f, .25f, 0, 0)[0] == 0 &&
                        sampleMirror(1.1f, 0, 0, 0)[1] == 0,
                    "beyond finite and zero mirror allowance pixels are black");
            const auto interior = sampleMirror(.4f, .25f, 0, 0);
            require(sampleMirror(.4f, .25f, 1, 1) == interior,
                    "mirror styling never changes original image pixels");
            require(sampleMirror(1.2f, .25f, 0, 1)[0] > reflected[0] + 30,
                    "mirror stretch moves reflected sampling toward real edge");
            require(sampleMirror(1.2f, .25f, 1, 0)[1] < reflected[1] - 8,
                    "mirror softness softens and dims only reflected area");
            require(sampleMirror(1.249f, .25f, 1, 0)[1] < 10,
                    "mirror softness feathers outer allowance into black");
            require(std::abs(sampleMirror(1.2f, .25f, 0, 0, .25f, .75f)[0] - 164) <= 2,
                    "finite mirror reflects inside cropped image boundary");
            require(sampleMirror(1.3f, -1, 0, 0)[1] == 128,
                    "legacy omitted mirror allowance preserves sampler reflection");
            // Measure the actual production warp after aspect fitting, using a
            // gradient so source-image span is directly visible in pixel values.
            config.width = 640;
            config.height = 360;
            config.offsetX = config.offsetY = 0;
            require(renderer.render(window, config), "reserve test canvas");
            ComPtr<ID3D11Texture2D> reserveReadback;
            renderer.canvas->GetDesc(&d);
            d.Usage = D3D11_USAGE_STAGING;
            d.BindFlags = d.MiscFlags = 0;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            require(SUCCEEDED(renderer.device->CreateTexture2D(&d, nullptr, &reserveReadback)),
                    "reserve canvas readback allocation");
            const auto reserveSpan = [&]() {
                require(renderer.render(window, config), "production reserve render");
                renderer.context->CopyResource(reserveReadback.Get(), renderer.canvas.Get());
                D3D11_MAPPED_SUBRESOURCE result{};
                require(SUCCEEDED(renderer.context->Map(reserveReadback.Get(), 0, D3D11_MAP_READ, 0,
                                                       &result)), "reserve pixel readback");
                const auto row = static_cast<unsigned char*>(result.pData) + 180 * result.RowPitch;
                const int span = int(row[480 * 4]) - int(row[160 * 4]);
                renderer.context->Unmap(reserveReadback.Get(), 0);
                return span;
            };
            const auto zeroSpan = reserveSpan();
            config.reserve = {13, 13, 13, 13};
            const auto thirteenSpan = reserveSpan();
            config.reserve = {30, 30, 30, 30};
            const auto thirtySpan = reserveSpan();
            require(std::abs(zeroSpan - 128) <= 2 && std::abs(thirteenSpan - zeroSpan * .74) <= 2 &&
                        std::abs(thirtySpan - zeroSpan * .40) <= 2,
                    "0/13/30 percent reserve keeps 100/74/40 percent source span after aspect fitting");
            const auto savedPath = std::filesystem::temp_directory_path() /
                (L"OpenGridXR-gpu-framing-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
            { std::ofstream saved(savedPath);
              saved << "[lmu]\nReserveLeft=13\nReserveRight=13\nReserveTop=13\nReserveBottom=13\n"
                       "OffsetX=0\nOffsetY=0\n"; }
            og::OutputFramingReload reload(savedPath.wstring(), "lmu");
            const auto originalDevice = renderer.device.Get();
            const auto originalSource = renderer.local.Get();
            const auto originalSequence = renderer.sequence;
            const bool reloaded = reload.poll(config, 1000);
            std::filesystem::remove(savedPath);
            require(reloaded && reserveSpan() == thirteenSpan &&
                        renderer.device.Get() == originalDevice && renderer.local.Get() == originalSource &&
                        renderer.sequence == originalSequence,
                    "saved reserve updates actual GPU pixels without recreating renderer or capture");
            HANDLE previewReader{}, previewWriter{};
            require(CreatePipe(&previewReader, &previewWriter, nullptr, 4096) != 0, "GPU preview pipe");
            og::OutputFramingPreview preview(previewReader);
            const auto savedConfig = config;
            const auto previewCommand = [&](const char* command) {
                DWORD written{};
                require(WriteFile(previewWriter, command, DWORD(std::strlen(command)), &written, nullptr) != 0,
                        "GPU preview command");
                preview.poll(1000); preview.apply(savedConfig, config);
            };
            previewCommand("F 30 30 30 30 0 0\n");
            require(reserveSpan() == thirtySpan && savedConfig.reserve.left == 13,
                    "unsaved reserve stdin command changes actual GPU pixels while saved config stays intact");
            previewCommand("R\n");
            require(reserveSpan() == thirteenSpan, "discard restores saved reserve GPU pixels immediately");
            CloseHandle(previewWriter); CloseHandle(previewReader);
            std::cout << "Reserve source spans (0/13/30%): " << zeroSpan << "/" << thirteenSpan <<
                "/" << thirtySpan << "; saved 13% applied to same renderer\n";
            renderer.upright.reset();
            renderer.upright.correction({}, {}, 0, false, 100);
            const auto pitched = vrst::axis_angle({1, 0, 0}, .2);
            require(vrst::angle_between(renderer.upright.correction(pitched, pitched, 0, false, 100), {}) > .05,
                    "pitch anchor exists before focused reset");
            active = &renderer;
            wndproc(window, WM_KEYDOWN, 'R', 0);
            active = nullptr;
            require(vrst::angle_between(renderer.upright.correction(pitched, pitched, 0, false, 100), {}) < 1e-6,
                    "focused R key reanchors pitch stabilization");
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
