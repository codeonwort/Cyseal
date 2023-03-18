#pragma once

#include "core/int_types.h"
#include "rhi/gpu_resource_view.h"
#include <memory>
#include <vector>

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
	void resizeVolatileHeaps(uint32 maxDescriptors);
	void resizeHitGroupShaderTable(uint32 maxRecords);

private:
	std::unique_ptr<RaytracingPipelineStateObject> RTPSO;
	std::unique_ptr<RootSignature> globalRootSignature;
	std::unique_ptr<RootSignature> raygenLocalRootSignature;
	std::unique_ptr<RootSignature> closestHitLocalRootSignature;

	std::unique_ptr<RaytracingShaderTable> raygenShaderTable;
	std::unique_ptr<RaytracingShaderTable> missShaderTable;
	std::unique_ptr<RaytracingShaderTable> hitGroupShaderTable;
	uint32 totalHitGroupShaderRecord = 0;

	std::unique_ptr<ShaderStage> raygenShader;
	std::unique_ptr<ShaderStage> closestHitShader;
	std::unique_ptr<ShaderStage> missShader;

	std::vector<std::unique_ptr<DescriptorHeap>> volatileViewHeaps;
	uint32 totalVolatileDescriptors = 0;

	std::unique_ptr<ShaderResourceView> skyboxSRV;
	std::unique_ptr<ShaderResourceView> skyboxFallbackSRV;
};
