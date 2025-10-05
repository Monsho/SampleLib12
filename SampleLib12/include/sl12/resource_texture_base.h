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
		virtual bool IsViewValid() const = 0;

		bool IsSameSubType(u32 subID) const
		{
			return subTypeID_ == subID;
		}

	protected:
		ResourceItemTextureBase(ResourceHandle handle, u32 subID)
			: ResourceItemBase(handle, ResourceItemTextureBase::kType)
			, subTypeID_(subID)
		{}

		u32	subTypeID_;
	};	// class ResourceItemTextureBase

}	// namespace sl12


//	EOF
