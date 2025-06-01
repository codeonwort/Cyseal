#pragma once

#include "rhi/render_device.h"
#include "d3d_util.h"
#include "d3d_swap_chain.h"

// #todo-dx12: Is there any way to automatically select latest ID3D12Device?
// Currently latest version is ID3D12Device9, but I don't need newer APIs yet.
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nn-d3d12-id3d12device5
#define ID3D12DeviceLatest ID3D12Device5
#define IDXGIFactoryLatest IDXGIFactory4

class D3DDevice : public RenderDevice
{

public:
	D3DDevice();

	virtual void onInitialize(const RenderDeviceCreateParams& createParams) override;

	virtual void onDestroy() override;

	virtual void recreateSwapChain(void* nativeWindowHandle, uint32 width, uint32 height) override;

	virtual void flushCommandQueue() override;

	// ------------------------------------------------------------------------
	// DearImgui

	virtual void initializeDearImgui() override;
	virtual void beginDearImguiNewFrame() override;
	virtual void renderDearImgui(RenderCommandList* commandList) override;
	virtual void shutdownDearImgui() override;

	// ------------------------------------------------------------------------
	// Create

	virtual RenderCommandList* createRenderCommandList() override;
	virtual RenderCommandAllocator* createRenderCommandAllocator() override;

	virtual VertexBuffer* createVertexBuffer(uint32 sizeInBytes, EBufferAccessFlags usageFlags, const wchar_t* inDebugName) override;
	virtual VertexBuffer* createVertexBuffer(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override;

	virtual IndexBuffer* createIndexBuffer(uint32 sizeInBytes, EPixelFormat format, EBufferAccessFlags usageFlags, const wchar_t* inDebugName) override;
	virtual IndexBuffer* createIndexBuffer(IndexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes, EPixelFormat format) override;

	virtual Buffer* createBuffer(const BufferCreateParams& createParams) override;
	virtual Texture* createTexture(const TextureCreateParams& createParams) override;

	virtual ShaderStage* createShader(EShaderStage shaderStage, const char* debugName) override;

	virtual GraphicsPipelineState* createGraphicsPipelineState(const GraphicsPipelineDesc& desc) override;

	virtual ComputePipelineState* createComputePipelineState(const ComputePipelineDesc& desc) override;

	virtual RaytracingPipelineStateObject* createRaytracingPipelineStateObject(const RaytracingPipelineStateObjectDesc& desc) override;

	virtual RaytracingShaderTable* createRaytracingShaderTable(
		RaytracingPipelineStateObject* RTPSO,
		uint32 numShaderRecords,
		uint32 rootArgumentSize,
		const wchar_t* debugName) override;

	virtual DescriptorHeap* createDescriptorHeap(const DescriptorHeapDesc& desc) override;

	virtual ConstantBufferView* createCBV(Buffer* buffer, DescriptorHeap* descriptorHeap, uint32 sizeInBytes, uint32 offsetInBytes) override;
	virtual ShaderResourceView* createSRV(GPUResource* gpuResource, DescriptorHeap* descriptorHeap, const ShaderResourceViewDesc& createParams) override;
	virtual UnorderedAccessView* createUAV(GPUResource* gpuResource, DescriptorHeap* descriptorHeap, const UnorderedAccessViewDesc& createParams) override;
	virtual RenderTargetView* createRTV(GPUResource* gpuResource, DescriptorHeap* descriptorHeap, const RenderTargetViewDesc& createParams) override;
	virtual DepthStencilView* createDSV(GPUResource* gpuResource, DescriptorHeap* descriptorHeap, const DepthStencilViewDesc& createParams) override;

	virtual ShaderResourceView* createSRV(GPUResource* gpuResource, const ShaderResourceViewDesc& createParams) override;
	virtual UnorderedAccessView* createUAV(GPUResource* gpuResource, const UnorderedAccessViewDesc& createParams) override;
	virtual RenderTargetView* createRTV(GPUResource* gpuResource, const RenderTargetViewDesc& createParams) override;
	virtual DepthStencilView* createDSV(GPUResource* gpuResource, const DepthStencilViewDesc& createParams) override;

	virtual CommandSignature* createCommandSignature(const CommandSignatureDesc& inDesc, GraphicsPipelineState* inPipelineState) override;
	virtual IndirectCommandGenerator* createIndirectCommandGenerator(const CommandSignatureDesc& inDesc, uint32 maxCommandCount) override;

	// ------------------------------------------------------------------------
	// Copy

	virtual void copyDescriptors(
		uint32 numDescriptors,
		DescriptorHeap* destHeap,
		uint32 destHeapDescriptorStartOffset,
		DescriptorHeap* srcHeap,
		uint32 srcHeapDescriptorStartOffset) override;

	// ------------------------------------------------------------------------
	// Getters
	
	virtual uint32 getConstantBufferDataAlignment() const { return D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT; }

	// ------------------------------------------------------------------------
	// Utils

	inline uint32 getDescriptorSizeCbvSrvUav()         const { return descSizeCBV_SRV_UAV;     }

	inline IDXGIFactoryLatest* getDXGIFactory()        const { return dxgiFactory.Get();       }
	inline ID3D12DeviceLatest* getRawDevice()          const { return device.Get();            }
	inline ID3D12CommandQueue* getRawCommandQueue()    const { return rawCommandQueue;         }

	inline D3D_SHADER_MODEL    getHighestShaderModel() const { return highestShaderModel;      }
	inline IDxcUtils*          getDxcUtils()           const { return dxcUtils.Get();          }
	inline IDxcCompiler3*      getDxcCompiler()        const { return dxcCompiler.Get();       }
	inline IDxcIncludeHandler* getDxcIncludeHandler()  const { return dxcIncludeHandler.Get(); }

private:
	void getHardwareAdapter(IDXGIFactory2* factory, IDXGIAdapter1** outAdapter);

// #todo-renderdevice: Move non-renderdevice members into other places
private:
	WRL::ComPtr<ID3D12DeviceLatest>   device;

	WRL::ComPtr<IDXGIFactoryLatest>   dxgiFactory;

	WRL::ComPtr<ID3D12Fence>          fence;
	UINT                              currentFence = 0;

	UINT                              descSizeRTV = 0;
	UINT                              descSizeDSV = 0;
	UINT                              descSizeCBV_SRV_UAV = 0;
	UINT                              descSizeSampler = 0;
	UINT                              quality4xMSAA = 1;

	// Raw interfaces
	ID3D12CommandQueue*               rawCommandQueue = nullptr;
	D3DSwapChain*                     d3dSwapChain = nullptr;

	// Shader Model
	D3D_SHADER_MODEL                  highestShaderModel = D3D_SHADER_MODEL_6_0;

	// DXC
	WRL::ComPtr<IDxcUtils>            dxcUtils;
	WRL::ComPtr<IDxcCompiler3>        dxcCompiler;
	WRL::ComPtr<IDxcIncludeHandler>   dxcIncludeHandler;
};
