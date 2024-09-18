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
	uint32 numTextureMemoryBarriers, const TextureMemoryBarrier* textureMemoryBarriers)
{
	// #todo-barrier: DX12 enhanced barriers
	// https://learn.microsoft.com/en-us/windows-hardware/drivers/display/enhanced-barriers
	// https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html#excessive-sync-latency

	uint32 totalBarriers = numBufferMemoryBarriers + numTextureMemoryBarriers;
	std::vector<D3D12_RESOURCE_BARRIER> rawBarriers(totalBarriers);
	for (uint32 i = 0; i < numBufferMemoryBarriers; ++i)
	{
		rawBarriers[i] = into_d3d::resourceBarrier(bufferMemoryBarriers[i]);
	}
	for (uint32 i = 0; i < numTextureMemoryBarriers; ++i)
	{
		rawBarriers[i + numBufferMemoryBarriers] = into_d3d::resourceBarrier(textureMemoryBarriers[i]);
	}

	commandList->ResourceBarrier(totalBarriers, rawBarriers.data());
}

void D3DRenderCommandList::clearRenderTargetView(RenderTargetView* RTV, const float* rgba)
{
	auto d3dRTV = static_cast<D3DRenderTargetView*>(RTV);
	auto rawRTV = d3dRTV->getCPUHandle();

	commandList->ClearRenderTargetView(rawRTV, rgba, 0, nullptr);
}

