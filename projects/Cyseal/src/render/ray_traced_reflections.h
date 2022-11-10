#pragma once

#include <memory>

class RenderCommandList;
class Material;
class SceneProxy;
class Camera;
class RootSignature;
class RaytracingPipelineStateObject;

class RayTracedReflections final
{
public:
	void initialize();

	bool isAvailable() const;

	void renderRayTracedReflections(
		RenderCommandList* commandList,
		const SceneProxy* scene,
		const Camera* camera);

private:
	//void something();

private:
	std::unique_ptr<RaytracingPipelineStateObject> RTPSO;
	std::unique_ptr<RootSignature> globalRootSignature;
	std::unique_ptr<RootSignature> localRootSignature;
};
