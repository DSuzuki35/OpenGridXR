#include "stereo_test_fixture.hpp"
#include <d3d11shader.h>
namespace {
void checkSamplers(const void* code,size_t bytes,const lmuqv::StereoShaderVariant& variant){
    ID3D11ShaderReflection* reflection=nullptr;assert(SUCCEEDED(D3DReflect(code,bytes,IID_ID3D11ShaderReflection,reinterpret_cast<void**>(&reflection))));
    D3D11_SHADER_DESC desc{};assert(SUCCEEDED(reflection->GetDesc(&desc)));uint32_t mask=0;
    for(UINT i=0;i<desc.BoundResources;++i){D3D11_SHADER_INPUT_BIND_DESC binding{};assert(SUCCEEDED(reflection->GetResourceBindingDesc(i,&binding)));
        if(binding.Type==D3D_SIT_SAMPLER){assert(binding.BindPoint<16&&binding.BindCount&&binding.BindCount<=16-binding.BindPoint);for(UINT j=0;j<binding.BindCount;++j)mask|=1u<<(binding.BindPoint+j);}}
    assert(mask==variant.samplerSlots);drop(reflection);
}
}
int main(int argc,char** argv){
    bool hardware=false;for(int i=1;i<argc;++i)hardware|=!std::strcmp(argv[i],"--hardware");Fixture f(hardware);
    for(bool direct:{false,true}){
        if(direct&&!lmuqv::query_stereo_capabilities(f.d).vertexArrayIndex)continue;
        f.convert(direct);const auto reference=f.draw(false),stereo=f.draw(true);
        assert(f.vertex.samplerSlots==0&&f.pixelVariant.samplerSlots==0);
        for(unsigned eye=0;eye<2;++eye){assert(reference.color[eye]==stereo.color[eye]);unsigned nearPixels=0,farPixels=0;for(size_t i=0;i<reference.depth[eye].size();++i){assert(std::fabs(reference.depth[eye][i]-stereo.depth[eye][i])<1e-6f);nearPixels+=std::fabs(stereo.depth[eye][i]-.25f)<1e-6f;farPixels+=std::fabs(stereo.depth[eye][i]-.75f)<1e-6f;}assert(nearPixels>100&&farPixels>100);}
        assert(reference.color[0]!=reference.color[1]);std::cout<<"Converted SM4 VS+PS route="<<(direct?"vprt":"geometry")<<": one draw vs two reference eyes, separate VS/PS constants, asymmetric views and near/far color/depth PASS\n";
    }
    // Conversion failure must leave no partially usable shader or binding plan.
    lmuqv::StereoShaderVariant rejected;std::string error;assert(!lmuqv::convert_stereo_shader(nullptr,0,lmuqv::StereoShaderStage::vertex,1,false,rejected,error)&&rejected.bytecode.empty());
    auto* code=compile(vsSource,"vs_4_0");auto broken=std::vector<uint8_t>(static_cast<uint8_t*>(code->GetBufferPointer()),static_cast<uint8_t*>(code->GetBufferPointer())+code->GetBufferSize());drop(code);broken.back()^=1;
    assert(!lmuqv::convert_stereo_shader(broken.data(),broken.size(),lmuqv::StereoShaderStage::vertex,1,false,rejected,error)&&error=="invalid DXBC checksum"&&rejected.bytecode.empty());
    code=compile("float4 main(float3 p:POSITION,uint id:SV_InstanceID):SV_Position{return float4(p+float3(id,0,0),1);}","vs_4_0");assert(!lmuqv::convert_stereo_shader(code->GetBufferPointer(),code->GetBufferSize(),lmuqv::StereoShaderStage::vertex,1,false,rejected,error));drop(code);
    assert(error=="existing instanced/stereo input requires a separate adapter");
    code=compile("cbuffer C:register(b0){float4 offsets[2];};float4 main(float3 p:POSITION):SV_Position{return float4(p,1)+offsets[uint(abs(p.x))&1];}","vs_4_0");
    assert(!lmuqv::convert_stereo_shader(code->GetBufferPointer(),code->GetBufferSize(),lmuqv::StereoShaderStage::vertex,1,false,rejected,error)&&error=="selected CB uses dynamic indexing"&&rejected.bytecode.empty());drop(code);
    code=compile("cbuffer C:register(b13){float4 offset;};float4 main(float3 p:POSITION):SV_Position{return float4(p,1)+offset;}","vs_4_0");
    assert(!lmuqv::convert_stereo_shader(code->GetBufferPointer(),code->GetBufferSize(),lmuqv::StereoShaderStage::vertex,1,false,rejected,error)&&error=="packed constant slot unavailable or invalid CB size"&&rejected.bytecode.empty());drop(code);
    for(int i=1;i+1<argc;++i){const bool vertex=!std::strcmp(argv[i],"--captured-vs"),pixel=!std::strcmp(argv[i],"--captured-ps");if(!vertex&&!pixel)continue;const auto bytes=file(argv[++i]);
        for(bool direct:{false,true}){if((pixel&&direct)||(vertex&&direct&&!lmuqv::query_stereo_capabilities(f.d).vertexArrayIndex))continue;lmuqv::StereoShaderVariant converted;
            const auto okay=lmuqv::convert_stereo_shader(bytes.data(),bytes.size(),vertex?lmuqv::StereoShaderStage::vertex:lmuqv::StereoShaderStage::pixel,vertex?((1u<<0)|(1u<<4)):((1u<<0)|(1u<<3)),direct,converted,error);if(!okay)std::cerr<<error<<'\n';assert(okay);
            checkSamplers(bytes.data(),bytes.size(),converted);
            HRESULT hr=E_FAIL;if(vertex){ID3D11VertexShader* shader=nullptr;hr=f.d->CreateVertexShader(converted.bytecode.data(),converted.bytecode.size(),nullptr,&shader);drop(shader);if(!direct){code=compile(converted.geometrySource.c_str(),"gs_5_0");ID3D11GeometryShader* gs=nullptr;assert(SUCCEEDED(f.d->CreateGeometryShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&gs)));drop(gs);drop(code);}}
            else{ID3D11PixelShader* shader=nullptr;hr=f.d->CreatePixelShader(converted.bytecode.data(),converted.bytecode.size(),nullptr,&shader);drop(shader);}if(FAILED(hr))std::cerr<<"captured shader create 0x"<<std::hex<<unsigned(hr)<<std::dec<<'\n';assert(SUCCEEDED(hr));std::cout<<"Captured "<<(vertex?"VS":"PS")<<" converted and accepted by D3D11, paired CB registers="<<converted.eyeStride*2<<", direct="<<direct<<" (creation check, no game rendering equivalence claimed)\n";
        }
    }
    for(int i=1;i+1<argc;++i)if(!std::strcmp(argv[i],"--validate-captured-directory")){
        unsigned inspected=0,accepted=0,longPadding=0;std::map<std::string,unsigned> refusals;
        for(const auto& entry:std::filesystem::directory_iterator(argv[++i])){
            const auto name=entry.path().filename().string();const bool vertex=name.rfind("vs-",0)==0,pixel=name.rfind("ps-",0)==0;
            if(entry.path().extension()!=".dxbc"||(!vertex&&!pixel))continue;const auto bytes=file(entry.path().string().c_str());++inspected;longPadding+=(bytes.size()-20)%64>=56;
            lmuqv::StereoShaderVariant converted;const auto okay=lmuqv::convert_stereo_shader(bytes.data(),bytes.size(),vertex?lmuqv::StereoShaderStage::vertex:lmuqv::StereoShaderStage::pixel,vertex?((1u<<0)|(1u<<4)):((1u<<0)|(1u<<3)),false,converted,error);
            assert(error!="invalid DXBC checksum"&&error!="truncated container"&&error!="invalid DXBC header"&&error!="invalid DXBC chunk range");
            if(!okay){++refusals[error];continue;}
            checkSamplers(bytes.data(),bytes.size(),converted);
            HRESULT hr=E_FAIL;if(vertex){ID3D11VertexShader* shader=nullptr;hr=f.d->CreateVertexShader(converted.bytecode.data(),converted.bytecode.size(),nullptr,&shader);drop(shader);}
            else{ID3D11PixelShader* shader=nullptr;hr=f.d->CreatePixelShader(converted.bytecode.data(),converted.bytecode.size(),nullptr,&shader);drop(shader);}
            if(FAILED(hr))std::cerr<<"converted local program rejected: "<<name<<" hr=0x"<<std::hex<<unsigned(hr)<<std::dec<<'\n';assert(SUCCEEDED(hr));++accepted;
        }
        assert(inspected&&longPadding);std::cout<<"Local corpus: "<<inspected<<" valid checksums ("<<longPadding<<" with two-block padding), "<<accepted<<" converted programs accepted by D3D11; unsupported programs refused:\n";
        for(const auto& refusal:refusals)std::cout<<"  "<<refusal.second<<" "<<refusal.first<<'\n';
    }
    std::cout<<"Stereo shader conversion "<<(hardware?"hardware":"WARP")<<" passed; game integration remains disabled\n";
}
