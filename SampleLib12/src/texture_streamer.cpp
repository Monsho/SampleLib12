#include "sl12/texture_streamer.h"

#include "sl12/resource_streaming_texture.h"


namespace sl12
{
	//--------
	ID3D12Heap* TextureStreamHeapHandle::GetHeapDep()
	{
		assert(pParentAllocator_ != nullptr);
		assert(pParentHeap_ != nullptr);
		return pParentHeap_->GetHeapDep();
	}

	//--------
	u32 TextureStreamHeapHandle::GetTileOffset()
	{
		assert(pParentAllocator_ != nullptr);
		assert(pParentHeap_ != nullptr);
		u64 byteSize = pParentHeap_->GetAllocateSize() * heapAllocIndex_;
		return (u32)(byteSize / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES);
	}

	//--------
	void TextureStreamHeapHandle::Invalidate()
	{
		if (IsValid())
		{
			pParentAllocator_->Free(*this);
			pParentAllocator_ = nullptr;
			pParentHeap_ = nullptr;
			heapAllocIndex_ = kStreamHeapNoneIndex;
		}
	}

	
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
	TextureStreamHeap::~TextureStreamHeap()
	{
		assert(resourcesInUse_.size() == unusedCount_);
		if (pNativeHeap_)
		{
			pParentDevice_->PendingKill(new ReleaseObjectItem<ID3D12Heap>(pNativeHeap_));
			pNativeHeap_ = nullptr;
		}
	}

	//--------
	bool TextureStreamHeap::Initialize(Device* pDevice, u32 maxSize, u32 allocSize)
	{
		D3D12_HEAP_DESC desc{};
		desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
		desc.SizeInBytes = maxSize;
		desc.Alignment = 0;
		desc.Flags = D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES;
		HRESULT hr = pDevice->GetDeviceDep()->CreateHeap(&desc, IID_PPV_ARGS(&pNativeHeap_));
		if (FAILED(hr))
		{
			return false;
		}

		pParentDevice_ = pDevice;
		allocateSize_ = allocSize;
		unusedCount_ = maxSize / allocSize;
		resourcesInUse_.resize(unusedCount_);
		memset(resourcesInUse_.data(), 0, sizeof(resourcesInUse_[0]) * unusedCount_);
		return true;
	}

	//--------
	u32 TextureStreamHeap::Allocate(ResourceHandle target)
	{
		assert(unusedCount_ > 0);
		for (size_t i = 0; i < resourcesInUse_.size(); i++)
		{
			if (!resourcesInUse_[i].IsValid())
			{
				resourcesInUse_[i] = target;
				unusedCount_--;
				return (u32)i;
			}
		}
		return kStreamHeapNoneIndex;
	}
	
	//--------
	void TextureStreamHeap::Free(u32 Index)
	{
		unusedCount_++;
		resourcesInUse_[Index] = ResourceHandle();
	}


	//--------
	TextureStreamAllocator::~TextureStreamAllocator()
	{
		for (auto&& heaps : heapMap_)
		{
			for (auto heap : heaps.second)
			{
				pParentDevice_->KillObject(heap);
			}
		}
	}
	
	//--------
	TextureStreamHeapHandle TextureStreamAllocator::Allocate(ResourceHandle target, u32 size)
	{
		std::lock_guard<std::mutex> lock(mutex_);

		if (heapMap_.find(size) == heapMap_.end())
		{
			heapMap_[size] = HeapArray();
		}

		// find or create heap.
		HeapArray& harray = heapMap_[size];
		TextureStreamHeap* heap = nullptr;
		for (auto h : harray)
		{
			if (h->GetUnusedCount() > 0)
			{
				heap = h;
				break;
			}
		}
		if (!heap)
		{
			heap = new TextureStreamHeap();
			u32 maxSize = std::max(size, kStreamHeapSizeMax);
			if (!heap->Initialize(pParentDevice_, maxSize, size))
			{
				delete heap;
				return TextureStreamHeapHandle();
			}
			harray.push_back(heap);
		}

		// allocate.
		u32 index = heap->Allocate(target);
		if (index == kStreamHeapNoneIndex)
		{
			return TextureStreamHeapHandle();
		}
		return TextureStreamHeapHandle(this, heap, index);
	}
	
	//--------
	void TextureStreamAllocator::Free(TextureStreamHeapHandle handle)
	{
		std::lock_guard<std::mutex> lock(mutex_);

		assert(handle.pParentAllocator_ == this);
		handle.pParentHeap_->Free(handle.heapAllocIndex_);
	}

	//--------
	u64 TextureStreamAllocator::GetAllHeapSize() const
	{
		u64 ret = 0;
		for (auto&& heaps : heapMap_)
		{
			for (auto&& heap : heaps.second)
			{
				ret += (u64)heap->GetHeapSize();
			}
		}
		return ret;
	}

	//--------
	void TextureStreamAllocator::GabageCollect()
	{
		std::lock_guard<std::mutex> lock(mutex_);

		for (auto&& heaps : heapMap_)
		{
			auto it = heaps.second.begin();
			while (it != heaps.second.end())
			{
				if (!(*it)->IsAllocated())
				{
					auto p = (*it);
					it = heaps.second.erase(it);
					pParentDevice_->KillObject(p);
				}
				else
				{
					it++;
				}
			}
		}
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
	const StreamTextureSet* TextureStreamer::GetTextureSetFromID(u64 id) const
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
			auto it = std::find_if(requestList_.begin(), requestList_.end(), [handle](const RequestItem& rhs){ return rhs.handle == handle; });
			if (it == requestList_.end())
			{
				requestList_.push_back(item);
			}
			else if (it->targetWidth > targetWidth)
			{
				it->targetWidth = targetWidth;
			}
		}

		std::lock_guard<std::mutex> lock(requestMutex_);
		requestCV_.notify_one();
	}

	//--------
	u32 TextureStreamer::GetCurrentMaxWidth(StreamTextureSetHandle handle) const
	{
		auto set = GetTextureSetFromID(handle.id_);
		if (set)
		{
			u32 width = 0;
			for (auto han : set->handles)
			{
				const ResourceItemStreamingTexture* tex = han.GetItem<ResourceItemStreamingTexture>();
				u32 w, h;
				tex->GetCurrentSize(w, h);
				width = std::max(width, w);
			}
			return width;
		}
		return 0;
	}


} // namespace sl12

//	EOF
