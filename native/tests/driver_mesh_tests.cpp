#include "../wheels/mesh_renderer.h"
#include "dashboard_placement.hpp"
#include "wheel_model.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace ovrlay;
namespace fs=std::filesystem;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static void verifyFit(const fs::path& path) {
    using namespace driver_fit;
    std::ifstream file(path,std::ios::binary);char magic[8];uint32_t header[7];
    file.read(magic,8);file.read(reinterpret_cast<char*>(header),sizeof(header));
    check(file && header[0]==5,"built-in driver has v5 anatomy metadata");
    const auto vertices=header[1],indices=header[2],joints=header[3];
    file.seekg(36ull+vertices*68ull+indices*4ull+241ull*joints*48+241ull*4*6*4);
    Metadata meta;uint32_t roles[128];Frame frames[241];
    file.read(reinterpret_cast<char*>(&meta),sizeof(meta));file.read(reinterpret_cast<char*>(roles),joints*4);
    file.read(reinterpret_cast<char*>(frames),sizeof(frames));check(bool(file),"read calibration metadata");
    const DriverFit fits[]={{},{-.3f,-.3f,.75f,-20,.7f,-.15f},{.3f,.4f,1.25f,30,1.15f,.15f},
                            {-.08f,.04f,1.08f,12,.85f,.06f}};
    bool sawLimit=false;
    const DriverMount settingsList[]={{},{.025f,.03f,.025f,10.f}};
    for(const auto& settings:settingsList)for(float width:{.27f,.30f,.32f,.36f})for(const auto& fit:fits) {
      const auto plan=prepare(frames,meta,fit,settings,width);
      if(!plan.valid) {
          float worst=0;
          for(unsigned f=0;f<241;f++) {
              const float a=(float(f)*3-360)*.01745329252f;const auto g=gripRotation(a);
              const auto q=solve(frames[f],meta,plan,inverseRigid(steering(settings,a)),g,width);
              for(unsigned side=0;side<2;side++)worst=std::max(worst,std::acos(std::clamp(dot(unit(g.vector(frames[f].points[side][6]-frames[f].points[side][2])),unit(q.wrists[side]-q.elbows[side])),-1.f,1.f))*57.2957795f);
          }
          std::cerr<<"Invalid plan width "<<width<<" tilt "<<settings.tiltDegrees<<" height "<<fit.seatHeight<<" worst "<<worst<<"\n";
      }
      check(plan.valid,"normal wheel sizes have a feasible anatomical fit");
      const auto reference=solve(frames[120],meta,plan,Matrix{},Matrix{},width);
      for(unsigned f=0;f<481;f++) {
        Frame sampled=frames[f/2];
        if(f%2)for(unsigned side=0;side<2;side++)for(unsigned point=0;point<7;point++)
            sampled.points[side][point]=(sampled.points[side][point]+frames[f/2+1].points[side][point])*.5f;
        const float radians=(float(f)*1.5f-360)*.01745329252f,c=std::cos(radians),s=std::sin(radians);
        Matrix grip{{c,s,0,0,-s,c,0,0,0,0,1,0}};
        const auto actual=og::steered_dashboard({{0,0,0,1},{0,0,0}},{settings.pivotX,settings.pivotY,settings.pivotZ},settings.tiltDegrees,radians);
        const auto x=og::spatial_rotate(actual.orientation,{1,0,0}),y=og::spatial_rotate(actual.orientation,{0,1,0}),z=og::spatial_rotate(actual.orientation,{0,0,1});
        const Matrix world{{x.x,y.x,z.x,actual.position.x,x.y,y.y,z.y,actual.position.y,x.z,y.z,z.z,actual.position.z}};
        const auto expectedWorld=steering(settings,radians);
        for(unsigned i=0;i<12;i++)check(std::abs(expectedWorld.v[i]-world.v[i])<1e-5f,"fit sweep matches actual tilted pivot steering");
        const Matrix mount=inverseRigid(world);
            const auto solved=solve(sampled,meta,plan,mount,grip,width);sawLimit|=solved.limited;
            if(!wristsComfortable(solved,sampled,grip)) {
                std::cerr<<"Wrist limit frame "<<f<<" width "<<width<<" seat "<<fit.seatHeight<<"\n";
                for(unsigned side=0;side<2;side++)std::cerr<<std::acos(std::clamp(dot(unit(grip.vector(sampled.points[side][6]-sampled.points[side][2])),unit(solved.wrists[side]-solved.elbows[side])),-1.f,1.f))*57.2957795f<<" ";
                check(false,"wrist flexion stays within 60 degrees");
            }
            for(unsigned side=0;side<2;side++) {
                const auto* p=sampled.points[side];const Vec sh=p[0]*meta.width,el=p[1]*meta.width,wr=p[2]*meta.width;
                check(length(solved.wrists[side]-grip.point(p[2]*width))<1e-6f,"fit preserves grip wrist target");
                check(std::abs(length(solved.elbows[side]-solved.shoulders[side])-meta.upper[side])<.0002f,"upper arm length is anatomical");
                check(std::abs(length(solved.wrists[side]-solved.elbows[side])-meta.lower[side])<.0002f,"forearm length is anatomical");
                check(length(solved.transforms[3+side].point(el)-solved.transforms[5+side].point(el))<.0002f,"elbow seam remains joined");
                check(length(solved.transforms[5+side].point(wr)-solved.transforms[7+side].point(wr))<.0002f,"cuff remains joined to wrist");
                const Vec hip=p[3]*meta.width,knee=p[4]*meta.width,ankle=p[5]*meta.width;
                check(length(solved.transforms[9+side].point(knee)-solved.transforms[11+side].point(knee))<.0002f,"knee seam remains joined");
                check(length(solved.transforms[11+side].point(ankle)-solved.transforms[13+side].point(ankle))<.0002f,"ankle seam remains joined");
                check(std::abs(length(solved.transforms[9+side].point(knee)-solved.transforms[9+side].point(hip))-length(knee-hip)*fit.legLength)<.0002f,"leg length follows fit");
            }
            for(const auto& transform:solved.transforms)for(float value:transform.v)check(std::isfinite(value),"fit transform finite");
            for(unsigned role:{0u,9u,10u,11u,12u,13u,14u}) {
                const auto stable=multiply(mount,reference.transforms[role]);
                for(unsigned i=0;i<12;i++)check(std::abs(stable.v[i]-solved.transforms[role].v[i])<.0001f,"seat and legs stay stationary throughout steering");
            }
        }
    }
    check(sawLimit,"extreme fit reports reach limit");
    const auto narrowPlan=prepare(frames,meta,{},{},.27f),widePlan=prepare(frames,meta,{},{},.36f);
    const auto narrowWheel=solve(frames[120],meta,narrowPlan,Matrix{},Matrix{},.27f),wideWheel=solve(frames[120],meta,widePlan,Matrix{},Matrix{},.36f);
    for(unsigned role:{0u,9u,10u,11u,12u,13u,14u})for(unsigned i=0;i<12;i++)if(i%4!=3)
        check(std::abs(narrowWheel.transforms[role].v[i]-wideWheel.transforms[role].v[i])<1e-6f,"body proportions independent of wheel width");
    DriverFit invalid;invalid.seatHeight=std::numeric_limits<float>::quiet_NaN();invalid.legLength=99;
    const auto clean=sanitized(invalid);check(clean.seatHeight==0 && clean.legLength==1.15f,"nonfinite and out-of-range fit sanitized");
    check(!prepare(frames,meta,{},{},3.f).valid,"physically impossible wheel size cannot render stretched anatomy");
    const DriverMount regressionMount{-.00799165f,.00581979f,-.0173977f,-.711802f};
    const DriverFit regressionFit{-.244535f,-.24798f,.831632f,-5.76017f,1.14265f,-.0438939f};
    const auto regressionPlan=prepare(frames,meta,regressionFit,regressionMount,.359067f);
    check(regressionPlan.valid,"interpolated handover regression has a safe calibration");
    const float regressionAngle=-79.5f*.01745329252f;const auto regressionGrip=gripRotation(regressionAngle);
    const auto regressionFrame=sample(frames,(-79.5f+360)/3);
    const auto regression=solve(regressionFrame,meta,regressionPlan,inverseRigid(steering(regressionMount,regressionAngle)),regressionGrip,.359067f);
    check(wristsComfortable(regression,regressionFrame,regressionGrip),"interpolated handover cannot produce extreme wrist flexion");
}
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
        verifyFit(argv[2]);
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
        DriverFit fit;
        const float wheelWidth=kind==4?.32f:.30f;
        auto pose=[&](float angle,unsigned glove,unsigned suit,bool torso,bool legs,bool custom=false){
            wheelPose=og::steered_dashboard(identityPose,custom?XrVector3f{.025f,.03f,.025f}:XrVector3f{},custom?10.f:0.f,angle);
            const auto x=wheelLocal({1,0,0},true),y=wheelLocal({0,1,0},true),z=wheelLocal({0,0,1},true),p=wheelLocal({});
            const float mount[]={x.x,y.x,z.x,p.x,x.y,y.y,z.y,p.y,x.z,y.z,z.z,p.z};
            return r.selectDriver(dev.get(),0,argv[2],angle,mount,glove,suit,torso,legs,fit,wheelWidth,
                                  custom?DriverMount{.025f,.03f,.025f,10.f}:DriverMount{});
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
            check(changed(first,draw(0))<200u*(desc.Width/512)*(desc.Height/512),"continuous periodic steering after full turn");
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
        fit={};pose(0,0,0,true,true);const auto standardBody=draw(0,true);capture(L"-body-default");
        fit={-.08f,.04f,1.08f,12,.85f,.06f};pose(0,0,0,true,true);
        check(changed(standardBody,draw(0,true))>500,"seated fit changes visible body");capture(L"-body-fitted");
        check(changed(draw(0,true),draw(1,true))>150,"fitted body stereo disparity");
        for(const auto& extreme:{DriverFit{-.3f,-.3f,.75f,-20,.7f,-.15f},DriverFit{.3f,.4f,1.25f,30,1.15f,.15f}}) {
            fit=extreme;
            for(float angle:{0.f,70.f,90.f,180.f,360.f}) {
                check(pose(angle*rad,0,0,true,true,true),"extreme fit selected");
                draw(0,true);draw(1,true);
                capture(std::wstring(fit.seatHeight<0?L"-fit-low-":L"-fit-high-")+std::to_wstring(int(angle)));
            }
        }
        fit={};
        check(!r.selectDriver(dev.get(),0,argv[2],0,identity,0,0,true,true,fit,3.f) && r.driverFitLimited(),"impossible wheel size reports unavailable fit");
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
