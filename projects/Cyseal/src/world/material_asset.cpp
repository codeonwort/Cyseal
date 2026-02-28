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

void MaterialAsset::setDoubleSided(bool value)
{
	const auto& desc = value
		? GraphicsPipelineKeyDesc::kNoCullPipelineKeyDesc
		: GraphicsPipelineKeyDesc::kDefaultPipelineKeyDesc;
	updatePipelineKey(desc);

	// #wip: Need to update material data buffer if it's changed after first upload.
	bDoubleSided = value;
}

void MaterialAsset::updatePipelineKey(const GraphicsPipelineKeyDesc& desc)
{
	pipelineKey = GraphicsPipelineKeyDesc::assemblePipelineKey(desc);
	pipelineFreeNumber = MaterialShaderDatabase::get().getFreeNumberForPipelineKey(pipelineKey);
}
