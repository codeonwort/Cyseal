#pragma once

#include "material_shader.h"
#include <map>

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

	/// <summary>
	/// Returns a non-negative integer for the given key.
	/// Free numbers are intended to be used as linear indices starting from 0.
	/// </summary>
	/// <param name="key">A pipeline key.</param>
	/// <returns>The corresponding free number.</returns>
	uint32 getFreeNumberForPipelineKey(GraphicsPipelineKey key) const;

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

	void createFreeNumberForPipelineKey(GraphicsPipelineKey key);

	std::vector<std::pair<GraphicsPipelineKey, MaterialShaderPasses>> database;
	std::map<GraphicsPipelineKey, uint32> freeNumberTable;
};
