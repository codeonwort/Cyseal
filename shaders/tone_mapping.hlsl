
// #todo-hlsl: Parameterize
#define GAMMA_CORRECTION 2.2
#define EXPOSURE         1.0

// ------------------------------------------------------------------------
// Resource bindings

Texture2D sceneColor : register(t0);
SamplerState sceneColorSampler : register(s0);

// ------------------------------------------------------------------------
// Vertex shader

struct Interpolants
{
    float4 posH : SV_POSITION;
    float2 uv : TEXCOORD0;
};

Interpolants mainVS(uint vertexID: SV_VertexID)
{
    Interpolants output;

    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.posH = float4(output.uv * 2.0 + -1.0, 0.0, 1.0);

    return output;
}

// ------------------------------------------------------------------------
// Pixel shader

float4 mainPS(Interpolants interpolants) : SV_TARGET
{
    float2 screenUV = interpolants.uv;
    screenUV.y = 1.0 - screenUV.y;

    float4 color = sceneColor.SampleLevel(sceneColorSampler, screenUV, 0.0);

    // Reinhard tone mapper
    color.rgb = float3(1.0, 1.0, 1.0) - exp(-color.rgb * EXPOSURE);

    // Gamma correction
    float gamma = 1.0 / GAMMA_CORRECTION;
    color.rgb = pow(color.rgb, float3(gamma, gamma, gamma));

    return color;
}
