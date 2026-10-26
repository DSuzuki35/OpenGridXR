#include "image_processing.hpp"
#include <wrl/client.h>
#include <d3dcompiler.h>
#include <d3d11_1.h>
#include <d3d11sdklayers.h>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
static void require(bool yes, const char *message) { if (!yes) throw std::runtime_error(message); }
static void checked(HRESULT hr, const char *message) { require(SUCCEEDED(hr), message); }

struct Fixture {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> fill;
    ComPtr<ID3D11Buffer> constants;
    ~Fixture() {
        ComPtr<ID3D11InfoQueue> info;
        if(SUCCEEDED(device.As(&info))) for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {
            SIZE_T size=0; info->GetMessage(i,nullptr,&size);
            std::vector<char> bytes(size);
            auto message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data()); info->GetMessage(i,message,&size);
            if(message->Severity<=D3D11_MESSAGE_SEVERITY_WARNING) std::cerr<<message->pDescription<<'\n';
        }
    }
    Fixture(D3D_DRIVER_TYPE driver, IDXGIAdapter *selected=nullptr) {
        D3D_FEATURE_LEVEL level;
        const D3D_FEATURE_LEVEL requested[]{D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        auto hr=D3D11CreateDevice(selected, driver, nullptr, D3D11_CREATE_DEVICE_DEBUG, requested, 2,
            D3D11_SDK_VERSION, &device, &level, &context);
        if(hr==DXGI_ERROR_SDK_COMPONENT_MISSING)
            hr=D3D11CreateDevice(selected,driver,nullptr,0,requested,2,D3D11_SDK_VERSION,&device,&level,&context);
        checked(hr,"create GPU test device");
        ComPtr<IDXGIDevice> dx; ComPtr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC adapterDesc{};
        device.As(&dx); dx->GetAdapter(&adapter); adapter->GetDesc(&adapterDesc);
        std::wcout<<adapterDesc.Description<<L'\n';
        // Generates a soft vertical edge with nonopaque alpha. Alternating MSAA
        // samples exercise actual resolve, not only identical clear samples.
        const char *shader = R"(
cbuffer C : register(b0) { uint pattern; uint sampleVary; float2 padding; };
float4 vs(uint i : SV_VertexID) : SV_Position {
    return float4(i == 1 ? 3 : -1, i == 2 ? -3 : 1, 0, 1);
}
float4 ps(float4 p : SV_Position, uint sample : SV_SampleIndex) : SV_Target {
    float v = pattern == 1 ? .5 : (p.x < 8 ? .25 : .75);
    if (sampleVary != 0) v += (sample % 2 == 0 ? -.04 : .04);
    return pattern == 2 ? float4(.8,.2,.1,.4) : float4(v,v,v,.4);
})";
        ComPtr<ID3DBlob> code, error;
        checked(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "vs", "vs_5_0",
            0, 0, &code, &error), "compile fixture vertex shader");
        checked(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(),
            nullptr, &vertex), "fixture vertex shader");
        code.Reset();
        checked(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "ps", "ps_5_0",
            0, 0, &code, &error), "compile fixture pixel shader");
        checked(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(),
            nullptr, &fill), "fixture pixel shader");
        D3D11_BUFFER_DESC cb{}; cb.ByteWidth=16; cb.Usage=D3D11_USAGE_DEFAULT;
        cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        checked(device->CreateBuffer(&cb,nullptr,&constants), "fixture constants");
    }
    ComPtr<ID3D11Texture2D> texture(DXGI_FORMAT format, unsigned samples, unsigned pattern) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width=d.Height=16; d.ArraySize=2; d.MipLevels=1;
        d.Format=(format==DXGI_FORMAT_B8G8R8A8_UNORM || format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
            ? DXGI_FORMAT_B8G8R8A8_TYPELESS : DXGI_FORMAT_R8G8B8A8_TYPELESS;
        d.SampleDesc.Count=samples;
        d.Usage=D3D11_USAGE_DEFAULT; d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> image;
        checked(device->CreateTexture2D(&d,nullptr,&image), "typeless array test image");
        context->ClearState();
        for(unsigned slice=0;slice<2;++slice) {
            D3D11_RENDER_TARGET_VIEW_DESC r{}; r.Format=format;
            if(samples==1) { r.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                r.Texture2DArray.FirstArraySlice=slice; r.Texture2DArray.ArraySize=1; }
            else { r.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
                r.Texture2DMSArray.FirstArraySlice=slice; r.Texture2DMSArray.ArraySize=1; }
            ComPtr<ID3D11RenderTargetView> target;
            checked(device->CreateRenderTargetView(image.Get(),&r,&target), "test array RTV");
            D3D11_VIEWPORT vp{0,0,16,16,0,1};
            context->RSSetViewports(1,&vp);
            context->OMSetRenderTargets(1,target.GetAddressOf(),nullptr);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(vertex.Get(),nullptr,0); context->PSSetShader(fill.Get(),nullptr,0);
            unsigned cb[4]{pattern,samples>1 ? 1u : 0u,0,0};
            context->UpdateSubresource(constants.Get(),0,nullptr,cb,0,0);
            context->PSSetConstantBuffers(0,1,constants.GetAddressOf());
            context->Draw(3,0);
        }
        return image;
    }
    std::vector<unsigned> pixels(ID3D11Texture2D *image, DXGI_FORMAT format) {
        context->OMSetRenderTargets(0,nullptr,nullptr);
        D3D11_TEXTURE2D_DESC d{}; image->GetDesc(&d);
        ComPtr<ID3D11Texture2D> resolved, staging, copy;
        // Resolve an immutable snapshot. In particular repeated direct resolves
        // on the Iris Xe driver can discard an MSAA array slice's readback data.
        auto copied=d; copied.BindFlags=D3D11_BIND_RENDER_TARGET; copied.MiscFlags=0;
        checked(device->CreateTexture2D(&copied,nullptr,&copy),"readback immutable snapshot");
        context->CopyResource(copy.Get(),image);
        auto read=d; read.SampleDesc={1,0}; read.Format=format; read.BindFlags=0;
        checked(device->CreateTexture2D(&read,nullptr,&resolved), "readback resolve image");
        for(unsigned s=0;s<d.ArraySize;++s) {
            if(d.SampleDesc.Count>1) context->ResolveSubresource(resolved.Get(),s,copy.Get(),s,format);
            else context->CopySubresourceRegion(resolved.Get(),s,0,0,0,copy.Get(),s,nullptr);
        }
        read.Usage=D3D11_USAGE_STAGING; read.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        checked(device->CreateTexture2D(&read,nullptr,&staging), "readback staging");
        context->CopyResource(staging.Get(),resolved.Get());
        std::vector<unsigned> result;
        for(unsigned s=0;s<d.ArraySize;++s) {
            D3D11_MAPPED_SUBRESOURCE m{};
            checked(context->Map(staging.Get(),s,D3D11_MAP_READ,0,&m), "readback mapping");
            for(unsigned y=0;y<d.Height;++y) {
                auto row=reinterpret_cast<const unsigned*>(static_cast<const char*>(m.pData)+y*m.RowPitch);
                result.insert(result.end(),row,row+d.Width);
            }
            context->Unmap(staging.Get(),s);
        }
        return result;
    }
    std::vector<unsigned> samples(ID3D11Texture2D *image) {
        // Read actual MSAA sample values through a shader; a resolved comparison
        // alone would miss accidental flattening of untouched pixels' samples.
        D3D11_TEXTURE2D_DESC d{}; image->GetDesc(&d);
        require(d.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS && d.SampleDesc.Count==4,
            "per-sample fixture format");
        const char *source=R"(
Texture2DMSArray<float4,4> inputImage : register(t0);
RWStructuredBuffer<uint> outputPixels : register(u0);
[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID) {
    float4 c=inputImage.Load(int3(p.xy,p.z/4),p.z%4);
    uint4 b=(uint4)round(saturate(c)*255);
    outputPixels[(p.z*16+p.y)*16+p.x]=b.x|(b.y<<8)|(b.z<<16)|(b.w<<24);
})";
        ComPtr<ID3DBlob> code,errors;
        checked(D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&code,&errors),
            "compile MSAA sample readback");
        ComPtr<ID3D11ComputeShader> shader;
        checked(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader),
            "MSAA sample readback shader");
        D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY; v.Texture2DMSArray.ArraySize=2;
        ComPtr<ID3D11ShaderResourceView> srv;
        checked(device->CreateShaderResourceView(image,&v,&srv),"MSAA sample readback SRV");
        D3D11_BUFFER_DESC b{}; b.ByteWidth=16*16*2*4*sizeof(unsigned); b.Usage=D3D11_USAGE_DEFAULT;
        b.BindFlags=D3D11_BIND_UNORDERED_ACCESS; b.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        b.StructureByteStride=sizeof(unsigned);
        ComPtr<ID3D11Buffer> gpu,cpu;
        checked(device->CreateBuffer(&b,nullptr,&gpu),"MSAA sample output");
        ComPtr<ID3D11UnorderedAccessView> uav;
        checked(device->CreateUnorderedAccessView(gpu.Get(),nullptr,&uav),"MSAA sample output UAV");
        context->ClearState();
        context->CSSetShader(shader.Get(),nullptr,0); context->CSSetShaderResources(0,1,srv.GetAddressOf());
        context->CSSetUnorderedAccessViews(0,1,uav.GetAddressOf(),nullptr); context->Dispatch(2,2,8);
        context->ClearState();
        b.Usage=D3D11_USAGE_STAGING; b.BindFlags=0; b.MiscFlags=0; b.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        checked(device->CreateBuffer(&b,nullptr,&cpu),"MSAA sample staging");
        context->CopyResource(cpu.Get(),gpu.Get());
        D3D11_MAPPED_SUBRESOURCE m{};
        checked(context->Map(cpu.Get(),0,D3D11_MAP_READ,0,&m),"MSAA sample readback");
        auto values=static_cast<const unsigned*>(m.pData);
        std::vector<unsigned> result(values,values+16*16*8);
        context->Unmap(cpu.Get(),0);
        return result;
    }
};
static int channel(unsigned p) { return int(p&255); }
static void stateRestoration(Fixture &f) {
    auto image=f.texture(DXGI_FORMAT_R8G8B8A8_UNORM,1,0);
    auto other=f.texture(DXGI_FORMAT_R8G8B8A8_UNORM,1,1);
    f.context->ClearState();
    D3D11_RENDER_TARGET_VIEW_DESC rd{}; rd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY; rd.Texture2DArray.ArraySize=1;
    ComPtr<ID3D11RenderTargetView> rtv;
    checked(f.device->CreateRenderTargetView(image.Get(),&rd,&rtv),"state fixture RTV");
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format=rd.Format;
    sv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY; sv.Texture2DArray.MipLevels=1;
    sv.Texture2DArray.ArraySize=2;
    ComPtr<ID3D11ShaderResourceView> srv;
    checked(f.device->CreateShaderResourceView(other.Get(),&sv,&srv),"state fixture SRV");
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth=512; bd.Usage=D3D11_USAGE_DEFAULT;
    bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> cb;
    checked(f.device->CreateBuffer(&bd,nullptr,&cb),"state fixture constants");
    bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS; bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride=4;
    ComPtr<ID3D11Buffer> uavBuffer;
    checked(f.device->CreateBuffer(&bd,nullptr,&uavBuffer),"state fixture UAV buffer");
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements=128; ud.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_APPEND;
    ComPtr<ID3D11UnorderedAccessView> uav;
    checked(f.device->CreateUnorderedAccessView(uavBuffer.Get(),&ud,&uav),"state fixture UAV");
    D3D11_BLEND_DESC blendDesc{}; blendDesc.RenderTarget[0].BlendEnable=TRUE;
    blendDesc.RenderTarget[0].SrcBlend=D3D11_BLEND_BLEND_FACTOR;
    blendDesc.RenderTarget[0].DestBlend=D3D11_BLEND_INV_BLEND_FACTOR;
    blendDesc.RenderTarget[0].BlendOp=blendDesc.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    ComPtr<ID3D11BlendState> blend;
    checked(f.device->CreateBlendState(&blendDesc,&blend),"state fixture blend");
    D3D11_DEPTH_STENCIL_DESC depthDesc{}; depthDesc.DepthEnable=TRUE;
    depthDesc.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; depthDesc.DepthFunc=D3D11_COMPARISON_GREATER;
    ComPtr<ID3D11DepthStencilState> depth;
    checked(f.device->CreateDepthStencilState(&depthDesc,&depth),"state fixture depth");
    D3D11_RASTERIZER_DESC rasterDesc{}; rasterDesc.FillMode=D3D11_FILL_WIREFRAME;
    rasterDesc.CullMode=D3D11_CULL_FRONT; rasterDesc.ScissorEnable=TRUE;
    ComPtr<ID3D11RasterizerState> raster;
    checked(f.device->CreateRasterizerState(&rasterDesc,&raster),"state fixture rasterizer");
    D3D11_SAMPLER_DESC samplerDesc{}; samplerDesc.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU=samplerDesc.AddressV=samplerDesc.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.MaxLOD=D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;
    checked(f.device->CreateSamplerState(&samplerDesc,&sampler),"state fixture sampler");
    D3D11_QUERY_DESC query{D3D11_QUERY_OCCLUSION_PREDICATE,0};
    ComPtr<ID3D11Predicate> predicate;
    checked(f.device->CreatePredicate(&query,&predicate),"state fixture predication");
    f.context->Begin(predicate.Get()); f.context->End(predicate.Get());
    f.context->SetPredication(predicate.Get(),TRUE); // Empty query suppresses draws unless disabled.
    float factors[4]{.2f,.3f,.4f,.5f};
    f.context->OMSetBlendState(blend.Get(),factors,0x55aa55aa);
    f.context->OMSetDepthStencilState(depth.Get(),37);
    UINT initialCount=17;
    f.context->OMSetRenderTargetsAndUnorderedAccessViews(1,rtv.GetAddressOf(),nullptr,1,1,
        uav.GetAddressOf(),&initialCount);
    f.context->RSSetState(raster.Get());
    D3D11_VIEWPORT viewports[2]{{2,3,7,8,.2f,.8f},{1,2,8,9,.1f,.7f}};
    D3D11_RECT scissors[2]{{1,2,5,8},{2,3,9,10}};
    f.context->RSSetViewports(2,viewports); f.context->RSSetScissorRects(2,scissors);
    f.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
    f.context->VSSetShader(f.vertex.Get(),nullptr,0); f.context->PSSetShader(f.fill.Get(),nullptr,0);
    f.context->PSSetShaderResources(0,1,srv.GetAddressOf()); f.context->PSSetSamplers(0,1,sampler.GetAddressOf());
    ComPtr<ID3D11DeviceContext1> context1;
    checked(f.context.As(&context1),"D3D11.1 constant-buffer range test");
    UINT first=16,count=16;
    context1->PSSetConstantBuffers1(0,1,cb.GetAddressOf(),&first,&count);
    og::ImageProcessor processor(f.device.Get(),f.context.Get());
    og::SharpeningSettings settings; settings.mode=og::SharpeningMode::Luma;
    require(processor.process(image.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,{{0,0,0,16,16}},settings).status==
        og::ImageProcessStatus::Applied,"process under unusual game state");
    ComPtr<ID3D11Buffer> actualCb; UINT actualFirst{},actualCount{};
    context1->PSGetConstantBuffers1(0,1,&actualCb,&actualFirst,&actualCount);
    require(actualCb.Get()==cb.Get() && actualFirst==first && actualCount==count,
        "constant buffer binding AND D3D11.1 range restored");
    ComPtr<ID3D11RenderTargetView> actualTarget;
    f.context->OMGetRenderTargets(1,&actualTarget,nullptr);
    require(actualTarget.Get()==rtv.Get(),"render target restored");
    ComPtr<ID3D11UnorderedAccessView> actualUav;
    f.context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,1,1,&actualUav);
    require(actualUav.Get()==uav.Get(),"OM UAV binding restored");
    ComPtr<ID3D11BlendState> actualBlend; float actualFactors[4]{}; UINT mask{};
    f.context->OMGetBlendState(&actualBlend,actualFactors,&mask);
    require(actualBlend.Get()==blend.Get() && mask==0x55aa55aa &&
        memcmp(factors,actualFactors,sizeof(factors))==0,"blend state and factors restored");
    ComPtr<ID3D11DepthStencilState> actualDepth; UINT stencil{};
    f.context->OMGetDepthStencilState(&actualDepth,&stencil);
    require(actualDepth.Get()==depth.Get() && stencil==37,"depth and stencil state restored");
    ComPtr<ID3D11RasterizerState> actualRaster; f.context->RSGetState(&actualRaster);
    require(actualRaster.Get()==raster.Get(),"rasterizer restored");
    D3D11_VIEWPORT actualViewports[2]{}; UINT viewportCount=2;
    D3D11_RECT actualScissors[2]{}; UINT scissorCount=2;
    f.context->RSGetViewports(&viewportCount,actualViewports);
    f.context->RSGetScissorRects(&scissorCount,actualScissors);
    require(viewportCount==2 && memcmp(viewports,actualViewports,sizeof(viewports))==0 &&
        scissorCount==2 && memcmp(scissors,actualScissors,sizeof(scissors))==0,"viewports and scissors restored");
    D3D11_PRIMITIVE_TOPOLOGY topology;
    f.context->IAGetPrimitiveTopology(&topology);
    require(topology==D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP,"input topology restored");
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps;
    f.context->VSGetShader(&vs,nullptr,nullptr); f.context->PSGetShader(&ps,nullptr,nullptr);
    require(vs.Get()==f.vertex.Get() && ps.Get()==f.fill.Get(),"game shaders restored");
    ComPtr<ID3D11ShaderResourceView> actualSrv; ComPtr<ID3D11SamplerState> actualSampler;
    f.context->PSGetShaderResources(0,1,&actualSrv); f.context->PSGetSamplers(0,1,&actualSampler);
    require(actualSrv.Get()==srv.Get() && actualSampler.Get()==sampler.Get(),"game texture and sampler restored");
    ComPtr<ID3D11Predicate> actualPredicate; BOOL predicateValue{};
    f.context->GetPredication(&actualPredicate,&predicateValue);
    require(actualPredicate.Get()==predicate.Get() && predicateValue==TRUE,"game predication restored");
    f.context->SetPredication(nullptr,FALSE);
    auto pixels=f.pixels(image.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);
    require(channel(pixels[8*16+7])<64,"game predicate did not suppress sharpening");
    f.context->ClearState();
    D3D11_BUFFER_DESC stagingDesc{}; stagingDesc.ByteWidth=4; stagingDesc.Usage=D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> counter;
    checked(f.device->CreateBuffer(&stagingDesc,nullptr,&counter),"UAV counter readback buffer");
    f.context->CopyStructureCount(counter.Get(),0,uav.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    checked(f.context->Map(counter.Get(),0,D3D11_MAP_READ,0,&mapped),"UAV counter readback");
    auto preservedCount=*static_cast<UINT*>(mapped.pData); f.context->Unmap(counter.Get(),0);
    require(preservedCount==17,"append UAV counter preserved");
}
static void visorColor(Fixture &f) {
    og::ImageProcessor processor(f.device.Get(),f.context.Get());
    const std::vector<og::ImageRegion> region{{1,4,3,8,10}};
    og::SharpeningSettings off;
    og::SceneColorSettings neutral{true,100,100,100};
    auto bypass=f.texture(DXGI_FORMAT_R8G8B8A8_UNORM,1,2);
    auto initial=f.pixels(bypass.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);
    require(processor.process(bypass.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,region,off,neutral).status==og::ImageProcessStatus::Skipped,"neutral visor adds no image pass");
    require(f.pixels(bypass.Get(),DXGI_FORMAT_R8G8B8A8_UNORM)==initial,"neutral visor is pixel exact");
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB})
    for(auto mode:{og::SharpeningMode::Off,og::SharpeningMode::Luma,og::SharpeningMode::Cas})
    for(unsigned samples:{1u,4u}) {
        auto image=f.texture(format,samples,2);
        auto before=f.pixels(image.Get(),format);
        og::SharpeningSettings sharpen;sharpen.mode=mode;
        og::SceneColorSettings color{true,125,85,60};
        require(processor.process(image.Get(),format,region,sharpen,color).status==og::ImageProcessStatus::Applied,"visor changes game RGB with sharpening off or fused");
        auto after=f.pixels(image.Get(),format);
        for(size_t i=0;i<after.size();++i) {
            unsigned x=unsigned(i)%16,y=(unsigned(i)/16)%16,slice=unsigned(i)/256;
            if(slice!=1||x<4||x>=12||y<3||y>=13){require(after[i]==before[i],"visor grading excludes other eyes and non-projection pixels");continue;}
            float rgb[3];
            for(unsigned c=0;c<3;++c)rgb[c]=std::clamp((float((before[i]>>(c*8))&255)/255*.85f-.5f)*1.25f+.5f,0.f,1.f);
            float gray=rgb[0]*.2126f+rgb[1]*.7152f+rgb[2]*.0722f;
            for(unsigned c=0;c<3;++c){int expected=int(std::lround(std::clamp(gray+(rgb[c]-gray)*.6f,0.f,1.f)*255));require(std::abs(int((after[i]>>(c*8))&255)-expected)<=2,"visor RGB follows brightness contrast saturation in display space");}
            require((after[i]>>24)==(before[i]>>24),"visor grading preserves scene alpha");
        }
    }
    og::SceneColorSettings disabled{false,150,50,0};
    require(processor.process(bypass.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,region,off,disabled).status==og::ImageProcessStatus::Skipped,"disabled helmet ignores grade values");
    auto invalid=neutral;invalid.contrast=std::numeric_limits<float>::quiet_NaN();
    require(processor.process(bypass.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,region,off,invalid).status==og::ImageProcessStatus::Invalid,"nonfinite visor input rejected");
    invalid=neutral;invalid.saturation=151;
    require(processor.process(bypass.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,region,off,invalid).status==og::ImageProcessStatus::Invalid,"out-of-range visor input rejected");
}
static void run(Fixture &f) {
    visorColor(f);
    og::ImageProcessor processor(f.device.Get(),f.context.Get());
    const std::vector<og::ImageRegion> region{{1,4,3,8,10}};
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                     DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB})
    for(unsigned samples:{1u,4u})
    for(auto mode:{og::SharpeningMode::Luma,og::SharpeningMode::Cas}) {
        og::SharpeningSettings s; s.mode=mode;
        auto image=f.texture(format,samples,0);
        auto before=f.pixels(image.Get(),format);
        auto off=s; off.mode=og::SharpeningMode::Off;
        require(processor.process(image.Get(),format,region,off).status==og::ImageProcessStatus::Skipped,
            "off is a true bypass");
        off=s; off.strength=0;
        require(processor.process(image.Get(),format,region,off).status==og::ImageProcessStatus::Skipped,
            "zero strength is a true bypass");
        const auto bypass=f.pixels(image.Get(),format);
        if(bypass!=before) for(size_t i=0;i<before.size();++i) if(bypass[i]!=before[i]) {
            std::cerr<<"Mismatch index "<<i<<" before "<<std::hex<<before[i]<<" after "<<bypass[i]<<std::dec<<'\n';
            break;
        }
        require(bypass==before, "bypass preserves all pixels and samples");
        // A missing image pass or a pass modifying the wrong eye fails this check.
        auto result=processor.process(image.Get(),format,region,s);
        require(result.status==og::ImageProcessStatus::Applied,"enabled image pass executes");
        auto after=f.pixels(image.Get(),format);
        const unsigned dark=256+8*16+7, bright=dark+1;
        if(!(channel(after[dark])<channel(before[dark]) && channel(after[bright])>channel(before[bright])))
            std::cerr<<"edge before "<<channel(before[dark])<<","<<channel(before[bright])
                     <<" after "<<channel(after[dark])<<","<<channel(after[bright])<<'\n';
        require(channel(after[dark])<channel(before[dark]) &&
                channel(after[bright])>channel(before[bright]), "soft edge contrast increases");
        for(unsigned i=0;i<after.size();++i) {
            unsigned x=i%16,y=(i/16)%16, slice=i/256;
            if(slice!=1 || x<4 || x>=12 || y<3 || y>=13)
                require(after[i]==before[i],"other eye and pixels outside imageRect remain exact");
            require((after[i]>>24)==(before[i]>>24),"sharpening preserves alpha");
            if(mode==og::SharpeningMode::Luma)
                require(std::abs(channel(after[i])-channel(before[i]))<=10,"luma delta respects clamp");
        }
        auto flat=f.texture(format,samples,1);
        auto flatBefore=f.pixels(flat.Get(),format);
        require(processor.process(flat.Get(),format,region,s).status==og::ImageProcessStatus::Applied,
            "flat image processing executes");
        auto flatAfter=f.pixels(flat.Get(),format);
        for(unsigned i=0;i<flatAfter.size();++i)
            require(std::abs(channel(flatAfter[i])-channel(flatBefore[i]))<=1,
                "flat gray remains unchanged, including sRGB resolve and encode");
    }
    auto image=f.texture(DXGI_FORMAT_R8G8B8A8_UNORM,1,0);
    auto before=f.pixels(image.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);
    og::SharpeningSettings s; s.mode=og::SharpeningMode::Luma;
    require(processor.process(image.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,{{1,15,0,2,4}},s).status==
        og::ImageProcessStatus::Invalid,"invalid rectangle rejected before writing");
    for(const auto &invalid:std::vector<std::vector<og::ImageRegion>>{
        {{2,0,0,16,16}},{{0,0,0,0,16}},{{0,UINT32_MAX,0,2,16}},
        {{0,0,0,12,16},{0,8,0,8,16}}})
        require(processor.process(image.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,invalid,s).status==
            og::ImageProcessStatus::Invalid,"bad slice, empty, overflow and overlapping regions rejected");
    auto invalidSettings=s; invalidSettings.strength=std::numeric_limits<float>::quiet_NaN();
    require(processor.process(image.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,region,invalidSettings).status==
        og::ImageProcessStatus::Invalid,"nonfinite shader input rejected");
    require(processor.process(image.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,region,s).status==
        og::ImageProcessStatus::Unsupported,"unsupported HDR/format mismatch bypassed");
    require(f.pixels(image.Get(),DXGI_FORMAT_R8G8B8A8_UNORM)==before,"invalid input leaves image intact");
    for(auto mode:{og::SharpeningMode::Luma,og::SharpeningMode::Cas})
    for(unsigned pattern=1;pattern<=4;++pattern) {
        image=f.texture(DXGI_FORMAT_R8G8B8A8_UNORM,1,0);
        before=f.pixels(image.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);
        s.mode=mode; s.pattern=pattern; s.radius=6; s.strength=100;
        // Each half is uniform. Sampling across the eye/viewport boundary would
        // invent an edge and change these pixels, despite a valid in-bounds texture.
        require(processor.process(image.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,
            {{0,0,0,8,16},{0,8,0,8,16}},s).status==og::ImageProcessStatus::Applied,"split-eye processing");
        require(f.pixels(image.Get(),DXGI_FORMAT_R8G8B8A8_UNORM)==before,
            "all Luma patterns and CAS clamp sampling to each eye rectangle");
    }
    image=f.texture(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,4,1);
    before=f.pixels(image.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    D3D11_TEXTURE2D_DESC captureDesc{}; image->GetDesc(&captureDesc);
    captureDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; captureDesc.BindFlags=0;
    captureDesc.SampleDesc={1,0};
    ComPtr<ID3D11Texture2D> rawCapture;
    checked(f.device->CreateTexture2D(&captureDesc,nullptr,&rawCapture),"raw stream capture fixture");
    for(unsigned frame=0;frame<8;++frame) {
        // The stream consumer resolves first, as it does before processing in the layer.
        for(unsigned slice=0;slice<2;++slice)
            f.context->ResolveSubresource(rawCapture.Get(),slice,image.Get(),slice,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        s.mode=frame%2 ? og::SharpeningMode::Cas : og::SharpeningMode::Luma;
        require(processor.process(image.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,region,s).status==
            og::ImageProcessStatus::Applied,"process after raw stream MSAA resolve");
        const auto after=f.pixels(image.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        for(size_t i=0;i<after.size();++i)
            require(std::abs(channel(after[i])-channel(before[i]))<=1,
                "MSAA processing after capture preserves every eye over repeated frames");
    }
    image=f.texture(DXGI_FORMAT_R8G8B8A8_UNORM,4,0);
    auto beforeSamples=f.samples(image.Get());
    s.mode=og::SharpeningMode::Cas;
    require(processor.process(image.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,region,s).status==
        og::ImageProcessStatus::Applied,"MSAA sample preservation pass");
    auto afterSamples=f.samples(image.Get());
    require(beforeSamples[0]!=beforeSamples[256],"sample fixture has distinct coverage values");
    for(unsigned slice=0;slice<2;++slice) for(unsigned sample=0;sample<4;++sample)
    for(unsigned y=0;y<16;++y) for(unsigned x=0;x<16;++x) {
        const auto index=((slice*4+sample)*16+y)*16+x;
        if(slice==0 || x<4 || x>=12 || y<3 || y>=13)
            require(afterSamples[index]==beforeSamples[index],"untouched MSAA samples remain bit exact");
    }
}
int main() {
    try {
        Fixture warp(D3D_DRIVER_TYPE_WARP); run(warp); stateRestoration(warp);
        ComPtr<IDXGIFactory1> factory;
        checked(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"enumerate physical test adapters");
        unsigned tested=0;
        for(UINT i=0;;++i) {
            ComPtr<IDXGIAdapter1> adapter;
            if(factory->EnumAdapters1(i,&adapter)==DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 desc{}; adapter->GetDesc1(&desc);
            if(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            Fixture hardware(D3D_DRIVER_TYPE_UNKNOWN,adapter.Get()); run(hardware); stateRestoration(hardware);
            ++tested;
        }
        std::cout<<"Image processing pixel and context-state tests passed on WARP and "<<tested<<" physical adapters\n";
        return 0;
    } catch(const std::exception &e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
