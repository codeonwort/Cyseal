#ifndef _BSDF_H
#define _BSDF_H

#include "common.hlsl"

#define MIRROR_REFLECTION_ROUGHNESS 0.001

// #todo-deprecated: Keep legacy code for future reference. Cleanup someday.
#define REWORK_SPECULAR_BRDF 1
#define LEGACY_SPECULAR_BRDF 0

// ---------------------------------------------------------
// Definitions

struct MicrofacetBRDFInput
{
	float3 inRayDir;
	float3 surfaceNormal;
	float3 baseColor;
	float roughness;
	float metallic;
	float rand0, rand1; // Uniform R.V.
};
struct MicrofacetBRDFOutput
{
	float3 diffuseReflectance;
	float3 specularReflectance;
	float3 outRayDir;
	float pdf;
};

// ---------------------------------------------------------
// Utils

bool microfacetBRDFOutputIsInvalid(in MicrofacetBRDFOutput output)
{
	return output.pdf == 0;
}

bool microfacetBRDFOutputHasNaN(MicrofacetBRDFOutput output)
{
	bool b1 = any(isnan(output.diffuseReflectance));
	bool b2 = any(isnan(output.specularReflectance));
	bool b3 = any(isnan(output.outRayDir));
	bool b4 = isnan(output.pdf);
	return b1 || b2 || b3 || b4;
}

// This is always confusing, M = (T B N) and it is column-major by default.
// So mul(vector, matrix) in that order.
float3 rotateVector(float3 v, float3x3 M)
{
	return mul(v, M);
}

// V   : Incoming direction
// N   : Surface normal
// ior : Index of Refraction (= ior_in_prev_matter / ior_in_next_matter)
float3 getRefractedDirection(float3 V, float3 N, float ior)
{
	float inCosTheta = dot(-V, N);
	float outSinThetaSq = ior * ior * (1 - inCosTheta * inCosTheta);

	return ior * V + (ior * inCosTheta - sqrt(1 - outSinThetaSq)) * N;
}

