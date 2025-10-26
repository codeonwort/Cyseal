#pragma once

#include "core/int_types.h"
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

	ConstantBufferView*    sceneUniformBuffer;
	GPUScene*              gpuScene;
	AccelerationStructure* raytracingScene;
	ShaderResourceView*    skyboxSRV;
	Texture*               gbuffer0Texture;
	Texture*               gbuffer1Texture;
	ShaderResourceView*    gbuffer0SRV;
	ShaderResourceView*    gbuffer1SRV;
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
	void initialize();

	bool isAvailable() const;

	void renderIndirectSpecular(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectSpecularInput& passInput);

private:
	void initializeClassifierPipeline();
	void initializeRaytracingPipeline();
	void initializeTemporalPipeline();
	void initializeAMDReflectionDenoiser();

	void resizeTextures(RenderCommandList* commandList, uint32 newWidth, uint32 newHeight);
	void resizeHitGroupShaderTable(uint32 swapchainIndex, uint32 maxRecords);

	// classifierPhase requires some resources that are created by raytracingPhase.
	// But classifierPhase runs first, so prepare such resources here.
	void prepareRaytracingResources(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectSpecularInput& passInput);

	void classifierPhase(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectSpecularInput& passInput);
	void raytracingPhase(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectSpecularInput& passInput);
	void legacyDenoisingPhase(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectSpecularInput& passInput);

	void amdReprojPhase(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectSpecularInput& passInput);

private:
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
	UniquePtr<Texture>                       raytracingTexture;
	UniquePtr<ShaderResourceView>            raytracingSRV;
	UniquePtr<UnorderedAccessView>           raytracingUAV;
	UniquePtr<RenderTargetView>              raytracingRTV;

private:
	UniquePtr<ComputePipelineState>          amdReprojectPipeline;
	VolatileDescriptorHelper                 amdReprojectPassDescriptor;
};
