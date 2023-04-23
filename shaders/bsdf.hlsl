#ifndef _BSDF_H
#define _BSDF_H

#include "common.hlsl"

// cosTheta = dot(incident_or_exitant_light, half_vector)
float3 fresnelSchlick(float cosTheta, float3 F0)
{
	return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// N: macrosurface normal
float geometryBeckmann(float3 N, float3 H, float3 V, float roughness)
{
	float thetaV = acos(dot(N, V));
	float tanThetaV = tan(thetaV);
	float a = 1.0 / (roughness * tanThetaV);
	float aa = a * a;

	if (dot(V, H) / dot(V, N) <= 0.0)
	{
		return 0.0;
	}

	if (a < 1.6)
	{
		float num = 3.535 * a + 2.181 * aa;
		float denom = 1.0 + 2.276 * a + 2.577 * aa;
		return num / denom;
	}
	return 1.0;
}

float geometrySmithBeckmann(float3 N, float3 H, float3 V, float3 L, float roughness)
{
	float ggx2 = geometryBeckmann(N, H, V, roughness);
	float ggx1 = geometryBeckmann(N, H, L, roughness);
	return 1.0f / (1.0f + ggx1 * ggx2);
}

float distributionBeckmann(float3 N, float3 H, float roughness)
{
	float cosH = dot(N, H);
	if (roughness == 0.0)
	{
		return 1.0;
	}
	if (H.z < 0.0) cosH = -cosH;
	float cosH2 = cosH * cosH;
	float rr = roughness * roughness;

	float exp_x = (1.0 - cosH2) / (rr * cosH);
	float num = (cosH > 0.0 ? 1.0 : 0.0) * exp(-exp_x);

	float denom = PI * rr * cosH2 * cosH2;
	return num / denom;
}

float ErfInv(float x)
{
	float w, p;
	x = clamp(x, -.99999f, .99999f);
	w = -log((1 - x) * (1 + x));
	if (w < 5) {
		w = w - 2.5f;
		p = 2.81022636e-08f;
		p = 3.43273939e-07f + p * w;
		p = -3.5233877e-06f + p * w;
		p = -4.39150654e-06f + p * w;
		p = 0.00021858087f + p * w;
		p = -0.00125372503f + p * w;
		p = -0.00417768164f + p * w;
		p = 0.246640727f + p * w;
		p = 1.50140941f + p * w;
	} else {
		w = sqrt(w) - 3;
		p = -0.000200214257f;
		p = 0.000100950558f + p * w;
		p = 0.00134934322f + p * w;
		p = -0.00367342844f + p * w;
		p = 0.00573950773f + p * w;
		p = -0.0076224613f + p * w;
		p = 0.00943887047f + p * w;
		p = 1.00167406f + p * w;
		p = 2.83297682f + p * w;
	}
	return p * x;
}

float Erf(float x)
{
	// constants
	float a1 = 0.254829592f;
	float a2 = -0.284496736f;
	float a3 = 1.421413741f;
	float a4 = -1.453152027f;
	float a5 = 1.061405429f;
	float p = 0.3275911f;

	// Save the sign of x
	int sign = 1;
	if (x < 0) sign = -1;
	x = abs(x);

	// A&S formula 7.1.26
	float t = 1 / (1 + p * x);
	float y =
		1 -
		(((((a5 * t + a4) * t) + a3) * t + a2) * t + a1) * t * exp(-x * x);

	return sign * y;
}

float CosTheta(float3 w) { return w.z; }
float Cos2Theta(float3 w) { return w.z * w.z; }
float AbsCosTheta(float3 w) { return abs(w.z); }
float Sin2Theta(float3 w) { return max(0.0f, 1.0f - Cos2Theta(w)); }
float SinTheta(float3 w) { return sqrt(Sin2Theta(w)); }
float CosPhi(float3 w) {
	float sinTheta = SinTheta(w);
	return (sinTheta == 0) ? 1 : clamp(w.x / sinTheta, -1, 1);
}
float SinPhi(float3 w) {
	float sinTheta = SinTheta(w);
	return (sinTheta == 0) ? 0 : clamp(w.y / sinTheta, -1, 1);
}
void BeckmannSample11(
	float cosThetaI, float U1, float U2,
	out float slope_x, out float slope_y)
{
	const float Pi = PI;

	/* Special case (normal incidence) */
	if (cosThetaI > .9999) {
		float r = sqrt(-log(1.0f - U1));
		float sinPhi = sin(2 * Pi * U2);
		float cosPhi = cos(2 * Pi * U2);
		slope_x = r * cosPhi;
		slope_y = r * sinPhi;
		return;
	}

	/* The original inversion routine from the paper contained
	   discontinuities, which causes issues for QMC integration
	   and techniques like Kelemen-style MLT. The following code
	   performs a numerical inversion with better behavior */
	float sinThetaI =
		sqrt(max((float)0, (float)1 - cosThetaI * cosThetaI));
	float tanThetaI = sinThetaI / cosThetaI;
	float cotThetaI = 1 / tanThetaI;

	/* Search interval -- everything is parameterized
	   in the Erf() domain */
	float a = -1, c = Erf(cotThetaI);
	float sample_x = max(U1, (float)1e-6f);

	/* Start with a good initial guess */
	// float b = (1-sample_x) * a + sample_x * c;

	/* We can do better (inverse of an approximation computed in
	 * Mathematica) */
	float thetaI = acos(cosThetaI);
	float fit = 1 + thetaI * (-0.876f + thetaI * (0.4265f - 0.0594f * thetaI));
	float b = c - (1 + c) * pow(1 - sample_x, fit);

	/* Normalization factor for the CDF */
	const float SQRT_PI_INV = 1.f / sqrt(Pi);
	float normalization =
		1 /
		(1 + c + SQRT_PI_INV * tanThetaI * exp(-cotThetaI * cotThetaI));

	int it = 0;
	while (++it < 10) {
		/* Bisection criterion -- the oddly-looking
		   Boolean expression are intentional to check
		   for NaNs at little additional cost */
		if (!(b >= a && b <= c)) b = 0.5f * (a + c);

		/* Evaluate the CDF and its derivative
		   (i.e. the density function) */
		float invErf = ErfInv(b);
		float value =
			normalization *
			(1 + b + SQRT_PI_INV * tanThetaI * exp(-invErf * invErf)) -
			sample_x;
		float derivative = normalization * (1 - invErf * tanThetaI);

		if (abs(value) < 1e-5f) break;

		/* Update bisection intervals */
		if (value > 0)
			c = b;
		else
			a = b;

		b -= value / derivative;
	}

	/* Now convert back into a slope value */
	slope_x = ErfInv(b);

	/* Simulate Y component */
	slope_y = ErfInv(2.0f * max(U2, (float)1e-6f) - 1.0f);
}

float3 BeckmannSample(float3 wi, float alpha_x, float alpha_y, float U1, float U2)
{
	// 1. stretch wi
	float3 wiStretched = normalize(float3(alpha_x * wi.x, alpha_y * wi.y, wi.z));

	// 2. simulate P22_{wi}(x_slope, y_slope, 1, 1)
	float slope_x, slope_y;
	BeckmannSample11(CosTheta(wiStretched), U1, U2, slope_x, slope_y);

	// 3. rotate
	float tmp = CosPhi(wiStretched) * slope_x - SinPhi(wiStretched) * slope_y;
	slope_y = SinPhi(wiStretched) * slope_x + CosPhi(wiStretched) * slope_y;
	slope_x = tmp;

	// 4. unstretch
	slope_x = alpha_x * slope_x;
	slope_y = alpha_y * slope_y;

	// 5. compute normal
	return normalize(float3(-slope_x, -slope_y, 1.0));
}

float3 sampleWh(float rand0, float rand1, float3 Wo, float alpha)
{
	// https://pbr-book.org/3ed-2018/Light_Transport_I_Surface_Reflection/Sampling_Reflection_Functions#sec:microfacet-sample
	// Sample from the distribution of visible microfacets from a given wo.
	float3 Wh;
	bool bFlip = Wo.z < 0.0;
	Wh = BeckmannSample(bFlip ? -Wo : Wo, alpha, alpha, rand0, rand1);
	if (bFlip) Wh = -Wh;
	return Wh;
}

void microfactBRDF(
	float3 inRayDir, float3 surfaceNormal,
	float3 baseColor, float roughness, float metallic,
	float rand0, float rand1,
	out float3 outReflectance, out float3 outScatteredDir)
{
	float3 worldT, worldB;
	computeTangentFrame(surfaceNormal, worldT, worldB);
	float3x3 localToWorld = float3x3(worldT, worldB, surfaceNormal);
	float3x3 worldToLocal = transpose(localToWorld);

	float3 N = float3(0, 0, 1); // #todo-pathtracing: No bump mapping yet
	float3 Wo = mul(-inRayDir, worldToLocal);
	float3 Wh = sampleWh(rand0, rand1, Wo, roughness);
	float3 Wi = reflect(-Wo, Wh);
	float NdotWi = abs(dot(N, Wi));

	float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);

	float3 F = fresnelSchlick(abs(dot(Wh, Wo)), F0);
	float G = geometrySmithBeckmann(N, Wh, Wo, Wi, roughness);
	float NDF = distributionBeckmann(N, Wh, roughness);

	float3 kS = F;
	float3 kD = 1.0 - kS;
	float3 diffuse = baseColor - (1.0 - metallic);
	float3 specular = (F * G * NDF) / (4.0 * NdotWi * abs(dot(N, Wo)) + 0.001);

	outReflectance = (kD * diffuse + kS * specular) * NdotWi;
	outScatteredDir = mul(Wi, localToWorld);
	// #todo-pathtracing: outPdf
}

#endif // _BSDF_H