// cosTheta = dot(incident_or_exitant_light, half_vector)
float3 fresnelSchlick(float cosTheta, float3 F0)
{
	return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
	return F0 + (max(1.0 - roughness, F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

// ---------------------------------------------------------
// BSDF

#if REWORK_SPECULAR_BRDF

namespace bsdf_private
{
	float square(float x) { return x * x; }
	
	float cosTheta(float3 w) { return w.z; }
	float cos2Theta(float3 w) { return sqrt(w.z); }
	float absCosTheta(float3 w) { return abs(w.z); }
	
	float sin2Theta(float3 w) { return max(0, 1 - cos2Theta(w)); }
	float sinTheta(float3 w) { return sqrt(sin2Theta(w)); }
	
	float tanTheta(float3 w) { return sinTheta(w) / cosTheta(w); }
	float tan2Theta(float3 w) { return sin2Theta(w) / cos2Theta(w); }
	
	float cosPhi(float3 w)
	{
		float t = sinTheta(w);
		return (t == 0) ? 1 : clamp(w.x / t, -1, 1);
	}
	float sinPhi(float3 w)
	{
		float t = sinTheta(w);
		return (t == 0) ? 0 : clamp(w.y / t, -1, 1);
	}
	
	// Subroutine for masking function. Specific for Trowbridge-Reitz distribution.
	// w       : microsurface normal
	// alpha_x : roughnessX
	// alpha_y : roughnessY
	float geometry1_lambda(float3 w, float alpha_x, float alpha_y)
	{
		float t = tan2Theta(w);
		if (isinf(t)) return 0;
		float alpha2 = square(cosPhi(w) * alpha_x) + square(sinPhi(w) * alpha_y);
		return 0.5 * (sqrt(1 + alpha2 * t) - 1);
	}
	
	float geometry1(float3 w, float alpha_x, float alpha_y)
	{
		return 1 / (1 + geometry1_lambda(w, alpha_x, alpha_y));
	}
}

// Torrance-Sparrow specular BRDF
// Reference: https://pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory
namespace torranceSparrowBrdf
{
	float trowbridgeReitzDistribution(float3 wm, float alpha_x, float alpha_y)
	{
		float t = bsdf_private::tan2Theta(wm);
		if (isinf(t)) return 0;
		float cos4Theta = bsdf_private::square(bsdf_private::cos2Theta(wm));
		float e = t * (bsdf_private::square(bsdf_private::cosPhi(wm) / alpha_x) + bsdf_private::square(bsdf_private::sinPhi(wm) / alpha_y));
		return 1 / (PI * alpha_x * alpha_y * cos4Theta * bsdf_private::square(1 + e));
	}

	// Masking-shadowing function.
	// All vectors are in local space.
	// Wo      : incoming path direction
	// Wi      : scattered direction
	// alpha_x : roughnessX
	// alpha_y : roughnessY
	float geometrySmithGGX(float3 Wo, float3 Wi, float alpha_x, float alpha_y)
	{
		float a1 = bsdf_private::geometry1_lambda(Wo, alpha_x, alpha_y);
		float a2 = bsdf_private::geometry1_lambda(Wi, alpha_x, alpha_y);
		return 1 / (1 + a1 + a2);
	}

	// https://pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory#SamplingtheDistributionofVisibleNormals
	// 9.6.4 Sampling the Distribution of Visible Normals
	float D(float3 w, float3 wm, float alpha_x, float alpha_y)
	{
		float d = trowbridgeReitzDistribution(wm, alpha_x, alpha_y);
		return (bsdf_private::geometry1(w, alpha_x, alpha_y) / bsdf_private::absCosTheta(w)) * d * abs(dot(w, wm));
	}
	float PDF(float3 w, float3 wm, float alpha_x, float alpha_y)
	{
		return D(w, wm, alpha_x, alpha_y);
	}

	float2 sampleUniformDiskPolar(float2 u)
	{
		float r = sqrt(u.x);
		float theta = 2 * PI * u.y;
		return float2(r * cos(theta), r * sin(theta));
	}

	// w = Wo
	// u = uniform random x, y
	// alpha_x, alpha_y = roughnessX,Y
	float3 sample_wm(float3 w, float2 u, float alpha_x, float alpha_y)
	{
		// Transform w to hemispherical config.
		float3 wh = normalize(float3(alpha_x * w.x, alpha_y * w.y, w.z));
		if (wh.z < 0)
			wh = -wh;
		// Find orthonormal basis for visible normal sampling.
		float3 T1 = (wh.z < 0.99999) ? normalize(cross(float3(0, 0, 1), wh)) : float3(1, 0, 0);
		float3 T2 = cross(wh, T1);
		// Generate uniformly distributed points on the unit disk.
		float2 p = sampleUniformDiskPolar(u);
		// Warp hemispherical projection for visible normal sampling.
		float h = sqrt(1 - (p.x * p.x));
		p.y = lerp(h, p.y, (1 + wh.z) / 2);
		// Reproject to hemisphere and transform normal to ellipsoid config.
		float pz = sqrt(max(0, 1 - dot(p, p)));
		float3 nh = p.x * T1 + p.y * T2 + pz * wh;
		return normalize(float3(alpha_x * nh.x, alpha_y * nh.y, max(1e-6, nh.z)));
	}

	MicrofacetBRDFOutput microfacetBRDF(MicrofacetBRDFInput input)
	{
		// 1. Transform to local space.
	
		float3 inRayDir = input.inRayDir;
		// #todo-pathtracing: Transform surfaceNormal properly when normalmap is introduced.
		float3 surfaceNormal = input.surfaceNormal;
		float3 baseColor = input.baseColor;
		float roughness = input.roughness;
		float metallic = input.metallic;
		float rand0 = input.rand0;
		float rand1 = input.rand1;

		// Incoming ray can hit any side of the surface, so if hit backface, then rather flip the surfaceNormal.
		if (dot(surfaceNormal, inRayDir) > 0.0)
		{
			surfaceNormal *= -1;
		}

		// Do all BRDF calculations in local space where macrosurface normal is z-axis (0, 0, 1).
		// Pick random tangent and bitangent in xy-plane.
		float3 worldT, worldB;
		computeTangentFrame(surfaceNormal, worldT, worldB);
		float3x3 localToWorld = float3x3(worldT, worldB, surfaceNormal);
		float3x3 worldToLocal = transpose(localToWorld);
	
		// 2. Find Wh and Wi from Wo.
		
		// Wo, Wi faces outwards. Wh is normalized half-vector.
		float3 N = float3(0, 0, 1);
		float3 Wo = rotateVector(-inRayDir, worldToLocal);
		float3 Wh, Wi;
		if (roughness < MIRROR_REFLECTION_ROUGHNESS)
		{
			Wi = reflect(-Wo, N);
			Wh = normalize(Wo + Wi);
		}
		else
		{
			Wh = sample_wm(Wo, float2(rand0, rand1), roughness, roughness);
			Wi = reflect(-Wo, Wh);
		}

		// As I'm sampling Wh and deriving Wi from Wo and Wh, Wi can go other side of the surface.
		// In that case, invalidate current sample by setting pdf = 0.
		// The integrator should reject an invalid sample on its own.
		if (Wi.z <= 0.0)
		{
			MicrofacetBRDFOutput output;
			output.diffuseReflectance = 0;
			output.specularReflectance = 0;
			output.outRayDir = rotateVector(Wi, localToWorld);
			output.pdf = 0;
			return output;
		}
		
		// 3. Compute PDF of Wi.
		
		float pdf = PDF(Wo, Wh, roughness, roughness) / (4 * abs(dot(Wo, Wh)));
		
		// 4. Compute specular BRDF.
		
		float cosTheta_o = bsdf_private::absCosTheta(Wo);
		float cosTheta_i = bsdf_private::absCosTheta(Wi);
		
		float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
		float3 _f = fresnelSchlick(dot(Wh, Wi), F0);
		float _d = trowbridgeReitzDistribution(Wh, roughness, roughness);
		float _g = geometrySmithGGX(Wo, Wi, roughness, roughness);
		
		// 5. Compute diffuse BRDF.
		
		float3 kD = 1.0 - _f;
		float3 diffuse = baseColor * (1.0 - metallic);
		
		// 6. Return the result.
		
		MicrofacetBRDFOutput output;
		output.diffuseReflectance = (kD * diffuse) * cosTheta_i;
		output.outRayDir = rotateVector(Wi, localToWorld);
		if (roughness < MIRROR_REFLECTION_ROUGHNESS)
		{
			output.specularReflectance = _f * cosTheta_i;
			output.pdf = 1.0;
		}
		else
		{
			float3 specularBRDF = (_d * _f * _g) / (4 * cosTheta_i * cosTheta_o);
			output.specularReflectance = specularBRDF * cosTheta_i;
			output.pdf = pdf;
		}

		return output;
	}
}

#endif

#if LEGACY_SPECULAR_BRDF

// All vectors are in local space.
// N     : macrosurface normal
// M     : half-vector
// alpha : roughness
float distributionGGX(float3 N, float3 M, float alpha)
{
	float NdotM = dot(N, M);

	float a = NdotM * alpha;
	float k = alpha / (1.0 - NdotM * NdotM + a * a);
	return k * k / PI;
}

// V : Wi or Wo
// M : half-vector
float geometry1(float3 V, float3 M, float alpha)
{
	float VdotM = dot(V, M);
	float k = alpha * alpha * saturate(1.0 - (1.0 / (VdotM * VdotM)));
	return 2.0 / (1.0 + sqrt(1.0 + k));
}

// All vectors are in local space.
// M     : half-vector
// Wo    : incoming path direction
// Wi    : scattered direction
// alpha : roughness
float geometrySmithGGX(float3 M, float3 Wo, float3 Wi, float alpha)
{
	return geometry1(Wo, M, alpha) * geometry1(Wi, M, alpha);
	//float MdotWo = dot(M, Wo);
	//float MdotWi = dot(M, Wi);
	//float a2 = alpha * alpha;
	//float g1 = MdotWi * sqrt(MdotWo * MdotWo * (1.0 - a2) + a2);
	//float g2 = MdotWo * sqrt(MdotWi * MdotWi * (1.0 - a2) + a2);
	//return 0.5 / (g1 + g2);
}

// https://hal.science/hal-01509746/document
// All vectors are in local space.
// V_      : half-vector
// alpha_x : roughnessX
// alpha_x : roughnessY
// U1, U2  : Random floats uniformly distributed in [0, 1).
float3 sampleGGXVNDF(float3 V_, float alpha_x, float alpha_y, float U1, float U2)
{
	// stretch view
	float3 V = normalize(float3(alpha_x * V_.x, alpha_y * V_.y, V_.z));
	// orthonormal basis
	float3 T1 = (V.z < 0.9999) ? normalize(cross(V, float3(0, 0, 1))) : float3(1, 0, 0);
	float3 T2 = cross(T1, V);
	// sample point with polar coordinates (r, phi)
	float a = 1.0 / (1.0 + V.z);
	float r = sqrt(U1);
	float phi = (U2 < a) ? U2 / a * PI : PI + (U2 - a) / (1.0 - a) * PI;
	float P1 = r * cos(phi);
	float P2 = r * sin(phi) * ((U2 < a) ? 1.0 : V.z);
	// compute normal
	float3 N = P1 * T1 + P2 * T2 + sqrt(max(0.0, 1.0 - P1 * P1 - P2 * P2)) * V;
	// unstretch
	N = normalize(float3(alpha_x * N.x, alpha_y * N.y, max(0.0, N.z)));
	return N;
}

// "Microfacet Models for Refraction through Rough Surfaces"
MicrofacetBRDFOutput legacyMicrofacetBRDF(MicrofacetBRDFInput input)
{
	float3 inRayDir      = input.inRayDir;
	// #todo-pathtracing: Transform surfaceNormal properly when normalmap is introduced.
	float3 surfaceNormal = input.surfaceNormal;
	float3 baseColor     = input.baseColor;
	float roughness      = input.roughness;
	float metallic       = input.metallic;
	float rand0          = input.rand0;
	float rand1          = input.rand1;

	// Incoming ray can hit any side of the surface, so if hit backface, then rather flip the surfaceNormal.
	if (dot(surfaceNormal, inRayDir) > 0.0)
	{
		surfaceNormal *= -1;
	}

	// Do all BRDF calculations in local space where macrosurface normal is z-axis (0, 0, 1).
	// Pick random tangent and bitangent in xy-plane.
	float3 worldT, worldB;
	computeTangentFrame(surfaceNormal, worldT, worldB);
	float3x3 localToWorld = float3x3(worldT, worldB, surfaceNormal);
	float3x3 worldToLocal = transpose(localToWorld);
	
	// Wo, Wi faces outwards.
	// Wh = normalized half-vector
	float3 N = float3(0, 0, 1);
	float3 Wo = rotateVector(-inRayDir, worldToLocal);
	float3 Wh, Wi;
	if (roughness < MIRROR_REFLECTION_ROUGHNESS)
	{
		Wi = reflect(-Wo, N);
		Wh = normalize(Wo + Wi);
	}
	else
	{
		Wh = sampleGGXVNDF(Wo, roughness, roughness, rand0, rand1);
		Wi = reflect(-Wo, Wh);
	}

	// As I'm sampling Wh and deriving Wi from Wo and Wh, Wi actually can go other side of the surface.
	// In that case, invalidate current sample by setting pdf = 0.
	// The integrator will reject a sample with zero probability.
	bool bInvalidWi = Wi.z <= 0.0;
	if (bInvalidWi)
	{
		MicrofacetBRDFOutput output;
		output.diffuseReflectance = 0;
		output.specularReflectance = 0;
		output.outRayDir = rotateVector(Wi, localToWorld);
		output.pdf = 0;
		return output;
	}

	float NdotWo = dot(N, Wo);
	float NdotWi = dot(N, Wi);

	float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);

	float3 F = fresnelSchlick(dot(Wh, Wi), F0);
	float G, NDF;
	if (roughness < MIRROR_REFLECTION_ROUGHNESS)
	{
		G = 1;
		NDF = 1;
	}
	else
	{
		G = geometrySmithGGX(Wh, Wo, Wi, roughness);
		NDF = distributionGGX(N, Wh, roughness);
	}

	float3 kS = F;
	float3 kD = 1.0 - kS;
	float3 diffuse = baseColor * (1.0 - metallic);
	float3 specular = (F * G * NDF) / (4.0 * NdotWi * NdotWo + 0.001);
	
	MicrofacetBRDFOutput output;
	output.diffuseReflectance = (kD * diffuse) * NdotWi;
	output.outRayDir = rotateVector(Wi, localToWorld);
	if (roughness < MIRROR_REFLECTION_ROUGHNESS)
	{
		output.specularReflectance = kS * NdotWi;
		output.pdf = 1.0;
	}
	else
	{
		output.specularReflectance = kS * specular * NdotWi;
		output.pdf = 1.0 / (0.001 + 4.0 * dot(Wh, Wo));
	}
	return output;
}

#endif // LEGACY_SPECULAR_BRDF

#endif // _BSDF_H
