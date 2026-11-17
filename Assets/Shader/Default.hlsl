// 기본 텍스처 셰이더 (위치 + UV)
//
// 행렬은 C++(SimpleMath/DirectXMath)과 같은 행 우선(row_major) + 행 벡터 규약: mul(v, M)
// → CPU에서 전치(transpose) 없이 그대로 상수 버퍼에 올린다.

cbuffer PerFrame : register(b0)
{
    row_major float4x4 gViewProjection; // View * Projection (CPU에서 미리 곱함)
};

cbuffer PerObject : register(b1)
{
    row_major float4x4 gWorld;
};

Texture2D    gAlbedo  : register(t0);
SamplerState gSampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float2 uv       : TEXCOORD0;
};

struct PSInput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

PSInput VSMain(VSInput input)
{
    PSInput output;

    const float4 worldPosition = mul(float4(input.position, 1.0f), gWorld);
    output.position = mul(worldPosition, gViewProjection);
    output.uv       = input.uv;

    return output;
}

float4 PSMain(PSInput input) : SV_Target
{
    return gAlbedo.Sample(gSampler, input.uv);
}
