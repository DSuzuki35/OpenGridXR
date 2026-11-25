#include "mesh_renderer.h"
#include <windows.h>
#include <wrl/client.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <io.h>
#include <fcntl.h>
#include <cstring>
#include <vector>
#include "mesh_post_shader.h"
#include "led_color.h"
#include "../include/wheel_model.hpp"

namespace ovrlay {
namespace {
struct Header { char magic[8]; uint32_t version,vertices,indices,width,height,reserved; };
static_assert(sizeof(Header)==32);
struct LegacyVertex { float position[3],normal[3],uv[2]; };
static_assert(sizeof(LegacyVertex)==32);
// Group ordinals match the actual SimHub device mappers. Preserve distinct
// fifth rotary LEDs and one/two-LED buttons; never resample them to four colors.
constexpr uint32_t physicalRingCounts[kWheelModels][kRingCount]={
    {4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4},
    {4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4},
    {5,4,5,5,5,2,2,1,2,2,2,2,2,1,2,2,2},
    {4,4,4,4,4,4,4,4,4,4,4,4,4,0,0,0,0},
    {5,4,5,5,5,2,2,1,2,2,2,2,2,1,2,2,2}
};
constexpr char shader[]=R"hlsl(
cbuffer Parameters : register(b0) {
    float4 row0; float4 row1; float4 row2;
    float4 eyeLocal; float4 aperture; float4 fov; float4 rectangle;
    float4 depth; float4 model; float4 clipping;
    float4 effects; float4 body; float4 target; float4 probeUv;
};
cbuffer Lights : register(b1) {
    float4 lightColors[96]; float4 ringCenters[17]; float4 lightOptions;
};
#ifdef DRIVER
Texture2DArray<float4> atlas : register(t0);
#else
Texture2D<float4> atlas : register(t0);
#endif
Texture2D<float4> screenPage : register(t2);
Texture2D<float4> sceneProbe : register(t3);
Texture2DArray<float> sceneDepth : register(t1);
#ifdef DRIVER
Texture2DArray<float4> driverNormal : register(t4);
#endif
SamplerState linearSampler : register(s0);
struct Input { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; float lightGroup : TEXCOORD1; };
#ifndef SHADING_FREQUENCY
#define SHADING_FREQUENCY sample
#endif
struct Varying { float4 position : SV_Position; SHADING_FREQUENCY float2 uv : TEXCOORD0;
                 SHADING_FREQUENCY float3 eye : TEXCOORD1; SHADING_FREQUENCY float3 normal : TEXCOORD2;
                 SHADING_FREQUENCY float2 localXY : TEXCOORD3; nointerpolation uint lightGroup : TEXCOORD4;
                 SHADING_FREQUENCY float localDistance : TEXCOORD5;
#ifdef DRIVER
                 float3 driverPosition : TEXCOORD6; float3 driverView : TEXCOORD7;
#endif
};
Varying vs(Input input) {
    Varying o;
    float3 p=input.position*model.x;
    float w=eyeLocal.z-p.z;
    float2 plane=eyeLocal.z*p.xy-eyeLocal.xy*p.z;
    o.position=float4(2*(plane-aperture.xy*w)/aperture.zw,
        (w-clipping.x)*clipping.y/(clipping.y-clipping.x),w);
    float4 local=float4(p,1);
    o.eye=float3(dot(row0,local),dot(row1,local),dot(row2,local));
    o.uv=input.uv; o.normal=input.normal;
    o.localXY=input.position.xy; o.lightGroup=(uint)input.lightGroup;
    o.localDistance=w;
#ifdef DRIVER
    o.driverPosition=p; o.driverView=eyeLocal.xyz-p;
#endif
    return o;
}
#ifdef DRIVER
cbuffer DriverPose : register(b2) {
    float4 jointRows[384];
    float4 mount[3]; // neutral driver to the rotating wheel, translation in metres
    float4 gripMount[3];
    float4 gloveTint, suitTint;
};
struct SkinInput {
    float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; float part : TEXCOORD1;
    uint4 joints : BLENDINDICES; float4 weights : BLENDWEIGHT; float followGrip : TEXCOORD2;
};
Varying skinVS(SkinInput input) {
    float3 p=0,n=0;
    [unroll] for(uint j=0;j<4;j++) {
        uint row=input.joints[j]*3;
        [unroll] for(uint axis=0;axis<3;axis++) {
            float4 r=jointRows[row+axis];
            p[axis]+=input.weights[j]*dot(r,float4(input.position,1));
            n[axis]+=input.weights[j]*dot(r.xyz,input.normal);
        }
    }
    Input v;
    float4 m=float4(p*model.x,1);
    float3 shoulder=float3(dot(mount[0],m),dot(mount[1],m),dot(mount[2],m));
    float3 grip=float3(dot(gripMount[0],m),dot(gripMount[1],m),dot(gripMount[2],m));
    v.position=lerp(shoulder,grip,input.followGrip)/model.x;
    v.normal=n; // illumination is stationary in the driver's neutral frame
    v.uv=input.uv; v.lightGroup=input.part;
    Varying o=vs(v);
    o.driverPosition=p*model.x;
    float3 localEye=eyeLocal.xyz-float3(mount[0].w,mount[1].w,mount[2].w);
    float3 neutralEye=localEye.x*mount[0].xyz+localEye.y*mount[1].xyz+localEye.z*mount[2].xyz;
    o.driverView=neutralEye-o.driverPosition;
    return o;
}

// Derivatives follow the deformed surface and reflected hand UVs. No tangent
// stream, compute pass or extra geometry pass is needed.
float3 driverTextureUv(Varying input) {
    float cloth=input.lightGroup!=0?1:0;
    return suitTint.w>.5?float3(input.uv.x*2-cloth,input.uv.y,cloth):float3(input.uv,0);
}
float3 detailNormal(Varying input,float3 n) {
    float3 dx=ddx(input.driverPosition),dy=ddy(input.driverPosition);
    float2 ux=ddx(input.uv),uy=ddy(input.uv);
    float determinant=ux.x*uy.y-ux.y*uy.x;
    float3 t=dx*uy.y-dy*ux.y,b=dy*ux.x-dx*uy.x;
    float inv=rsqrt(max(max(dot(t,t),dot(b,b)),1e-16));
    t*=inv*(determinant<0?-1:1);b*=inv*(determinant<0?-1:1);
    // Atlas packing changes UV aspect ratio, not the surface's normal strength.
    // Use an orthonormal frame so cloth/glove detail does not flatten on one axis.
    t-=n*dot(n,t);t*=rsqrt(max(dot(t,t),1e-16));
    float3 perpendicular=cross(n,t);
    b=perpendicular*(dot(perpendicular,b)<0?-1:1);
    float3 detail=driverNormal.Sample(linearSampler,driverTextureUv(input)).xyz*2-1;
    if(input.lightGroup!=0 && suitTint.w<.5) {
        // Fine woven fabric: fade subpixel threads to their mean in VR.
        float2 thread=input.uv*900;
        float fade=saturate(1-max(fwidth(thread.x),fwidth(thread.y)));
        detail=normalize(float3(.12*sin(thread.x*6.2831853)*fade,
                               .12*sin(thread.y*6.2831853)*fade,1));
    }
    return abs(determinant)>1e-12?normalize(t*detail.x+b*detail.y+n*detail.z):n;
}
#endif
float3 bodyColor(float3 rgb) {
    if(any(body!=1)) {
        float3 srgb=lerp(1.055*pow(max(rgb,0),1/2.4)-.055,rgb*12.92,rgb<=.0031308);
        srgb=(srgb*body.x-.5)*body.y+.5;
        srgb=lerp(dot(srgb,float3(.2126,.7152,.0722)).xxx,srgb,body.z);
        srgb=pow(saturate(srgb),1/body.w);
        rgb=lerp(pow((srgb+.055)/1.055,2.4),srgb/12.92,srgb<=.04045);
    }
    return rgb;
}
float3 casingColor(float3 diffuse,float2 localXY) {
    float3 color=bodyColor(diffuse);
    // Both eyes use the same physical point on the model to address the probe.
    [branch] if(eyeLocal.w>0) {
        float3 ambient=sceneProbe.SampleLevel(linearSampler,localXY*probeUv.xy+probeUv.zw,0).rgb;
        float luma=dot(ambient,float3(.2126,.7152,.0722));
        float3 gain=clamp(.40+1.5*lerp(luma,ambient,.65),.40,1.55);
        color*=lerp(1,gain,eyeLocal.w);
    }
    return color;
}
float3 groupMean(uint ring) {
    uint first=(uint)ringCenters[ring].z,count=(uint)ringCenters[ring].w;
    float3 color=0;[unroll] for(uint i=0;i<5;++i) if(i<count) color+=lightColors[first+i].rgb;
    return color/max(count,1);
}
float4 shaded(Varying input,out float3 emission) {
#ifdef DRIVER
    float4 rgba=atlas.Sample(linearSampler,driverTextureUv(input));
#else
    float4 rgba=atlas.Sample(linearSampler,input.uv);
#endif
    emission=0;
    // Display UVs address the dashboard page rather than the model atlas.
#ifdef DRIVER
    if(input.lightGroup==0) clip(rgba.a-.02);
#else
    if(input.lightGroup!=200) clip(rgba.a-.02);
#endif
    // Broad neutral cockpit illumination keeps controls legible without PBR.
    float3 n=normalize(input.normal);
    float lighting=.42+.43*saturate(dot(n,normalize(float3(-.3,.6,1))))
        +.15*saturate(dot(n,normalize(float3(.7,-.3,.4))));
#ifdef DRIVER
    n=detailNormal(input,n);
    float3 view=normalize(input.driverView);
    float3 key=normalize(float3(-.35,.65,1));
    float diffuse=.24+.58*saturate(dot(n,key))+.18*saturate(dot(n,normalize(float3(.65,.2,.3))));
    float sheen=pow(saturate(1-dot(n,view)),4);
    if(input.lightGroup==0) {
        float chroma=max(0,rgba.r-max(rgba.g,rgba.b));
        rgba.rgb=saturate(rgba.rgb-float3(chroma,0,0)+gloveTint.rgb*chroma);
        // Broad dielectric leather/textile highlight, never a glowing color.
        float specular=.08*pow(saturate(dot(n,normalize(key+view))),24);
        rgba.rgb=rgba.rgb*diffuse+specular+.018*sheen;
    } else {
        if(suitTint.w>.5) {
            // Original neutral cloth atlas: RGB holds panels/weave/crease AO,
            // alpha preserves the fine contrasting thread under every theme.
            float chroma=max(suitTint.r,max(suitTint.g,suitTint.b))-min(suitTint.r,min(suitTint.g,suitTint.b));
            float3 tint=lerp(suitTint.rgb+chroma*.10,1,rgba.a);
            rgba=float4(rgba.rgb*tint*diffuse+.012*sheen,1);
        } else {
            float2 thread=input.uv*900;
            float fade=saturate(1-max(fwidth(thread.x),fwidth(thread.y)));
            float weave=1+.035*sin(thread.x*6.2831853)*sin(thread.y*6.2831853)*fade;
            rgba=float4(suitTint.rgb*(.22*diffuse*weave+.018*sheen),1);
        }
    }
    rgba.rgb=casingColor(rgba.rgb,input.localXY);
#else
    if(input.lightGroup==200) {
        clip(clipping.w-.5);
        float4 page=screenPage.Sample(linearSampler,input.uv);
        rgba=float4(page.rgb,1);
    } else if(input.lightGroup>200) {
        if(lightOptions.x>.5) rgba.rgb=lightColors[input.lightGroup-201].rgb;
    } else if(input.lightGroup>=101) {
        float3 original=rgba.rgb;
        if(lightOptions.x>.5) {
            uint ring=(input.lightGroup-101)/4;
            float3 color=groupMean(ring)*effects.x;
            // The authored black/white sticker is opaque. Its white ink, not
            // its full rectangle or a body-filter pedestal, carries emission.
            float ink=saturate((min(original.r,min(original.g,original.b))-.003)/.987);
            // Gray antialiased texels represent ink coverage over black, not
            // a gray substrate. Stale/black LED data must not retain gray ink.
            rgba.rgb=casingColor(float3(0,0,0),input.localXY)*(1-ink)+color*ink;emission=color*ink;
        } else rgba.rgb=casingColor(original*lighting,input.localXY);
    } else if(lightOptions.x>.5 && input.lightGroup>0) {
        uint group=input.lightGroup;
        float3 color;
        if(group<=65) {
            uint ring=(group-1)/4;
            if(lightOptions.w>.5) color=groupMean(ring);
            else {
                uint first=ring*4;
                float2 direction=input.localXY-ringCenters[ring].xy;
                float angle=dot(direction,direction)>1e-12 ? atan2(direction.x,direction.y)*(2/3.141592653589793) : 0;
                float phase=angle+4+lightOptions.y;
                uint sector=(uint)floor(phase);
                color=lerp(lightColors[first+(sector%4)].rgb,
                           lightColors[first+((sector+1)%4)].rgb,frac(phase));
            }
        } else color=lightColors[(uint)lightOptions.z+group-69].rgb;
        // Classified shoulders/diffusers exclude dark caps and icon geometry.
        // Preserve atlas alpha while removing the baked hue and diffuse shading:
        // SimHub's hardware color already includes the requested brightness.
        rgba.rgb=color*(input.lightGroup>=69 ? effects.w : effects.x);emission=rgba.rgb;
    } else if(input.lightGroup) rgba.rgb*=lighting;
    else rgba.rgb=casingColor(rgba.rgb*lighting,input.localXY);
    #endif
    rgba.a*=model.y;
    emission*=rgba.a;
    if(clipping.z>.5) {
        float forward=-input.eye.z;
        if(forward>0) {
            float2 uv=float2((input.eye.x/forward-fov.x)/(fov.y-fov.x),
                            (fov.z-input.eye.y/forward)/(fov.z-fov.w));
            if(all(uv>=0) && all(uv<1)) {
                int2 coordinate=int2(rectangle.xy+floor(uv*rectangle.zw));
                float stored=sceneDepth.Load(int4(coordinate,int(model.w),0));
                if(stored>=depth.x && stored<=depth.y) {
                    float t=(stored-depth.x)/(depth.y-depth.x);
                    float reciprocal=lerp(depth.z,depth.w,t);
                    if(reciprocal>0 && 1/reciprocal<forward-model.z) discard;
                }
            }
        }
    }
    return rgba;
}
struct Color {float4 color:SV_Target0;float2 occlusion:SV_Target1;};
Color ps(Varying input) {Color o;float3 unused;o.color=shaded(input,unused);o.occlusion=float2(input.localDistance,o.color.a);o.color.rgb=saturate(o.color.rgb)*o.color.a;return o;}
struct Emission {float4 color:SV_Target0;float4 light:SV_Target1;float2 occlusion:SV_Target2;};
Emission psEmission(Varying input) {
    Emission o;float3 radiance;o.color=shaded(input,radiance);
    o.light=float4(radiance,dot(radiance,float3(.2126,.7152,.0722))*input.localDistance);
    o.occlusion=float2(input.localDistance,o.color.a);
    // Resolve display-range surface color by coverage. Full HDR radiance stays
    // in the separate emission target, so gain cannot erase MSAA edge coverage.
    o.color.rgb=saturate(o.color.rgb)*o.color.a;
    return o;
}
)hlsl";
}

