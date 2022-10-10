#pragma once

#include <stdint.h>


namespace sl12
{
	typedef int8_t		s8;
	typedef int16_t		s16;
	typedef int32_t		s32;
	typedef int64_t		s64;
	typedef uint8_t		u8;
	typedef uint16_t	u16;
	typedef uint32_t	u32;
	typedef uint64_t	u64;

	struct ResourceUsage
	{
		enum Bit
		{
			Unknown					= 0x0,
			ConstantBuffer			= 0x1 << 0,
			VertexBuffer			= 0x1 << 1,
			IndexBuffer				= 0x1 << 2,
			ShaderResource			= 0x1 << 3,
			RenderTarget			= 0x1 << 4,
			DepthStencil			= 0x1 << 5,
			UnorderedAccess			= 0x1 << 6,
			AccelerationStructure	= 0x1 << 7,
		};
	};	// struct ResourceUsage

}	// namespace sl12

//	EOF
