#ifndef _MATERIAL_H
#define _MATERIAL_H

#define MATERIAL_ID_NONE        0
#define MATERIAL_ID_DEFAULT_LIT 1
#define MATERIAL_ID_TRANSPARENT 2

// Should match with MaterialConstants in gpu_scene.h.
struct Material
{
    float3 albedoMultiplier;
    float  roughness;
    uint   albedoTextureIndex;
    float3 emission;
    float  metalMask;
    uint   materialID;
    float  indexOfRefraction;
    uint   _pad;
};

#endif // _MATERIAL_H