#include "driver_asset.inl"

bool MeshTargets::prepare(ID3D11Device* device,uint32_t w,uint32_t h,bool bloom,bool masks,uint32_t sampleCount) noexcept {
    if(width!=w || height!=h || samples!=sampleCount) {
        depth.reset();depthView.reset();color.reset();emission.reset();
        colorSource.reset();emissionSource.reset();colorTarget.reset();emissionTarget.reset();
        sampleColor.reset();sampleEmission.reset();sampleOcclusion.reset();sampleColorTarget.reset();sampleEmissionTarget.reset();sampleOcclusionTarget.reset();
        for(int i=0;i<2;++i) {blur[i].reset();blurSource[i].reset();blurTarget[i].reset();occlusion[i].reset();occlusionSource[i].reset();occlusionTarget[i].reset();}
        width=height=samples=0;
    }
    if(!depthView) {
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;
        d.Format=DXGI_FORMAT_R32_TYPELESS;d.SampleDesc.Count=sampleCount;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        D3D11_DEPTH_STENCIL_VIEW_DESC view{};view.Format=DXGI_FORMAT_D32_FLOAT;view.ViewDimension=sampleCount>1?D3D11_DSV_DIMENSION_TEXTURE2DMS:D3D11_DSV_DIMENSION_TEXTURE2D;
        if(FAILED(device->CreateTexture2D(&d,nullptr,depth.put())) || FAILED(device->CreateDepthStencilView(depth.get(),&view,depthView.put()))) return false;
        width=w;height=h;samples=sampleCount;
    }
    auto create=[&](uint32_t x,uint32_t y,DXGI_FORMAT format,uint32_t count,Com<ID3D11Texture2D>& texture,Com<ID3D11ShaderResourceView>* source,Com<ID3D11RenderTargetView>& view) {
        if(view) return true;
        D3D11_TEXTURE2D_DESC d{};d.Width=x;d.Height=y;d.MipLevels=d.ArraySize=1;d.Format=format;
        d.SampleDesc.Count=count;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET|(source?D3D11_BIND_SHADER_RESOURCE:0);
        return SUCCEEDED(device->CreateTexture2D(&d,nullptr,texture.put())) && (!source || SUCCEEDED(device->CreateShaderResourceView(texture.get(),nullptr,source->put()))) &&
               SUCCEEDED(device->CreateRenderTargetView(texture.get(),nullptr,view.put()));
    };
    constexpr auto rgba=DXGI_FORMAT_R16G16B16A16_FLOAT,rg=DXGI_FORMAT_R32G32_FLOAT;
    if(!create(w,h,rgba,1,color,&colorSource,colorTarget) || (samples>1 && !create(w,h,rgba,samples,sampleColor,nullptr,sampleColorTarget))) return false;
    if(masks) {
        for(int eye=0;eye<2;++eye) if(!create(w,h,rg,1,occlusion[eye],&occlusionSource[eye],occlusionTarget[eye])) return false;
        if(samples>1 && !create(w,h,rg,samples,sampleOcclusion,nullptr,sampleOcclusionTarget)) return false;
    }
    if(!bloom) return true;
    return create(w,h,rgba,1,emission,&emissionSource,emissionTarget) &&
           (samples==1 || create(w,h,rgba,samples,sampleEmission,nullptr,sampleEmissionTarget)) &&
           create((w+3)/4,(h+3)/4,rgba,1,blur[0],&blurSource[0],blurTarget[0]) && create((w+3)/4,(h+3)/4,rgba,1,blur[1],&blurSource[1],blurTarget[1]);
}

