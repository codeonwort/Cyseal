#pragma once

#include "rhi/gpu_resource.h"
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

	virtual const TextureCreateParams& getCreateParams() const override { return createParams; }

	virtual void uploadData(
		RenderCommandList& commandList,
		const void* buffer,
		uint64 rowPitch,
		uint64 slicePitch,
		uint32 subresourceIndex = 0) override;
	virtual void setDebugName(const wchar_t* debugName) override;

	virtual RenderTargetView* getRTV() const override;
	virtual ShaderResourceView* getSRV() const override;
	virtual DepthStencilView* getDSV() const override;

	virtual uint32 getSRVDescriptorIndex() const override { return srvDescriptorIndex; }
	virtual uint32 getRTVDescriptorIndex() const override { return rtvDescriptorIndex; }
	virtual uint32 getDSVDescriptorIndex() const override { return dsvDescriptorIndex; }

	virtual DescriptorHeap* getSourceSRVHeap() const override { return srvHeap; }
	virtual DescriptorHeap* getSourceRTVHeap() const override { return rtvHeap; }
	virtual DescriptorHeap* getSourceDSVHeap() const override { return dsvHeap; }

	virtual void* getRawResource() const override { return rawResource.Get(); }

private:
	WRL::ComPtr<ID3D12Resource> rawResource;
	TextureCreateParams createParams;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = { NULL };
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = { NULL };
	uint32 srvDescriptorIndex = 0xffffffff;
	uint32 rtvDescriptorIndex = 0xffffffff;
	uint32 dsvDescriptorIndex = 0xffffffff;

	std::unique_ptr<D3DRenderTargetView> rtv;
	std::unique_ptr<ShaderResourceView> srv;
	std::unique_ptr<D3DDepthStencilView> dsv;

	// Source descriptor heaps from which this texture allocated its descriptors.
	DescriptorHeap* srvHeap = nullptr;
	DescriptorHeap* rtvHeap = nullptr;
	DescriptorHeap* dsvHeap = nullptr;

	// Note: ComPtr's are CPU objects but this resource needs to stay in scope until
	// the command list that references it has finished executing on the GPU.
	// We will flush the GPU at the end of this method to ensure the resource is not
	// prematurely destroyed.
	WRL::ComPtr<ID3D12Resource> textureUploadHeap;
	bool bIsPixelShaderResourceState = false; // I don't have resource barrier auto-tracking :/
};
