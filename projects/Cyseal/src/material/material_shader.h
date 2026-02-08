#pragma once

#include "rhi/rhi_forward.h"
#include "rhi/pipeline_state.h"

// #todo-renderer: Support other topologies
#define kPrimitiveTopology           EPrimitiveTopology::TRIANGLELIST

// Encode GraphicsPipelineKeyDesc into single integer.
using GraphicsPipelineKey = uint32;

struct GraphicsPipelineKeyDesc
{
	ECullMode cullMode;

public:
	static GraphicsPipelineKey assemblePipelineKey(const GraphicsPipelineKeyDesc& desc);

	// #todo-renderer: Hard-coded for now
	static const GraphicsPipelineKeyDesc kDefaultPipelineKeyDesc;
	static const GraphicsPipelineKeyDesc kNoCullPipelineKeyDesc;

	static const GraphicsPipelineKeyDesc kPipelineKeyDescs[];
	static size_t numPipelineKeyDescs();
};
