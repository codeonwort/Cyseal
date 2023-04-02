#pragma once

#include "core/int_types.h"
#include "core/smart_pointer.h"
#include "rhi/gpu_resource_view.h"

class RenderCommandList;
class Material;
class SceneProxy;
class Camera;
class Texture;
class RootSignature;
class ShaderStage;
class RaytracingPipelineStateObject;
class RaytracingShaderTable;
class DescriptorHeap;
class AccelerationStructure;
class ConstantBufferView;
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
	UniquePtr<RootSignature> globalRootSignature;
	UniquePtr<RootSignature> raygenLocalRootSignature;
	UniquePtr<RootSignature> closestHitLocalRootSignature;

	UniquePtr<RaytracingShaderTable> raygenShaderTable;
	UniquePtr<RaytracingShaderTable> missShaderTable;
	BufferedUniquePtr<RaytracingShaderTable> hitGroupShaderTable;
	std::vector<uint32> totalHitGroupShaderRecord;

	UniquePtr<ShaderStage> raygenShader;
	UniquePtr<ShaderStage> closestHitShader;
	UniquePtr<ShaderStage> missShader;

	std::vector<uint32> totalVolatileDescriptor;
	BufferedUniquePtr<DescriptorHeap> volatileViewHeap;

	UniquePtr<ShaderResourceView> skyboxSRV;
	UniquePtr<ShaderResourceView> skyboxFallbackSRV;
};
