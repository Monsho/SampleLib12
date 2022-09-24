#include "cbuffer.hlsli"
#include "math.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
	float3	normal		: NORMAL;
	float4	tangent		: TANGENT;
	float2	uv			: TEXCOORD0;
};

struct PSOutput
{
	float4	color	: SV_TARGET0;
};

ConstantBuffer<SceneCB>					cbScene			: register(b0);

Texture2D			texColor		: register(t0);
Texture2D			texNormal		: register(t1);
Texture2D			texORM			: register(t2);
SamplerState		texColor_s		: register(s0);

PSOutput main(PSInput In)
{
	PSOutput Out = (PSOutput)0;

	float4 baseColor = texColor.Sample(texColor_s, In.uv);
	float3 orm = texORM.Sample(texColor_s, In.uv);

	float3 T, B, N;
	GetTangentSpace(In.normal, In.tangent, T, B, N);
	float3 normalInTS = texNormal.Sample(texColor_s, In.uv).xyz * 2 - 1;
	normalInTS *= float3(1, -sign(In.tangent.w), 1);
	float3 normalInWS = ConvertVectorTangetToWorld(normalInTS, T, B, N);

	Out.color = baseColor * saturate(normalInWS.y);

	return Out;
}
