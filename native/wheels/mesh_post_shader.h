#pragma once
// Full-screen LED-only bloom, using quarter-resolution separable filtering.
// RGB is linear premultiplied radiance. A carries radiance-weighted local eye
// distance, allowing the final halo to respect foreground game surfaces.
constexpr char meshPostShader[]=R"hlsl(
cbuffer Parameters : register(b0) {
    float4 row0; float4 row1; float4 row2;
    float4 eyeLocal; float4 aperture; float4 fov; float4 rectangle;
    float4 depth; float4 model; float4 clipping;
    float4 effects; float4 body; float4 target;
};
Texture2D<float4> source : register(t0);
Texture2D<float4> haloSource : register(t1);
Texture2DArray<float> gameDepth : register(t3);
SamplerState linearSampler : register(s0);
struct Post {float4 position:SV_Position;float2 uv:TEXCOORD0;};
Post postVS(uint id:SV_VertexID) {
    Post o; o.uv=float2((id<<1)&2,id&2);
    o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;
}
float4 downsample(Post input):SV_Target {
    int2 start=int2(input.position.xy)*4;float4 sum=0;
    [unroll] for(int y=0;y<4;++y) [unroll] for(int x=0;x<4;++x)
        sum+=source.Load(int3(min(start+int2(x,y),int2(target.xy)-1),0));
    return sum/16;
}
float4 blurred(float2 uv,float2 direction) {
    float4 sum=source.SampleLevel(linearSampler,uv,0)*.38774;
    sum+=(source.SampleLevel(linearSampler,uv+direction,0)+source.SampleLevel(linearSampler,uv-direction,0))*.24477;
    sum+=(source.SampleLevel(linearSampler,uv+direction*2,0)+source.SampleLevel(linearSampler,uv-direction*2,0))*.06136;
    return sum;
}
float4 horizontal(Post input):SV_Target {return blurred(input.uv,float2(effects.z*.125/target.z,0));}
float4 vertical(Post input):SV_Target {return blurred(input.uv,float2(0,effects.z*.125/target.w));}
float4 copyColor(Post input):SV_Target {
    float4 base=source.Load(int3(int2(input.position.xy),0));
    return float4(base.a>1e-8?base.rgb/base.a:0,base.a);
}
float4 composite(Post input):SV_Target {
    float4 base=source.Load(int3(int2(input.position.xy),0));
    float4 halo=haloSource.SampleLevel(linearSampler,input.uv,0);
    float luminance=dot(halo.rgb,float3(.2126,.7152,.0722));
    float3 radiance=halo.rgb*effects.y;
    if(luminance>1e-8) {
        float localDistance=halo.a/luminance;
        // Only visible emitter samples entered this mask. Optical bloom may
        // spread across their own casing, including the recessed shift lenses.
        // Game foreground depth remains independently enforced below.
        if(clipping.z>.5) {
            float2 plane=aperture.xy+(input.uv*float2(1,-1)+float2(-.5,.5))*aperture.zw;
            float4 local=float4(plane,0,1);
            float3 eye=float3(dot(row0,local),dot(row1,local),dot(row2,local));
            float forward=-eye.z;
            if(forward>0) {
                float2 uv=float2((eye.x/forward-fov.x)/(fov.y-fov.x),(fov.z-eye.y/forward)/(fov.z-fov.w));
                if(all(uv>=0) && all(uv<1)) {
                    int2 coordinate=int2(rectangle.xy+floor(uv*rectangle.zw));
                    float sampled=gameDepth.Load(int4(coordinate,int(model.w),0));
                    if(sampled>=depth.x && sampled<=depth.y) {
                        float reciprocal=lerp(depth.z,depth.w,(sampled-depth.x)/(depth.y-depth.x));
                        if(reciprocal>0 && 1/reciprocal<forward*localDistance/eyeLocal.z-model.z) radiance=0;
                    }
                }
            }
        }
    } else radiance=0;
    float haloAlpha=saturate(max(radiance.r,max(radiance.g,radiance.b)));
    float alpha=base.a+haloAlpha*(1-base.a);
    return float4(alpha>1e-8 ? (base.rgb+radiance)/alpha : 0,alpha);
}
)hlsl";
