#pragma once

#include "rhi/texture.h"
#include "rhi/gpu_resource_barrier.h"
#include "d3d_util.h"

class RenderTargetView;
class ShaderResourceView;
class D3DRenderTargetView;
class D3DShaderResourceView;
class D3DDepthStencilView;
class D3DUnorderedAccessView;

class D3DTexture : public Texture
{
public:
	void initialize(const TextureCreateParams& params);

	virtual void setDebugName(const wchar_t* debugName) override;

	virtual const TextureCreateParams& getCreateParams() const override { return createParams; }

	virtual void uploadData(
		RenderCommandList& commandList,
		const void* buffer,
		uint64 rowPitch,
		uint64 slicePitch,
		uint32 subresourceIndex = 0) override;

	virtual uint64 getRowPitch() const override { return rowPitch; }

	virtual uint64 getReadbackBufferSize() const override { return readbackBufferSize; }

	virtual bool prepareReadback(RenderCommandList* commandList) override;

	virtual bool readbackData(void* dst) override;

	virtual void* getRawResource() const override { return rawResource.Get(); }

	void saveLastMemoryLayout(ETextureMemoryLayout layout) { lastMemoryLayout = layout; }

private:
	WRL::ComPtr<ID3D12Resource> rawResource;
	TextureCreateParams createParams;
	ETextureMemoryLayout lastMemoryLayout;

	// Note: ComPtr's are CPU objects but this resource needs to stay in scope until
	// the command list that references it has finished executing on the GPU.
	// We will flush the GPU at the end of this method to ensure the resource is not
	// prematurely destroyed.
	WRL::ComPtr<ID3D12Resource> textureUploadHeap;

	uint64 rowPitch = 0;

	WRL::ComPtr<ID3D12Resource> readbackBuffer;
	D3D12_TEXTURE_COPY_LOCATION readbackFootprintDesc;
	uint64 readbackBufferSize = 0;
	bool bReadbackPrepared = false;
};
