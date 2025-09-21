#include "d3d_render_command.h"
#include "d3d_resource.h"
#include "d3d_resource_view.h"
#include "d3d_pipeline_state.h"
#include "d3d_buffer.h"
#include "d3d_shader.h"
#include "d3d_into.h"
#include "core/assertion.h"

#include <vector>

#ifndef _DEBUG
	#define USE_PIX
#endif
#include <pix3.h>

DEFINE_LOG_CATEGORY_STATIC(LogD3DCommandList);

static void reportUndeclaredShaderParameter(const char* name)
{
	// #todo-log: How to stop error spam? Track same errors and report only once?
	//CYLOG(LogD3DCommandList, Error, L"Undeclared parameter: %S", name);
}

//////////////////////////////////////////////////////////////////////////
// D3DRenderCommandQueue

void D3DRenderCommandQueue::initialize(RenderDevice* renderDevice)
{
	device = static_cast<D3DDevice*>(renderDevice);

	D3D12_COMMAND_QUEUE_DESC desc{};
	desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	HR(device->getRawDevice()->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)));
}

void D3DRenderCommandQueue::executeCommandList(class RenderCommandList* commandList)
{
	auto rawList = static_cast<D3DRenderCommandList*>(commandList);
	ID3D12CommandList* const lists[] = { rawList->getRaw() };
	queue->ExecuteCommandLists(1, lists);
}

//////////////////////////////////////////////////////////////////////////
// D3DRenderCommandAllocator

void D3DRenderCommandAllocator::initialize(RenderDevice* renderDevice)
{
	device = static_cast<D3DDevice*>(renderDevice);

	HR( device->getRawDevice()->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(allocator.GetAddressOf()))
	);
}

void D3DRenderCommandAllocator::onReset()
{
	HR( allocator->Reset() );
}

//////////////////////////////////////////////////////////////////////////
// D3DRenderCommandList

void D3DRenderCommandList::initialize(RenderDevice* renderDevice)
{
	device = static_cast<D3DDevice*>(renderDevice);

	auto rawDevice = device->getRawDevice();
	// #todo-dx12: Use the first allocator to create a command list
	// but this list will be reset with a different allocator every frame.
	// Is it safe? (No error in my PC but does the spec guarantees it?)
	auto tempAllocator = static_cast<D3DRenderCommandAllocator*>(device->getCommandAllocator(0))->getRaw();

	HR( rawDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		tempAllocator,
		nullptr, // Initial state
		IID_PPV_ARGS(commandList.GetAddressOf()))
	);
	HR( commandList->Close() );
}

void D3DRenderCommandList::reset(RenderCommandAllocator* allocator)
{
	ID3D12CommandAllocator* d3dAllocator = static_cast<D3DRenderCommandAllocator*>(allocator)->getRaw();
	HR( commandList->Reset(d3dAllocator, nullptr) );
}

void D3DRenderCommandList::close()
{
	HR( commandList->Close() );
}

void D3DRenderCommandList::iaSetPrimitiveTopology(EPrimitiveTopology topology)
{
	D3D12_PRIMITIVE_TOPOLOGY d3dTopology = into_d3d::primitiveTopology(topology);
	commandList->IASetPrimitiveTopology(d3dTopology);
}

void D3DRenderCommandList::iaSetVertexBuffers(
	int32 startSlot,
	uint32 numViews,
	VertexBuffer* const* vertexBuffers)
{
	std::vector<D3D12_VERTEX_BUFFER_VIEW> views;
	views.resize(numViews);

	for (uint32 i = 0; i < numViews; ++i)
	{
		auto buffer = static_cast<D3DVertexBuffer*>(vertexBuffers[i]);
		views[i] = buffer->getVertexBufferView();
	}

	commandList->IASetVertexBuffers(startSlot, numViews, &views[0]);
}

void D3DRenderCommandList::iaSetIndexBuffer(IndexBuffer* indexBuffer)
{
	auto buffer = static_cast<D3DIndexBuffer*>(indexBuffer);
	auto viewDesc = buffer->getIndexBufferView();
	commandList->IASetIndexBuffer(&viewDesc);
}

