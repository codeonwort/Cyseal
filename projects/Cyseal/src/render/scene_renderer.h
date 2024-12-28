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
class RayTracedReflections;
class ToneMapping;
class BufferVisualization;
class PathTracingPass;

// Render a 3D scene with hybrid rendering. (rasterization + raytracing)
class SceneRenderer final : public Renderer
{
public:
	~SceneRenderer() = default;

	virtual void initialize(RenderDevice* renderDevice) override;
	virtual void destroy() override;
	virtual void render(const SceneProxy* scene, const Camera* camera, const RendererOptions& renderOptions) override;

	virtual void recreateSceneTextures(uint32 sceneWidth, uint32 sceneHeight) override;
	
private:
	void updateSceneUniform(
		RenderCommandList* commandList,
		uint32 swapchainIndex,
		const SceneProxy* scene,
		const Camera* camera);

	void rebuildFrameResources(RenderCommandList* commandList, const SceneProxy* scene);

	void rebuildAccelerationStructure(RenderCommandList* commandList, const SceneProxy* scene);

private:
	RenderDevice* device = nullptr;

	struct DeferredCleanup { GPUResource* resource; /*uint32 count;*/ }; // Don't remember why I put 'count' there...?
	std::vector<DeferredCleanup> deferredCleanupList;

	// ------------------------------------------------------------------------
	// #todo-renderer: Temporarily manage render targets in the renderer.
	UniquePtr<Texture> RT_sceneColor;
	UniquePtr<ShaderResourceView> sceneColorSRV;
	UniquePtr<RenderTargetView> sceneColorRTV;

	UniquePtr<Texture> RT_sceneDepth;
	UniquePtr<DepthStencilView> sceneDepthDSV;

	// Gonna stick to forward shading, but render thin GBuffers like DOOM reboot series.
	UniquePtr<Texture> RT_thinGBufferA; // #todo-renderer: Maybe switch to R10G10B10A2?
	UniquePtr<RenderTargetView> thinGBufferARTV;
	UniquePtr<UnorderedAccessView> thinGBufferAUAV;

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
	GPUScene*             gpuScene            = nullptr;
	GPUCulling*           gpuCulling          = nullptr;
	BasePass*             basePass            = nullptr;
	RayTracedReflections* rtReflections       = nullptr;
	ToneMapping*          toneMapping         = nullptr;
	BufferVisualization*  bufferVisualization = nullptr;

	PathTracingPass*      pathTracingPass     = nullptr;
};
