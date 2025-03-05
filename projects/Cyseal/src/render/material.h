#pragma once

#include "world/gpu_resource_asset.h"

class MaterialAsset
{
public:
	SharedPtr<TextureAsset> albedoTexture;
	vec3                    albedoMultiplier = vec3(1.0f, 1.0f, 1.0f);
	float                   roughness        = 0.0f;
	vec3                    emission         = vec3(0.0f);
	float                   metalMask        = 0.0f;
};

enum class EMaterialId : uint32
{
	None        = 0,
	DefaultLit  = 1,
	Transparent = 2, // #todo-material: Implement transmission.
};

// Should match with Material in material.hlsl.
struct MaterialConstants
{
	vec3   albedoMultiplier   = vec3(1.0f, 1.0f, 1.0f);
	float  roughness          = 0.0f;

	uint32 albedoTextureIndex = 0xffffffff;
	vec3   emission           = vec3(0.0f, 0.0f, 0.0f);

	float  metalMask          = 0.0f;
	uint32 materialID         = (uint32)EMaterialId::DefaultLit;
	float  indexOfRefraction  = 1.0f;
	uint32 _pad0;
};
