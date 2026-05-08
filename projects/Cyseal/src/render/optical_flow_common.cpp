#include "optical_flow_common.h"

void ffxSpdSetup(
	uint32 dispatchThreadGroupCountXY[2],
	uint32 workGroupOffset[2],
	uint32 numWorkGroupsAndMips[2],
	const uint32 rectInfo[4],
	const int32 mips)
{
	// determines the offset of the first tile to downsample based on
	// left (rectInfo[0]) and top (rectInfo[1]) of the subregion.
	workGroupOffset[0] = rectInfo[0] / 64;
	workGroupOffset[1] = rectInfo[1] / 64;

	uint32 endIndexX = (rectInfo[0] + rectInfo[2] - 1) / 64;  // rectInfo[0] = left, rectInfo[2] = width
	uint32 endIndexY = (rectInfo[1] + rectInfo[3] - 1) / 64;  // rectInfo[1] = top, rectInfo[3] = height

	// we only need to dispatch as many thread groups as tiles we need to downsample
	// number of tiles per slice depends on the subregion to downsample
	dispatchThreadGroupCountXY[0] = endIndexX + 1 - workGroupOffset[0];
	dispatchThreadGroupCountXY[1] = endIndexY + 1 - workGroupOffset[1];

	// number of thread groups per slice
	numWorkGroupsAndMips[0] = (dispatchThreadGroupCountXY[0]) * (dispatchThreadGroupCountXY[1]);

	if (mips >= 0)
	{
		numWorkGroupsAndMips[1] = uint32(mips);
	}
	else
	{
		// calculate based on rect width and height
		uint32 resolution = (std::max)(rectInfo[2], rectInfo[3]);
		numWorkGroupsAndMips[1] = (uint32)((std::min(std::floor(std::log2(float(resolution))), float(12))));
	}
}
