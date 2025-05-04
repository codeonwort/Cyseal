#pragma once

#include "core/int_types.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "render/scene_render_pass.h"
#include "render/renderer_options.h"
#include "render/util/volatile_descriptor.h"
#include "render/util/texture_sequence.h"

class SceneProxy;
class Camera;

struct IndirectDiffuseInput
{
	const SceneProxy*          scene;
	const Camera*              camera;
	EIndirectDiffuseMode       mode;

	uint32                     sceneWidth;
	uint32                     sceneHeight;

	class GPUScene*            gpuScene;
	class BilateralBlur*       bilateralBlur;

	ConstantBufferView*        sceneUniformBuffer;
	AccelerationStructure*     raytracingScene;
	ShaderResourceView*        skyboxSRV;
	ShaderResourceView*        gbuffer0SRV;
	ShaderResourceView*        gbuffer1SRV;
	ShaderResourceView*        sceneDepthSRV;
	ShaderResourceView*        prevSceneDepthSRV;
	ShaderResourceView*        velocityMapSRV;

	Texture*                   indirectDiffuseTexture;
	UnorderedAccessView*       indirectDiffuseUAV;
};

class IndirectDiffusePass final : public SceneRenderPass
{
public:
	void initialize();

	bool isAvailable() const;

	void renderIndirectDiffuse(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectDiffuseInput& passInput);

private:
	void initializeRaytracingPipeline();
	void initializeTemporalPipeline();

	void resizeTextures(RenderCommandList* commandList, uint32 newWidth, uint32 newHeight);
	void resizeHitGroupShaderTable(uint32 swapchainIndex, uint32 maxRecords);

private:
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

	// Resources
	uint32                                   historyWidth = 0;
	uint32                                   historyHeight = 0;
	UniquePtr<Texture>                       raytracingTexture;
	UniquePtr<ShaderResourceView>            raytracingSRV;
	UniquePtr<UnorderedAccessView>           raytracingUAV;
	TextureSequence                          colorHistory;
	TextureSequence                          momentHistory;
	uint32                                   frameCounter = 0;
	UniquePtr<ShaderResourceView>            stbnSRV;
};
