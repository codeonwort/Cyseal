#pragma once

#include "world/gpu_resource_asset.h"

class Material
{
public:
	std::shared_ptr<TextureAsset> albedoTexture;
	float albedoMultiplier[3] = { 1.0f, 1.0f, 1.0f };
	float roughness = 0.0f;
};