void D3DRenderCommandList::rsSetViewport(const Viewport& viewport)
{
	D3D12_VIEWPORT rawViewport{
		viewport.topLeftX,
		viewport.topLeftY,
		viewport.width,
		viewport.height,
		viewport.minDepth,
		viewport.maxDepth
	};
	commandList->RSSetViewports(1, &rawViewport);
}

void D3DRenderCommandList::rsSetScissorRect(const ScissorRect& scissorRect)
{
	D3D12_RECT rect{
		static_cast<LONG>(scissorRect.left),
		static_cast<LONG>(scissorRect.top),
		static_cast<LONG>(scissorRect.right),
		static_cast<LONG>(scissorRect.bottom)
	};
	commandList->RSSetScissorRects(1, &rect);
}

void D3DRenderCommandList::resourceBarriers(
	uint32 numBufferMemoryBarriers, const BufferMemoryBarrier* bufferMemoryBarriers,
	uint32 numTextureMemoryBarriers, const TextureMemoryBarrier* textureMemoryBarriers,
	uint32 numUAVBarriers, GPUResource* const* uavBarrierResources)
{
	// #todo-barrier: DX12 enhanced barriers
	// https://learn.microsoft.com/en-us/windows-hardware/drivers/display/enhanced-barriers
	// https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html#excessive-sync-latency

	uint32 totalBarriers = numBufferMemoryBarriers + numTextureMemoryBarriers + numUAVBarriers;
	std::vector<D3D12_RESOURCE_BARRIER> rawBarriers(totalBarriers);
	for (uint32 i = 0; i < numBufferMemoryBarriers; ++i)
	{
		rawBarriers[i] = into_d3d::resourceBarrier(bufferMemoryBarriers[i]);
	}
	for (uint32 i = 0; i < numTextureMemoryBarriers; ++i)
	{
		rawBarriers[i + numBufferMemoryBarriers] = into_d3d::resourceBarrier(textureMemoryBarriers[i]);
	}
	for (uint32 i = 0; i < numUAVBarriers; ++i)
	{
		D3D12_RESOURCE_BARRIER uavBarrier{
			.Type       = D3D12_RESOURCE_BARRIER_TYPE_UAV,
			.Flags      = D3D12_RESOURCE_BARRIER_FLAG_NONE,
			.UAV        = { .pResource = into_d3d::id3d12Resource(uavBarrierResources[i]) }
		};
		rawBarriers[i + numBufferMemoryBarriers + numTextureMemoryBarriers] = uavBarrier;
	}

	commandList->ResourceBarrier(totalBarriers, rawBarriers.data());

	// Store last state.
	for (uint32 i = 0; i < numTextureMemoryBarriers; ++i)
	{
		auto& desc = textureMemoryBarriers[i];
		static_cast<D3DTexture*>(desc.texture)->saveLastMemoryLayout(desc.stateAfter);
	}
}

void D3DRenderCommandList::barrier(
	uint32 numBufferBarriers, const BufferBarrier* bufferBarriers,
	uint32 numTextureBarriers, const TextureBarrier* textureBarriers,
	uint32 numGlobalBarriers, const GlobalBarrier* globalBarriers)
{
	std::vector<D3D12_BARRIER_GROUP> groups;
	std::vector<D3D12_BUFFER_BARRIER> d3dBufferBarriers(numBufferBarriers);
	std::vector<D3D12_TEXTURE_BARRIER> d3dTextureBarriers(numTextureBarriers);
	std::vector<D3D12_GLOBAL_BARRIER> d3dGlobalBarriers(numGlobalBarriers);
	if (numBufferBarriers > 0)
	{
		for (size_t i = 0; i < numBufferBarriers; ++i)
		{
			d3dBufferBarriers[i] = into_d3d::bufferBarrier(bufferBarriers[i]);
		}
		D3D12_BARRIER_GROUP group;
		group.Type = D3D12_BARRIER_TYPE_BUFFER;
		group.NumBarriers = numBufferBarriers;
		group.pBufferBarriers = d3dBufferBarriers.data();
	}
	if (numTextureBarriers > 0)
	{
		for (size_t i = 0; i < numTextureBarriers; ++i)
		{
			d3dTextureBarriers[i] = into_d3d::textureBarrier(textureBarriers[i]);
		}
		D3D12_BARRIER_GROUP group;
		group.Type = D3D12_BARRIER_TYPE_TEXTURE;
		group.NumBarriers = numTextureBarriers;
		group.pTextureBarriers = d3dTextureBarriers.data();
	}
	if (numGlobalBarriers > 0)
	{
		for (size_t i = 0; i < numGlobalBarriers; ++i)
		{
			d3dGlobalBarriers[i] = into_d3d::globalBarrier(globalBarriers[i]);
		}
		D3D12_BARRIER_GROUP group;
		group.Type = D3D12_BARRIER_TYPE_GLOBAL;
		group.NumBarriers = numGlobalBarriers;
		group.pGlobalBarriers = d3dGlobalBarriers.data();
	}

	commandList->Barrier((uint32)groups.size(), groups.data());
}

