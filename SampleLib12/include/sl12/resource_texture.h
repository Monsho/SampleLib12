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


		static ResourceItemBase* LoadFunction(ResourceLoader* pLoader, ResourceHandle handle, const std::string& filepath);

	private:
		ResourceItemTexture()
			: ResourceItemTextureBase()
		{}

	private:
		Texture			texture_;
		TextureView		textureView_;
	};	// class ResourceItemTexture

}	// namespace sl12


//	EOF
