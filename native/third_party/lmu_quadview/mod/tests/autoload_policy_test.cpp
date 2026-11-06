#include "../autoload_policy.hpp"
#include <cassert>
#include <iostream>
static XrResult XRAPI_CALL getA(XrInstance,const char*,PFN_xrVoidFunction*){return XR_SUCCESS;}
static XrResult XRAPI_CALL getB(XrInstance,const char*,PFN_xrVoidFunction*){return XR_SUCCESS;}
static XrResult XRAPI_CALL getC(XrInstance,const char*,PFN_xrVoidFunction*){return XR_SUCCESS;}
static XrResult XRAPI_CALL make(XrInstanceCreateInfo const*,XrApiLayerCreateInfo const*,XrInstance*){return XR_SUCCESS;}
static bool hasQuad(const XrInstanceCreateInfo* info){
    for(unsigned i=0;i<info->enabledExtensionCount;++i)if(!strcmp(info->enabledExtensionNames[i],XR_VARJO_QUAD_VIEWS_EXTENSION_NAME))return true;
    return false;
}
static XrResult forward(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* layer,XrInstance* instance){
    auto next=*layer;next.nextInfo=layer->nextInfo->next;
    return layer->nextInfo->nextCreateApiLayerInstance(info,&next,instance);
}
static XrResult XRAPI_CALL overlay(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* layer,XrInstance* instance){
    assert(!hasQuad(info)); // The OSD must see LMU's original stereo request.
    return forward(info,layer,instance);
}
static XrResult XRAPI_CALL adapter(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* layer,XrInstance* instance){
    auto modified=*info;const char* extensions[]{XR_VARJO_QUAD_VIEWS_EXTENSION_NAME};
    modified.enabledExtensionCount=1;modified.enabledExtensionNames=extensions;
    return forward(&modified,layer,instance);
}
static XrResult XRAPI_CALL provider(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* layer,XrInstance* instance){
    assert(hasQuad(info));assert(!layer->nextInfo->next);*instance=reinterpret_cast<XrInstance>(uintptr_t(1));return XR_SUCCESS;
}
int main(){
    using namespace lmuqv::autoload;
    assert(keep_layer("unrelated_overlay","QVFR"));
    assert(!keep_layer("XR_APILAYER_MBUCCHIA_quad_views_foveated","QVFR"));
    assert(keep_layer("XR_APILAYER_DIENERTECH_VECTORXR","QVFR"));
    assert(!keep_layer("XR_APILAYER_DIENERTECH_VECTORXR","VectorXR"));
    assert(!keep_layer("XR_APILAYER_DIENERTECH_VECTORXR","Native"));
    assert(!keep_layer("XR_APILAYER_OPENGRIDXR","QVFR"));
    assert(keep_layer("XR_APILAYER_OPENGRIDXR","VRS"));
    assert(!keep_layer("XR_APILAYER_MBUCCHIA_toolkit","VRS"));
    assert(!keep_layer("XR_APILAYER_LMU_quadview","QVFR"));
    assert(quote(L"C:\\a b\\")==L"\"C:\\a b\\\\\"");
    assert(quote(L"a\"b")==L"\"a\\\"b\"");
    assert(quote(L"")==L"\"\"");
    assert(valid_backend(L"QVFR")&&!valid_backend(L"other"));
    {
        Chain order;order.entries.push_back({"XR_APILAYER_DIENERTECH_VECTORXR",getA,overlay});
        order.terminalGet=getA;order.terminalCreate=make;
        Entry selected{"XR_APILAYER_MBUCCHIA_quad_views_foveated",getC,provider};
        order.complete({"XR_APILAYER_LMU_quadview",getB,adapter},&selected);
        assert(order.entries[0].name=="XR_APILAYER_DIENERTECH_VECTORXR"&&order.entries[1].name=="XR_APILAYER_LMU_quadview"&&order.entries[2].name==selected.name);
        XrApiLayerCreateInfo layer{};layer.nextInfo=order.nodes.data();XrInstanceCreateInfo info{};XrInstance instance{};
        assert(order.entries.front().create(&info,&layer,&instance)==XR_SUCCESS&&instance);
        assert(order.entries.front().get==getA&&order.nodes[0].nextGetInstanceProcAddr==getB&&order.nodes[1].nextGetInstanceProcAddr==getC);
    }
    // Removing middle nodes must preserve each retained layer's own callbacks,
    // rather than borrowing the removed predecessor's downstream callbacks.
    XrApiLayerNextInfo input[4]{};
    const char* names[]={"XR_APILAYER_LMU_autostart","XR_APILAYER_OPENGRIDXR","unknown_overlay","XR_APILAYER_MBUCCHIA_quad_views_foveated"};
    for(int i=0;i<4;i++) {
        input[i].structType=XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO;
        input[i].structVersion=XR_API_LAYER_NEXT_INFO_STRUCT_VERSION;input[i].structSize=sizeof(input[i]);
        strcpy_s(input[i].layerName,names[i]);input[i].next=i<3?&input[i+1]:nullptr;
        input[i].nextCreateApiLayerInstance=make;input[i].nextGetInstanceProcAddr=getA;
    }
    input[1].nextGetInstanceProcAddr=getB;input[3].nextGetInstanceProcAddr=getC;
    Chain chain;chain.read(input,"QVFR");
    assert(chain.entries.size()==1&&chain.entries[0].name=="unknown_overlay"&&chain.entries[0].get==getB);
    assert(chain.terminalGet==getC);chain.entries.insert(chain.entries.begin(),{"XR_APILAYER_LMU_quadview",getA,make});chain.link();
    assert(chain.nodes[0].nextGetInstanceProcAddr==getB&&chain.nodes[0].next==&chain.nodes[1]);
    assert(chain.nodes[1].nextGetInstanceProcAddr==getC&&chain.nodes[1].next==nullptr);
    input[3].next=input;bool rejected=false;try{Chain invalid;invalid.read(input,"QVFR");}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    std::cout<<"Autoload policy tests passed\n";
}
