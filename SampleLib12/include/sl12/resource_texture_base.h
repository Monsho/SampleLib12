#pragma once

#include "sl12/types.h"
#include "sl12/resource_loader.h"


namespace sl12
{
	class Texture;
	class TextureView;

	class ResourceItemTextureBase
		: public ResourceItemBase
	{
	public:
		static const u32 kType = TYPE_FOURCC("TEX_");

		~ResourceItemTextureBase()
		{}

		virtual Texture& GetTexture() = 0;
		virtual const Texture& GetTexture() const = 0;
		virtual TextureView& GetTextureView() = 0;
		virtual const TextureView& GetTextureView() const = 0;

	protected:
		ResourceItemTextureBase()
			: ResourceItemBase(ResourceItemTextureBase::kType)
		{}
	};	// class ResourceItemTextureBase

}	// namespace sl12


//	EOF
