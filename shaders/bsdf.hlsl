#ifndef _BSDF_H
#define _BSDF_H

#include "common.hlsl"

// cosTheta = dot(incident_or_exitant_light, half_vector)
float3 fresnelSchlick(float cosTheta, float3 F0)
{
	return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

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

// This is always confusing, M = (T B N) and it is column-major by default.
// So mul(vector, matrix) in that order.
float3 rotateVector(float3 v, float3x3 M)
{
	return mul(v, M);
}

// "Microfacet Models for Refraction through Rough Surfaces"
void microfactBRDF(
	float3 inRayDir, float3 surfaceNormal,
	float3 baseColor, float roughness, float metallic,
	float rand0, float rand1,
	out float3 outReflectance, out float3 outScatteredDir, out float outPdf)
{
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
	
	// Wh = normalized half-vector
	float3 N = float3(0, 0, 1); // #todo-pathtracing: No bump mapping yet
	float3 Wo = rotateVector(-inRayDir, worldToLocal);
	float3 Wh = sampleGGXVNDF(Wo, roughness, roughness, rand0, rand1);
	float3 Wi = reflect(-Wo, Wh);

	// As I'm sampling Wh and deriving Wi from Wo and Wh, Wi actually can go other side of the surface.
	// In that case, invalidate current sample by setting pdf = 0.
	// The integrator will reject a sample with zero probability.
	bool bInvalidWi = Wi.z <= 0.0;
	if (bInvalidWi)
	{
		outReflectance = 0;
		outScatteredDir = rotateVector(Wi, localToWorld);
		outPdf = 0;
		return;
	}

	float NdotWo = dot(N, Wo);
	float NdotWi = dot(N, Wi);

	float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);

	float3 F = fresnelSchlick(dot(Wh, Wi), F0);
	float G = geometrySmithGGX(Wh, Wo, Wi, roughness);
	float NDF = distributionGGX(N, Wh, roughness);

	float3 kS = F;
	float3 kD = 1.0 - kS;
	float3 diffuse = baseColor * (1.0 - metallic);
	float3 specular = (F * G * NDF) / (4.0 * NdotWi * NdotWo + 0.001);

	outReflectance = (kD * diffuse + kS * specular) * NdotWi;
	outScatteredDir = rotateVector(Wi, localToWorld);
	outPdf = 1.0 / (0.001 + 4.0 * dot(Wh, Wo));
}

#endif // _BSDF_H
