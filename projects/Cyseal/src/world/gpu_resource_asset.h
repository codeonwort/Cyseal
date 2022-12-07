#pragma once

#include <memory>
#include "rhi/gpu_resource.h"

// Application-side view of GPU resources.

template<typename T>
class GPUResourceAsset
{
public:
	GPUResourceAsset(std::shared_ptr<T> inRHI = nullptr)
		: rhi(inRHI)
	{}

	inline std::shared_ptr<T> getGPUResource() const { return rhi; }
	inline void setGPUResource(std::shared_ptr<T> inRHI) { rhi = inRHI; }

private:
	std::shared_ptr<T> rhi;
};

using TextureAsset = GPUResourceAsset<Texture>;
using VertexBufferAsset = GPUResourceAsset<VertexBuffer>;
using IndexBufferAsset = GPUResourceAsset<IndexBuffer>;
