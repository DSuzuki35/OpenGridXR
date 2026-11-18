#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../nv_vrs_sdk.hpp"
#include "../nv_vrs.hpp"
#include <d3dcompiler.h>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <cstdint>
#include <thread>

namespace {
template<class T>struct Com{T* p=nullptr;~Com(){if(p)p->Release();}T* operator->()const{return p;}T** put(){return &p;}};
void check(HRESULT r,const char* what){if(FAILED(r))throw std::runtime_error(std::string(what)+" HRESULT="+std::to_string(r));}
void require(bool value,const char* what){if(!value)throw std::runtime_error(what);}
struct Bench {
    lmuqv::nv_vrs_detail::Api api;
    Com<IDXGIFactory1> factory;Com<IDXGIAdapter1> adapter;
    Com<ID3D11Device> device;Com<ID3D11DeviceContext> context;
    Com<ID3D11Device1> device1;Com<ID3D11DeviceContext1> context1;
    Com<ID3D11VertexShader> vs;Com<ID3D11PixelShader> ps;
    Com<ID3D11RasterizerState> raster;Com<ID3D11BlendState> blend;Com<ID3D11DepthStencilState> depth;
    Com<ID3D11Texture2D> target;Com<ID3D11RenderTargetView> rtv;
    Com<ID3D11Texture2D> rate0,rate1;
    Com<ID3D11NvShadingRateResourceView> view0,view1;
    unsigned msaa;
    explicit Bench(unsigned samples):msaa(samples){
        check(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(factory.put())),"factory");
        for(UINT i=0;;++i){IDXGIAdapter1* candidate=nullptr;if(factory->EnumAdapters1(i,&candidate)==DXGI_ERROR_NOT_FOUND)break;
            DXGI_ADAPTER_DESC1 desc{};candidate->GetDesc1(&desc);
            if(desc.VendorId==0x10de){adapter.p=candidate;std::wcout<<L"adapter="<<desc.Description<<L"\n";break;}candidate->Release();}
        require(adapter.p,"NVIDIA adapter unavailable");
        D3D_FEATURE_LEVEL feature{};check(D3D11CreateDevice(adapter.p,D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,device.put(),&feature,context.put()),"device");
        check(device->QueryInterface(__uuidof(ID3D11Device1),reinterpret_cast<void**>(device1.put())),"device1");
        check(context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(context1.put())),"context1");
        require(api.supported(device.p),"NVAPI VRS unavailable");
        wchar_t module[32768]{};GetModuleFileNameW(api.module,module,32768);std::wcout<<L"nvapi="<<module<<L"\n";
        const char* source="float4 vs(uint i:SV_VertexID):SV_Position {float2 p=float2((i<<1)&2,i&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);} float4 ps(float4 p:SV_Position):SV_Target{return float4(p.x/256,p.y/256,.25,1);}";
        Com<ID3DBlob> vb,pb,errors;check(D3DCompile(source,std::strlen(source),"vrs-test",nullptr,nullptr,"vs","vs_5_0",0,0,vb.put(),errors.put()),"compile vs");
        check(D3DCompile(source,std::strlen(source),"vrs-test",nullptr,nullptr,"ps","ps_5_0",0,0,pb.put(),nullptr),"compile ps");
        check(device->CreateVertexShader(vb->GetBufferPointer(),vb->GetBufferSize(),nullptr,vs.put()),"vs");
        check(device->CreatePixelShader(pb->GetBufferPointer(),pb->GetBufferSize(),nullptr,ps.put()),"ps");
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;rd.MultisampleEnable=samples>1;
        check(device->CreateRasterizerState(&rd,raster.put()),"raster");D3D11_BLEND_DESC bd{};bd.RenderTarget[0].RenderTargetWriteMask=15;check(device->CreateBlendState(&bd,blend.put()),"blend");
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthFunc=D3D11_COMPARISON_ALWAYS;check(device->CreateDepthStencilState(&dd,depth.put()),"depth");
        UINT levels=0;check(device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM,samples,&levels),"MSAA caps");require(levels>0,"MSAA unsupported");
        D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=256;td.ArraySize=td.MipLevels=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=samples;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        check(device->CreateTexture2D(&td,nullptr,target.put()),"target");check(device->CreateRenderTargetView(target.p,nullptr,rtv.put()),"rtv");
        td.Width=td.Height=16;td.Format=DXGI_FORMAT_R8_UINT;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;td.Usage=D3D11_USAGE_IMMUTABLE;
        unsigned char zero[256]{},one[256]{};for(auto& v:one)v=1;
        D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=zero;initial.SysMemPitch=16;
        check(device->CreateTexture2D(&td,&initial,rate0.put()),"rate0");initial.pSysMem=one;check(device->CreateTexture2D(&td,&initial,rate1.put()),"rate1");
        NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC desc{};desc.version=NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC_VER;desc.Format=DXGI_FORMAT_R8_UINT;desc.ViewDimension=NV_SRRV_DIMENSION_TEXTURE2D;
        require(api.createView(device.p,rate0.p,&desc,view0.put())==NVAPI_OK,"view0");require(api.createView(device.p,rate1.p,&desc,view1.put())==NVAPI_OK,"view1");
    }
    void pipeline(){
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.p,nullptr,0);context->PSSetShader(ps.p,nullptr,0);context->GSSetShader(nullptr,nullptr,0);context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);
        context->RSSetState(raster.p);D3D11_VIEWPORT viewport{0,0,256,256,0,1};context->RSSetViewports(1,&viewport);
        context->OMSetRenderTargets(1,&rtv.p,nullptr);context->OMSetBlendState(blend.p,nullptr,0xffffffff);context->OMSetDepthStencilState(depth.p,0);context->SetPredication(nullptr,FALSE);
    }
    uint64_t draw(){
        pipeline();Com<ID3D11Query> query;D3D11_QUERY_DESC desc{};desc.Query=D3D11_QUERY_PIPELINE_STATISTICS;
        check(device->CreateQuery(&desc,query.put()),"statistics query");context->Begin(query.p);for(unsigned i=0;i<4;++i)context->Draw(3,0);context->End(query.p);
        context->Flush(); // Standalone test submission only; never part of the scope helper.
        D3D11_QUERY_DATA_PIPELINE_STATISTICS stats{};
        for(unsigned poll=0;poll<500;++poll){HRESULT hr=context->GetData(query.p,&stats,sizeof(stats),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(hr==S_OK)return stats.PSInvocations;
            if(hr!=S_FALSE)check(hr,"statistics read");
            Sleep(1);}
        throw std::runtime_error("bounded query timeout");
    }
    uint64_t rate(NV_PIXEL_SHADING_RATE value){require(api.setView(context.p,view0.p)==NVAPI_OK,"bind rate view");require(api.set_uniform(context.p,value),"set rate");return draw();}
    void table01(){
        NV_D3D11_VIEWPORT_SHADING_RATE_DESC viewport{};viewport.enableVariablePixelShadingRate=true;
        for(auto& r:viewport.shadingRateTable)r=NV_PIXEL_X1_PER_RASTER_PIXEL;
        viewport.shadingRateTable[1]=NV_PIXEL_X1_PER_2X2_RASTER_PIXELS;
        NV_D3D11_VIEWPORTS_SHADING_RATE_DESC desc{};desc.version=NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER;desc.numViewports=1;desc.pViewports=&viewport;
        require(api.rates(context.p,&desc)==NVAPI_OK,"table01");
    }
    void run(){
        require(api.disable(context.p),"initial disable");auto base=draw();auto coarse2=rate(NV_PIXEL_X1_PER_2X2_RASTER_PIXELS);auto coarse4=rate(NV_PIXEL_X1_PER_4X4_RASTER_PIXELS);
        require(api.disable(context.p),"restore disable");auto restored=draw();
        std::cout<<"msaa="<<msaa<<" baseline="<<base<<" rate2x2="<<coarse2<<" rate4x4="<<coarse4<<" restored="<<restored<<"\n";
        require(coarse2<base&&coarse4<coarse2&&restored==base,"VRS does not reduce PS invocations / reset mismatch");
        {
            lmuqv::NvVrs helper;
            require(!helper.configure(context.p,256,256,2,true,lmuqv::NvVrsOwnership::Unknown),"unknown ownership accepted");
            require(helper.configure(context.p,256,256,2,true,lmuqv::NvVrsOwnership::ExclusiveKnownDisabled),"scope configure");
            require(helper.begin_outer(context.p),"scope begin");auto scope=draw();require(helper.end_outer(context.p),"scope end");
            require(scope==coarse2&&draw()==base,"scope does not reduce/restore");
            require(helper.begin_outer(context.p),"reset scope begin");helper.reset();require(!helper.ready()&&!helper.active()&&draw()==base,"active reset does not restore");
            require(helper.configure(context.p,256,256,1,true,lmuqv::NvVrsOwnership::ExclusiveKnownDisabled),"rate1 configure");
            require(helper.begin_outer(context.p)&&draw()==base&&helper.end_outer(context.p),"rate1 baseline");
            // Deliberately perturb this standalone context after configure to prove
            // rate1 begin/end/reset do not issue hidden NVAPI setters. Production
            // callers must still honor exclusive known-disabled ownership.
            require(rate(NV_PIXEL_X1_PER_2X2_RASTER_PIXELS)==coarse2,"rate1 probe setup");
            require(helper.begin_outer(context.p)&&draw()==coarse2,"rate1 begin issued a setter");
            require(helper.end_outer(context.p)&&draw()==coarse2,"rate1 end issued a setter");
            require(helper.begin_outer(context.p),"rate1 active reset begin");
            helper.reset();require(!helper.active()&&!helper.ready()&&!helper.faulted()&&draw()==coarse2,"rate1 reset issued a setter");
            // Configure must restore the baseline even though scopes are inert.
            require(helper.configure(context.p,256,256,1,true,lmuqv::NvVrsOwnership::ExclusiveKnownDisabled)&&draw()==base,"rate1 configure did not restore baseline");
            require(helper.begin_outer(context.p),"rate1 overlap begin");
            require(!helper.begin_outer(context.p)&&!helper.active()&&helper.faulted()&&!helper.ready(),"rate1 overlap must fail closed");
            helper.reset();
            require(helper.configure(context.p,256,256,1,true,lmuqv::NvVrsOwnership::ExclusiveKnownDisabled)&&helper.begin_outer(context.p),"rate1 mismatch setup");
            require(!helper.end_outer(nullptr)&&!helper.active()&&helper.faulted()&&!helper.ready(),"rate1 context mismatch must fail closed");
            helper.reset();require(draw()==base,"rate1 invalid calls changed baseline");
            require(helper.configure(context.p,256,256,4,true,lmuqv::NvVrsOwnership::ExclusiveKnownDisabled),"rate4 configure");
            require(helper.begin_outer(context.p)&&draw()==coarse4&&helper.end_outer(context.p),"rate4 scope");helper.reset();
            std::cout<<"helper_scopes msaa="<<msaa<<" rate1=baseline_no_scope_setters configure_restores=1 invalid_calls_fail_closed=1 rate2="<<scope<<" rate4="<<coarse4<<" reset=baseline\n";
            {   // 0.5.2: one shading-rate map per eye; steady focus needs no further upload.
                require(helper.configure(context.p,256,256,2,true,lmuqv::NvVrsOwnership::ExclusiveKnownDisabled),"per-eye configure");
                const lmuqv::VrsFocus smallFocus{.375f,.375f,.625f,.625f},wideFocus{.125f,.125f,.875f,.875f};
                auto foveated=[&](unsigned eye,const lmuqv::VrsFocus& focus){
                    require(helper.update_focus(context.p,focus,eye),"per-eye update");
                    require(helper.begin_outer(context.p),"per-eye begin");auto value=draw();require(helper.end_outer(context.p),"per-eye end");return value;};
                const auto eye0=foveated(0,smallFocus),eye1=foveated(1,wideFocus);
                const auto uploadsAfterBoth=helper.uploads();
                const auto eye0Again=foveated(0,smallFocus),eye1Again=foveated(1,wideFocus);
                require(eye0<eye1&&eye1<base,"per-eye maps not applied");
                require(eye0Again==eye0&&eye1Again==eye1,"per-eye map selection changed results");
                require(uploadsAfterBoth==2&&helper.uploads()==2,"steady per-eye focus uploaded again");
                require(draw()==base,"per-eye scopes did not restore baseline");helper.reset();
                std::cout<<"per_eye_maps msaa="<<msaa<<" eye0_small="<<eye0<<" eye1_wide="<<eye1<<" repeat_equal=1 uploads="<<uploadsAfterBoth<<" steady_reuploads=0\n";
            }
            require(helper.configure(context.p,256,256,2,true,lmuqv::NvVrsOwnership::ExclusiveKnownDisabled)&&helper.begin_outer(context.p),"failure test configure");
            std::thread wrongThread([&](){helper.reset();});wrongThread.join();
            require(helper.faulted()&&!helper.ready(),"reset failure was not sticky");
            require(!helper.configure(context.p,256,256,2,true,lmuqv::NvVrsOwnership::ExclusiveKnownDisabled),"sticky reset failure allowed configure");
            require(api.disable(context.p)&&draw()==base,"test owner could not explicitly recover known baseline");
            std::cout<<"helper_reset_failure msaa="<<msaa<<" sticky=1 reconfigure_rejected=1 owner_recovered_baseline=1\n";
        }
        Com<ID3DDeviceContextState> stateB,stateA;D3D_FEATURE_LEVEL feature{};D3D_FEATURE_LEVEL wanted[]={D3D_FEATURE_LEVEL_11_0};
        check(device1->CreateDeviceContextState(0,wanted,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&feature,stateB.put()),"context state");
        require(api.setView(context.p,view0.p)==NVAPI_OK&&api.set_uniform(context.p,NV_PIXEL_X1_PER_2X2_RASTER_PIXELS),"A table2");
        context1->SwapDeviceContextState(stateB.p,stateA.put());require(api.set_uniform(context.p,NV_PIXEL_X1_PER_RASTER_PIXEL),"B table1");
        auto b=draw();context1->SwapDeviceContextState(stateA.p,nullptr);auto a=draw();
        std::cout<<"state_swap_table msaa="<<msaa<<" A_after_restore="<<a<<" expectedA="<<coarse2<<" B="<<b<<" expectedB="<<base<<" captured="<<(a==coarse2&&b==base)<<"\n";
        table01();require(api.setView(context.p,view1.p)==NVAPI_OK,"A resource1");
        Com<ID3DDeviceContextState> stateC,stateD;
        check(device1->CreateDeviceContextState(0,wanted,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&feature,stateC.put()),"context state C");
        context1->SwapDeviceContextState(stateC.p,stateD.put());table01();require(api.setView(context.p,view0.p)==NVAPI_OK,"C resource0");
        auto c=draw();context1->SwapDeviceContextState(stateD.p,nullptr);table01();auto d=draw(); // Reapply only table to isolate SRRV binding restoration.
        std::cout<<"state_swap_resource msaa="<<msaa<<" A_after_restore="<<d<<" expectedA="<<coarse2<<" B="<<c<<" expectedB="<<base<<" captured="<<(d==coarse2&&c==base)<<"\n";
        require(api.disable(context.p),"final disable");context->ClearState();
    }
};
}
int main(){try{for(unsigned msaa:{1u,2u}){Bench bench(msaa);bench.run();}return 0;}catch(const std::exception& e){std::cerr<<"NV VRS hardware test: "<<e.what()<<"\n";return 1;}}
