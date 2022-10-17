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
};

class DepthStencilView
{
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
};
