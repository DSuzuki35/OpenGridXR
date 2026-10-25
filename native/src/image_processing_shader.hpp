#pragma once
// Adapted from MIT CeeJayDK SweetFX LumaSharpen and AMD FidelityFX CAS.
// Full notices, pinned sources and adaptation details: ../third_party/lumasharpen
// and ../third_party/cas. CAS here is sharpen-only, full precision, per-channel,
// five-tap (the upstream CAS_SLOW + CAS_GO_SLOWER, noScaling=true path).
static constexpr char imageProcessingShader[] = R"hlsl(
Texture2DArray<float4> sourceImage : register(t0);
SamplerState linearClamp : register(s0);
cbuffer Settings : register(b0) {
    float2 reciprocalSize;
    float2 minimumPixel;
    float2 maximumPixel;
    float slice;
    float strength;
    float lumaClamp;
    float lumaRadius;
    uint lumaPattern;
    uint mode;
};
float4 vertexMain(uint id : SV_VertexID) : SV_Position {
    return float4(id == 1 ? 3.0 : -1.0, id == 2 ? -3.0 : 1.0, 0.0, 1.0);
}
float4 sampleAt(float2 pixel) {
    return sourceImage.SampleLevel(linearClamp,
        float3(clamp(pixel, minimumPixel, maximumPixel) * reciprocalSize, slice), 0);
}
float3 lumaSharpen(float2 p, float3 original) {
    float3 blurred;
    float amount = strength * 3.0;
    if (lumaPattern == 1) {
        float2 delta = lumaRadius / 3.0;
        blurred = (sampleAt(p + delta).rgb + sampleAt(p - delta).rgb) * .5;
        amount *= 1.5;
    } else if (lumaPattern == 2) {
        float r = .5 * lumaRadius;
        blurred = (sampleAt(p + float2(r,-r)).rgb + sampleAt(p + float2(-r,-r)).rgb
                 + sampleAt(p + float2(r,r)).rgb + sampleAt(p + float2(-r,r)).rgb) * .25;
    } else if (lumaPattern == 3) {
        blurred = (sampleAt(p + float2(.4,-1.2)*lumaRadius).rgb
                 + sampleAt(p - float2(1.2,.4)*lumaRadius).rgb
                 + sampleAt(p + float2(1.2,.4)*lumaRadius).rgb
                 + sampleAt(p - float2(.4,-1.2)*lumaRadius).rgb) * .25;
        amount *= .51;
    } else {
        blurred = (sampleAt(p + float2(.5,-lumaRadius)).rgb
                 + sampleAt(p + float2(-lumaRadius,-.5)).rgb
                 + sampleAt(p + float2(lumaRadius,.5)).rgb
                 + sampleAt(p + float2(-.5,lumaRadius)).rgb) * .25;
        amount *= .666;
    }
    // Direct clamp also defines clamp=0, avoiding the upstream optimized divide by zero.
    float delta = clamp(dot(original - blurred, float3(.2126,.7152,.0722)) * amount,
                        -lumaClamp, lumaClamp);
    return saturate(original + delta);
}
float3 casSharpen(float2 p, float3 e) {
    float3 b = sampleAt(p + float2(0,-1)).rgb;
    float3 d = sampleAt(p + float2(-1,0)).rgb;
    float3 f = sampleAt(p + float2(1,0)).rgb;
    float3 h = sampleAt(p + float2(0,1)).rgb;
    float3 mn = min(min(min(d,e),min(f,b)),h);
    float3 mx = max(max(max(d,e),max(f,b)),h);
    // Guard a black neighborhood. CAS's mathematical limit there is zero weight.
    float3 amplitude = sqrt(saturate(min(mn,1.0-mx) / max(mx,1e-6)));
    float peak = -1.0 / lerp(8.0,5.0,strength);
    float3 weight = amplitude * peak;
    float3 sharpened = saturate(((b+d+f+h)*weight+e) / (1.0+4.0*weight));
    // Our user intensity includes an exact zero bypass; upstream sharpness=0 is not off.
    return lerp(e,sharpened,strength);
}
float4 pixelMain(float4 position : SV_Position) : SV_Target {
    float4 original = sampleAt(position.xy);
    return float4(mode == 1 ? lumaSharpen(position.xy,original.rgb)
                            : casSharpen(position.xy,original.rgb), original.a);
}
)hlsl";