void D3DRenderCommandList::clearRenderTargetView(RenderTargetView* RTV, const float* rgba)
{
	auto d3dRTV = static_cast<D3DRenderTargetView*>(RTV);
	auto rawRTV = d3dRTV->getCPUHandle();

	commandList->ClearRenderTargetView(rawRTV, rgba, 0, nullptr);
}

void D3DRenderCommandList::clearDepthStencilView(DepthStencilView* DSV, EDepthClearFlags clearFlags, float depth, uint8_t stencil)
{
	auto d3dDSV = static_cast<D3DDepthStencilView*>(DSV);
	auto rawDSV = d3dDSV->getCPUHandle();

	commandList->ClearDepthStencilView(
		rawDSV, (D3D12_CLEAR_FLAGS)clearFlags,
		depth, stencil,
		0, nullptr);
}

void D3DRenderCommandList::copyTexture2D(Texture* src, Texture* dst)
{
	D3D12_TEXTURE_COPY_LOCATION pDst{
		.pResource        = into_d3d::id3d12Resource(dst),
		.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
		.SubresourceIndex = 0,
	};
	D3D12_TEXTURE_COPY_LOCATION pSrc{
		.pResource        = into_d3d::id3d12Resource(src),
		.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
		.SubresourceIndex = 0,
	};
	D3D12_BOX srcRegion{
		.left   = 0,
		.top    = 0,
		.front  = 0,
		.right  = src->getCreateParams().width,
		.bottom = src->getCreateParams().height,
		.back   = 1,
	};
	commandList->CopyTextureRegion(&pDst, 0, 0, 0, &pSrc, &srcRegion);
}

void D3DRenderCommandList::omSetRenderTarget(RenderTargetView* RTV, DepthStencilView* DSV)
{
	CHECK(RTV != nullptr || DSV != nullptr); // At least one of them should exist... right?

	uint32 numRTV = (RTV != nullptr) ? 1 : 0;
	D3D12_CPU_DESCRIPTOR_HANDLE rawRTV = { NULL };
	D3D12_CPU_DESCRIPTOR_HANDLE rawDSV = { NULL };
	if (RTV != nullptr)
	{
		rawRTV = static_cast<D3DRenderTargetView*>(RTV)->getCPUHandle();
	}
	if (DSV != nullptr)
	{
		rawDSV = static_cast<D3DDepthStencilView*>(DSV)->getCPUHandle();
	}

	constexpr bool RTsSingleHandleToDescriptorRange = true; // Whatever
	commandList->OMSetRenderTargets(
		numRTV, (RTV != nullptr ? &rawRTV : NULL),
		RTsSingleHandleToDescriptorRange,
		(DSV != nullptr ? &rawDSV : NULL));
}

