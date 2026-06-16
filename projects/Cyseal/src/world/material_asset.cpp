#include "material_asset.h"
#include "material/material_database.h"

MaterialAsset::MaterialAsset()
{
	updatePipelineKey(GraphicsPipelineKeyDesc::kDefaultPipelineKeyDesc);
}

uint32 MaterialAsset::getPipelineKey() const
{
	static_assert(sizeof(uint32) == sizeof(GraphicsPipelineKey));
	return (uint32)pipelineKey;
}

uint32 MaterialAsset::getPipelineFreeNumber() const
{
	return pipelineFreeNumber;
}

void MaterialAsset::setEmission(const vec3& value)
{
	bDirty = emission != value;
	emission = value;
}

void MaterialAsset::setRoughness(float value)
{
	bDirty = roughness != value;
	roughness = value;
}

float MaterialAsset::getPerceptualRoughness() const
{
	return Cymath::sqrt(getRoughness());
}

void MaterialAsset::setPerceptualRoughness(float value)
{
	setRoughness(value * value);
}

void MaterialAsset::setDoubleSided(bool value)
{
	const auto& desc = value
		? GraphicsPipelineKeyDesc::kNoCullPipelineKeyDesc
		: GraphicsPipelineKeyDesc::kDefaultPipelineKeyDesc;
	updatePipelineKey(desc);

	bDirty = bDoubleSided != value;
	bDoubleSided = value;
}

void MaterialAsset::updatePipelineKey(const GraphicsPipelineKeyDesc& desc)
{
	pipelineKey = GraphicsPipelineKeyDesc::assemblePipelineKey(desc);
	pipelineFreeNumber = MaterialShaderDatabase::get().getFreeNumberForPipelineKey(pipelineKey);
}
