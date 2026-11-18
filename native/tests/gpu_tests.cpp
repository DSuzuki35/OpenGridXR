// Compile the production renderer into this isolated GPU fixture. No runtime
// registration or game configuration is needed to exercise shared textures.
#include "../src/output.cpp"
#include "cockpit_depth.hpp"
#include <iostream>
#include <fstream>
#include <stdexcept>
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string(argv[1])=="--monitor-clone"){
            SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            POINT origin{};
            MONITORINFO mi{sizeof(mi)};
            require(GetMonitorInfoW(MonitorFromPoint(origin,MONITOR_DEFAULTTOPRIMARY),&mi),"source monitor");
            auto identity=og::monitorIdentity(mi.rcMonitor);
            require(!identity.empty(),"source display identity");
            HDC dc=CreateCompatibleDC(nullptr);
            BITMAPINFO bi{};
            bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=320;bi.bmiHeader.biHeight=-180;
            bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;
            void* bits=nullptr;
            auto bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
            require(dc && bits && bitmap,"in-memory clone destination");
            auto old=SelectObject(dc,bitmap);
            std::optional<og::MonitorClone> capture;
            og::selectMonitorCapture(capture,og::OutputActivity::Clone);
            bool painted=false;
            for(unsigned i=0;i<120 && !painted;++i){
                painted=capture->paint(dc,{0,0,320,180},mi.rcMonitor,nullptr,identity);
                if(!painted)Sleep(16);
            }
            og::selectMonitorCapture(capture,og::OutputActivity::Scene);
            SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
            require(painted&&!capture,"captured primary display to memory and released duplication");
            std::cout<<"Cloned the physical primary monitor and released the session.\n";
            return 0;
        }
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
            D3D11_TEXTURE2D_DESC depthDesc = d;
            depthDesc.Format = DXGI_FORMAT_R32_FLOAT;
            depthDesc.MiscFlags = 0;
            std::vector<float> depths(64 * 64, .25f);
            D3D11_SUBRESOURCE_DATA depthPixels{depths.data(), 64 * sizeof(float), 0};
            ComPtr<ID3D11Texture2D> depthTexture;
            require(SUCCEEDED(producer->CreateTexture2D(&depthDesc, &depthPixels, &depthTexture)),
                    "captured eye depth texture");
            D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = depthDesc.Format;
            srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            srv.Texture2DArray.ArraySize = srv.Texture2DArray.MipLevels = 1;
            ComPtr<ID3D11ShaderResourceView> depthView;
            require(SUCCEEDED(producer->CreateShaderResourceView(depthTexture.Get(), &srv, &depthView)),
                    "captured eye depth view");
            og::CockpitDepthEye eye{};
            eye.view = depthView.Get();
            eye.pose.orientation.w = 1;
            eye.rect.extent = {64, 64};
            eye.fov = {-.7f, .7f, .7f, -.7f};
            eye.invNear = 10;
            og::StreamDepthPublisher depthPublisher;
            require(depthPublisher.publish(producer.Get(), ctx.Get(), eye, frame.depth),
                    "publish depth alongside captured eye");
            require(renderer.consume(frame) && frame.ready == 0,
                    "production consumer copies shared GPU frame and acknowledges");
            require(renderer.streamDepth.view() != nullptr, "production output receives matching depth");
            require(key->AcquireSync(0, 1000) == S_OK, "prepare next eye without depth");
            key->ReleaseSync(1);
            frame.ready = 1;
            ++frame.sequence;
            frame.depth = {};
            require(renderer.consume(frame) && !renderer.streamDepth.view(),
                    "next color frame withdraws depth when the sim omits it");
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
            const auto checkFrameColor = [&](const og::SceneColorSettings &color,
                                             std::array<int, 3> expected, const char *message) {
                require(key->AcquireSync(0, 1000) == S_OK, "reuse raw eye with new color settings");
                key->ReleaseSync(1);
                frame.ready = 1;
                ++frame.sequence;
                frame.color = color;
                require(renderer.consume(frame) && renderer.render(window, config), "render new frame color");
                renderer.context->CopyResource(readback.Get(), buffer.Get());
                require(SUCCEEDED(renderer.context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
                        "graded GPU readback");
                const auto center = static_cast<unsigned char *>(mapped.pData) +
                                    (d.Height / 2) * mapped.RowPitch + (d.Width / 2) * 4;
                bool matches = true;
                for (int i = 0; i < 3; ++i)
                    matches &= std::abs(int(center[i]) - expected[i]) <= 1;
                renderer.context->Unmap(readback.Get(), 0);
                require(matches, message);
            };
            checkFrameColor({true, 125, 85, 60, 135}, {168, 133, 58},
                            "one display-encoded brightness, contrast, gamma and saturation pass");
            checkFrameColor({true, 100, 100, 100, 100}, {192, 128, 32},
                            "cycling back to neutral restores the original raw eye");
            checkFrameColor({false, 125, 85, 60, 135}, {192, 128, 32},
                            "disabled correction bypasses the stored preset");
            const auto helmetDir = std::filesystem::temp_directory_path() /
                ("opengrid-gpu-visor-" + std::to_string(GetCurrentProcessId()));
            std::filesystem::create_directories(helmetDir);
            // One red PNG pixel with alpha 128. Its final color distinguishes grading
            // the scene before the visor from grading the already-composited image.
            constexpr unsigned char redPng[]{
                137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,
                31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,248,207,192,208,0,0,4,129,
                1,128,44,85,206,176,0,0,0,0,73,69,78,68,174,66,96,130};
            const auto openImage = helmetDir / "OpenGridXR-stream-helmet-iracing-open.png";
            std::ofstream(openImage, std::ios::binary).write(reinterpret_cast<const char *>(redPng), sizeof(redPng));
            const auto manifest = helmetDir / "OpenGridXR-stream-helmet-iracing.json";
            std::ofstream(manifest) << R"({"version":2,"enabled":true,"generation":"test",
                "leftImage":"OpenGridXR-stream-helmet-iracing-left.png","rightImage":"OpenGridXR-stream-helmet-iracing-right.png",
                "halfImage":"OpenGridXR-stream-helmet-iracing-half.png","openImage":"OpenGridXR-stream-helmet-iracing-open.png",
                "visorHotkey":true,"visorPosition":"closed","streamOpacity":50})";
            require(renderer.streamHelmet.initialize(renderer.device.Get(), renderer.context.Get(), "iracing", helmetDir),
                    "live stream visor fixture");
            frame.visorPosition = 2;
            checkFrameColor({true, 125, 85, 60, 135}, {194, 99, 39},
                            "live open visor at half opacity uses 75 percent grade before blending artwork");
            renderer.streamHelmet.reset();
            std::filesystem::remove(openImage);
            std::filesystem::remove(manifest);
            std::filesystem::remove(helmetDir);
            frame.visorPosition = -1;
            checkFrameColor({}, {192, 128, 32}, "restore ungraded raw eye");
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
                renderer.context->PSSetShader(renderer.ps.Get(), nullptr, 0);
                renderer.context->PSSetConstantBuffers(0, 1, renderer.constants.GetAddressOf());
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
            {
                const auto canvas = renderer.canvas;
                const auto target = renderer.canvasTarget;
                const auto view = renderer.canvasView;
                config.width = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION + 1;
                require(!renderer.render(window, config) && renderer.canvas.Get() == canvas.Get() &&
                        renderer.canvasTarget.Get() == target.Get() && renderer.canvasView.Get() == view.Get(),
                        "failed resize must retain the complete previous canvas");
                config.width = 800;
                require(renderer.render(window, config) && renderer.canvas.Get() != canvas.Get() &&
                        renderer.canvasTarget && renderer.canvasView, "retry must produce a complete new canvas");
                config.width = 640;
                require(renderer.render(window, config), "restore reserve test canvas size");
            }
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
            require(vrst::angle_between(renderer.upright.correction(pitched, pitched, 0, false, 100), {}) < 1e-6,
                    "focused R key reanchors pitch stabilization");
            const auto roll = vrst::axis_angle({0, 0, 1}, .6);
            renderer.frame.orientation[2] = float(roll.z);
            renderer.frame.orientation[3] = float(roll.w);
            renderer.frame.sequence++;
            renderer.frame.published += 17;
            config.horizon = 100;
            config.mirror = true;
            const int tunedSpan = reserveSpan();
            wndproc(window, WM_KEYDOWN, 'B', 0);
            const int originalSpan = reserveSpan();
            require(originalSpan == thirteenSpan && tunedSpan < originalSpan - 8,
                    "focused B shows the original pose with the same crop on the GPU canvas");
            wndproc(window, WM_KEYDOWN, 'B', 0);
            require(reserveSpan() == tunedSpan, "B returns to the corrected camera on the same frame");
            config.stabilize = false;
            require(reserveSpan() == originalSpan, "saved bypass retains the original camera framing");
            wndproc(window, WM_KEYDOWN, 'P', 0);
            require(paused && renderer.presentCanvas(window), "paused output can repaint the captured canvas");
            wndproc(window, WM_KEYDOWN, 'P', 1LL << 30);
            require(paused, "held pause key does not resume");
            wndproc(window, WM_KEYDOWN, 'P', 0);
            require(!paused, "focused P resumes output");
            active = nullptr;
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
