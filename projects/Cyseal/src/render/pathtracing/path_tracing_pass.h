#pragma once

#include "core/int_types.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/texture.h"
#include "render/scene_render_pass.h"
#include "render/renderer_options.h"
#include "render/util/volatile_descriptor.h"
#include "render/util/texture_sequence.h"

class SceneProxy;
class Camera;

struct PathTracingInput
{
	const SceneProxy*          scene;
	const Camera*              camera;
	EPathTracingMode           mode;
	EPathTracingKernel         kernel;

	Float4x4                   prevViewProjInvMatrix;
	Float4x4                   prevViewProjMatrix;
	bool                       bCameraHasMoved;
	uint32                     sceneWidth;
	uint32                     sceneHeight;

	class GPUScene*            gpuScene;
	class BilateralBlur*       bilateralBlur;

	AccelerationStructure*     raytracingScene;
	ConstantBufferView*        sceneUniformBuffer;
	Texture*                   sceneColorTexture;
	UnorderedAccessView*       sceneColorUAV;
	ShaderResourceView*        sceneDepthSRV;
	ShaderResourceView*        prevSceneDepthSRV;
	ShaderResourceView*        velocityMapSRV;
	ShaderResourceView*        gbuffer0SRV;
	ShaderResourceView*        gbuffer1SRV;
	ShaderResourceView*        skyboxSRV;
};

class PathTracingPass final : public SceneRenderPass
{
public:
	void initialize();

	bool isAvailable() const;

	void renderPathTracing(RenderCommandList* commandList, uint32 swapchainIndex, const PathTracingInput& passInput);

private:
	void initializeRaytracingPipeline();
	void initializeTemporalPipeline();

	void executeMegaKernel(RenderCommandList* commandList, uint32 swapchainIndex, const PathTracingInput& passInput);

	void resizeTextures(RenderCommandList* commandList, uint32 newWidth, uint32 newHeight);
	void resizeHitGroupShaderTable(uint32 swapchainIndex, const SceneProxy* scene);

private:
	// Ray pass
	UniquePtr<RaytracingPipelineStateObject> RTPSO;
	UniquePtr<RaytracingShaderTable>         raygenShaderTable;
	UniquePtr<RaytracingShaderTable>         missShaderTable;
	BufferedUniquePtr<RaytracingShaderTable> hitGroupShaderTable;
	std::vector<uint32>                      totalHitGroupShaderRecord;
	VolatileDescriptorHelper                 rayPassDescriptor;

	// Temporal pass
	UniquePtr<ComputePipelineState>          temporalPipeline;
	VolatileDescriptorHelper                 temporalPassDescriptor;

	uint32                                   historyWidth = 0;
	uint32                                   historyHeight = 0;
	UniquePtr<Texture>                       raytracingTexture;
	UniquePtr<ShaderResourceView>            raytracingSRV;
	UniquePtr<UnorderedAccessView>           raytracingUAV;
	TextureSequence                          colorHistory;
	TextureSequence                          momentHistory;
};
