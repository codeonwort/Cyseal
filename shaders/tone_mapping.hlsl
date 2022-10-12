
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

    //float4 sceneColorSample = sceneColor.SampleLevel(sceneColorSampler, screenUV, 0.0);
    //sceneColorSample.rgb *= exp(1.0 / 2.2);

    float4 sceneColorSample = float4(1.0, 0.0, 0.0, 1.0);

    clip(screenUV.x - 0.5);

    return sceneColorSample;
}
