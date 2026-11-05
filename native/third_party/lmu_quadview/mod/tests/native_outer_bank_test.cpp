// Real WARP storage with synthetic native wrapper/container ABI entry points.
// This checks our ownership/restoration contract, not proprietary renderer code.
#include "../native_outer_bank.hpp"
#include <iostream>
#include <vector>
#include <stdexcept>
#include <map>
#include <string>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class T>T& field(void* p,unsigned offset){return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+offset);}
struct Wrapper {alignas(8) std::byte bytes[0xb0]{};unsigned refs=1;std::string name;};
struct Container {alignas(8) std::byte bytes[0x70]{};void* rtv=nullptr;void* dsv=nullptr;};
unsigned liveWrappers=0,liveContainers=0;
bool failFactory=false;
std::map<std::string,Wrapper*> liveNames;
unsigned nameCollisions=0;
void addref(void* p){++reinterpret_cast<Wrapper*>(p)->refs;}
void destroyContainer(void* p){
    if(!p)return;
    auto* c=static_cast<Container*>(p);
    for(unsigned offset:{0x20u,0x30u,0x68u})if(auto* v=field<IUnknown*>(p,offset))v->Release();
    if(c->rtv)static_cast<IUnknown*>(c->rtv)->Release();
    if(c->dsv)static_cast<IUnknown*>(c->dsv)->Release();
    delete c;--liveContainers;
}
void release(void* p){
    auto* w=static_cast<Wrapper*>(p);if(--w->refs)return;
    if(auto* a=field<void*>(p,0xa8))release(a);
    destroyContainer(field<void*>(p,0xa0));
    if(!w->name.empty())liveNames.erase(w->name);
    delete w;--liveWrappers;
}
int factory(void*,void** result,const char* name,unsigned w,unsigned h,unsigned format,unsigned mips,unsigned flags,unsigned sample){
    if(failFactory)return -1;
    // Original fixture targets have empty names. Only live named wrappers
    // participate in native resource-name ownership; final release frees it.
    if(name&&*name&&liveNames.count(name)){++nameCollisions;return -1;}
    auto* p=new Wrapper;++liveWrappers;*result=p;
    if(name&&*name){p->name=name;liveNames.emplace(p->name,p);}
    field<unsigned>(p,0)=flags;field<unsigned>(p,0x28)=w;field<unsigned>(p,0x2c)=h;
    field<unsigned>(p,0x34)=mips;field<unsigned>(p,0x38)=format;field<unsigned>(p,0x3c)=sample;
    return 0;
}
void* allocate(std::size_t bytes){require(bytes==0x70,"native allocation size");++liveContainers;return new Container;}
void* construct(void* p,const void*){return p;}
template<unsigned Offset>void set(void* p,void* value){if(value)static_cast<IUnknown*>(value)->AddRef();field<void*>(p,Offset)=value;}
template<bool Depth>void setView(void* p,void* value){
    auto* c=static_cast<Container*>(p);auto** slot=Depth?&c->dsv:&c->rtv;
    if(value)static_cast<IUnknown*>(value)->AddRef();
    *slot=value;
    const unsigned offset=Depth?0x50:0x38;
    field<void*>(p,offset)=slot;field<void*>(p,offset+8)=slot+1;
}
void install(void* wrapper,void** c){field<void*>(wrapper,0xa0)=*c;*c=nullptr;}
void discard(void*,void* c){destroyContainer(c);}
bool readable(const void* p,std::size_t bytes){
    MEMORY_BASIC_INFORMATION info{};
    return p&&VirtualQuery(p,&info,sizeof(info))==sizeof(info)&&info.State==MEM_COMMIT&&
        !(info.Protect&(PAGE_NOACCESS|PAGE_GUARD))&&
        reinterpret_cast<std::uintptr_t>(p)+bytes<=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
}
void log(const char*){}
struct Fixture {
    std::byte* image=static_cast<std::byte*>(VirtualAlloc(nullptr,0x1c00000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    std::vector<std::byte> renderer=std::vector<std::byte>(0x83000),ssr=std::vector<std::byte>(0x608);
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    std::vector<void*> originals;
    template<class Fn>void thunk(unsigned rva,Fn fn){
        auto* p=image+rva;DWORD old=0;require(VirtualProtect(p,16,PAGE_EXECUTE_READWRITE,&old),"thunk protect");
        p[0]=std::byte{0x48};p[1]=std::byte{0xb8};auto address=reinterpret_cast<std::uintptr_t>(fn);
        std::memcpy(p+2,&address,8);p[10]=std::byte{0xff};p[11]=std::byte{0xe0};
        FlushInstructionCache(GetCurrentProcess(),p,12);
    }
    Fixture(){
        require(image!=nullptr,"fixture image");
        require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP");
        thunk(0x543950,factory);thunk(0x54f680,addref);thunk(0x552a00,release);
        thunk(0x9ea868,allocate);thunk(0x603e90,construct);thunk(0x6056f0,set<0x20>);
        thunk(0x6053f0,setView<true>);thunk(0x605670,set<0x30>);thunk(0x6055c0,setView<false>);
        thunk(0x605770,set<0x68>);thunk(0x552d30,install);thunk(0x5431b0,discard);
        field<ID3D11DeviceContext*>(renderer.data(),0x76f68)=context;
        field<void*>(image,0x1b18d90)=ssr.data();
        field<void*>(image,0x1b1c750)=target(128,96,false);
        field<void*>(image,0x1b1c758)=target(128,96,true);
        field<void*>(image,0x1b1c740)=target(128,96,false);
        field<void*>(image,0x1b1c748)=target(128,96,false);
        field<void*>(ssr.data(),0x5d0)=target(64,48,false,7);
        field<void*>(ssr.data(),0x5f8)=target(64,48,false,7);
        auto* depth=target(64,48,true);addref(depth);
        field<void*>(field<void*>(ssr.data(),0x5d0),0xa8)=depth;
    }
    void* target(unsigned w,unsigned h,bool depth,unsigned mips=1){
        void* wrapper=nullptr;require(!factory(nullptr,&wrapper,"",w,h,depth?0:0x4e,mips,depth?0:0x100200a,0),"source wrapper");
        auto* c=allocate(0x70);field<void*>(wrapper,0xa0)=c;
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=mips;d.ArraySize=1;d.SampleDesc.Count=1;
        d.Format=depth?DXGI_FORMAT_D32_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT;
        d.BindFlags=depth?D3D11_BIND_DEPTH_STENCIL:D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        d.MiscFlags=mips>1?D3D11_RESOURCE_MISC_GENERATE_MIPS:0;
        ID3D11Texture2D* t=nullptr;require(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&t)),"source texture");set<0x20>(c,t);t->Release();
        if(depth){ID3D11DepthStencilView* v=nullptr;require(SUCCEEDED(device->CreateDepthStencilView(t,nullptr,&v)),"source DSV");setView<true>(c,v);v->Release();}
        else {
            ID3D11RenderTargetView* v=nullptr;require(SUCCEEDED(device->CreateRenderTargetView(t,nullptr,&v)),"source RTV");setView<false>(c,v);v->Release();
            ID3D11ShaderResourceView* s=nullptr;require(SUCCEEDED(device->CreateShaderResourceView(t,nullptr,&s)),"source SRV");set<0x30>(c,s);s->Release();
        }
        originals.push_back(wrapper);return wrapper;
    }
    ~Fixture(){context->ClearState();for(auto* p:originals)release(p);context->Release();device->Release();VirtualFree(image,0,MEM_RELEASE);}
    bool prepare(lmuqv::NativeOuterBank& bank,bool ssrEnabled=true,unsigned width=32,unsigned height=24){return bank.prepare(reinterpret_cast<std::uintptr_t>(image),reinterpret_cast<std::uintptr_t>(renderer.data()),width,height,1.f,readable,log,ssrEnabled);}
    void requireBlackMips(void* wrapper){
        auto* texture=field<ID3D11Texture2D*>(field<void*>(wrapper,0xa0),0x20);
        D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        d.BindFlags=d.MiscFlags=0;lmuqv::native_outer_detail::Com<ID3D11Texture2D> staging;
        require(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging.p)),"SSR readback staging");context->CopyResource(staging.p,texture);
        for(unsigned mip=0;mip<d.MipLevels;++mip){
            D3D11_MAPPED_SUBRESOURCE map{};require(SUCCEEDED(context->Map(staging.p,mip,D3D11_MAP_READ,0,&map)),"SSR mip map");
            const auto* pixel=static_cast<const unsigned short*>(map.pData);
            const bool black=pixel[0]==0&&pixel[1]==0&&pixel[2]==0&&pixel[3]==0;
            context->Unmap(staging.p,mip);require(black,"SSR first-frame history not cleared");
        }
    }
};
void independent_bank_names(){
    Fixture f;lmuqv::NativeOuterBank left,right;
    const auto before=nameCollisions;
    const bool leftReady=f.prepare(left,true,80,64)&&left.ssrActive();
    void* leftColor=nullptr;
    if(leftReady){
        lmuqv::NativeOuterBindings b;require(left.bind(b)&&b.apply(),"left name-test binding");
        leftColor=field<void*>(f.ssr.data(),0x5d0);addref(leftColor);
        require(field<unsigned>(leftColor,0x28)==64&&field<unsigned>(leftColor,0x2c)==48,"left capped SSR size");
    }
    // Different outer sizes still cap both SSR banks to the same 64x48.
    const bool rightReady=f.prepare(right,true,96,72)&&right.ssrActive();
    bool independent=false;
    if(rightReady){
        lmuqv::NativeOuterBindings b;require(right.bind(b)&&b.apply(),"right name-test binding");
        auto* rightColor=field<void*>(f.ssr.data(),0x5d0);
        independent=rightColor!=leftColor&&field<unsigned>(rightColor,0x28)==64&&field<unsigned>(rightColor,0x2c)==48;
    }
    require(left.reset()&&right.reset(),"name-test reset");
    // Keep one old wrapper alive across reset: the next generation must also
    // receive a new name, even for the same bank address and dimensions.
    const bool rebuilt=f.prepare(left,true,80,64)&&left.ssrActive();
    require(left.reset(),"name-test rebuild reset");
    if(leftColor)release(leftColor);
    require(nameCollisions==before,"independent SSR banks reused a live native resource name");
    require(leftReady&&rightReady&&independent&&rebuilt,"independent SSR bank preparation failed");
    require(liveNames.empty(),"released bank names remained registered");
}
void run(){
    unsigned w=0,h=0;
    using Bank=lmuqv::NativeOuterBank;
    require(Bank::ssrDimensions(1690,1716,756,768,w,h)&&w==756&&h==768,"captured SSR sizes");
    require(Bank::ssrDimensions(100,50,40,100,w,h)&&w==40&&h==50,"mixed axes/floor boundary");
    require(!Bank::ssrDimensions(100,50,39,100,w,h),"SSR below quality floor");
    require(!Bank::ssrDimensions(100,50,100,19,w,h),"SSR height below quality floor");
    require(!Bank::ssrDimensions(0,50,100,50,w,h)&&!Bank::ssrDimensions(100,50,0,50,w,h)&&
        !Bank::ssrDimensions(100,50,100,0,w,h)&&!Bank::ssrDimensions(16385,50,100,50,w,h),"SSR invalid dimensions");
    Fixture f;lmuqv::NativeOuterBank bank;
    auto*& color=field<void*>(f.ssr.data(),0x5d0);auto*& blur=field<void*>(f.ssr.data(),0x5f8);
    void* original=color;void* originalBlur=blur;
    require(f.prepare(bank)&&bank.ssrActive(),"SSR bank prepare");
    {
        lmuqv::NativeOuterBindings bindings;require(bank.bind(bindings)&&bindings.apply(),"SSR bindings");
        require(color!=original&&blur!=originalBlur,"SSR slots not replaced");
        require(field<unsigned>(color,0x28)==32&&field<unsigned>(color,0x2c)==24&&field<unsigned>(color,0x34)==6,"SSR dimensions/mips");
        auto* associated=field<void*>(color,0xa8);
        require(associated&&field<unsigned>(associated,0x28)==32&&field<unsigned>(associated,0x2c)==24,"SSR matching associated depth");
        require(field<unsigned>(original,0x28)==64&&field<unsigned>(original,0x34)==7,"focus wrapper mutated");
        f.requireBlackMips(color);f.requireBlackMips(blur);
        bindings.restore();require(color==original&&blur==originalBlur,"focus restore");
        require(bindings.apply()&&color!=original,"outer resume");
    }
    require(color==original&&blur==originalBlur,"RAII restore");
    const auto allocated=liveWrappers;require(f.prepare(bank)&&liveWrappers==allocated,"stable bank rebuilt");
    for(unsigned offset:{0x28u,0x2cu,0x34u}){
        const auto saved=field<unsigned>(original,offset);
        field<unsigned>(original,offset)=saved+1;
        require(f.prepare(bank)&&!bank.ssrActive(),"ready SSR bank accepted changed wrapper metadata");
        require(color==original&&blur==originalBlur&&field<unsigned>(original,offset)==saved+1,
            "metadata fallback modified original wrapper or slots");
        field<unsigned>(original,offset)=saved;
        require(f.prepare(bank)&&bank.ssrActive(),"restored metadata did not rebuild SSR bank");
    }
    require(bank.reset(),"bank reset");
    require(f.prepare(bank,false),"disabled SSR bank");
    {lmuqv::NativeOuterBindings b;require(bank.bind(b)&&b.apply()&&color==original,"disabled SSR changed slots");}
    require(bank.reset(),"disabled reset");
    field<void*>(f.ssr.data(),0x5e8)=original;
    require(f.prepare(bank)&&!bank.ssrActive(),"SSR alias did not fall back to primary bank");
    {
        lmuqv::NativeOuterBindings b;require(bank.bind(b)&&b.apply(),"SSR fallback primary bindings");
        require(color==original&&blur==originalBlur,"SSR fallback changed native SSR");
        require(field<unsigned>(field<void*>(f.image,0x1b1c750),0x28)==32,"SSR fallback lost small primary bank");
    }
    field<void*>(f.ssr.data(),0x5e8)=nullptr;
    require(bank.reset(),"alias failure reset");
    require(f.prepare(bank,true,24,20)&&!bank.ssrActive(),"quality-floor primary fallback");
    require(bank.reset(),"quality-floor reset");
    require(f.prepare(bank,true,32,64)&&bank.ssrActive(),"mixed-axis SSR bank");
    {
        lmuqv::NativeOuterBindings b;require(bank.bind(b)&&b.apply(),"mixed-axis binding");
        require(field<unsigned>(color,0x28)==32&&field<unsigned>(color,0x2c)==48,"mixed-axis SSR enlarged");
    }
    require(bank.reset(),"mixed-axis reset");
    // A live singleton replacement must rebuild both exact slots, without
    // retaining slot addresses into the previous object's storage.
    require(f.prepare(bank)&&bank.ssrActive(),"replacement baseline");
    auto replacement=f.ssr;field<void*>(f.image,0x1b18d90)=replacement.data();
    require(f.prepare(bank)&&bank.ssrActive(),"replacement rebuild");
    {
        lmuqv::NativeOuterBindings b;require(bank.bind(b)&&b.apply(),"replacement binding");
        require(color==original&&field<void*>(replacement.data(),0x5d0)!=original,"old singleton slots modified");
    }
    require(bank.reset(),"replacement reset");field<void*>(f.image,0x1b18d90)=f.ssr.data();
    // Unknown mip metadata fails only the SSR extension; the smaller primary
    // scene remains available and original singleton slots stay untouched.
    field<unsigned>(original,0x34)=99;
    require(f.prepare(bank)&&!bank.ssrActive(),"invalid mip metadata fallback");
    field<unsigned>(original,0x34)=7;require(bank.reset(),"metadata reset");
    failFactory=true;require(!f.prepare(bank),"factory failure accepted");failFactory=false;
    require(color==original&&blur==originalBlur,"failed preparation mutated original slots");
    require(bank.reset(),"failed reset");
}
}
int main(){try{independent_bank_names();run();require(!liveWrappers&&!liveContainers&&liveNames.empty(),"native ownership leak");std::cout<<"NativeOuterBank SSR WARP/native ABI fixture passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
