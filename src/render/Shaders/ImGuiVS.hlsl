cbuffer Params : register(b0) { float4x4 proj; uint unusedFlag; };
struct VSIn  { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };
VSOut main(VSIn i)
{
    VSOut o;
    o.pos = mul(proj, float4(i.pos, 0.0, 1.0));
    o.col = i.col;
    o.uv  = i.uv;
    return o;
}
