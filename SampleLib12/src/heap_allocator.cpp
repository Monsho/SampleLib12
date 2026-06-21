#include <sl12/heap_allocator.h>

#include <sl12/device.h>


namespace
{
	sl12::u64 AlignUp(sl12::u64 value, sl12::u64 alignment)
	{
		if (alignment == 0)
		{
			return value;
		}
		return ((value + alignment - 1) / alignment) * alignment;
	}
}

namespace sl12
{
	//----
	HeapAllocator::~HeapAllocator()
	{
		Destroy();
	}

	//----
	bool HeapAllocator::Initialize(Device* pDev, D3D12_HEAP_FLAGS heapFlags, u64 blockSize)
	{
		if (!pDev)
		{
			return false;
		}

		pDevice_ = pDev;
		heapFlags_ = heapFlags;
		blockSize_ = blockSize;
		return true;
	}

	//----
	HeapAllocation HeapAllocator::Allocate(const D3D12_RESOURCE_DESC& desc, u64 aliasKey)
	{
		std::lock_guard<std::mutex> lock(mutex_);

		HeapAllocation ret;
		if (!pDevice_)
		{
			return ret;
		}
		if (aliasKey != 0)
		{
			auto aliasIt = aliasAllocations_.find(aliasKey);
			if (aliasIt != aliasAllocations_.end())
			{
				aliasIt->second.refCount++;
				return aliasIt->second.allocation;
			}
		}

		auto info = pDevice_->GetDeviceDep()->GetResourceAllocationInfo(0, 1, &desc);
		if (info.SizeInBytes == 0 || info.SizeInBytes == UINT64_MAX)
		{
			return ret;
		}

		u64 alignment = info.Alignment != 0 ? info.Alignment : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		u64 size = AlignUp(info.SizeInBytes, alignment);

		for (u32 i = 0; i < heaps_.size(); i++)
		{
			if (AllocateFromBlock(i, size, alignment, ret))
			{
				ret.aliasKey = aliasKey;
				if (aliasKey != 0)
				{
					aliasAllocations_[aliasKey] = AliasAllocation{ ret, 1 };
				}
				return ret;
			}
		}

		u32 heapIndex = 0xffffffff;
		if (!CreateHeap(std::max(blockSize_, size), alignment, heapIndex))
		{
			return HeapAllocation();
		}
		AllocateFromBlock(heapIndex, size, alignment, ret);
		ret.aliasKey = aliasKey;
		if (aliasKey != 0)
		{
			aliasAllocations_[aliasKey] = AliasAllocation{ ret, 1 };
		}
		return ret;
	}

	//----
	void HeapAllocator::Free(const HeapAllocation& allocation)
	{
		std::lock_guard<std::mutex> lock(mutex_);

		if (!allocation.IsValid() || allocation.heapIndex >= heaps_.size())
		{
			return;
		}
		if (allocation.aliasKey != 0)
		{
			auto aliasIt = aliasAllocations_.find(allocation.aliasKey);
			if (aliasIt == aliasAllocations_.end())
			{
				return;
			}
			if (--aliasIt->second.refCount > 0)
			{
				return;
			}
			aliasAllocations_.erase(aliasIt);
		}

		HeapBlock& heap = heaps_[allocation.heapIndex];
		Range newRange{ allocation.offset, allocation.size };
		auto it = heap.freeRanges.begin();
		for (; it != heap.freeRanges.end(); ++it)
		{
			if (newRange.offset < it->offset)
			{
				break;
			}
		}
		it = heap.freeRanges.insert(it, newRange);

		if (it != heap.freeRanges.begin())
		{
			auto prev = it - 1;
			if (prev->offset + prev->size == it->offset)
			{
				prev->size += it->size;
				it = heap.freeRanges.erase(it);
				it = prev;
			}
		}
		if ((it + 1) != heap.freeRanges.end())
		{
			auto next = it + 1;
			if (it->offset + it->size == next->offset)
			{
				it->size += next->size;
				heap.freeRanges.erase(next);
			}
		}
	}

	//----
	void HeapAllocator::Destroy()
	{
		std::lock_guard<std::mutex> lock(mutex_);

		for (auto&& heap : heaps_)
		{
			SafeRelease(heap.pHeap);
		}
		aliasAllocations_.clear();
		heaps_.clear();
	}

	//----
	bool HeapAllocator::CreateHeap(u64 size, u64 alignment, u32& outIndex)
	{
		D3D12_HEAP_DESC desc{};
		desc.SizeInBytes = AlignUp(size, alignment);
		desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
		desc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		desc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		desc.Properties.CreationNodeMask = 1;
		desc.Properties.VisibleNodeMask = 1;
		desc.Alignment = alignment;
		desc.Flags = heapFlags_;

		HeapBlock block;
		HRESULT hr = pDevice_->GetDeviceDep()->CreateHeap(&desc, IID_PPV_ARGS(&block.pHeap));
		if (FAILED(hr))
		{
			return false;
		}

		block.size = desc.SizeInBytes;
		block.freeRanges.push_back(Range{ 0, block.size });
		heaps_.push_back(block);
		outIndex = (u32)(heaps_.size() - 1);
		return true;
	}

	//----
	bool HeapAllocator::AllocateFromBlock(u32 index, u64 size, u64 alignment, HeapAllocation& outAllocation)
	{
		HeapBlock& heap = heaps_[index];
		for (auto it = heap.freeRanges.begin(); it != heap.freeRanges.end(); ++it)
		{
			u64 alignedOffset = AlignUp(it->offset, alignment);
			u64 padding = alignedOffset - it->offset;
			if (it->size < padding + size)
			{
				continue;
			}

			u64 endOffset = alignedOffset + size;
			u64 rangeEnd = it->offset + it->size;
			Range before{ it->offset, padding };
			Range after{ endOffset, rangeEnd - endOffset };

			it = heap.freeRanges.erase(it);
			if (after.size > 0)
			{
				it = heap.freeRanges.insert(it, after);
			}
			if (before.size > 0)
			{
				heap.freeRanges.insert(it, before);
			}

			outAllocation.pHeap = heap.pHeap;
			outAllocation.offset = alignedOffset;
			outAllocation.size = size;
			outAllocation.alignment = alignment;
			outAllocation.heapIndex = index;
			return true;
		}
		return false;
	}

}	// namespace sl12

//	EOF
