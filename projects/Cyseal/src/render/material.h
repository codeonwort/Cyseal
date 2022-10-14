#pragma once

class Texture;

class Material
{
public:
	Texture* albedoTexture = nullptr;
	float albedoMultiplier[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
};
