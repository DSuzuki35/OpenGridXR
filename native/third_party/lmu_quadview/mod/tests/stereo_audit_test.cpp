#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../stereo_audit.hpp"
#include <d3dcompiler.h>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <string>
#include <thread>
#include <vector>
namespace {
template<class T> void drop(T*& p){if(p){p->Release();p=nullptr;}}
std::vector<std::string> messages;
struct GroupedNumbers:std::numpunct<char> {char do_thousands_sep()const override{return ',';}std::string do_grouping()const override{return "\3";}};
void log(const char* s){messages.emplace_back(s);}
bool contains(const char* s){for(const auto& m:messages)if(m.find(s)!=std::string::npos)return true;return false;}
ID3DBlob* compile(const char* source,const char* target){ID3DBlob *code=nullptr,*errors=nullptr;const auto hr=D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,"main",target,0,0,&code,&errors);if(FAILED(hr)&&errors)std::cerr<<static_cast<char*>(errors->GetBufferPointer());drop(errors);assert(SUCCEEDED(hr));return code;}
std::vector<std::filesystem::path> files(const std::filesystem::path& path){std::vector<std::filesystem::path> result;for(const auto& file:std::filesystem::directory_iterator(path))if(file.path().extension()==".json")result.push_back(file.path());return result;}
std::string read(const std::filesystem::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
unsigned count(const std::string& s,const char* needle){unsigned n=0;size_t p=0;while((p=s.find(needle,p))!=std::string::npos){++n;p+=std::strlen(needle);}return n;}
}
int main(int argc,char** argv){
    using namespace lmuqv;
    std::locale::global(std::locale(std::locale::classic(),new GroupedNumbers));
    const bool quiet=argc>1&&!std::strcmp(argv[1],"--diagnostics-off");if(quiet)SetEnvironmentVariableA("LMU_QV_DIAGNOSTICS","0");
    const bool hardware=argc>1&&!std::strcmp(argv[1],"--hardware");
    bool exportShaders=false;for(int i=1;i<argc;++i)exportShaders|=!std::strcmp(argv[i],"--shader-export");
    bool proofRequested=false;for(int i=1;i<argc;++i)proofRequested|=!std::strcmp(argv[i],"--pair-proof");
    SetEnvironmentVariableA("LMU_QV_STEREO_SHADER_CAPTURE",exportShaders?"1":"0");
    SetEnvironmentVariableA("LMU_QV_STEREO_DRAW_PROOF",proofRequested?"1":"0");
    wchar_t executable[32768]{};assert(GetModuleFileNameW(nullptr,executable,32768));
    const auto directory=std::filesystem::path(executable).parent_path()/("stereo-audit-test-"+std::to_string(GetTickCount64()));assert(std::filesystem::create_directory(directory));
    assert(!stereo_audit_prepare(false,directory.c_str(),log));
    assert(!stereo_audit_configure(nullptr,false,directory.c_str(),log)&&!stereo_audit_enabled());
    if(!quiet){assert(!stereo_audit_configure(nullptr,true,directory.c_str(),log));assert(stereo_audit_prepare(true,directory.c_str(),log));}
    else assert(!stereo_audit_prepare(true,directory.c_str(),log));
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    if(quiet){assert(!stereo_audit_configure(context,true,directory.c_str(),log));stereo_audit_frame(true);{StereoAuditScope pass(StereoAuditPass::focus0);}stereo_audit_frame_end();stereo_audit_stop();assert(files(directory).empty()&&messages.empty());drop(context);drop(device);std::cout<<"Stereo audit diagnostics-off: no hooks/capture/files\n";return 0;}
    assert(stereo_audit_enabled()&&contains("early device interception active"));
    auto* code=compile("cbuffer C:register(b2){float4 offset;} float4 main(float2 p:POSITION):SV_Position{return float4(p+offset.xy,0.5,1);}","vs_5_0");
    ID3D11VertexShader* vs=nullptr;assert(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vs)));
    ID3D11InputLayout* layout=nullptr;D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};assert(SUCCEEDED(device->CreateInputLayout(&element,1,code->GetBufferPointer(),code->GetBufferSize(),&layout)));drop(code);
    code=compile("float4 main():SV_Target{return float4(0,1,0,1);}","ps_5_0");ID3D11PixelShader* ps=nullptr;assert(SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&ps)));drop(code);
    // xrCreateSession happens after the game's mesh layouts and some shader
    // objects exist. Confirming the same device must preserve that metadata.
    assert(stereo_audit_configure(context,true,directory.c_str(),log)&&contains("preserving early shader/layout definitions"));
    ID3D11Texture2D* texture=nullptr;D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=8;td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    assert(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&texture)));ID3D11RenderTargetView* rtv=nullptr;assert(SUCCEEDED(device->CreateRenderTargetView(texture,nullptr,&rtv)));
    ID3D11Buffer *vb=nullptr,*ib=nullptr,*cb=nullptr;const float vertices[]={-1,-1,-1,3,3,-1};const UINT indices[]={0,1,2};
    auto buffer=[&](UINT bytes,UINT bind,const void* src,ID3D11Buffer** out){D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.BindFlags=bind;D3D11_SUBRESOURCE_DATA data{src,0,0};assert(SUCCEEDED(device->CreateBuffer(&d,src?&data:nullptr,out)));};
    buffer(sizeof(vertices),D3D11_BIND_VERTEX_BUFFER,vertices,&vb);buffer(sizeof(indices),D3D11_BIND_INDEX_BUFFER,indices,&ib);buffer(16,D3D11_BIND_CONSTANT_BUFFER,nullptr,&cb);
    context->OMSetRenderTargets(1,&rtv,nullptr);D3D11_VIEWPORT viewport{0,0,8,8,0,1};context->RSSetViewports(1,&viewport);
    const UINT stride=8,offset=0;context->IASetVertexBuffers(0,1,&vb,&stride,&offset);context->IASetIndexBuffer(ib,DXGI_FORMAT_R32_UINT,0);context->IASetInputLayout(layout);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs,nullptr,0);context->PSSetShader(ps,nullptr,0);context->VSSetConstantBuffers(2,1,&cb);const float zero[4]{};context->UpdateSubresource(cb,0,nullptr,zero,0,0);
    int nativeMeshIdentity=0;
    auto draw=[&]{const auto native=stereo_audit_native_prepare_begin(&nativeMeshIdentity);context->UpdateSubresource(cb,0,nullptr,zero,0,0);stereo_audit_native_prepare_end(native);context->DrawIndexed(3,0,0);};
    draw();assert(files(directory).empty()); // All ordinary draws are forwarded.
    ID3D11Query *query=nullptr,*timer=nullptr;D3D11_QUERY_DESC queryDesc{D3D11_QUERY_OCCLUSION,0};assert(SUCCEEDED(device->CreateQuery(&queryDesc,&query)));
    queryDesc.Query=D3D11_QUERY_TIMESTAMP_DISJOINT;assert(SUCCEEDED(device->CreateQuery(&queryDesc,&timer)));context->Begin(timer);context->Begin(query);
    for(unsigned frame=0;frame<2;++frame){
        stereo_audit_frame(true); // Holding the key must not stop/restart.
        for(unsigned pass=0;pass<4;++pass){StereoAuditScope scope{StereoAuditPass(pass)};StereoAuditGroup group(7);draw();}
        stereo_audit_frame_end();
        if(frame==0)context->End(query);
    }
    assert(contains("stereo_audit complete")&&files(directory).size()==1);
    auto first=read(files(directory)[0]);assert(count(first,"\"kind\":")==8&&count(first,"\"sha256\":\"")==2);
    assert(first.find("\"scene_frames\":2")!=std::string::npos&&first.find("\"rendering\":\"legacy\"")!=std::string::npos);
    assert(first.find("\"name\":\"offset\"")!=std::string::npos&&first.find("\"group\":7")!=std::string::npos&&first.find("\"layout_known\":1")!=std::string::npos);
    assert(first.find("\"early_device\":1")!=std::string::npos&&first.find("\"shader_definitions_dropped\":0")!=std::string::npos);
    // Timing-only and timing+occlusion scopes must be distinguished, including
    // scopes begun before a capture. Only the former permits private replay.
    assert(count(first,"\"native_mesh\":1")==8&&count(first,"\"active_queries\":2")==4&&count(first,"\"active_queries\":1")==4);
    assert(count(first,"\"active_query_mask\":10")==4&&count(first,"\"active_query_mask\":8")==4);context->End(timer);drop(timer);
    assert(first.find(proofRequested?"\"stereo_pair_proof\":{\"requested\":1":"\"stereo_pair_proof\":{\"requested\":0")!=std::string::npos);
    if(proofRequested)assert(first.find("\"status\":\"inconclusive\"")!=std::string::npos&&first.find("no eligible draw with safe query state")!=std::string::npos);drop(query);
    assert(count(first,"\"bytecode_file\":")==unsigned(exportShaders?2:0));
    unsigned bytecodeFiles=0;if(std::filesystem::exists(directory/L"stereo-shaders"))for(const auto& file:std::filesystem::directory_iterator(directory/L"stereo-shaders")){
        const auto data=read(file.path());assert(data.size()>4&&data.substr(0,4)=="DXBC");++bytecodeFiles;}
    assert(bytecodeFiles==unsigned(exportShaders?2:0));
    // Reproduce cache exhaustion using >1,024 real, distinct programs. Identical
    // CreateShader calls may return the same object, so merely repeating them
    // would not exercise the old capacity bug on every D3D11 implementation.
    std::vector<ID3D11PixelShader*> extra;
    for(unsigned i=0;i<1100;++i){
        const auto source="float4 main():SV_Target{return float4("+std::to_string(i+1)+".0/2048.0,1,0,1);}";
        code=compile(source.c_str(),"ps_5_0");ID3D11PixelShader* shader=nullptr;
        assert(SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader)));extra.push_back(shader);drop(code);
    }
    // Create another object/program request containing the same DXBC. It must
    // not consume an extra unique-program slot (objects may be driver-deduped).
    code=compile("float4 main():SV_Target{return float4(1100.0/2048.0,1,0,1);}","ps_5_0");ID3D11PixelShader* duplicate=nullptr;
    assert(SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&duplicate)));drop(code);
    stereo_audit_frame(false);stereo_audit_frame(true);
    context->PSSetShader(duplicate,nullptr,0);
    {StereoAuditScope pass(StereoAuditPass::focus0);for(unsigned i=0;i<4100;++i)context->DrawIndexed(3,0,0);}
    context->PSSetShader(ps,nullptr,0);
    stereo_audit_frame_end();stereo_audit_frame(false);stereo_audit_frame(true); // Save the bounded capture early.
    assert(files(directory).size()==2);bool bounded=false;
    for(const auto& file:files(directory)){const auto data=read(file);if(data.find("\"reason\":\"hotkey\"")!=std::string::npos){assert(count(data,"\"kind\":")==4096&&data.find("\"dropped\":4")!=std::string::npos);
        assert(data.find("\"shader_definitions_dropped\":0")!=std::string::npos&&data.find("\"unique_programs\":1102")!=std::string::npos&&count(data,"\"sha256\":\"")==2);bounded=true;}}
    assert(bounded);
    // A normal close on another thread saves stable buffered records, without
    // D3D context calls there; hook/resource cleanup waits for the open scope.
    stereo_audit_frame(false);stereo_audit_frame(true);
    {StereoAuditScope scope(StereoAuditPass::focus0);draw();std::thread close([]{stereo_audit_stop();});close.join();assert(!stereo_audit_enabled());}
    assert(files(directory).size()==3&&contains("cleanup deferred"));draw();
    // Confirm capture hooks never changed the resulting image.
    auto readDesc=td;readDesc.BindFlags=0;readDesc.Usage=D3D11_USAGE_STAGING;readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Texture2D* staging=nullptr;assert(SUCCEEDED(device->CreateTexture2D(&readDesc,nullptr,&staging)));
    context->CopyResource(staging,texture);D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&map)));uint32_t pixel=0;std::memcpy(&pixel,static_cast<const char*>(map.pData)+4*map.RowPitch+4*4,4);assert(pixel==0xff00ff00u);context->Unmap(staging,0);drop(staging);
    stereo_audit_stop();context->ClearState();for(auto*& shader:extra)drop(shader);drop(duplicate);drop(cb);drop(ib);drop(vb);drop(rtv);drop(texture);drop(layout);drop(ps);drop(vs);drop(context);drop(device);
    std::cout<<"Stereo draw audit "<<(hardware?"hardware":"WARP")<<" passed: early device capture, preserved layouts, >1024 programs, deduplicated/used-only definitions, opt-in shader export, transparent draws, reflection/SHA, CB updates, frame limits, held key, file saving, pixel readback and cross-thread deferred cleanup\n";
}
