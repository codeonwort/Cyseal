#pragma once

#include "gpu_resource_asset.h"
#include "render/material.h"
#include "material/material_shader.h"

class MaterialAsset
{
public:
	MaterialAsset();

	uint32 getPipelineKey() const;

	inline bool getDoubleSided() const { return bDoubleSided; }
	void setDoubleSided(bool value);

private:
	void updatePipelineKey(const GraphicsPipelineKeyDesc& desc);

public:
	EMaterialId             materialID        = EMaterialId::DefaultLit;
	SharedPtr<TextureAsset> albedoTexture;
	vec3                    albedoMultiplier  = vec3(1.0f, 1.0f, 1.0f);
	float                   roughness         = 0.0f;
	vec3                    emission          = vec3(0.0f);
	float                   metalMask         = 0.0f;
	float                   indexOfRefraction = IoR::Air;
	vec3                    transmittance     = vec3(0.0f);
private:
	bool                    bDoubleSided      = false;

	GraphicsPipelineKey     pipelineKey;
};
