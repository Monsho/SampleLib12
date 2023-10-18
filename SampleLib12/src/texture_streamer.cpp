#include "sl12/texture_streamer.h"

#include "sl12/resource_streaming_texture.h"


namespace sl12
{
	//--------
	bool StreamTextureSetHandle::IsValid() const
	{
		if (!pParentStreamer_)
			return false;
		auto pTexSet = pParentStreamer_->GetTextureSetFromID(id_);
		if (!pTexSet)
			return false;
		return true;
	}

	//--------
	const StreamTextureSet* StreamTextureSetHandle::GetTextureSet() const
	{
		if (!pParentStreamer_)
			return nullptr;
		return pParentStreamer_->GetTextureSetFromID(id_);
	}

	
	//--------
	TextureStreamer::~TextureStreamer()
	{
		Destroy();
	}

	//--------
	bool TextureStreamer::Initialize(Device* pDevice)
	{
		assert(pDevice != nullptr);

		pDevice_ = pDevice;
		handleID_ = 0;
		texSetMap_.clear();

		// create thread.
		std::thread th([&]
		{
			isAlive_ = true;
			while (isAlive_)
			{
				{
					std::unique_lock<std::mutex> lock(requestMutex_);
					requestCV_.wait(lock, [&] { return !requestList_.empty() || !isAlive_; });
				}

				if (!isAlive_)
				{
					break;
				}

				if (!ThreadBody())
				{
					break;
				}
			}
		});
		loadingThread_ = std::move(th);

		return true;
	}

	//--------
	bool TextureStreamer::ThreadBody()
	{
		isLoading_ = true;

		std::list<RequestItem> items;
		{
			std::lock_guard<std::mutex> lock(listMutex_);
			items.swap(requestList_);
		}

		for (auto&& item : items)
		{
			auto texSet = item.handle.GetTextureSet();
			for (auto handle : texSet->handles)
			{
				auto resSTex = const_cast<ResourceItemStreamingTexture*>(handle.GetItem<ResourceItemStreamingTexture>());
				ResourceItemStreamingTexture::ChangeMiplevel(pDevice_, resSTex, item.targetWidth);
			}

			if (!isAlive_)
			{
				return false;
			}
		}

		isLoading_ = false;

		return true;
	}

	//--------
	void TextureStreamer::Destroy()
	{
		isAlive_ = false;
		requestCV_.notify_one();

		if (loadingThread_.joinable())
			loadingThread_.join();

		requestList_.clear();
		texSetMap_.clear();
	}

	//--------
	StreamTextureSetHandle TextureStreamer::RegisterTextureSet(const std::vector<ResourceHandle>& textures)
	{
		auto pNewSet = std::make_unique<StreamTextureSet>();
		for (auto&& tex : textures)
		{
			auto base = tex.GetItem<ResourceItemTextureBase>();
			if (base && base->IsSameSubType(ResourceItemStreamingTexture::kSubType))
			{
				pNewSet->handles.push_back(tex);
			}
		}
		if (pNewSet->handles.empty())
		{
			return StreamTextureSetHandle();
		}
		
		u64 id;
		{
			std::lock_guard<std::mutex> lock(listMutex_);
			auto it = texSetMap_.begin();
			do
			{
				id = handleID_.fetch_add(1);
				it = texSetMap_.find(id);
			} while (it != texSetMap_.end());
			texSetMap_[id] = std::move(pNewSet);
		}

		return StreamTextureSetHandle(this, id);
	}
	
	//--------
	const StreamTextureSet* TextureStreamer::GetTextureSetFromID(u64 id)
	{
		auto it = texSetMap_.find(id);
		if (it == texSetMap_.end())
		{
			return nullptr;
		}
		return it->second.get();
	}

	//--------
	void TextureStreamer::RequestStreaming(StreamTextureSetHandle handle, u32 targetWidth)
	{
		if (!handle.IsValid())
		{
			return;
		}
		
		RequestItem item;
		item.handle = handle;
		item.targetWidth = targetWidth;

		{
			std::lock_guard<std::mutex> lock(listMutex_);
			requestList_.push_back(item);
		}

		std::lock_guard<std::mutex> lock(requestMutex_);
		requestCV_.notify_one();
	}

} // namespace sl12

//	EOF
