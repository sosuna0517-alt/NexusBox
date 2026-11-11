cbuffer Params : register(b0) { float4x4 unusedProj; uint useLinear; };
Texture2D    src     : register(t0);
SamplerState sPoint  : register(s0);
SamplerState sLinear : register(s1);
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    float3 c = useLinear ? src.Sample(sLinear, uv).rgb : src.Sample(sPoint, uv).rgb;
    return float4(c, 1.0);   // cores output XRGB: force opaque
}
