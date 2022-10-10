#include "d3d_texture.h"
#include "d3d_device.h"
#include "d3d_pipeline_state.h"
#include "d3d_render_command.h"
#include "core/assertion.h"

// Convert API-agnostic structs into D3D12 structs
namespace into_d3d
{
	inline D3D12_RESOURCE_DIMENSION textureDimension(ETextureDimension dimension)
	{
		switch (dimension)
		{
		case ETextureDimension::UNKNOWN: return D3D12_RESOURCE_DIMENSION_UNKNOWN;
		case ETextureDimension::TEXTURE1D: return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
		case ETextureDimension::TEXTURE2D: return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		case ETextureDimension::TEXTURE3D: return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
		}
		CHECK_NO_ENTRY();
		return D3D12_RESOURCE_DIMENSION_UNKNOWN;
	}

	inline D3D12_RESOURCE_DESC textureDesc(const TextureCreateParams& params)
	{
		D3D12_RESOURCE_DESC desc;
		ZeroMemory(&desc, sizeof(desc));

		desc.Dimension = textureDimension(params.dimension);
		desc.Alignment = 0; // #todo-dx12: Always default alignment
		desc.Width = params.width;
		desc.Height = params.height;
		desc.DepthOrArraySize = params.depth;
		desc.MipLevels = params.mipLevels;
		desc.Format = into_d3d::pixelFormat(params.format);
		desc.SampleDesc.Count = params.sampleCount;
		desc.SampleDesc.Quality = params.sampleQuality;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; // #todo-dx12: Always default layout
		desc.Flags = D3D12_RESOURCE_FLAG_NONE; // #todo-dx12: Do I need allow flags?

		return desc;
	}
}

void D3DTexture::initialize(const TextureCreateParams& params)
{
	createParams = params;

	auto device = getD3DDevice()->getRawDevice();
	D3D12_RESOURCE_DESC textureDesc = into_d3d::textureDesc(params);
	
	HR(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&rawResource)));

	if (0 != (params.accessFlags & ETextureAccessFlags::CPU_WRITE))
	{
		const UINT64 uploadBufferSize = ::GetRequiredIntermediateSize(rawResource.Get(), 0, 1);

		HR(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&textureUploadHeap)));
	}
	
	if (0 != (params.accessFlags & ETextureAccessFlags::SRV))
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = textureDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; // #todo-dx12: SRV ViewDimension
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = textureDesc.MipLevels;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		getD3DDevice()->allocateSRVHeapHandle(srvHandle, descriptorIndexInHeap);
		device->CreateShaderResourceView(rawResource.Get(), &srvDesc, srvHandle);
	}

	// #todo-texture: RTV and UAV
}

void D3DTexture::uploadData(RenderCommandList& commandList, const void* buffer, uint64 rowPitch, uint64 slicePitch)
{
	CHECK(0 != (createParams.accessFlags & ETextureAccessFlags::CPU_WRITE));

	ID3D12GraphicsCommandList* rawCommandList = static_cast<D3DRenderCommandList*>(&commandList)->getRaw();

	D3D12_SUBRESOURCE_DATA textureData;
	textureData.pData = buffer;
	textureData.RowPitch = rowPitch;
	textureData.SlicePitch = slicePitch;

	UpdateSubresources(rawCommandList, rawResource.Get(), textureUploadHeap.Get(), 0, 0, 1, &textureData);

	commandList.transitionResource(this,
		EGPUResourceState::COPY_DEST,
		EGPUResourceState::PIXEL_SHADER_RESOURCE);
}

void D3DTexture::setDebugName(const wchar_t* debugName)
{
	rawResource->SetName(debugName);
}
