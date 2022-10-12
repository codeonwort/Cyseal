#pragma once

class RenderDevice;
class VertexBuffer;
class Texture;

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

class ShaderResourceView
{
public:
	ShaderResourceView(Texture* inOwner) : owner(inOwner) {}
protected:
	Texture* owner;
};

class UnorderedAccessView
{
	
public:
	virtual void initialize(RenderDevice* renderDevice) = 0;

};
