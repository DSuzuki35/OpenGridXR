#include "../src/layer.cpp"
#include "lmu_cockpit_capture.hpp"
#include <iostream>
#include <stdexcept>
#include <future>
#include <chrono>
#include <d3d11sdklayers.h>

using Microsoft::WRL::ComPtr;
using namespace og;
using namespace og::lmu;
namespace {
CockpitCapture observer{};
int installs{}, disables{};
void check(bool ok,const char* msg) { if (!ok) throw std::runtime_error(msg); }
void ok(HRESULT result) { check(SUCCEEDED(result),"D3D11 operation failed"); }

ComPtr<ID3DBlob> compile(const char* source,const char* profile) {
    ComPtr<ID3DBlob> bytecode,errors;
    auto hr=D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"main",profile,0,0,&bytecode,&errors);
    if (FAILED(hr) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    ok(hr);
    return bytecode;
}

struct Fixture {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> depth;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11ShaderResourceView> srv;
    D3D11_VIEWPORT viewport{0,0,8,8,0,1};
    XrSession session=reinterpret_cast<XrSession>(1);
    XrSpace space=reinterpret_cast<XrSpace>(2);
    XrView views[2]{};
    XrCompositionLayerProjectionView eyes[2]{};
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    const XrCompositionLayerBaseHeader* header=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    CockpitDepthFrame result{};
    CockpitProjection projection{1,0,0,0,0,1,0,0,-.1f,.2f,0,1,0,0,.1f,0};
    bool direct;
    explicit Fixture(UINT samples, bool directRegistration=true) : direct(directRegistration) {
        auto createGpu=[&](UINT flags) {
            return D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
        };
        const auto hr=createGpu(D3D11_CREATE_DEVICE_DEBUG);
        if (hr==DXGI_ERROR_SDK_COMPONENT_MISSING) ok(createGpu(0)); else ok(hr);
        for (unsigned i=0;i<2;++i) {
            views[i].pose.orientation.w=1;
            views[i].pose.position.x=i?.03f:-.03f;
            eyes[i].pose=views[i].pose;
        }
        layer.space=space; layer.views=eyes; layer.viewCount=2;
        end.layers=&header; end.layerCount=1;
        D3D11_TEXTURE2D_DESC d{};
        d.Width=d.Height=8; d.Format=DXGI_FORMAT_R32_TYPELESS;
        d.SampleDesc.Count=samples; d.ArraySize=d.MipLevels=1;
        d.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
        ok(device->CreateTexture2D(&d,nullptr,&depth));
        D3D11_DEPTH_STENCIL_VIEW_DESC v{};
        v.Format=DXGI_FORMAT_D32_FLOAT;
        v.ViewDimension=samples==1?D3D11_DSV_DIMENSION_TEXTURE2D:D3D11_DSV_DIMENSION_TEXTURE2DMS;
        ok(device->CreateDepthStencilView(depth.Get(),&v,&dsv));
        D3D11_SHADER_RESOURCE_VIEW_DESC s{};
        s.Format=DXGI_FORMAT_R32_FLOAT;
        s.ViewDimension=samples==1?D3D11_SRV_DIMENSION_TEXTURE2D:D3D11_SRV_DIMENSION_TEXTURE2DMS;
        if (samples==1) s.Texture2D.MipLevels=1;
        ok(device->CreateShaderResourceView(depth.Get(),&s,&srv));
        if (direct) cockpit_open(session,device.Get());
    }
    ~Fixture() { release(); if (direct) cockpit_close(session); }
    void release() {
        for(auto& eye:result.eyes) if(eye.view) {eye.view->Release();eye.view=nullptr;}
    }
    void verifyDebugState() {
        ComPtr<ID3D11InfoQueue> messages;
        if (FAILED(device.As(&messages))) return;
        const auto count=messages->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 i=0;i<count;++i) {
            SIZE_T size=0; ok(messages->GetMessage(i,nullptr,&size));
            std::vector<char> data(size);
            auto* message=reinterpret_cast<D3D11_MESSAGE*>(data.data());
            ok(messages->GetMessage(i,message,&size));
            if (message->Severity<=D3D11_MESSAGE_SEVERITY_WARNING)
                throw std::runtime_error(message->pDescription);
        }
    }
    void locate(XrTime time) {
        XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO};
        info.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; info.space=space; info.displayTime=time;
        XrViewState state{XR_TYPE_VIEW_STATE};
        state.viewStateFlags=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
        cockpit_locate(session,&info,&state,views,true);
    }
    void copy(unsigned index) { observer(index,projection,context.Get(),depth.Get(),srv.Get(),viewport); }
    bool submit(XrTime time) {
        release(); end.displayTime=time; cockpit_submit(session,&end);
        return cockpit_request(device.Get(),session,time,true,&result);
    }
    float read(unsigned eye, UINT x=0, UINT y=0) {
        ComPtr<ID3D11Resource> resource; result.eyes[eye].view->GetResource(&resource);
        ComPtr<ID3D11Texture2D> texture; ok(resource.As(&texture));
        D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d);
        d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ; d.BindFlags=0;
        ComPtr<ID3D11Texture2D> staging; ok(device->CreateTexture2D(&d,nullptr,&staging));
        context->CopyResource(staging.Get(),texture.Get());
        D3D11_MAPPED_SUBRESOURCE map{}; ok(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));
        check(x<d.Width && y<d.Height,"depth read outside texture");
        const auto row=static_cast<const char*>(map.pData)+y*map.RowPitch;
        const float value=reinterpret_cast<const float*>(row)[x];
        context->Unmap(staging.Get(),0); return value;
    }
    void msaaScene() {
        auto vsCode=compile("float4 main(uint i:SV_VertexID):SV_Position{return float4(i==2?3:-1,i==1?3:-1,0,1);}","vs_5_0");
        auto psCode=compile("float main(float4 p:SV_Position,uint i:SV_SampleIndex):SV_Depth{return .1f + .2f*i + .01f*uint(p.x) + .001f*uint(p.y);}","ps_5_0");
        ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps;
        ok(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs));
        ok(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps));
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.MultisampleEnable=TRUE;
        ComPtr<ID3D11RasterizerState> rs; ok(device->CreateRasterizerState(&rd,&rs));
        context->RSSetState(rs.Get());context->RSSetViewports(1,&viewport);
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->OMSetRenderTargets(0,nullptr,dsv.Get());
        context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,1,0); context->Draw(3,0);
    }
};
Fixture* hostFixture{};
LmuCockpitDepthFn exported{};
bool submitted{};
void hostRouting() {
    Fixture f(1,false);
    hostFixture=&f;
    exported=reinterpret_cast<LmuCockpitDepthFn>(GetProcAddress(GetModuleHandleW(nullptr),"OpenGridLmuCockpitDepthV1"));
    check(exported!=nullptr,"could not resolve provider ABI");
    auto instance=std::make_unique<Instance>();
    const auto h=reinterpret_cast<XrInstance>(31);
    instance->handle=h; instance->game="lmu"; instance->engineLoaded=true; instance->config.enabled=true;
    instance->CreateSession=[](XrInstance,const XrSessionCreateInfo*,XrSession* out){*out=hostFixture->session;return XR_SUCCESS;};
    instance->DestroySession=[](XrSession){return XR_SUCCESS;};
    instance->LocateViews=[](XrSession,const XrViewLocateInfo*,XrViewState* state,uint32_t,uint32_t* count,XrView* views){
        *count=2;state->viewStateFlags=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
        for (unsigned i=0;i<2;++i) {views[i]=hostFixture->views[i];views[i].fov={-.7f,.7f,.7f,-.7f};}
        return XR_SUCCESS;
    };
    instance->EndFrame=[](XrSession session,const XrFrameEndInfo* info){
        auto& f=*hostFixture;
        submitted=exported(f.device.Get(),session,info->displayTime,true,&f.result);
        return XR_SUCCESS;
    };
    instances[h]=std::move(instance);
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};binding.device=f.device.Get();
    XrSessionCreateInfo createInfo{XR_TYPE_SESSION_CREATE_INFO};createInfo.next=&binding;
    XrSession session{};
    check(create_session(h,&createInfo,&session)==XR_SUCCESS && session==f.session,"ordinary stereo session creation");
    instances[h]->game.clear(); // Suppress production status-file writes in this fixture.
    check(!observer,"ordinary OpenXR session installed a hook before demand");
    check(!capture_cockpit_depth(*sessions.at(session),f.end,f.result) && !observer,
          "reading inactive stream depth installed an observer");
    f.end.displayTime=99;
    check(end_frame(session,&f.end)==XR_SUCCESS && !submitted && observer,"first downstream request arms next scene");
    XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO};info.space=f.space;info.displayTime=100;
    info.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    XrViewState state{XR_TYPE_VIEW_STATE};XrView views[2]{};uint32_t count{};
    check(locate_views(session,&info,&state,2,&count,views)==XR_SUCCESS,"host locate");
    f.copy(0); f.copy(1); f.end.displayTime=100;
    check(end_frame(session,&f.end)==XR_SUCCESS && submitted,"downstream did not see sealed current stereo pair");
    f.release();
    check(!exported(f.device.Get(),session,100,true,&f.result),"frame not retired after downstream submit");
    check(!capture_cockpit_depth(*sessions.at(session),f.end,f.result),"stream reused a retired frame");
    check(destroy_session(session)==XR_SUCCESS && !observer,"session teardown did not retire observer");
    check(!exported(f.device.Get(),session,100,true,&f.result),"retired session accepted request");
    instances.erase(h); hostFixture=nullptr;
}
void concurrentSubmission() {
    using namespace std::chrono_literals;
    Fixture f(1);
    cockpit_request(f.device.Get(),f.session,0,true,nullptr);
    f.locate(200);
    ComPtr<ID3D11Multithread> guard; ok(f.context.As(&guard));
    guard->SetMultithreadProtected(TRUE);
    guard->Enter();
    std::promise<void> started;
    auto ready=started.get_future();
    auto copying=std::async(std::launch::async,[&]{started.set_value();f.copy(0);});
    ready.get();
    const bool blocked=copying.wait_for(40ms)==std::future_status::timeout;
    auto submit=std::async(std::launch::async,[&]{return f.submit(200);});
    const bool completed=submit.wait_for(2s)==std::future_status::ready;
    guard->Leave();
    copying.get();
    check(blocked && completed && !submit.get(),"submission waited for in-flight GPU capture or exposed a partial pair");
}
}
namespace og::lmu {
bool cockpit_observer(CockpitCapture next) { observer=next; if(next) ++installs; else ++disables; return true; }
XrResult XRAPI_CALL get_proc(XrInstance,const char*,PFN_xrVoidFunction*) { return XR_ERROR_FUNCTION_UNSUPPORTED; }
XrResult XRAPI_CALL create_instance(const XrInstanceCreateInfo*,const XrApiLayerCreateInfo*,XrInstance*) {
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}
}

