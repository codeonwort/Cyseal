#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "core/vec3.h"
#include "core/matrix.h"
#include <math.h>
#include <stdlib.h>

// --------------------------------------------------------

// #todo-test: Check if porting was valid (possibly different math conventions).
// #todo-test: Assert if distribution is right.
// #todo-test: Find out why BxDF in shaders produces NaN.

// Port of bsdf.hlsl
namespace BxDF
{
	constexpr float PI = 3.14159265f;

	float saturate(float x)
	{
		return std::min(1.0f, std::max(0.0f, x));
	}

	void computeTangentFrame(vec3 N, vec3& T, vec3& B)
	{
		vec3 v = std::abs(N.z) < 0.99f ? vec3(0, 0, 1) : vec3(1, 0, 0);
		T = normalize(cross(v, N));
		B = normalize(cross(N, T));
	}

	// cosTheta = dot(incident_or_exitant_light, half_vector)
	vec3 fresnelSchlick(float cosTheta, vec3 F0)
	{
		return F0 + (1.0f - F0) * std::pow(1.0f - cosTheta, 5.0f);
	}

	vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
	{
		return F0 + (vecMax(1.0f - roughness, F0) - F0) * std::pow(1.0f - cosTheta, 5.0f);
	}

	// All vectors are in local space.
	// N     : macrosurface normal
	// M     : half-vector
	// alpha : roughness
	float distributionGGX(vec3 N, vec3 M, float alpha)
	{
		float NdotM = dot(N, M);

		float a = NdotM * alpha;
		float k = alpha / (1.0f - NdotM * NdotM + a * a);
		return k * k / PI;
	}

	// V : Wi or Wo
	// M : half-vector
	float geometry1(vec3 V, vec3 M, float alpha)
	{
		float VdotM = dot(V, M);
		float k = alpha * alpha * saturate(1.0f - (1.0f / (VdotM * VdotM)));
		return 2.0f / (1.0f + std::sqrt(1.0f + k));
	}

	// All vectors are in local space.
	// M     : half-vector
	// Wo    : incoming path direction
	// Wi    : scattered direction
	// alpha : roughness
	float geometrySmithGGX(vec3 M, vec3 Wo, vec3 Wi, float alpha)
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
	vec3 sampleGGXVNDF(vec3 V_, float alpha_x, float alpha_y, float U1, float U2)
	{
		// stretch view
		vec3 V = normalize(vec3(alpha_x * V_.x, alpha_y * V_.y, V_.z));
		// orthonormal basis
		vec3 T1 = (V.z < 0.9999f) ? normalize(cross(V, vec3(0, 0, 1))) : vec3(1, 0, 0);
		vec3 T2 = cross(T1, V);
		// sample point with polar coordinates (r, phi)
		float a = 1.0f / (1.0f + V.z);
		float r = std::sqrt(U1);
		float phi = (U2 < a) ? U2 / a * PI : PI + (U2 - a) / (1.0f - a) * PI;
		float P1 = r * std::cos(phi);
		float P2 = r * std::sin(phi) * ((U2 < a) ? 1.0f : V.z);
		// compute normal
		vec3 N = P1 * T1 + P2 * T2 + std::sqrt(std::max(0.0f, 1.0f - P1 * P1 - P2 * P2)) * V;
		// unstretch
		N = normalize(vec3(alpha_x * N.x, alpha_y * N.y, std::max(0.0f, N.z)));
		return N;
	}

	// "Microfacet Models for Refraction through Rough Surfaces"
	void microfacetBRDF(
		vec3 inRayDir, vec3 surfaceNormal,
		vec3 baseColor, float roughness, float metallic,
		float rand0, float rand1,
		vec3& outReflectance, vec3& outScatteredDir, float& outPdf)
	{
		// Incoming ray can hit any side of the surface, so if hit backface, then rather flip the surfaceNormal.
		if (dot(surfaceNormal, inRayDir) > 0.0)
		{
			surfaceNormal *= -1;
		}

		// Do all BRDF calculations in local space where macrosurface normal is z-axis (0, 0, 1).
		// Pick random tangent and bitangent in xy-plane.
		vec3 worldT, worldB;
		computeTangentFrame(surfaceNormal, worldT, worldB);
		float localToWorldData[16] = {
			worldT.x, worldB.x, surfaceNormal.x, 0.0f,
			worldT.y, worldB.y, surfaceNormal.y, 0.0f,
			worldT.z, worldB.z, surfaceNormal.z, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f,
		};
		Matrix localToWorld; localToWorld.copyFrom(localToWorldData);
		Matrix worldToLocal = localToWorld.transpose();

		// Wh = normalized half-vector
		vec3 N = vec3(0, 0, 1); // #todo-pathtracing: No bump mapping yet
		vec3 Wo = worldToLocal.transformDirection(-inRayDir);
		vec3 Wh = sampleGGXVNDF(Wo, roughness, roughness, rand0, rand1);
		vec3 Wi = reflect(-Wo, Wh);

		// As I'm sampling Wh and deriving Wi from Wo and Wh, Wi actually can go other side of the surface.
		// In that case, invalidate current sample by setting pdf = 0.
		// The integrator will reject a sample with zero probability.
		bool bInvalidWi = Wi.z <= 0.0;
		if (bInvalidWi)
		{
			outReflectance = 0;
			outScatteredDir = localToWorld.transformDirection(Wi);
			outPdf = 0;
			return;
		}

		float NdotWo = dot(N, Wo);
		float NdotWi = dot(N, Wi);

		vec3 F0 = lerp(vec3(0.04f), baseColor, metallic);

		vec3 F = fresnelSchlick(dot(Wh, Wi), F0);
		float G = geometrySmithGGX(Wh, Wo, Wi, roughness);
		float NDF = distributionGGX(N, Wh, roughness);

		vec3 kS = F;
		vec3 kD = 1.0f - kS;
		vec3 diffuse = baseColor * (1.0f - metallic);
		vec3 specular = (F * G * NDF) / (4.0f * NdotWi * NdotWo + 0.001f);

		outReflectance = (kD * diffuse + kS * specular) * NdotWi;
		outScatteredDir = localToWorld.transformDirection(Wi);
		outPdf = 1.0f / (0.001f + 4.0f * dot(Wh, Wo));
	}
}

namespace UnitTest
{
	TEST_CLASS(TestBxDF)
	{
	public:
		TEST_METHOD(MicrofacetBRDF)
		{
			std::srand(1234);

			vec3 rayDir = normalize(vec3(1.0f, -1.0f, 1.0f));
			vec3 surfaceNormal = normalize(vec3(0.0f, 1.0f, -0.5f));
			vec3 baseColor = vec3(0.9f);
			float roughness = 0.01f;
			float metalMask = 0.0f;
			
			for (int32 i = 0; i < 1000; ++i)
			{
				float rand0 = (float)std::rand() / RAND_MAX;
				float rand1 = (float)std::rand() / RAND_MAX;

				vec3 reflectance;
				vec3 scatteredDir;
				float pdf;

				BxDF::microfacetBRDF(
					rayDir, surfaceNormal, baseColor, roughness, metalMask, rand0, rand1,
					reflectance, scatteredDir, pdf);

				Assert::IsFalse(anyIsNaN(reflectance), L"Reflectance is NaN");
				Assert::IsFalse(anyIsNaN(scatteredDir), L"Scattered direction is NaN");
				Assert::IsFalse(isnan(pdf), L"PDF is NaN");
			}
		}
	};
}
