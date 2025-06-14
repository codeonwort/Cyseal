#pragma once

#include "core/smart_pointer.h"
#include "rhi/gpu_resource.h"
#include "rhi/buffer.h"
#include "rhi/texture.h"

// Application-side view of GPU resources.

template<typename T>
class GPUResourceAsset
{
public:
	GPUResourceAsset(SharedPtr<T> inRHI = nullptr)
		: rhi(inRHI)
	{}

	~GPUResourceAsset()
	{
		rhi.reset();
	}

	inline SharedPtr<T> getGPUResource() const { return rhi; }
	inline void setGPUResource(SharedPtr<T> inRHI) { rhi = inRHI; }

private:
	SharedPtr<T> rhi;
};

using TextureAsset = GPUResourceAsset<Texture>;
using VertexBufferAsset = GPUResourceAsset<VertexBuffer>;
using IndexBufferAsset = GPUResourceAsset<IndexBuffer>;
