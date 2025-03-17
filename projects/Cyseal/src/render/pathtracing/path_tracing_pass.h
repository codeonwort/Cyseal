#pragma once

#include "core/int_types.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/texture.h"
#include "render/renderer_options.h"
#include "render/util/volatile_descriptor.h"

class SceneProxy;
class Camera;
class GPUScene;

struct PathTracingInput
{
	const SceneProxy*          scene;
	const Camera*              camera;
	EPathTracingMode           mode;

	Float4x4                   prevViewProjInvMatrix;
	Float4x4                   prevViewProjMatrix;
	bool                       bCameraHasMoved;
	uint32                     sceneWidth;
	uint32                     sceneHeight;

	GPUScene*                  gpuScene;
	AccelerationStructure*     raytracingScene;
	ConstantBufferView*        sceneUniformBuffer;
	UnorderedAccessView*       sceneColorUAV;
	ShaderResourceView*        sceneDepthSRV;
	ShaderResourceView*        prevSceneDepthSRV;
	ShaderResourceView*        gbuffer0SRV;
	ShaderResourceView*        gbuffer1SRV;
	ShaderResourceView*        skyboxSRV;
};

class PathTracingPass final
{
public:
	void initialize();

	bool isAvailable() const;

	void renderPathTracing(RenderCommandList* commandList, uint32 swapchainIndex, const PathTracingInput& passInput);

private:
	void resizeTextures(RenderCommandList* commandList, uint32 newWidth, uint32 newHeight);
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
	UniquePtr<ShaderResourceView> colorHistorySRV[2];

	UniquePtr<Texture> colorScratch;
	UniquePtr<UnorderedAccessView> colorScratchUAV;

	VolatileDescriptorHelper rayPassDescriptor;
	VolatileDescriptorHelper blurPassDescriptor;
};
