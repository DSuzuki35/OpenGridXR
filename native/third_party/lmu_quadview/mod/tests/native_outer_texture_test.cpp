// Offline real-D3D11 WARP tests. No game or hardware-device access.
#include "../native_outer_texture.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
template<class T>struct Com {
    T* p=nullptr;
    ~Com(){if(p)p->Release();}
    Com()=default;Com(const Com&)=delete;Com& operator=(const Com&)=delete;
    T** put(){return &p;}
    T* operator->()const{return p;}
};
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void check(HRESULT hr,const char* message){require(SUCCEEDED(hr),message);}
struct Device {
    Com<ID3D11Device> d;Com<ID3D11DeviceContext> c;
    Device(){check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,d.put(),nullptr,c.put()),"WARP create");}
    ~Device(){c->ClearState();}
};
template<class V>void belongs(V* view,ID3D11Texture2D* texture){
    Com<ID3D11Resource> resource;view->GetResource(resource.put());
    require(resource.p==static_cast<ID3D11Resource*>(texture),"cloned view resource mismatch");
}
// GetDesc defines the active union member, not padding or unused union bytes.
// WARP versions differ in the storage they return outside that active member.
bool same_view_desc(const D3D11_SHADER_RESOURCE_VIEW_DESC& a,const D3D11_SHADER_RESOURCE_VIEW_DESC& b){
    return a.Format==b.Format&&a.ViewDimension==b.ViewDimension&&
        (a.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2DMS||
        (a.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D&&a.Texture2D.MostDetailedMip==b.Texture2D.MostDetailedMip&&a.Texture2D.MipLevels==b.Texture2D.MipLevels));
}
bool same_view_desc(const D3D11_RENDER_TARGET_VIEW_DESC& a,const D3D11_RENDER_TARGET_VIEW_DESC& b){
    return a.Format==b.Format&&a.ViewDimension==b.ViewDimension&&
        (a.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2DMS||
        (a.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2D&&a.Texture2D.MipSlice==b.Texture2D.MipSlice));
}
bool same_view_desc(const D3D11_DEPTH_STENCIL_VIEW_DESC& a,const D3D11_DEPTH_STENCIL_VIEW_DESC& b){
    return a.Format==b.Format&&a.ViewDimension==b.ViewDimension&&a.Flags==b.Flags&&
        (a.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2DMS||
        (a.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2D&&a.Texture2D.MipSlice==b.Texture2D.MipSlice));
}
bool same_view_desc(const D3D11_UNORDERED_ACCESS_VIEW_DESC& a,const D3D11_UNORDERED_ACCESS_VIEW_DESC& b){
    return a.Format==b.Format&&a.ViewDimension==b.ViewDimension&&a.ViewDimension==D3D11_UAV_DIMENSION_TEXTURE2D&&a.Texture2D.MipSlice==b.Texture2D.MipSlice;
}
template<class V,class Desc>void equal_view(V* first,V* second){
    Desc a{},b{};first->GetDesc(&a);second->GetDesc(&b);
    require(same_view_desc(a,b),"cloned view descriptor changed");
}
void descriptor_storage_regression(){
    D3D11_SHADER_RESOURCE_VIEW_DESC a,b;
    std::memset(&a,0xa5,sizeof(a));std::memset(&b,0x5a,sizeof(b));
    a.Format=b.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    a.ViewDimension=b.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
    a.Texture2D.MostDetailedMip=b.Texture2D.MostDetailedMip=0;
    a.Texture2D.MipLevels=b.Texture2D.MipLevels=1;
    require(same_view_desc(a,b),"inactive SRV union storage must not affect descriptor equality");
    b.Texture2D.MipLevels=2;require(!same_view_desc(a,b),"active mip count changes must fail equality");
    D3D11_RENDER_TARGET_VIEW_DESC r1,r2;std::memset(&r1,0xa5,sizeof(r1));std::memset(&r2,0x5a,sizeof(r2));
    r1.Format=r2.Format=DXGI_FORMAT_R8G8B8A8_UNORM;r1.ViewDimension=r2.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;r1.Texture2D.MipSlice=r2.Texture2D.MipSlice=0;
    require(same_view_desc(r1,r2),"inactive RTV bytes ignored");r2.Texture2D.MipSlice=1;require(!same_view_desc(r1,r2),"RTV mip changes detected");
    r1.ViewDimension=r2.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DMS;require(same_view_desc(r1,r2),"MSAA RTV has no meaningful union fields");
    D3D11_DEPTH_STENCIL_VIEW_DESC d1,d2;std::memset(&d1,0xa5,sizeof(d1));std::memset(&d2,0x5a,sizeof(d2));
    d1.Format=d2.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;d1.ViewDimension=d2.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;d1.Flags=d2.Flags=0;d1.Texture2D.MipSlice=d2.Texture2D.MipSlice=0;
    require(same_view_desc(d1,d2),"inactive DSV bytes ignored");d2.Flags=D3D11_DSV_READ_ONLY_DEPTH;require(!same_view_desc(d1,d2),"DSV flags changes detected");
    D3D11_UNORDERED_ACCESS_VIEW_DESC u1,u2;std::memset(&u1,0xa5,sizeof(u1));std::memset(&u2,0x5a,sizeof(u2));
    u1.Format=u2.Format=DXGI_FORMAT_R8G8B8A8_UNORM;u1.ViewDimension=u2.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;u1.Texture2D.MipSlice=u2.Texture2D.MipSlice=0;
    require(same_view_desc(u1,u2),"inactive UAV bytes ignored");u2.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;require(!same_view_desc(u1,u2),"UAV format changes detected");
}
void same_texture_desc(ID3D11Texture2D* source,ID3D11Texture2D* result,UINT width,UINT height){
    D3D11_TEXTURE2D_DESC a{},b{};source->GetDesc(&a);result->GetDesc(&b);
    a.Width=width;a.Height=height;
    require(std::memcmp(&a,&b,sizeof(a))==0,"texture descriptor changed beyond dimensions");
}
void empty(const lmuqv::NativeOuterTexture& clone){
    require(!clone.texture()&&!clone.dsv()&&!clone.srv()&&!clone.rtv()&&!clone.uav(),"expected empty clone");
}
void color(Device& device,DXGI_FORMAT format){
    D3D11_TEXTURE2D_DESC desc{};desc.Width=64;desc.Height=48;desc.MipLevels=desc.ArraySize=1;
    desc.Format=format;desc.SampleDesc.Count=1;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    Com<ID3D11Texture2D> original;check(device.d->CreateTexture2D(&desc,nullptr,original.put()),"color texture");
    Com<ID3D11RenderTargetView> rtv;Com<ID3D11ShaderResourceView> srv;Com<ID3D11UnorderedAccessView> uav;
    D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=DXGI_FORMAT_R8G8B8A8_UNORM;ud.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;
    check(device.d->CreateRenderTargetView(original.p,&rd,rtv.put()),"color RTV");
    check(device.d->CreateShaderResourceView(original.p,&sd,srv.put()),"color SRV");
    check(device.d->CreateUnorderedAccessView(original.p,&ud,uav.put()),"color UAV");
    lmuqv::NativeOuterTexture clone;empty(clone);
    require(clone.create(original.p,nullptr,srv.p,rtv.p,uav.p,16,12),"color clone create");
    require(clone.texture()!=original.p&&!clone.dsv(),"clone did not create independent color resource");
    same_texture_desc(original.p,clone.texture(),16,12);
    belongs(clone.rtv(),clone.texture());belongs(clone.srv(),clone.texture());belongs(clone.uav(),clone.texture());
    equal_view<ID3D11RenderTargetView,D3D11_RENDER_TARGET_VIEW_DESC>(rtv.p,clone.rtv());
    equal_view<ID3D11ShaderResourceView,D3D11_SHADER_RESOURCE_VIEW_DESC>(srv.p,clone.srv());
    equal_view<ID3D11UnorderedAccessView,D3D11_UNORDERED_ACCESS_VIEW_DESC>(uav.p,clone.uav());
    const float green[4]{0,1,0,1};device.c->ClearRenderTargetView(clone.rtv(),green);
    D3D11_TEXTURE2D_DESC read{};clone.texture()->GetDesc(&read);read.BindFlags=0;
    read.Usage=D3D11_USAGE_STAGING;read.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Com<ID3D11Texture2D> staging;check(device.d->CreateTexture2D(&read,nullptr,staging.put()),"staging");
    device.c->CopyResource(staging.p,clone.texture());D3D11_MAPPED_SUBRESOURCE map{};
    check(device.c->Map(staging.p,0,D3D11_MAP_READ,0,&map),"map clone");
    auto* first=static_cast<const unsigned char*>(map.pData);
    const bool greenPixel=first[0]==0&&first[1]==255&&first[2]==0&&first[3]==255;
    device.c->Unmap(staging.p,0);require(greenPixel,"cloned RTV not writable");

    auto* saved=clone.texture();auto* savedRtv=clone.rtv();
    for(const auto& size:{std::pair<UINT,UINT>{0,12},{16,0},{65,12},{16,49},{16385,12}}){
        require(!clone.create(original.p,nullptr,srv.p,rtv.p,uav.p,size.first,size.second),"invalid size accepted");
        require(clone.texture()==saved&&clone.rtv()==savedRtv,"invalid size changed old state");
    }
    require(!clone.create(nullptr,nullptr,nullptr,nullptr,nullptr,16,12)&&clone.texture()==saved,"null source changed state");
    Com<ID3D11Texture2D> other;check(device.d->CreateTexture2D(&desc,nullptr,other.put()),"other texture");
    Com<ID3D11RenderTargetView> otherRtv;check(device.d->CreateRenderTargetView(other.p,&rd,otherRtv.put()),"other RTV");
    require(!clone.create(original.p,nullptr,srv.p,otherRtv.p,uav.p,16,12)&&clone.texture()==saved,"foreign resource accepted");
    Device second;Com<ID3D11Texture2D> otherDeviceTexture;
    check(second.d->CreateTexture2D(&desc,nullptr,otherDeviceTexture.put()),"other device texture");
    Com<ID3D11ShaderResourceView> otherSrv;check(second.d->CreateShaderResourceView(otherDeviceTexture.p,&sd,otherSrv.put()),"other device SRV");
    require(!clone.create(original.p,nullptr,otherSrv.p,rtv.p,uav.p,16,12)&&clone.texture()==saved,"foreign device accepted");
    // Immutable descriptor is supported by original+data, but cannot be cloned
    // without its contents. Rejecting it must preserve the previous clone.
    desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    unsigned char pixels[64*48*4]{};D3D11_SUBRESOURCE_DATA initial{pixels,64*4,0};
    Com<ID3D11Texture2D> immutable;check(device.d->CreateTexture2D(&desc,&initial,immutable.put()),"immutable original");
    require(!clone.create(immutable.p,nullptr,nullptr,nullptr,nullptr,16,12)&&clone.texture()==saved,"failed allocation changed state");
    require(clone.create(original.p,nullptr,nullptr,nullptr,nullptr,64,48),"same-size texture-only create");
    require(clone.texture()!=saved&&!clone.rtv()&&!clone.srv()&&!clone.uav(),"replacement retained old views");
    clone.reset();clone.reset();empty(clone);
    desc.Usage=D3D11_USAGE_DEFAULT;desc.ArraySize=2;
    Com<ID3D11Texture2D> array;check(device.d->CreateTexture2D(&desc,nullptr,array.put()),"array original");
    require(!clone.create(array.p,nullptr,nullptr,nullptr,nullptr,16,12),"array accepted");empty(clone);
    desc.ArraySize=1;desc.MipLevels=2;
    Com<ID3D11Texture2D> mips;check(device.d->CreateTexture2D(&desc,nullptr,mips.put()),"mips original");
    require(!clone.create(mips.p,nullptr,nullptr,nullptr,nullptr,16,12),"mips accepted");empty(clone);
    std::cout<<"color format="<<unsigned(format)<<" descriptors/views/readback/atomic failure/reset passed\n";
}
void depth(Device& device){
    D3D11_TEXTURE2D_DESC desc{};desc.Width=64;desc.Height=48;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R24G8_TYPELESS;desc.SampleDesc.Count=1;
    desc.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
    Com<ID3D11Texture2D> original;check(device.d->CreateTexture2D(&desc,nullptr,original.put()),"depth texture");
    D3D11_DEPTH_STENCIL_VIEW_DESC dd{};dd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    Com<ID3D11DepthStencilView> dsv;check(device.d->CreateDepthStencilView(original.p,&dd,dsv.put()),"depth DSV");
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
    Com<ID3D11ShaderResourceView> srv;check(device.d->CreateShaderResourceView(original.p,&sd,srv.put()),"depth SRV");
    lmuqv::NativeOuterTexture clone;
    require(clone.create(original.p,dsv.p,srv.p,nullptr,nullptr,16,12),"typeless depth clone");
    same_texture_desc(original.p,clone.texture(),16,12);belongs(clone.dsv(),clone.texture());belongs(clone.srv(),clone.texture());
    equal_view<ID3D11DepthStencilView,D3D11_DEPTH_STENCIL_VIEW_DESC>(dsv.p,clone.dsv());
    equal_view<ID3D11ShaderResourceView,D3D11_SHADER_RESOURCE_VIEW_DESC>(srv.p,clone.srv());
    device.c->ClearDepthStencilView(clone.dsv(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,.5f,37);
    clone.reset();clone.reset();empty(clone);
    std::cout<<"typeless depth DSV/SRV passed\n";
}
void ssr_mips(Device& device){
    D3D11_TEXTURE2D_DESC desc{};desc.Width=64;desc.Height=48;desc.MipLevels=7;desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.SampleDesc.Count=1;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
    Com<ID3D11Texture2D> original;check(device.d->CreateTexture2D(&desc,nullptr,original.put()),"SSR texture");
    Com<ID3D11RenderTargetView> rtv;Com<ID3D11ShaderResourceView> srv;
    check(device.d->CreateRenderTargetView(original.p,nullptr,rtv.put()),"SSR RTV");
    check(device.d->CreateShaderResourceView(original.p,nullptr,srv.put()),"SSR SRV");
    lmuqv::NativeOuterTexture clone;
    require(!clone.create(original.p,nullptr,srv.p,rtv.p,nullptr,32,24),"default path accepted mip chain");
    require(clone.create(original.p,nullptr,srv.p,rtv.p,nullptr,32,24,true),"SSR mip clone");
    D3D11_TEXTURE2D_DESC actual{};clone.texture()->GetDesc(&actual);
    require(actual.Width==32&&actual.Height==24&&actual.MipLevels==6&&actual.MiscFlags==desc.MiscFlags,"SSR mip descriptor");
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};clone.srv()->GetDesc(&sd);
    require(sd.Texture2D.MostDetailedMip==0&&sd.Texture2D.MipLevels==6,"SSR SRV chain");
    const float green[4]{0,1,0,1};device.c->ClearRenderTargetView(clone.rtv(),green);device.c->GenerateMips(clone.srv());
    actual.Usage=D3D11_USAGE_STAGING;actual.CPUAccessFlags=D3D11_CPU_ACCESS_READ;actual.BindFlags=actual.MiscFlags=0;
    Com<ID3D11Texture2D> staging;check(device.d->CreateTexture2D(&actual,nullptr,staging.put()),"SSR staging");
    device.c->CopyResource(staging.p,clone.texture());
    D3D11_MAPPED_SUBRESOURCE map{};check(device.c->Map(staging.p,5,D3D11_MAP_READ,0,&map),"SSR final mip map");
    const auto* p=static_cast<const unsigned short*>(map.pData);
    const bool greenMip=p[0]==0&&p[1]==0x3c00&&p[2]==0&&p[3]==0x3c00;
    device.c->Unmap(staging.p,5);require(greenMip,"SSR final mip not generated");
    auto* saved=clone.texture();
    sd.Texture2D.MipLevels=3;
    Com<ID3D11ShaderResourceView> partial;check(device.d->CreateShaderResourceView(original.p,&sd,partial.put()),"SSR partial SRV");
    require(!clone.create(original.p,nullptr,partial.p,rtv.p,nullptr,32,24,true)&&clone.texture()==saved,"partial mip chain accepted");
    std::cout<<"SSR mip resizing/generation/readback/rejection passed\n";
}
void msaa(Device& device){
    UINT quality=0;check(device.d->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM,2,&quality),"MSAA query");
    if(!quality){std::cout<<"SKIP WARP MSAA2 unsupported\n";return;}
    D3D11_TEXTURE2D_DESC desc{};desc.Width=64;desc.Height=48;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=2;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    Com<ID3D11Texture2D> original;check(device.d->CreateTexture2D(&desc,nullptr,original.put()),"MSAA texture");
    Com<ID3D11RenderTargetView> rtv;Com<ID3D11ShaderResourceView> srv;
    check(device.d->CreateRenderTargetView(original.p,nullptr,rtv.put()),"MSAA RTV");
    check(device.d->CreateShaderResourceView(original.p,nullptr,srv.put()),"MSAA SRV");
    lmuqv::NativeOuterTexture clone;require(clone.create(original.p,nullptr,srv.p,rtv.p,nullptr,16,12),"MSAA clone");
    same_texture_desc(original.p,clone.texture(),16,12);belongs(clone.rtv(),clone.texture());belongs(clone.srv(),clone.texture());
    equal_view<ID3D11RenderTargetView,D3D11_RENDER_TARGET_VIEW_DESC>(rtv.p,clone.rtv());
    equal_view<ID3D11ShaderResourceView,D3D11_SHADER_RESOURCE_VIEW_DESC>(srv.p,clone.srv());
    const float blue[4]{0,0,1,1};device.c->ClearRenderTargetView(clone.rtv(),blue);
    std::cout<<"WARP MSAA2 RTV/SRV passed\n";
}
}
int main(){try{descriptor_storage_regression();Device device;color(device,DXGI_FORMAT_R8G8B8A8_UNORM);color(device,DXGI_FORMAT_R8G8B8A8_TYPELESS);depth(device);msaa(device);ssr_mips(device);std::cout<<"NativeOuterTexture tests passed\n";return 0;}
catch(const std::exception& e){std::cerr<<"NativeOuterTexture: "<<e.what()<<"\n";return 1;}}
