#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../nv_multiview_backend.hpp"
#include <d3dcompiler.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace lmuqv::nv_multiview;
namespace {
template<class T>void drop(T*& p){if(p){p->Release();p=nullptr;}}
ID3DBlob* compile(const std::string& text,const char* profile){
    ID3DBlob *code=nullptr,*errors=nullptr;
    auto hr=D3DCompile(text.data(),text.size(),nullptr,nullptr,nullptr,"main",profile,
        D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if(FAILED(hr)&&errors)std::cerr<<static_cast<const char*>(errors->GetBufferPointer());
    drop(errors);assert(SUCCEEDED(hr));return code;
}
const char* constants="cbuffer Views:register(b0){row_major float4x4 viewMatrix[4];float4 tint[4];uint selected;};\n";
std::string vertex_source(unsigned views,bool sps=false){
    std::ostringstream s;s<<constants;
    if(!views)s<<"struct Out{float4 pos:SV_POSITION;float4 color:COLOR0;};Out main(float3 p:POSITION,float3 c:COLOR0){Out o;o.pos=mul(float4(p,1),viewMatrix[selected]);o.color=float4(c,1)*tint[selected];return o;}";
    else if(sps){
        s<<"struct Out{float4 pos:SV_POSITION;float4 right:NV_X_RIGHT;float4 color:COLOR0;uint4 mask:NV_VIEWPORT_MASK;};"
            "Out main(float3 p:POSITION,float3 c:COLOR0){Out o;o.pos=mul(float4(p,1),viewMatrix[0]);"
            "o.right=mul(float4(p,1),viewMatrix[1]).xxxx;o.color=float4(c,1)*tint[0];o.mask=1;return o;}";
    }else {
        s<<"struct Out{float4 p0:SV_POSITION;";
        for(unsigned i=1;i<views;++i)s<<"float4 p"<<i<<":NV_POSITION_VIEW_"<<i<<"_SEMANTIC;";
        for(unsigned i=0;i<views;++i)s<<"float4 c"<<i<<":LMU_COLOR_VIEW_"<<i<<"_SEMANTIC;";
        s<<"uint4 mask:NV_VIEWPORT_MASK;uint4 mask2:NV_VIEWPORT_MASK_2_SEMANTIC;};Out main(float3 p:POSITION,float3 c:COLOR0){Out o;";
        for(unsigned i=0;i<views;++i)s<<"o.p"<<i<<"=mul(float4(p,1),viewMatrix["<<i<<"]);o.c"<<i<<"=float4(c,1)*tint["<<i<<"];";
        s<<"o.mask=1;o.mask2=0;return o;}";
    }
    return s.str();
}
std::string pixel_source(bool nv){return std::string("float4 main(float4 p:SV_POSITION,float4 c:")+
    (nv?"LMU_COLOR_VIEW_0_SEMANTIC":"COLOR0")+"):SV_TARGET{return c;}";}
NV_CUSTOM_SEMANTIC semantic(NV_CUSTOM_SEMANTIC_TYPE type,const char* name){
    NV_CUSTOM_SEMANTIC s{};s.version=NV_CUSTOM_SEMANTIC_VERSION;s.NVCustomSemanticType=type;
    strcpy_s(s.NVCustomSemanticNameString,name);return s;
}
std::array<NV_CUSTOM_SEMANTIC,4> semantics(){return {{semantic(NV_POSITION_SEMANTIC,"NV_POSITION"),semantic(NV_GENERIC_ATTRIBUTE_SEMANTIC,"LMU_COLOR"),
    semantic(NV_VIEWPORT_MASK_SEMANTIC,"NV_VIEWPORT_MASK"),semantic(NV_VIEWPORT_MASK_2_SEMANTIC,"NV_VIEWPORT_MASK_2_SEMANTIC")}};}
struct Constants {float matrix[4][16]{};float tint[4][4]{};unsigned selected=0,padding[3]{};};
struct Image {std::vector<float> color,depth;};
struct Fixture {
    static constexpr unsigned width=32;
    unsigned drawIndices=3;
    ID3D11Device* d=nullptr;ID3D11DeviceContext* c=nullptr;
    ID3D11Texture2D *color=nullptr,*depth=nullptr;
    ID3D11RenderTargetView *rt=nullptr,*slice[4]{};
    ID3D11DepthStencilView *ds=nullptr,*depthSlice[4]{};
    ID3D11Buffer *vb=nullptr,*ib=nullptr,*cb=nullptr;
    ID3D11InputLayout* layout=nullptr;ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;
    ID3D11RasterizerState* raster=nullptr;ID3D11DepthStencilState* depthState=nullptr;
    Constants data{};
    ID3D11Buffer* buffer(unsigned bytes,unsigned bind,const void* ptr){
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;desc.BindFlags=bind;
        D3D11_SUBRESOURCE_DATA initial{ptr,0,0};ID3D11Buffer* result=nullptr;
        assert(SUCCEEDED(d->CreateBuffer(&desc,ptr?&initial:nullptr,&result)));return result;
    }
    explicit Fixture(bool hardware){
        D3D_FEATURE_LEVEL level{};assert(SUCCEEDED(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,
            nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&level,&c)));
        D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=width;td.ArraySize=4;td.MipLevels=td.SampleDesc.Count=1;
        td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;assert(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&color)));
        td.Format=DXGI_FORMAT_D32_FLOAT;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;assert(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&depth)));
        D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;rd.Texture2DArray.ArraySize=4;
        assert(SUCCEEDED(d->CreateRenderTargetView(color,&rd,&rt)));
        D3D11_DEPTH_STENCIL_VIEW_DESC dd{};dd.Format=DXGI_FORMAT_D32_FLOAT;dd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DARRAY;dd.Texture2DArray.ArraySize=4;
        assert(SUCCEEDED(d->CreateDepthStencilView(depth,&dd,&ds)));
        rd.Texture2DArray.ArraySize=dd.Texture2DArray.ArraySize=1;
        for(unsigned i=0;i<4;++i){rd.Texture2DArray.FirstArraySlice=dd.Texture2DArray.FirstArraySlice=i;
            assert(SUCCEEDED(d->CreateRenderTargetView(color,&rd,&slice[i])));assert(SUCCEEDED(d->CreateDepthStencilView(depth,&dd,&depthSlice[i])));
            auto& m=data.matrix[i];m[0]=.68f+.04f*i;m[5]=.85f-.08f*i;m[10]=.55f;m[15]=1.0f+.06f*i;
            m[1]=.025f*i;m[4]=-.035f*i;m[3]=.04f*i;m[7]=-.025f*i; // independent projective W
            m[12]=-.15f+.1f*i;m[13]=.12f-.065f*i;m[14]=.18f+.09f*i;
            data.tint[i][0]=.3f+.18f*i;data.tint[i][1]=.95f-.17f*i;data.tint[i][2]=.55f+.08f*i;data.tint[i][3]=1;
        }
        const float vertices[]{-.8f,-.7f,.3f,1,.2f,.4f, .8f,-.6f,.5f,.3f,1,.5f, -.1f,.85f,.4f,.5f,.3f,1,
            -.5f,-.4f,.7f,.1f,.8f,1, .6f,-.3f,.2f,1,.8f,.1f, .2f,.65f,.1f,.8f,.1f,1};
        const unsigned indices[]{0,1,2,3,4,5};vb=buffer(sizeof(vertices),D3D11_BIND_VERTEX_BUFFER,vertices);
        ib=buffer(sizeof(indices),D3D11_BIND_INDEX_BUFFER,indices);cb=buffer(sizeof(data),D3D11_BIND_CONSTANT_BUFFER,&data);
        auto* v=compile(vertex_source(0),"vs_5_0");auto* p=compile(pixel_source(false),"ps_5_0");
        assert(SUCCEEDED(d->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs)));
        assert(SUCCEEDED(d->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps)));
        D3D11_INPUT_ELEMENT_DESC elements[]{{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}};
        assert(SUCCEEDED(d->CreateInputLayout(elements,2,v->GetBufferPointer(),v->GetBufferSize(),&layout)));drop(v);drop(p);
        D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;
        assert(SUCCEEDED(d->CreateRasterizerState(&rs,&raster)));D3D11_DEPTH_STENCIL_DESC dz{};
        dz.DepthEnable=TRUE;dz.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;dz.DepthFunc=D3D11_COMPARISON_LESS;
        assert(SUCCEEDED(d->CreateDepthStencilState(&dz,&depthState)));
    }
    void bind(){
        const UINT stride=24,offset=0;c->IASetVertexBuffers(0,1,&vb,&stride,&offset);c->IASetIndexBuffer(ib,DXGI_FORMAT_R32_UINT,0);
        c->IASetInputLayout(layout);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->GSSetShader(nullptr,nullptr,0);c->HSSetShader(nullptr,nullptr,0);c->DSSetShader(nullptr,nullptr,0);
        c->VSSetConstantBuffers(0,1,&cb);c->RSSetState(raster);c->OMSetDepthStencilState(depthState,0);
        D3D11_VIEWPORT viewport{0,0,float(width),float(width),0,1};c->RSSetViewports(1,&viewport);
    }
    void clear(){const float zero[4]{};c->ClearRenderTargetView(rt,zero);c->ClearDepthStencilView(ds,D3D11_CLEAR_DEPTH,1,0);}
    Image read(){
        c->OMSetRenderTargets(0,nullptr,nullptr);Image image;
        auto readTexture=[&](ID3D11Texture2D* texture,unsigned components,std::vector<float>& output){
            D3D11_TEXTURE2D_DESC td{};texture->GetDesc(&td);td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;td.Usage=D3D11_USAGE_STAGING;
            ID3D11Texture2D* staging=nullptr;assert(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&staging)));c->CopyResource(staging,texture);
            output.resize(size_t(width)*width*4*components);
            for(unsigned i=0;i<4;++i){D3D11_MAPPED_SUBRESOURCE m{};auto mapResult=c->Map(staging,i,D3D11_MAP_READ,0,&m);
                if(FAILED(mapResult)){
                    std::ostringstream message;message<<"GPU readback failed: map=0x"<<std::hex<<unsigned(mapResult)
                        <<" deviceRemovedReason=0x"<<unsigned(d->GetDeviceRemovedReason());drop(staging);
                    throw std::runtime_error(message.str());
                }
                for(unsigned y=0;y<width;++y)std::memcpy(output.data()+size_t(i*width+y)*width*components,
                    static_cast<const char*>(m.pData)+y*m.RowPitch,size_t(width)*components*sizeof(float));c->Unmap(staging,i);}
            drop(staging);
        };readTexture(color,4,image.color);readTexture(depth,1,image.depth);return image;
    }
    Image legacy(unsigned views){clear();bind();c->VSSetShader(vs,nullptr,0);c->PSSetShader(ps,nullptr,0);
        for(unsigned i=0;i<views;++i){data.selected=i;c->UpdateSubresource(cb,0,nullptr,&data,0,0);c->OMSetRenderTargets(1,&slice[i],depthSlice[i]);c->DrawIndexed(drawIndices,0,0);}return read();}
    ~Fixture(){c->ClearState();drop(vs);drop(ps);drop(layout);drop(vb);drop(ib);drop(cb);drop(raster);drop(depthState);
        for(auto*& p:slice)drop(p);for(auto*& p:depthSlice)drop(p);drop(rt);drop(ds);drop(color);drop(depth);drop(c);drop(d);}
};
int vertexCalls=0,pixelCalls=0,modeCalls=0,failVertex=0,failPixel=0,failEnable=0,failDisable=0;
std::vector<unsigned> modeViews;
NvAPI_Status __cdecl initialize(){return NVAPI_OK;}
NvAPI_Status __cdecl querySps(IUnknown*,NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS* p){p->bSinglePassStereoSupported=1;p->bSinglePassStereoXYZWSupported=1;return NVAPI_OK;}
NvAPI_Status __cdecl queryMvr(IUnknown*,NV_QUERY_MULTIVIEW_SUPPORT_PARAMS* p){p->bMultiViewSupported=1;return NVAPI_OK;}
NvAPI_Status __cdecl unsupported(IUnknown*,NV_QUERY_MULTIVIEW_SUPPORT_PARAMS*){return NVAPI_NO_IMPLEMENTATION;}
NvAPI_Status __cdecl vertex(ID3D11Device* d,const void* b,SIZE_T n,ID3D11ClassLinkage*,const NvAPI_D3D11_CREATE_VERTEX_SHADER_EX* a,ID3D11VertexShader** out){
    ++vertexCalls;assert(a->version==NVAPI_D3D11_CREATEVERTEXSHADEREX_VERSION&&a->NumCustomSemantics==4&&!a->UseWithFastGS&&!a->UseSpecificShaderExt);
    assert(a->pCustomSemantics[0].NVCustomSemanticType==NV_POSITION_SEMANTIC);if(failVertex)return NVAPI_ERROR;
    return SUCCEEDED(d->CreateVertexShader(b,n,nullptr,out))?NVAPI_OK:NVAPI_ERROR;
}
NvAPI_Status __cdecl pixel(ID3D11Device* d,const void* b,SIZE_T n,ID3D11ClassLinkage*,const NvAPI_D3D11_CREATE_PIXEL_SHADER_EX* a,ID3D11PixelShader** out){
    ++pixelCalls;assert(a->version==NVAPI_D3D11_CREATEPIXELSHADEREX_VERSION&&a->NumCustomSemantics==4&&!a->reserved);
    if(failPixel)return NVAPI_ERROR;return SUCCEEDED(d->CreatePixelShader(b,n,nullptr,out))?NVAPI_OK:NVAPI_ERROR;
}
NvAPI_Status record(unsigned views){++modeCalls;modeViews.push_back(views);return (views==1?failDisable:failEnable)?NVAPI_ERROR:NVAPI_OK;}
NvAPI_Status __cdecl spsMode(IUnknown*,NvU32 views,NvU32 offset,NvU8 independent){assert(!independent&&offset==1);return record(views);}
NvAPI_Status __cdecl mvrMode(IUnknown*,NV_MULTIVIEW_PARAMS* p){assert(p->version==NV_MULTIVIEW_PARAMS_VER&&!p->independentViewportMaskEnable);
    for(unsigned i=0;i<p->numViews;++i)assert(p->renderTargetIndexOffset[i]==i);return record(p->numViews);}
