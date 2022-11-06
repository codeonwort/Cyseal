#pragma once

#include "gpu_resource.h"
#include "render_command.h"
#include "util/enum_util.h"

class RenderTargetView;

// ------------------------------------ //
// #todo-vulkan: Vulkan API for texture //
// 
// VkImage, VkDeviceMemory, VkImageCreateInfo,
// VkMemoryRequirements, VkMemoryAllocateInfo,
// vkCreateImage, vkGetImageMemoryRequirements,
// vkAllocateMemory, vkBindImageMemory, ...
// ------------------------------------ //

/*
typedef struct D3D12_RESOURCE_DESC
{
    D3D12_RESOURCE_DIMENSION Dimension;
    UINT64 Alignment;
    UINT64 Width;
    UINT Height;
    UINT16 DepthOrArraySize;
    UINT16 MipLevels;
    DXGI_FORMAT Format;
    DXGI_SAMPLE_DESC SampleDesc;
    D3D12_TEXTURE_LAYOUT Layout;
    D3D12_RESOURCE_FLAGS Flags;
} D3D12_RESOURCE_DESC;

typedef struct VkImageCreateInfo {
    VkStructureType          sType;
    const void*              pNext;
    VkImageCreateFlags       flags;
    VkImageType              imageType;
    VkFormat                 format;
    VkExtent3D               extent;
    uint32_t                 mipLevels;
    uint32_t                 arrayLayers;
    VkSampleCountFlagBits    samples;
    VkImageTiling            tiling;
    VkImageUsageFlags        usage;
    VkSharingMode            sharingMode;
    uint32_t                 queueFamilyIndexCount;
    const uint32_t*          pQueueFamilyIndices;
    VkImageLayout            initialLayout;
} VkImageCreateInfo;
*/

class Texture : public GPUResource
{
public:
    virtual void uploadData(RenderCommandList& commandList, const void* buffer, uint64 rowPitch, uint64 slicePitch) = 0;
    virtual void setDebugName(const wchar_t* debugName) = 0;

    virtual RenderTargetView* getRTV() const = 0;
    virtual ShaderResourceView* getSRV() const = 0;
    virtual DepthStencilView* getDSV() const = 0;

    // Element index in the descriptor heap from which the descriptor was created.
    virtual uint32 getSRVDescriptorIndex() const = 0;
    virtual uint32 getRTVDescriptorIndex() const = 0;
    virtual uint32 getDSVDescriptorIndex() const = 0;
    virtual uint32 getUAVDescriptorIndex() const = 0;

private:
    // #todo-texture: getUAV()
};
