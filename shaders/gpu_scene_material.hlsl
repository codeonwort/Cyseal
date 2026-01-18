#include "material.hlsl"

struct MaterialCommand
{
	uint         itemIndex;
	uint         _pad0;
	uint         _pad1;
	uint         _pad2;
	Material     materialData;
};

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
    uint numCommands;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants>     pushConstants;

RWStructuredBuffer<Material>      materialBuffer;
StructuredBuffer<MaterialCommand> commandBuffer;

// ------------------------------------------------------------------------
// Compute shader

[numthreads(1, 1, 1)]
void mainCS(uint3 tid: SV_DispatchThreadID)
{
    uint commandID = tid.x;
    if (commandID >= pushConstants.numCommands)
    {
        return;
    }

	MaterialCommand cmd = commandBuffer.Load(commandID);
	materialBuffer[cmd.itemIndex] = cmd.materialData;
}