void D3DRenderCommandList::clearDepthStencilView(
	DepthStencilView* DSV,
	EDepthClearFlags clearFlags,
	float depth,
	uint8_t stencil)
{
	auto d3dDSV = static_cast<D3DDepthStencilView*>(DSV);
	auto rawDSV = d3dDSV->getCPUHandle();

	commandList->ClearDepthStencilView(
		rawDSV, (D3D12_CLEAR_FLAGS)clearFlags,
		depth, stencil,
		0, nullptr);
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

void D3DRenderCommandList::setPipelineState(PipelineState* state)
{
	auto rawState = static_cast<D3DGraphicsPipelineState*>(state)->getRaw();
	commandList->SetPipelineState(rawState);
}

void D3DRenderCommandList::setRaytracingPipelineState(RaytracingPipelineStateObject* rtpso)
{
	ID3D12StateObject* rawRTPSO
		= static_cast<D3DRaytracingPipelineStateObject*>(rtpso)->getRaw();
	commandList->SetPipelineState1(rawRTPSO);
}

void D3DRenderCommandList::setGraphicsRootSignature(RootSignature* rootSignature)
{
	auto rawSignature = static_cast<D3DRootSignature*>(rootSignature)->getRaw();
	commandList->SetGraphicsRootSignature(rawSignature);
}

void D3DRenderCommandList::setComputeRootSignature(RootSignature* rootSignature)
{
	auto rawSignature = static_cast<D3DRootSignature*>(rootSignature)->getRaw();
	commandList->SetComputeRootSignature(rawSignature);
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

void D3DRenderCommandList::setGraphicsRootDescriptorTable(
	uint32 rootParameterIndex,
	DescriptorHeap* descriptorHeap,
	uint32 descriptorStartOffset)
{
	ID3D12DescriptorHeap* rawHeap = static_cast<D3DDescriptorHeap*>(descriptorHeap)->getRaw();
	D3D12_GPU_DESCRIPTOR_HANDLE tableHandle = rawHeap->GetGPUDescriptorHandleForHeapStart();
	tableHandle.ptr += (uint64)descriptorStartOffset * (uint64)device->getDescriptorSizeCbvSrvUav();
	
	commandList->SetGraphicsRootDescriptorTable(rootParameterIndex, tableHandle);
}

void D3DRenderCommandList::setGraphicsRootConstant32(
	uint32 rootParameterIndex,
	uint32 constant32,
	uint32 destOffsetIn32BitValues)
{
	commandList->SetGraphicsRoot32BitConstant(
		rootParameterIndex,
		constant32,
		destOffsetIn32BitValues);
}

void D3DRenderCommandList::setGraphicsRootConstant32Array(
	uint32 rootParameterIndex,
	uint32 numValuesToSet,
	const void* srcData,
	uint32 destOffsetIn32BitValues)
{
	commandList->SetGraphicsRoot32BitConstants(
		rootParameterIndex,
		numValuesToSet,
		srcData,
		destOffsetIn32BitValues);
}

void D3DRenderCommandList::setGraphicsRootDescriptorSRV(
	uint32 rootParameterIndex,
	ShaderResourceView* srv)
{
	D3DShaderResourceView* d3dSRV = static_cast<D3DShaderResourceView*>(srv);
	D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = d3dSRV->getGPUVirtualAddress();

	commandList->SetGraphicsRootShaderResourceView(rootParameterIndex, gpuAddr);
}

void D3DRenderCommandList::setGraphicsRootDescriptorCBV(
	uint32 rootParameterIndex,
	ConstantBufferView* cbv)
{
	D3DConstantBufferView* d3dCBV = static_cast<D3DConstantBufferView*>(cbv);
	D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = d3dCBV->getGPUVirtualAddress();

	commandList->SetGraphicsRootConstantBufferView(rootParameterIndex, gpuAddr);
}

void D3DRenderCommandList::setGraphicsRootDescriptorUAV(
	uint32 rootParameterIndex,
	UnorderedAccessView* uav)
{
	D3DUnorderedAccessView* d3dUAV = static_cast<D3DUnorderedAccessView*>(uav);
	D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = d3dUAV->getGPUVirtualAddress();

	commandList->SetGraphicsRootUnorderedAccessView(rootParameterIndex, gpuAddr);
}

void D3DRenderCommandList::setComputeRootConstant32(
	uint32 rootParameterIndex,
	uint32 constant32,
	uint32 destOffsetIn32BitValues)
{
	commandList->SetComputeRoot32BitConstant(
		rootParameterIndex,
		constant32,
		destOffsetIn32BitValues);
}

void D3DRenderCommandList::setComputeRootConstant32Array(
	uint32 rootParameterIndex,
	uint32 numValuesToSet,
	const void* srcData,
	uint32 destOffsetIn32BitValues)
{
	commandList->SetComputeRoot32BitConstants(
		rootParameterIndex,
		numValuesToSet,
		srcData,
		destOffsetIn32BitValues);
}

void D3DRenderCommandList::setComputeRootDescriptorTable(
	uint32 rootParameterIndex,
	DescriptorHeap* descriptorHeap,
	uint32 descriptorStartOffset)
{
	ID3D12DescriptorHeap* rawHeap = static_cast<D3DDescriptorHeap*>(descriptorHeap)->getRaw();
	D3D12_GPU_DESCRIPTOR_HANDLE tableHandle = rawHeap->GetGPUDescriptorHandleForHeapStart();
	tableHandle.ptr += (uint64)descriptorStartOffset * (uint64)device->getDescriptorSizeCbvSrvUav();

	commandList->SetComputeRootDescriptorTable(rootParameterIndex, tableHandle);
}

void D3DRenderCommandList::bindComputeShaderParameters(
	ShaderStage* shader,
	const ShaderParameterTable* inParameters,
	DescriptorHeap* descriptorHeap)
{
	D3DShaderStage* d3dShader = static_cast<D3DShaderStage*>(shader);
	ID3D12RootSignature* d3dRootSig = d3dShader->getRootSignature();

	ID3D12DescriptorHeap* d3dDescriptorHeap = static_cast<D3DDescriptorHeap*>(descriptorHeap)->getRaw();
	D3D12_GPU_DESCRIPTOR_HANDLE baseHandle = d3dDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	const uint64 descriptorSize = (uint64)device->getDescriptorSizeCbvSrvUav();
	auto computeDescriptorHandle = [&baseHandle, &descriptorSize](uint32 descriptorIndex) {
		D3D12_GPU_DESCRIPTOR_HANDLE handle = baseHandle;
		handle.ptr += (uint64)descriptorIndex * descriptorSize;
		return handle;
	};

	//CHECK_NO_ENTRY(); // WIP: Bind parameters

	ID3D12DescriptorHeap* d3dDescriptorHeaps[] = { d3dDescriptorHeap };
	commandList->SetComputeRootSignature(d3dRootSig);
	commandList->SetDescriptorHeaps(_countof(d3dDescriptorHeaps), d3dDescriptorHeaps);

	// #wip-dxc-reflection: Root Descriptor vs Descriptor Table
	uint32 descriptorIx = 0;

	for (const auto& inParam : inParameters->pushConstants)
	{
		const D3DShaderParameter* param = d3dShader->findShaderParameter(inParam.name);
		commandList->SetComputeRoot32BitConstant(param->rootParameterIndex, inParam.value, inParam.destOffsetIn32BitValues);
	}
	for (const auto& inParam : inParameters->constantBuffers)
	{
		// For now, always use root descriptor for constant buffers.
		const D3DShaderParameter* param = d3dShader->findShaderParameter(inParam.name);
#if 0
		ConstantBufferView* buffer = inParam.buffer;
		gRenderDevice->copyDescriptors(1, descriptorHeap, descriptorIx, buffer->getSourceHeap(), buffer->getDescriptorIndexInHeap());
		commandList->SetComputeRootDescriptorTable(param->rootParameterIndex, computeDescriptorHandle(descriptorIx));
		++descriptorIx;
#else
		D3DConstantBufferView* buffer = static_cast<D3DConstantBufferView*>(inParam.buffer);
		commandList->SetComputeRootConstantBufferView(param->rootParameterIndex, buffer->getGPUVirtualAddress());
#endif
	}
	for (const auto& inParam : inParameters->structuredBuffers)
	{
		const D3DShaderParameter* param = d3dShader->findShaderParameter(inParam.name);
		ShaderResourceView* buffer = inParam.buffer;
		gRenderDevice->copyDescriptors(1, descriptorHeap, descriptorIx, buffer->getSourceHeap(), buffer->getDescriptorIndexInHeap());
		commandList->SetComputeRootDescriptorTable(param->rootParameterIndex, computeDescriptorHandle(descriptorIx));
		++descriptorIx;
	}
	for (const auto& inParam : inParameters->rwBuffers)
	{
		const D3DShaderParameter* param = d3dShader->findShaderParameter(inParam.name);
		UnorderedAccessView* buffer = inParam.buffer;
		gRenderDevice->copyDescriptors(1, descriptorHeap, descriptorIx, buffer->getSourceHeap(), buffer->getDescriptorIndexInHeap());
		commandList->SetComputeRootDescriptorTable(param->rootParameterIndex, computeDescriptorHandle(descriptorIx));
		++descriptorIx;
	}
	for (const auto& inParam : inParameters->rwStructuredBuffers)
	{
		const D3DShaderParameter* param = d3dShader->findShaderParameter(inParam.name);
		UnorderedAccessView* buffer = inParam.buffer;
		gRenderDevice->copyDescriptors(1, descriptorHeap, descriptorIx, buffer->getSourceHeap(), buffer->getDescriptorIndexInHeap());
		commandList->SetComputeRootDescriptorTable(param->rootParameterIndex, computeDescriptorHandle(descriptorIx));
		++descriptorIx;
	}
	for (const auto& inParam : inParameters->textures)
	{
		const D3DShaderParameter* param = d3dShader->findShaderParameter(inParam.name);
		ShaderResourceView* buffer = inParam.texture;
		gRenderDevice->copyDescriptors(1, descriptorHeap, descriptorIx, buffer->getSourceHeap(), buffer->getDescriptorIndexInHeap());
		commandList->SetComputeRootDescriptorTable(param->rootParameterIndex, computeDescriptorHandle(descriptorIx));
		++descriptorIx;
	}
	for (const auto& inParam : inParameters->rwTextures)
	{
		const D3DShaderParameter* param = d3dShader->findShaderParameter(inParam.name);
		UnorderedAccessView* buffer = inParam.texture;
		gRenderDevice->copyDescriptors(1, descriptorHeap, descriptorIx, buffer->getSourceHeap(), buffer->getDescriptorIndexInHeap());
		commandList->SetComputeRootDescriptorTable(param->rootParameterIndex, computeDescriptorHandle(descriptorIx));
		++descriptorIx;
	}
}

void D3DRenderCommandList::setComputeRootDescriptorSRV(
	uint32 rootParameterIndex,
	ShaderResourceView* srv)
{
	D3DShaderResourceView* d3dSRV = static_cast<D3DShaderResourceView*>(srv);
	D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = d3dSRV->getGPUVirtualAddress();

	commandList->SetComputeRootShaderResourceView(rootParameterIndex, gpuAddr);
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

	D3DAccelerationStructure* accelStruct = new D3DAccelerationStructure;
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

void D3DRenderCommandList::dispatchRays(const DispatchRaysDesc& inDesc)
{
	auto getGpuAddress = [](RaytracingShaderTable* table) {
		return static_cast<D3DRaytracingShaderTable*>(table)->getGpuVirtualAddress();
	};
	auto getSizeInBytes = [](RaytracingShaderTable* table) {
		return static_cast<D3DRaytracingShaderTable*>(table)->getSizeInBytes();
	};
	auto getStrideInBytes = [](RaytracingShaderTable* table) {
		return static_cast<D3DRaytracingShaderTable*>(table)->getStrideInBytes();
	};

	D3D12_DISPATCH_RAYS_DESC desc{};
	desc.RayGenerationShaderRecord.StartAddress = getGpuAddress(inDesc.raygenShaderTable);
	desc.RayGenerationShaderRecord.SizeInBytes = getSizeInBytes(inDesc.raygenShaderTable);
	desc.MissShaderTable.StartAddress = getGpuAddress(inDesc.missShaderTable);
	desc.MissShaderTable.SizeInBytes = getSizeInBytes(inDesc.missShaderTable);
	desc.MissShaderTable.StrideInBytes = getStrideInBytes(inDesc.missShaderTable);
	desc.HitGroupTable.StartAddress = getGpuAddress(inDesc.hitGroupTable);
	desc.HitGroupTable.SizeInBytes = getSizeInBytes(inDesc.hitGroupTable);
	desc.HitGroupTable.StrideInBytes = getStrideInBytes(inDesc.hitGroupTable);
	// #todo-dxr: CallableShaderTable for dispatchRays()
	//desc.CallableShaderTable.StartAddress = 0;
	//desc.CallableShaderTable.SizeInBytes = 0;
	//desc.CallableShaderTable.StrideInBytes = 0;
	
	desc.Width = inDesc.width;
	desc.Height = inDesc.height;
	desc.Depth = inDesc.depth;

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
