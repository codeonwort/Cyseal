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
	
	void updateSceneUniform(
		RenderCommandList* commandList,
		uint32 swapchainIndex,
		const SceneProxy* scene,
		const Camera* camera);

	void rebuildAccelerationStructure(RenderCommandList* commandList, const SceneProxy* scene);

private:
	RenderDevice* device = nullptr;

	// ------------------------------------------------------------------------
	// #todo-renderer: Temporarily manage render targets in the renderer.
	Texture* RT_sceneColor = nullptr;
	UniquePtr<ShaderResourceView> sceneColorSRV;

	Texture* RT_sceneDepth = nullptr;
	UniquePtr<DepthStencilView> sceneDepthDSV;

	// Gonna stick to forward shading, but render thin GBuffers like DOOM reboot series.
	Texture* RT_thinGBufferA = nullptr; // #todo-renderer: Maybe switch to R10G10B10A2?

	Texture* RT_indirectSpecular = nullptr;
	UniquePtr<ShaderResourceView> indirectSpecularSRV;

	Texture* RT_pathTracing = nullptr;
	UniquePtr<ShaderResourceView> pathTracingSRV;

	// #todo-renderer: Temp dedicated memory and desc heap for scene uniforms
	UniquePtr<Buffer> sceneUniformMemory;
	UniquePtr<DescriptorHeap> sceneUniformDescriptorHeap;
	BufferedUniquePtr<ConstantBufferView> sceneUniformCBVs;

	AccelerationStructure* accelStructure = nullptr;

	UniquePtr<ShaderResourceView> grey2DSRV; // SRV for fallback texture

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
