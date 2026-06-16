#pragma once

#include "gpu_resource_asset.h"
#include "render/material.h"
#include "material/material_shader.h"

class MaterialAsset
{
public:
	MaterialAsset();

	uint32 getPipelineKey() const;
	uint32 getPipelineFreeNumber() const;

	inline bool isDirty() const { return bDirty; }
	inline void clearDirtyFlag() { bDirty = false; }

	inline vec3 getEmission() const { return emission; }
	void setEmission(const vec3& value);

	// Returns linear roughness.
	inline float getRoughness() const { return roughness; }
	// Set linear roughness. If your value is a perceptual roughness, pass (value * value).
	void setRoughness(float value);

	float getPerceptualRoughness() const;
	void setPerceptualRoughness(float value);

	inline bool getDoubleSided() const { return bDoubleSided; }
	void setDoubleSided(bool value);

private:
	void updatePipelineKey(const GraphicsPipelineKeyDesc& desc);

public:
	EMaterialId             materialID        = EMaterialId::DefaultLit;
	SharedPtr<TextureAsset> albedoTexture;
	vec3                    albedoMultiplier  = vec3(1.0f, 1.0f, 1.0f);
	float                   metalMask         = 0.0f;
	float                   indexOfRefraction = IoR::Air;
	vec3                    transmittance     = vec3(0.0f);
private:
	vec3                    emission          = vec3(0.0f);
	float                   roughness         = 0.0f; // Linear roughness.
	bool                    bDoubleSided      = false;

	GraphicsPipelineKey     pipelineKey;
	uint32                  pipelineFreeNumber;
	bool                    bDirty = true;
};
