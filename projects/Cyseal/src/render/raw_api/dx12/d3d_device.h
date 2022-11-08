#pragma once

#include "render/render_device.h"
#include "d3d_util.h"
#include "d3d_swap_chain.h"

class D3DDevice : public RenderDevice
{

public:
	D3DDevice();
	~D3DDevice();

	virtual void initialize(const RenderDeviceCreateParams& createParams) override;

	virtual void recreateSwapChain(void* nativeWindowHandle, uint32 width, uint32 height) override;

	virtual void flushCommandQueue() override;

	virtual bool supportsRayTracing() override;

	virtual VertexBuffer* createVertexBuffer(uint32 sizeInBytes, const wchar_t* inDebugName) override;
	virtual VertexBuffer* createVertexBuffer(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override;

	virtual IndexBuffer* createIndexBuffer(uint32 sizeInBytes, const wchar_t* inDebugName) override;
	virtual IndexBuffer* createIndexBuffer(IndexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override;

	virtual Texture* createTexture(const TextureCreateParams& createParams) override;

	virtual ShaderStage* createShader(EShaderStage shaderStage, const char* debugName) override;

	virtual RootSignature* createRootSignature(const RootSignatureDesc& desc) override;
	virtual PipelineState* createGraphicsPipelineState(const GraphicsPipelineDesc& desc) override;
	virtual PipelineState* createComputePipelineState(const ComputePipelineDesc& desc) override;

	virtual DescriptorHeap* createDescriptorHeap(const DescriptorHeapDesc& desc) override;

	virtual ConstantBuffer* createConstantBuffer(uint32 totalBytes) override;
	virtual StructuredBuffer* createStructuredBuffer(uint32 numElements, uint32 stride) override;

	virtual void copyDescriptors(
		uint32 numDescriptors,
		DescriptorHeap* destHeap,
		uint32 destHeapDescriptorStartOffset,
		DescriptorHeap* srcHeap,
		uint32 srcHeapDescriptorStartOffset) override;

	uint32 getDescriptorSizeCbvSrvUav() { return descSizeCBV_SRV_UAV; }

	inline IDXGIFactory4* getDXGIFactory() const { return dxgiFactory.Get(); }
	inline ID3D12Device5* getRawDevice() const { return device.Get(); }
	inline ID3D12CommandQueue* getRawCommandQueue() const { return rawCommandQueue; }

	// #todo-renderdevice: Needs abstraction layer and release mechanism
	void allocateSRVHandle(D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32& outDescriptorIndex);
	void allocateRTVHandle(D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32& outDescriptorIndex);
	void allocateDSVHandle(D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32& outDescriptorIndex);

private:
	void getHardwareAdapter(IDXGIFactory2* factory, IDXGIAdapter1** outAdapter);

// #todo-renderdevice: Move non-renderdevice members into other places
private:
	WRL::ComPtr<IDXGIFactory4>        dxgiFactory;

	WRL::ComPtr<ID3D12Device5>        device;

	WRL::ComPtr<ID3D12Fence>          fence;
	UINT                              currentFence;

	UINT                              descSizeRTV;
	UINT                              descSizeDSV;
	UINT                              descSizeCBV_SRV_UAV;
	UINT                              descSizeSampler;
	UINT                              quality4xMSAA;

	bool                              rayTracingEnabled;

	// Raw interfaces
	ID3D12CommandQueue*               rawCommandQueue;
	ID3D12GraphicsCommandList4*       rawCommandList;
	D3DSwapChain*                     d3dSwapChain;
};
