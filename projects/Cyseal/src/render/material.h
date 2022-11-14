#pragma once

class Texture;

class Material
{
public:
	Texture* albedoTexture = nullptr;
	float albedoMultiplier[3] = { 1.0f, 1.0f, 1.0f };
	float roughness = 0.0f;
};
