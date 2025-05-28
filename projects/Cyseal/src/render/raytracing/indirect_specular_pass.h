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
	ShaderResourceView*    gbuffer0SRV;
	ShaderResourceView*    gbuffer1SRV;
	ShaderResourceView*    sceneDepthSRV;
	ShaderResourceView*    prevSceneDepthSRV;
	ShaderResourceView*    velocityMapSRV;
	Texture*               indirectSpecularTexture;
};

class IndirecSpecularPass final : public SceneRenderPass
{
public:
	void initialize();

	bool isAvailable() const;

	void renderIndirectSpecular(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectSpecularInput& passInput);

private:
	void initializeRaytracingPipeline();
	void initializeTemporalPipeline();

	void resizeTextures(RenderCommandList* commandList, uint32 newWidth, uint32 newHeight);
	void resizeHitGroupShaderTable(uint32 swapchainIndex, uint32 maxRecords);

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
	TextureSequence                          colorHistory;
	TextureSequence                          momentHistory;
	UniquePtr<Texture>                       raytracingTexture;
	UniquePtr<ShaderResourceView>            raytracingSRV;
	UniquePtr<UnorderedAccessView>           raytracingUAV;
};
