#pragma once

#include "core/int_types.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/texture.h"
#include "render/renderer.h"

class SceneProxy;
class Camera;
class GPUScene;

struct PathTracingInput
{
	const SceneProxy*          scene;
	const Camera*              camera;
	EPathTracingMode           mode;

	Float4x4                   prevViewInvMatrix;
	Float4x4                   prevProjInvMatrix;
	Float4x4                   prevViewProjMatrix;
	bool                       bCameraHasMoved;
	uint32                     sceneWidth;
	uint32                     sceneHeight;

	GPUScene*                  gpuScene;
	AccelerationStructure*     raytracingScene;
	ConstantBufferView*        sceneUniformBuffer;
	UnorderedAccessView*       sceneColorUAV;
	const TextureCreateParams* sceneDepthDesc;
	ShaderResourceView*        sceneDepthSRV;
	UnorderedAccessView*       worldNormalUAV;
	ShaderResourceView*        skyboxSRV;
};

class PathTracingPass final
{
private:
	class VolatileDescriptorHelper
	{
	public:
		void initialize(const wchar_t* inPassName, uint32 swapchainCount, uint32 uniformTotalSize);
		void resizeDescriptorHeap(uint32 swapchainIndex, uint32 maxDescriptors);
		inline DescriptorHeap* getDescriptorHeap(uint32 swapchainIndex) const { return descriptorHeap.at(swapchainIndex); }
		inline ConstantBufferView* getUniformCBV(uint32 swapchainIndex) const { return uniformCBVs.at(swapchainIndex); }
	private:
		std::wstring passName;
		std::vector<uint32> totalDescriptor; // size = swapchain count
		BufferedUniquePtr<DescriptorHeap> descriptorHeap; // size = swapchain count
		// #todo-renderer: Temp dedicated memory for uniforms
		UniquePtr<Buffer> uniformMemory;
		UniquePtr<DescriptorHeap> uniformDescriptorHeap;
		BufferedUniquePtr<ConstantBufferView> uniformCBVs;
	};

public:
	void initialize();

	bool isAvailable() const;

	void renderPathTracing(RenderCommandList* commandList, uint32 swapchainIndex, const PathTracingInput& passInput);

private:
	void resizeTextures(RenderCommandList* commandList, uint32 newWidth, uint32 newHeight, const TextureCreateParams* sceneDepthDesc);
	void resizeHitGroupShaderTable(uint32 swapchainIndex, const SceneProxy* scene);

private:
	UniquePtr<RaytracingPipelineStateObject> RTPSO;
	UniquePtr<RaytracingShaderTable> raygenShaderTable;
	UniquePtr<RaytracingShaderTable> missShaderTable;
	BufferedUniquePtr<RaytracingShaderTable> hitGroupShaderTable;
	std::vector<uint32> totalHitGroupShaderRecord;

	UniquePtr<ComputePipelineState> blurPipelineState;

	uint32 historyWidth = 0;
	uint32 historyHeight = 0;
	UniquePtr<Texture> momentHistory[2];
	UniquePtr<UnorderedAccessView> momentHistoryUAV[2];

	UniquePtr<Texture> colorHistory[2];
	UniquePtr<UnorderedAccessView> colorHistoryUAV[2];

	UniquePtr<Texture> colorScratch;
	UniquePtr<UnorderedAccessView> colorScratchUAV;

	UniquePtr<Texture> prevSceneDepth;
	UniquePtr<UnorderedAccessView> prevSceneDepthUAV;

	VolatileDescriptorHelper rayPassDescriptor;
	VolatileDescriptorHelper blurPassDescriptor;
};