bool MeshAsset::initialize(ID3D11Device* device,const wchar_t* path,uint32_t kind) noexcept {
    if(!device || !path) return false;
    try {
        // Keep reading this immutable file if the desktop app atomically installs
        // an updated model; Windows needs explicit delete-sharing for rename.
        HANDLE handle=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,
            OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
        if(handle==INVALID_HANDLE_VALUE)return false;
        const int fd=_open_osfhandle(reinterpret_cast<intptr_t>(handle),_O_RDONLY|_O_BINARY);
        if(fd<0){CloseHandle(handle);return false;}
        FILE* file=_fdopen(fd,"rb");
        if(!file){_close(fd);return false;}
        struct Close { FILE* f; ~Close(){std::fclose(f);} } close{file};
        if(_fseeki64(file,0,SEEK_END)!=0) return false;
        auto bytes=_ftelli64(file);
        if(bytes<32 || bytes>128*1024*1024 || _fseeki64(file,0,SEEK_SET)!=0) return false;
        Header h{}; if(std::fread(&h,1,sizeof(h),file)!=sizeof(h)) return false;
        if(std::memcmp(h.magic,"AGMESH1\0",8) || (h.version!=1 && h.version!=2 && h.version!=3 && h.version!=4 && h.version!=5) || h.reserved ||
           !h.vertices || h.vertices>1000000 || !h.indices || h.indices>3000000 || h.indices%3 ||
           !h.width || !h.height || h.width>4096 || h.height>4096) return false;
        uint64_t expected=32+uint64_t(h.vertices)*(h.version==1?sizeof(LegacyVertex):sizeof(MeshVertex))+uint64_t(h.indices)*4+uint64_t(h.width)*h.height*4;
        if(expected!=uint64_t(bytes)) return false;
        std::vector<MeshVertex> v(h.vertices); std::vector<uint32_t> ix(h.indices);
        std::vector<uint8_t> pixels(size_t(h.width)*h.height*4);
        if(h.version==1) {
            std::vector<LegacyVertex> legacy(h.vertices);
            if(std::fread(legacy.data(),sizeof(LegacyVertex),legacy.size(),file)!=legacy.size()) return false;
            for(size_t i=0;i<legacy.size();++i) std::memcpy(&v[i],&legacy[i],sizeof(LegacyVertex));
        } else if(std::fread(v.data(),sizeof(MeshVertex),v.size(),file)!=v.size()) return false;
        if(std::fread(ix.data(),4,ix.size(),file)!=ix.size() || std::fread(pixels.data(),1,pixels.size(),file)!=pixels.size()) return false;
        float ringMin[kRingCount][2],ringMax[kRingCount][2]; bool ringUsed[kRingCount]{};
        for(uint32_t r=0;r<kRingCount;++r) for(int a=0;a<2;++a) {ringMin[r][a]=1e10f;ringMax[r][a]=-1e10f;}
        for(int axis=0;axis<3;++axis) { minimum[axis]=1e10f; maximum[axis]=-1e10f; }
        for(const auto& p:v) {
            float norm=0;
            for(int axis=0;axis<3;++axis) {
                if(!std::isfinite(p.position[axis]) || std::fabs(p.position[axis])>10 || !std::isfinite(p.normal[axis])) return false;
                norm+=p.normal[axis]*p.normal[axis];
                minimum[axis]=std::min(minimum[axis],p.position[axis]); maximum[axis]=std::max(maximum[axis],p.position[axis]);
            }
            if(norm<.25f || norm>4 || !std::isfinite(norm) || !std::isfinite(p.uv[0]) || !std::isfinite(p.uv[1]) ||
                p.uv[0]<0 || p.uv[0]>1 || p.uv[1]<0 || p.uv[1]>1) return false;
            if(!std::isfinite(p.lightGroup) || p.lightGroup<0 || p.lightGroup>296 || p.lightGroup!=std::floor(p.lightGroup)) return false;
            uint32_t group=uint32_t(p.lightGroup);
            taggedLights=taggedLights || group!=0;
            bool icon=h.version>=3 && group>=(h.version>=4?101u:121u) && group<=165 && (group-101)%4==0;
            bool custom = h.version==5 && group>=200 && group<=296;
            if(group && !icon && !custom && !((group>=69 && group<=(h.version>=4?kModelLightCount:kLightCount)) || (group<=65 && (group-1)%4==0))) return false;
            if(group && group<=65) {
                uint32_t ring=(group-1)/4; ringUsed[ring]=true;
                for(int a=0;a<2;++a) {ringMin[ring][a]=std::min(ringMin[ring][a],p.position[a]);ringMax[ring][a]=std::max(ringMax[ring][a],p.position[a]);}
            }
        }
        if(std::fabs(maximum[0]-minimum[0]-1)>.01f) return false;
        const float height=maximum[1]-minimum[1];
        probeUv[0]=1.f/(maximum[0]-minimum[0]);
        probeUv[1]=height>.0001f ? -1.f/height : 0;
        probeUv[2]=-minimum[0]*probeUv[0];
        probeUv[3]=height>.0001f ? -maximum[1]*probeUv[1] : .5f;
        for(uint32_t index:ix) if(index>=h.vertices) return false;
        for(size_t i=0;i<ix.size();i+=3) if(v[ix[i]].lightGroup!=v[ix[i+1]].lightGroup || v[ix[i]].lightGroup!=v[ix[i+2]].lightGroup) return false;
        // Fitted display surfaces measured on the three GSI models in oVRlay.
        // The small forward separation prevents z-fighting with the black glass.
        struct Screen {float x,y,z,w,h;};
        constexpr Screen screens[]={{.000067566f,.1101643f,.14745412f,.31625782f,.19145731f},{},{},
            {.00087239f,.10100077f,.13489926f,.35551293f,.19981463f},
            {-.01335391f,.10472478f,.12867644f,.34169041f,.20149369f}};
        Screen scr{};
        if(kind>=1 && kind<=5) scr=screens[kind-1];
        else if(kind==0 && std::none_of(v.begin(),v.end(),[](const auto& vertex){return vertex.lightGroup==200;})) {
            const auto h=maximum[1]-minimum[1];
            scr={0,minimum[1]+.65f*h,maximum[2],.32f,std::min(.18f,h*.4f)};
        }
        if(scr.w>0 && scr.h>0) {
            auto first=uint32_t(v.size());
            for(unsigned j=0;j<4;++j) {
                float x=float(j&1),y=float(j>>1);
                v.push_back({{scr.x+(x-.5f)*scr.w,scr.y+(.5f-y)*scr.h,scr.z+.00167f},
                    {0,0,1},{x,y},200});
            }
            for(unsigned j:{0u,1u,2u,1u,3u,2u}) ix.push_back(first+j);
        }
        // Stable batches preserve source ordering within each class. Keep LED
        // geometry and authored icon ink at sample frequency; ordinary casing
        // needs MSAA coverage but only one centroid texture/light evaluation.
        std::vector<uint32_t> ordered;ordered.reserve(ix.size());
        const auto centroidShading=[&](size_t i) {
            const float group=v[ix[i]].lightGroup;
            return group==0 || group==200; // casing or dashboard display
        };
        for(size_t i=0;i<ix.size();i+=3) if(centroidShading(i)) ordered.insert(ordered.end(),ix.begin()+i,ix.begin()+i+3);
        bodyIndexCount=uint32_t(ordered.size());
        for(size_t i=0;i<ix.size();i+=3) if(!centroidShading(i)) ordered.insert(ordered.end(),ix.begin()+i,ix.begin()+i+3);
        ix.swap(ordered);
        for(uint32_t ring=0;ring<kRingCount;++ring) for(int a=0;a<2;++a)
            centers[ring][a]=ringUsed[ring] ? (ringMin[ring][a]+ringMax[ring][a])*.5f : 0;
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=UINT(v.size()*sizeof(MeshVertex));bd.Usage=D3D11_USAGE_IMMUTABLE;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{v.data(),0,0};
        if(FAILED(device->CreateBuffer(&bd,&initial,vertices.put()))) return false;
        bd.ByteWidth=UINT(ix.size()*4);bd.BindFlags=D3D11_BIND_INDEX_BUFFER;initial.pSysMem=ix.data();
        if(FAILED(device->CreateBuffer(&bd,&initial,indices.put()))) return false;
        D3D11_TEXTURE2D_DESC td{};td.Width=h.width;td.Height=h.height;td.MipLevels=td.ArraySize=1;
        td.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_IMMUTABLE;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        initial={pixels.data(),h.width*4,0};
        if(FAILED(device->CreateTexture2D(&td,&initial,atlas.put())) || FAILED(device->CreateShaderResourceView(atlas.get(),nullptr,atlasView.put()))) return false;
        indexCount=uint32_t(ix.size());return true;
    } catch(...) {indexCount=0;return false;}
}

