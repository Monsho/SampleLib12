#include <sl12/cbv_manager.h>
#include <algorithm>
#include <sl12/command_list.h>


namespace sl12
{
	//----------------
	//----
	CbvInstance::CbvInstance(Device* pDev, BufferSuballocAllocator* Allocator, const BufferSuballocInfo& Mem, ConstantBufferView* View, u32 Size)
		: pAllocator_(Allocator), memInfo_(Mem), view_(View, pDev), allocSize_(Size)
	{}

	//----
	CbvInstance::~CbvInstance()
	{
		if (pAllocator_)
		{
			pAllocator_->Free(memInfo_);
			view_.Reset();
		}
	}


	//----------------
	//----
	CbvHandle::CbvHandle()
		: pManager_(nullptr), pInstance_(nullptr)
	{}
	
	//----
	CbvHandle::CbvHandle(CbvHandle&& Right)
	{
		Reset();

		pManager_  = Right.pManager_;
		pInstance_ = Right.pInstance_;
		Right.pManager_ = nullptr;
		Right.pInstance_ = nullptr;
	}
	
	//----
	CbvHandle& CbvHandle::operator=(CbvHandle&& Right)
	{
		if (this != &Right)
		{
			Reset();

			pManager_  = Right.pManager_;
			pInstance_ = Right.pInstance_;
			Right.pManager_ = nullptr;
			Right.pInstance_ = nullptr;
		}
		return *this;
	}
	
	//----
	CbvHandle::~CbvHandle()
	{
		Reset();
	}

	//----
	void CbvHandle::Reset()
	{
		if (pManager_ && pInstance_)
		{
			pManager_->ReturnInstance(pInstance_);
			pManager_ = nullptr;
			pInstance_ = nullptr;
		}
	}


	//----------------
	//----
	CbvManager::CbvManager(Device* pDev)
		: pParentDevice_(pDev)
	{
		const size_t kBlockSize = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
		residentAllocator_ = MakeUnique<BufferSuballocAllocator>(nullptr, pDev, kBlockSize, BufferHeap::Default, ResourceUsage::ConstantBuffer, D3D12_RESOURCE_STATE_COMMON);
		temporalAllocator_ = MakeUnique<BufferSuballocAllocator>(nullptr, pDev, kBlockSize, BufferHeap::Dynamic, ResourceUsage::ConstantBuffer, D3D12_RESOURCE_STATE_GENERIC_READ);
		ringBuffer_ = MakeUnique<CopyRingBuffer>(nullptr, pDev);
	}

	//----
	CbvManager::~CbvManager()
	{
		for (auto&& it : pendingInstances_)
		{
			auto p = it;
			delete p;
		}
		for (auto&& mIt : residentUnused_)
		{
			for (auto&& lIt : mIt.second)
			{
				auto p = lIt;
				delete p;
			}
		}
		for (auto&& mIt : temporalUnused_)
		{
			for (auto&& lIt : mIt.second)
			{
				auto p = lIt;
				delete p;
			}
		}
		residentUnused_.clear();
		temporalUnused_.clear();
		
		residentAllocator_.Reset();
		temporalAllocator_.Reset();
		ringBuffer_.Reset();
	}

	//----
	CbvHandle CbvManager::GetResident(size_t size)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		
		const size_t kBlockSize = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
		u32 allocSize = (u32)GetAlignedSize(size, kBlockSize);

		// find unused list.
		if (residentUnused_.find(allocSize) != residentUnused_.end())
		{
			if (!residentUnused_[allocSize].empty())
			{
				auto It = residentUnused_[allocSize].begin();
				auto pInst = *It;
				residentUnused_[allocSize].erase(It);
				return CbvHandle(this, pInst);
			}
		}

		// create new instance.
		auto info = residentAllocator_->Alloc(allocSize);
		auto cbv = new ConstantBufferView();
		bool isCbvInit = cbv->Initialize(pParentDevice_, info.GetBuffer(), info.GetOffset(), allocSize);
		assert(isCbvInit);
		