void units(){
    Fixture f(false);RenderApi api{{initialize,querySps,queryMvr},vertex,pixel,spsMode,mvrMode};
    Backend b;assert(b.initialize(f.d,f.c,ContextContract::exclusively_owned_and_known_disabled,api));assert(b.supports(Route::sps2)&&b.supports(Route::mvr4));
    auto semanticsTable=semantics();auto* vs=compile(vertex_source(4),"vs_5_0");auto* ps=compile(pixel_source(true),"ps_5_0");
    Bytecode v{vs->GetBufferPointer(),vs->GetBufferSize()},p{ps->GetBufferPointer(),ps->GetBufferSize()};std::string error;
    failVertex=1;{ShaderSet shaders;assert(!b.create_shaders(Route::mvr4,v,p,semanticsTable.data(),4,semanticsTable.data(),4,shaders,error));assert(!shaders.vertex&&!shaders.pixel&&pixelCalls==0);}failVertex=0;
    failPixel=1;{ShaderSet shaders;assert(!b.create_shaders(Route::mvr4,v,p,semanticsTable.data(),4,semanticsTable.data(),4,shaders,error));assert(!shaders.vertex&&!shaders.pixel);}failPixel=0;
    {ShaderSet shaders;assert(b.create_shaders(Route::mvr4,v,p,semanticsTable.data(),4,semanticsTable.data(),4,shaders,error));}
    semanticsTable[0].Reserved=1;{ShaderSet shaders;const int previous=vertexCalls;assert(!b.create_shaders(Route::mvr4,v,p,semanticsTable.data(),4,semanticsTable.data(),4,shaders,error));assert(vertexCalls==previous);}semanticsTable[0].Reserved=0;
    {ShaderSet shaders;const int previous=vertexCalls;assert(!b.create_shaders(Route::sps2,v,p,semanticsTable.data(),4,semanticsTable.data(),4,shaders,error));assert(vertexCalls==previous);
        assert(!b.create_shaders(Route::mvr4,v,p,semanticsTable.data(),2,semanticsTable.data(),2,shaders,error));assert(vertexCalls==previous);}
    failEnable=1;auto result=b.submit_indexed(Route::mvr4,6,0,0);assert(!result.submitted&&result.disabled&&modeViews==std::vector<unsigned>({4,1}));
    failEnable=0;failDisable=1;f.bind();f.c->VSSetShader(f.vs,nullptr,0);f.c->PSSetShader(f.ps,nullptr,0);f.c->OMSetRenderTargets(1,&f.rt,f.ds);
    result=b.submit_indexed(Route::sps2,3,0,0);assert(result.submitted&&!result.disabled&&b.poisoned());
    auto count=modeCalls;assert(!b.submit_indexed(Route::mvr4,6,0,0).submitted&&modeCalls==count);failDisable=0;
    Backend fallback;api.capabilities.multiview=unsupported;assert(fallback.initialize(f.d,f.c,ContextContract::exclusively_owned_and_known_disabled,api));
    assert(!fallback.supports(Route::mvr4));assert(!fallback.submit_indexed(Route::mvr4,6,0,0).submitted&&modeCalls==count);
    drop(vs);drop(ps);std::cout<<"NVAPI injection: shader errors, invalid semantics, enable/reset failure, poisoning and unsupported fallback passed"<<std::endl;
}
void hardware(){
    Fixture f(true);Backend b;assert(b.initialize(f.d,f.c,ContextContract::exclusively_owned_and_known_disabled));
    for(auto route:{Route::mvr4}){
        assert(b.supports(route));const unsigned views=route==Route::sps2?2:4;auto* v=compile(vertex_source(views),"vs_5_0");auto* p=compile(pixel_source(true),"ps_5_0");
        auto semanticTable=semantics();ShaderSet shaders;std::string error;
        if(!b.create_shaders(route,{v->GetBufferPointer(),v->GetBufferSize()},{p->GetBufferPointer(),p->GetBufferSize()},semanticTable.data(),4,semanticTable.data(),4,shaders,error))std::cerr<<error<<'\n';
        assert(shaders.vertex&&shaders.pixel);drop(v);drop(p);
        std::cout<<"hardware route "<<views<<" starting (private 32x32 array, three vertices)"<<std::endl;
        const auto reference=f.legacy(views);f.clear();f.bind();f.c->UpdateSubresource(f.cb,0,nullptr,&f.data,0,0);
        f.c->VSSetShader(shaders.vertex,nullptr,0);f.c->PSSetShader(shaders.pixel,nullptr,0);f.c->OMSetRenderTargets(1,&f.rt,f.ds);
        const auto submission=b.submit_indexed(route,f.drawIndices,0,0);assert(submission.submitted&&submission.disabled);
        auto actual=f.read();float maxColor=0,maxDepth=0;unsigned colorDiff=0,depthDiff=0;
        for(size_t i=0;i<actual.color.size();++i){const float e=std::abs(actual.color[i]-reference.color[i]);maxColor=(std::max)(maxColor,e);if(!std::isfinite(e)||e>2e-6f)++colorDiff;}
        for(size_t i=0;i<actual.depth.size();++i){const float e=std::abs(actual.depth[i]-reference.depth[i]);maxDepth=(std::max)(maxDepth,e);if(!std::isfinite(e)||e>2e-6f)++depthDiff;}
        std::cout<<"MVR4 colorDifferent="<<colorDiff<<" depthDifferent="<<depthDiff<<" maxColor="<<maxColor<<" maxDepth="<<maxDepth<<" coverage=";
        for(unsigned view=0;view<4;++view){unsigned covered=0;for(unsigned i=0;i<Fixture::width*Fixture::width;++i)if(actual.depth[view*Fixture::width*Fixture::width+i]<1)++covered;std::cout<<covered<<' ';assert(view<views?covered>20:covered==0);}
        std::cout<<'\n';assert(colorDiff==0&&depthDiff==0);
        // Reset must affect the actual hardware, not merely return NVAPI_OK.
        f.clear();f.bind();f.c->VSSetShader(f.vs,nullptr,0);f.c->PSSetShader(f.ps,nullptr,0);f.data.selected=0;
        f.c->UpdateSubresource(f.cb,0,nullptr,&f.data,0,0);f.c->OMSetRenderTargets(1,&f.rt,f.ds);f.c->DrawIndexed(f.drawIndices,0,0);actual=f.read();
        for(unsigned view=0;view<4;++view){unsigned covered=0;for(unsigned i=0;i<Fixture::width*Fixture::width;++i)if(actual.depth[view*Fixture::width*Fixture::width+i]<1)++covered;assert(view==0?covered>20:covered==0);}
        std::cout<<"ordinary draw after reset: only slice 0 covered\n";
    }
}
}
int main(int argc,char** argv){
    try{units();if(argc>1&&std::strcmp(argv[1],"--hardware")==0)hardware();return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<"; hardware validation FAILED, no equivalence established\n";return 1;}
}
