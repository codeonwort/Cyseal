#pragma once

#include "render/gpu_resource.h"
#include "render/texture.h"
#include "d3d_util.h"

class D3DTexture : public Texture
{
public:
	void initialize(const TextureCreateParams& params);

	virtual void uploadData(RenderCommandList& commandList, const void* buffer, uint64 rowPitch, uint64 slicePitch);
	virtual void setDebugName(const wchar_t* debugName);

private:
	WRL::ComPtr<ID3D12Resource> rawResource;
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;

	// Note: ComPtr's are CPU objects but this resource needs to stay in scope until
	// the command list that references it has finished executing on the GPU.
	// We will flush the GPU at the end of this method to ensure the resource is not
	// prematurely destroyed.
	WRL::ComPtr<ID3D12Resource> textureUploadHeap;
};
