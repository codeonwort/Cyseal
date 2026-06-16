#pragma once

#include "core/int_types.h"
#include "core/matrix.h"
#include "core/cymath.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource_view.h"
#include "render/scene_render_pass.h"
#include "render/renderer_options.h"
#include "render/util/volatile_descriptor.h"
#include "render/util/texture_sequence.h"

class MaterialAsset;
class SceneProxy;
class Camera;
class GPUScene;

struct IndirectSpecularInput
{
	class ClearResourcePass*   clearResourcePass;

	const SceneProxy*          scene;
	EIndirectSpecularMode      mode;
	EIndirectSpecularDebugMode debugMode;
	uint32                     randomSeed;

	uint32                     unscaledRenderWidth;
	uint32                     unscaledRenderHeight;
	uint32                     sceneWidth;
	uint32                     sceneHeight;
	Float4x4                   invProjection;
	Float4x4                   invView;
	Float4x4                   prevViewProjection;

	ConstantBufferView*        sceneUniformBuffer;
	GPUScene*                  gpuScene;
	AccelerationStructure*     raytracingScene;
	ShaderResourceView*        skyboxSRV;
	Texture*                   gbuffer0Texture;
	Texture*                   gbuffer1Texture;
	ShaderResourceView*        gbuffer0SRV;
	ShaderResourceView*        gbuffer1SRV;
	Texture*                   normalTexture;
	ShaderResourceView*        normalSRV;
	Texture*                   roughnessTexture;
	ShaderResourceView*        roughnessSRV;
	Texture*                   prevNormalTexture;
	ShaderResourceView*        prevNormalSRV;
	Texture*                   prevRoughnessTexture;
	ShaderResourceView*        prevRoughnessSRV;
	Texture*                   sceneDepthTexture;
	ShaderResourceView*        sceneDepthSRV;
	Texture*                   prevSceneDepthTexture;
	ShaderResourceView*        prevSceneDepthSRV;
	Texture*                   hizTexture;
	ShaderResourceView*        hizSRV;
	Texture*                   velocityMapTexture;
	ShaderResourceView*        velocityMapSRV;
	Buffer*                    tileCoordBuffer;
	Buffer*                    tileCounterBuffer;
	UnorderedAccessView*       tileCoordBufferUAV;
	UnorderedAccessView*       tileCounterBufferUAV;
	Texture*                   indirectSpecularTexture;
};

class IndirecSpecularPass final : public SceneRenderPass
{
	struct PassFrameInfo
	{
		uint32 currFrame;
		uint32 prevFrame;
	};
	struct RaytracingPipelineResources
	{
		UniquePtr<RaytracingPipelineStateObject> pipelineStateObject;
		UniquePtr<RaytracingShaderTable>         raygenShaderTable;
		UniquePtr<RaytracingShaderTable>         missShaderTable;
		BufferedUniquePtr<RaytracingShaderTable> hitGroupShaderTable;
	};

public:
	void initialize(RenderDevice* inRenderDevice);

	bool isAvailable() const;

	void renderIndirectSpecular(RenderCommandList* commandList, const FrameInfo& frameInfo, const IndirectSpecularInput& passInput);

private:
	void initializeClassifierPipeline();
	void initializeRaytracingPipeline();
	void initializeTemporalPipeline();

	void initializeAMDReflectionDenoiser();
	void initializeAMDFinalizeColor();

	void resizeTextures(RenderCommandList* commandList, uint32 newUnscaledWidth, uint32 newUnscaledHeight);
	void resizeHitGroupShaderTable(const PassFrameInfo& passFrameInfo, uint32 maxRecords);

	// classifierPhase requires some resources that are created by raytracingPhase.
	// But classifierPhase runs first, so prepare such resources here.
	void prepareRaytracingResources(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectSpecularInput& passInput, const RaytracingPipelineResources& rayResources);

	void classifierPhase(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectSpecularInput& passInput);
	void raytracingPhase(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectSpecularInput& passInput, const RaytracingPipelineResources& rayResources);
	void legacyDenoisingPhase(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectSpecularInput& passInput);

