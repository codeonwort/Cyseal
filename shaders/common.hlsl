struct MeshData
{
    float4x4 modelMatrix;
    uint     positionBufferOffset;
    uint     nonPositionBufferOffset;
    uint     indexBufferOffset;
    uint     _pad0;
};

struct SceneUniform
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    float4x4 viewProjMatrix;

    float4x4 viewInvMatrix;
    float4x4 projInvMatrix;
    float4x4 viewProjInvMatrix;

    float4 cameraPosition; // (x, y, z, ?)
    float4 sunDirection;   // (x, y, z, ?)
    float4 sunIlluminance; // (r, g, b, ?)
};
