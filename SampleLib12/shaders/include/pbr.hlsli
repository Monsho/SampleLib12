#ifndef PBR_HLSLI
#define PBR_HLSLI

float3 DiffuseLambert(in float3 diffuseColor)
{
	return diffuseColor / PI;
}

float SpecularDGGX(in float NoH, in float roughness)
{
	float NoH2 = NoH * NoH;
	float a2 = NoH2 * roughness * roughness;
	float k = roughness / (1 - NoH2 + a2);
	return k * k / PI;
}

float SpecularGSmithCorrelated(in float NoV, in float NoL, in float roughness)
{
	float a2 = roughness * roughness;
	float V = NoL * sqrt(NoV * NoV * (1 - a2) + a2);
	float L = NoV * sqrt(NoL * NoL * (1 - a2) + a2);
	return 0.5 / (V + L);
}

float3 SpecularFSchlick(in float VoH, in float3 f0)
{
	float f = pow(1 - VoH, 5);
	return f + f0 * (1 - f);
}

float3 BrdfGGX(in float3 diffuseColor, in float3 specularColor, in float linearRoughness, in float3 N, in float3 L, in float3 V)
{
	float3 H = normalize(L + V);

	float NoV = saturate(abs(dot(N, V)) + Epsilon);
	float NoL = saturate(dot(N, L));
	float NoH = saturate(dot(N, H));
	float LoH = saturate(dot(L, H));

	float roughness = linearRoughness * linearRoughness;

	float D = SpecularDGGX(NoH, roughness);
	float G = SpecularGSmithCorrelated(NoV, NoL, roughness);
	float3 F = SpecularFSchlick(LoH, specularColor);

	float3 SResult = D * G * F;
	float3 DResult = DiffuseLambert(diffuseColor);

	return (DResult + SResult) * NoL;
}

#endif // PBR_HLSLI
//	EOF
