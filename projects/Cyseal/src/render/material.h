#pragma once

#include "world/gpu_resource_asset.h"

class Material
{
public:
	SharedPtr<TextureAsset> albedoTexture;
	float albedoMultiplier[3] = { 1.0f, 1.0f, 1.0f };
	float roughness = 0.0f;
	vec3 emission = vec3(0.0f);
};
