// crt-royale.hlsl — HLSL translation scaffold of CRT-Royale (AMD donation shader
// suite) for the NexusBox chain: decone -> linearize -> mask -> scanlines ->
// phosphor -> tonemap. The reference pass graph lives in the Slang pipeline;
// this single-file variant fuses the cheap passes for the 120Hz budget.
Texture2D src : register(t0);
SamplerState linearSampler : register(s0);

cbuffer PostParams : register(b0)
{
    float2 srcSize, outSize;
    float  curvature, scanIntensity, maskStrength, gamma;
};

float2 Barrel(float2 uv)
{
    float2 cc = uv - 0.5;
    float  r2 = dot(cc, cc);
    return uv + cc * r2 * curvature * 0.2;
}

float4 main(float4 pos : SV_POSITION, float2 uvIn : TEXCOORD0) : SV_Target
{
    float2 uv = Barrel(uvIn);
    if (any(uv < 0.0) || any(uv > 1.0)) return float4(0, 0, 0, 1);

    float3 c = src.Sample(linearSampler, uv).rgb;
    c = pow(abs(c), 2.2);                       // linearize

    // Aperture grille (RGB triad) at output pixel frequency.
    float2 px = uv * outSize;
    float tri = fmod(px.x, 3.0);
    float3 mask = float3(tri < 1.0, tri >= 1.0 && tri < 2.0, tri >= 2.0);
    c *= lerp(1.0, mask * 1.5, maskStrength);

    // Scanlines
    float line = 0.5 + 0.5 * cos(uv.y * srcSize.y * 3.14159265);
    c *= lerp(1.0, line, scanIntensity);

    c = pow(max(c, 0.0), 1.0 / gamma);          // tonemap out
    return float4(c, 1.0);
}