void D3DRenderCommandList::omSetRenderTargets(
	uint32 numRTVs, RenderTargetView* const* RTVs, DepthStencilView* DSV)
{
	CHECK(numRTVs <= D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
	CHECK(numRTVs > 0 || DSV != nullptr);

	D3D12_CPU_DESCRIPTOR_HANDLE rawRTVs[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT] = { NULL, };
	D3D12_CPU_DESCRIPTOR_HANDLE rawDSV = { NULL };
	for (uint32 i = 0; i < numRTVs; ++i)
	{
		rawRTVs[i] = static_cast<D3DRenderTargetView*>(RTVs[i])->getCPUHandle();
	}
	if (DSV != nullptr)
	{
		rawDSV = static_cast<D3DDepthStencilView*>(DSV)->getCPUHandle();
	}

	// Do I benefit much if they are contiguous? Anyway there can be at most 8 RTVs...
	constexpr bool RTsSingleHandleToDescriptorRange = false;
	commandList->OMSetRenderTargets(
		numRTVs, (numRTVs > 0 ? rawRTVs : NULL),
		RTsSingleHandleToDescriptorRange,
		(DSV != nullptr ? &rawDSV : nullptr));
}

void D3DRenderCommandList::setGraphicsPipelineState(GraphicsPipelineState* state)
{
	auto pipelineWrapper = static_cast<D3DGraphicsPipelineState*>(state);
	ID3D12PipelineState* d3dPipeline = pipelineWrapper->getPipelineState();
	commandList->SetPipelineState(d3dPipeline);
}

void D3DRenderCommandList::setComputePipelineState(ComputePipelineState* state)
{
	auto pipelineWrapper = static_cast<D3DComputePipelineState*>(state);
	ID3D12PipelineState* d3dPipeline = pipelineWrapper->getPipelineState();
	commandList->SetPipelineState(d3dPipeline);
}

void D3DRenderCommandList::setRaytracingPipelineState(RaytracingPipelineStateObject* rtpso)
{
	auto pipelineWrapper = static_cast<D3DRaytracingPipelineStateObject*>(rtpso);
	ID3D12StateObject* rawRTPSO = pipelineWrapper->getRaw();
	commandList->SetPipelineState1(rawRTPSO);
}

void D3DRenderCommandList::setDescriptorHeaps(uint32 count, DescriptorHeap* const* heaps)
{
	std::vector<ID3D12DescriptorHeap*> rawHeaps;
	rawHeaps.resize(count);
	for (uint32 i = 0; i < count; ++i)
	{
		rawHeaps[i] = static_cast<D3DDescriptorHeap*>(heaps[i])->getRaw();
	}
	commandList->SetDescriptorHeaps(count, rawHeaps.data());
}

// #todo-dx12: bindGraphicsShaderParameters(), bindComputeShaderParameters(), and bindRaytracingShaderParameters() are almost same, but a little hard to extract common part.
void D3DRenderCommandList::bindGraphicsShaderParameters(PipelineState* pipelineState, const ShaderParameterTable* inParameters, DescriptorHeap* descriptorHeap)
{
	D3DGraphicsPipelineState* d3dPipelineState = static_cast<D3DGraphicsPipelineState*>(pipelineState);
	ID3D12RootSignature* d3dRootSig = d3dPipelineState->getRootSignature();

	ID3D12DescriptorHeap* d3dDescriptorHeap = static_cast<D3DDescriptorHeap*>(descriptorHeap)->getRaw();
	D3D12_GPU_DESCRIPTOR_HANDLE baseHandle = d3dDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	const uint64 descriptorSize = (uint64)device->getDescriptorSizeCbvSrvUav();
	auto calcDescriptorHandle = [&baseHandle, &descriptorSize](uint32 descriptorIndex) {
		D3D12_GPU_DESCRIPTOR_HANDLE handle = baseHandle;
		handle.ptr += (uint64)descriptorIndex * descriptorSize;
		return handle;
	};

	// https://docs.microsoft.com/en-us/windows/win32/direct3d12/using-a-root-signature
	commandList->SetGraphicsRootSignature(d3dRootSig);

	ID3D12DescriptorHeap* d3dDescriptorHeaps[] = { d3dDescriptorHeap };
	commandList->SetDescriptorHeaps(_countof(d3dDescriptorHeaps), d3dDescriptorHeaps);

	auto setRootDescriptorTables = [d3dPipelineState, device = gRenderDevice, &calcDescriptorHandle, descriptorHeap]<typename T>(ID3D12GraphicsCommandList* cmdList, const std::vector<T>& parameters, uint32* inoutDescriptorIx)
	{
		for (const auto& inParam : parameters)
		{
			const D3DShaderParameter* param = d3dPipelineState->findShaderParameter(inParam.name);
			if (param == nullptr)
			{
				reportUndeclaredShaderParameter(inParam.name.c_str());
				continue;
			}
			device->copyDescriptors(inParam.count, descriptorHeap, *inoutDescriptorIx, inParam.sourceHeap, inParam.startIndex);
			cmdList->SetGraphicsRootDescriptorTable(param->rootParameterIndex, calcDescriptorHandle(*inoutDescriptorIx));
			*inoutDescriptorIx += inParam.count;
		}
	};

	// #todo-dx12: Root Descriptor vs Descriptor Table
	// For now, always use descriptor table.
	uint32 descriptorIx = 0;

	for (const auto& inParam : inParameters->_pushConstants)
	{
		const D3DShaderParameter* param = d3dPipelineState->findShaderParameter(inParam.name);
		if (param == nullptr)
		{
			reportUndeclaredShaderParameter(inParam.name.c_str());
			continue;
		}
		if (inParam.values.size() == 1)
		{
			commandList->SetGraphicsRoot32BitConstant(param->rootParameterIndex, inParam.values[0], inParam.destOffsetIn32BitValues);
		}
		else
		{
			commandList->SetGraphicsRoot32BitConstants(param->rootParameterIndex, (UINT)inParam.values.size(), inParam.values.data(), inParam.destOffsetIn32BitValues);
		}
	}
	setRootDescriptorTables(commandList.Get(), inParameters->constantBuffers, &descriptorIx);
	setRootDescriptorTables(commandList.Get(), inParameters->structuredBuffers, &descriptorIx);
	setRootDescriptorTables(commandList.Get(), inParameters->rwBuffers, &descriptorIx);
	setRootDescriptorTables(commandList.Get(), inParameters->rwStructuredBuffers, &descriptorIx);
	setRootDescriptorTables(commandList.Get(), inParameters->byteAddressBuffers, &descriptorIx);
	setRootDescriptorTables(commandList.Get(), inParameters->textures, &descriptorIx);
	setRootDescriptorTables(commandList.Get(), inParameters->rwTextures, &descriptorIx);
	CHECK(inParameters->accelerationStructures.size() == 0); // Not allowed in graphics pipeline.
}

void D3DRenderCommandList::updateGraphicsRootConstants(PipelineState* pipelineState, const ShaderParameterTable* inParameters)
{
	D3DGraphicsPipelineState* d3dPipelineState = static_cast<D3DGraphicsPipelineState*>(pipelineState);

	for (const auto& inParam : inParameters->_pushConstants)
	{
		const D3DShaderParameter* param = d3dPipelineState->findShaderParameter(inParam.name);
		if (param == nullptr)
		{
			reportUndeclaredShaderParameter(inParam.name.c_str());
			continue;
		}
		if (inParam.values.size() == 1)
		{
			commandList->SetGraphicsRoot32BitConstant(param->rootParameterIndex, inParam.values[0], inParam.destOffsetIn32BitValues);
		}
		else
		{
			commandList->SetGraphicsRoot32BitConstants(param->rootParameterIndex, (UINT)inParam.values.size(), inParam.values.data(), inParam.destOffsetIn32BitValues);
		}
	}
}

void D3DRenderCommandList::bindComputeShaderParameters(
	PipelineState* pipelineState,
	const ShaderParameterTable* inParameters,
	DescriptorHeap* descriptorHeap,
	DescriptorIndexTracker* tracker)
{
	D3DComputePipelineState* d3dPipelineState = static_cast<D3DComputePipelineState*>(pipelineState);
	ID3D12RootSignature* d3dRootSig = d3dPipelineState->getRootSignature();

	ID3D12DescriptorHeap* d3dDescriptorHeap = static_cast<D3DDescriptorHeap*>(descriptorHeap)->getRaw();
	D3D12_GPU_DESCRIPTOR_HANDLE baseHandle = d3dDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	const uint64 descriptorSize = (uint64)device->getDescriptorSizeCbvSrvUav();
	auto calcDescriptorHandle = [&baseHandle, &descriptorSize](uint32 descriptorIndex) {
		D3D12_GPU_DESCRIPTOR_HANDLE handle = baseHandle;
		handle.ptr += (uint64)descriptorIndex * descriptorSize;
		return handle;
	};

	ID3D12DescriptorHeap* d3dDescriptorHeaps[] = { d3dDescriptorHeap };
	commandList->SetComputeRootSignature(d3dRootSig);
	commandList->SetDescriptorHeaps(_countof(d3dDescriptorHeaps), d3dDescriptorHeaps);

	auto setRootDescriptorTables = [d3dPipelineState, device = gRenderDevice, &calcDescriptorHandle, descriptorHeap]<typename T>(ID3D12GraphicsCommandList* cmdList, const std::vector<T>& parameters, uint32* inoutDescriptorIx)
	{
		for (const auto& inParam : parameters)
		{
			const D3DShaderParameter* param = d3dPipelineState->findShaderParameter(inParam.name);
			if (param == nullptr)
			{
				reportUndeclaredShaderParameter(inParam.name.c_str());
				continue;
			}
			device->copyDescriptors(inParam.count, descriptorHeap, *inoutDescriptorIx, inParam.sourceHeap, inParam.startIndex);
			cmdList->SetComputeRootDescriptorTable(param->rootParameterIndex, calcDescriptorHandle(*inoutDescriptorIx));
			*inoutDescriptorIx += inParam.count;
		}
	};

	// #todo-dx12: Root Descriptor vs Descriptor Table
	// For now, always use descriptor table.
	uint32 descriptorIx = (tracker == nullptr) ? 0 : tracker->lastIndex;

	for (const auto& inParam : inParameters->_pushConstants)
	{
		const D3DShaderParameter* param = d3dPipelineState->findShaderParameter(inParam.name);
		if (param == nullptr)
		{
			reportUndeclaredShaderParameter(inParam.name.c_str());
			continue;
		}
		if (inParam.values.size() == 1)
		{
			commandList->SetComputeRoot32BitConstant(param->rootParameterIndex, inParam.values[0], inParam.destOffsetIn32BitValues);
		}
		else
		{
			commandList->SetComputeRoot32BitConstants(param->rootParameterIndex, (UINT)inParam.values.size(), inParam.values.data(), inParam.destOffsetIn32BitValues);
		}
	}
	setRootDescriptorTables(commandList.Get(), inParameters->constantBuffers, &descriptorIx);
	setRootDescriptorTables(commandList.Get(), inParameters->structuredBuffers, &descriptorIx);
	setRootDescriptorTables(commandList.Get(), inParameters->rwBuffers, &descriptorIx);
	setRootDescriptorTables(commandList.Get(), inParameters->rwStructuredBuffers, &descriptorIx);
	setRootDescriptorTables(commandList.Get(), inParameters->byteAddressBuffers, &descriptorIx);
	setRootDescriptorTables(commandList.Get(), inParameters->textures, &descriptorIx);
	setRootDescriptorTables(commandList.Get(), inParameters->rwTextures, &descriptorIx);
	CHECK(inParameters->accelerationStructures.size() == 0); // Not allowed in compute pipeline.

	if (tracker != nullptr) tracker->lastIndex = descriptorIx;
}

void D3DRenderCommandList::drawIndexedInstanced(
	uint32 indexCountPerInstance,
	uint32 instanceCount,
	uint32 startIndexLocation,
	int32 baseVertexLocation,
	uint32 startInstanceLocation)
{
	commandList->DrawIndexedInstanced(
		indexCountPerInstance,
		instanceCount,
		startIndexLocation,
		baseVertexLocation,
		startInstanceLocation);
}

void D3DRenderCommandList::drawInstanced(
	uint32 vertexCountPerInstance,
	uint32 instanceCount,
	uint32 startVertexLocation,
	uint32 startInstanceLocation)
{
	commandList->DrawInstanced(
		vertexCountPerInstance,
		instanceCount,
		startVertexLocation,
		startInstanceLocation);
}

void D3DRenderCommandList::executeIndirect(
	CommandSignature* commandSignature,
	uint32 maxCommandCount,
	Buffer* argumentBuffer,
	uint64 argumentBufferOffset,
	Buffer* countBuffer /*= nullptr*/,
	uint64 countBufferOffset /*= 0*/)
{
	commandList->ExecuteIndirect(
		static_cast<D3DCommandSignature*>(commandSignature)->getRaw(),
		maxCommandCount,
		into_d3d::id3d12Resource(argumentBuffer),
		argumentBufferOffset,
		countBuffer ? into_d3d::id3d12Resource(countBuffer) : nullptr,
		countBufferOffset);
}

void D3DRenderCommandList::dispatchCompute(uint32 threadGroupX, uint32 threadGroupY, uint32 threadGroupZ)
{
	commandList->Dispatch(threadGroupX, threadGroupY, threadGroupZ);
}

AccelerationStructure* D3DRenderCommandList::buildRaytracingAccelerationStructure(
	uint32 numBLASDesc,
	BLASInstanceInitDesc* blasDescArray)
{
	ID3D12DeviceLatest* rawDevice = device->getRawDevice();

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags
		= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescArray(numBLASDesc);

	D3DAccelerationStructure* accelStruct = new D3DAccelerationStructure(device);
	accelStruct->initialize(numBLASDesc);

	for (uint32 blasIndex = 0; blasIndex < numBLASDesc; ++blasIndex)
	{
		const auto& geomDescArray = blasDescArray[blasIndex].geomDescs;
		const uint32 numGeomDesc = (uint32)geomDescArray.size();

		std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> d3d_geomDescArray(numGeomDesc);
		for (uint32 i = 0; i < numGeomDesc; ++i)
		{
			into_d3d::raytracingGeometryDesc(geomDescArray[i], d3d_geomDescArray[i]);
		}

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomLevelInputs{};
		bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		bottomLevelInputs.Flags = buildFlags;
		bottomLevelInputs.NumDescs = (uint32)d3d_geomDescArray.size();
		bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		bottomLevelInputs.pGeometryDescs = d3d_geomDescArray.data();

		accelStruct->buildBLAS(commandList.Get(), blasIndex, blasDescArray[blasIndex], bottomLevelInputs);
	}

	accelStruct->waitForBLASBuild(commandList.Get());

	accelStruct->buildTLAS(commandList.Get(), buildFlags);

	return accelStruct;
}

void D3DRenderCommandList::bindRaytracingShaderParameters(
	RaytracingPipelineStateObject* pipelineState,
	const ShaderParameterTable* inParameters,
	DescriptorHeap* descriptorHeap,
	DescriptorHeap* samplerHeap /* = nullptr */)
{
	// #todo-sampler: Currently only support static samplers. What to do in future:
	// 1. Implement RenderDevice::createSampler().
	// 2. Maintain global sampler heap.
	// 3. Use samplerHeap to bind samplers.
	// 4. Add samplerHeap parameter to bindGraphicsShaderParameters() and bindComputeShaderParameters() also.
	CHECK(samplerHeap == nullptr);

	CHECK(descriptorHeap != nullptr && descriptorHeap->getCreateParams().type == EDescriptorHeapType::CBV_SRV_UAV);
	CHECK(samplerHeap == nullptr || samplerHeap->getCreateParams().type == EDescriptorHeapType::SAMPLER);

	D3DRaytracingPipelineStateObject* d3dPipelineState = static_cast<D3DRaytracingPipelineStateObject*>(pipelineState);
	ID3D12RootSignature* globalRootSig = d3dPipelineState->getGlobalRootSignature();

	ID3D12DescriptorHeap* d3dDescriptorHeap = static_cast<D3DDescriptorHeap*>(descriptorHeap)->getRaw();
	uint32 numValidHeaps = 1;

	D3D12_GPU_DESCRIPTOR_HANDLE baseHandle = d3dDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	const uint64 descriptorSize = (uint64)device->getDescriptorSizeCbvSrvUav();
	auto calcDescriptorHandle = [&baseHandle, &descriptorSize](uint32 descriptorIndex) {
		D3D12_GPU_DESCRIPTOR_HANDLE handle = baseHandle;
		handle.ptr += (uint64)descriptorIndex * descriptorSize;
		return handle;
	};

	ID3D12DescriptorHeap* d3dSamplerHeap = nullptr;
	if (samplerHeap != nullptr)
	{
		d3dSamplerHeap = static_cast<D3DDescriptorHeap*>(samplerHeap)->getRaw();
		numValidHeaps += 1;
	}

	ID3D12DescriptorHeap* d3dDescriptorHeaps[] = { d3dDescriptorHeap, d3dSamplerHeap };
	commandList->SetComputeRootSignature(globalRootSig);
	commandList->SetDescriptorHeaps(numValidHeaps, d3dDescriptorHeaps);

	auto setRootDescriptorTables = [d3dPipelineState, device = gRenderDevice, &calcDescriptorHandle, descriptorHeap]
		<typename T>(ID3D12GraphicsCommandList* cmdList, const std::vector<T>& parameters, uint32* inoutDescriptorIx)
	{
		for (const auto& inParam : parameters)
		{
			const D3DShaderParameter* param = d3dPipelineState->findGlobalShaderParameter(inParam.name);
			if (param == nullptr)
			{
				reportUndeclaredShaderParameter(inParam.name.c_str());
				continue;
			}
			device->copyDescriptors(inParam.count, descriptorHeap, *inoutDescriptorIx, inParam.sourceHeap, inParam.startIndex);
			cmdList->SetComputeRootDescriptorTable(param->rootParameterIndex, calcDescriptorHandle(*inoutDescriptorIx));
			*inoutDescriptorIx += inParam.count;
		}
	};

	// #todo-dx12: Root Descriptor vs Descriptor Table
	// For now, always use descriptor table.
	uint32 descriptorIx = 0;
	uint32 samplerDescriptorIx = 0;

	for (const auto& inParam : inParameters->_pushConstants)
	{
		const D3DShaderParameter* param = d3dPipelineState->findGlobalShaderParameter(inParam.name);
		if (param == nullptr)
		{
			reportUndeclaredShaderParameter(inParam.name.c_str());
			continue;
		}
		if (inParam.values.size() == 1)
		{
			commandList->SetComputeRoot32BitConstant(param->rootParameterIndex, inParam.values[0], inParam.destOffsetIn32BitValues);
		}
		else
		{
			commandList->SetComputeRoot32BitConstants(param->rootParameterIndex, (UINT)inParam.values.size(), inParam.values.data(), inParam.destOffsetIn32BitValues);
		}
	}
	setRootDescriptorTables(commandList.Get(), inParameters->constantBuffers, &descriptorIx);
	setRootDescriptorTables(commandList.Get(), inParameters->structuredBuffers, &descriptorIx);
	setRootDescriptorTables(commandList.Get(), inParameters->rwBuffers, &descriptorIx);
	setRootDescriptorTables(commandList.Get(), inParameters->rwStructuredBuffers, &descriptorIx);
	setRootDescriptorTables(commandList.Get(), inParameters->byteAddressBuffers, &descriptorIx);
	setRootDescriptorTables(commandList.Get(), inParameters->textures, &descriptorIx);
	setRootDescriptorTables(commandList.Get(), inParameters->rwTextures, &descriptorIx);
	for (const auto& inParam : inParameters->accelerationStructures)
	{
		D3DShaderResourceView* d3dSRV = static_cast<D3DShaderResourceView*>(inParam.srv);
		D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = d3dSRV->getGPUVirtualAddress();

		const D3DShaderParameter* param = d3dPipelineState->findGlobalShaderParameter(inParam.name);
		if (param == nullptr)
		{
			reportUndeclaredShaderParameter(inParam.name.c_str());
			continue;
		}
		commandList->SetComputeRootShaderResourceView(param->rootParameterIndex, gpuAddr);
	}
}

void D3DRenderCommandList::dispatchRays(const DispatchRaysDesc& inDesc)
{
	D3D12_DISPATCH_RAYS_DESC desc{};
	into_d3d::dispatchRaysDesc(inDesc, desc);
	commandList->DispatchRays(&desc);
}

void D3DRenderCommandList::beginEventMarker(const char* eventName)
{
	::PIXBeginEvent(commandList.Get(), 0x00000000, eventName);
}

void D3DRenderCommandList::endEventMarker()
{
	::PIXEndEvent(commandList.Get());
}