	void amdReprojPhase(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectSpecularInput& passInput);
	void amdPrefilterPhase(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectSpecularInput& passInput);
	void amdResolveTemporalPhase(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectSpecularInput& passInput);
	void amdFinalizeOutputPhase(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectSpecularInput& passInput);

private:
	RenderDevice*                            device = nullptr;

	RNG<float>                               rng;

	// Tile classification pass
	UniquePtr<ComputePipelineState>          classifierPipeline;
	VolatileDescriptorHelper                 classifierPassDescriptor;
	UniquePtr<ComputePipelineState>          indirectRaysPipeline;
	VolatileDescriptorHelper                 indirectRaysPassDescriptor;

	// Ray pass
	RaytracingPipelineResources              raytracingPipelineResources[2]; // standard, debug mode
	std::vector<uint32>                      totalHitGroupShaderRecord;
	VolatileDescriptorHelper                 rayPassDescriptor;

	// Ray debug pass (reuses ray pass resources other than raygen shader)
	UniquePtr<RaytracingPipelineStateObject> rayDebugPipeline;

	// Ray pass indirect dispatch
	UniquePtr<CommandSignature>              rayCommandSignature;
	UniquePtr<IndirectCommandGenerator>      rayCommandGenerator;
	UniquePtr<Buffer>                        rayCommandBuffer;
	UniquePtr<UnorderedAccessView>           rayCommandBufferUAV;

	// Temporal pass
	UniquePtr<ComputePipelineState>          temporalPipeline;
	VolatileDescriptorHelper                 temporalPassDescriptor;

	uint32                                   unscaledHistoryWidth = 0;
	uint32                                   unscaledHistoryHeight = 0;
	uint32                                   actualHistoryWidth[2];
	uint32                                   actualHistoryHeight[2];

	TextureSequence                          colorHistory;
	TextureSequence                          momentHistory;
	TextureSequence                          sampleCountHistory;
	UniquePtr<Texture>                       raytracingTexture;
	UniquePtr<ShaderResourceView>            raytracingSRV;
	UniquePtr<UnorderedAccessView>           raytracingUAV;

// Only for AMD reflection denoiser
private:
	UniquePtr<ComputePipelineState>          amdReprojectPipeline;
	VolatileDescriptorHelper                 amdReprojectPassDescriptor;

	UniquePtr<ComputePipelineState>          amdPrefilterPipeline;
	VolatileDescriptorHelper                 amdPrefilterPassDescriptor;

	UniquePtr<ComputePipelineState>          amdResolveTemporalPipeline;
	VolatileDescriptorHelper                 amdResolveTemporalPassDescriptor;

	UniquePtr<CommandSignature>              amdCommandSignature;
	UniquePtr<IndirectCommandGenerator>      amdCommandGenerator;
	UniquePtr<Buffer>                        amdCommandBuffer;
	UniquePtr<UnorderedAccessView>           amdCommandBufferUAV;

	TextureSequence                          avgRadianceHistory;
	UniquePtr<Texture>                       reprojectedRadianceTexture;
	UniquePtr<ShaderResourceView>            reprojectedRadianceSRV;
	UniquePtr<UnorderedAccessView>           reprojectedRadianceUAV;
	TextureSequence                          amdRadianceHistory;
	TextureSequence                          amdVarianceHistory;
	TextureSequence                          amdSampleCountHistory;

	// Only necessary portions are updated by indirect dispatch, so the whole radiance texture is quite dirty.
	// Prepare a texture, clear it every frame, then copy valid pixels from the radiance texture.
	// This texture is a waste of bandwidth and memory, just here for convenience of coding...
	UniquePtr<ComputePipelineState>          amdFinalizePipeline;
	VolatileDescriptorHelper                 amdFinalizePassDescriptor;
	UniquePtr<Texture>                       amdFinalColorTexture;
	UniquePtr<RenderTargetView>              amdFinalColorRTV;
	UniquePtr<UnorderedAccessView>           amdFinalColorUAV;
};
