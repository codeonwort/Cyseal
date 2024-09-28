#pragma once

#include "rhi/buffer.h"
#include "d3d_util.h"
#include "d3d_resource_view.h"

class RenderDevice;
class D3DDevice;
class D3DShaderResourceView;

class D3DBuffer : public Buffer
{
public:
	virtual ~D3DBuffer();
	virtual void initialize(const BufferCreateParams& inCreateParams) override;
	virtual void writeToGPU(RenderCommandList* commandList, uint32 numUploads, Buffer::UploadDesc* uploadDescs) override;

	virtual void* getRawResource() const { return defaultBuffer.Get(); }
	virtual void setDebugName(const wchar_t* inDebugName) override;

private:
	WRL::ComPtr<ID3D12Resource> defaultBuffer;

	// #todo-renderdevice: Always holding an upload buffer
	// as the same size as the default buffer is inefficient.
	WRL::ComPtr<ID3D12Resource> uploadBuffer;
	uint8* uploadMapPtr = nullptr;
};

class D3DVertexBuffer : public VertexBuffer
{
public:
	virtual void initialize(uint32 sizeInBytes, EBufferAccessFlags usageFlags) override;

	virtual void initializeWithinPool(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override;

	virtual void updateData(RenderCommandList* commandList, void* data, uint32 strideInBytes) override;

	virtual uint32 getVertexCount() const override { return vertexCount; };

	virtual uint64 getBufferOffsetInBytes() const override { return offsetInDefaultBuffer; }

	void setDebugName(const wchar_t* inDebugName);

	inline D3D12_VERTEX_BUFFER_VIEW getVertexBufferView() const { return view; }

	virtual void* getRawResource() const override { return defaultBuffer.Get(); }

private:
	// Own buffer or reference to the global buffer.
	WRL::ComPtr<ID3D12Resource> defaultBuffer;

	// #todo-dx12: destroy after the command list is executed and realized.
	WRL::ComPtr<ID3D12Resource> uploadBuffer;

	uint64 offsetInDefaultBuffer = 0;
	D3D12_VERTEX_BUFFER_VIEW view;

	uint32 vertexCount = 0;
};

class D3DIndexBuffer : public IndexBuffer
{
public:
	virtual void initialize(uint32 sizeInBytes, EPixelFormat format, EBufferAccessFlags usageFlags) override;

	virtual void initializeWithinPool(IndexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override;

	virtual void updateData(RenderCommandList* commandList, void* data, EPixelFormat format) override;

	virtual uint32 getIndexCount() const override { return indexCount; }
	virtual EPixelFormat getIndexFormat() const override { return indexFormat; }

	void setDebugName(const wchar_t* inDebugName);

	inline D3D12_INDEX_BUFFER_VIEW getIndexBufferView() const { return view; }
	D3D12_GPU_VIRTUAL_ADDRESS getGPUVirtualAddress() const;

	// offsetInPool
	virtual uint64 getBufferOffsetInBytes() const { return offsetInDefaultBuffer; }

	virtual void* getRawResource() const override { return defaultBuffer.Get(); }

private:
	WRL::ComPtr<ID3D12Resource> defaultBuffer;

	// #todo-dx12: destroy after the command list is executed and realized.
	WRL::ComPtr<ID3D12Resource> uploadBuffer;

	D3D12_INDEX_BUFFER_VIEW view;
	uint64 offsetInDefaultBuffer = 0;

	uint32 indexCount = 0;
	EPixelFormat indexFormat = EPixelFormat::R32_UINT;
};
