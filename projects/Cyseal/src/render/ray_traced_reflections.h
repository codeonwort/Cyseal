#pragma once

#include "core/int_types.h"
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
	//void something();

private:
	std::unique_ptr<RaytracingPipelineStateObject> RTPSO;
	std::unique_ptr<RootSignature> globalRootSignature;
	std::unique_ptr<RootSignature> raygenLocalRootSignature;
	std::unique_ptr<RootSignature> closestHitLocalRootSignature;

	std::unique_ptr<RaytracingShaderTable> raygenShaderTable;
	std::unique_ptr<RaytracingShaderTable> missShaderTable;
	std::unique_ptr<RaytracingShaderTable> hitGroupShaderTable;

	std::unique_ptr<ShaderStage> raygenShader;
	std::unique_ptr<ShaderStage> closestHitShader;
	std::unique_ptr<ShaderStage> missShader;

	std::vector<std::unique_ptr<DescriptorHeap>> volatileViewHeaps;
};
