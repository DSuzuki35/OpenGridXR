#include "../wheels/mesh_renderer.h"
#include "dashboard_placement.hpp"
#include "wheel_model.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace ovrlay;
namespace fs=std::filesystem;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F> void loaded(F select) {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(!select()) {
        check(std::chrono::steady_clock::now()<deadline,"asynchronous asset load timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
// A known opaque suit plane tests depth ordering independently of artwork.
struct DriverPlane {
    fs::path path;
    DriverPlane() {
        wchar_t folder[MAX_PATH]{},file[MAX_PATH]{};
        check(GetTempPathW(MAX_PATH,folder) && GetTempFileNameW(folder,L"agd",0,file),"temporary AGD");
        path=file;
    }
    ~DriverPlane(){std::error_code ec;fs::remove(path,ec);}
    void write(float z,bool invalidIndex=false,uint32_t normal=0,bool cloth=false,uint32_t albedo=0xffffffffu,uint32_t adjacent=0xffffffffu) const {
        std::ofstream out(path,std::ios::binary|std::ios::trunc);
        auto bytes=[&](const auto& value){out.write(reinterpret_cast<const char*>(&value),sizeof(value));};
        out.write("AGDRVR1",8);
        const uint32_t header[]={cloth?4u:normal?3u:2u,4,6,1,241,cloth?2u:1u,1};bytes(header);
        for(unsigned c=0;c<4;c++) {
            const float attributes[]={c&1?3.f:-3.f,c&2?3.f:-3.f,z,0,0,1,float(c&1),float((c>>1)&1),normal&&!cloth?0.f:1.f};
            const uint32_t joints[4]{};const float weights[]={1,0,0,0};
            bytes(attributes);bytes(joints);bytes(weights);
        }
        const uint32_t triangles[]={0,1,2,1,invalidIndex?4u:3u,2};bytes(triangles);
        const float identity[]={1,0,0,0,0,1,0,0,0,0,1,0};
        const float bounds[]={-3,-3,z,3,3,z};
        for(unsigned f=0;f<241;f++)bytes(identity);
        for(unsigned i=0;i<241*4;i++)bytes(bounds);
        if(cloth)bytes(adjacent);
        bytes(albedo);
        if(normal){bytes(normal);if(cloth)bytes(normal);}
        check(bool(out),"write depth fixture");
    }
};
int wmain(int argc,wchar_t** argv) {
    try {
        check(argc>=3,"expected wheel and driver paths");
        Com<ID3D11Device> dev;Com<ID3D11DeviceContext> ctx;
        auto create=[&](D3D_DRIVER_TYPE type){return D3D11CreateDevice(nullptr,type,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,dev.put(),nullptr,ctx.put());};
        check(SUCCEEDED(create(D3D_DRIVER_TYPE_HARDWARE)) || SUCCEEDED(create(D3D_DRIVER_TYPE_WARP)),"D3D11 device");
        MeshRenderer r;check(r.initializePipeline(dev.get()),"wheel shader compilation");
        const auto modelId=fs::path(argv[1]).stem().string();
        const auto kind=og::wheel_kind(modelId);
        check(kind && og::wheel_driver_model(modelId)==fs::path(argv[2]).stem().string(),"built-in wheel selects its fitted driver asset");
        loaded([&]{return r.selectModel(dev.get(),0,argv[1],kind,kind);});
        r.setLights(false,ModelLightsFrame{},0);
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=argc>3?1536:512;desc.SampleDesc.Count=desc.ArraySize=desc.MipLevels=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        Com<ID3D11Texture2D> target,staging;Com<ID3D11RenderTargetView> view;
        check(SUCCEEDED(dev->CreateTexture2D(&desc,nullptr,target.put())) &&
              SUCCEEDED(dev->CreateRenderTargetView(target.get(),nullptr,view.put())),"output target");
        desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check(SUCCEEDED(dev->CreateTexture2D(&desc,nullptr,staging.put())),"readback staging");
        const XrPosef identityPose{{0,0,0,1},{0,0,0}};
        XrPosef wheelPose=identityPose;
        auto wheelLocal=[&](XrVector3f world,bool vector=false) {
            if(!vector) {
                world.x-=wheelPose.position.x;world.y-=wheelPose.position.y;world.z-=wheelPose.position.z;
            }
            const auto q=wheelPose.orientation;
            return og::spatial_rotate({-q.x,-q.y,-q.z,q.w},world);
        };
        auto draw=[&](unsigned eye,bool camera=false) {
            const XrVector3f origin{eye?.032f:-.032f,.355f,.475f};
            const auto local=wheelLocal(origin);
            MeshEyeParameters p{};p.eyeLocal[0]=local.x;p.eyeLocal[1]=local.y;p.eyeLocal[2]=local.z;p.model[0]=kind==4?.32f:.30f;p.model[1]=1;
            const float visible[]={-.6f,-.6f,.6f,.6f};
            check(r.aperture(p,visible),"visible wheel aperture");
            if(!camera) {p.aperture[0]=0;p.aperture[1]=-.075f;p.aperture[2]=.80f;p.aperture[3]=.80f;}
            check(r.render(dev.get(),ctx.get(),target.get(),desc.Format,nullptr,p,view.get(),nullptr,eye),"draw");
            ctx->CopyResource(staging.get(),target.get());
            D3D11_MAPPED_SUBRESOURCE map{};check(SUCCEEDED(ctx->Map(staging.get(),0,D3D11_MAP_READ,0,&map)),"readback map");
            std::vector<uint32_t> result(desc.Width*desc.Height);
            for(unsigned y=0;y<desc.Height;y++)memcpy(result.data()+y*desc.Width,
                static_cast<unsigned char*>(map.pData)+y*map.RowPitch,desc.Width*4);
            ctx->Unmap(staging.get(),0);
            if(!camera)return result;
            std::vector<uint32_t> photo(result.size());
            for(unsigned y=0;y<desc.Height;y++)for(unsigned x=0;x<desc.Width;x++) {
                // Sample the rendered layer through a fixed camera, including the rotated mount.
                const XrVector3f ray=wheelLocal({(float(x)+.5f)/desc.Width*.72f-.36f-origin.x,
                    .225f-(float(y)+.5f)/desc.Height*.72f-origin.y,-origin.z},true);
                const float t=-local.z/ray.z;
                const float u=((local.x+t*ray.x-p.aperture[0])/p.aperture[2]+.5f)*desc.Width-.5f;
                const float v=(.5f-(local.y+t*ray.y-p.aperture[1])/p.aperture[3])*desc.Height-.5f;
                const int ix=int(std::floor(u)),iy=int(std::floor(v));
                // Match ordinary compositor linear filtering, rather than
                // magnifying the diagnostic image with a nearest-neighbor copy.
                if(ix>=0 && iy>=0 && ix+1<int(desc.Width) && iy+1<int(desc.Height)) {
                    uint32_t color=0;
                    for(unsigned channel=0;channel<4;channel++) {
                        float value=0;
                        for(unsigned j=0;j<2;j++)for(unsigned i=0;i<2;i++)
                            value+=float((result[(iy+j)*desc.Width+ix+i]>>(8*channel))&255)*
                                   (i?u-ix:1-(u-ix))*(j?v-iy:1-(v-iy));
                        color|=uint32_t(std::clamp(value+.5f,0.f,255.f))<<(channel*8);
                    }
                    photo[y*desc.Width+x]=color;
                }
            }
            return photo;
        };
        auto changed=[](const auto& a,const auto& b){
            unsigned count=0;
            for(size_t i=0;i<a.size();i++)if(a[i]!=b[i]) {
                for(unsigned shift:{0u,8u,16u,24u}) {
                    if(std::abs(int((a[i]>>shift)&255)-int((b[i]>>shift)&255))>2){++count;break;}
                }
            }
            return count;
        };
        const auto wheel=draw(0);
        auto wheelRestored=[&]{
            const auto back=draw(0);
            unsigned maxDelta=0;
            for(size_t i=0;i<back.size();i++)for(unsigned shift:{0u,8u,16u,24u}) {
                unsigned delta=unsigned(std::abs(int((back[i]>>shift)&255)-int((wheel[i]>>shift)&255)));
                maxDelta=std::max(maxDelta,delta);
            }
            // The sRGB/MSAA resolve may round a boundary sample one code value differently.
            return maxDelta<=1;
        };
        const float identity[]={1,0,0,0,0,1,0,0,0,0,1,0};
        auto pose=[&](float angle,unsigned glove,unsigned suit,bool torso,bool legs,bool custom=false){
            wheelPose=og::steered_dashboard(identityPose,custom?XrVector3f{.025f,.03f,.025f}:XrVector3f{},custom?10.f:0.f,angle);
            const auto x=wheelLocal({1,0,0},true),y=wheelLocal({0,1,0},true),z=wheelLocal({0,0,1},true),p=wheelLocal({});
            const float mount[]={x.x,y.x,z.x,p.x,x.y,y.y,z.y,p.y,x.z,y.z,z.z,p.z};
            return r.selectDriver(dev.get(),0,argv[2],angle,mount,glove,suit,torso,legs);
        };
        loaded([&]{return pose(0,0,0,false,false);});
        const auto arms=draw(0),right=draw(1);
        check(changed(wheel,arms)>150,"driver pixels");
        check(changed(arms,right)>150,"stereo disparity");
        check(pose(0,3,0,false,false) && changed(arms,draw(0))>50,"glove tint");
        check(pose(0,0,4,false,false) && changed(arms,draw(0))>50,"suit tint");
        check(pose(1.7f,0,0,false,false) && changed(arms,draw(0))>150,"steering skin palette");
        check(pose(0,0,0,true,true) && changed(arms,draw(0))>150,"body visibility");
        constexpr float rad=.01745329252f;
        for(float angle:{15.f,90.f,-15.f,-90.f}) {
            pose(angle*rad,0,0,false,false);const auto first=draw(0);
            pose((angle+std::copysign(360.f,angle))*rad,0,0,false,false);
            check(changed(first,draw(0))<200,"continuous periodic steering after full turn");
        }
        auto capture=[&](const std::wstring& suffix){
            if(argc<4)return;
            const auto body=draw(0,true);
            std::ofstream ppm(fs::path(std::wstring(argv[3])+suffix+L".ppm"),std::ios::binary);
            ppm<<"P6\n"<<desc.Width<<" "<<desc.Height<<"\n255\n";for(uint32_t color:body) ppm.write(reinterpret_cast<char*>(&color),3);
        };
        pose(0,0,0,false,false);capture(L"-neutral");
        pose(0,0,1,false,false);capture(L"-black-suit");
        pose(0,0,4,false,false);capture(L"-white-suit");
        for(float angle:{70.f,90.f,120.f,180.f,360.f,-90.f}) {
            pose(angle*rad,0,0,false,false);capture(L"-angle"+std::to_wstring(int(angle)));
        }
        pose(90.f*rad,0,0,false,false);const auto standard=draw(0,true);
        pose(90.f*rad,0,0,false,false,true);capture(L"-custom90");
        check(changed(standard,draw(0,true))>500,"custom calibrated mount");
        wheelPose=identityPose;
        r.forgetDriver(0);
        check(wheelRestored(),"disabled driver restores wheel");
        check(!r.selectDriver(dev.get(),0,argv[1],0,identity,0,0,false,false),"wrong magic initially unavailable");
        loaded([&]{return r.idle();});
        check(!r.selectDriver(dev.get(),0,argv[1],0,identity,0,0,false,false) && wheelRestored(),"invalid AGD preserves wheel");
        DriverPlane plane;
        auto selectPlane=[&]{return r.selectDriver(dev.get(),0,plane.path.wstring(),0,identity,0,4,false,false);};
        plane.write(-1.5f);loaded(selectPlane);const auto behind=draw(0);
        r.forgetDriver(0);plane.write(.8f);loaded(selectPlane);const auto front=draw(0);
        unsigned ordered=0;
        for(size_t i=0;i<wheel.size();i++)if((wheel[i]>>24)==255 && wheel[i]==behind[i] && wheel[i]!=front[i])++ordered;
        check(ordered>500,"wheel occludes rear suit plane and front suit occludes wheel");
        r.forgetDriver(0);plane.write(.8f,false,0xffff8080);loaded(selectPlane);const auto flatNormal=draw(0);
        r.forgetDriver(0);plane.write(.8f,false,0xffc880e6);loaded(selectPlane);
        check(changed(flatNormal,draw(0))>500,"normal map changes material lighting on identical geometry");
        r.forgetDriver(0);plane.write(.8f,false,0xffff8080,true,0x00b0b0b0);loaded(selectPlane);const auto cloth=draw(0);
        r.forgetDriver(0);plane.write(.8f,false,0xffff8080,true,0x00b0b0b0,0xff000000);loaded(selectPlane);
        check(changed(cloth,draw(0))==0,"neighboring glove texture cannot bleed into cloth");
        r.forgetDriver(0);plane.write(.8f,false,0xffff8080,true,0x00404040);loaded(selectPlane);
        check(changed(cloth,draw(0))>500,"cloth albedo changes suit surface instead of a constant tint");
        r.forgetDriver(0);plane.write(.8f,false,0xffc880e6,true,0x00b0b0b0);loaded(selectPlane);
        check(changed(cloth,draw(0))>500,"cloth normal map shades wrinkles and seams");
        r.forgetDriver(0);plane.write(0,true);
        check(!selectPlane(),"malformed index initially unavailable");loaded([&]{return r.idle();});
        check(!selectPlane() && r.driverFailed(0) && wheelRestored(),"out-of-range index rejected without hiding wheel");
        std::cout<<"Visible driver changed "<<changed(wheel,arms)<<" pixels; stereo, steering, themes, optional body, cleanup and invalid asset passed.\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
