#ifndef _MATERIAL_H
#define _MATERIAL_H

// Should match with MaterialConstants in gpu_scene.h.
struct Material
{
    float3 albedoMultiplier;
    float  roughness;
    uint   albedoTextureIndex;
    float3 emission;
    float  metalMask;
    uint3  _pad;
};

#endif // _MATERIAL_H
