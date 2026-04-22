#pragma once

#include "render/scene_render_pass.h"
#include "render/util/volatile_descriptor.h"
#include "rhi/rhi_forward.h"
#include "rhi/descriptor_heap.h"
#include "core/smart_pointer.h"

enum class EClearTextureDimension : uint32
{
	TEXTURE_DIMENSION_1D = 0,
	TEXTURE_DIMENSION_2D = 1,
	TEXTURE_DIMENSION_3D = 2,

	Count,
};

enum class EClearTextureFormat : uint32
{
	TEXTURE_FORMAT_FLOAT4 = 0,
	TEXTURE_FORMAT_FLOAT2 = 1,
	TEXTURE_FORMAT_FLOAT1 = 2,
	TEXTURE_FORMAT_UINT4  = 3,
	TEXTURE_FORMAT_UINT2  = 4,
	TEXTURE_FORMAT_UINT1  = 5,
	TEXTURE_FORMAT_INT4   = 6,
	TEXTURE_FORMAT_INT2   = 7,
	TEXTURE_FORMAT_INT1   = 8,

	Count,
};

class ClearResourcePass final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inRenderDevice);

	void prepareForFrame(uint32 swapchainIndex);

	void enqueueClear(Texture* texture, UnorderedAccessView* uav);

	/// CAUTION: No barrier after clear.
	void executeClears(RenderCommandList* commandList, uint32 swapchainIndex);

private:
	RenderDevice* device = nullptr;

	UniquePtr<ComputePipelineState> pipelines[(uint32)EClearTextureDimension::Count][(uint32)EClearTextureFormat::Count];
	VolatileDescriptorHelper passDescriptor;
	DescriptorIndexTracker tracker;

	std::vector<Texture*> texturesToClear;
	std::vector<UnorderedAccessView*> UAVsToClear;
};
