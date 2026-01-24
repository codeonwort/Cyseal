#pragma once

#include "render/material.h"
#include "gpu_resource_asset.h"

class MaterialAsset
{
public:
	EMaterialId             materialID        = EMaterialId::DefaultLit;
	SharedPtr<TextureAsset> albedoTexture;
	vec3                    albedoMultiplier  = vec3(1.0f, 1.0f, 1.0f);
	float                   roughness         = 0.0f;
	vec3                    emission          = vec3(0.0f);
	float                   metalMask         = 0.0f;
	float                   indexOfRefraction = IoR::Air;
	vec3                    transmittance     = vec3(0.0f);

	bool                    bDoubleSided      = false;
};
