#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../stereo_instancing.hpp"
#include <d3dcompiler.h>
#include <dxgi.h>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
namespace {
template<class T> void drop(T*& p){if(p){p->Release();p=nullptr;}}
const char* shader=R"(
cbuffer Cameras:register(b0) {row_major float4x4 views[2];uint fixedEye;float3 padding;};
struct Input {float3 position:POSITION;float3 color:COLOR0;float3 instanceColor:COLOR1;uint instance:SV_InstanceID;};
struct Base {float4 position:SV_Position;float3 color:COLOR0;nointerpolation uint eye:TEXCOORD0;};
struct Routed {float4 position:SV_Position;float3 color:COLOR0;uint eye:SV_RenderTargetArrayIndex;};
Base evaluate(Input input,uint eye,uint object) {
    Base o;float3 p=input.position;p.x+=(float(object)-1.5)*1.1;
    o.position=mul(float4(p,1),views[eye]);o.color=input.color*input.instanceColor;o.eye=eye;return o;
}
Routed VSDirect(Input input) {Base b=evaluate(input,input.instance%2,input.instance/2);Routed o;o.position=b.position;o.color=b.color;o.eye=b.eye;return o;}
Base VSGeometry(Input input) {return evaluate(input,input.instance%2,input.instance/2);}
Base VSReference(Input input) {return evaluate(input,fixedEye,input.instance);}
[maxvertexcount(3)] void GSMain(triangle Base input[3],inout TriangleStream<Routed> stream) {
    for(uint i=0;i<3;++i){Routed o;o.position=input[i].position;o.color=input[i].color;o.eye=input[i].eye;stream.Append(o);}
}
float4 PSMain(float4 position:SV_Position,float3 color:COLOR0):SV_Target {return float4(color,1);}
)";
ID3DBlob* compile(const char* entry,const char* profile){
    ID3DBlob *code=nullptr,*errors=nullptr;
    const auto result=D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if(FAILED(result)&&errors)std::cerr<<static_cast<const char*>(errors->GetBufferPointer());
    drop(errors);assert(SUCCEEDED(result));return code;
}
struct Mat {float v[16]{};};
Mat multiply(const Mat& a,const Mat& b){Mat r;for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)for(unsigned k=0;k<4;++k)r.v[y*4+x]+=a.v[y*4+k]*b.v[k*4+x];return r;}
Mat camera(float eye,float l,float r,float b,float t){
    Mat view;for(unsigned i=0;i<4;++i)view.v[i*4+i]=1;view.v[12]=-eye;
    Mat projection;projection.v[0]=2/(r-l);projection.v[5]=2/(t-b);projection.v[8]=(l+r)/(l-r);projection.v[9]=(t+b)/(b-t);
    projection.v[10]=20.0f/19;projection.v[11]=1;projection.v[14]=-20.0f/19;return multiply(view,projection);
}
struct alignas(16) Cameras {Mat views[2];UINT eye=0;float pad[3]{};};
struct Pixels {std::array<std::vector<uint32_t>,2> color;std::array<std::vector<float>,2> depth;};
struct Fixture {
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    ID3D11Texture2D *color=nullptr,*depth=nullptr;ID3D11RenderTargetView *both=nullptr,*eyes[2]{};
    ID3D11DepthStencilView *bothDepth=nullptr,*eyeDepth[2]{};
    ID3D11VertexShader *direct=nullptr,*geometry=nullptr,*reference=nullptr;ID3D11GeometryShader* gs=nullptr;ID3D11PixelShader* ps=nullptr;
    ID3D11InputLayout *normalLayout=nullptr,*stereoLayout=nullptr;
    ID3D11Buffer *vertices=nullptr,*indices=nullptr,*colors=nullptr,*cameras=nullptr;
    ID3D11RasterizerState* raster=nullptr;ID3D11DepthStencilState* depthState=nullptr;
    lmuqv::StereoCapabilities capabilities;static constexpr UINT size=96;
    explicit Fixture(bool hardware){
        D3D_FEATURE_LEVEL level{};assert(SUCCEEDED(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
        capabilities=lmuqv::query_stereo_capabilities(device);assert(capabilities.route!=lmuqv::StereoRoute::unavailable);
        if(hardware){IDXGIDevice* dxgi=nullptr;IDXGIAdapter* adapter=nullptr;DXGI_ADAPTER_DESC desc{};
            assert(SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(&dxgi))));assert(SUCCEEDED(dxgi->GetAdapter(&adapter)));assert(SUCCEEDED(adapter->GetDesc(&desc)));
            std::wcout<<L"Hardware adapter: "<<desc.Description<<L'\n';drop(adapter);drop(dxgi);}
        D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=size;td.ArraySize=2;td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        assert(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&color)));td.Format=DXGI_FORMAT_D32_FLOAT;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;assert(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&depth)));
        D3D11_RENDER_TARGET_VIEW_DESC rt{};rt.Format=DXGI_FORMAT_R8G8B8A8_UNORM;rt.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;rt.Texture2DArray.ArraySize=2;assert(SUCCEEDED(device->CreateRenderTargetView(color,&rt,&both)));
        D3D11_DEPTH_STENCIL_VIEW_DESC ds{};ds.Format=DXGI_FORMAT_D32_FLOAT;ds.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DARRAY;ds.Texture2DArray.ArraySize=2;assert(SUCCEEDED(device->CreateDepthStencilView(depth,&ds,&bothDepth)));
        for(UINT i=0;i<2;++i){rt.Texture2DArray.ArraySize=1;rt.Texture2DArray.FirstArraySlice=i;assert(SUCCEEDED(device->CreateRenderTargetView(color,&rt,&eyes[i])));
            ds.Texture2DArray.ArraySize=1;ds.Texture2DArray.FirstArraySlice=i;assert(SUCCEEDED(device->CreateDepthStencilView(depth,&ds,&eyeDepth[i])));}
        auto* code=compile("VSGeometry","vs_5_0");assert(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&geometry)));
        D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",1,DXGI_FORMAT_R32G32B32_FLOAT,1,0,D3D11_INPUT_PER_INSTANCE_DATA,1}};
        assert(SUCCEEDED(device->CreateInputLayout(elements,3,code->GetBufferPointer(),code->GetBufferSize(),&normalLayout)));elements[2].InstanceDataStepRate=2;
        assert(SUCCEEDED(device->CreateInputLayout(elements,3,code->GetBufferPointer(),code->GetBufferSize(),&stereoLayout)));drop(code);
        code=compile("VSReference","vs_5_0");assert(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&reference)));drop(code);
        if(capabilities.vertexArrayIndex){code=compile("VSDirect","vs_5_0");assert(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&direct)));drop(code);}
        code=compile("GSMain","gs_5_0");assert(SUCCEEDED(device->CreateGeometryShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&gs)));drop(code);
        code=compile("PSMain","ps_5_0");assert(SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&ps)));drop(code);
        // Near triangle first, then the larger far triangle: depth must preserve the near surface.
        const float data[]={-.65f,-.7f,2,1,0,0, 0,.75f,2,1,0,0, .65f,-.7f,2,1,0,0,
                            -1.4f,-1.4f,4,0,1,0, 0,1.5f,4,0,1,0, 1.4f,-1.4f,4,0,1,0};
        const UINT index[]={0,1,2,3,4,5};const float instanceColors[]={1,.7f,.5f, .6f,1,.4f, 1,.5f,1, .5f,1,1};
        auto buffer=[&](UINT bytes,UINT bind,const void* src,ID3D11Buffer** out){D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.BindFlags=bind;D3D11_SUBRESOURCE_DATA initial{src,0,0};assert(SUCCEEDED(device->CreateBuffer(&d,src?&initial:nullptr,out)));};
        buffer(sizeof(data),D3D11_BIND_VERTEX_BUFFER,data,&vertices);buffer(sizeof(index),D3D11_BIND_INDEX_BUFFER,index,&indices);buffer(sizeof(instanceColors),D3D11_BIND_VERTEX_BUFFER,instanceColors,&colors);buffer(sizeof(Cameras),D3D11_BIND_CONSTANT_BUFFER,nullptr,&cameras);
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;assert(SUCCEEDED(device->CreateRasterizerState(&rd,&raster)));
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=TRUE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;dd.DepthFunc=D3D11_COMPARISON_LESS;assert(SUCCEEDED(device->CreateDepthStencilState(&dd,&depthState)));
    }
    Pixels draw(bool stereo,bool useGeometry,UINT count,UINT start){
        const float clear[]={0,0,0,1};context->ClearRenderTargetView(both,clear);context->ClearDepthStencilView(bothDepth,D3D11_CLEAR_DEPTH,1,0);
        ID3D11Buffer* vb[]={vertices,colors};const UINT strides[]={24,12},offsets[]={0,0};context->IASetVertexBuffers(0,2,vb,strides,offsets);context->IASetIndexBuffer(indices,DXGI_FORMAT_R32_UINT,0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->IASetInputLayout(stereo?stereoLayout:normalLayout);
        context->RSSetState(raster);D3D11_VIEWPORT viewport{0,0,float(size),float(size),0,1};context->RSSetViewports(1,&viewport);context->OMSetDepthStencilState(depthState,0);
        context->PSSetShader(ps,nullptr,0);context->VSSetConstantBuffers(0,1,&cameras);
        Cameras cb{{camera(-.14f,-.9f,1.1f,-1,1),camera(.14f,-1.1f,.9f,-.85f,1.15f)}};
        if(stereo){
            context->OMSetRenderTargets(1,&both,bothDepth);context->VSSetShader(useGeometry?geometry:direct,nullptr,0);context->GSSetShader(useGeometry?gs:nullptr,nullptr,0);
            context->UpdateSubresource(cameras,0,nullptr,&cb,0,0);assert(lmuqv::submit_stereo_indexed(context,6,count,0,0,start));
        }else for(UINT eye=0;eye<2;++eye){
            context->OMSetRenderTargets(1,&eyes[eye],eyeDepth[eye]);context->VSSetShader(reference,nullptr,0);context->GSSetShader(nullptr,nullptr,0);cb.eye=eye;
            context->UpdateSubresource(cameras,0,nullptr,&cb,0,0);context->DrawIndexedInstanced(6,count,0,0,start);
        }
        context->OMSetRenderTargets(0,nullptr,nullptr);Pixels output;
        auto read=[&](ID3D11Texture2D* texture,bool isDepth){D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);d.ArraySize=1;d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Texture2D* staging=nullptr;
            assert(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging)));
            for(UINT eye=0;eye<2;++eye){context->CopySubresourceRegion(staging,0,0,0,0,texture,eye,nullptr);D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&map)));
                if(isDepth)output.depth[eye].resize(size*size);else output.color[eye].resize(size*size);
                for(UINT y=0;y<size;++y){void* destination=isDepth?static_cast<void*>(output.depth[eye].data()+y*size):static_cast<void*>(output.color[eye].data()+y*size);std::memcpy(destination,static_cast<const char*>(map.pData)+y*map.RowPitch,size*4);}
                context->Unmap(staging,0);}drop(staging);};
        read(color,false);read(depth,true);return output;
    }
    ~Fixture(){context->ClearState();drop(depthState);drop(raster);drop(cameras);drop(colors);drop(indices);drop(vertices);drop(stereoLayout);drop(normalLayout);drop(ps);drop(gs);drop(reference);drop(geometry);drop(direct);for(auto*& p:eyes)drop(p);for(auto*& p:eyeDepth)drop(p);drop(both);drop(bothDepth);drop(color);drop(depth);drop(context);drop(device);}
};
void equivalent(const Pixels& expected,const Pixels& actual){
    if(actual.color!=expected.color){for(unsigned eye=0;eye<2;++eye){unsigned differences=0;for(unsigned i=0;i<actual.color[eye].size();++i)if(actual.color[eye][i]!=expected.color[eye][i]){if(differences<2)std::cerr<<"eye="<<eye<<" pixel="<<i<<" expected="<<std::hex<<expected.color[eye][i]<<" actual="<<actual.color[eye][i]<<std::dec<<'\n';++differences;}std::cerr<<"color differences="<<differences<<'\n';}}
    assert(actual.color==expected.color);assert(actual.color[0]!=actual.color[1]);
    for(unsigned eye=0;eye<2;++eye){unsigned foreground=0,background=0,red=0,green=0;for(unsigned i=0;i<Fixture::size*Fixture::size;++i){
        assert(std::abs(expected.depth[eye][i]-actual.depth[eye][i])<.000001f);if(actual.depth[eye][i]<1)++foreground;else ++background;
        const auto p=actual.color[eye][i];if((p&255)&&!((p>>8)&255)){++red;assert(std::abs(actual.depth[eye][i]-10.0f/19)<.000001f);}
        if(((p>>8)&255)&&!(p&255)){++green;assert(std::abs(actual.depth[eye][i]-15.0f/19)<.000001f);}
    }assert(foreground>40&&background>40&&red>10&&green>10);}
}
}
int main(int argc,char** argv){
    using namespace lmuqv;StereoInstances instances;assert(stereo_instances(3,4,instances)&&instances.count==6&&instances.start==4);
    assert(!stereo_instances(0x80000000u,0,instances));assert(stereo_instances(1,0xffffffffu,instances)&&instances.start==0xffffffffu);
    UINT step=0;assert(stereo_step_rate(3,step)&&step==6);assert(!stereo_step_rate(0x80000000u,step));assert(query_stereo_capabilities(nullptr).route==StereoRoute::unavailable);
    assert(!submit_stereo_indexed(nullptr,3,1,0,0,0));
    const bool hardware=argc>1&&!std::strcmp(argv[1],"--hardware");Fixture f(hardware);
    const std::array<std::array<UINT,2>,3> ranges{{{1,0},{2,1},{4,0}}};
    for(auto range:ranges){std::cout<<"case instances="<<range[0]<<" start="<<range[1]<<" route=geometry\n"<<std::flush;const auto expected=f.draw(false,false,range[0],range[1]);equivalent(expected,f.draw(true,true,range[0],range[1]));if(f.capabilities.vertexArrayIndex){std::cout<<"route=vprt\n"<<std::flush;equivalent(expected,f.draw(true,false,range[0],range[1]));}}
    std::cout<<"Portable instanced stereo "<<(hardware?"hardware":"WARP")<<" passed: route="<<stereo_route_name(f.capabilities.route)<<", one stereo draw vs two eye draws, asymmetric projections, color/depth, parallax, existing instancing, base instance, IA step rate and overflow guards; no NVAPI\n";
}
