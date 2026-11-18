#pragma once
// Offline diagnostic build only. Read a bounded number of constant buffers at
// the draw, before LMU can overwrite them between the outer and focus scenes.
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <map>
#include <string>
#include <vector>

namespace lmuqv::focus_mvr::shadow_probe {
using Microsoft::WRL::ComPtr;
constexpr GUID reflectionId={0xb762af0a,0xda21,0x4323,{0xab,0x44,0xcd,0x56,0xa1,0x30,0x62,0x58}};
inline void tag(ID3D11DeviceChild* shader,const void* code,SIZE_T bytes){
    ComPtr<ID3D11ShaderReflection> reflection;
    if(SUCCEEDED(D3DReflect(code,bytes,IID_PPV_ARGS(&reflection))))
        shader->SetPrivateDataInterface(reflectionId,reflection.Get());
}
inline ComPtr<ID3D11ShaderReflection> reflection(ID3D11DeviceChild* shader){
    ComPtr<ID3D11ShaderReflection> result;
    if(shader){UINT size=sizeof(void*);shader->GetPrivateData(reflectionId,&size,result.GetAddressOf());}
    return result;
}
struct Draw {
    std::array<UINT,6> signature{};
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> vb,ib;
    UINT stride{},offset{},indexOffset{};
    DXGI_FORMAT indexFormat{};
    ComPtr<ID3D11Resource> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    std::map<std::string,std::vector<unsigned char>> fields;
};
struct Probe {
    Draw outer;
    unsigned attempts[2]{}, matches[2]{},eye{};
    bool samplingOuter{},samplingFocus{};
};
inline thread_local Probe probe;

inline void read_fields(ID3D11DeviceContext* ctx,ID3D11ShaderReflection* ref,bool vertex,Draw& draw){
    if(!ref)return;
    D3D11_SHADER_DESC shader{};
    if(FAILED(ref->GetDesc(&shader)))return;
    ComPtr<ID3D11DeviceContext1> ctx1;ctx->QueryInterface(IID_PPV_ARGS(&ctx1));
    for(UINT index=0;index<shader.ConstantBuffers;++index){
        auto cb=ref->GetConstantBufferByIndex(index);
        D3D11_SHADER_BUFFER_DESC metadata{};D3D11_SHADER_INPUT_BIND_DESC binding{};
        if(FAILED(cb->GetDesc(&metadata))||!metadata.Name||FAILED(ref->GetResourceBindingDescByName(metadata.Name,&binding))||
           binding.Type!=D3D_SIT_CBUFFER||binding.BindPoint>=D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)continue;
        ComPtr<ID3D11Buffer> buffer;
        UINT first=0,count=4096;
        if(ctx1){
            if(vertex)ctx1->VSGetConstantBuffers1(binding.BindPoint,1,&buffer,&first,&count);
            else ctx1->PSGetConstantBuffers1(binding.BindPoint,1,&buffer,&first,&count);
        }else if(vertex)ctx->VSGetConstantBuffers(binding.BindPoint,1,&buffer);
        else ctx->PSGetConstantBuffers(binding.BindPoint,1,&buffer);
        if(!buffer)continue;
        D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);
        if(desc.ByteWidth>1024*1024||first>desc.ByteWidth/16)continue;
        desc.BindFlags=desc.MiscFlags=desc.StructureByteStride=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Device> device;ctx->GetDevice(&device);
        ComPtr<ID3D11Buffer> staging;
        if(FAILED(device->CreateBuffer(&desc,nullptr,&staging)))continue;
        ctx->CopyResource(staging.Get(),buffer.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if(FAILED(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))continue;
        struct Unmap {ID3D11DeviceContext* c;ID3D11Buffer* b;~Unmap(){c->Unmap(b,0);}} unmap{ctx,staging.Get()};
        const auto* bytes=static_cast<const unsigned char*>(mapped.pData)+first*16;
        UINT limit=(std::min)(desc.ByteWidth-first*16,(std::min)(count,4096u)*16);
        for(UINT i=0;i<metadata.Variables;++i){
            D3D11_SHADER_VARIABLE_DESC v{};
            if(FAILED(cb->GetVariableByIndex(i)->GetDesc(&v))||!v.Name||!(v.uFlags&D3D_SVF_USED)||
               v.StartOffset>limit||v.Size>limit-v.StartOffset)continue;
            auto key=std::string(vertex?"VS.":"PS.")+metadata.Name+"."+v.Name;
            draw.fields[key].assign(bytes+v.StartOffset,bytes+v.StartOffset+v.Size);
        }
    }
}
inline void begin(unsigned eye){
    probe.eye=eye;
    probe.outer={};
    probe.samplingFocus=false;
    probe.samplingOuter=eye<2&&probe.matches[eye]<3&&probe.attempts[eye]++<16;
}
inline void focus(){probe.samplingOuter=false;probe.samplingFocus=bool(probe.outer.ps);}
inline void finish(){probe.samplingOuter=probe.samplingFocus=false;probe.outer={};}
inline void inspect(ID3D11DeviceContext* c,const std::array<UINT,6>& signature){
    if(!probe.samplingOuter&&!probe.samplingFocus)return;
    if(signature[0]>4||(probe.samplingFocus&&signature!=probe.outer.signature))return;
    Draw d;d.signature=signature;
    c->PSGetShader(&d.ps,nullptr,nullptr);
    if(!d.ps||(probe.samplingFocus&&d.ps!=probe.outer.ps))return;
    auto ps=reflection(d.ps.Get());
    D3D11_SHADER_INPUT_BIND_DESC binding{};
    if(!ps||FAILED(ps->GetResourceBindingDescByName("sShad_Cmp_Texture",&binding))||binding.Type!=D3D_SIT_TEXTURE)return;
    c->PSGetShaderResources(binding.BindPoint,1,&d.view);
    if(!d.view)return;
    c->VSGetShader(&d.vs,nullptr,nullptr);
    c->IAGetVertexBuffers(0,1,&d.vb,&d.stride,&d.offset);
    c->IAGetIndexBuffer(&d.ib,&d.indexFormat,&d.indexOffset);
    if(probe.samplingFocus&&(d.vs!=probe.outer.vs||d.vb!=probe.outer.vb||d.ib!=probe.outer.ib||
       d.stride!=probe.outer.stride||d.offset!=probe.outer.offset||d.indexOffset!=probe.outer.indexOffset||
       d.indexFormat!=probe.outer.indexFormat))return;
    d.view->GetResource(&d.texture);
    read_fields(c,reflection(d.vs.Get()).Get(),true,d);
    read_fields(c,ps.Get(),false,d);
    if(probe.samplingOuter){probe.outer=std::move(d);probe.samplingOuter=false;return;}
    char line[512];
    std::snprintf(line,sizeof(line),"mvr_shadow_compare eye=%u sample=%u same_resource=%d same_srv=%d outer_fields=%zu focus_fields=%zu",
        probe.eye,++probe.matches[probe.eye],int(d.texture==probe.outer.texture),int(d.view==probe.outer.view),probe.outer.fields.size(),d.fields.size());
    log_line(line);
    for(const auto& [key,value]:probe.outer.fields){
        auto it=d.fields.find(key);
        std::snprintf(line,sizeof(line),"mvr_shadow_field eye=%u sample=%u name=%s bytes=%zu verdict=%s",
            probe.eye,probe.matches[probe.eye],key.c_str(),value.size(),it==d.fields.end()?"missing":it->second==value?"equal":"different");
        log_line(line);
    }
    finish();
}
}
