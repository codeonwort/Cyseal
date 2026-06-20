#pragma once

#include "core/int_types.h"
#include "core/cymath.h"
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
	EIndirectDiffuseMode       mode;
	EIndirectDiffuseDebugMode  debugMode;
	uint32                     randomSeed; // Ignored if zero.

	uint32                     unscaledRenderWidth;  // display resolution x
	uint32                     unscaledRenderHeight; // display resolution y
	uint32                     sceneWidth;           // render resolution x
	uint32                     sceneHeight;          // render resolution y

	class GPUScene*            gpuScene;
	class BilateralBlur*       bilateralBlur;

	// Bilateral blur coefficients
	float                      cPhi;
	float                      nPhi;
	float                      pPhi;

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
	struct PassFrameInfo
	{
		uint32 currFrame;
		uint32 prevFrame;
	};

public:
	void initialize(RenderDevice* inDevice);

	bool isAvailable() const;

	void renderIndirectDiffuse(RenderCommandList* commandList, const FrameInfo& frameInfo, const IndirectDiffuseInput& passInput);

private:
	void initializeRaytracingPipeline();
	void initializeTemporalPipeline();

	void resizeTextures(RenderCommandList* commandList, uint32 newUnscaledWidth, uint32 newUnscaledHeight);
	void resizeHitGroupShaderTable(uint32 resourceIndex, uint32 maxRecords);

	void prepareRaytracingResources(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectDiffuseInput& passInput);
	void raytracingPhase(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectDiffuseInput& passInput);
	void reprojectPhase(RenderCommandList* commandList, const PassFrameInfo& passFrameInfo, const IndirectDiffuseInput& passInput);
	void denoisePhase(RenderCommandList* commandList, const FrameInfo& frameInfo, const PassFrameInfo& passFrameInfo, const IndirectDiffuseInput& passInput);

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

	// Resources
	uint32                                   unscaledHistoryWidth = 0;
	uint32                                   unscaledHistoryHeight = 0;
	uint32                                   actualHistoryWidth[2];
	uint32                                   actualHistoryHeight[2];
	UniquePtr<Texture>                       raytracingTexture;
	UniquePtr<ShaderResourceView>            raytracingSRV;
	UniquePtr<UnorderedAccessView>           raytracingUAV;
	TextureSequence                          colorHistory;
	TextureSequence                          momentHistory;
	uint32                                   frameCounter = 0;
	UniquePtr<ShaderResourceView>            stbnSRV;
};
