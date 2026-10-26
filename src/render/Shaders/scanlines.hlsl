// scanlines.hlsl — Slang/GLSL-style pass translated to HLSL for the NexusBox
// post-processing chain. Runs at output resolution after the scale pass.
// Uniforms are bound via a constant buffer root parameter in the full build.
Texture2D src : register(t0);
SamplerState linearSampler : register(s0);

cbuffer PostParams : register(b0)
{
    float2 srcSize;      // core resolution
    float2 outSize;      // 3840x2160
    float  scanIntensity; // 0.0 .. 1.0
    float  gamma;
};

float3 ApplyScanlines(float2 uv)
{
    // Linear-prefiltered source, then synthetic aperture grille.
    float3 color = src.Sample(linearSampler, uv).rgb;
    const float linesPerPx = outSize.y / srcSize.y;    // vertical lines per screen px
    float line = 0.5 + 0.5 * cos(uv.y * srcSize.y * 3.14159265);
    color *= lerp(1.0, line, scanIntensity);
    return color;
}

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    float3 c = ApplyScanlines(uv);
    c = pow(c, 1.0 / gamma);
    return float4(c, 1.0);
}
