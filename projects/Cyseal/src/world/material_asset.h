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

// Material property getters & setters //
public:
	inline EMaterialId getMaterialID() const { return materialID; }
	inline void setMaterialID(EMaterialId value) { bDirty = materialID != value; materialID = value; }

	inline const SharedPtr<TextureAsset>& getAlbedoTexture() const { return albedoTexture; }
	inline void setAlbedoTexture(const SharedPtr<TextureAsset>& value) { bDirty = true; albedoTexture = value; }

	inline vec3 getAlbedoMultiplier() const { return albedoMultiplier; }
	inline void setAlbedoMultiplier(const vec3& value) { bDirty = albedoMultiplier != value; albedoMultiplier = value; }

	// Get linear roughness.
	inline float getRoughness() const { return roughness; }
	// Set linear roughness. If your value is a perceptual roughness, pass (value * value).
	inline void setRoughness(float value) { bDirty = roughness != value; roughness = value; }

	float getPerceptualRoughness() const;
	inline void setPerceptualRoughness(float value) { setRoughness(value * value); }

	inline float getMetalMask() const { return metalMask; }
	inline void setMetalMask(float value) { bDirty = metalMask != value; metalMask = value; }

	inline vec3 getEmission() const { return emission; }
	inline void setEmission(const vec3& value) { bDirty = emission != value; emission = value; }

	inline float getIndexOfRefraction() const { return indexOfRefraction; }
	inline void setIndexOfRefraction(float value) { bDirty = indexOfRefraction != value; indexOfRefraction = value; }

	inline vec3 getTransmittance() const { return transmittance; }
	inline void setTransmittance(const vec3& value) { bDirty = transmittance != value; transmittance = value; }

	inline bool getDoubleSided() const { return bDoubleSided; }
	void setDoubleSided(bool value);

private:
	void updatePipelineKey(const GraphicsPipelineKeyDesc& desc);

private:
	EMaterialId             materialID        = EMaterialId::DefaultLit;
	SharedPtr<TextureAsset> albedoTexture;
	vec3                    albedoMultiplier  = vec3(1.0f, 1.0f, 1.0f);
	float                   roughness         = 0.0f; // Linear roughness
	float                   metalMask         = 0.0f;
	vec3                    emission          = vec3(0.0f);
	float                   indexOfRefraction = IoR::Air;
	vec3                    transmittance     = vec3(0.0f);
	bool                    bDoubleSided      = false;

	GraphicsPipelineKey     pipelineKey;
	uint32                  pipelineFreeNumber;
	bool                    bDirty = true;
};