bool MeshRenderer::initializePipeline(ID3D11Device* device) noexcept {
    if(pipelineReady) return true;
    if(!device || device->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0) return false;
    try {
        wchar_t compilerPath[MAX_PATH]{}; UINT count=GetSystemDirectoryW(compilerPath,MAX_PATH);
        constexpr wchar_t suffix[]=L"\\d3dcompiler_47.dll";
        if(!count || count+sizeof(suffix)/sizeof(wchar_t)>MAX_PATH) return false;
        std::memcpy(compilerPath+count,suffix,sizeof(suffix));
        HMODULE library=LoadLibraryW(compilerPath); if(!library) return false;
        using Compile=HRESULT(WINAPI*)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
        Compile compile{}; FARPROC address=GetProcAddress(library,"D3DCompile"); std::memcpy(&compile,&address,sizeof(compile));
        Com<ID3DBlob> vs,ps,errors;
        bool ok=compile && SUCCEEDED(compile(shader,sizeof(shader)-1,"oVRlayMesh",nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,vs.put(),errors.put()));
        ok=ok && SUCCEEDED(compile(shader,sizeof(shader)-1,"oVRlayMesh",nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,ps.put(),errors.put()));
        D3D11_INPUT_ELEMENT_DESC elements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0}};
        ok=ok && SUCCEEDED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,vertex.put())) &&
           SUCCEEDED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,pixel.put())) &&
           SUCCEEDED(device->CreateInputLayout(elements,4,vs->GetBufferPointer(),vs->GetBufferSize(),layout.put()));
        auto buildPixel=[&](const char* text,size_t length,const char* entry,Com<ID3D11PixelShader>& output) {
            ps.reset();errors.reset();
            return compile && SUCCEEDED(compile(text,length,"oVRlayBloom",nullptr,nullptr,entry,"ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,ps.put(),errors.put())) &&
                   SUCCEEDED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,output.put()));
        };
        ok=ok && buildPixel(shader,sizeof(shader)-1,"psEmission",emissionPixel);
        D3D_SHADER_MACRO bodyMacros[]={{"SHADING_FREQUENCY","centroid"},{nullptr,nullptr}};
        auto buildBody=[&](const char* entry,Com<ID3D11PixelShader>& output) {
            ps.reset();errors.reset();
            return compile && SUCCEEDED(compile(shader,sizeof(shader)-1,"oVRlayBody",bodyMacros,nullptr,entry,"ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,ps.put(),errors.put())) &&
                SUCCEEDED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,output.put()));
        };
        ok=ok && buildBody("ps",bodyPixel) && buildBody("psEmission",bodyEmissionPixel);
        vs.reset();errors.reset();
        ok=ok && SUCCEEDED(compile(meshPostShader,sizeof(meshPostShader)-1,"oVRlayBloom",nullptr,nullptr,"postVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,vs.put(),errors.put())) &&
           SUCCEEDED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,postVertex.put()));
        ok=ok && buildPixel(meshPostShader,sizeof(meshPostShader)-1,"downsample",downsamplePixel) &&
           buildPixel(meshPostShader,sizeof(meshPostShader)-1,"horizontal",horizontalPixel) &&
           buildPixel(meshPostShader,sizeof(meshPostShader)-1,"vertical",verticalPixel) &&
           buildPixel(meshPostShader,sizeof(meshPostShader)-1,"composite",compositePixel) && buildPixel(meshPostShader,sizeof(meshPostShader)-1,"copyColor",copyPixel);
        vs.reset(); ps.reset(); errors.reset(); FreeLibrary(library);
        if(!ok) return false;
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(MeshEyeParameters);bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if(FAILED(device->CreateBuffer(&bd,nullptr,constants.put()))) return false;
        bd.ByteWidth=sizeof(MeshLightsParameters);
        if(FAILED(device->CreateBuffer(&bd,nullptr,lightConstants.put()))) return false;
        D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD=D3D11_FLOAT32_MAX;
        samples=4;
        for(auto format:{DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_D32_FLOAT,DXGI_FORMAT_R32G32_FLOAT}) {
            UINT quality=0;if(FAILED(device->CheckMultisampleQualityLevels(format,4,&quality)) || !quality) samples=1;
            if(format!=DXGI_FORMAT_D32_FLOAT) {
                UINT support=0;constexpr UINT required=D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE|D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET;
                if(FAILED(device->CheckFormatSupport(format,&support)) || (support&required)!=required) samples=1;
            }
        }
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;rd.MultisampleEnable=TRUE;
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable=TRUE; dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc=D3D11_COMPARISON_LESS;
        ok=SUCCEEDED(device->CreateSamplerState(&sd,sampler.put())) && SUCCEEDED(device->CreateRasterizerState(&rd,rasterizer.put())) &&
           SUCCEEDED(device->CreateDepthStencilState(&dd,depthState.put())) && SUCCEEDED(device->CreateDeferredContext(0,deferred.put()));
        dd.DepthEnable=FALSE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
        ok=ok && SUCCEEDED(device->CreateDepthStencilState(&dd,noDepthState.put()));
        pipelineReady=ok;
        return ok;
    } catch(...) { return false; }
}


