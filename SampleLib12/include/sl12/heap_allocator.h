#pragma once

#include <sl12/util.h>
#include <vector>
#include <mutex>
#include <map>


namespace sl12
{
	class Device;

	struct HeapAllocation
	{
		ID3D12Heap*		pHeap = nullptr;
		u64				offset = 0;
		u64				size = 0;
		u64				alignment = 0;
		u64				requestedSize = 0;
		u64				aliasKey = 0;
		u32				heapIndex = 0xffffffff;

		bool IsValid() const
		{
			return pHeap != nullptr;
		}
	};	// struct HeapAllocation

	class HeapAllocator
	{
	public:
		struct Statistics
		{
			u64		totalSize = 0;
			u64		allocatedSize = 0;
			u64		logicalAllocatedSize = 0;
			u64		freeSize = 0;
			u64		overlappedSize = 0;
			u32		heapCount = 0;
			u32		aliasAllocationCount = 0;
		};

		HeapAllocator()
		{}
		~HeapAllocator();

		bool Initialize(Device* pDev, D3D12_HEAP_FLAGS heapFlags, u64 blockSize);
		HeapAllocation Allocate(const D3D12_RESOURCE_DESC& desc, u64 aliasKey = 0);
		HeapAllocation Allocate(const D3D12_RESOURCE_DESC& desc, u64 aliasKey, u64 aliasSize, u64 aliasAlignment);
		void Free(const HeapAllocation& allocation);
		Statistics GetStatistics() const;
		void Destroy();

	private:
		struct Range
		{
			u64		offset = 0;
			u64		size = 0;
		};

		struct HeapBlock
		{
			ID3D12Heap*			pHeap = nullptr;
			u64					size = 0;
			std::vector<Range>	freeRanges;
		};

		struct AliasAllocation
		{
			HeapAllocation	allocation;
			u64				logicalSize = 0;
			u32				refCount = 0;
		};

	private:
		bool CreateHeap(u64 size, u64 alignment, u32& outIndex);
		bool AllocateFromBlock(u32 index, u64 size, u64 alignment, HeapAllocation& outAllocation);

	private:
		Device*					pDevice_ = nullptr;
		D3D12_HEAP_FLAGS		heapFlags_ = D3D12_HEAP_FLAG_NONE;
		u64						blockSize_ = 64 * 1024 * 1024;
		std::vector<HeapBlock>	heaps_;
		std::map<u64, AliasAllocation>	aliasAllocations_;
		mutable std::mutex				mutex_;
	};	// class HeapAllocator

}	// namespace sl12

//	EOF
