#pragma once

#include "rhi/rhi_forward.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource.h"
#include "rhi/gpu_resource_view.h"

#include <vector>

struct StaticMeshSection;

// -----------------------------------------
// PSO permutation

using GraphicsPipelineKey = uint32;

struct GraphicsPipelineKeyDesc
{
	static GraphicsPipelineKey assemblePipelineKey(const GraphicsPipelineKeyDesc& desc);

	// #wip: Hard-coded for now
	static const GraphicsPipelineKeyDesc kDefaultPipelineKeyDesc;
	static const GraphicsPipelineKeyDesc kNoCullPipelineKeyDesc;
	static const GraphicsPipelineKeyDesc kPipelineKeyDescs[];
	static size_t numPipelineKeyDescs();

	ECullMode cullMode;
};

struct IndirectDrawHelper
{
	void initialize(RenderDevice* inRenderDevice, GraphicsPipelineState* pipelineState, GraphicsPipelineKey inPipelineKey);

	void resizeResources(uint32 swapchainIndex, uint32 maxDrawCount);

	RenderDevice*                          device = nullptr;
	GraphicsPipelineKey                    pipelineKey;

	UniquePtr<CommandSignature>            commandSignature;
	UniquePtr<IndirectCommandGenerator>    argumentBufferGenerator;

	BufferedUniquePtr<Buffer>              argumentBuffer;
	BufferedUniquePtr<Buffer>              culledArgumentBuffer;
	BufferedUniquePtr<Buffer>              drawCounterBuffer;

	BufferedUniquePtr<ShaderResourceView>  argumentBufferSRV;
	BufferedUniquePtr<UnorderedAccessView> culledArgumentBufferUAV;
	BufferedUniquePtr<UnorderedAccessView> drawCounterBufferUAV;
};

// Can't think of better name
struct GraphicsPipelineItem
{
	GraphicsPipelineState* pipelineState;
	IndirectDrawHelper* indirectDrawHelper;
};

class GraphicsPipelineStatePermutation
{
public:
	~GraphicsPipelineStatePermutation();

	GraphicsPipelineItem findPipeline(GraphicsPipelineKey key) const;

	void insertPipeline(GraphicsPipelineKey key, GraphicsPipelineItem item);

private:
	std::map<GraphicsPipelineKey, GraphicsPipelineItem> pipelines;
};

// -----------------------------------------
// Mesh rendering

// Per-pipeline draw list.
struct StaticMeshDrawList
{
	std::vector<const StaticMeshSection*> meshes;
	std::vector<uint32> objectIDs;

	void reserve(size_t n)
	{
		meshes.reserve(n);
		objectIDs.reserve(n);
	}
};

class StaticMeshRendering final
{
public:
	void initialize(IndirectDrawHelper* inIndirectDrawHelper);

private:
	void fillIndirectDrawBuffer();
	void performGpuCulling();
	void submitIndirectDraws();

private:
	IndirectDrawHelper* indirectDrawHelper = nullptr;
};
