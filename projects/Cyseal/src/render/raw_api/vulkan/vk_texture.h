#pragma once

#include "render/gpu_resource.h"
#include "render/texture.h"

// #todo-vulkan: Texture wrapper
class VulkanTexture : public Texture
{
public:
	void initialize(const TextureCreateParams& params) {}

	void uploadData(RenderCommandList& commandList, const void* buffer, uint64 rowPitch, uint64 slicePitch) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	virtual RenderTargetView* getRTV() const override { return nullptr; }


	void setDebugName(const wchar_t* debugName) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}


	uint32 getSRVDescriptorIndex() const override
	{
		//throw std::logic_error("The method or operation is not implemented.");
		return 0;
	}

	uint32 getRTVDescriptorIndex() const override
	{
		//throw std::logic_error("The method or operation is not implemented.");
		return 0;
	}

	uint32 getUAVDescriptorIndex() const override
	{
		//throw std::logic_error("The method or operation is not implemented.");
		return 0;
	}

private:
	// vkImage here
};
