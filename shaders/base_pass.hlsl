
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
    return float4(1.0, 1.0, 0.0, 1.0);
}
