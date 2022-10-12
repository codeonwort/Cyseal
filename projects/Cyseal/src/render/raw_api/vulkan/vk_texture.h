#pragma once

#include "render/gpu_resource.h"
#include "render/texture.h"

// #todo-vulkan: Texture wrapper
class VulkanTexture : public Texture
{
public:
	void initialize(const TextureCreateParams& params) {}

	virtual void uploadData(RenderCommandList& commandList, const void* buffer, uint64 rowPitch, uint64 slicePitch) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	virtual RenderTargetView* getRTV() const override { return nullptr; }
	virtual ShaderResourceView* getSRV() const override { return nullptr; }
	virtual DepthStencilView* getDSV() const override { return nullptr; }

	virtual void setDebugName(const wchar_t* debugName) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}


	virtual uint32 getSRVDescriptorIndex() const override
	{
		//throw std::logic_error("The method or operation is not implemented.");
		return 0;
	}

	virtual uint32 getRTVDescriptorIndex() const override
	{
		//throw std::logic_error("The method or operation is not implemented.");
		return 0;
	}

	virtual uint32 getDSVDescriptorIndex() const override
	{
		return 0;
	}

	virtual uint32 getUAVDescriptorIndex() const override
	{
		//throw std::logic_error("The method or operation is not implemented.");
		return 0;
	}

private:
	// vkImage here
};
