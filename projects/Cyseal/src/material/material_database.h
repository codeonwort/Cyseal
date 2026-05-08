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
	/// <summary>
	/// Compile all material shaders and construct a database of pipelines.
	/// </summary>
	/// <param name="device">Render device used for compiling shaders.</param>
	/// <param name="bSkipCompile">If true, skip compiling shaders and only build the database. Public APIs will work but all GraphicsPipelineState instances will be null. Also, if true, the device argument can be null.</param>
	void compileMaterials(RenderDevice* device, bool bSkipCompile = false);

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

	bool bInitialized = false;
	bool bCompilationSkipped = false;
	std::vector<std::pair<GraphicsPipelineKey, MaterialShaderPasses>> database;
	std::map<GraphicsPipelineKey, uint32> freeNumberTable;
};
