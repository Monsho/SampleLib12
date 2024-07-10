#pragma once

#include "sl12/util.h"
#include "sl12/types.h"
#include "sl12/resource_loader.h"


namespace sl12
{
	class TextureStreamAllocator;
	class TextureStreamHeap;
	class ResourceItemStreamingTexture;
	
	static const u32 kStreamHeapSizeMax = 64 * 1024 * 1024; // 64MB
	static const u32 kStreamHeapNoneIndex = 0xffffffff;

	//--------
	class TextureStreamHeapHandle
	{
		friend class TextureStreamAllocator;
		
	public:
		TextureStreamHeapHandle()
		{}
		TextureStreamHeapHandle(const TextureStreamHeapHandle& rhs)
			: pParentAllocator_(rhs.pParentAllocator_)
			, pParentHeap_(rhs.pParentHeap_)
			, heapAllocIndex_(rhs.heapAllocIndex_)
		{}

		bool IsValid() const
		{
			return (pParentAllocator_ != nullptr) && (pParentHeap_ != nullptr);
		}
		ID3D12Heap* GetHeapDep();
		u32 GetTileOffset();

		void Invalidate();

	private:
		TextureStreamHeapHandle(
			TextureStreamAllocator* pAllocator,
			TextureStreamHeap* pHeap,
			u32 index)
			: pParentAllocator_(pAllocator)
			, pParentHeap_(pHeap)
			, heapAllocIndex_(index)
		{}
		
	private:
		TextureStreamAllocator*	pParentAllocator_ = nullptr;
		TextureStreamHeap*		pParentHeap_ = nullptr;
		u32						heapAllocIndex_ = kStreamHeapNoneIndex;
	};	// class TextureStreamHeapHandle
	
	//--------
	class TextureStreamHeap
	{
		friend class TextureStreamAllocator;
		friend class TextureStreamHeapHandle;
		
	public:
		~TextureStreamHeap();

	private:
		TextureStreamHeap()
		{}

		bool Initialize(Device* pDevice, u32 maxSize, u32 allocSize);
		u32 Allocate(ResourceHandle target);
		void Free(u32 Index);

		ID3D12Heap* GetHeapDep()
		{
			return pNativeHeap_;
		}
		u32 GetAllocateSize() const
		{
			return allocateSize_;
		}
		u32 GetUnusedCount() const
		{
			return unusedCount_;
		}
		u32 GetHeapSize() const
		{
			return allocateSize_ * (u32)resourcesInUse_.size();
		}
		bool IsAllocated() const
		{
			return unusedCount_ < (u32)resourcesInUse_.size();
		}
		std::vector<ResourceHandle>& GetResourcesInUse()
		{
			return resourcesInUse_;
		}
		
	private:
		Device*			pParentDevice_ = nullptr;
		ID3D12Heap*		pNativeHeap_ = nullptr;
		u32				allocateSize_;
		u32				unusedCount_;
		std::vector<ResourceHandle>	resourcesInUse_;
	};	// class TextureStreamHeap

	//--------
	class TextureStreamAllocator
	{
		friend class Device;

		using HeapArray = std::vector<TextureStreamHeap*>;
		
	public:
		TextureStreamAllocator(Device* pDev)
			: pParentDevice_(pDev)
			, poolLimitSize_(0)
			, currentHeapSize_(0)
		{}
		~TextureStreamAllocator();
		
		TextureStreamHeapHandle Allocate(ResourceHandle target, u32 size);
		void Free(TextureStreamHeapHandle handle);

		u64 GetCurrentHeapSize() const
		{
			return currentHeapSize_;
		}

		void GabageCollect(class TextureStreamer* pStreamer);
		void SetPoolLimitSize(u64 size);

	private:
		Device*						pParentDevice_ = nullptr;
		std::map<u32, HeapArray>	heapMap_;
		std::mutex					mutex_;
		u64							poolLimitSize_;
		u64							currentHeapSize_;
	};	// class TextureStreamAllocator

	//--------
	class TextureStreamer
	{
		friend class ResourceItemStreamingTexture;
		
	public:
		TextureStreamer()
		{}
		~TextureStreamer();

		bool Initialize(Device* pDevice);
		void Destroy();

		void RequestStreaming(ResourceHandle handle, u32 targetWidth);

		u32 GetCurrentMaxWidth(ResourceHandle handle) const;

	private:
		bool ThreadBody();
		
	private:
		struct RequestItem
		{
			ResourceHandle	handle;
			u32				targetWidth;
		};	// struct RequestItem

		Device*				pDevice_ = nullptr;
		std::atomic<u64>	handleID_ = 0;

		std::mutex				requestMutex_;
		std::mutex				listMutex_;
		std::condition_variable	requestCV_;
		std::thread				loadingThread_;
		std::list<RequestItem>	requestList_;
		bool					isAlive_ = false;
		bool					isLoading_ = false;
	};	// class TextureStreamer
	
} // namespace sl12

//	EOF
