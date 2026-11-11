Texture2D    tex     : register(t0);
SamplerState sLinear : register(s1);
float4 main(float4 pos : SV_POSITION, float4 col : COLOR0, float2 uv : TEXCOORD0) : SV_Target
{
    return col * tex.Sample(sLinear, uv);
}
