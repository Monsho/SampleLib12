#pragma once

#include <vector>
#include <sl12/unique_handle.h>

#include "buffer.h"


namespace sl12
{
    class Buffer;
    
    //----------------
    class BufferSuballocator
    {
        friend class BufferSuballocAllocator;
        friend class BufferSuballocInfo;

    private:
        struct Chunk
        {
            u32		head = 0;
            u32		count = 0;

            Chunk() {}
            Chunk(u32 h, u32 c)
                : head(h), count(c)
            {}
        };	//struct Chunk

    public:
        BufferSuballocator(
            Device* pDev,
            size_t BlockSize,
            size_t NeedSize,
            BufferHeap::Type HeapType,
            u32 Usage,
            D3D12_RESOURCE_STATES InitState);
        ~BufferSuballocator();

    private:
        bool Alloc(size_t size, D3D12_GPU_VIRTUAL_ADDRESS& address);
        void Free(D3D12_GPU_VIRTUAL_ADDRESS address, size_t size);

        Buffer* GetBuffer()
        {
            return &pBuffer_;
        }
        size_t GetOffset(D3D12_GPU_VIRTUAL_ADDRESS address);

    private:
        UniqueHandle<Buffer>        pBuffer_;
        size_t						totalSize_ = 0;
        size_t                      blockSize_ = 0;
        u32							totalBlockCount_ = 0;
        D3D12_GPU_VIRTUAL_ADDRESS	headAddress_;

        std::list<Chunk>			unusedChunks_;
    };	// class BufferSuballocator

    //----------------
    class BufferSuballocInfo
    {
        friend class BufferSuballocAllocator;

    public:
        BufferSuballocInfo()
        {}
        BufferSuballocInfo(const BufferSuballocInfo& t)
            : address_(t.address_), size_(t.size_), pSuballocator_(t.pSuballocator_)
        {}

        BufferSuballocInfo& operator=(const BufferSuballocInfo& t)
        {
            address_ = t.address_;
            size_ = t.size_;
            pSuballocator_ = t.pSuballocator_;
            return *this;
        }

        Buffer* GetBuffer()
        {
            return pSuballocator_ ? pSuballocator_->GetBuffer() : nullptr;
        }
        size_t GetOffset()
        {
            return pSuballocator_ ? pSuballocator_->GetOffset(address_) : 0;
        }

    private:
        BufferSuballocInfo(D3D12_GPU_VIRTUAL_ADDRESS a, size_t s, BufferSuballocator* p)
            : address_(a), size_(s), pSuballocator_(p)
        {}

    private:
        D3D12_GPU_VIRTUAL_ADDRESS	address_ = 0;
        size_t						size_ = 0;
        BufferSuballocator*		    pSuballocator_ = nullptr;
    };	// class BufferSuballocInfo

    //----------------
    class BufferSuballocAllocator
    {
    public:
        BufferSuballocAllocator(
            Device* pDev,
            size_t BlockSize,
            BufferHeap::Type HeapType,
            u32 Usage,
            D3D12_RESOURCE_STATES InitState);
        ~BufferSuballocAllocator();

        BufferSuballocInfo Alloc(size_t size);
        void Free(BufferSuballocInfo& info);

    private:
        Device*		            pDevice_ = nullptr;
        size_t                  blockSize_ = 0;
        BufferHeap::Type        heapType_ = BufferHeap::Default;
        u32                     usage_ = 0;
        D3D12_RESOURCE_STATES   initState_ = D3D12_RESOURCE_STATE_GENERIC_READ;
        std::vector<UniqueHandle<BufferSuballocator>>	suballocators_;
    };	// class BufferSuballocAllocator
    

}   // namespace sl12

//  EOF