		CbvInstance* pInst = new CbvInstance(pParentDevice_, &residentAllocator_, info, cbv, allocSize);
		return CbvHandle(this, pInst);
	}

	//----
	CbvHandle CbvManager::GetTemporal(const void* pData, size_t size)
	{
		std::lock_guard<std::mutex> lock(mutex_);

		const size_t kBlockSize = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
		u32 allocSize = (u32)GetAlignedSize(size, kBlockSize);

		CbvInstance* pInst = nullptr;
		
		// find unused list.
		if (temporalUnused_.find(allocSize) != temporalUnused_.end())
		{
			if (!temporalUnused_[allocSize].empty())
			{
				auto It = temporalUnused_[allocSize].begin();
				pInst = *It;
				temporalUnused_[allocSize].erase(It);
			}
		}

		// create new instance.
		if (!pInst)
		{
			auto info = temporalAllocator_->Alloc(allocSize);
			auto cbv = new ConstantBufferView();
			bool isCbvInit = cbv->Initialize(pParentDevice_, info.GetBuffer(), info.GetOffset(), allocSize);
			assert(isCbvInit);
			pInst = new CbvInstance(pParentDevice_, &temporalAllocator_, info, cbv, allocSize);
		}

		// copy data.
		if (pInst)
		{
			D3D12_RANGE range{};
			range.Begin = pInst->memInfo_.GetOffset();
			range.End = range.Begin + size;
			void* p = pInst->memInfo_.GetBuffer()->Map(range);
			memcpy(p, pData, size);
			pInst->memInfo_.GetBuffer()->Unmap(range);
		}
		
		return CbvHandle(this, pInst);
	}

	//----
	void CbvManager::ReturnInstance(CbvInstance* Instance)
	{
		assert(Instance != nullptr);

		Instance->pendingCount_ = 2;

		std::lock_guard<std::mutex> lock(mutex_);
		pendingInstances_.push_back(Instance);
	}

	//----
	void CbvManager::BeginNewFrame()
	{
		std::lock_guard<std::mutex> lock(mutex_);

		std::vector<CbvInstance*> tmp;
		tmp.swap(pendingInstances_);
		for (auto&& inst : tmp)
		{
			if (inst->pendingCount_ > 0)
			{
				inst->pendingCount_--;
				pendingInstances_.push_back(inst);
				continue;
			}
			
			if (inst->pAllocator_ == &residentAllocator_)
			{
				if (residentUnused_.find(inst->allocSize_) == residentUnused_.end())
				{
					residentUnused_[inst->allocSize_] = std::list<CbvInstance*>();
				}
				residentUnused_[inst->allocSize_].push_back(inst);
			}
			else
			{
				assert(inst->pAllocator_ == &temporalAllocator_);

				if (temporalUnused_.find(inst->allocSize_) == temporalUnused_.end())
				{
					temporalUnused_[inst->allocSize_] = std::list<CbvInstance*>();
				}
				temporalUnused_[inst->allocSize_].push_back(inst);
			}
		}

		ringBuffer_->BeginNewFrame();
		copyRequests_.clear();
	}

	//----
	void CbvManager::RequestResidentCopy(CbvHandle& Handle, const void* pData, size_t size)
	{
		if (Handle.pInstance_->pAllocator_ != &residentAllocator_)
		{
			return;
		}

		CopyRequest req{};
		req.copySrc = ringBuffer_->CopyToRing(pData, (u32)size);
		req.copyDst = Handle.pInstance_;

		std::lock_guard<std::mutex> lock(mutex_);
		copyRequests_.push_back(req);
	}

	//----
	void CbvManager::ExecuteCopy(CommandList* pCmdList, bool bTransition)
	{
		std::lock_guard<std::mutex> lock(mutex_);

		std::sort(copyRequests_.begin(), copyRequests_.end(),
			[](const CopyRequest& l, const CopyRequest& r) { return l.copyDst->memInfo_.GetBuffer() < r.copyDst->memInfo_.GetBuffer(); });

		Buffer* prev = nullptr;
		std::vector<Buffer*> transitions;
		transitions.reserve(copyRequests_.size());
		for (auto&& req : copyRequests_)
		{
			if (prev != req.copyDst->memInfo_.GetBuffer() && bTransition)
			{
				prev = req.copyDst->memInfo_.GetBuffer();
				transitions.push_back(prev);

				pCmdList->TransitionBarrier(req.copyDst->memInfo_.GetBuffer(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
			}

			pCmdList->GetLatestCommandList()->CopyBufferRegion(
				req.copyDst->memInfo_.GetBuffer()->GetResourceDep(), req.copyDst->memInfo_.GetOffset(),
				req.copySrc.pBuffer->GetResourceDep(), req.copySrc.offset, req.copySrc.size);
		}

		if (bTransition)
		{
			for (auto&& t : transitions)
			{
				pCmdList->AddTransitionBarrier(t, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
			}
			pCmdList->FlushBarriers();
		}
	}

}   // namespace sl12

//  EOF
