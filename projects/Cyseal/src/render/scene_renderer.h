#pragma once

#include "renderer.h"
#include "rhi/gpu_resource_view.h"
#include "core/smart_pointer.h"

class Buffer;
class Texture;
class DescriptorHeap;
class ConstantBufferView;

// Render passes
class GPUScene;
class GPUCulling;
class BasePass;
class IndirecSpecularPass;
class ToneMapping;
class BufferVisualization;
class PathTracingPass;

// Should match with common.hlsl
struct SceneUniform
{
	Float4x4 viewMatrix;
	Float4x4 projMatrix;
	Float4x4 viewProjMatrix;

	Float4x4 viewInvMatrix;
	Float4x4 projInvMatrix;
	Float4x4 viewProjInvMatrix;

	CameraFrustum cameraFrustum;

	vec3 cameraPosition; float _pad0;
	vec3 sunDirection;   float _pad1;
	vec3 sunIlluminance; float _pad2;
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

	// Render gbuffers for hybrid raytracing.
	UniquePtr<Texture> RT_gbuffers[NUM_GBUFFERS];
	UniquePtr<RenderTargetView> gbufferRTVs[NUM_GBUFFERS];
	UniquePtr<ShaderResourceView> gbufferSRVs[NUM_GBUFFERS];
	UniquePtr<UnorderedAccessView> gbufferUAVs[NUM_GBUFFERS];

	UniquePtr<Texture> RT_indirectSpecular;
	UniquePtr<ShaderResourceView> indirectSpecularSRV;
	UniquePtr<RenderTargetView> indirectSpecularRTV;
	UniquePtr<UnorderedAccessView> indirectSpecularUAV;

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
	GPUScene*             gpuScene              = nullptr;
	GPUCulling*           gpuCulling            = nullptr;
	BasePass*             basePass              = nullptr;
	IndirecSpecularPass*  indirectSpecularPass  = nullptr;
	ToneMapping*          toneMapping           = nullptr;
	BufferVisualization*  bufferVisualization   = nullptr;
	PathTracingPass*      pathTracingPass       = nullptr;
};
