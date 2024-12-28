#pragma once

#include "core/int_types.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/gpu_resource_view.h"

class SceneProxy;
class Camera;
class GPUScene;

class PathTracingPass final
{
public:
	void initialize();

	bool isAvailable() const;

	void renderPathTracing(
		RenderCommandList* commandList,
		uint32 swapchainIndex,
		const SceneProxy* scene,
		const Camera* camera,
		bool bCameraHasMoved,
		ConstantBufferView* sceneUniformBuffer,
		AccelerationStructure* raytracingScene,
		GPUScene* gpuScene,
		UnorderedAccessView* sceneColorUAV,
		ShaderResourceView* skyboxSRV,
		uint32 sceneWidth,
		uint32 sceneHeight);

private:
	void resizeTextures(RenderCommandList* commandList, uint32 newWidth, uint32 newHeight);
	void resizeVolatileHeap(uint32 swapchainIndex, uint32 maxDescriptors);
	void resizeHitGroupShaderTable(uint32 swapchainIndex, const SceneProxy* scene);

private:
	UniquePtr<RaytracingPipelineStateObject> RTPSO;

	UniquePtr<RaytracingShaderTable> raygenShaderTable;
	UniquePtr<RaytracingShaderTable> missShaderTable;
	BufferedUniquePtr<RaytracingShaderTable> hitGroupShaderTable;
	std::vector<uint32> totalHitGroupShaderRecord;

	// #todo-renderer: Temp dedicated memory
	UniquePtr<Buffer> uniformMemory;
	UniquePtr<DescriptorHeap> uniformDescriptorHeap;
	BufferedUniquePtr<ConstantBufferView> uniformCBVs;

	uint32 historyWidth = 0;
	uint32 historyHeight = 0;
	UniquePtr<Texture> momentHistory[2];

	std::vector<uint32> totalVolatileDescriptor;
	BufferedUniquePtr<DescriptorHeap> volatileViewHeap;
};
