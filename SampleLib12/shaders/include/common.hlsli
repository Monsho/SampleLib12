#ifndef COMMON_HLSLI
#define COMMON_HLSLI

#define LANE_COUNT_IN_WAVE	32

float4 RotateGridSuperSample(Texture2D tex, SamplerState sam, float2 uv, float bias)
{
	float2 dx = ddx(uv);
	float2 dy = ddy(uv);

	float2 uvOffsets = float2(0.125, 0.375);
	float2 offsetUV = float2(0.0, 0.0);

	float4 col = 0;
	offsetUV = uv + uvOffsets.x * dx + uvOffsets.y * dy;
	col += tex.SampleBias(sam, offsetUV, bias);
	offsetUV = uv - uvOffsets.x * dx - uvOffsets.y * dy;
	col += tex.SampleBias(sam, offsetUV, bias);
	offsetUV = uv + uvOffsets.y * dx - uvOffsets.x * dy;
	col += tex.SampleBias(sam, offsetUV, bias);
	offsetUV = uv - uvOffsets.y * dx + uvOffsets.x * dy;
	col += tex.SampleBias(sam, offsetUV, bias);
	col *= 0.25;

	return col;
}

// get triangle indices functions.
uint3 GetTriangleIndices2byte(uint offset, ByteAddressBuffer indexBuffer)
{
	uint alignedOffset = offset & ~0x3;
	uint2 indices4 = indexBuffer.Load2(alignedOffset);

	uint3 ret;
	if (alignedOffset == offset)
	{
		ret.x = indices4.x & 0xffff;
		ret.y = (indices4.x >> 16) & 0xffff;
		ret.z = indices4.y & 0xffff;
	}
	else
	{
		ret.x = (indices4.x >> 16) & 0xffff;
		ret.y = indices4.y & 0xffff;
		ret.z = (indices4.y >> 16) & 0xffff;
	}

	return ret;
}

uint3 Get16bitIndices(uint primIdx, ByteAddressBuffer indexBuffer)
{
	uint indexOffset = primIdx * 2 * 3;
	return GetTriangleIndices2byte(indexOffset, indexBuffer);
}

uint3 Get32bitIndices(uint primIdx, ByteAddressBuffer indexBuffer)
{
	uint indexOffset = primIdx * 4 * 3;
	uint3 ret = indexBuffer.Load3(indexOffset);
	return ret;
}

#endif // COMMON_HLSLI
//	EOF
