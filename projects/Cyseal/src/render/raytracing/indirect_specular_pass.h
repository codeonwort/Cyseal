#pragma once

#include "core/int_types.h"
#include "core/matrix.h"
#include "core/smart_pointer.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/rhi_forward.h"
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
	const SceneProxy*      scene;
	EIndirectSpecularMode  mode;

	uint32                 sceneWidth;
	uint32                 sceneHeight;
	Float4x4               invProjection;
	Float4x4               invView;
	Float4x4               prevViewProjection;

	ConstantBufferView*    sceneUniformBuffer;
	GPUScene*              gpuScene;
	AccelerationStructure* raytracingScene;
	ShaderResourceView*    skyboxSRV;
	Texture*               gbuffer0Texture;
	Texture*               gbuffer1Texture;
	ShaderResourceView*    gbuffer0SRV;
	ShaderResourceView*    gbuffer1SRV;
	Texture*               normalTexture;
	ShaderResourceView*    normalSRV;
	Texture*               roughnessTexture;
	ShaderResourceView*    roughnessSRV;
	Texture*               prevNormalTexture;
	ShaderResourceView*    prevNormalSRV;
	Texture*               prevRoughnessTexture;
	ShaderResourceView*    prevRoughnessSRV;
	Texture*               sceneDepthTexture;
	ShaderResourceView*    sceneDepthSRV;
	Texture*               prevSceneDepthTexture;
	ShaderResourceView*    prevSceneDepthSRV;
	Texture*               hizTexture;
	ShaderResourceView*    hizSRV;
	Texture*               velocityMapTexture;
	ShaderResourceView*    velocityMapSRV;
	Buffer*                tileCoordBuffer;
	Buffer*                tileCounterBuffer;
	UnorderedAccessView*   tileCoordBufferUAV;
	UnorderedAccessView*   tileCounterBufferUAV;
	Texture*               indirectSpecularTexture;
};

class IndirecSpecularPass final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inRenderDevice);

	bool isAvailable() const;

	void renderIndirectSpecular(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectSpecularInput& passInput);

private:
	void initializeClassifierPipeline();
	void initializeRaytracingPipeline();
	void initializeTemporalPipeline();

	void initializeAMDReflectionDenoiser();
	void initializeAMDFinalizeColor();

	void resizeTextures(RenderCommandList* commandList, uint32 newWidth, uint32 newHeight);
	void resizeHitGroupShaderTable(uint32 swapchainIndex, uint32 maxRecords);

	// classifierPhase requires some resources that are created by raytracingPhase.
	// But classifierPhase runs first, so prepare such resources here.
	void prepareRaytracingResources(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectSpecularInput& passInput);

	void classifierPhase(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectSpecularInput& passInput);
	void raytracingPhase(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectSpecularInput& passInput);
	void legacyDenoisingPhase(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectSpecularInput& passInput);

	void amdReprojPhase(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectSpecularInput& passInput);
	void amdPrefilterPhase(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectSpecularInput& passInput);
	void amdResolveTemporalPhase(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectSpecularInput& passInput);
	void amdFinalizeOutputPhase(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectSpecularInput& passInput);

private:
	RenderDevice*                            device = nullptr;

	// Tile classification pass
	UniquePtr<ComputePipelineState>          classifierPipeline;
	VolatileDescriptorHelper                 classifierPassDescriptor;
	UniquePtr<ComputePipelineState>          indirectRaysPipeline;
	VolatileDescriptorHelper                 indirectRaysPassDescriptor;

	// Ray pass
	UniquePtr<RaytracingPipelineStateObject> RTPSO;
	UniquePtr<RaytracingShaderTable>         raygenShaderTable;
	UniquePtr<RaytracingShaderTable>         missShaderTable;
	BufferedUniquePtr<RaytracingShaderTable> hitGroupShaderTable;
	std::vector<uint32>                      totalHitGroupShaderRecord;
	VolatileDescriptorHelper                 rayPassDescriptor;

	// Ray pass indirect dispatch
	UniquePtr<CommandSignature>              rayCommandSignature;
	UniquePtr<IndirectCommandGenerator>      rayCommandGenerator;
	UniquePtr<Buffer>                        rayCommandBuffer;
	UniquePtr<UnorderedAccessView>           rayCommandBufferUAV;

	// Temporal pass
	UniquePtr<ComputePipelineState>          temporalPipeline;
	VolatileDescriptorHelper                 temporalPassDescriptor;

	uint32                                   historyWidth = 0;
	uint32                                   historyHeight = 0;
	TextureSequence                          colorHistory;
	TextureSequence                          momentHistory;
	TextureSequence                          sampleCountHistory;
	UniquePtr<Texture>                       raytracingTexture;
	UniquePtr<ShaderResourceView>            raytracingSRV;
	UniquePtr<UnorderedAccessView>           raytracingUAV;
	UniquePtr<RenderTargetView>              raytracingRTV;

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

	UniquePtr<Texture>                       avgRadianceTexture;
	UniquePtr<ShaderResourceView>            avgRadianceSRV;
	UniquePtr<UnorderedAccessView>           avgRadianceUAV;
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
