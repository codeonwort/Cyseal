#pragma once

#include "core/int_types.h"
#include "core/smart_pointer.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/rhi_forward.h"

class Material;
class SceneProxy;
class Camera;
class GPUScene;

class RayTracedReflections final
{
public:
	void initialize();

	bool isAvailable() const;

	void renderRayTracedReflections(
		RenderCommandList* commandList,
		uint32 swapchainIndex,
		const SceneProxy* scene,
		const Camera* camera,
		ConstantBufferView* sceneUniformBuffer,
		AccelerationStructure* raytracingScene,
		GPUScene* gpuScene,
		Texture* thinGBufferATexture,
		Texture* indirectSpecularTexture,
		uint32 sceneWidth,
		uint32 sceneHeight);

private:
	void resizeVolatileHeap(uint32 swapchainIndex, uint32 maxDescriptors);
	void resizeHitGroupShaderTable(uint32 swapchainIndex, uint32 maxRecords);

private:
	UniquePtr<RaytracingPipelineStateObject> RTPSO;

	UniquePtr<RaytracingShaderTable> raygenShaderTable;
	UniquePtr<RaytracingShaderTable> missShaderTable;
	BufferedUniquePtr<RaytracingShaderTable> hitGroupShaderTable;
	std::vector<uint32> totalHitGroupShaderRecord;

	std::vector<uint32> totalVolatileDescriptor;
	BufferedUniquePtr<DescriptorHeap> volatileViewHeap;

	UniquePtr<UnorderedAccessView> indirectSpecularUAV;
	UniquePtr<UnorderedAccessView> thinGBufferAUAV;

	UniquePtr<ShaderResourceView> skyboxSRV;
	UniquePtr<ShaderResourceView> skyboxFallbackSRV;
};
