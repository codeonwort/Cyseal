#pragma once

// Should match with GPUSceneItem in common.hlsl
struct GPUSceneItem
{
	enum class FlagBits : uint32
	{
		IsValid = 1 << 0, // #wip: Use this to skip invalid items in the gpu scene shader.
	};

	Float4x4 localToWorld;
	Float4x4 prevLocalToWorld;

	vec3     localMinBounds;
	uint32   positionBufferOffset;

	vec3     localMaxBounds;
	uint32   nonPositionBufferOffset;

	uint32   indexBufferOffset;
	vec2     _pad0;
	FlagBits flags;
};
ENUM_CLASS_FLAGS(GPUSceneItem::FlagBits);

// Should match with definitions in gpu_scene.hlsl
struct GPUSceneEvictCommand
{
	uint32       sceneItemIndex;
};
struct GPUSceneAllocCommand
{
	uint32       sceneItemIndex;
	uint32       _pad0;
	uint32       _pad1;
	uint32       _pad2;
	GPUSceneItem sceneItem;
};
struct GPUSceneUpdateCommand
{
	uint32       sceneItemIndex;
	uint32       _pad0;
	uint32       _pad1;
	uint32       _pad2;
	Float4x4     localToWorld;
	Float4x4     prevLocalToWorld;
};
