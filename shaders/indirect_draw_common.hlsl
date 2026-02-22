#ifndef _INDIRECT_DRAW_COMMON_H
#define _INDIRECT_DRAW_COMMON_H

#include "indirect_arguments.hlsl"

// #wip: Specific to base pass
struct StaticMeshDrawCommand
{
	uint                         sceneItemIndex; // index in gpu scene buffer
	D3D12_VERTEX_BUFFER_VIEW     positionBufferView;
	D3D12_VERTEX_BUFFER_VIEW     nonPositionBufferView;
	D3D12_INDEX_BUFFER_VIEW      indexBufferView;
	D3D12_DRAW_INDEXED_ARGUMENTS drawIndexedArguments;
};

#endif
