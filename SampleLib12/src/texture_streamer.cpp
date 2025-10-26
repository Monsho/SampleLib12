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
		// if not found an empty heap, create new heap.
		if (!heap)
		{
			// when limit size set, check current heap size.
			if (poolLimitSize_ > 0 && currentHeapSize_ >= poolLimitSize_)
			{
				return TextureStreamHeapHandle();
			}

			// create new heap.
			heap = new TextureStreamHeap();
			u32 maxSize = std::max(size, kStreamHeapSizeMax);
			if (!heap->Initialize(pParentDevice_, maxSize, size))
			{
				// failed initialize, delete heap.
				delete heap;
				return TextureStreamHeapHandle();
			}
			currentHeapSize_ += heap->GetHeapSize();
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
	void TextureStreamAllocator::GabageCollect(TextureStreamer* pStreamer)
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
					currentHeapSize_ -= p->GetHeapSize();
					pParentDevice_->KillObject(p);
				}
				else
				{
					it++;
				}
			}
		}

		if (pStreamer && (poolLimitSize_ > 0) && (currentHeapSize_ > poolLimitSize_))
		{
			// get largest size HeapArray.
			HeapArray* pTargetArray = nullptr;
			u32 maxSize = 0;
			for (auto&& heaps : heapMap_)
			{
				if (maxSize < heaps.first && !heaps.second.empty())
				{
					maxSize = heaps.first;
					pTargetArray = &heaps.second;
				}
			}

			// request that resources in one heap upward miplevel. 
			if (pTargetArray)
			{
				auto heap = (*pTargetArray)[0];
				for (auto handle : heap->resourcesInUse_)
				{
					if (!handle.IsValid())
					{
						continue;
					}
					
					pStreamer->RequestStreaming(handle, pStreamer->GetCurrentMaxWidth(handle) / 2);
				}
			}
		}
	}

	//--------
	void TextureStreamAllocator::SetPoolLimitSize(u64 size)
	{
		poolLimitSize_ = size;
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
			auto handle = item.handle;
			auto resSTex = const_cast<ResourceItemStreamingTexture*>(handle.GetItem<ResourceItemStreamingTexture>());
			ResourceItemStreamingTexture::ChangeMiplevel(pDevice_, resSTex, item.targetWidth);

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
	}

	//--------
	void TextureStreamer::RequestStreaming(ResourceHandle handle, u32 targetWidth)
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
	u32 TextureStreamer::GetCurrentMaxWidth(ResourceHandle handle) const
	{
		if (!handle.IsValid())
		{
			return 0;
		}
		if (handle.GetItemBase()->GetTypeID() == ResourceItemTextureBase::kType)
		{
			if (handle.GetItem<ResourceItemTextureBase>()->IsSameSubType(ResourceItemStreamingTexture::kSubType))
			{
				const ResourceItemStreamingTexture* tex = handle.GetItem<ResourceItemStreamingTexture>();
				u32 w, h;
				tex->GetCurrentSize(w, h);
				return w;
			}
		}
		return 0;
	}


} // namespace sl12

//	EOF
