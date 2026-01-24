#pragma once

enum class EMaterialId : uint32
{
	None        = 0,
	DefaultLit  = 1, // Microfacet BRDF, no transmission
	Glass       = 2, // Only transmission
};

// Eugene Hecht, "Optics" 5th ed.
// Table of index of refraction
namespace IoR
{
	constexpr float Air                             = 1.00029f;
	constexpr float Ice                             = 1.31f;
	constexpr float Water                           = 1.333f;
	constexpr float EthyAlcohol                     = 1.36f; // C2H5OH
	constexpr float Kerosene                        = 1.448f;
	constexpr float FusedQuartz                     = 1.4584f; // SiO2
	constexpr float KaroSyrup                       = 1.46f;
	constexpr float CarbonTetrachloride             = 1.46f; // CCl4
	constexpr float OliveOil                        = 1.47f;
	constexpr float Turpentine                      = 1.472f;
	constexpr float OldFormularPyrex                = 1.48f;
	constexpr float Benzene41_CarbonTetrachloride59 = 1.48f; // 41% Benzene + 59% carbon tetrachloride
	constexpr float MethylMethacrylate              = 1.492f;
	constexpr float Benzene                         = 1.501f; // C6H6
	constexpr float Plexiglass                      = 1.51f;
	constexpr float OilOfCedarwood                  = 1.51f;
	constexpr float CrownGlass                      = 1.52f;
	constexpr float SodiumChrloride                 = 1.544f; // NaCl
	constexpr float LightFlintGlass                 = 1.58f;
	constexpr float Polycarbonate                   = 1.586f;
	constexpr float Polystyrene                     = 1.591f;
	constexpr float CarbonDisulfide                 = 1.628f; // CS2
	constexpr float DenseFlintGlass                 = 1.66f;
	constexpr float Sapphire                        = 1.77f;
	constexpr float LanthanumFlintGlass             = 1.8f;
	constexpr float HeavyFlintGlass                 = 1.89f;
	constexpr float Zircon                          = 1.923f; // ZrO2.SiO2
	constexpr float Fabulite                        = 2.409f; // SrTiO3
	constexpr float Diamond                         = 2.417f;
	constexpr float Rutile                          = 2.907f; // TiO2
	constexpr float GalliumPhosphide                = 3.5f;
}

// Should match with Material in material.hlsl.
struct MaterialConstants
{
	vec3   albedoMultiplier   = vec3(1.0f, 1.0f, 1.0f);
	float  roughness          = 0.0f;

	uint32 albedoTextureIndex = 0xffffffff;
	vec3   emission           = vec3(0.0f, 0.0f, 0.0f);

	float  metalMask          = 0.0f;
	uint32 materialID         = (uint32)EMaterialId::DefaultLit;
	float  indexOfRefraction  = 1.0f;
	uint32 _pad0;

	vec3   transmittance       = vec3(0.0f);
	uint32 _pad1;
};
