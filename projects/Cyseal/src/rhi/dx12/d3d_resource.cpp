#include "d3d_resource.h"
#include "d3d_resource_view.h"
#include "d3d_device.h"
#include "d3d_render_command.h"
#include "rhi/render_device.h"

//////////////////////////////////////////////////////////////////////////
// D3DAccelerationStructure

D3DAccelerationStructure::~D3DAccelerationStructure()
{
	if (instanceDescBuffer)
	{
		instanceDescBuffer->Unmap(0, 0);
	}
}

ShaderResourceView* D3DAccelerationStructure::getSRV() const
{
	return srv.get();
}

void D3DAccelerationStructure::initialize(uint32 numBLAS)
{
	ID3D12DeviceLatest* device = getD3DDevice()->getRawDevice();
	totalBLAS = numBLAS;
	
	blasScratchResourceArray.resize(totalBLAS);
	blasResourceArray.resize(totalBLAS);

	allocateUploadBuffer(
		nullptr,
		totalBLAS * sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
		&instanceDescBuffer,
		L"AccelStruct_InstanceDesc");
	instanceDescBuffer->Map(0, nullptr, (void**)(&instanceDescMapPtr));

	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = { NULL };
	srv = std::make_unique<D3DShaderResourceView>(this, nullptr, 0xffffffff, cpuHandle);
}

void D3DAccelerationStructure::buildBLAS(
	ID3D12GraphicsCommandList4* commandList,
	uint32 blasIndex,
	const BLASInstanceInitDesc& blasInitDesc,
	const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& bottomLevelInputs)
{
	CHECK(blasIndex < totalBLAS);
	ID3D12DeviceLatest* device = getD3DDevice()->getRawDevice();

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo{};
	device->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &prebuildInfo);
	CHECK(prebuildInfo.ResultDataMaxSizeInBytes > 0);

	wchar_t debugName[256];
	swprintf_s(debugName, L"AccelStruct_BLASScratchBuffer_%u", blasIndex);
	allocateUAVBuffer(
		prebuildInfo.ScratchDataSizeInBytes,
		&blasScratchResourceArray[blasIndex],
		D3D12_RESOURCE_STATE_COMMON,
		debugName);

	swprintf_s(debugName, L"AccelStruct_BLAS_%u", blasIndex);
	allocateUAVBuffer(
		prebuildInfo.ResultDataMaxSizeInBytes,
		&blasResourceArray[blasIndex],
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		debugName);

	D3D12_RAYTRACING_INSTANCE_DESC instanceDesc{};
	memcpy(instanceDesc.Transform[0], blasInitDesc.instanceTransform[0], sizeof(float) * 4);
	memcpy(instanceDesc.Transform[1], blasInitDesc.instanceTransform[1], sizeof(float) * 4);
	memcpy(instanceDesc.Transform[2], blasInitDesc.instanceTransform[2], sizeof(float) * 4);
	instanceDesc.InstanceID = 0;
	instanceDesc.InstanceMask = 1;
	instanceDesc.InstanceContributionToHitGroupIndex = blasIndex;
	instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
	instanceDesc.AccelerationStructure = blasResourceArray[blasIndex]->GetGPUVirtualAddress();

	uint8* destPtr = instanceDescMapPtr + (blasIndex * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
	memcpy_s(destPtr, sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
		&instanceDesc, sizeof(D3D12_RAYTRACING_INSTANCE_DESC));

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blasBuildDesc{};
	blasBuildDesc.Inputs = bottomLevelInputs;
	blasBuildDesc.ScratchAccelerationStructureData = blasScratchResourceArray[blasIndex]->GetGPUVirtualAddress();
	blasBuildDesc.DestAccelerationStructureData = blasResourceArray[blasIndex]->GetGPUVirtualAddress();

	commandList->BuildRaytracingAccelerationStructure(&blasBuildDesc, 0, nullptr);
}

void D3DAccelerationStructure::waitForBLASBuild(ID3D12GraphicsCommandList4* commandList)
{
	// UAV barrier for TLAS build
	std::vector<CD3DX12_RESOURCE_BARRIER> blasWaitBarriers(totalBLAS);
	for (uint32 i = 0; i < totalBLAS; ++i)
	{
		blasWaitBarriers[i] = CD3DX12_RESOURCE_BARRIER::UAV(blasResourceArray[i].Get());
	}
	commandList->ResourceBarrier(totalBLAS, blasWaitBarriers.data());
}

