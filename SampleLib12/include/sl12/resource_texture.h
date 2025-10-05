#pragma once

#include "sl12/types.h"
#include "sl12/resource_loader.h"
#include "sl12/resource_texture_base.h"
#include "sl12/texture.h"
#include "sl12/texture_view.h"


namespace sl12
{

	class ResourceItemTexture
		: public ResourceItemTextureBase
	{
	public:
		static const u32 kSubType = TYPE_FOURCC("RTEX");

		~ResourceItemTexture();

		Texture& GetTexture() override
		{
			return texture_;
		}
		const Texture& GetTexture() const override
		{
			return texture_;
		}
		TextureView& GetTextureView() override
		{
			return textureView_;
		}
		const TextureView& GetTextureView() const override
		{
			return textureView_;
		}
		bool IsViewValid() const override
		{
			return true;
		}


		static ResourceItemBase* LoadFunction(ResourceLoader* pLoader, ResourceHandle handle, const std::string& filepath);

	private:
		ResourceItemTexture(ResourceHandle handle)
			: ResourceItemTextureBase(handle, kSubType)
		{}

	private:
		Texture			texture_;
		TextureView		textureView_;
	};	// class ResourceItemTexture

}	// namespace sl12


//	EOF
