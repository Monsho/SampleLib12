#pragma once

#include <sl12/buffer_suballocator.h>
#include <sl12/buffer.h>


namespace sl12
{
	//----------------
	//----
	BufferSuballocator::BufferSuballocator(
		Device* pDev,
		size_t BlockSize,
		size_t NeedSize,
		BufferHeap::Type HeapType,
		u32 Usage,
		D3D12_RESOURCE_STATES InitState)
	: blockSize_(BlockSize)
	{
		size_t alloc_size = 4 * 1024 * 1024;	// 4MB
		while (NeedSize > alloc_size)
		{
			alloc_size *= 2;
		}

		pBuffer_ = MakeUnique<Buffer>(pDev);

		BufferDesc desc{};
		desc.size = alloc_size;
		desc.heap = HeapType;
		desc.usage = Usage;
		desc.initialState = InitState;

		bool isSucceeded = pBuffer_->Initialize(pDev, desc);
		assert(isSucceeded);

		totalSize_ = alloc_size;
		totalBlockCount_ = (u32)(alloc_size / blockSize_);
		headAddress_ = pBuffer_->GetResourceDep()->GetGPUVirtualAddress();
		unusedChunks_.push_back(Chunk(0, totalBlockCount_));
	}

	//----
	BufferSuballocator::~BufferSuballocator()
	{
		pBuffer_.Reset();
		unusedChunks_.clear();
	}

	//----
	bool BufferSuballocator::Alloc(size_t size, D3D12_GPU_VIRTUAL_ADDRESS& address)
	{
		u32 block_count = (u32)((size + blockSize_ - 1) / blockSize_);
		if (block_count > totalBlockCount_)
		{
			return false;
		}

		// find continuous blocks.
		for (auto it = unusedChunks_.begin(); it != unusedChunks_.end(); it++)
		{
			if (it->count >= block_count)
			{
				size_t offset = (size_t)it->head * blockSize_;
				address = headAddress_ + offset;
				it->head += block_count;
				it->count -= block_count;
				if (!it->count)
				{
					unusedChunks_.erase(it);
				}
				return true;
			}
		}

		return false;
	}

	//----
	void BufferSuballocator::Free(D3D12_GPU_VIRTUAL_ADDRESS address, size_t size)
	{
		// insert chunk.
		u32 block_count = (u32)((size + blockSize_ - 1) / blockSize_);
		size_t offset = address - headAddress_;
		u32 block_head = (u32)(offset / blockSize_);
		bool is_inserted = false;
		for (auto it = unusedChunks_.begin(); it != unusedChunks_.end(); it++)
		{
			if (it->head > block_head)
			{
				unusedChunks_.insert(it, Chunk(block_head, block_count));
				is_inserted = true;
			}
		}
		if (!is_inserted)
		{
			unusedChunks_.push_back(Chunk(block_head, block_count));
		}

		// joint continuous chunks.
		auto p = unusedChunks_.begin();
		auto c = p;
		c++;
		while (c != unusedChunks_.end())
		{
			if (p->head + p->count == c->head)
			{
				p->count += c->count;
				c = unusedChunks_.erase(c);
			}
			else
			{
				p++; c++;
			}
		}
	}

	//----
	size_t BufferSuballocator::GetOffset(D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		assert(address >= headAddress_);
		return (size_t)(address - headAddress_);
	}


	//----------------
	//----
	BufferSuballocAllocator::BufferSuballocAllocator(
		Device* pDev,
		size_t BlockSize,
		BufferHeap::Type HeapType,
		u32 Usage,
		D3D12_RESOURCE_STATES InitState)
	: pDevice_(pDev)
	, blockSize_(BlockSize)
	, heapType_(HeapType)
	, usage_(Usage)
	, initState_(InitState)
	{}

	//----
	BufferSuballocAllocator::~BufferSuballocAllocator()
	{
		pDevice_ = nullptr;
		suballocators_.clear();
	}

	//----
	BufferSuballocInfo BufferSuballocAllocator::Alloc(size_t size)
	{
		assert(pDevice_ != nullptr);

		// allocate from existed suballocators.
		for (auto it = suballocators_.begin(); it != suballocators_.end(); it++)
		{
			D3D12_GPU_VIRTUAL_ADDRESS address;
			if ((*it)->Alloc(size, address))
			{
				return BufferSuballocInfo(address, size, &(*it));
			}
		}

		// allocate new suballocator.
		auto sub = MakeUnique<BufferSuballocator>(nullptr, pDevice_, blockSize_, size, heapType_, usage_, initState_);
		D3D12_GPU_VIRTUAL_ADDRESS address;
		bool success = sub->Alloc(size, address);
		assert(success);

		BufferSuballocInfo ret(address, size, &sub);
		suballocators_.push_back(std::move(sub));
		return ret;
	}

	//----
	void BufferSuballocAllocator::Free(BufferSuballocInfo& info)
	{
		if (info.pSuballocator_)
		{
			info.pSuballocator_->Free(info.address_, info.size_);
		}
	}

	
}   // namespace sl12

//  EOF
