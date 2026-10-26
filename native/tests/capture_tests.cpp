#include "../src/layer.cpp"
#include <iostream>
#include <stdexcept>
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
XrTime locatedOverlayTime{};
XrResult XRAPI_CALL overlayLocate(XrSpace space, XrSpace, XrTime time, XrSpaceLocation *out) {
    locatedOverlayTime = time;
    out->pose = {{0, 0, 0, 1}, {2, 3, -1}};
    out->locationFlags =
        space == reinterpret_cast<XrSpace>(10)
            ? XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT
            : 0;
    return XR_SUCCESS;
}
void quadGrading(Session &session) {
    std::array<ComPtr<ID3D11Texture2D>,2> textures;
    std::array<XrSwapchain,2> handles{reinterpret_cast<XrSwapchain>(101),reinterpret_cast<XrSwapchain>(102)};
    XrCompositionLayerProjectionView views[4]{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                            {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=16;desc.ArraySize=2;desc.MipLevels=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    for(unsigned i=0;i<2;++i) {
        require(SUCCEEDED(session.device->CreateTexture2D(&desc,nullptr,&textures[i])),"quad context/focus texture");
        D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;rd.Texture2DArray.ArraySize=2;
        ComPtr<ID3D11RenderTargetView> rtv;require(SUCCEEDED(session.device->CreateRenderTargetView(textures[i].Get(),&rd,&rtv)),"quad RTV");
        float red[]{1,0,0,1};session.context->ClearRenderTargetView(rtv.Get(),red);
        auto chain=std::make_unique<Swapchain>();chain->session=&session;
        chain->info.width=chain->info.height=16;chain->info.arraySize=2;chain->info.format=DXGI_FORMAT_R8G8B8A8_UNORM;
        chain->info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        XrSwapchainImageD3D11KHR image{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};image.texture=textures[i].Get();chain->images.push_back(image);
        chain->acquired.push_back({0,true});chains[handles[i]]=std::move(chain);
        for(unsigned eye=0;eye<2;++eye)views[i*2+eye].subImage={handles[i],{{int32_t(i*4),int32_t(i*4)},{int32_t(16-i*8),int32_t(16-i*8)}},eye};
    }
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.viewCount=4;projection.views=views;
    auto base=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.layerCount=1;end.layers=&base;
    remember_projection_regions(session,&end);
    require(chains.at(handles[0])->projectionRegions.size()==2&&chains.at(handles[1])->projectionRegions.size()==2,
            "quad submission collects both eye slices of separate context and focus images");
    session.instance->config.visorColor={true,100,50,0};
    auto before=session.visorColorAppliedCalls;
    for(unsigned i=0;i<2;++i){
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};require(release_image(handles[i],&release)==XR_SUCCESS,"quad grade release");
        auto stagingDesc=desc;stagingDesc.BindFlags=0;stagingDesc.Usage=D3D11_USAGE_STAGING;stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;require(SUCCEEDED(session.device->CreateTexture2D(&stagingDesc,nullptr,&staging)),"quad readback");
        session.context->CopyResource(staging.Get(),textures[i].Get());
        for(unsigned eye=0;eye<2;++eye){
            D3D11_MAPPED_SUBRESOURCE map{};require(SUCCEEDED(session.context->Map(staging.Get(),eye,D3D11_MAP_READ,0,&map)),"quad eye readback");
            auto center=*reinterpret_cast<const uint32_t*>(static_cast<const char*>(map.pData)+8*map.RowPitch+8*4);
            auto corner=*static_cast<const uint32_t*>(map.pData);session.context->Unmap(staging.Get(),eye);
            require((center&255)>=26&&(center&255)<=28&&((center>>8)&255)==(center&255)&&((center>>16)&255)==(center&255),"every context/focus eye receives one actual grayscale brightness grade");
            if(i)require(corner==0xff0000ff,"focus rectangle leaves outside pixels unchanged");
        }
    }
    require(session.visorColorAppliedCalls==before+2,"one grade per context/focus image");
    views[1].subImage=views[0].subImage;remember_projection_regions(session,&end);
    require(chains.at(handles[0])->projectionRegions.size()==1,"duplicate quad rectangles processed once");
    views[1].subImage.imageRect={{1,1},{8,8}};remember_projection_regions(session,&end);
    const auto result=session.processor->process(textures[0].Get(),DXGI_FORMAT_R8G8B8A8_UNORM,chains.at(handles[0])->projectionRegions,{},session.instance->config.visorColor);
    require(result.status==og::ImageProcessStatus::Invalid,"overlapping quad rectangles rejected before repeated grading");
    chains.clear();
}
int main() {
    try {
        og::OverlayPose panel;
        panel.width = 2;
        panel.height = 1;
        panel.position[2] = -2;
        const float half = std::sqrt(.5f);
        XrPosef rotatedReference{{0, 0, half, half}, {4, 5, 6}};
        og::CapturedOverlay geometry;
        require(og::capture_overlay_corners(panel, rotatedReference, rotatedReference, geometry) &&
                    std::abs(geometry.corners[0][0] + 1) < 1e-5 &&
                    std::abs(geometry.corners[0][1] - .5f) < 1e-5 &&
                    std::abs(geometry.corners[0][2] + 2) < 1e-5,
                "camera inverse cancels shared translated and rolled reference space");
        auto tilted = panel;
        tilted.orientation[1] = half;
        tilted.orientation[3] = half;
        const XrPosef identityEye{{0, 0, 0, 1}, {0, 0, 0}};
        require(
            og::capture_overlay_corners(tilted, rotatedReference, identityEye, geometry) &&
                std::abs(geometry.corners[0][0] - 3.5f) < 1e-5 &&
                std::abs(geometry.corners[0][1] - 5) < 1e-5 &&
                std::abs(geometry.corners[0][2] - 5) < 1e-5 &&
                std::abs(geometry.corners[1][2] - 3) < 1e-5,
            "noncommuting reference roll and panel yaw preserve each corner's perspective depth");
        for (auto requestedFormat : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB})
            for (unsigned samples : {1u, 4u}) {
                Instance instance;
                instance.game = "capture-test-" + std::to_string(GetCurrentProcessId());
                instance.config.enabled = true;
                instance.config.streaming = true;
                instance.config.fps = 60;
                Session session;
                session.instance = &instance;
                session.handle = reinterpret_cast<XrSession>(42);
                D3D_FEATURE_LEVEL level{};
                require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                                    nullptr, 0, D3D11_SDK_VERSION, &session.device,
                                                    &level, &session.context)),
                        "capture producer hardware device");
                ComPtr<IDXGIDevice> dx;
                ComPtr<IDXGIAdapter> a;
                session.device.As(&dx);
                dx->GetAdapter(&a);
                DXGI_ADAPTER_DESC ad{};
                a->GetDesc(&ad);
                session.adapter = ad.AdapterLuid;
                require(session.channel.open(instance.game) && session.channel.lock(),
                        "capture channel");
                session.channel.data()->consumer = GetCurrentProcessId();
                session.channel.data()->consumerBeat = GetTickCount64();
                session.channel.unlock();
                std::vector<uint32_t> red(64 * 64, 0xff0000ff), blue(64 * 64, 0xffff0000);
                D3D11_TEXTURE2D_DESC d{};
                d.Width = d.Height = 64;
                d.ArraySize = d.MipLevels = 1;
                d.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
                d.SampleDesc.Count = samples;
                d.Usage = D3D11_USAGE_DEFAULT;
                d.BindFlags = D3D11_BIND_RENDER_TARGET;
                D3D11_SUBRESOURCE_DATA pixels{red.data(), 64 * 4, 0};
                ComPtr<ID3D11Texture2D> source;
                require(SUCCEEDED(session.device->CreateTexture2D(&d, nullptr, &source)),
                        "synthetic runtime swapchain image");
                D3D11_RENDER_TARGET_VIEW_DESC rt{};
                rt.Format = requestedFormat;
                rt.ViewDimension =
                    samples > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
                ComPtr<ID3D11RenderTargetView> renderTarget;
                require(SUCCEEDED(session.device->CreateRenderTargetView(source.Get(), &rt,
                                                                         &renderTarget)),
                        "typed runtime RTV over typeless image");
                float redColor[4]{1, 0, 0, 1};
                session.context->ClearRenderTargetView(renderTarget.Get(), redColor);
                auto swap = std::make_unique<Swapchain>();
                swap->session = &session;
                swap->info.width = swap->info.height = 64;
                swap->info.arraySize = 1;
                swap->info.format = requestedFormat;
                swap->info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
                XrSwapchainImageD3D11KHR image{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};
                image.texture = source.Get();
                swap->images.push_back(image);
                snapshot(*swap, 0);
                require(swap->valid, "capture takes snapshot while acquired image remains owned");
                float blueColor[4]{0, 0, 1, 1};
                session.context->ClearRenderTargetView(renderTarget.Get(), blueColor);
                XrSwapchain handle = reinterpret_cast<XrSwapchain>(43);
                chains[handle] = std::move(swap);
                XrCompositionLayerProjectionView views[2]{
                    {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                    {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
                for (auto &view : views) {
                    view.subImage.swapchain = handle;
                    view.subImage.imageRect = {{8, 8}, {48, 48}};
                    view.pose.orientation.w = 1;
                    view.fov = {-.7f, .7f, .7f, -.7f};
                }
                instance.config.eye = 1;
                instance.LocateSpace = overlayLocate;
                views[1].pose.position = {1, 1, 1};
                XrCompositionLayerProjection projectionLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
                projectionLayer.viewCount = 2;
                projectionLayer.views = views;
                const XrCompositionLayerBaseHeader *header =
                    reinterpret_cast<const XrCompositionLayerBaseHeader *>(&projectionLayer);
                XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
                end.layerCount = 1;
                end.layers = &header;
                end.displayTime = 123456789;
                session.overlaysAvailable = session.overlays.open("test");
                require(session.overlaysAvailable && session.overlays.lock(),
                        "capture geometry fixture mapping");
                auto *overlayData = session.overlays.data();
                *overlayData = og::OverlayShared{};
                overlayData->consumerPid = overlayData->producerPid = GetCurrentProcessId();
                overlayData->consumerBeat = overlayData->producerBeat = GetTickCount64();
                auto &selected = overlayData->slots[0];
                selected.enabled = selected.showInStream = 1;
                selected.sourceRevision = 7;
                selected.revision = 4;
                selected.frames[0].generation = 22;
                selected.frames[0].width = 100;
                selected.frames[0].height = 50;
                selected.frames[1] = selected.frames[0];
                selected.frames[1].generation = 23;
                selected.pose = panel;
                selected.pose.valid = 1;
                selected.pose.sourceRevision = 7;
                selected.pose.configRevision = 4;
                selected.pose.sourceGeneration = 22;
                selected.pose.space = 10;
                selected.pose.pixelWidth = 100;
                selected.pose.pixelHeight = 50;
                overlayData->slots[1] = selected;
                overlayData->slots[1].revision = 5;
                overlayData->slots[2] = selected;
                overlayData->slots[2].showInStream = 0;
                overlayData->slots[3] = selected;
                overlayData->slots[3].pose.space = 11;
                overlayData->slots[4] = selected;
                overlayData->slots[4].recenter = 1;
                session.overlays.unlock();
                remember_projection_regions(session, &end);
                require(chains.at(handle)->projectionRegions.size() == 1 &&
                            chains.at(handle)->projectionRegions[0].x == 8 &&
                            chains.at(handle)->projectionRegions[0].width == 48,
                        "only submitted projection rectangle is classified, duplicate eye rect "
                        "deduplicated");
                XrCompositionLayerQuad hud{XR_TYPE_COMPOSITION_LAYER_QUAD};
                hud.subImage.swapchain = handle;
                const XrCompositionLayerBaseHeader *mixed[]{
                    header, reinterpret_cast<const XrCompositionLayerBaseHeader *>(&hud)};
                auto mixedEnd = end;
                mixedEnd.layerCount = 2;
                mixedEnd.layers = mixed;
                remember_projection_regions(session, &mixedEnd);
                require(chains.at(handle)->projectionRegions.empty(),
                        "projection/HUD shared image is conservatively excluded");
                chains.at(handle)->info.usageFlags =
                    XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                remember_projection_regions(session, &end);
                require(chains.at(handle)->projectionRegions.empty(),
                        "depth attachment never classified for sharpening");
                chains.at(handle)->info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
                remember_projection_regions(session, &end);
                session.context->ClearRenderTargetView(renderTarget.Get(), redColor);
                instance.config.visorColor={true,100,50,0};
                session.processor=std::make_unique<og::ImageProcessor>(session.device.Get(),session.context.Get());
                instance.ReleaseSwapchainImage=[](XrSwapchain,const XrSwapchainImageReleaseInfo*)->XrResult{return XR_SUCCESS;};
                chains.at(handle)->acquired.push_back({0,true});
                XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                require(release_image(handle,&releaseInfo)==XR_SUCCESS&&session.visorColorAppliedCalls==1&&session.sharpenAppliedCalls==0,
                        "real release path applies visor grade separately from sharpening");
                session.context->ClearRenderTargetView(renderTarget.Get(), blueColor);
                publish(session, &end);
                require(session.channel.lock(), "capture frame metadata lock");
                auto frame = *session.channel.data();
                session.channel.unlock();
                require(frame.ready == 1 && frame.width == 48 && frame.height == 48 &&
                            frame.sequence == 1,
                        "selected subimage publishes correct dimensions and sequence");
                require(frame.format == uint32_t(requestedFormat),
                        "typeless runtime image publishes its typed OpenXR swapchain format");
                require(frame.captureEye == 1 && frame.displayTime == end.displayTime &&
                            locatedOverlayTime == end.displayTime && frame.position[0] == 1,
                        "captured geometry uses actual selected eye position and raw frame display "
                        "time");
                require(
                    frame.overlays[0].valid && frame.overlays[0].sourceRevision == 7 &&
                        frame.overlays[0].pixelWidth == 100 &&
                        std::abs(frame.overlays[0].corners[0][0]) < 1e-5 &&
                        std::abs(frame.overlays[0].corners[0][1] - 2.5f) < 1e-5 &&
                        std::abs(frame.overlays[0].corners[0][2] + 4) < 1e-5,
                    "GPU captured image atomically carries exact quad corners in its eye camera");
                require(!frame.overlays[1].valid && !frame.overlays[2].valid &&
                            !frame.overlays[3].valid && !frame.overlays[4].valid,
                        "changed config, unselected, untracked and pending recenter geometry never "
                        "guessed");
                ComPtr<ID3D11Texture2D> shared, staging;
                require(SUCCEEDED(session.device->OpenSharedResource(
                            reinterpret_cast<HANDLE>(frame.texture), IID_PPV_ARGS(&shared))),
                        "capture shared texture opens");
                ComPtr<IDXGIKeyedMutex> key;
                shared.As(&key);
                require(key->AcquireSync(1, 0) == S_OK, "published capture ownership");
                shared->GetDesc(&d);
                ComPtr<ID3D11ShaderResourceView> sampleView;
                require(SUCCEEDED(session.device->CreateShaderResourceView(shared.Get(), nullptr,
                                                                           &sampleView)),
                        "typed captured image can be sampled by output");
                d.Usage = D3D11_USAGE_STAGING;
                d.BindFlags = 0;
                d.MiscFlags = 0;
                d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                require(SUCCEEDED(session.device->CreateTexture2D(&d, nullptr, &staging)),
                        "capture staging texture");
                session.context->CopyResource(staging.Get(), shared.Get());
                D3D11_MAPPED_SUBRESOURCE mapped{};
                require(
                    SUCCEEDED(session.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
                    "capture pixel readback");
                auto pixel = *static_cast<uint32_t *>(mapped.pData);
                session.context->Unmap(staging.Get(), 0);
                key->ReleaseSync(0);
                require(pixel == 0xff0000ff,
                        "capture preserved pre-release pixels despite runtime image reuse");
                const auto recapture = [&](bool expected, const char *message) {
                    require(session.channel.lock(), "prepare next captured frame");
                    session.channel.data()->ready = 0;
                    session.channel.data()->consumerBeat = GetTickCount64();
                    session.channel.unlock();
                    session.lastCapture = 0;
                    ++end.displayTime;
                    publish(session, &end);
                    require(session.channel.lock(), "read next captured frame");
                    const auto next = *session.channel.data();
                    session.channel.unlock();
                    require(next.ready && bool(next.overlays[0].valid) == expected, message);
                    require(key->AcquireSync(1, 1000) == S_OK, "consume repeated captured image");
                    key->ReleaseSync(0);
                    return next;
                };
                for (unsigned buffer : {1u, 0u, 1u, 0u}) {
                    require(session.overlays.lock(), "rotate overlay ring buffer");
                    overlayData->slots[0].current = buffer;
                    session.overlays.unlock();
                    const auto next = recapture(
                        true, "same-source same-size ring rotation preserves captured geometry");
                    require(next.overlays[0].pixelWidth == 100 &&
                                std::abs(next.overlays[0].corners[0][1] - 2.5f) < 1e-5,
                            "ring resource identity never changes quad geometry");
                }
                require(session.overlays.lock(), "resize current overlay frame");
                overlayData->slots[0].frames[0].width = 200;
                session.overlays.unlock();
                recapture(false, "changed pixel dimensions reject stale aspect geometry");
                require(session.overlays.lock(), "refresh resized quad geometry");
                overlayData->slots[0].pose.pixelWidth = 200;
                overlayData->slots[0].pose.height = .5f;
                session.overlays.unlock();
                const auto resized = recapture(true, "matching resized quad geometry resumes");
                require(resized.overlays[0].pixelWidth == 200 &&
                            std::abs(resized.overlays[0].corners[0][1] - 2.25f) < 1e-5,
                        "resized geometry uses refreshed spatial aspect");
                require(session.overlays.lock(), "replace source epoch");
                ++overlayData->slots[0].sourceRevision;
                session.overlays.unlock();
                recapture(false, "new source epoch still rejects prior source geometry");
                chains.clear();
                quadGrading(session);
            }
        std::cout << "Layer acquired-image snapshot, subimage crop, shared GPU publish and "
                     "readback passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
