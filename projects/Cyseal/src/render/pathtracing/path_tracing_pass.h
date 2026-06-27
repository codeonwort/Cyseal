#pragma once

#include "core/int_types.h"
#include "core/cymath.h"
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
	class FinalBlitPass*       blitPass;
	const SceneProxy*          scene;
	const Camera*              camera;
	EPathTracingMode           mode;
	EPathTracingKernel         kernel;
	uint32                     randomSeed;

	bool                       bCameraHasMoved;
	uint32                     sceneWidth;
	uint32                     sceneHeight;

	class GPUScene*            gpuScene;
	class BilateralBlur*       bilateralBlur;

	AccelerationStructure*     raytracingScene;
	ConstantBufferView*        sceneUniformBuffer;
	Texture*                   sceneColorTexture;
	UnorderedAccessView*       sceneColorUAV;
	RenderTargetView*          sceneColorRTV;
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
	void initialize(RenderDevice* inDevice);

	bool isAvailable() const;

	void renderPathTracing(RenderCommandList* commandList, const FrameInfo& frameInfo, const PathTracingInput& passInput);

private:
	void initializeRaytracingPipeline();
	void initializeTemporalPipeline();
	void initializeFinalMergePipeline();

	void executeMegaKernel(RenderCommandList* commandList, const FrameInfo& frameInfo, const PathTracingInput& passInput);
	void executeTemporalReconstruction(RenderCommandList* commandList, const FrameInfo& frameInfo, const PathTracingInput& passInput);
	void executeVarianceGuidedFilter(RenderCommandList* commandList, const FrameInfo& frameInfo, const PathTracingInput& passInput);

	void resizeTextures(RenderCommandList* commandList, uint32 newWidth, uint32 newHeight);
	void resizeHitGroupShaderTable(uint32 resourceIndex, const SceneProxy* scene);

private:
	RenderDevice*                            device = nullptr;

	RNG<float>                               rng;

	// Raytracing pass
	UniquePtr<RaytracingPipelineStateObject> RTPSO;
	UniquePtr<RaytracingShaderTable>         raygenShaderTable;
	UniquePtr<RaytracingShaderTable>         missShaderTable;
	BufferedUniquePtr<RaytracingShaderTable> hitGroupShaderTable;
	std::vector<uint32>                      totalHitGroupShaderRecord;
	VolatileDescriptorHelper                 rayPassDescriptor;

	// Temporal reprojection pass
	UniquePtr<ComputePipelineState>          temporalPipeline;
	VolatileDescriptorHelper                 temporalPassDescriptor;

	// Spatial reconstruction resources
	uint32                                   historyWidth = 0;
	uint32                                   historyHeight = 0;
	// One for direct lighting; another for indirect lighting.
	UniquePtr<Texture>                       raytracingTextures[2];
	UniquePtr<ShaderResourceView>            raytracingSRVs[2];
	UniquePtr<UnorderedAccessView>           raytracingUAVs[2];
	// Track and filter direct and indirect lighting separately.
	TextureSequence                          directColorHistory;
	TextureSequence                          directMomentHistory;
	TextureSequence                          giColorHistory;
	TextureSequence                          giMomentHistory;
	// Final direct and indirect lighting results.
	UniquePtr<Texture>                       finalTextures[2];
	UniquePtr<UnorderedAccessView>           finalUAVs[2];

	// Final merge
	UniquePtr<ComputePipelineState>          finalMergePipeline;
	VolatileDescriptorHelper                 finalMergePassDescriptor;
};
