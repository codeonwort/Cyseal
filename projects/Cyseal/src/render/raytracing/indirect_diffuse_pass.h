#pragma once

#include "core/int_types.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "render/renderer_options.h"
#include "render/util/volatile_descriptor.h"

class MaterialAsset;
class SceneProxy;
class Camera;
class GPUScene;

struct IndirectDiffuseInput
{
	const SceneProxy*          scene;
	const Camera*              camera;
	EIndirectDiffuseMode       mode;

	Float4x4                   prevViewProjInvMatrix;
	Float4x4                   prevViewProjMatrix;
	uint32                     sceneWidth;
	uint32                     sceneHeight;

	ConstantBufferView*        sceneUniformBuffer;
	GPUScene*                  gpuScene;
	AccelerationStructure*     raytracingScene;
	ShaderResourceView*        skyboxSRV;
	ShaderResourceView*        gbuffer0SRV;
	ShaderResourceView*        gbuffer1SRV;
	ShaderResourceView*        sceneDepthSRV;
	ShaderResourceView*        prevSceneDepthSRV;
	UnorderedAccessView*       indirectDiffuseUAV;
};

class IndirectDiffusePass final
{
public:
	void initialize();

	bool isAvailable() const;

	void renderIndirectDiffuse(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectDiffuseInput& passInput);

private:
	void resizeTextures(RenderCommandList* commandList, uint32 newWidth, uint32 newHeight);
	void resizeHitGroupShaderTable(uint32 swapchainIndex, uint32 maxRecords);

private:
	UniquePtr<RaytracingPipelineStateObject> RTPSO;

	UniquePtr<RaytracingShaderTable> raygenShaderTable;
	UniquePtr<RaytracingShaderTable> missShaderTable;
	BufferedUniquePtr<RaytracingShaderTable> hitGroupShaderTable;
	std::vector<uint32> totalHitGroupShaderRecord;

	UniquePtr<ComputePipelineState> blurPipelineState;

	uint32 historyWidth = 0;
	uint32 historyHeight = 0;
	UniquePtr<Texture> colorHistory[2];
	UniquePtr<UnorderedAccessView> colorHistoryUAV[2];
	UniquePtr<ShaderResourceView> colorHistorySRV[2];
	UniquePtr<Texture> momentHistory[2];
	UniquePtr<UnorderedAccessView> momentHistoryUAV[2];
	UniquePtr<Texture> colorScratch;
	UniquePtr<UnorderedAccessView> colorScratchUAV;

	uint32 frameCounter = 0;
	UniquePtr<ShaderResourceView> stbnSRV;

	VolatileDescriptorHelper rayPassDescriptor;
	VolatileDescriptorHelper blurPassDescriptor;
};
