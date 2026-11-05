#include "stereo_test_fixture.hpp"
#include "../stereo_draw_proof.hpp"
#include <d3d11sdklayers.h>
#include <thread>

namespace {
void waitQuery(ID3D11DeviceContext* c,ID3D11Query* query,void* data,UINT bytes){
    HRESULT hr=S_FALSE;const auto deadline=GetTickCount64()+5000;
    while((hr=c->GetData(query,data,bytes,0))==S_FALSE&&GetTickCount64()<deadline)Sleep(1);
    assert(hr==S_OK);
}
void bind(Fixture& f,unsigned eye){
    auto* c=f.c;const UINT stride=24,offset=0;c->IASetVertexBuffers(0,1,&f.vertices,&stride,&offset);c->IASetIndexBuffer(f.indices,DXGI_FORMAT_R32_UINT,0);c->IASetInputLayout(f.layout);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(f.referenceVs,nullptr,0);c->PSSetShader(f.referencePs,nullptr,0);c->GSSetShader(nullptr,nullptr,0);c->RSSetState(f.raster);D3D11_VIEWPORT v{0,0,float(Fixture::width),float(Fixture::width),0,1};c->RSSetViewports(1,&v);c->OMSetDepthStencilState(f.depthState,0);c->OMSetRenderTargets(1,&f.eyeRt[eye],f.eyeDs[eye]);
    c->UpdateSubresource(f.camera,0,nullptr,&f.cameras[eye],0,0);c->UpdateSubresource(f.extra,0,nullptr,f.extras[eye],0,0);c->UpdateSubresource(f.pixel,0,nullptr,f.scales[eye],0,0);c->VSSetConstantBuffers(0,1,&f.camera);c->VSSetConstantBuffers(4,1,&f.extra);c->PSSetConstantBuffers(3,1,&f.pixel);
    c->CSSetConstantBuffers(5,1,&f.pixel); // A stage the replay must leave untouched.
}
void checkState(Fixture& f,unsigned eye){
    ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;f.c->VSGetShader(&vs,nullptr,nullptr);f.c->PSGetShader(&ps,nullptr,nullptr);assert(vs==f.referenceVs&&ps==f.referencePs);drop(vs);drop(ps);
    ID3D11RenderTargetView* target=nullptr;ID3D11DepthStencilView* depth=nullptr;f.c->OMGetRenderTargets(1,&target,&depth);assert(target==f.eyeRt[eye]&&depth==f.eyeDs[eye]);drop(target);drop(depth);
    ID3D11Buffer *original=nullptr,*packed=nullptr,*compute=nullptr;f.c->VSGetConstantBuffers(0,1,&original);f.c->VSGetConstantBuffers(13,1,&packed);f.c->CSGetConstantBuffers(5,1,&compute);assert(original==f.camera&&!packed&&compute==f.pixel);drop(original);drop(compute);
}
std::vector<uint8_t> nativePixels(Fixture& f){
    D3D11_TEXTURE2D_DESC d{};f.color->GetDesc(&d);const auto stride=d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT?8:d.Format==DXGI_FORMAT_R32G32B32A32_FLOAT?16:4;d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Texture2D* staging=nullptr;assert(SUCCEEDED(f.d->CreateTexture2D(&d,nullptr,&staging)));f.c->CopyResource(staging,f.color);
    std::vector<uint8_t> values(size_t(d.Width)*d.Height*2*stride);for(unsigned eye=0;eye<2;++eye){D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(f.c->Map(staging,eye,D3D11_MAP_READ,0,&mapped)));for(unsigned y=0;y<d.Height;++y)std::memcpy(values.data()+size_t(eye*d.Height+y)*d.Width*stride,static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch,size_t(d.Width)*stride);f.c->Unmap(staging,eye);}drop(staging);return values;
}
bool hasStencil(DXGI_FORMAT format){return format==DXGI_FORMAT_D24_UNORM_S8_UINT||format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT;}
void setDepthTargets(Fixture& f,DXGI_FORMAT format,UINT flags,bool plain){
    f.c->OMSetRenderTargets(0,nullptr,nullptr);drop(f.ds);for(auto*& p:f.eyeDs)drop(p);drop(f.depth);
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=Fixture::width;td.ArraySize=plain?1:4;td.MipLevels=td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    td.Format=format==DXGI_FORMAT_D16_UNORM?DXGI_FORMAT_R16_TYPELESS:format==DXGI_FORMAT_D24_UNORM_S8_UINT?DXGI_FORMAT_R24G8_TYPELESS:format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT?DXGI_FORMAT_R32G8X24_TYPELESS:DXGI_FORMAT_D32_FLOAT;
    assert(SUCCEEDED(f.d->CreateTexture2D(&td,nullptr,&f.depth)));
    D3D11_DEPTH_STENCIL_VIEW_DESC vd{};vd.Format=format;vd.ViewDimension=plain?D3D11_DSV_DIMENSION_TEXTURE2D:D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
    if(!plain)vd.Texture2DArray.ArraySize=td.ArraySize;assert(SUCCEEDED(f.d->CreateDepthStencilView(f.depth,&vd,&f.ds)));
    f.c->ClearDepthStencilView(f.ds,D3D11_CLEAR_DEPTH|(hasStencil(format)?D3D11_CLEAR_STENCIL:0),1,0x37);
    vd.Flags=flags;
    for(UINT eye=0;eye<2;++eye){if(!plain){vd.Texture2DArray.ArraySize=1;vd.Texture2DArray.FirstArraySlice=eye+1;}assert(SUCCEEDED(f.d->CreateDepthStencilView(f.depth,&vd,&f.eyeDs[eye])));}
}
std::vector<uint8_t> nativeDepthPixels(Fixture& f){
    D3D11_TEXTURE2D_DESC td{};f.depth->GetDesc(&td);const UINT bytes=td.Format==DXGI_FORMAT_R16_TYPELESS?2:td.Format==DXGI_FORMAT_R32G8X24_TYPELESS?8:4;
    td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Texture2D* staging=nullptr;assert(SUCCEEDED(f.d->CreateTexture2D(&td,nullptr,&staging)));f.c->CopyResource(staging,f.depth);
    std::vector<uint8_t> data(size_t(td.Width)*td.Height*td.ArraySize*bytes);
    for(UINT slice=0;slice<td.ArraySize;++slice){D3D11_MAPPED_SUBRESOURCE m{};assert(SUCCEEDED(f.c->Map(staging,slice,D3D11_MAP_READ,0,&m)));
        for(UINT y=0;y<td.Height;++y)std::memcpy(data.data()+size_t(slice*td.Height+y)*td.Width*bytes,static_cast<const uint8_t*>(m.pData)+y*m.RowPitch,td.Width*bytes);f.c->Unmap(staging,slice);}
    if(bytes==8)for(size_t i=0;i<data.size();i+=8)data[i+5]=data[i+6]=data[i+7]=0; // Undefined padding is not stencil/depth.
    drop(staging);return data;
}
void seedDepth(Fixture& f,unsigned eye,unsigned mode,DXGI_FORMAT format){
    bind(f,eye);D3D11_DEPTH_STENCIL_VIEW_DESC vd{};f.eyeDs[eye]->GetDesc(&vd);vd.Flags=0;ID3D11DepthStencilView* writable=nullptr;assert(SUCCEEDED(f.d->CreateDepthStencilView(f.depth,&vd,&writable)));
    f.c->ClearDepthStencilView(writable,D3D11_CLEAR_DEPTH|(hasStencil(format)?D3D11_CLEAR_STENCIL:0),mode==2||mode==6?0.0f:1.0f,UINT8(0x30+eye*3));
    if(mode==1||mode==3){ // Actual geometry depth prepass for EQUAL/no-write shading.
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=TRUE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;dd.DepthFunc=D3D11_COMPARISON_LESS;ID3D11DepthStencilState* prepass=nullptr;assert(SUCCEEDED(f.d->CreateDepthStencilState(&dd,&prepass)));
        f.c->OMSetDepthStencilState(prepass,0);f.c->OMSetRenderTargets(0,nullptr,writable);f.c->PSSetShader(nullptr,nullptr,0);f.c->DrawIndexed(6,0,0);drop(prepass);
    }
    drop(writable);bind(f,eye);
}
void checkDebug(Fixture& f,bool debug){
    if(!debug)return;ID3D11InfoQueue* queue=nullptr;assert(SUCCEEDED(f.d->QueryInterface(__uuidof(ID3D11InfoQueue),reinterpret_cast<void**>(&queue))));
    for(UINT64 i=0;i<queue->GetNumStoredMessagesAllowedByRetrievalFilter();++i){SIZE_T bytes=0;assert(SUCCEEDED(queue->GetMessage(i,nullptr,&bytes)));std::vector<uint8_t> data(bytes);auto* message=reinterpret_cast<D3D11_MESSAGE*>(data.data());assert(SUCCEEDED(queue->GetMessage(i,message,&bytes)));
        if(message->Severity==D3D11_MESSAGE_SEVERITY_ERROR||message->Severity==D3D11_MESSAGE_SEVERITY_CORRUPTION){std::cerr<<message->pDescription<<'\n';assert(false);}}
    drop(queue);
}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);std::cout<<std::unitbuf;
    bool hardware=false,debug=false;for(int i=1;i<argc;++i){hardware|=!std::strcmp(argv[i],"--hardware");debug|=!std::strcmp(argv[i],"--debug");}
    if(debug){ID3D11Device* probe=nullptr;const auto hr=D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_DEBUG,nullptr,0,D3D11_SDK_VERSION,&probe,nullptr,nullptr);if(FAILED(hr)){std::cerr<<"D3D11 debug layer unavailable: 0x"<<std::hex<<unsigned(hr)<<'\n';return 2;}drop(probe);}
    auto* vertex=compile(vsSource,"vs_4_0");auto* pixel=compile(psSource,"ps_4_0");
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT})for(bool geometry:{false,true}){
        Fixture f(hardware,format,debug?D3D11_CREATE_DEVICE_DEBUG:0);const float marker[4]{.75f,.25f,.5f,1};f.c->ClearRenderTargetView(f.rt,marker);f.c->ClearDepthStencilView(f.ds,D3D11_CLEAR_DEPTH,1,0);const auto before=nativePixels(f);bind(f,0);
        lmuqv::StereoProofDraw draw;draw.key=91;draw.indexCount=6;draw.vertexCode=vertex->GetBufferPointer();draw.vertexBytes=vertex->GetBufferSize();draw.pixelCode=pixel->GetBufferPointer();draw.pixelBytes=pixel->GetBufferSize();draw.vertexConstants=1|(1u<<4);draw.pixelConstants=1u<<3;draw.forceGeometry=geometry;
        lmuqv::StereoDrawProof proof;proof.begin();ID3D11Query *timer=nullptr,*occlusion=nullptr;
        D3D11_QUERY_DESC queryDesc{D3D11_QUERY_TIMESTAMP_DISJOINT,0};assert(SUCCEEDED(f.d->CreateQuery(&queryDesc,&timer)));f.c->Begin(timer);
        queryDesc.Query=D3D11_QUERY_OCCLUSION;assert(SUCCEEDED(f.d->CreateQuery(&queryDesc,&occlusion)));f.c->Begin(occlusion);
        // Begin/End tracking in the caller must withhold extra replay draws
        // during result-counting query scopes, even alongside a timing query.
        // The occlusion result must stay empty: no diagnostic draws or snapshots.
        proof.observe(f.c,false,draw,false,true);assert(proof.result().status=="armed"&&proof.result().querySkips==1&&proof.result().timingQueryOverlaps==0);
        f.c->End(occlusion);UINT64 samples=~UINT64(0);waitQuery(f.c,occlusion,&samples,sizeof(samples));assert(samples==0);drop(occlusion);
        proof.observe(f.c,false,draw,true,true);if(proof.result().status!="waiting_right")std::cerr<<proof.result().reason<<'\n';assert(proof.result().status=="waiting_right");checkState(f,0);
        // The SAME native buffers now contain the right-eye values. Retaining
        // only their pointers would corrupt the original left-eye reference.
        bind(f,1);proof.observe(f.c,true,draw,true,true);const auto result=proof.result();if(result.status!="passed")std::cerr<<result.status<<": "<<result.reason<<" differences="<<result.colorDifferent[0]<<'/'<<result.colorDifferent[1]<<" depth="<<result.depthDifferent[0]<<'/'<<result.depthDifferent[1]<<'\n';assert(result.status=="passed"&&result.covered[0]>100&&result.covered[1]>100&&result.timingQueryOverlaps==2);checkState(f,1);assert(before==nativePixels(f));proof.finish();
        f.c->End(timer);D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing{};waitQuery(f.c,timer,&timing,sizeof(timing));assert(timing.Frequency>0);drop(timer);
        std::cout<<"Private stereo proof format="<<unsigned(format)<<" route="<<result.route<<": GPU CB snapshots/packing, separate eye data, color/depth, native targets and context restoration PASS\n";
        // A mismatch in a high texture slot must be rejected even when a simple
        // shader happens not to sample it. No first-16-only sharing assumption.
        bind(f,0);proof.begin();proof.observe(f.c,false,draw,true);bind(f,1);
        D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;ID3D11Texture2D* t=nullptr;ID3D11ShaderResourceView* srv=nullptr;assert(SUCCEEDED(f.d->CreateTexture2D(&td,nullptr,&t)));assert(SUCCEEDED(f.d->CreateShaderResourceView(t,nullptr,&srv)));f.c->PSSetShaderResources(20,1,&srv);
        proof.observe(f.c,true,draw,true);assert(proof.result().status=="unavailable"&&proof.result().reason.find("SRV")!=std::string::npos);checkState(f,1);proof.finish();ID3D11ShaderResourceView* empty=nullptr;f.c->PSSetShaderResources(20,1,&empty);drop(srv);drop(t);
        bind(f,0);proof.begin();proof.observe(f.c,false,draw,true);auto different=draw;different.key=92;bind(f,1);proof.observe(f.c,true,different,true);proof.finish();assert(proof.result().status=="inconclusive"&&proof.result().reason=="no matching right-eye draw");
        // A stale sampler in an undeclared slot cannot affect this shader.
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.ComparisonFunc=D3D11_COMPARISON_ALWAYS;sd.MaxLOD=D3D11_FLOAT32_MAX;
        ID3D11SamplerState* unused=nullptr;assert(SUCCEEDED(f.d->CreateSamplerState(&sd,&unused)));bind(f,0);proof.begin();proof.observe(f.c,false,draw,true);bind(f,1);f.c->PSSetSamplers(8,1,&unused);proof.observe(f.c,true,draw,true);assert(proof.result().status=="passed");proof.finish();ID3D11SamplerState* noSampler=nullptr;f.c->PSSetSamplers(8,1,&noSampler);drop(unused);
        proof.begin();bind(f,0);proof.observe(f.c,false,draw,true);std::thread close([&]{proof.finish();});close.join();assert(proof.result().status=="inconclusive");
        checkDebug(f,debug);
    }
    for(bool geometry:{false,true}){
        Fixture f(hardware,DXGI_FORMAT_R16G16B16A16_FLOAT,debug?D3D11_CREATE_DEVICE_DEBUG:0);f.c->ClearDepthStencilView(f.ds,D3D11_CLEAR_DEPTH,1,0);
        const char* sampledSource="cbuffer Frame:register(b3){float4 scale;}; Texture2D tex:register(t20); SamplerState samp:register(s8); float4 main(float4 p:SV_Position,float3 color:COLOR0):SV_Target{return float4(color*scale.rgb,1)*tex.SampleLevel(samp,float2(.5,.5),0);}";
        auto* sampled=compile(sampledSource,"ps_4_0");drop(f.referencePs);assert(SUCCEEDED(f.d->CreatePixelShader(sampled->GetBufferPointer(),sampled->GetBufferSize(),nullptr,&f.referencePs)));
        lmuqv::StereoShaderVariant variant;std::string error;assert(lmuqv::convert_stereo_shader(sampled->GetBufferPointer(),sampled->GetBufferSize(),lmuqv::StereoShaderStage::pixel,1u<<3,false,variant,error)&&variant.samplerSlots==(1u<<8));
        D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const uint32_t value=0xff804020;D3D11_SUBRESOURCE_DATA initial{&value,4,0};ID3D11Texture2D* t=nullptr;ID3D11ShaderResourceView* srv=nullptr;assert(SUCCEEDED(f.d->CreateTexture2D(&td,&initial,&t)));assert(SUCCEEDED(f.d->CreateShaderResourceView(t,nullptr,&srv)));
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.ComparisonFunc=D3D11_COMPARISON_ALWAYS;sd.MaxLOD=D3D11_FLOAT32_MAX;ID3D11SamplerState *a=nullptr,*b=nullptr;assert(SUCCEEDED(f.d->CreateSamplerState(&sd,&a)));sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;assert(SUCCEEDED(f.d->CreateSamplerState(&sd,&b)));
        lmuqv::StereoProofDraw draw;draw.key=93;draw.indexCount=6;draw.vertexCode=vertex->GetBufferPointer();draw.vertexBytes=vertex->GetBufferSize();draw.pixelCode=sampled->GetBufferPointer();draw.pixelBytes=sampled->GetBufferSize();draw.vertexConstants=1|(1u<<4);draw.pixelConstants=1u<<3;draw.forceGeometry=geometry;
        lmuqv::StereoDrawProof proof;
        for(bool mismatch:{false,true}){
            bind(f,0);f.c->PSSetShaderResources(20,1,&srv);f.c->PSSetSamplers(8,1,&a);proof.begin();proof.observe(f.c,false,draw,true);assert(proof.result().status=="waiting_right");
            bind(f,1);auto* rightSampler=mismatch?b:a;f.c->PSSetSamplers(8,1,&rightSampler);proof.observe(f.c,true,draw,true);assert(proof.result().status==(mismatch?"unavailable":"passed"));
            if(mismatch)assert(proof.result().reason=="PS sampler slot 8 differs between eyes");checkState(f,1);proof.finish();
        }
        f.c->ClearState();drop(a);drop(b);drop(srv);drop(t);drop(sampled);checkDebug(f,debug);
        std::cout<<"Sampler declarations: unused slot 8 allowed; used slot 8 checked; high texture slot 20 retained, route="<<(geometry?"geometry":"vprt")<<" PASS\n";
    }
    for(auto depthFormat:{DXGI_FORMAT_D16_UNORM,DXGI_FORMAT_D24_UNORM_S8_UINT,DXGI_FORMAT_D32_FLOAT,DXGI_FORMAT_D32_FLOAT_S8X24_UINT})for(unsigned mode=0;mode<7;++mode)for(bool geometry:{false,true}){
        if(mode==5&&!hasStencil(depthFormat))continue;
        const bool plain=mode==3;const UINT flags=mode==3?D3D11_DSV_READ_ONLY_DEPTH|(hasStencil(depthFormat)?D3D11_DSV_READ_ONLY_STENCIL:0):0;
        Fixture f(hardware,DXGI_FORMAT_R16G16B16A16_FLOAT,debug?D3D11_CREATE_DEVICE_DEBUG:0);setDepthTargets(f,depthFormat,flags,plain);
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=mode!=4;dd.DepthWriteMask=mode==1||mode==3?D3D11_DEPTH_WRITE_MASK_ZERO:D3D11_DEPTH_WRITE_MASK_ALL;
        dd.DepthFunc=mode==1||mode==3?D3D11_COMPARISON_EQUAL:mode==2?D3D11_COMPARISON_GREATER:D3D11_COMPARISON_LESS;
        if(mode==5){dd.StencilEnable=TRUE;dd.StencilReadMask=dd.StencilWriteMask=0xff;dd.FrontFace.StencilFunc=D3D11_COMPARISON_ALWAYS;dd.FrontFace.StencilFailOp=D3D11_STENCIL_OP_INVERT;dd.FrontFace.StencilDepthFailOp=D3D11_STENCIL_OP_INCR_SAT;dd.FrontFace.StencilPassOp=D3D11_STENCIL_OP_DECR_SAT;dd.BackFace=dd.FrontFace;}
        drop(f.depthState);assert(SUCCEEDED(f.d->CreateDepthStencilState(&dd,&f.depthState)));
        const float marker[4]{.75f,.25f,.5f,1};f.c->ClearRenderTargetView(f.rt,marker);seedDepth(f,0,mode,depthFormat);const auto colorBefore=nativePixels(f),depthBefore=nativeDepthPixels(f);
        lmuqv::StereoProofDraw draw;draw.key=92;draw.indexCount=6;draw.vertexCode=vertex->GetBufferPointer();draw.vertexBytes=vertex->GetBufferSize();draw.pixelCode=pixel->GetBufferPointer();draw.pixelBytes=pixel->GetBufferSize();draw.vertexConstants=1|(1u<<4);draw.pixelConstants=1u<<3;draw.forceGeometry=geometry;
        lmuqv::StereoDrawProof proof;proof.begin();proof.observe(f.c,false,draw,true);if(proof.result().status!="waiting_right")std::cerr<<proof.result().reason<<'\n';assert(proof.result().status=="waiting_right");checkState(f,0);assert(nativeDepthPixels(f)==depthBefore);
        // The source left-eye depth is deliberately overwritten before the
        // right eye. A retained source pointer cannot pass this regression.
        f.c->ClearDepthStencilView(f.ds,D3D11_CLEAR_DEPTH|(hasStencil(depthFormat)?D3D11_CLEAR_STENCIL:0),mode==2?1.0f:0.0f,0xab);
        seedDepth(f,1,mode,depthFormat);const auto depthRight=nativeDepthPixels(f);proof.observe(f.c,true,draw,true);const auto result=proof.result();
        if(result.status!=(mode==6?"inconclusive":"passed"))std::cerr<<"depth="<<unsigned(depthFormat)<<" mode="<<mode<<" "<<result.status<<": "<<result.reason<<" color="<<result.colorDifferent[0]<<'/'<<result.colorDifferent[1]<<" depth="<<result.depthDifferent[0]<<'/'<<result.depthDifferent[1]<<" stencil="<<result.stencilDifferent[0]<<'/'<<result.stencilDifferent[1]<<'\n';
        assert(result.status==(mode==6?"inconclusive":"passed")&&result.depthFormat==unsigned(depthFormat)&&result.depthCopied[0]&&result.depthCopied[1]);
        assert(result.nativeDepth[0][1]==unsigned(dd.DepthWriteMask)&&result.nativeDepth[0][2]==unsigned(dd.DepthFunc)&&result.nativeDepth[0][4]==flags);
        assert(nativePixels(f)==colorBefore&&nativeDepthPixels(f)==depthRight);checkState(f,1);proof.finish();checkDebug(f,debug);
        std::cout<<"Native depth snapshot format="<<unsigned(depthFormat)<<" mode="<<mode<<" route="<<result.route<<": per-eye seeds, source reuse, color/depth/stencil and native preservation PASS\n";
    }
    drop(vertex);drop(pixel);std::cout<<"Stereo private-draw proof "<<(hardware?"hardware":"WARP")<<" passed: no game draw suppression; timing scopes allowed, empty occlusion result preserved, high texture slots, unmatched eye and cross-thread release\n";
}
