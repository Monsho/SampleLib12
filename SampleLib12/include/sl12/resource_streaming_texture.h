#pragma once

#include "sl12/unique_handle.h"
#include "sl12/types.h"
#include "sl12/resource_loader.h"
#include "sl12/resource_texture_base.h"
#include "sl12/streaming_texture_format.h"
#include "sl12/texture.h"
#include "sl12/texture_view.h"


namespace sl12
{

	class ResourceItemStreamingTexture
		: public ResourceItemTextureBase
	{
	public:
		static const u32 kSubType = TYPE_FOURCC("STEX");

		~ResourceItemStreamingTexture();

		Texture& GetTexture() override
		{
			assert(currTexture_.IsValid());
			return *(&currTexture_);
		}
		const Texture& GetTexture() const override
		{
			assert(currTexture_.IsValid());
			return *(&currTexture_);
		}
		TextureView& GetTextureView() override
		{
			assert(currTextureView_.IsValid());
			return *(&currTextureView_);
		}
		const TextureView& GetTextureView() const override
		{
			assert(currTextureView_.IsValid());
			return *(&currTextureView_);
		}


		static ResourceItemBase* LoadFunction(ResourceLoader* pLoader, ResourceHandle handle, const std::string& filepath);
		static bool ChangeMiplevel(Device* pDevice, ResourceItemStreamingTexture* pSTex, u32 nextWidth);

	private:
		ResourceItemStreamingTexture()
			: ResourceItemTextureBase(kSubType)
		{}

		u32 CalcMipLevel(u32 nextWidth);

	private:
		StreamingTextureHeader		streamingHeader_;
		UniqueHandle<Texture>		currTexture_;
		UniqueHandle<TextureView>	currTextureView_;
		u32							currMiplevel_;
	};	// class ResourceItemStreamingTexture

}	// namespace sl12


//	EOF
