#pragma once

class RenderDevice;
class VertexBuffer;

enum class EResourceViewDimension
{
	Buffer,
	TEXTURE_1D,
	TEXTURE_2D,
	TEXTURE_3D
};

class RenderTargetView
{

// public:
// 	virtual void initialize(RenderDevice* renderDevice) = 0;

};

class DepthStencilView
{
	
// public:
// 	virtual void initialize(RenderDevice* renderDevice) = 0;

};

class UnorderedAccessView
{
	
public:
	virtual void initialize(RenderDevice* renderDevice) = 0;

};
