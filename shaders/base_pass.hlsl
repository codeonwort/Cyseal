
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
    row_major float4x4 mvpTransform; // #todo-matrix: Use column-major?

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
    float3 normal : NORMAL;
};

VertexOutput mainVS(VertexInput input)
{
    VertexOutput output;

    // (row vector) * (row-major matrix)
    output.posH = mul(float4(input.posL, 1.0), mvpTransform);

    // (column-major matrix) * (column vector)
    //output.posH = mul(mvpTransform, float4(input.posL, 1.0));

    output.normal = normalize(input.posL);

    return output;
}

float4 mainPS(VertexOutput vout) : SV_TARGET
{
    // Variables
    float3 L = normalize(float3(0.0, -1.0, -1.0));
    float3 N = normalize(vout.normal);
    float NdotL = max(0.0, dot(N, -L));

    // Material properties
    float3 albedo = float3(m_r, m_g, m_b);

    // Lighting
    float3 diffuse = albedo * NdotL;
    float3 specular = float3(0.0, 0.0, 0.0);

    float3 luminance = diffuse + specular;
    float opacity = m_a;

    return float4(luminance, opacity);
}
