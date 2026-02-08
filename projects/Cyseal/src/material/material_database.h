#pragma once

#include "material_shader.h"

class RenderDevice;

struct MaterialShaderPasses
{
	GraphicsPipelineState* depthPrepass = nullptr;
	GraphicsPipelineState* depthAndVisibility = nullptr;
	GraphicsPipelineState* basePass = nullptr;
};

class MaterialShaderDatabase
{
public:
	static MaterialShaderDatabase& get();

public:
	void compileMaterials(RenderDevice* device);
	void destroyMaterials();

	const MaterialShaderPasses* findPasses(GraphicsPipelineKey key) const;

private:
	GraphicsPipelineState* createDepthPipeline(
		RenderDevice* device,
		const GraphicsPipelineKeyDesc& pipelineKeyDesc,
		ShaderStage* vs,
		ShaderStage* ps,
		bool bUseVisibilityBuffer);

	GraphicsPipelineState* createBasePipeline(
		RenderDevice* device,
		const GraphicsPipelineKeyDesc& pipelineKeyDesc,
		ShaderStage* vs,
		ShaderStage* ps);

	std::vector<std::pair<GraphicsPipelineKey, MaterialShaderPasses>> database;
};
