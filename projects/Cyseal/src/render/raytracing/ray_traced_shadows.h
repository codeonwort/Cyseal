#pragma once

#include "core/int_types.h"
#include "core/smart_pointer.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/rhi_forward.h"
#include "render/renderer_options.h"
#include "render/util/volatile_descriptor.h"

class Material;
class SceneProxy;
class Camera;
class GPUScene;

struct RayTracedShadowsInput
{
	const SceneProxy*          scene;
	const Camera*              camera;
	ERayTracedShadowsMode      mode;
	uint32                     sceneWidth;
	uint32                     sceneHeight;
	ConstantBufferView*        sceneUniformBuffer;
	GPUScene*                  gpuScene;
	AccelerationStructure*     raytracingScene;
	UnorderedAccessView*       shadowMaskUAV;
};

class RayTracedShadowsPass final
{
public:
	void initialize();

	bool isAvailable() const;

	void renderRayTracedShadows(RenderCommandList* commandList, uint32 swapchainIndex, const RayTracedShadowsInput& passInput);

private:
	void resizeHitGroupShaderTable(uint32 swapchainIndex, uint32 maxRecords);

private:
	UniquePtr<RaytracingPipelineStateObject> RTPSO;

	UniquePtr<RaytracingShaderTable>         raygenShaderTable;
	UniquePtr<RaytracingShaderTable>         missShaderTable;
	BufferedUniquePtr<RaytracingShaderTable> hitGroupShaderTable;
	std::vector<uint32>                      totalHitGroupShaderRecord;

	VolatileDescriptorHelper                 rayPassDescriptor;

};