bool MeshRenderer::selectModel(ID3D11Device* device,unsigned slot,const std::wstring& path,unsigned kind,unsigned deviceKind) noexcept {
    driver=nullptr;
    if(slot>=15 || deviceKind>6 || kind>og::wheel_names.size() || !pipelineReady) return false;
    try {
        active=models.select(slot,path,[&] {
            return std::async(std::launch::async,[retained=Microsoft::WRL::ComPtr<ID3D11Device>(device),path,kind] {
                auto asset=std::make_shared<MeshAsset>();
                if(!asset->initialize(retained.Get(),path.c_str(),kind)) asset.reset();
                return asset;
            });
        });
    } catch(...) {active=nullptr;}
    if(!active)return false;
    std::memcpy(lights.centers,active->centers,sizeof(lights.centers));
    // Artura uses individually tagged physical pixels (groups 201+), so it
    // never reads the GSI rotary-ring allocation table.
    kind=deviceKind && deviceKind<=kWheelModels?deviceKind:1;
    uint32_t first=0;
    for(uint32_t ring=0;ring<kRingCount;++ring) {lights.centers[ring][2]=float(first);lights.centers[ring][3]=float(physicalRingCounts[kind-1][ring]);first+=physicalRingCounts[kind-1][ring];}
    lights.options[2]=float(first);
    // Other models' physical group/index allocations are verified, but their
    // individual angular wiring is not. Show each whole control's exact mean
    // instead of assigning unverified per-LED positions. P1 keeps its sectors.
    lights.options[3]=kind==1?0.f:1.f;return true;
}

