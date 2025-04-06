#include "cbuffer.hlsli"

struct ResourceIndex
{
	uint cbScene;
	uint texDepth;
	uint texAOHistory;
	uint rwOutput;
	uint samLinearClamp;
};

ConstantBuffer<ResourceIndex>	cbResIndex	: register(b0);

float IGN(float2 uv, int frame)
{
	uv += float(frame)  * (float2(47, 17) * 0.695f);
	float3 magic = float3(0.06711056, 0.00583715, 52.9829189);  
	return frac(magic.z * frac(dot(uv, magic.xy)));
}

float ComputeAO(uint2 pixelPos, float depth)
{
	ConstantBuffer<SceneCB> cbScene = ResourceDescriptorHeap[cbResIndex.cbScene];
	Texture2D<float> texDepth = ResourceDescriptorHeap[cbResIndex.texDepth];
	Texture2D<float> texHistory = ResourceDescriptorHeap[cbResIndex.texAOHistory];
	SamplerState samLinearClamp = SamplerDescriptorHeap[cbResIndex.samLinearClamp];

	// compute view depth.
	float2 screenPos = ((float2)pixelPos + 0.5) / cbScene.screenSize;
	float2 clipSpacePos = screenPos * float2(2, -2) + float2(-1, 1);
	float4 viewPos = mul(cbScene.mtxProjToView, float4(clipSpacePos, depth, 1));
	float Zorig = viewPos.z / viewPos.w;

	float angle = IGN(float2(pixelPos), cbScene.frameTime) * 3.1415926;
	float2 offset = float2(cos(angle), sin(angle)) / cbScene.screenSize;
	float ao = 0.0;
	float count = 0.0;
	for (int i = 1; i <= 12; i++)
	{
		float2 uv = saturate(screenPos + offset * float(i));
		float d = texDepth.SampleLevel(samLinearClamp, uv, 0);
		float2 csPos = uv * float2(2, -2) + float2(-1, 1);
		float4 vPos = mul(cbScene.mtxProjToView, float4(csPos, d, 1));
		float Z = vPos.z / vPos.w;
		float dZ = Z - Zorig;
		if (dZ > 1.0 && dZ < 1000.0)
		{
			ao += 1.0;
		}

		uv = saturate(screenPos - offset * float(i));
		d = texDepth.SampleLevel(samLinearClamp, uv, 0);
		csPos = uv * float2(2, -2) + float2(-1, 1);
		vPos = mul(cbScene.mtxProjToView, float4(csPos, d, 1));
		Z = vPos.z / vPos.w;
		dZ = Z - Zorig;
		if (dZ > 1.0 && dZ < 1000.0)
		{
			ao += 1.0;
		}

		count += 2.0;
	}

	float currAO = 1.0 - (ao / count);
	float prevAO = texHistory.SampleLevel(samLinearClamp, screenPos, 0.0);
	return lerp(currAO, prevAO, 0.9);
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
	RWTexture2D<float> rwOutput = ResourceDescriptorHeap[cbResIndex.rwOutput];

	if (all(pixelPos < (uint2)cbScene.screenSize))
	{
		float depth = texDepth[pixelPos];
		[branch]
		if (depth >= 1.0)
		{
			rwOutput[pixelPos] = 1.0;
		}
		else
		{
			rwOutput[pixelPos] = ComputeAO(pixelPos, depth);
		}
	}
}