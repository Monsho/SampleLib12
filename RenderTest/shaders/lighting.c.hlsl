#include "cbuffer.hlsli"

struct ResourceIndex
{
	uint cbScene;
	uint texGBufferA;
	uint texGBufferB;
	uint texGBufferC;
	uint texDepth;
	uint texAO;
	uint rLight;
	uint rwOutput;
};

ConstantBuffer<ResourceIndex>	cbResIndex	: register(b0);

float3 Lighting(uint2 pixelPos, float depth)
{
	ConstantBuffer<SceneCB> cbScene = ResourceDescriptorHeap[cbResIndex.cbScene];
	Texture2D texGBufferA = ResourceDescriptorHeap[cbResIndex.texGBufferA];
	Texture2D texGBufferB = ResourceDescriptorHeap[cbResIndex.texGBufferB];
	Texture2D texGBufferC = ResourceDescriptorHeap[cbResIndex.texGBufferC];
	Texture2D texAO = ResourceDescriptorHeap[cbResIndex.texAO];
	StructuredBuffer<LightData> rLight = ResourceDescriptorHeap[cbResIndex.rLight];

	// get gbuffer.
	float4 color = texGBufferA[pixelPos];
	float3 orm = texGBufferB[pixelPos].xyz;
	float3 normal = normalize(texGBufferC[pixelPos].xyz * 2.0 - 1.0);
	float ao = texAO[pixelPos].x;
	if (!(cbScene.frag & 0x01))
	{
		ao = 1.0;
	}

	// get world position.
	float2 screenPos = ((float2)pixelPos + 0.5) / cbScene.screenSize;
	float2 clipSpacePos = screenPos * float2(2, -2) + float2(-1, 1);
	float4 worldPos = mul(cbScene.mtxProjToWorld, float4(clipSpacePos, depth, 1));
	worldPos.xyz /= worldPos.w;

	// apply light.
	float NoL = dot(normal, -rLight[0].dir);
	NoL = NoL * 0.5 + 0.5;
	float3 diffuse = lerp(color.rgb, 0, orm.b);
	return diffuse * NoL * rLight[0].color * ao;
}

[numthreads(8, 8, 1)]
void main(
	uint3 gid : SV_GroupID,
	uint3 gtid : SV_GroupThreadID,
	uint3 did : SV_DispatchThreadID)
{
	uint2 pixelPos = did.xy;

	ConstantBuffer<SceneCB> cbScene = ResourceDescriptorHeap[cbResIndex.cbScene];
	Texture2D<float> texDepth = ResourceDescriptorHeap[cbResIndex.texDepth];
	RWTexture2D<float4> rwOutput = ResourceDescriptorHeap[cbResIndex.rwOutput];

	if (all(pixelPos < (uint2)cbScene.screenSize))
	{
		float depth = texDepth[pixelPos];
		[branch]
		if (depth >= 1.0)
		{
			rwOutput[pixelPos] = float4(0, 0, 1, 1);
		}
		else
		{
			rwOutput[pixelPos] = float4(Lighting(pixelPos, depth), 1);
		}
	}
}