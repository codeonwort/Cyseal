#include "d3d_pipeline_state.h"
#include "d3d_buffer.h"
#include "d3d_into.h"
#include "rhi/render_command.h"
#include "rhi/buffer.h"

D3DIndirectCommandGenerator::~D3DIndirectCommandGenerator()
{
	if (memblock != nullptr)
	{
		::free(memblock);
		memblock = nullptr;
	}
}

void D3DIndirectCommandGenerator::initialize(
	const CommandSignatureDesc& inSigDesc,
	uint32 inMaxCommandCount)
{
	byteStride = into_d3d::calcCommandSignatureByteStride(inSigDesc, paddingBytes);
	maxCommandCount = inMaxCommandCount;

	memblock = reinterpret_cast<uint8*>(::malloc(byteStride * inMaxCommandCount));
}

void D3DIndirectCommandGenerator::resizeMaxCommandCount(uint32 newMaxCount)
{
	CHECK(byteStride != 0 && currentWritePtr == nullptr);

	maxCommandCount = newMaxCount;
	if (memblock != nullptr)
	{
		::free(memblock);
	}
	memblock = reinterpret_cast<uint8*>(::malloc(byteStride * maxCommandCount));
}

void D3DIndirectCommandGenerator::beginCommand(uint32 commandIx)
{
	CHECK(currentWritePtr == nullptr && commandIx < maxCommandCount);
	currentWritePtr = memblock + byteStride * commandIx;
}

void D3DIndirectCommandGenerator::writeConstant32(uint32 constant)
{
	CHECK(currentWritePtr != nullptr);
	::memcpy_s(currentWritePtr, sizeof(uint32), &constant, sizeof(uint32));
	currentWritePtr += sizeof(uint32);
}

void D3DIndirectCommandGenerator::writeVertexBufferView(VertexBuffer* vbuffer)
{
	CHECK(currentWritePtr != nullptr);
	D3D12_VERTEX_BUFFER_VIEW view = static_cast<D3DVertexBuffer*>(vbuffer)->getVertexBufferView();
	::memcpy_s(currentWritePtr, sizeof(D3D12_VERTEX_BUFFER_VIEW), &view, sizeof(D3D12_VERTEX_BUFFER_VIEW));
	currentWritePtr += sizeof(D3D12_VERTEX_BUFFER_VIEW);
}

void D3DIndirectCommandGenerator::writeIndexBufferView(IndexBuffer* ibuffer)
{
	CHECK(currentWritePtr != nullptr);
	D3D12_INDEX_BUFFER_VIEW view = static_cast<D3DIndexBuffer*>(ibuffer)->getIndexBufferView();
	::memcpy_s(currentWritePtr, sizeof(D3D12_INDEX_BUFFER_VIEW), &view, sizeof(D3D12_INDEX_BUFFER_VIEW));
	currentWritePtr += sizeof(D3D12_INDEX_BUFFER_VIEW);
}

void D3DIndirectCommandGenerator::writeDrawArguments(
	uint32 vertexCountPerInstance,
	uint32 instanceCount,
	uint32 startVertexLocation,
	uint32 startInstanceLocation)
{
	CHECK(currentWritePtr != nullptr);
	D3D12_DRAW_ARGUMENTS args{
		.VertexCountPerInstance = vertexCountPerInstance,
		.InstanceCount = instanceCount,
		.StartVertexLocation = startVertexLocation,
		.StartInstanceLocation = startInstanceLocation
	};
	::memcpy_s(currentWritePtr, sizeof(D3D12_DRAW_ARGUMENTS), &args, sizeof(D3D12_DRAW_ARGUMENTS));
	currentWritePtr += sizeof(D3D12_DRAW_ARGUMENTS);
}

