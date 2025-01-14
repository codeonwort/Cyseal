#pragma once

#include "core/int_types.h"
#include "core/smart_pointer.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/rhi_forward.h"
#include "render/renderer.h"
#include "render/util/volatile_descriptor.h"

class Material;
class SceneProxy;
class Camera;
class GPUScene;

struct IndirectSpecularInput
{
	const SceneProxy*          scene;
	const Camera*              camera;
	EIndirectSpecularMode      mode;

	Float4x4                   prevViewInvMatrix;
	Float4x4                   prevProjInvMatrix;
	Float4x4                   prevViewProjMatrix;
	bool                       bCameraHasMoved;
	uint32                     sceneWidth;
	uint32                     sceneHeight;

	ConstantBufferView*        sceneUniformBuffer;
	GPUScene*                  gpuScene;
	AccelerationStructure*     raytracingScene;
	ShaderResourceView*        skyboxSRV;
	ShaderResourceView*        gbuffer0SRV;
	ShaderResourceView*        gbuffer1SRV;
	ShaderResourceView*        sceneDepthSRV;
	UnorderedAccessView*       indirectSpecularUAV;
};

class IndirecSpecularPass final
{
public:
	void initialize();

	bool isAvailable() const;

	void renderIndirectSpecular(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectSpecularInput& passInput);

private:
	void resizeTextures(RenderCommandList* commandList, uint32 newWidth, uint32 newHeight);
	void resizeHitGroupShaderTable(uint32 swapchainIndex, uint32 maxRecords);

private:
	UniquePtr<RaytracingPipelineStateObject> RTPSO;

	UniquePtr<RaytracingShaderTable> raygenShaderTable;
	UniquePtr<RaytracingShaderTable> missShaderTable;
	BufferedUniquePtr<RaytracingShaderTable> hitGroupShaderTable;
	std::vector<uint32> totalHitGroupShaderRecord;

	uint32 historyWidth = 0;
	uint32 historyHeight = 0;
	UniquePtr<Texture> colorHistory[2];
	UniquePtr<UnorderedAccessView> colorHistoryUAV[2];
	UniquePtr<Texture> momentHistory[2];
	UniquePtr<UnorderedAccessView> momentHistoryUAV[2];
	UniquePtr<Texture> colorScratch;
	UniquePtr<UnorderedAccessView> colorScratchUAV;

	VolatileDescriptorHelper rayPassDescriptor;
};
