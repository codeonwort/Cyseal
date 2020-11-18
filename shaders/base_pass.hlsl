
/* Not SM 5.1?
struct UBO
{
    float r;
    float g;
    float b;
    float a;
};

ConstantBuffer<UBO> ubo : register(b0);
*/

cbuffer UBO : register(b0)
{
    float m_r;
    float m_g;
    float m_b;
    float m_a;
};

struct VertexInput
{
    float3 posL : POSITION;
};

struct VertexOutput
{
    float4 posH : SV_POSITION;
};

VertexOutput mainVS(VertexInput input)
{
    VertexOutput output;

    output.posH = float4(input.posL, 1.0);

    return output;
}

float4 mainPS(VertexOutput vout) : SV_TARGET
{
    return float4(m_r, m_g, m_b, m_a);

	float r = vout.posH.z;
    return float4(r, r, r, 1.0);
}