int main() try {
    {
        Fixture f(1);
        check(!observer,"observer installed while disabled");
        cockpit_request(f.device.Get(),f.session,0,true,nullptr);
        check(observer && installs==1,"demand did not arm capture");
        f.locate(101);
        f.context->ClearDepthStencilView(f.dsv.Get(),D3D11_CLEAR_DEPTH,.25f,0); f.copy(0);
        f.context->ClearDepthStencilView(f.dsv.Get(),D3D11_CLEAR_DEPTH,.75f,0); f.copy(1);
        check(f.submit(101) && f.result.space==f.space && f.result.time==101,"stereo pair unavailable");
        check(f.read(0)==.25f && f.read(1)==.75f,"shared source overwrote first eye");
        CockpitDepthFrame stream{};
        check(!cockpit_read(reinterpret_cast<XrSession>(3),101,stream) && !cockpit_read(f.session,102,stream),
              "stream returned another session or time");
        check(cockpit_read(f.session,101,stream) && stream.eyes[0].view==f.result.eyes[0].view &&
              stream.eyes[1].view==f.result.eyes[1].view,"stream and headset selected different depth");
        for (auto& eye : stream.eyes) eye.view->Release();
        check(f.result.eyes[0].invNear==0 && f.result.eyes[0].invFar==10 &&
              f.result.eyes[1].pose.position.x==.03f && f.result.eyes[0].rect.extent.width==8,"eye mapping incorrect");
        f.locate(102); f.copy(0); // overlapping locate/capture during submission must not overwrite either image
        check(f.read(0)==.25f && f.read(1)==.75f,"submitting pair overwritten");
        cockpit_submit(f.session,nullptr);
        f.locate(103); f.copy(0); check(!f.submit(103),"missing eye reused prior depth");
        cockpit_submit(f.session,nullptr);
        f.locate(104); f.copy(0);f.copy(1);check(!f.submit(105),"mismatched frame time accepted");
        cockpit_submit(f.session,nullptr);
        f.locate(106); f.copy(1);f.copy(0);check(!f.submit(106),"out of order pair accepted");
        cockpit_request(f.device.Get(),f.session,0,false,nullptr);
        check(!observer && disables==1,"observer active after disabling");
        f.verifyDebugState();
    }
    {
        Fixture f(4);
        cockpit_request(f.device.Get(),f.session,0,true,nullptr);
        f.locate(110);f.msaaScene();f.copy(0);
        f.projection[10]=1; f.projection[14]=-.1f; f.copy(1);
        ComPtr<ID3D11DepthStencilView> bound;
        f.context->OMGetRenderTargets(0,nullptr,&bound);
        check(bound.Get()==f.dsv.Get(),"MSAA resolve changed engine render target");
        check(f.submit(110),"MSAA pair missing");
        check(std::abs(f.read(0)-.7f)<1e-5f && std::abs(f.read(1)-.1f)<1e-5f,"MSAA nearest sample in reverse/ordinary depth");
        cockpit_submit(f.session,nullptr);
        f.locate(111);
        f.viewport={2,1,5,3,0,1};
        f.copy(0);
        f.viewport={1,4,5,3,0,1};
        f.copy(1);
        check(f.submit(111),"cropped MSAA pair missing");
        for (auto& eye:f.result.eyes) {
            ComPtr<ID3D11Resource> resource; eye.view->GetResource(&resource);
            ComPtr<ID3D11Texture2D> texture; ok(resource.As(&texture));
            D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
            check(desc.Width==5 && desc.Height==3 && eye.rect.offset.x==0 && eye.rect.offset.y==0 &&
                  eye.rect.extent.width==5 && eye.rect.extent.height==3,"resolved viewport bounds");
        }
        check(std::abs(f.read(0)-.121f)<1e-5f && std::abs(f.read(0,4,2)-.163f)<1e-5f &&
              std::abs(f.read(1)-.114f)<1e-5f,"cropped viewport did not retain exact native pixels");
        cockpit_submit(f.session,nullptr);
        f.locate(112);
        f.viewport={3,0,5,3,0,1};
        f.copy(0); f.copy(1);
        check(f.submit(112) && std::abs(f.read(0)-.13f)<1e-5f,"resolve reused a stale viewport origin");
        f.verifyDebugState();
    }
    hostRouting();
    concurrentSubmission();
    std::cout<<"LMU stereo capture, frame ownership, MSAA direction, D3D11 state and host routing passed\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
