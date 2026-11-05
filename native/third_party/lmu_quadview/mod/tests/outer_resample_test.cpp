// Offline real-D3D11 WARP tests. Never loads or accesses LMU.
#include "../outer_resample.hpp"
#include <d3dcompiler.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <limits>

namespace {
template<class T> struct Com {
    T* p{};
    ~Com(){if(p)p->Release();}
    Com()=default;
    Com(const Com&)=delete;
    Com& operator=(const Com&)=delete;
    T** put(){return &p;}
    T* operator->()const{return p;}
};
void require(bool okay,const char* text){if(!okay)throw std::runtime_error(text);}
void check(HRESULT hr,const char* text){require(SUCCEEDED(hr),text);}
struct Device {
    Com<ID3D11Device> d;
    Com<ID3D11DeviceContext> c;
    explicit Device(D3D_FEATURE_LEVEL requested=D3D_FEATURE_LEVEL_11_0){
        const D3D_FEATURE_LEVEL levels[]{requested};
        check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,1,
            D3D11_SDK_VERSION,d.put(),nullptr,c.put()),"WARP device");
    }
    ~Device(){if(c.p)c->ClearState();}
};
struct Texture {
    Com<ID3D11Texture2D> t;
    Com<ID3D11RenderTargetView> r;
    Texture(Device& dev,UINT width,UINT height,DXGI_FORMAT format,UINT bind,
            const void* data=nullptr,DXGI_FORMAT viewFormat=DXGI_FORMAT_UNKNOWN,
            UINT samples=1,UINT array=1){
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=width;desc.Height=height;desc.MipLevels=1;desc.ArraySize=array;
        desc.Format=format;desc.SampleDesc.Count=samples;desc.BindFlags=bind;
        D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=data;initial.SysMemPitch=width*4;
        check(dev.d->CreateTexture2D(&desc,data?&initial:nullptr,t.put()),"texture");
        if(bind&D3D11_BIND_RENDER_TARGET){
            D3D11_RENDER_TARGET_VIEW_DESC view{};view.Format=viewFormat==DXGI_FORMAT_UNKNOWN?format:viewFormat;
            view.ViewDimension=samples>1?D3D11_RTV_DIMENSION_TEXTURE2DMS:D3D11_RTV_DIMENSION_TEXTURE2D;
            if(array==1)check(dev.d->CreateRenderTargetView(t.p,&view,r.put()),"RTV");
        }
    }
};
void shader(const char* source,const char* entry,const char* profile,Com<ID3DBlob>& blob){
    Com<ID3DBlob> errors;
    HRESULT hr=D3DCompile(source,std::strlen(source),"resample-test",nullptr,nullptr,
        entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,blob.put(),errors.put());
    if(FAILED(hr)&&errors.p)std::cerr<<static_cast<const char*>(errors->GetBufferPointer());
    check(hr,"compile sentinel shader");
}

