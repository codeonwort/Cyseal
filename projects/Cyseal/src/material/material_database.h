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

private:
	GraphicsPipelineState* createDepthPipeline(
		RenderDevice* device,
		const GraphicsPipelineKeyDesc& pipelineKeyDesc,
		ShaderStage* vs,
		ShaderStage* ps,
		bool bUseVisibilityBuffer);

	std::vector<std::pair<GraphicsPipelineKey, MaterialShaderPasses>> database;
};
