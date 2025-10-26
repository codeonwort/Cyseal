#pragma once

#include "renderer.h"
#include "rhi/rhi_forward.h"
#include "rhi/gpu_resource_view.h"
#include "core/smart_pointer.h"

// Should match with common.hlsl
struct SceneUniform
{
	Float4x4      viewMatrix;
	Float4x4      projMatrix;
	Float4x4      viewProjMatrix;

	Float4x4      viewInvMatrix;
	Float4x4      projInvMatrix;
	Float4x4      viewProjInvMatrix;

	Float4x4      prevViewProjMatrix;
	Float4x4      prevViewProjInvMatrix;

	float         screenResolution[4]; // (w, h, 1/w, 1/h)
	CameraFrustum cameraFrustum;
	vec3          cameraPosition; float _pad0;
	vec3          sunDirection;   float _pad1;
	vec3          sunIlluminance; float _pad2;
};

// Render a 3D scene with hybrid rendering. (rasterization + raytracing)
class SceneRenderer final : public Renderer
{
public:
	constexpr static uint32 NUM_GBUFFERS = 2;

public:
	~SceneRenderer() = default;

	virtual void initialize(RenderDevice* renderDevice) override;
	virtual void destroy() override;
	virtual void render(const SceneProxy* scene, const Camera* camera, const RendererOptions& renderOptions) override;

	virtual void recreateSceneTextures(uint32 sceneWidth, uint32 sceneHeight) override;
	
private:
	void resetCommandList(RenderCommandAllocator* commandAllocator, RenderCommandList* commandList);
	void immediateFlushCommandQueue(RenderCommandQueue* commandQueue, RenderCommandAllocator* commandAllocator, RenderCommandList* commandList);

	void updateSceneUniform(RenderCommandList* commandList, uint32 swapchainIndex, const SceneProxy* scene, const Camera* camera);

	void rebuildFrameResources(RenderCommandList* commandList, const SceneProxy* scene);

	void rebuildAccelerationStructure(RenderCommandList* commandList, const SceneProxy* scene);

private:
	RenderDevice* device = nullptr;

	struct DeferredCleanup { GPUResource* resource; /*uint32 count;*/ }; // Don't remember why I put 'count' there...?
	std::vector<DeferredCleanup> deferredCleanupList;

	SceneUniform sceneUniformData;
	SceneUniform prevSceneUniformData;

	// ------------------------------------------------------------------------
	// #todo-renderer: Temporarily manage render targets in the renderer.
	UniquePtr<Texture> RT_sceneColor;
	UniquePtr<ShaderResourceView> sceneColorSRV;
	UniquePtr<RenderTargetView> sceneColorRTV;

	TextureCreateParams sceneDepthDesc;
	UniquePtr<Texture> RT_sceneDepth;
	UniquePtr<DepthStencilView> sceneDepthDSV;
	UniquePtr<ShaderResourceView> sceneDepthSRV;

	UniquePtr<Texture> RT_prevSceneDepth;
	UniquePtr<ShaderResourceView> prevSceneDepthSRV;

	UniquePtr<Texture> RT_hiz;
	UniquePtr<ShaderResourceView> hizSRV;
	BufferedUniquePtr<UnorderedAccessView> hizUAVs;

	UniquePtr<Texture> RT_velocityMap;
	UniquePtr<ShaderResourceView> velocityMapSRV;
	UniquePtr<RenderTargetView> velocityMapRTV;

	// Render gbuffers for hybrid raytracing.
	UniquePtr<Texture> RT_gbuffers[NUM_GBUFFERS];
	UniquePtr<RenderTargetView> gbufferRTVs[NUM_GBUFFERS];
	UniquePtr<ShaderResourceView> gbufferSRVs[NUM_GBUFFERS];
	UniquePtr<UnorderedAccessView> gbufferUAVs[NUM_GBUFFERS];

	UniquePtr<Texture> RT_prevNormalTexture;
	UniquePtr<ShaderResourceView> prevNormalSRV;
	UniquePtr<UnorderedAccessView> prevNormalUAV;

	UniquePtr<Texture> RT_prevRoughnessTexture;
	UniquePtr<ShaderResourceView> prevRoughnessSRV;
	UniquePtr<UnorderedAccessView> prevRoughnessUAV;

	UniquePtr<Texture> RT_shadowMask;
	UniquePtr<RenderTargetView> shadowMaskRTV;
	UniquePtr<ShaderResourceView> shadowMaskSRV;
	UniquePtr<UnorderedAccessView> shadowMaskUAV;

	UniquePtr<Texture> RT_indirectDiffuse;
	UniquePtr<ShaderResourceView> indirectDiffuseSRV;
	UniquePtr<RenderTargetView> indirectDiffuseRTV;
	UniquePtr<UnorderedAccessView> indirectDiffuseUAV;

	UniquePtr<Texture> RT_indirectSpecular;
	UniquePtr<ShaderResourceView> indirectSpecularSRV;
	UniquePtr<RenderTargetView> indirectSpecularRTV;
	UniquePtr<UnorderedAccessView> indirectSpecularUAV;
	UniquePtr<Buffer> indirectSpecularTileCoordBuffer;
	UniquePtr<UnorderedAccessView> indirectSpecularTileCoordBufferUAV;
	UniquePtr<Buffer> indirectSpecularTileCounterBuffer;
	UniquePtr<UnorderedAccessView> indirectSpecularTileCounterBufferUAV;

	UniquePtr<Texture> RT_pathTracing;
	UniquePtr<ShaderResourceView> pathTracingSRV;
	UniquePtr<UnorderedAccessView> pathTracingUAV;

	// #todo-renderer: Temp dedicated memory and desc heap for scene uniforms
	UniquePtr<Buffer> sceneUniformMemory;
	UniquePtr<DescriptorHeap> sceneUniformDescriptorHeap;
	BufferedUniquePtr<ConstantBufferView> sceneUniformCBVs;

	UniquePtr<AccelerationStructure> accelStructure;

	UniquePtr<ShaderResourceView> grey2DSRV; // SRV for fallback texture
	UniquePtr<ShaderResourceView> skyboxSRV;

	// ------------------------------------------------------------------------
	// Render passes
	class GPUScene*             gpuScene              = nullptr;
	class GPUCulling*           gpuCulling            = nullptr;
	class BilateralBlur*        bilateralBlur         = nullptr;
	class RayTracedShadowsPass* rayTracedShadowsPass  = nullptr;
	class BasePass*             basePass              = nullptr;
	class HiZPass*              hizPass               = nullptr;
	class SkyPass*              skyPass               = nullptr;
	class IndirectDiffusePass*  indirectDiffusePass   = nullptr;
	class IndirecSpecularPass*  indirectSpecularPass  = nullptr;
	class ToneMapping*          toneMapping           = nullptr;
	class BufferVisualization*  bufferVisualization   = nullptr;
	class PathTracingPass*      pathTracingPass       = nullptr;
	class DenoiserPluginPass*   denoiserPluginPass    = nullptr;
	class StoreHistoryPass*     storeHistoryPass      = nullptr;

	std::vector<class SceneRenderPass*> sceneRenderPasses;
};
