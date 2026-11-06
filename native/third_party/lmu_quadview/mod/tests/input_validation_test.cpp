// 0.5.1: spec-invalid application input must be rejected, never dereferenced.
// Fake inputs only: no runtime, device, hooks or headset.
#define main submission_regression_main
#include "frame_submission_test.cpp"
#undef main
#include <stdexcept>
#include <iostream>
namespace {
void require(bool valid,const char* why){if(!valid)throw std::runtime_error(why);}
XrResult XRAPI_CALL neverCreate(const XrInstanceCreateInfo*,const XrApiLayerCreateInfo*,XrInstance*){throw std::runtime_error("downstream reached with invalid input");}
}
int main(){
    try{
        // xrCreateApiLayerInstance: null extension list or null entry.
        XrApiLayerNextInfo next{};next.structType=XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO;next.structVersion=XR_API_LAYER_NEXT_INFO_STRUCT_VERSION;
        next.structSize=sizeof(next);next.nextCreateApiLayerInstance=neverCreate;
        XrApiLayerCreateInfo layer{};layer.structType=XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO;layer.structVersion=XR_API_LAYER_CREATE_INFO_STRUCT_VERSION;
        layer.structSize=sizeof(layer);layer.nextInfo=&next;
        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};XrInstance instance=XR_NULL_HANDLE;
        info.enabledExtensionCount=2;info.enabledExtensionNames=nullptr;
        require(layerCreateApiLayerInstance(&info,&layer,&instance)==XR_ERROR_VALIDATION_FAILURE,"null extension list accepted");
        const char* names[]{"XR_KHR_D3D11_enable",nullptr};info.enabledExtensionNames=names;
        require(layerCreateApiLayerInstance(&info,&layer,&instance)==XR_ERROR_VALIDATION_FAILURE,"null extension name accepted");
        // xrEndFrame: null info or null layer list with a count (no frame operation in flight).
        require(layerEndFrame(XR_NULL_HANDLE,nullptr)==XR_ERROR_VALIDATION_FAILURE,"null frame-end info accepted");
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.layerCount=1;end.layers=nullptr;
        require(layerEndFrame(XR_NULL_HANDLE,&end)==XR_ERROR_VALIDATION_FAILURE,"null layer list accepted");
        EndFrameLogGate gate;
        for(unsigned frame=1;frame<=250;++frame)
            require(gate.allow(false,0,true,XR_ERROR_RUNTIME_FAILURE,true)==(frame<=10||frame%120==0),"failure log throttle missed its bound");
        require(gate.allow(false,1,true,XR_SUCCESS,false),"recovery state change not logged");
        require(!gate.allow(false,1,true,XR_SUCCESS,false),"stable success logged every frame");
        require(gate.allow(true,1,true,XR_SUCCESS,false),"periodic logging suppressed");
        require(gate.allow(false,1,false,XR_SUCCESS,false),"shouldRender state change not logged");
        std::cout<<"Input validation PASS: invalid extension lists and frame-end layers rejected without dereference\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
