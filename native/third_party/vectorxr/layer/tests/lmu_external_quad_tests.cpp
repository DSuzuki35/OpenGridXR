// OpenGrid regression: use the real private-engine entry points with an
// external quad provider whose manifest extensions cannot be probed via GIPA.
#include "lmu_test_runtime.hpp"

namespace {
constexpr auto quadType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO;
unsigned externalSubmissions = 0;
XrResult XRAPI_CALL ExternalProperties(XrInstance i, XrSystemId id, XrSystemProperties* p) {
    RtGetSystemProperties(i,id,p);
    for(auto* n=static_cast<XrBaseOutStructure*>(p->next);n;n=n->next)
        if(n->type==XR_TYPE_SYSTEM_FOVEATED_RENDERING_PROPERTIES_VARJO)
            reinterpret_cast<XrSystemFoveatedRenderingPropertiesVARJO*>(n)->supportsFoveatedRendering=XR_TRUE;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL ExternalTypes(XrInstance,XrSystemId,uint32_t cap,uint32_t* count,XrViewConfigurationType* types) {
    *count=1;if(cap)types[0]=quadType;return XR_SUCCESS;
}
XrResult XRAPI_CALL ExternalSizes(XrInstance,XrSystemId,XrViewConfigurationType type,uint32_t cap,uint32_t* count,XrViewConfigurationView* views) {
    Check(type==quadType,"external provider receives quad size query unchanged");*count=4;
    for(uint32_t i=0;i<std::min(cap,4u);++i){views[i].recommendedImageRectWidth=300+i;views[i].recommendedImageRectHeight=200+i;
        views[i].maxImageRectWidth=4096;views[i].maxImageRectHeight=4096;views[i].recommendedSwapchainSampleCount=1;views[i].maxSwapchainSampleCount=1;}
    return XR_SUCCESS;
}
XrResult XRAPI_CALL ExternalBegin(XrSession,const XrSessionBeginInfo* info) {
    Check(info->primaryViewConfigurationType==quadType,"external provider begins a quad session, never stereo");return XR_SUCCESS;
}
XrResult XRAPI_CALL ExternalLocate(XrSession,const XrViewLocateInfo* info,XrViewState* state,uint32_t cap,uint32_t* count,XrView* views) {
    Check(info->viewConfigurationType==quadType,"external provider locates quad views unchanged");*count=4;
    state->viewStateFlags=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    for(uint32_t i=0;i<std::min(cap,4u);++i){views[i].pose={{0,0,0,1},{float(i),0,0}};views[i].fov={-.7f,.8f,.6f,-.5f};}
    return XR_SUCCESS;
}
XrResult XRAPI_CALL ExternalEnd(XrSession,const XrFrameEndInfo* info) {
    Check(info->layerCount==1,"one external quad projection survives submission");
    const auto* p=reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[0]);
    Check(p->viewCount==4,"external quad projection is not split or composed by the engine");
    for(unsigned i=0;i<4;++i)Check(p->views[i].pose.position.x==float(i)&&p->views[i].fov.angleLeft==-.7f,"external submitted geometry preserved");
    ++externalSubmissions;return XR_SUCCESS;
}
XrResult XRAPI_CALL ExternalGet(XrInstance instance,const char* name,PFN_xrVoidFunction* function) {
    if(instance==XR_NULL_HANDLE){*function=nullptr;return XR_ERROR_FUNCTION_UNSUPPORTED;}
    const std::map<std::string,PFN_xrVoidFunction> overrides{
        {"xrGetSystemProperties",Function(ExternalProperties)},{"xrEnumerateViewConfigurations",Function(ExternalTypes)},
        {"xrEnumerateViewConfigurationViews",Function(ExternalSizes)},{"xrBeginSession",Function(ExternalBegin)},
        {"xrLocateViews",Function(ExternalLocate)},{"xrEndFrame",Function(ExternalEnd)}};
    const auto at=overrides.find(name);if(at!=overrides.end()){*function=at->second;return XR_SUCCESS;}
    return RtGetInstanceProcAddr(instance,name,function);
}
// Run the native D3D11 headset path through loader-facing entry points. The game
// requests only a color attachment, before its first frame or projection.
void RunHelmetLighting(Game& game, bool liveEnable) {
    BeginCase(liveEnable ? "helmet-live-scene-light" : "helmet-scene-light",game);
    SetEnv("OPENGRIDXR_LMU_INTEGRATED","0");
    App app=StartApp();
    depthxr::OpenXrLayer::Instance().SetLayerDirectory(g_root);
    auto lighting = [&](int strength) {
        std::ofstream sidecar(g_root/"OpenGridXR-helmet-lmu.json");
        sidecar<<R"({"version":2,"enabled":true,"leftImage":"OpenGridXR-helmet-lmu-left.png","rightImage":"OpenGridXR-helmet-lmu-right.png","ambientLight":)"<<strength<<"}";
    };
    lighting(liveEnable ? 0 : 100);
    // A uniform gray 4x4 RGBA PNG with 50% alpha.
    constexpr unsigned char png[]{
        137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82,
        0, 0, 0, 4, 0, 0, 0, 4, 8, 6, 0, 0, 0, 169, 241, 158,
        126, 0, 0, 0, 15, 73, 68, 65, 84, 120, 156, 99, 104, 64, 3, 12,
        164, 11, 0, 0, 64, 128, 32, 1, 202, 129, 118, 187, 0, 0, 0, 0,
        73, 69, 78, 68, 174, 66, 96, 130};
    {std::ofstream art(g_root/"OpenGridXR-helmet-lmu-left.png",std::ios::binary);
     art.write(reinterpret_cast<const char*>(png),sizeof(png));}
    SessionCreate create(game.device.Get(),app.system);
    Check(app.api.create_session(app.api.instance,&create.info,&app.session)==XR_SUCCESS,"helmet test session");
    auto info=ColorInfo();info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    Target source=MakeTarget(app,info,"game scene");
    Check(g_runtime.swapchain_requests.back().usageFlags==(info.usageFlags|XR_SWAPCHAIN_USAGE_SAMPLED_BIT),
          "an enabled helmet can turn on lighting after the game creates its color-only eyes");
    BeginSession(app);
    if (liveEnable) {
        lighting(100);
        Sleep(510); // The renderer checks the saved sidecar twice per second.
    }
    std::vector<Color> submitted;
    g_runtime.on_end_frame=[&](const XrFrameEndInfo& frame) {
        Check(frame.layerCount==3,"runtime receives the game projection and both helmet quads");
        auto* left=reinterpret_cast<const XrCompositionLayerQuad*>(frame.layers[1]);
        auto* right=reinterpret_cast<const XrCompositionLayerQuad*>(frame.layers[2]);
        Check(left->type==XR_TYPE_COMPOSITION_LAYER_QUAD && right->type==left->type &&
              left->subImage.swapchain==right->subImage.swapchain,"both eyes submit one shared lighting result");
        auto* chain=FindSwapchain(left->subImage.swapchain);
        std::vector<Color> texels;
        Check(chain && chain->released && ReadBack11({{chain->images11[*chain->released].Get(),0}},texels),
              "read the helmet actually handed to the runtime");
        submitted.push_back(texels.front());
    };
    auto frame=[&] {
        Check(Frame(app,game,{&source},{{255,0,0,255}},{{{&source,0},{&source,0}}})==XR_SUCCESS,"submit helmet test frame");
    };
    frame();frame();frame();
    const Color neutral{128,128,128,128};
    Check(submitted.size()==3 && submitted.front()==neutral && submitted.back()[0]>submitted.back()[2]+20 &&
          submitted.back()[3]==128,"live-enabled lighting follows red game pixels and preserves artwork alpha");
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    Check(XR_FAILED(app.api.release(source.swapchain,&release)),"runtime rejects inconsistent release");
    frame();frame();
    Check(submitted[3]==neutral && submitted[4]==neutral,"failed release disables subsequent reads from the inconsistent chain");
    g_runtime.on_end_frame=nullptr;
    Check(app.api.destroy_swapchain(source.swapchain)==XR_SUCCESS,"destroy projection source");
    StopApp(app);
}

void RunExternal(Game& game,const char* provider,bool integrated) {
    BeginCase(std::string(provider)+(integrated?"-integrated":"-ordinary"),game);
    SetEnv("OPENGRIDXR_LMU_INTEGRATED",integrated?"1":"0");SetEnv("OPENGRIDXR_LMU_PROVIDER",provider);SetEnv("LMU_QV_MODE","quad");
    const char* extensions[]={kD3D11Extension,kQuadViewsExtension,kFoveatedExtension};
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};info.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
    info.enabledExtensionCount=3;info.enabledExtensionNames=extensions;
    depthxr::XrApiLayerNextInfo next{};next.structType=depthxr::XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO;
    next.structVersion=depthxr::XR_API_LAYER_NEXT_INFO_STRUCT_VERSION;next.structSize=sizeof(next);
    next.nextGetInstanceProcAddr=ExternalGet;next.nextCreateApiLayerInstance=RtCreateApiLayerInstance;
    depthxr::XrApiLayerCreateInfo chain{};chain.structType=depthxr::XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO;
    chain.structVersion=depthxr::XR_API_LAYER_CREATE_INFO_STRUCT_VERSION;chain.structSize=sizeof(chain);chain.nextInfo=&next;
    App app;Check(xrCreateApiLayerInstance(&info,&chain,&app.api.instance)==XR_SUCCESS,"private engine creates instance");
    const auto& forwarded=g_runtime.instance_attempts.front();
    const bool expected=integrated&&std::string(provider)!="vectorxr";
    Check((std::find(forwarded.begin(),forwarded.end(),kQuadViewsExtension)!=forwarded.end())==expected&&
          (std::find(forwarded.begin(),forwarded.end(),kFoveatedExtension)!=forwarded.end())==expected,
          "only integrated external LMU provider retains both quad extensions without a pre-instance probe");
    app.api=Load(app.api.instance);app.system=kSystem;
    if(expected){
        XrSystemFoveatedRenderingPropertiesVARJO foveated{XR_TYPE_SYSTEM_FOVEATED_RENDERING_PROPERTIES_VARJO};
        XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};properties.next=&foveated;
        Check(LayerFunction<PFN_xrGetSystemProperties>(app.api.instance,"xrGetSystemProperties")(app.api.instance,kSystem,&properties)==XR_SUCCESS&&foveated.supportsFoveatedRendering,"external foveation capability survives");
        uint32_t count=0;XrViewConfigurationType type{};
        Check(LayerFunction<PFN_xrEnumerateViewConfigurations>(app.api.instance,"xrEnumerateViewConfigurations")(app.api.instance,kSystem,1,&count,&type)==XR_SUCCESS&&count==1&&type==quadType,"external quad configuration survives");
        XrViewConfigurationView sizes[4]{};for(auto& size:sizes)size.type=XR_TYPE_VIEW_CONFIGURATION_VIEW;
        Check(app.api.enumerate_views(app.api.instance,kSystem,quadType,4,&count,sizes)==XR_SUCCESS&&count==4&&sizes[0].recommendedImageRectWidth==300&&sizes[3].recommendedImageRectWidth==303,"external sizes are neither emulated nor rescaled");
        XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
        Check(app.api.d3d11_requirements(app.api.instance,kSystem,&requirements)==XR_SUCCESS,"D3D11 requirements");
        SessionCreate create(game.device.Get(),kSystem);Check(app.api.create_session(app.api.instance,&create.info,&app.session)==XR_SUCCESS,"external D3D11 session");BeginSession(app,quadType);
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=quadType;locate.displayTime=1;locate.space=app.space;
        XrViewState state{XR_TYPE_VIEW_STATE};XrView views[4]{};for(auto& view:views)view.type=XR_TYPE_VIEW;
        Check(app.api.locate_views(app.session,&locate,&state,4,&count,views)==XR_SUCCESS&&count==4&&views[3].pose.position.x==3,"all external located views survive");
        XrCompositionLayerProjectionView submitted[4]{};for(unsigned i=0;i<4;++i){submitted[i].type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;submitted[i].pose=views[i].pose;submitted[i].fov=views[i].fov;}
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.space=app.space;projection.viewCount=4;projection.views=submitted;
        const auto* base=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=1;end.layerCount=1;end.layers=&base;
        Check(app.api.end_frame(app.session,&end)==XR_SUCCESS,"external four-view end frame");
    }
    StopApp(app);
}
}
int main(){Game game;Check(MakeGame(game),"WARP game device");IsolateHost();
    RunExternal(game,"qvfr",true);RunExternal(game,"native",true);RunExternal(game,"qvfr",false);RunExternal(game,"vectorxr",true);
    RunHelmetLighting(game,false);
    RunHelmetLighting(game,true);
    Check(externalSubmissions==2,"both external providers reached downstream submission");
    std::cout<<"Integrated LMU external quad lifecycle passed\n";return 0;
}
