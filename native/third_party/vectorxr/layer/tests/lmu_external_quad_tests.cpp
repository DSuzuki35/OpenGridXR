// OpenGrid regression: use the real private-engine entry points with an
// external quad provider whose manifest extensions cannot be probed via GIPA.
#define main bridge_test_main
#include "d3d11_bridge_layer_tests.cpp"
#undef main

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
void RunExternal(Game& game,const char* provider,bool integrated) {
    BeginCase(std::string(provider)+(integrated?"-integrated":"-ordinary"),game,false);
    SetEnv("OPENGRIDXR_LMU_INTEGRATED",integrated?"1":"0");SetEnv("OPENGRIDXR_LMU_PROVIDER",provider);SetEnv("LMU_QV_MODE","quad");
    SetEnv("OPENGRIDXR_ENHANCEMENTS_D3D11_BRIDGE","off");
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
    StopApp(app);EndCase(game);
}
}
int main(){Game game;Check(MakeGame(false,game),"WARP game device");IsolateHost();
    RunExternal(game,"qvfr",true);RunExternal(game,"native",true);RunExternal(game,"qvfr",false);RunExternal(game,"vectorxr",true);
    Check(externalSubmissions==2,"both external providers reached downstream submission");
    std::cout<<"Integrated LMU external quad lifecycle passed\n";return 0;
}
