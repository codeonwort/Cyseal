#pragma once

#include "rhi/texture.h"
#include "rhi/gpu_resource_barrier.h"
#include "d3d_util.h"

class D3DDevice;

class D3DTexture : public Texture
{
public:
	D3DTexture(D3DDevice* inDevice) : device(inDevice) {}

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

	void saveLastMemoryLayout(EBarrierLayout layout) { lastMemoryLayout = layout; }

private:
	D3DDevice* device = nullptr;

	WRL::ComPtr<ID3D12Resource> rawResource;
	TextureCreateParams createParams;

	// #wip: Storing state here is not a good idea because multiple command lists could touch the same texture.
	// Same reason why I removed various views (UAV, SRV, RTV, ...) from Texture.
	EBarrierLayout lastMemoryLayout = EBarrierLayout::Common;

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
