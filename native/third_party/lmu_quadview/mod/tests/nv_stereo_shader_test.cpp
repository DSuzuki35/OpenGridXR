#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../nv_stereo_shader.hpp"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

namespace {
template<class T> void drop(T*& p){if(p){p->Release();p=nullptr;}}
ID3DBlob* compile(const char* source,const char* profile){
    ID3DBlob *code=nullptr,*errors=nullptr;const auto hr=D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,"main",profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if(FAILED(hr)&&errors){std::cerr<<static_cast<char*>(errors->GetBufferPointer());}
    drop(errors);assert(SUCCEEDED(hr));return code;
}
const char* nativeVs=R"(
cbuffer A:register(b0){float4 offset;};cbuffer B:register(b4){float4 tint;};
struct O{float4 p:SV_Position;float3 c:COLOR0;};
O main(float3 p:POSITION,uint instance:SV_InstanceID){O o;o.p=float4(p+float3(instance*.125,0,0),1)+offset;o.c=tint.rgb;return o;}
)";
void signature(const lmuqv::NvStereoShaderVariant& variant,bool vertex){
    ID3D11ShaderReflection* r=nullptr;assert(SUCCEEDED(D3DReflect(variant.bytecode.data(),variant.bytecode.size(),IID_ID3D11ShaderReflection,reinterpret_cast<void**>(&r))));
    D3D11_SHADER_DESC desc{};assert(SUCCEEDED(r->GetDesc(&desc)));
    bool instance=false,eye=false;
    for(unsigned i=0;i<desc.InputParameters;++i){D3D11_SIGNATURE_PARAMETER_DESC p{};assert(SUCCEEDED(r->GetInputParameterDesc(i,&p)));instance|=p.SystemValueType==D3D_NAME_INSTANCE_ID;eye|=std::string(p.SemanticName)=="LMU_STEREO_EYE_VIEW_0_SEMANTIC";}
    assert(vertex?instance:eye);if(vertex){assert(desc.OutputParameters==variant.views*3+2);for(unsigned i=0;i<desc.OutputParameters;++i){D3D11_SIGNATURE_PARAMETER_DESC p{};assert(SUCCEEDED(r->GetOutputParameterDesc(i,&p)));assert(p.Mask==15);}}drop(r);
}
void capturedVertexOutputs(ID3D11Device* d,ID3D11DeviceContext* c,ID3DBlob* native,const lmuqv::NvStereoShaderVariant& v){
    // WARP cannot exercise NVIDIA routing. Capture every cloned output before
    // rasterization to prove native instance IDs and per-view constants survive.
    std::vector<std::string> names;for(unsigned group=0;group<3;++group)for(unsigned view=0;view<v.views;++view)
        names.push_back(group==0?(view?"NV_POSITION_VIEW_"+std::to_string(view)+"_SEMANTIC":"SV_Position"):(group==1?"LMU_ATTR_COLOR_0":"LMU_STEREO_EYE")+std::string("_VIEW_")+std::to_string(view)+"_SEMANTIC");
    names.push_back("NV_VIEWPORT_MASK");names.push_back("NV_VIEWPORT_MASK_2_SEMANTIC");
    std::string source="struct O{";std::vector<D3D11_SO_DECLARATION_ENTRY> entries;
    for(unsigned i=0;i<names.size();++i){source+=(i<2*v.views?"float4":"nointerpolation uint4")+std::string(" a")+std::to_string(i)+":"+names[i]+";";entries.push_back({0,names[i].c_str(),0,0,BYTE(i<2*v.views||i>=3*v.views?4:1),0});}
    source+="};[maxvertexcount(1)]void main(point O i[1],inout PointStream<O> s){s.Append(i[0]);}";
    auto* code=compile(source.c_str(),"gs_5_0");ID3D11GeometryShader* gs=nullptr;const UINT stride=(v.views*9+8)*4;
    assert(SUCCEEDED(d->CreateGeometryShaderWithStreamOutput(code->GetBufferPointer(),code->GetBufferSize(),entries.data(),UINT(entries.size()),&stride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&gs)));drop(code);
    ID3D11VertexShader* vs=nullptr;assert(SUCCEEDED(d->CreateVertexShader(v.bytecode.data(),v.bytecode.size(),nullptr,&vs)));
    D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};ID3D11InputLayout* layout=nullptr;
    assert(SUCCEEDED(d->CreateInputLayout(&element,1,native->GetBufferPointer(),native->GetBufferSize(),&layout)));
    auto buffer=[&](UINT bytes,UINT bind,const void* data){D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;desc.BindFlags=bind;D3D11_SUBRESOURCE_DATA initial{data,0,0};ID3D11Buffer* b=nullptr;assert(SUCCEEDED(d->CreateBuffer(&desc,data?&initial:nullptr,&b)));return b;};
    const float point[3]{.25f,.5f,.75f};auto* vertices=buffer(sizeof(point),D3D11_BIND_VERTEX_BUFFER,point);
    std::vector<float> constants(v.eyeStride*v.views*4);for(unsigned view=0;view<v.views;++view)for(unsigned k=0;k<4;++k){constants[(view*2)*4+k]=float(view*4+k)*.0625f;constants[(view*2+1)*4+k]=float(view*4+k)*.125f;}
    auto* banks=buffer(UINT(constants.size()*4),D3D11_BIND_CONSTANT_BUFFER,constants.data());auto* output=buffer(stride*2,D3D11_BIND_STREAM_OUTPUT,nullptr);
    c->VSSetShader(vs,nullptr,0);c->VSSetConstantBuffers(v.packedSlot,1,&banks);c->GSSetShader(gs,nullptr,0);c->PSSetShader(nullptr,nullptr,0);c->IASetInputLayout(layout);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    const UINT vertexStride=12,offset=0;c->IASetVertexBuffers(0,1,&vertices,&vertexStride,&offset);c->SOSetTargets(1,&output,&offset);c->DrawInstanced(1,2,0,0);c->SOSetTargets(0,nullptr,nullptr);
    D3D11_BUFFER_DESC desc{};output->GetDesc(&desc);desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Buffer* staging=nullptr;assert(SUCCEEDED(d->CreateBuffer(&desc,nullptr,&staging)));c->CopyResource(staging,output);
    D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(c->Map(staging,0,D3D11_MAP_READ,0,&mapped)));const auto* raw=static_cast<const uint8_t*>(mapped.pData);
    for(unsigned instance=0;instance<2;++instance)for(unsigned view=0;view<v.views;++view){const auto* values=reinterpret_cast<const float*>(raw+instance*stride);for(unsigned k=0;k<4;++k){const float base=k<3?point[k]:1.f;const float expected=base+(k==0?instance*.125f:0)+constants[view*8+k];assert(std::fabs(values[view*4+k]-expected)<1e-6f);assert(values[v.views*4+view*4+k]==(k<3?constants[view*8+4+k]:0));}
        const auto* eye=reinterpret_cast<const uint32_t*>(values+v.views*8);assert(eye[view]==view);for(unsigned k=0;k<4;++k){assert(eye[v.views+k]==1);assert(eye[v.views+4+k]==0);}}
    c->Unmap(staging,0);c->ClearState();drop(staging);drop(output);drop(banks);drop(vertices);drop(layout);drop(vs);drop(gs);
}
void pixelBanks(ID3D11Device* d,ID3D11DeviceContext* c,const lmuqv::NvStereoShaderVariant& p){
    // Supply each view index through a normal shader on WARP. This checks the
    // converted PS uses bank 2/3, rather than accidentally masking to two eyes.
    auto* code=compile("cbuffer V:register(b0){uint eye;};struct O{float4 p:SV_Position;float4 c:LMU_ATTR_COLOR_0_VIEW_0_SEMANTIC;nointerpolation uint e:LMU_STEREO_EYE_VIEW_0_SEMANTIC;};O main(uint id:SV_VertexID){O o;o.p=float4(id==1?3:-1,id==2?3:-1,.5,1);o.c=1;o.e=eye;return o;}","vs_5_0");
    ID3D11VertexShader* vs=nullptr;assert(SUCCEEDED(d->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vs)));drop(code);ID3D11PixelShader* ps=nullptr;assert(SUCCEEDED(d->CreatePixelShader(p.bytecode.data(),p.bytecode.size(),nullptr,&ps)));
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;ID3D11Texture2D* target=nullptr;assert(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&target)));ID3D11RenderTargetView* rt=nullptr;assert(SUCCEEDED(d->CreateRenderTargetView(target,nullptr,&rt)));
    td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Texture2D* staging=nullptr;assert(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&staging)));
    std::vector<float> constants(p.eyeStride*p.views*4);for(unsigned i=0;i<constants.size();++i)constants[i]=float(i+1)*.0625f;
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=UINT(constants.size()*4);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA initial{constants.data(),0,0};ID3D11Buffer* banks=nullptr;assert(SUCCEEDED(d->CreateBuffer(&bd,&initial,&banks)));bd.ByteWidth=16;ID3D11Buffer* index=nullptr;assert(SUCCEEDED(d->CreateBuffer(&bd,nullptr,&index)));
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ID3D11RasterizerState* raster=nullptr;assert(SUCCEEDED(d->CreateRasterizerState(&rd,&raster)));
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->VSSetShader(vs,nullptr,0);c->PSSetShader(ps,nullptr,0);c->VSSetConstantBuffers(0,1,&index);c->PSSetConstantBuffers(p.packedSlot,1,&banks);c->OMSetRenderTargets(1,&rt,nullptr);c->RSSetState(raster);D3D11_VIEWPORT viewport{0,0,1,1,0,1};c->RSSetViewports(1,&viewport);
    for(unsigned view=0;view<p.views;++view){const uint32_t values[4]{view,0,0,0};c->UpdateSubresource(index,0,nullptr,values,0,0);c->Draw(3,0);c->CopyResource(staging,target);D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(c->Map(staging,0,D3D11_MAP_READ,0,&mapped)));const auto* color=static_cast<const float*>(mapped.pData);for(unsigned k=0;k<4;++k)assert(color[k]==constants[view*p.eyeStride*4+k]);c->Unmap(staging,0);}
    c->ClearState();drop(index);drop(banks);drop(raster);drop(staging);drop(rt);drop(target);drop(ps);drop(vs);
}
}
int main(){
    ID3D11Device* d=nullptr;ID3D11DeviceContext* c=nullptr;D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&level,&c)));
    auto* code=compile(nativeVs,"vs_4_0");std::string error;
    for(unsigned views:{2u,4u}){
        lmuqv::NvStereoShaderVariant v;assert(lmuqv::convert_nv_stereo_shader(code->GetBufferPointer(),code->GetBufferSize(),lmuqv::StereoShaderStage::vertex,17,views,v,error));
        assert(v.views==views&&v.eyeStride==2&&v.constants.size()==2&&v.constants[0].offset==0&&v.constants[1].offset==1&&v.customSemantics.size()==5);
        assert(v.customSemantics[0].type==5&&v.customSemantics[0].name=="NV_POSITION");signature(v,true);
        ID3D11VertexShader* vs=nullptr;assert(SUCCEEDED(d->CreateVertexShader(v.bytecode.data(),v.bytecode.size(),nullptr,&vs)));drop(vs);
        capturedVertexOutputs(d,c,code,v);
        auto* pixel=compile("cbuffer C:register(b3){float4 scale;};float4 main(float4 p:SV_Position,float3 c:COLOR0):SV_Target{return float4(c,1)*scale;}","ps_4_0");
        lmuqv::NvStereoShaderVariant p;assert(lmuqv::convert_nv_stereo_shader(pixel->GetBufferPointer(),pixel->GetBufferSize(),lmuqv::StereoShaderStage::pixel,8,views,p,error));signature(p,false);
        ID3D11PixelShader* ps=nullptr;assert(SUCCEEDED(d->CreatePixelShader(p.bytecode.data(),p.bytecode.size(),nullptr,&ps)));drop(ps);drop(pixel);
        pixelBanks(d,c,p);
    }
    lmuqv::NvStereoShaderVariant v;assert(!lmuqv::convert_nv_stereo_shader(code->GetBufferPointer(),code->GetBufferSize(),lmuqv::StereoShaderStage::vertex,17,3,v,error)&&v.bytecode.empty());drop(code);
    code=compile("cbuffer C:register(b0){float4 offsets[2];};float4 main(float3 p:POSITION):SV_Position{return float4(p,1)+offsets[uint(abs(p.x))&1];}","vs_4_0");
    assert(!lmuqv::convert_nv_stereo_shader(code->GetBufferPointer(),code->GetBufferSize(),lmuqv::StereoShaderStage::vertex,1,4,v,error)&&error=="selected CB uses dynamic indexing"&&v.bytecode.empty());drop(code);
    code=compile("cbuffer C:register(b0){float4 offset[1200];};float4 main(float3 p:POSITION):SV_Position{return float4(p,1)+offset[1199];}","vs_4_0");
    assert(!lmuqv::convert_nv_stereo_shader(code->GetBufferPointer(),code->GetBufferSize(),lmuqv::StereoShaderStage::vertex,1,4,v,error)&&error=="multiview constants exceed D3D11 CB capacity"&&v.constants.empty());drop(code);
    code=compile(nativeVs,"vs_5_0");assert(!lmuqv::convert_nv_stereo_shader(code->GetBufferPointer(),code->GetBufferSize(),lmuqv::StereoShaderStage::vertex,17,4,v,error)&&error=="SM4 input/output signatures and SHDR required");drop(code);
    std::string wide="struct O{float4 p:SV_Position;";for(unsigned i=0;i<6;++i)wide+="float4 a"+std::to_string(i)+":TEXCOORD"+std::to_string(i)+";";
    wide+="};O main(float4 p:POSITION){O o;o.p=p;";for(unsigned i=0;i<6;++i)wide+="o.a"+std::to_string(i)+"=p*"+std::to_string(i+1)+";";wide+="return o;}";
    code=compile(wide.c_str(),"vs_4_0");assert(!lmuqv::convert_nv_stereo_shader(code->GetBufferPointer(),code->GetBufferSize(),lmuqv::StereoShaderStage::vertex,0,4,v,error)&&error=="multiview outputs exceed D3D11 signature capacity"&&v.bytecode.empty());drop(code);
    assert(!lmuqv::convert_nv_stereo_shader(nullptr,0,lmuqv::StereoShaderStage::vertex,1,4,v,error)&&v.customSemantics.empty());
    drop(c);drop(d);std::cout<<"NV shader adapter WARP validation passed (MVR routing requires hardware test)\n";
}
