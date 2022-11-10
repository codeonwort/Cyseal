#pragma once

#include "render/render_device.h"
#include "d3d_util.h"
#include "d3d_swap_chain.h"

// #todo-dx12: Is there any way to automatically select latest ID3D12Device?
// Currently latest version is ID3D12Device9, but I don't need newer APIs yet.
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nn-d3d12-id3d12device5
#define ID3D12DeviceLatest ID3D12Device5

class D3DDevice : public RenderDevice
{

public:
	D3DDevice();
	~D3DDevice();

	virtual void initialize(const RenderDeviceCreateParams& createParams) override;

	virtual void recreateSwapChain(void* nativeWindowHandle, uint32 width, uint32 height) override;

	virtual void flushCommandQueue() override;

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
	virtual StructuredBuffer* createStructuredBuffer(
		uint32 numElements,
		uint32 stride,
		EBufferAccessFlags accessFlags) override;

	virtual void copyDescriptors(
		uint32 numDescriptors,
		DescriptorHeap* destHeap,
		uint32 destHeapDescriptorStartOffset,
		DescriptorHeap* srcHeap,
		uint32 srcHeapDescriptorStartOffset) override;

	uint32 getDescriptorSizeCbvSrvUav() { return descSizeCBV_SRV_UAV; }

	inline IDXGIFactory4* getDXGIFactory() const { return dxgiFactory.Get(); }
	inline ID3D12DeviceLatest* getRawDevice() const { return device.Get(); }
	inline ID3D12CommandQueue* getRawCommandQueue() const { return rawCommandQueue; }

	// #todo-renderdevice: Needs abstraction layer and release mechanism
	// #todo-renderdevice: Actually they are abusing desc heaps of gTextureManager.
	void allocateSRVHandle(DescriptorHeap*& outSourceHeap, D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32& outDescriptorIndex);
	void allocateRTVHandle(DescriptorHeap*& outSourceHeap, D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32& outDescriptorIndex);
	void allocateDSVHandle(DescriptorHeap*& outSourceHeap, D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32& outDescriptorIndex);
	void allocateUAVHandle(DescriptorHeap*& outSourceHeap, D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32& outDescriptorIndex);

private:
	void getHardwareAdapter(IDXGIFactory2* factory, IDXGIAdapter1** outAdapter);

// #todo-renderdevice: Move non-renderdevice members into other places
private:
	WRL::ComPtr<ID3D12DeviceLatest>   device;

	WRL::ComPtr<IDXGIFactory4>        dxgiFactory;

	WRL::ComPtr<ID3D12Fence>          fence;
	UINT                              currentFence;

	UINT                              descSizeRTV;
	UINT                              descSizeDSV;
	UINT                              descSizeCBV_SRV_UAV;
	UINT                              descSizeSampler;
	UINT                              quality4xMSAA;

	// Raw interfaces
	ID3D12CommandQueue*               rawCommandQueue;
	ID3D12GraphicsCommandList4*       rawCommandList;
	D3DSwapChain*                     d3dSwapChain;
};