void MeshRenderer::setLights(bool enabled,const ModelLightsFrame& frame,uint64_t now) noexcept {
    lightsNeedUpload=true;
    coloredLights=false;
    lights.options[0]=enabled?1.0f:0.0f;
    lights.options[1]=frame.ring_rotation<=3?float(frame.ring_rotation):0;
    std::memset(lights.colors,0,sizeof(lights.colors));
    if(!enabled || frame.count!=kModelLightCount || frame.ring_rotation>3 || !frame.sampled_tick_ms ||
       frame.sampled_tick_ms>now || now-frame.sampled_tick_ms>1000) return;
    for(uint32_t i=0;i<kModelLightCount;++i) for(int channel=0;channel<3;++channel) {
        const auto value=frame.rgba[i][channel];
        coloredLights=coloredLights || value>0;
        lights.colors[i][channel]=linearLedChannel(value);
    }
}

bool MeshRenderer::aperture(MeshEyeParameters& p,const float* visiblePlane) const noexcept {
    if(!ready() || !std::isfinite(p.model[0]) || p.model[0]<.001f || p.model[0]>3) return false;
    for(float x:p.eyeLocal) if(!std::isfinite(x)) return false;
    float scale=p.model[0],eyeZ=p.eyeLocal[2];
    // A front aperture cannot represent a camera on/behind its plane or inside
    // the wheel. Hide both eyes instead of emitting clipped or reversed stereo.
    if(eyeZ<.02f || eyeZ-active->maximum[2]*scale<.02f || eyeZ>100) return false;
    float lo[2]={1e10f,1e10f},hi[2]={-1e10f,-1e10f};
    for(int corner=0;corner<8;++corner) {
        float v[3]; for(int a=0;a<3;++a) v[a]=((corner&(1<<a))?active->maximum[a]:active->minimum[a])*scale;
        for(int a=0;a<2;++a) {
            float q=(eyeZ*v[a]-p.eyeLocal[a]*v[2])/(eyeZ-v[2]);
            if(!std::isfinite(q)) return false;
            lo[a]=std::min(lo[a],q); hi[a]=std::max(hi[a],q);
        }
    }
    float farDistance=eyeZ-active->minimum[2]*scale;
    if(driver) {
        // Transform the pose bounds into the rotating wheel's frame before projecting.
        // Clipping the twelve box edges handles shoulders that extend through the eye plane.
        const auto& d=*driver;
        auto includeBounds=[&](unsigned part,const float* transform) {
            float corners[8][3]{};
            for(unsigned c=0;c<8;c++) {
                float point[3];
                for(unsigned a=0;a<3;a++)point[a]=scale*d.currentBounds[part][a+((c>>a)&1)*3];
                for(unsigned a=0;a<3;a++) {
                    const float* r=transform+4*a;
                    corners[c][a]=r[0]*point[0]+r[1]*point[1]+r[2]*point[2]+r[3];
                }
                farDistance=std::max(farDistance,eyeZ-corners[c][2]);
            }
            constexpr float nearDistance=.01f;
            // Off-screen geometry need not consume pixels. If a view ray runs parallel
            // to the mount, use the layer's finite four-metre peripheral limit.
            constexpr float reach=4.f;
            auto include=[&](const float* v) {
                for(unsigned a=0;a<2;a++) {
                    const float q=std::clamp((eyeZ*v[a]-p.eyeLocal[a]*v[2])/std::max(nearDistance,eyeZ-v[2]),
                        visiblePlane?visiblePlane[a]:-reach,visiblePlane?visiblePlane[a+2]:reach);
                    lo[a]=std::min(lo[a],q);hi[a]=std::max(hi[a],q);
                }
            };
            for(unsigned c=0;c<8;c++) {
                const float* v=corners[c];
                if(eyeZ-v[2]>=nearDistance)include(v);
                for(unsigned a=0;a<3;a++)if(!(c&(1<<a))) {
                    const float* w=corners[c|(1<<a)];
                    if((eyeZ-v[2]>=nearDistance)==(eyeZ-w[2]>=nearDistance))continue;
                    const float t=(eyeZ-nearDistance-v[2])/(w[2]-v[2]);
                    float hit[]={v[0]+t*(w[0]-v[0]),v[1]+t*(w[1]-v[1]),eyeZ-nearDistance};
                    include(hit);
                }
            }
        };
        // Blended sleeve positions are inside the convex hull of both transforms.
        for(unsigned part=0;part<4;part++) {
            if(!d.counts[part] || (part==2 && !d.torso) || (part==3 && !d.legs))continue;
            includeBounds(part,part==0?d.pose.gripMount:d.pose.mount);
            if(part==1)includeBounds(part,d.pose.gripMount);
        }
    }
    for(int a=0;a<2;++a) {
        p.aperture[a]=(lo[a]+hi[a])*.5f; p.aperture[a+2]=(hi[a]-lo[a])*1.04f+.001f;
        if(p.aperture[a+2]<.001f || p.aperture[a+2]>10) return false;
    }
    p.clip[0]=driver?.01f:.001f; p.clip[1]=std::max(.1f,farDistance*1.1f);
    return true;
}