void D3DIndirectCommandGenerator::writeDrawIndexedArguments(
	uint32 indexCountPerInstance,
	uint32 instanceCount,
	uint32 startIndexLocation,
	int32 baseVertexLocation,
	uint32 startInstanceLocation)
{
	CHECK(currentWritePtr != nullptr);
	D3D12_DRAW_INDEXED_ARGUMENTS args{
		.IndexCountPerInstance = indexCountPerInstance,
		.InstanceCount = instanceCount,
		.StartIndexLocation = startIndexLocation,
		.BaseVertexLocation = baseVertexLocation,
		.StartInstanceLocation = startInstanceLocation,
	};
	::memcpy_s(currentWritePtr, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), &args, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));
	currentWritePtr += sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
}

void D3DIndirectCommandGenerator::writeDispatchArguments(
	uint32 threadGroupCountX,
	uint32 threadGroupCountY,
	uint32 threadGroupCountZ)
{
	CHECK(currentWritePtr != nullptr);
	D3D12_DISPATCH_ARGUMENTS args{
		.ThreadGroupCountX = threadGroupCountX,
		.ThreadGroupCountY = threadGroupCountY,
		.ThreadGroupCountZ = threadGroupCountZ
	};
	::memcpy_s(currentWritePtr, sizeof(D3D12_DISPATCH_ARGUMENTS), &args, sizeof(D3D12_DISPATCH_ARGUMENTS));
	currentWritePtr += sizeof(D3D12_DISPATCH_ARGUMENTS);
}

void D3DIndirectCommandGenerator::writeConstantBufferView(ConstantBufferView* view)
{
	D3D12_GPU_VIRTUAL_ADDRESS addr = static_cast<D3DConstantBufferView*>(view)->getGPUVirtualAddress();
	::memcpy_s(currentWritePtr, sizeof(D3D12_GPU_VIRTUAL_ADDRESS), &addr, sizeof(D3D12_GPU_VIRTUAL_ADDRESS));
	currentWritePtr += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
}

void D3DIndirectCommandGenerator::writeShaderResourceView(ShaderResourceView* view)
{
	D3D12_GPU_VIRTUAL_ADDRESS addr = static_cast<D3DShaderResourceView*>(view)->getGPUVirtualAddress();
	::memcpy_s(currentWritePtr, sizeof(D3D12_GPU_VIRTUAL_ADDRESS), &addr, sizeof(D3D12_GPU_VIRTUAL_ADDRESS));
	currentWritePtr += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
}

void D3DIndirectCommandGenerator::writeUnorderedAccessView(UnorderedAccessView* view)
{
	D3D12_GPU_VIRTUAL_ADDRESS addr = static_cast<D3DUnorderedAccessView*>(view)->getGPUVirtualAddress();
	::memcpy_s(currentWritePtr, sizeof(D3D12_GPU_VIRTUAL_ADDRESS), &addr, sizeof(D3D12_GPU_VIRTUAL_ADDRESS));
	currentWritePtr += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
}

void D3DIndirectCommandGenerator::writeDispatchMeshArguments(
	uint32 threadGroupCountX,
	uint32 threadGroupCountY,
	uint32 threadGroupCountZ)
{
	CHECK(currentWritePtr != nullptr);
	D3D12_DISPATCH_MESH_ARGUMENTS args{
		.ThreadGroupCountX = threadGroupCountX,
		.ThreadGroupCountY = threadGroupCountY,
		.ThreadGroupCountZ = threadGroupCountZ
	};
	::memcpy_s(currentWritePtr, sizeof(D3D12_DISPATCH_MESH_ARGUMENTS), &args, sizeof(D3D12_DISPATCH_MESH_ARGUMENTS));
	currentWritePtr += sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
}

void D3DIndirectCommandGenerator::endCommand()
{
	CHECK(currentWritePtr != nullptr);
	::memset(currentWritePtr, 0, paddingBytes);
	currentWritePtr = nullptr;
}

void D3DIndirectCommandGenerator::copyToBuffer(RenderCommandList* commandList, uint32 numCommands, Buffer* destBuffer, uint64 destOffset)
{
	CHECK(numCommands <= maxCommandCount);
	destBuffer->singleWriteToGPU(commandList, memblock, byteStride * numCommands, destOffset);
}
