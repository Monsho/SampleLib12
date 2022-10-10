#ifndef CBUFFER_HLSLI
#define  CBUFFER_HLSLI

#ifdef USE_IN_CPP
#	define		float4x4		DirectX::XMFLOAT4X4
#	define		float4			DirectX::XMFLOAT4
#	define		float3			DirectX::XMFLOAT3
#	define		float2			DirectX::XMFLOAT2
#	define		uint			UINT
#endif

struct SceneCB
{
    float4x4	mtxWorldToProj;
    float4x4	mtxWorldToView;
    float4x4    mtxProjToWorld;
    float4x4    mtxViewToWorld;
    float2      screenSize;
};

struct MeshCB
{
    float4x4	mtxLocalToWorld;
};

#endif // CBUFFER_HLSLI
//  EOF