bool MeshRenderer::render(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* target,DXGI_FORMAT,
                          ID3D11ShaderResourceView* depth,const MeshEyeParameters& input,ID3D11RenderTargetView* output,ID3D11ShaderResourceView* screen,uint32_t eye,
                          ID3D11ShaderResourceView* sceneProbe,float ambientLight) noexcept {
    if(!ready() || !device || !context || !target || !output || eye>1 || (input.clip[2]>.5f && !depth)) return false;
    D3D11_TEXTURE2D_DESC td{}; target->GetDesc(&td);
    if(!td.Width || !td.Height || td.Width>2048 || td.Height>2048 || td.ArraySize!=1 || td.SampleDesc.Count!=1) return false;
    bool bloom=active->taggedLights && (input.effects[0]>0 || input.effects[3]>0) && input.effects[1]>0 && input.effects[2]>0 && lights.options[0]>.5f && coloredLights;
    // A driver and an ordinary wheel can be visible together. Retain their
    // different sized intermediates rather than reallocating on every slot.
    auto& cache=driver?*driverTargets:targets;
    constexpr bool masks=false;
    if(!cache.prepare(device,td.Width,td.Height,bloom,masks,samples)) {
        if(samples==1 || !cache.prepare(device,td.Width,td.Height,bloom,masks,1)) return false;
        samples=1; // A quality allocation failure must not hide an otherwise renderable wheel.
    }
    MeshEyeParameters p=input;p.target[0]=float(td.Width);p.target[1]=float(td.Height);
    p.target[2]=float((td.Width+3)/4);p.target[3]=float((td.Height+3)/4);
    p.eyeLocal[3]=0;
    if(sceneProbe && std::isfinite(ambientLight) && ambientLight>0) {
        Com<ID3D11Device> probeDevice;
        sceneProbe->GetDevice(probeDevice.put());
        if(probeDevice.get()==device) {
            p.eyeLocal[3]=std::min(ambientLight,100.f)*.01f;
            std::copy_n(active->probeUv,4,p.probeUv);
        }
    }
    deferred->ClearState(); deferred->SetPredication(nullptr,FALSE);
    deferred->UpdateSubresource(constants.get(),0,nullptr,&p,0,0);
    if(lightsNeedUpload) deferred->UpdateSubresource(lightConstants.get(),0,nullptr,&lights,0,0);
    float transparent[4]{};
    auto colorTarget=cache.samples>1?cache.sampleColorTarget.get():cache.colorTarget.get();
    auto emissionTarget=cache.samples>1?cache.sampleEmissionTarget.get():cache.emissionTarget.get();
    auto occlusionTarget=cache.samples>1?cache.sampleOcclusionTarget.get():cache.occlusionTarget[eye].get();
    deferred->ClearRenderTargetView(colorTarget,transparent);
    if(bloom) deferred->ClearRenderTargetView(emissionTarget,transparent);
    if(masks) deferred->ClearRenderTargetView(occlusionTarget,transparent);
    deferred->ClearDepthStencilView(cache.depthView.get(),D3D11_CLEAR_DEPTH,1,0);
    deferred->IASetInputLayout(layout.get()); deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* vb=active->vertices.get(); UINT stride=sizeof(MeshVertex),offset=0;
    deferred->IASetVertexBuffers(0,1,&vb,&stride,&offset); deferred->IASetIndexBuffer(active->indices.get(),DXGI_FORMAT_R32_UINT,0);
    deferred->VSSetShader(vertex.get(),nullptr,0); deferred->PSSetShader(bloom?emissionPixel.get():pixel.get(),nullptr,0);
    ID3D11Buffer* cb=constants.get(); deferred->VSSetConstantBuffers(0,1,&cb); deferred->PSSetConstantBuffers(0,1,&cb);
    cb=lightConstants.get(); deferred->PSSetConstantBuffers(1,1,&cb);
    ID3D11ShaderResourceView* resources[]={active->atlasView.get(),depth,screen,sceneProbe};
    deferred->PSSetShaderResources(0,p.eyeLocal[3]>0?4:3,resources);
    ID3D11SamplerState* sample=sampler.get(); deferred->PSSetSamplers(0,1,&sample);
    ID3D11RenderTargetView* mask=masks?occlusionTarget:nullptr;
    ID3D11RenderTargetView* renderTargets[]={colorTarget,bloom?emissionTarget:mask,bloom?mask:nullptr};
    deferred->OMSetRenderTargets(bloom?(masks?3:2):(masks?2:1),renderTargets,cache.depthView.get());
    deferred->OMSetDepthStencilState(depthState.get(),0); deferred->RSSetState(rasterizer.get());
    D3D11_VIEWPORT viewport{0,0,float(td.Width),float(td.Height),0,1}; deferred->RSSetViewports(1,&viewport);
    auto bodyIndexCount=active->bodyIndexCount,indexCount=active->indexCount;
    if(bodyIndexCount) {
        deferred->PSSetShader(bloom?bodyEmissionPixel.get():bodyPixel.get(),nullptr,0);
        deferred->DrawIndexed(bodyIndexCount,0,0);
    }
    if(indexCount>bodyIndexCount) {
        deferred->PSSetShader(bloom?emissionPixel.get():pixel.get(),nullptr,0);
        deferred->DrawIndexed(indexCount-bodyIndexCount,bodyIndexCount,0);
    }
    if(driver) driver->draw(deferred.get(),bloom);
    deferred->OMSetRenderTargets(0,nullptr,nullptr);
    if(cache.samples>1) {
        deferred->ResolveSubresource(cache.color.get(),0,cache.sampleColor.get(),0,DXGI_FORMAT_R16G16B16A16_FLOAT);
        if(bloom) deferred->ResolveSubresource(cache.emission.get(),0,cache.sampleEmission.get(),0,DXGI_FORMAT_R16G16B16A16_FLOAT);
        if(masks) deferred->ResolveSubresource(cache.occlusion[eye].get(),0,cache.sampleOcclusion.get(),0,DXGI_FORMAT_R32G32_FLOAT);
    }
    {
        deferred->IASetInputLayout(nullptr);deferred->VSSetShader(postVertex.get(),nullptr,0);
        deferred->OMSetDepthStencilState(noDepthState.get(),0);
        ID3D11ShaderResourceView* none[4]{};
        auto pass=[&](ID3D11RenderTargetView* destination,ID3D11PixelShader* shader,ID3D11ShaderResourceView* first,
                      ID3D11ShaderResourceView* second=nullptr,ID3D11ShaderResourceView* third=nullptr,ID3D11ShaderResourceView* fourth=nullptr) {
            deferred->PSSetShaderResources(0,4,none);
            deferred->OMSetRenderTargets(1,&destination,nullptr);deferred->PSSetShader(shader,nullptr,0);
            ID3D11ShaderResourceView* views[]={first,second,third,fourth};deferred->PSSetShaderResources(0,4,views);
            deferred->Draw(3,0);
        };
        if(bloom) {
            viewport.Width=p.target[2];viewport.Height=p.target[3];deferred->RSSetViewports(1,&viewport);
            pass(cache.blurTarget[0].get(),downsamplePixel.get(),cache.emissionSource.get());
            pass(cache.blurTarget[1].get(),horizontalPixel.get(),cache.blurSource[0].get());
            pass(cache.blurTarget[0].get(),verticalPixel.get(),cache.blurSource[1].get());
            viewport.Width=p.target[0];viewport.Height=p.target[1];deferred->RSSetViewports(1,&viewport);
            pass(output,compositePixel.get(),cache.colorSource.get(),cache.blurSource[0].get(),nullptr,depth);
        } else pass(output,copyPixel.get(),cache.colorSource.get());
    }
    Com<ID3D11CommandList> commands;
    if(FAILED(deferred->FinishCommandList(FALSE,commands.put()))) { deferred->ClearState(); return false; }
    // One D3D operation with restore=TRUE preserves every game pipeline binding,
    // including predication. No OpenXR call happens under a D3D explicit lock.
    context->ExecuteCommandList(commands.get(),TRUE);
    lightsNeedUpload=false;
    return SUCCEEDED(device->GetDeviceRemovedReason());
}
}
