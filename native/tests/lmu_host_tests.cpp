#include "../src/layer.cpp"
#include <iostream>
#include <stdexcept>
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
XrResult XRAPI_CALL releaseOkay(XrSwapchain,const XrSwapchainImageReleaseInfo*){return XR_SUCCESS;}
uint32_t pixel(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* texture,unsigned x,unsigned y){
    D3D11_TEXTURE2D_DESC d;texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=d.MiscFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> read;require(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&read)),"staging");context->CopyResource(read.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE map{};require(SUCCEEDED(context->Map(read.Get(),0,D3D11_MAP_READ,0,&map)),"read");
    uint32_t result;std::memcpy(&result,static_cast<char*>(map.pData)+map.RowPitch*y+4*x,4);context->Unmap(read.Get(),0);return result;
}
int main(){try{
    {
        og::Config config; config.enabled=true; config.lmu.mode="quad"; config.lmu.provider="qvfr";
        SetEnvironmentVariableA("OPENGRIDXR_LMU_INTEGRATED",nullptr);
        require(og::lmu::prepare_saved(config,"iracing")&&og::lmu::environment("OPENGRIDXR_LMU_INTEGRATED").empty(),"other games never auto-activate LMU hooks");
        config.enabled=false;
        require(og::lmu::prepare_saved(config,"lmu")&&og::lmu::environment("OPENGRIDXR_LMU_INTEGRATED").empty(),"disabled layer stays disabled");
        config.enabled=true;config.lmu.mode="off";
        require(og::lmu::prepare_saved(config,"lmu")&&og::lmu::environment("OPENGRIDXR_LMU_INTEGRATED").empty(),"disabled LMU module stays disabled");
        config.lmu.mode="quad";SetEnvironmentVariableA("OPENGRIDXR_LMU_INTEGRATED","0");
        require(og::lmu::prepare_saved(config,"lmu")&&og::lmu::environment("OPENGRIDXR_LMU_INTEGRATED")=="0","explicit launch opt-out wins");
        SetEnvironmentVariableA("OPENGRIDXR_LMU_INTEGRATED",nullptr);
        SetEnvironmentVariableA("LMU_QV_DYNAMIC_FOCUS","1");SetEnvironmentVariableA("LMU_QV_CAPTURE_DIR","inherited-capture");
        config.lmu.offsetY=-50;config.lmu.smoothing=3;
        require(og::lmu::prepare_saved(config,"lmu"),"saved settings prepared");
        require(og::lmu::configure(config,"lmu",{})&&og::lmu::settings.provider=="qvfr","automatic adapter configured");
        require(og::lmu::environment("LMU_QV_DYNAMIC_FOCUS")=="0"&&og::lmu::environment("LMU_QV_CAPTURE_DIR").empty(),"automatic launch excludes adaptive focus and inherited capture");
        require(std::stod(og::lmu::environment("LMU_QV_FOCUS_OFFSET_Y_PERCENT"))==-50&&std::stod(og::lmu::environment("LMU_QV_OUTER_BLUR"))==3,"full UI adjustment range survives automatic startup");
        og::lmu::reset();
        require(og::lmu::environment("OPENGRIDXR_LMU_INTEGRATED").empty()&&og::lmu::environment("LMU_QV_DYNAMIC_FOCUS")=="1"&&og::lmu::environment("LMU_QV_CAPTURE_DIR")=="inherited-capture","destroy restores original process environment");
        SetEnvironmentVariableA("LMU_QV_DYNAMIC_FOCUS",nullptr);SetEnvironmentVariableA("LMU_QV_CAPTURE_DIR",nullptr);
    }
    Instance instance;instance.config.enabled=true;instance.config.mask=true;instance.ReleaseSwapchainImage=releaseOkay;
    for(auto& contour:instance.config.contours)contour={{.25f,.25f},{.75f,.25f},{.75f,.75f},{.25f,.75f}};
    instance.lmuIntegrated=instance.lmuQuad=true;
    const auto sessionHandle=reinterpret_cast<XrSession>(321);
    sessions[sessionHandle]=std::make_unique<Session>();auto& session=*sessions[sessionHandle];session.instance=&instance;
    require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&session.device,nullptr,&session.context)),"WARP");
    D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=64;d.ArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> texture;require(SUCCEEDED(session.device->CreateTexture2D(&d,nullptr,&texture)),"texture");
    ComPtr<ID3D11RenderTargetView> target;require(SUCCEEDED(session.device->CreateRenderTargetView(texture.Get(),nullptr,&target)),"target");
    const float white[4]={1,1,1,1};session.context->ClearRenderTargetView(target.Get(),white);
    og::lmu::Frame frame;frame.id=1;frame.time=12345;
    for(unsigned slot=0;slot<4;++slot){frame.views[slot].fov={float(std::atan(-1.)),float(std::atan(1.)),float(std::atan(1.)),float(std::atan(-1.))};frame.widths[slot]=frame.heights[slot]=64;}
    frame.fullOuterFov[0]=frame.fullOuterFov[1]=frame.views[0].fov;
    require(lmu_status(session).find("\"renderedFov\":[]")!=std::string::npos,"startup never reports zero FOVs as real geometry");
    lmu_image(sessionHandle,frame,2,texture.Get(),d.Format,{0,0,0,64,64});
    require((pixel(session.device.Get(),session.context.Get(),texture.Get(),2,2)&0xffffff)==0,"focus final image must apply full-eye aperture before release");
    require((pixel(session.device.Get(),session.context.Get(),texture.Get(),32,32)&0xffffff)==0xffffff,"focus aperture preserves visible rays");
    // A moving focus must sample the aperture in full-eye rays, never recenter it.
    frame.id=2;frame.views[2].fov={float(std::atan(.4)),float(std::atan(.8)),float(std::atan(.2)),float(std::atan(-.2))};
    session.context->ClearRenderTargetView(target.Get(),white);
    D3D11_VIEWPORT before{3,4,12,13,.2f,.8f};session.context->RSSetViewports(1,&before);
    lmu_image(sessionHandle,frame,2,texture.Get(),d.Format,{0,0,0,64,64});
    require((pixel(session.device.Get(),session.context.Get(),texture.Get(),8,32)&0xffffff)==0xffffff,"left focus rays inside aperture remain visible");
    require((pixel(session.device.Get(),session.context.Get(),texture.Get(),56,32)&0xffffff)==0,"right gaze-edge focus rays outside aperture stay hidden");
    D3D11_VIEWPORT after{};UINT count=1;session.context->RSGetViewports(&count,&after);
    require(count==1&&std::memcmp(&before,&after,sizeof(before))==0,"aperture restores complete viewport state");
    // Non-idempotent color grading must happen once even when a release retries.
    instance.config.visorColor={true,100,50,0}; // New correction must replace the legacy grade.
    instance.config.color.enabled=true;instance.config.color.preset=og::ColorPreset::Sun;
    instance.config.color.presets[1]={true,100,80,100,100};
    session.colorRuntime=std::make_unique<og::ColorRuntime>(instance.config.color);
    session.processor=std::make_unique<og::ImageProcessor>(session.device.Get(),session.context.Get());
    frame.id=3;frame.views[2].fov=frame.views[0].fov;
    session.context->ClearRenderTargetView(target.Get(),white);
    lmu_image(sessionHandle,frame,2,texture.Get(),d.Format,{0,0,0,64,64});
    const auto processed=pixel(session.device.Get(),session.context.Get(),texture.Get(),32,32);
    lmu_image(sessionHandle,frame,2,texture.Get(),d.Format,{0,0,0,64,64});
    require(session.visorColorAppliedCalls==1&&pixel(session.device.Get(),session.context.Get(),texture.Get(),32,32)==processed,"focus grading is not duplicated");
    require((processed&255)>190&&(processed&255)<220,"focus image actually graded");
    // Raw output owns its copy before runtime reuse; its metadata uses cropped
    // rays and the final submitted compensated pose, not locate pose or padding.
    require(session.channel.open("lmu-test-"+std::to_string(GetCurrentProcessId())),"channel");
    require(session.channel.lock(),"channel lock");
    *session.channel.data()=og::SharedFrame{};
    session.channel.data()->consumer=GetCurrentProcessId();session.channel.data()->consumerBeat=GetTickCount64();session.channel.unlock();session.nextViewerProbe=0;
    frame.id=4;frame.views[0].fov.angleUp=float(std::atan(.84));frame.views[0].fov.angleDown=float(std::atan(-.58));
    const float red[4]={1,0,0,1},blue[4]={0,0,1,1};session.context->ClearRenderTargetView(target.Get(),red);
    lmu_image(sessionHandle,frame,0,texture.Get(),d.Format,{0,0,0,64,64});
    require(session.lmuSnapshotFrame[0]==4,"outer snapshot tagged with rendered frame");
    require(lmu_status(session).find("\"focusFovValid\":false")!=std::string::npos,"partial rendered image is not a completed submission");
    session.colorRuntime->cycle(); // Midframe command must wait until all four views finish.
    session.context->ClearRenderTargetView(target.Get(),blue);
    require((pixel(session.device.Get(),session.context.Get(),session.lmuSnapshots[0].Get(),2,2)&0xffffff)==255,"owned raw outer survives mask, grading and runtime reuse");
    frame.views[2].fov={float(std::atan(-.4)),float(std::atan(.4)),float(std::atan(.4)),float(std::atan(-.4))};
    lmu_image(sessionHandle,frame,2,texture.Get(),d.Format,{0,0,0,64,64});
    require(session.colorRuntime->frameColor().gamma==100,"quad focus retains outer-frame Sun after a midframe cycle");
    instance.config.width=instance.config.height=128;
    XrCompositionLayerProjectionView submittedViews[4]{};
    for(auto& view:submittedViews){view.pose.orientation.w=1;view.pose.position.x=7;view.subImage.imageRect={{0,0},{64,64}};}
    XrCompositionLayerProjection submitted{XR_TYPE_COMPOSITION_LAYER_PROJECTION};submitted.viewCount=4;submitted.views=submittedViews;submitted.space=reinterpret_cast<XrSpace>(19);
    lmu_submit(sessionHandle,frame,submitted,frame.time);
    require(session.channel.lock(),"published channel");const auto published=*session.channel.data();session.channel.unlock();
    require(published.sequence==1&&published.position[0]==7&&published.reference==19,"stream preserves submitted pose and space");
    require(published.width>=128&&published.height>=128,"focus stream must not be reduced to low-resolution outer dimensions");
    require(session.sharedKey->AcquireSync(1,1000)==S_OK,"consume shared frame");
    require((pixel(session.device.Get(),session.context.Get(),session.sharedTexture.Get(),published.width/2,published.height/2)&0xffffff)==0xff0000,"clean output includes raw blue focus, not red outer or graded/masked focus");
    require((pixel(session.device.Get(),session.context.Get(),session.sharedTexture.Get(),2,2)&0xffffff)==255,"clean output preserves outer outside focus");
    session.sharedKey->ReleaseSync(0);
    require(std::abs(published.fov[2]-.84f)<1e-5&&std::abs(published.fov[3]+.58f)<1e-5,"cropped outer texture published with cropped FOV");
    instance.game="lmu";instance.lmuProvider="native";
    const auto statusDirectory=std::filesystem::temp_directory_path()/(L"opengrid-lmu-status-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(statusDirectory);SetEnvironmentVariableW(L"OPENGRIDXR_STATUS_DIR",statusDirectory.c_str());
    status(session,true);
    std::ifstream statusFile(statusDirectory/L"OpenGridXR-status-lmu.json");std::string statusJson((std::istreambuf_iterator<char>(statusFile)),{});
    require(statusJson.find("\"lmuRendering\":{\"mode\":\"quad\"")!=std::string::npos&&statusJson.find("\"focusFovValid\":true")!=std::string::npos&&
        statusJson.find("\"renderedFov\":[{\"angleLeft\":")!=std::string::npos,"written runtime JSON exposes completed geometry with OpenXR field names");
    require(statusJson.find("\"outerSizes\":[{\"width\":64,\"height\":64}")!=std::string::npos,"runtime JSON exposes real cropped dimensions");
    SetEnvironmentVariableW(L"OPENGRIDXR_STATUS_DIR",nullptr);
    ++frame.id;lmu_submit(sessionHandle,frame,submitted,frame.time);
    require(session.capturedFrames==1&&session.lmuCaptureState=="skipped","missing next-frame outer never republishes stale pixels");
    auto consume=[&](){
        require(session.channel.lock(),"consume lock");auto result=*session.channel.data();session.channel.data()->ready=0;session.channel.unlock();
        require(session.sharedKey->AcquireSync(1,1000)==S_OK,"consume next shared frame");return result;
    };
    // Right-eye moving focus: use its asymmetric rendered rays, not a centered
    // rectangle or the left eye's image. No extra copy for the unselected eye.
    require(session.channel.lock(),"clear channel");session.channel.data()->ready=0;session.channel.unlock();
    instance.config.eye=1;session.lastCapture=0;frame.id=6;
    frame.views[3].fov={float(std::atan(.3)),float(std::atan(.8)),float(std::atan(.4)),float(std::atan(-.4))};
    session.context->ClearRenderTargetView(target.Get(),red);lmu_image(sessionHandle,frame,1,texture.Get(),d.Format,{0,0,0,64,64});
    session.context->ClearRenderTargetView(target.Get(),blue);lmu_image(sessionHandle,frame,3,texture.Get(),d.Format,{0,0,0,64,64});
    require(session.lmuSnapshotFrame[0]==4,"unselected eye is not copied");
    require(session.colorRuntime->frameColor().gamma==135,"next quad frame adopts Night for outer and focus together");
    session.context->RSSetViewports(1,&before);
    lmu_submit(sessionHandle,frame,submitted,frame.time);auto right=consume();
    require(right.captureEye==1&&session.lmuCaptureState=="ready-focus","right eye focus composed");
    require((pixel(session.device.Get(),session.context.Get(),session.sharedTexture.Get(),right.width/2,right.height/2)&0xffffff)==255,"gaze-shifted focus leaves center outer visible");
    require((pixel(session.device.Get(),session.context.Get(),session.sharedTexture.Get(),right.width*3/4,right.height/2)&0xffffff)==0xff0000,"gaze-shifted focus follows right-eye tangent FOV");
    count=1;session.context->RSGetViewports(&count,&after);require(count==1&&std::memcmp(&before,&after,sizeof(before))==0,"stream restores game viewport");
    session.sharedKey->ReleaseSync(0);
    // A missing current focus must never reuse blue from the preceding frame.
    frame.id=7;session.lastCapture=0;session.context->ClearRenderTargetView(target.Get(),red);
    lmu_image(sessionHandle,frame,1,texture.Get(),d.Format,{0,0,0,64,64});lmu_submit(sessionHandle,frame,submitted,frame.time);auto fallback=consume();
    require(fallback.width==64&&session.lmuCaptureState=="ready-outer-only","missing focus reports explicit fallback");
    require((pixel(session.device.Get(),session.context.Get(),session.sharedTexture.Get(),48,32)&0xffffff)==255,"missing focus never blends stale pixels");
    session.sharedKey->ReleaseSync(0);
    // An atlas subrectangle is the complete focus image; surrounding pixels
    // must not leak in, even near the focus edge.
    auto rawFocus=session.lmuSnapshots[3];
    std::vector<uint32_t> green(64*64,0xff00ff00),blueInset(16*16,0xffff0000);
    session.context->UpdateSubresource(rawFocus.Get(),0,nullptr,green.data(),64*4,0);
    D3D11_BOX inset{8,16,0,24,32,1};session.context->UpdateSubresource(rawFocus.Get(),0,&inset,blueInset.data(),16*4,0);
    XrPosef pose{};pose.orientation.w=1;
    auto composite=session.lmuStream->compose(session.lmuSnapshots[1].Get(),rawFocus.Get(),frame.views[1].fov,frame.views[3].fov,pose,pose,{{8,16},{16,16}},128,128);
    require(composite&&(pixel(session.device.Get(),session.context.Get(),composite,96,64)&0xffffff)==0xff0000,"focus subrectangle maps without surrounding atlas pixels");
    // Relative camera rotation moves the patch in ray space, not screen space.
    auto rotated=pose;rotated.orientation.y=float(std::sin(.15));rotated.orientation.w=float(std::cos(.15));
    composite=session.lmuStream->compose(session.lmuSnapshots[1].Get(),rawFocus.Get(),frame.views[1].fov,frame.views[3].fov,pose,rotated,{{8,16},{16,16}},128,128);
    require(composite&&(pixel(session.device.Get(),session.context.Get(),composite,72,64)&0xffffff)==0xff0000,"submitted relative orientation reprojects focus");
    require((pixel(session.device.Get(),session.context.Get(),composite,108,64)&0xffffff)==255,"focus rotation does not leave an old patch");
    rotated.position.x=.01f;
    require(!session.lmuStream->compose(session.lmuSnapshots[1].Get(),rawFocus.Get(),frame.views[1].fov,frame.views[3].fov,pose,rotated,{{8,16},{16,16}},128,128),"different eye origins fail closed without depth");
    // sRGB sampling + sRGB target encoding must round-trip stored colors.
    auto srgbDesc=d;srgbDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;srgbDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> srgbOuter,srgbFocus;
    require(SUCCEEDED(session.device->CreateTexture2D(&srgbDesc,nullptr,&srgbOuter))&&SUCCEEDED(session.device->CreateTexture2D(&srgbDesc,nullptr,&srgbFocus)),"sRGB sources");
    std::vector<uint32_t> gray(64*64,0xff808080);
    session.context->UpdateSubresource(srgbOuter.Get(),0,nullptr,gray.data(),64*4,0);session.context->UpdateSubresource(srgbFocus.Get(),0,nullptr,gray.data(),64*4,0);
    composite=session.lmuStream->compose(srgbOuter.Get(),srgbFocus.Get(),frame.views[1].fov,frame.views[3].fov,pose,pose,{{0,0},{64,64}},128,128);
    require(composite&&(pixel(session.device.Get(),session.context.Get(),composite,96,64)&0xffffff)==0x808080,"sRGB stream brightness survives focus composition");
    // An active viewer with an occupied channel must not cause copies at game
    // frame rate while it is still consuming the previous published image.
    require(session.channel.lock(),"backpressure");session.channel.data()->ready=1;session.channel.data()->published=GetTickCount64();session.channel.data()->consumerBeat=GetTickCount64();session.channel.unlock();
    frame.id=8;session.lastCapture=0;session.context->ClearRenderTargetView(target.Get(),white);
    const auto previousFocusPixel=pixel(session.device.Get(),session.context.Get(),session.lmuSnapshots[3].Get(),32,32);
    lmu_image(sessionHandle,frame,1,texture.Get(),d.Format,{0,0,0,64,64});lmu_image(sessionHandle,frame,3,texture.Get(),d.Format,{0,0,0,64,64});
    require(session.lmuCaptureFrame==0,"occupied stream skips capture admission");
    require((pixel(session.device.Get(),session.context.Get(),session.lmuSnapshots[1].Get(),32,32)&0xffffff)==255&&pixel(session.device.Get(),session.context.Get(),session.lmuSnapshots[3].Get(),32,32)==previousFocusPixel,"occupied stream performs no outer/focus copies");
    require(session.channel.lock(),"consumed");session.channel.data()->ready=0;session.channel.unlock();
    frame.id=9;lmu_image(sessionHandle,frame,1,texture.Get(),d.Format,{0,0,0,64,64});lmu_image(sessionHandle,frame,3,texture.Get(),d.Format,{0,0,0,64,64});
    require(session.lmuSnapshotFrame[1]==9&&session.lmuSnapshotFrame[3]==9,"capture resumes after consumer releases frame");
    require(session.channel.lock(),"stale");session.channel.data()->ready=1;session.channel.data()->published=GetTickCount64()-1001;session.channel.unlock();
    frame.id=10;lmu_image(sessionHandle,frame,1,texture.Get(),d.Format,{0,0,0,64,64});lmu_image(sessionHandle,frame,3,texture.Get(),d.Format,{0,0,0,64,64});
    require(session.lmuSnapshotFrame[1]==10&&session.lmuSnapshotFrame[3]==10,"stale-ready recovery still admitted");
    lmu_submit(sessionHandle,frame,submitted,frame.time);consume();session.sharedKey->ReleaseSync(0);
    // No active viewer: neither focus nor outer gets an extra streaming copy.
    require(session.channel.lock(),"disconnect");session.channel.data()->consumerBeat=0;session.channel.unlock();session.nextViewerProbe=0;
    frame.id=11;session.lastCapture=0;lmu_image(sessionHandle,frame,1,texture.Get(),d.Format,{0,0,0,64,64});lmu_image(sessionHandle,frame,3,texture.Get(),d.Format,{0,0,0,64,64});
    require(session.lmuSnapshotFrame[1]==0&&session.lmuSnapshotFrame[3]==0,"stream snapshots disabled without viewer");
    sessions.clear();chains.clear();std::cout<<"Integrated image aperture PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
