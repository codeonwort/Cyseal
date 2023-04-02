struct GPUSceneItem
{
    float4x4 modelMatrix; // local to world
    float3   localMinBounds;
    uint     positionBufferOffset;
    float3   localMaxBounds;
    uint     nonPositionBufferOffset;
    uint     indexBufferOffset;
    float3   _pad0;
};

struct Material
{
    float3 albedoMultiplier;
    float  roughness;
    uint   albedoTextureIndex; float3 _pad0;
};

struct AABB
{
    float3 minBounds;
    float3 maxBounds;
};

struct Plane3D
{
    float3 normal; // Surface normal
    float distance; // Length of perp vector from O to the plane
};

struct Frustum3D
{
    // 0: top, 1: bottom, 2: left, 3: right, 4: near, 5: far
    Plane3D planes[6];
};

struct SceneUniform
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    float4x4 viewProjMatrix;

    float4x4 viewInvMatrix;
    float4x4 projInvMatrix;
    float4x4 viewProjInvMatrix;

    Frustum3D cameraFrustum;

    float4 cameraPosition; // (x, y, z, ?)
    float4 sunDirection;   // (x, y, z, ?)
    float4 sunIlluminance; // (r, g, b, ?)
};