// Representative nondefault state in every shader stage and the IA/RS/OM/SO
// families. High slots ensure a slot-0-only saver could not pass this test.
struct State {
    Device& dev;
    Texture color0,color1,depth;
    Com<ID3D11DepthStencilView> dsv;
    Com<ID3D11Buffer> buffer,index,uavBuffer,so,csBuffer,omBuffer;
    Com<ID3D11ShaderResourceView> srv;
    Com<ID3D11UnorderedAccessView> csUav,omUav;
    Com<ID3D11DeviceContext1> context1;
    Com<ID3D11SamplerState> sampler;
    Com<ID3D11BlendState> blend;
    Com<ID3D11DepthStencilState> ds;
    Com<ID3D11RasterizerState> rs;
    Com<ID3D11InputLayout> layout;
    Com<ID3D11VertexShader> vs;
    Com<ID3D11PixelShader> ps;
    Com<ID3D11GeometryShader> gs;
    Com<ID3D11HullShader> hs;
    Com<ID3D11DomainShader> domain;
    Com<ID3D11ComputeShader> cs;
    Com<ID3D11Predicate> predicate;
    std::array<D3D11_VIEWPORT,2> vps{{{1,2,12,13,.1f,.9f},{3,4,14,15,.2f,.8f}}};
    std::array<D3D11_RECT,2> scissors{{{1,2,11,12},{3,4,13,14}}};
    FLOAT factors[4]{.1f,.2f,.3f,.4f};
    explicit State(Device& d):dev(d),
        color0(d,32,32,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_RENDER_TARGET),
        color1(d,32,32,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_RENDER_TARGET),
        depth(d,32,32,DXGI_FORMAT_D24_UNORM_S8_UINT,D3D11_BIND_DEPTH_STENCIL){
        check(d.d->CreateDepthStencilView(depth.t.p,nullptr,dsv.put()),"DSV");
        check(d.c->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(context1.put())),"Context1");
        D3D11_BUFFER_DESC b{};b.ByteWidth=1024;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(d.d->CreateBuffer(&b,nullptr,buffer.put()),"CB");
        b.BindFlags=D3D11_BIND_VERTEX_BUFFER|D3D11_BIND_INDEX_BUFFER;
        check(d.d->CreateBuffer(&b,nullptr,index.put()),"VB/IB");
        b.BindFlags=D3D11_BIND_STREAM_OUTPUT;
        check(d.d->CreateBuffer(&b,nullptr,so.put()),"SO");
        b.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        b.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;b.StructureByteStride=16;
        check(d.d->CreateBuffer(&b,nullptr,uavBuffer.put()),"UAV buffer");
        check(d.d->CreateShaderResourceView(uavBuffer.p,nullptr,srv.put()),"sentinel SRV");
        check(d.d->CreateBuffer(&b,nullptr,csBuffer.put()),"CS UAV buffer");
        check(d.d->CreateBuffer(&b,nullptr,omBuffer.put()),"OM UAV buffer");
        check(d.d->CreateUnorderedAccessView(csBuffer.p,nullptr,csUav.put()),"CS UAV");
        check(d.d->CreateUnorderedAccessView(omBuffer.p,nullptr,omUav.put()),"OM UAV");
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;s.MaxLOD=1;
        check(d.d->CreateSamplerState(&s,sampler.put()),"sampler");
        D3D11_BLEND_DESC bl{};bl.AlphaToCoverageEnable=TRUE;
        bl.RenderTarget[0].RenderTargetWriteMask=7;
        check(d.d->CreateBlendState(&bl,blend.put()),"blend");
        D3D11_DEPTH_STENCIL_DESC dp{};dp.DepthEnable=TRUE;dp.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
        dp.DepthFunc=D3D11_COMPARISON_GREATER;
        check(d.d->CreateDepthStencilState(&dp,ds.put()),"depth state");
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_WIREFRAME;
        raster.CullMode=D3D11_CULL_FRONT;raster.ScissorEnable=TRUE;raster.DepthClipEnable=TRUE;
        check(d.d->CreateRasterizerState(&raster,rs.put()),"raster");
        constexpr const char* code=R"(
struct V {float4 p:SV_Position;};
V VS(float4 p:POSITION){V v;v.p=p;return v;}
float4 PS(V v):SV_Target{return v.p;}
[maxvertexcount(1)] void GS(point V p[1],inout PointStream<V> s){s.Append(p[0]);}
struct T {float edges[3]:SV_TessFactor;float inside:SV_InsideTessFactor;};
T TC(InputPatch<V,3> p,uint id:SV_PrimitiveID){T t;t.edges[0]=t.edges[1]=t.edges[2]=1;t.inside=1;return t;}
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")]
[outputcontrolpoints(3)][patchconstantfunc("TC")]
V HS(InputPatch<V,3> p,uint i:SV_OutputControlPointID){return p[i];}
[domain("tri")] V DS(T t,float3 uvw:SV_DomainLocation,const OutputPatch<V,3> p){return p[0];}
[numthreads(1,1,1)] void CS(uint3 id:SV_DispatchThreadID){}
)";
        Com<ID3DBlob> v,p,g,h,dn,c;
        shader(code,"VS","vs_5_0",v);shader(code,"PS","ps_5_0",p);
        shader(code,"GS","gs_5_0",g);shader(code,"HS","hs_5_0",h);
        shader(code,"DS","ds_5_0",dn);shader(code,"CS","cs_5_0",c);
        check(d.d->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,vs.put()),"VS");
        check(d.d->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,ps.put()),"PS");
        check(d.d->CreateGeometryShader(g->GetBufferPointer(),g->GetBufferSize(),nullptr,gs.put()),"GS");
        check(d.d->CreateHullShader(h->GetBufferPointer(),h->GetBufferSize(),nullptr,hs.put()),"HS");
        check(d.d->CreateDomainShader(dn->GetBufferPointer(),dn->GetBufferSize(),nullptr,domain.put()),"DS");
        check(d.d->CreateComputeShader(c->GetBufferPointer(),c->GetBufferSize(),nullptr,cs.put()),"CS");
        D3D11_INPUT_ELEMENT_DESC input{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        check(d.d->CreateInputLayout(&input,1,v->GetBufferPointer(),v->GetBufferSize(),layout.put()),"layout");
        D3D11_QUERY_DESC q{D3D11_QUERY_OCCLUSION_PREDICATE,0};
        check(d.d->CreatePredicate(&q,predicate.put()),"predicate");
        d.c->Begin(predicate.p);d.c->End(predicate.p);
    }
    void bind(){
        auto* c=dev.c.p;c->ClearState();
        ID3D11RenderTargetView* r[]{color0.r.p,color1.r.p};
        ID3D11UnorderedAccessView* pixelUav=omUav.p;
        c->OMSetRenderTargetsAndUnorderedAccessViews(2,r,dsv.p,2,1,&pixelUav,nullptr);
        ID3D11UnorderedAccessView* computeUav=csUav.p;c->CSSetUnorderedAccessViews(3,1,&computeUav,nullptr);
        c->OMSetBlendState(blend.p,factors,0x13579bdf);c->OMSetDepthStencilState(ds.p,19);
        c->RSSetState(rs.p);c->RSSetViewports(2,vps.data());c->RSSetScissorRects(2,scissors.data());
        c->VSSetShader(vs.p,nullptr,0);c->PSSetShader(ps.p,nullptr,0);c->GSSetShader(gs.p,nullptr,0);
        c->HSSetShader(hs.p,nullptr,0);c->DSSetShader(domain.p,nullptr,0);c->CSSetShader(cs.p,nullptr,0);
        ID3D11Buffer* cb=buffer.p;ID3D11ShaderResourceView* resource=srv.p;ID3D11SamplerState* sm=sampler.p;
        const UINT firstConstant=16,numConstants=16;
#define SET_STAGE(stage) context1->stage##SetConstantBuffers1(7,1,&cb,&firstConstant,&numConstants);c->stage##SetShaderResources(9,1,&resource);c->stage##SetSamplers(3,1,&sm)
        SET_STAGE(VS);SET_STAGE(PS);SET_STAGE(GS);SET_STAGE(HS);SET_STAGE(DS);SET_STAGE(CS);
#undef SET_STAGE
        c->IASetInputLayout(layout.p);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
        UINT stride=32,offset=16;ID3D11Buffer* vb=index.p;c->IASetVertexBuffers(3,1,&vb,&stride,&offset);
        c->IASetIndexBuffer(index.p,DXGI_FORMAT_R16_UINT,8);
        // A separate SO buffer and a predicate must also survive our draw.
        UINT soOffset=32;ID3D11Buffer* output=so.p;c->SOSetTargets(1,&output,&soOffset);
        c->SetPredication(predicate.p,TRUE);
    }
    void verify(){
        auto* c=dev.c.p;
        Com<ID3D11VertexShader> av;Com<ID3D11PixelShader> ap;Com<ID3D11GeometryShader> ag;
        Com<ID3D11HullShader> ah;Com<ID3D11DomainShader> ad;Com<ID3D11ComputeShader> ac;
        c->VSGetShader(av.put(),nullptr,nullptr);c->PSGetShader(ap.put(),nullptr,nullptr);
        c->GSGetShader(ag.put(),nullptr,nullptr);c->HSGetShader(ah.put(),nullptr,nullptr);
        c->DSGetShader(ad.put(),nullptr,nullptr);c->CSGetShader(ac.put(),nullptr,nullptr);
        require(av.p==vs.p&&ap.p==ps.p&&ag.p==gs.p&&ah.p==hs.p&&ad.p==domain.p&&ac.p==cs.p,"shader state");
#define GET_STAGE(stage) {Com<ID3D11Buffer> b;Com<ID3D11ShaderResourceView> r;Com<ID3D11SamplerState> s;UINT first=0,count=0;context1->stage##GetConstantBuffers1(7,1,b.put(),&first,&count);c->stage##GetShaderResources(9,1,r.put());c->stage##GetSamplers(3,1,s.put());require(b.p==buffer.p&&r.p==srv.p&&s.p==sampler.p&&first==16&&count==16,#stage " bindings/CB ranges");}
        GET_STAGE(VS);GET_STAGE(PS);GET_STAGE(GS);GET_STAGE(HS);GET_STAGE(DS);GET_STAGE(CS);
#undef GET_STAGE
        Com<ID3D11InputLayout> al;Com<ID3D11Buffer> vb,ib,os;
        UINT stride=0,offset=0,io=0;DXGI_FORMAT fmt{};D3D11_PRIMITIVE_TOPOLOGY top{};
        c->IAGetInputLayout(al.put());c->IAGetVertexBuffers(3,1,vb.put(),&stride,&offset);
        c->IAGetIndexBuffer(ib.put(),&fmt,&io);c->IAGetPrimitiveTopology(&top);c->SOGetTargets(1,os.put());
        require(al.p==layout.p&&vb.p==index.p&&stride==32&&offset==16&&ib.p==index.p&&fmt==DXGI_FORMAT_R16_UINT&&io==8&&top==D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST&&os.p==so.p,"IA/SO state");
        Com<ID3D11RenderTargetView> r0,r1;ID3D11RenderTargetView* raw[2]{};Com<ID3D11DepthStencilView> dd;
        c->OMGetRenderTargets(2,raw,dd.put());r0.p=raw[0];r1.p=raw[1];
        require(r0.p==color0.r.p&&r1.p==color1.r.p&&dd.p==dsv.p,"OM targets");
        Com<ID3D11UnorderedAccessView> cu,pu;
        c->CSGetUnorderedAccessViews(3,1,cu.put());
        c->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,2,1,pu.put());
        require(cu.p==csUav.p&&pu.p==omUav.p,"CS/OM UAV state");
        Com<ID3D11BlendState> b;FLOAT f[4]{};UINT mask=0;c->OMGetBlendState(b.put(),f,&mask);
        Com<ID3D11DepthStencilState> z;UINT ref=0;c->OMGetDepthStencilState(z.put(),&ref);
        Com<ID3D11RasterizerState> raster;c->RSGetState(raster.put());
        require(b.p==blend.p&&std::memcmp(f,factors,sizeof(f))==0&&mask==0x13579bdf&&z.p==ds.p&&ref==19&&raster.p==rs.p,"OM/RS objects");
        std::array<D3D11_VIEWPORT,16> vp{};std::array<D3D11_RECT,16> sc{};UINT nv=16,ns=16;
        c->RSGetViewports(&nv,vp.data());c->RSGetScissorRects(&ns,sc.data());
        require(nv==2&&ns==2&&std::memcmp(vp.data(),vps.data(),sizeof(vps))==0&&std::memcmp(sc.data(),scissors.data(),sizeof(scissors))==0,"viewport/scissor state");
        Com<ID3D11Predicate> pr;BOOL pv=FALSE;c->GetPredication(pr.put(),&pv);
        require(pr.p==predicate.p&&pv==TRUE,"predication state");
    }
};

