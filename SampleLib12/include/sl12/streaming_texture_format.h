#pragma once

#include <dxgiformat.h>

#include "sl12/types.h"


namespace sl12
{
	enum class StreamingTextureDimension
	{
		Texture1D,
		Texture2D,
		Texture3D,
		TextureCube
	};	// enum class StreamingTextureDimension
	
	struct StreamingTextureHeader
	{
		StreamingTextureDimension	dimension;
		DXGI_FORMAT					format;
		u32							width, height, depth;
		u32							mipLevels;
		u32							topMipCount;
		u32							tailMipCount;
	};	// struct StreamingTextureHeader

	struct StreamingSubresourceHeader
	{
		u64		offsetFromFileHead;
		u32		width, height;
		u32		rowSize;
		u32		rowCount;
	};	// struct StreamingSubresourceHeader
	
}	// namespace sl12

//	EOF
