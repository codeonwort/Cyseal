#pragma once

#include "render/gpu_resource.h"
#include "d3d_util.h"

class RenderDevice;
class D3DDevice;

class D3DVertexBuffer : public VertexBuffer
{
public:
	virtual void initialize(uint32 sizeInBytes) override;

	virtual void initializeWithinPool(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override;

	virtual void updateData(RenderCommandList* commandList, void* data, uint32 strideInBytes) override;

	virtual uint32 getVertexCount() const override { return vertexCount; };

	void setDebugName(const wchar_t* inDebugName);

	inline D3D12_VERTEX_BUFFER_VIEW getView() const { return view; }

private:
	// Own buffer or reference to the global buffer.
	WRL::ComPtr<ID3D12Resource> defaultBuffer;

	// #todo: destroy after the command list is executed and realized.
	WRL::ComPtr<ID3D12Resource> uploadBuffer;

	uint64 offsetInDefaultBuffer = 0;
	D3D12_VERTEX_BUFFER_VIEW view;

	uint32 vertexCount = 0;
};

class D3DIndexBuffer : public IndexBuffer
{
public:
	virtual void initialize(uint32 sizeInBytes) override;

	virtual void initializeWithinPool(IndexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override;

	virtual void updateData(RenderCommandList* commandList, void* data, EPixelFormat format) override;

	virtual uint32 getIndexCount() const override { return indexCount; }
	virtual EPixelFormat getIndexFormat() const override { return indexFormat; }

	void setDebugName(const wchar_t* inDebugName);

	inline D3D12_INDEX_BUFFER_VIEW getView() const { return view; }

private:
	WRL::ComPtr<ID3D12Resource> defaultBuffer;

	// #todo: destroy after the command list is executed and realized.
	WRL::ComPtr<ID3D12Resource> uploadBuffer;

	D3D12_INDEX_BUFFER_VIEW view;
	uint64 offsetInDefaultBuffer = 0;

	uint32 indexCount = 0;
	EPixelFormat indexFormat = EPixelFormat::R32_UINT;
};
