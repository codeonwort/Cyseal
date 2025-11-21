#include "depth_prepass.h"
#include "material.h"
#include "static_mesh.h"
#include "gpu_scene.h"
#include "gpu_culling.h"

#include "rhi/render_device.h"
#include "rhi/rhi_policy.h"
#include "rhi/swap_chain.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/shader.h"
#include "rhi/render_command.h"
#include "rhi/texture_manager.h"
#include "rhi/vertex_buffer_pool.h"

DepthPrepass::~DepthPrepass()
{
	//
}

void DepthPrepass::initialize(RenderDevice* inRenderDevice)
{
	//
}

void DepthPrepass::renderDepthPrepass(RenderCommandList* commandList, uint32 swapchainIndex, const DepthPrepassInput& passInput)
{
	//
}

GraphicsPipelineState* DepthPrepass::createPipeline(const GraphicsPipelineKeyDesc& pipelineKeyDesc)
{
	return nullptr;
}
