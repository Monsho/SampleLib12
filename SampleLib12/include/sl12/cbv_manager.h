#pragma once

#include <list>
#include <map>
#include <mutex>
#include <sl12/ring_buffer.h>
#include <sl12/buffer_suballocator.h>
#include <sl12/unique_handle.h>
#include <sl12/buffer_view.h>


namespace sl12
{
	//----------------
	class CbvInstance
	{
		friend class CbvManager;
		friend class CbvHandle;

	public:
	private:
		CbvInstance(Device* pDev, BufferSuballocAllocator* Allocator, const BufferSuballocInfo& Mem, ConstantBufferView* View, u32 Size);
		~CbvInstance();

	private:
		BufferSuballocAllocator*			pAllocator_;
		BufferSuballocInfo					memInfo_;
		UniqueHandle<ConstantBufferView>	view_;
		u32									allocSize_;
		u8									pendingCount_ = 0;
	};  // class CbvInstance

	
	//----------------
	class CbvHandle
	{
		friend class CbvManager;
		
	public:
		CbvHandle();
		CbvHandle(CbvHandle&& Right);
		CbvHandle& operator=(CbvHandle&& Right);
		~CbvHandle();

		CbvHandle(const CbvHandle&) = delete;
		CbvHandle& operator=(const CbvHandle&) = delete;

		void Reset();

		ConstantBufferView* GetCBV()
		{
			return (pInstance_ != nullptr) ? &pInstance_->view_ : nullptr;
		}
		bool IsValid() const
		{
			return pInstance_ != nullptr;
		}
		
	private:
		CbvHandle(CbvManager* Manager, CbvInstance* Instance)
			: pManager_(Manager), pInstance_(Instance)
		{}

	private:
		CbvManager*		pManager_ = nullptr;
		CbvInstance*	pInstance_ = nullptr;
	};  // class CbvHandle

	
	//----------------
	class CbvManager
	{
		friend class CbvHandle;

		struct CopyRequest
		{
			CopyRingBuffer::Result	copySrc;
			CbvInstance*			copyDst;
		};	// struct CopyRequest
		
	public:
		CbvManager(Device* pDev);
		~CbvManager();

		void BeginNewFrame();
		
		CbvHandle GetResident(size_t size);
		CbvHandle GetTemporal(const void* pData, size_t size);

		void RequestResidentCopy(CbvHandle& Handle, const void* pData, size_t size);
		void ExecuteCopy(CommandList* pCmdList);

	private:
		void ReturnInstance(CbvInstance* Instance);
		
	private:
		Device*     pParentDevice_ = nullptr;

		UniqueHandle<BufferSuballocAllocator>   residentAllocator_;
		UniqueHandle<BufferSuballocAllocator>   temporalAllocator_;
		UniqueHandle<CopyRingBuffer>            ringBuffer_;

		std::map<u32, std::list<CbvInstance*>>	residentUnused_;
		std::map<u32, std::list<CbvInstance*>>	temporalUnused_;
		std::vector<CbvInstance*>				pendingInstances_;

		std::vector<CopyRequest>				copyRequests_;
		
		std::mutex								mutex_;
	};  // class CbvManager

}   // namespace sl12

//  EOF
