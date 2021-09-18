#pragma once

#include "render/render_device.h"
#include "d3d_util.h"
#include "d3d_swap_chain.h"

// #todo-renderdevice: Magic number
#define MAX_SRV_DESCRIPTORS 1024

class D3DDevice : public RenderDevice
{

public:
	D3DDevice();
	~D3DDevice();

	virtual void initialize(const RenderDeviceCreateParams& createParams) override;

	virtual void recreateSwapChain(HWND hwnd, uint32 width, uint32 height) override;

	virtual void flushCommandQueue() override;

	virtual bool supportsRayTracing() override;

	virtual VertexBuffer* createVertexBuffer(void* data, uint32 sizeInBytes, uint32 strideInBytes) override;
	virtual IndexBuffer* createIndexBuffer(void* data, uint32 sizeInBytes, EPixelFormat format) override;
	virtual Texture* createTexture(const TextureCreateParams& createParams) override;
	virtual Shader* createShader() override;
	virtual RootSignature* createRootSignature(const RootSignatureDesc& desc) override;
	virtual PipelineState* createGraphicsPipelineState(const GraphicsPipelineDesc& desc) override;

	virtual DescriptorHeap* createDescriptorHeap(const DescriptorHeapDesc& desc) override;
	virtual ConstantBuffer* createConstantBuffer(DescriptorHeap* descriptorHeap, uint32 heapSize, uint32 payloadSize) override;

	inline IDXGIFactory4* getDXGIFactory() const { return dxgiFactory.Get(); }
	inline ID3D12Device5* getRawDevice() const { return device.Get(); }
	inline ID3D12CommandQueue* getRawCommandQueue() const { return rawCommandQueue; }

	// #todo-renderdevice: Needs abstraction layer and release mechanism
	inline ID3D12DescriptorHeap* getRawSRVHeap() const { return heapSRV.Get(); }
	inline D3D12_CPU_DESCRIPTOR_HANDLE allocateSRVHeapHandle()
	{
		CHECK(heapSRVOffset < MAX_SRV_DESCRIPTORS);

		D3D12_CPU_DESCRIPTOR_HANDLE handle = heapSRV->GetCPUDescriptorHandleForHeapStart();
		handle.ptr += SIZE_T(heapSRVOffset) * SIZE_T(descSizeCBV_SRV_UAV);
		++heapSRVOffset;

		return handle;
	}

private:
	void getHardwareAdapter(IDXGIFactory2* factory, IDXGIAdapter1** outAdapter);

	inline D3D12_CPU_DESCRIPTOR_HANDLE rawGetDepthStencilView() const
	{
		return heapDSV->GetCPUDescriptorHandleForHeapStart();
	}

// #todo-renderdevice: Move non-renderdevice members into other places
private:
	WRL::ComPtr<IDXGIFactory4>        dxgiFactory;

	WRL::ComPtr<ID3D12Device5>        device;

	WRL::ComPtr<ID3D12Fence>          fence;
	UINT                              currentFence;

	UINT                              descSizeRTV;
	UINT                              descSizeDSV;
	UINT                              descSizeCBV_SRV_UAV;
	UINT                              quality4xMSAA;

	bool                              rayTracingEnabled;

	// Raw interfaces
	ID3D12CommandQueue*               rawCommandQueue;
	ID3D12GraphicsCommandList4*       rawCommandList;
	class D3DRenderCommandAllocator*  d3dCommandAllocator;
	D3DSwapChain*                     d3dSwapChain;

	WRL::ComPtr<ID3D12DescriptorHeap> heapDSV;
	WRL::ComPtr<ID3D12DescriptorHeap> heapSRV;
	uint32                            heapSRVOffset = 0;
	// #todo-renderdevice: Do I need this? and as many as swap chain length?
	//WRL::ComPtr<ID3D12DescriptorHeap> heapCBV_SRV_UAV[D3DSwapChain::SWAP_CHAIN_BUFFER_COUNT];

	WRL::ComPtr<ID3D12Resource>       rawDepthStencilBuffer;

	uint32                            screenWidth;
	uint32                            screenHeight;

};
