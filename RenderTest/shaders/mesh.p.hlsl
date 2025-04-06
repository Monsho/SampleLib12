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
	float4	orm		: SV_TARGET1;
	float4	normal	: SV_TARGET2;
};

struct ResourceIndex
{
	uint	SceneCB;
	uint	texColor;
	uint	texNormal;
	uint	texORM;
	uint	samLinearWrap;
};

ConstantBuffer<ResourceIndex>	cbResIndex	: register(b0);

PSOutput main(PSInput In)
{
	PSOutput Out = (PSOutput)0;

	ConstantBuffer<SceneCB> cbScene = ResourceDescriptorHeap[cbResIndex.SceneCB];
	Texture2D texColor = ResourceDescriptorHeap[cbResIndex.texColor];
	Texture2D texNormal = ResourceDescriptorHeap[cbResIndex.texNormal];
	Texture2D texORM = ResourceDescriptorHeap[cbResIndex.texORM];
	SamplerState samLinearWrap = SamplerDescriptorHeap[cbResIndex.samLinearWrap];

	float4 baseColor = texColor.Sample(samLinearWrap, In.uv);
	float3 orm = texORM.Sample(samLinearWrap, In.uv);

	float3 T, B, N;
	GetTangentSpace(In.normal, In.tangent, T, B, N);
	float3 normalInTS = texNormal.Sample(samLinearWrap, In.uv).xyz * 2 - 1;
	normalInTS *= float3(1, -sign(In.tangent.w), 1);
	float3 normalInWS = ConvertVectorTangetToWorld(normalInTS, T, B, N);

	Out.color = baseColor;
	Out.orm.rgb = orm;
	Out.normal.xyz = normalInWS * 0.5 + 0.5;

	return Out;
}
