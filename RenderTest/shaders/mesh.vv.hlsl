#include "cbuffer.hlsli"

struct VSInput
{
	float3	position	: POSITION;
	float3	normal		: NORMAL;
	float4	tangent		: TANGENT;
	float2	uv			: TEXCOORD;
};

struct VSOutput
{
	float4	position	: SV_POSITION;
	float3	normal		: NORMAL;
	float4	tangent		: TANGENT;
	float2	uv			: TEXCOORD;
};

struct ResourceIndex
{
	uint	SceneCB;
	uint	MeshCB;
};

ConstantBuffer<ResourceIndex>	cbResIndex	: register(b0);

VSOutput main(const VSInput In)
{
	VSOutput Out = (VSOutput)0;

	ConstantBuffer<SceneCB> cbScene = ResourceDescriptorHeap[cbResIndex.SceneCB];
	ConstantBuffer<MeshCB> cbMesh = ResourceDescriptorHeap[cbResIndex.MeshCB];

	float4x4 mtxLocalToProj = mul(cbScene.mtxWorldToProj, cbMesh.mtxLocalToWorld);

	Out.position = mul(mtxLocalToProj, float4(In.position, 1));
	Out.normal = normalize(mul((float3x3)cbMesh.mtxLocalToWorld, In.normal));
	Out.tangent.xyz = normalize(mul((float3x3)cbMesh.mtxLocalToWorld, In.tangent.xyz));
	Out.tangent.w = In.tangent.w;
	Out.uv = In.uv;

	return Out;
}

//	EOF