void D3DAccelerationStructure::buildTLAS(
	ID3D12GraphicsCommandList4* commandList,
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags)
{
	ID3D12DeviceLatest* device = getD3DDevice()->getRawDevice();
	tlasBuildFlags = buildFlags;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs{};
	topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	topLevelInputs.Flags = buildFlags;
	topLevelInputs.NumDescs = totalBLAS;
	topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	topLevelInputs.InstanceDescs = instanceDescBuffer->GetGPUVirtualAddress();

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo{};
	device->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &prebuildInfo);
	CHECK(prebuildInfo.ResultDataMaxSizeInBytes > 0);

	allocateUAVBuffer(
		prebuildInfo.ScratchDataSizeInBytes,
		&tlasScratchResource,
		//D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_COMMON,
		L"AccelStruct_TLASScratchBuffer");

	allocateUAVBuffer(
		prebuildInfo.ResultDataMaxSizeInBytes,
		&tlasResource,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		L"AccelStruct_TLAS");

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlasBuildDesc{};
	tlasBuildDesc.Inputs = topLevelInputs;
	tlasBuildDesc.DestAccelerationStructureData = getTLASGpuVirtualAddress();
	tlasBuildDesc.ScratchAccelerationStructureData = tlasScratchResource->GetGPUVirtualAddress();

	commandList->BuildRaytracingAccelerationStructure(&tlasBuildDesc, 0, nullptr);
}

void D3DAccelerationStructure::rebuildTLAS(
	RenderCommandList* commandListWrapper,
	uint32 numInstanceUpdates,
	const BLASInstanceUpdateDesc* updateDescs)
{
	ID3D12GraphicsCommandList4* commandList = static_cast<D3DRenderCommandList*>(commandListWrapper)->getRaw();
	
	for (uint32 i = 0; i < numInstanceUpdates; ++i)
	{
		const uint32 blasIndex = updateDescs[i].blasIndex;
		CHECK(blasIndex < totalBLAS);

		uint8* destPtr = instanceDescMapPtr + (blasIndex * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));

		D3D12_RAYTRACING_INSTANCE_DESC instanceDesc{};
		memcpy_s(&instanceDesc, sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
			destPtr, sizeof(D3D12_RAYTRACING_INSTANCE_DESC));

		memcpy(instanceDesc.Transform[0], updateDescs[i].instanceTransform[0], sizeof(float) * 4);
		memcpy(instanceDesc.Transform[1], updateDescs[i].instanceTransform[1], sizeof(float) * 4);
		memcpy(instanceDesc.Transform[2], updateDescs[i].instanceTransform[2], sizeof(float) * 4);

		memcpy_s(destPtr, sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
			&instanceDesc, sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
	}

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs{};
	topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	topLevelInputs.Flags = tlasBuildFlags;
	topLevelInputs.NumDescs = totalBLAS;
	topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	topLevelInputs.InstanceDescs = instanceDescBuffer->GetGPUVirtualAddress();

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlasBuildDesc{};
	tlasBuildDesc.Inputs = topLevelInputs;
	tlasBuildDesc.DestAccelerationStructureData = getTLASGpuVirtualAddress();
	tlasBuildDesc.ScratchAccelerationStructureData = tlasScratchResource->GetGPUVirtualAddress();

	commandList->BuildRaytracingAccelerationStructure(&tlasBuildDesc, 0, nullptr);
}

void D3DAccelerationStructure::allocateUAVBuffer(
	UINT64 bufferSize,
	ID3D12Resource** ppResource,
	D3D12_RESOURCE_STATES initialResourceState /*= D3D12_RESOURCE_STATE_COMMON*/,
	const wchar_t* resourceName /*= nullptr*/)
{
	ID3D12DeviceLatest* device = getD3DDevice()->getRawDevice();

	auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	HR(device->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		initialResourceState,
		nullptr,
		IID_PPV_ARGS(ppResource)));
	if (resourceName)
	{
		(*ppResource)->SetName(resourceName);
	}
}

void D3DAccelerationStructure::allocateUploadBuffer(
	void* pData,
	UINT64 datasize,
	ID3D12Resource** ppResource,
	const wchar_t* resourceName /*= nullptr*/)
{
	ID3D12DeviceLatest* device = getD3DDevice()->getRawDevice();

	auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(datasize);
	HR(device->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(ppResource)));
	if (resourceName != nullptr)
	{
		(*ppResource)->SetName(resourceName);
	}
	if (pData != nullptr)
	{
		void* pMappedData;
		(*ppResource)->Map(0, nullptr, &pMappedData);
		memcpy(pMappedData, pData, datasize);
		(*ppResource)->Unmap(0, nullptr);
	}
}
