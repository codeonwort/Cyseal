#pragma once

#include "world/gpu_resource_asset.h"

class Material
{
public:
	SharedPtr<TextureAsset> albedoTexture;
	vec3                    albedoMultiplier = vec3(1.0f, 1.0f, 1.0f);
	float                   roughness        = 0.0f;
	vec3                    emission         = vec3(0.0f);
	float                   metalMask        = 0.0f;
};
