#pragma once

#include "render/gpu_resource.h"
#include "render/texture.h"
#include "d3d_util.h"

class RenderTargetView;
class ShaderResourceView;
class D3DRenderTargetView;
class D3DShaderResourceView;
class D3DDepthStencilView;

class D3DTexture : public Texture
{
public:
	void initialize(const TextureCreateParams& params);

	D3D12_GPU_VIRTUAL_ADDRESS getGPUVirtualAddress() const;

	virtual void uploadData(RenderCommandList& commandList, const void* buffer, uint64 rowPitch, uint64 slicePitch) override;
	virtual void setDebugName(const wchar_t* debugName) override;

	virtual RenderTargetView* getRTV() const override;
	virtual ShaderResourceView* getSRV() const override;
	virtual DepthStencilView* getDSV() const override;

	virtual uint32 getSRVDescriptorIndex() const override { return srvDescriptorIndex; }
	virtual uint32 getRTVDescriptorIndex() const override { return rtvDescriptorIndex; }
	virtual uint32 getDSVDescriptorIndex() const override { return dsvDescriptorIndex; }
	virtual uint32 getUAVDescriptorIndex() const override { return uavDescriptorIndex; }

private:
	WRL::ComPtr<ID3D12Resource> rawResource;
	TextureCreateParams createParams;

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = { NULL };
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = { NULL };
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = { NULL };
	D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = { NULL };
	uint32 srvDescriptorIndex = 0xffffffff;
	uint32 rtvDescriptorIndex = 0xffffffff;
	uint32 dsvDescriptorIndex = 0xffffffff;
	uint32 uavDescriptorIndex = 0xffffffff;

	std::unique_ptr<D3DRenderTargetView> rtv;
	std::unique_ptr<D3DShaderResourceView> srv;
	std::unique_ptr<D3DDepthStencilView> dsv;

	// Note: ComPtr's are CPU objects but this resource needs to stay in scope until
	// the command list that references it has finished executing on the GPU.
	// We will flush the GPU at the end of this method to ensure the resource is not
	// prematurely destroyed.
	WRL::ComPtr<ID3D12Resource> textureUploadHeap;
};