std::vector<unsigned char> read(Device& dev,ID3D11Texture2D* texture){
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.BindFlags=0;
    desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    Com<ID3D11Texture2D> staging;check(dev.d->CreateTexture2D(&desc,nullptr,staging.put()),"staging");
    dev.c->CopyResource(staging.p,texture);D3D11_MAPPED_SUBRESOURCE m{};
    check(dev.c->Map(staging.p,0,D3D11_MAP_READ,0,&m),"readback");
    std::vector<unsigned char> data(desc.Width*desc.Height*4);
    for(UINT y=0;y<desc.Height;++y)std::memcpy(data.data()+y*desc.Width*4,
        static_cast<const unsigned char*>(m.pData)+y*m.RowPitch,desc.Width*4);
    dev.c->Unmap(staging.p,0);return data;
}
void pixels(Device& dev,State& state,DXGI_FORMAT sourceFormat,DXGI_FORMAT destFormat,
            bool checker,int expected,float blurRadius=0){
    std::array<unsigned char,64> data{};
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){
        const unsigned value=checker?((x+y)%2?255:0):128;
        for(unsigned c=0;c<3;++c)data[(y*4+x)*4+c]=static_cast<unsigned char>(value);
        data[(y*4+x)*4+3]=173;
    }
    Texture src(dev,4,4,sourceFormat,D3D11_BIND_SHADER_RESOURCE,data.data());
    Texture dst(dev,2,2,destFormat,D3D11_BIND_RENDER_TARGET);
    lmuqv::OuterResample pass;state.bind();
    require(pass.draw(dev.c.p,src.t.p,dst.r.p,2,2,blurRadius),"valid resample draw");state.verify();
    const auto result=read(dev,dst.t.p);
    for(unsigned p=0;p<4;++p){
        for(unsigned c=0;c<3;++c)require(std::abs(int(result[p*4+c])-expected)<=2,"filtered color/gamma mismatch");
        require(std::abs(int(result[p*4+3])-173)<=1,"alpha gamma mismatch");
    }
}
void blur_pixels(Device& dev,State& state){
    for(unsigned scale:{1u,2u}){
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB}){
        const unsigned size=8*scale;
        std::vector<unsigned char> bytes(size*size*4);
        for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){
            for(unsigned c=0;c<3;++c)bytes[(y*size+x)*4+c]=(x/scale+y/scale)%2?255:0;
            bytes[(y*size+x)*4+3]=173;
        }
        Texture src(dev,size,size,format,D3D11_BIND_SHADER_RESOURCE,bytes.data());
        Texture dst(dev,8,8,format,D3D11_BIND_RENDER_TARGET);
        lmuqv::OuterResample pass;state.bind();
        require(pass.draw(dev.c.p,src.t.p,dst.r.p,8,8,0.f),"blur off draw");state.verify();
        const auto unblurred=read(dev,dst.t.p);
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)require(unblurred[(y*8+x)*4]==((x+y)%2?255:0),"blur off changed pixels");
        require(pass.draw(dev.c.p,src.t.p,dst.r.p,8,8,1.f),"blur draw");state.verify();
        const auto blurred=read(dev,dst.t.p);const int expected=format==DXGI_FORMAT_R8G8B8A8_UNORM?128:188;
        for(unsigned y=1;y<7;++y)for(unsigned x=1;x<7;++x){
            for(unsigned c=0;c<3;++c)require(std::abs(int(blurred[(y*8+x)*4+c])-expected)<=2,"blur failed checker smoothing / gamma");
            require(std::abs(int(blurred[(y*8+x)*4+3])-173)<=1,"blur changed alpha");
        }
        for(float radius:{-1.f,3.1f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
            require(!pass.draw(dev.c.p,src.t.p,dst.r.p,8,8,radius),"invalid blur accepted");state.verify();
            require(read(dev,dst.t.p)==blurred,"invalid blur altered destination");
        }
    }
    }
}
void padded_pixels(Device& dev,State& state){
    // Uneven 47% crop, including the fractional viewport from ceil render sizes.
    // Colored source quadrants must occupy the matching subrectangle, surrounded
    // by opaque black; repeating the draw must clear stale output/UI as well.
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB}){
        std::vector<unsigned char> bytes(8*8*4);
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){
            auto* p=&bytes[(y*8+x)*4];p[0]=x<4?64:192;p[1]=y<4?32:160;p[2]=96;p[3]=173;
        }
        Texture src(dev,8,8,format,D3D11_BIND_SHADER_RESOURCE,bytes.data());
        Texture dst(dev,20,20,format,D3D11_BIND_RENDER_TARGET);
        lmuqv::OuterResample pass;state.bind();
        const D3D11_VIEWPORT rect{2.5f,5.3f,15.f,9.4f,0,1};
        for(unsigned run=0;run<2;++run){
            const float white[4]{1,1,1,0};dev.c->ClearRenderTargetView(dst.r.p,white);
            require(pass.draw(dev.c.p,src.t.p,dst.r.p,20,20,0,&rect),"padded draw");state.verify();
            auto data=read(dev,dst.t.p);
            for(unsigned y=0;y<20;++y)for(unsigned x=0;x<20;++x){
                const auto* p=&data[(y*20+x)*4];
                if(x<2||x>18||y<5||y>14)require(p[0]==0&&p[1]==0&&p[2]==0&&p[3]==255,"padding not opaque black");
            }
            for(unsigned y:{7u,12u})for(unsigned x:{5u,14u}){
                const auto* p=&data[(y*20+x)*4];
                require(std::abs(int(p[0])-(x<10?64:192))<=1&&std::abs(int(p[1])-(y<10?32:160))<=1&&
                    std::abs(int(p[2])-96)<=1&&p[3]==173,"padded content scale/orientation/gamma");
            }
            auto bad=rect;bad.Width=21;
            require(!pass.draw(dev.c.p,src.t.p,dst.r.p,20,20,0,&bad),"out of bounds padding accepted");
            state.verify();require(read(dev,dst.t.p)==data,"invalid padding altered destination");
        }
    }
}
void invalid(Device& dev,State& state){
    lmuqv::OuterResample pass;Texture src(dev,4,4,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_SHADER_RESOURCE);
    Texture dst(dev,2,2,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_RENDER_TARGET);
    state.bind();
    require(!pass.draw(nullptr,src.t.p,dst.r.p,2,2),"null context accepted");
    require(!pass.draw(dev.c.p,nullptr,dst.r.p,2,2),"null source accepted");
    require(!pass.draw(dev.c.p,src.t.p,nullptr,2,2),"null RTV accepted");
    require(!pass.draw(dev.c.p,src.t.p,dst.r.p,0,2),"zero size accepted");
    require(!pass.draw(dev.c.p,src.t.p,dst.r.p,3,2),"mismatched destination size accepted");
    require(!pass.draw(dev.c.p,src.t.p,dst.r.p,0xffffffffu,2),"overflow/oversize accepted");
    Texture unsupported(dev,4,4,DXGI_FORMAT_R32_FLOAT,D3D11_BIND_SHADER_RESOURCE);
    require(!pass.draw(dev.c.p,unsupported.t.p,dst.r.p,2,2),"unsupported format accepted");
    Texture noSrv(dev,4,4,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_RENDER_TARGET);
    require(!pass.draw(dev.c.p,noSrv.t.p,dst.r.p,2,2),"source without SRV bind accepted");
    Texture msaa(dev,4,4,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_SHADER_RESOURCE,nullptr,DXGI_FORMAT_UNKNOWN,2);
    require(!pass.draw(dev.c.p,msaa.t.p,dst.r.p,2,2),"MSAA source accepted");
    Texture msaaDest(dev,2,2,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_RENDER_TARGET,nullptr,DXGI_FORMAT_UNKNOWN,2);
    require(!pass.draw(dev.c.p,src.t.p,msaaDest.r.p,2,2),"MSAA destination accepted");
    Texture badDest(dev,2,2,DXGI_FORMAT_R32_FLOAT,D3D11_BIND_RENDER_TARGET);
    require(!pass.draw(dev.c.p,src.t.p,badDest.r.p,2,2),"unsupported destination accepted");
    Texture array(dev,4,4,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_SHADER_RESOURCE,nullptr,DXGI_FORMAT_UNKNOWN,1,2);
    require(!pass.draw(dev.c.p,array.t.p,dst.r.p,2,2),"texture array accepted");
    Texture same(dev,2,2,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);
    require(!pass.draw(dev.c.p,same.t.p,same.r.p,2,2),"input/output alias accepted");
    Com<ID3D11DeviceContext> deferred;check(dev.d->CreateDeferredContext(0,deferred.put()),"deferred context");
    require(!pass.draw(deferred.p,src.t.p,dst.r.p,2,2),"deferred context accepted");
    Device other;Texture foreign(other,4,4,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_SHADER_RESOURCE);
    require(!pass.draw(dev.c.p,foreign.t.p,dst.r.p,2,2),"foreign source device accepted");
    Texture foreignDest(other,2,2,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_RENDER_TARGET);
    require(!pass.draw(dev.c.p,src.t.p,foreignDest.r.p,2,2),"foreign RTV device accepted");
    Device low(D3D_FEATURE_LEVEL_9_3);
    Texture lowSource(low,4,4,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_SHADER_RESOURCE);
    Texture lowDest(low,2,2,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_RENDER_TARGET);
    lmuqv::OuterResample unsupportedPass;
    require(!unsupportedPass.draw(low.c.p,lowSource.t.p,lowDest.r.p,2,2),"unsupported device feature level accepted");
    state.verify();
    require(pass.draw(dev.c.p,src.t.p,dst.r.p,2,2),"valid draw after rejected inputs");state.verify();
    require(!pass.draw(other.c.p,foreign.t.p,foreignDest.r.p,2,2),"bound-device switch without reset accepted");
    pass.reset();require(pass.draw(other.c.p,foreign.t.p,foreignDest.r.p,2,2),"device switch after reset failed");
    std::array<Com<ID3D11Texture2D>,9> sources;
    pass.reset();
    D3D11_TEXTURE2D_DESC desc{};src.t->GetDesc(&desc);
    for(unsigned i=0;i<9;++i){
        check(dev.d->CreateTexture2D(&desc,nullptr,sources[i].put()),"cache source");
        require(pass.draw(dev.c.p,sources[i].p,dst.r.p,2,2)==(i<8),"cache bound violated");
    }
    require(pass.draw(dev.c.p,sources[0].p,dst.r.p,2,2),"cached source rejected");
    pass.reset();require(pass.draw(dev.c.p,sources[8].p,dst.r.p,2,2),"reset did not clear cache");
    state.verify();
}
void channels_and_orientation(Device& dev,State& state){
    const std::array<std::array<unsigned char,4>,4> expected{{
        {{17,43,211,64}},{{233,101,29,128}},{{54,189,90,192}},{{123,78,34,255}}}};
    for(bool reverse:{false,true}){
        std::array<unsigned char,64> bytes{};
        for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){
            auto color=expected[(y/2)*2+x/2];if(reverse)std::swap(color[0],color[2]);
            std::memcpy(bytes.data()+(y*4+x)*4,color.data(),4);
        }
        Texture src(dev,4,4,reverse?DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM,
            D3D11_BIND_SHADER_RESOURCE,bytes.data());
        Texture dst(dev,2,2,reverse?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_B8G8R8A8_UNORM,
            D3D11_BIND_RENDER_TARGET);
        lmuqv::OuterResample pass;state.bind();require(pass.draw(dev.c.p,src.t.p,dst.r.p,2,2),"channel draw");state.verify();
        auto actual=read(dev,dst.t.p);
        for(unsigned i=0;i<4;++i){auto color=expected[i];if(!reverse)std::swap(color[0],color[2]);
            for(unsigned c=0;c<4;++c)require(std::abs(int(actual[i*4+c])-color[c])<=1,"color order/orientation");}
    }
}
void binding_hazards(Device& dev){
    std::array<unsigned char,64> bytes{};bytes.fill(128);
    Texture src(dev,4,4,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE,bytes.data());
    Texture dst(dev,2,2,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
    Com<ID3D11ShaderResourceView> srv;check(dev.d->CreateShaderResourceView(dst.t.p,nullptr,srv.put()),"hazard SRV");
    dev.c->ClearState();ID3D11RenderTargetView* rtv=src.r.p;dev.c->OMSetRenderTargets(1,&rtv,nullptr);
    ID3D11ShaderResourceView* view=srv.p;dev.c->PSSetShaderResources(9,1,&view);
    lmuqv::OuterResample pass;require(pass.draw(dev.c.p,src.t.p,dst.r.p,2,2),"hazard draw");
    Com<ID3D11RenderTargetView> target;Com<ID3D11ShaderResourceView> resource;
    dev.c->OMGetRenderTargets(1,target.put(),nullptr);dev.c->PSGetShaderResources(9,1,resource.put());
    require(target.p==src.r.p&&resource.p==srv.p,"SRV/RTV hazard restoration");
    auto result=read(dev,dst.t.p);for(auto v:result)require(std::abs(int(v)-128)<=1,"hazard output");
}
}
int main(){try{
    Device device;State state(device);state.bind();state.verify();
    pixels(device,state,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM,true,128);
    pixels(device,state,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM,true,128);
    pixels(device,state,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,true,188);
    pixels(device,state,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,false,128);
    pixels(device,state,DXGI_FORMAT_R8G8B8A8_TYPELESS,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,false,128);
    pixels(device,state,DXGI_FORMAT_B8G8R8A8_TYPELESS,DXGI_FORMAT_B8G8R8A8_UNORM,false,128);
    pixels(device,state,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,false,188);
    pixels(device,state,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM,false,55);
    pixels(device,state,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,false,128,1.f);
    pixels(device,state,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,false,188,1.f);
    pixels(device,state,DXGI_FORMAT_R8G8B8A8_TYPELESS,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,false,128,1.f);
    float radius=5;
    require(lmuqv::parse_outer_blur(nullptr,radius)&&radius==0,"absent blur default");
    require(lmuqv::parse_outer_blur("0.25",radius)&&radius==.25f,"fractional blur parse");
    require(lmuqv::parse_outer_blur("3",radius)&&radius==3,"max blur parse");
    for(const char* invalid:{"NaN","inf","-1","3.1","1junk","0x1","1,5"})require(!lmuqv::parse_outer_blur(invalid,radius)&&radius==0,"invalid blur parse");
    channels_and_orientation(device,state);
    blur_pixels(device,state);
    padded_pixels(device,state);
    invalid(device,state);
    binding_hazards(device);
    std::cout<<"OuterResample WARP pixel/gamma/state/validation/cache tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
