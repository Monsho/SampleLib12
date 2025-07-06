#include <sl12/render_graph.h>

#include <set>

#include "sl12/buffer.h"
#include "sl12/buffer_view.h"
#include "sl12/command_list.h"
#include "sl12/command_queue.h"


namespace
{
	enum class EOverlapResult
	{
		Overlapped,
		LHS_Before_RHS,
		LHS_After_RHS,
	};

	EOverlapResult TestOverlap(const sl12::CrossQueueDepsType& CrossQueueDeps, const sl12::TransientResourceLifespan& lhs, const sl12::TransientResourceLifespan& rhs)
	{
		bool before = true, after = true;
		for (size_t q = 0; q < sl12::HardwareQueue::Max; q++)
		{
			before &= CrossQueueDeps[rhs.first][q] >= lhs.last[q];
			after &= CrossQueueDeps[lhs.first][q] >= rhs.last[q];
		}
		return before ? EOverlapResult::LHS_Before_RHS : (after ? EOverlapResult::LHS_After_RHS : EOverlapResult::Overlapped);
	}

	sl12::u32 StateToUsage(sl12::TransientState::Value state)
	{
		static const sl12::u32 kUsages[] = {
			0,										// Common
			sl12::ResourceUsage::RenderTarget,		// RenderTarget
			sl12::ResourceUsage::DepthStencil,		// DepthStencil
			sl12::ResourceUsage::ShaderResource,	// ShaderResource
			sl12::ResourceUsage::UnorderedAccess,	// UnorderedAccess
			0,										// IndirectArgument
			0,										// CopySrc
			0,										// CopyDst
			0,										// Present
		};
		return kUsages[state];
	}

	D3D12_RESOURCE_STATES StateToD3D12State(sl12::TransientState::Value state)
	{
		static const D3D12_RESOURCE_STATES kD3D12States[] = {
			D3D12_RESOURCE_STATE_COMMON,				// Common
			D3D12_RESOURCE_STATE_RENDER_TARGET,			// RenderTarget
			D3D12_RESOURCE_STATE_DEPTH_WRITE,			// DepthStencil
			D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,	// ShaderResource
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,		// UnorderedAccess
			D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,		// IndirectArgument
			D3D12_RESOURCE_STATE_COPY_SOURCE,			// CopySrc
			D3D12_RESOURCE_STATE_COPY_DEST,				// CopyDst
			D3D12_RESOURCE_STATE_PRESENT,				// Present
		};
		return kD3D12States[state];
	}

	sl12::u16 NodeID2PassNo(const std::vector<sl12::RenderPassID>& sortedNodeIDs, sl12::RenderPassID nodeID)
	{
		auto dist = std::distance(sortedNodeIDs.begin(), std::find(sortedNodeIDs.begin(), sortedNodeIDs.end(), nodeID));
		assert(dist >= 0);
		return (sl12::u16)(dist + 1);
	};

	sl12::RenderPassID PassNo2NodeID(const std::vector<sl12::RenderPassID>& sortedNodeIDs, sl12::u16 passNo)
	{
		return sortedNodeIDs[passNo - 1];
	};
}

namespace sl12
{
	TransientResourceManager::~TransientResourceManager()
	{
		committedResources_.clear();
		unusedResources_.clear();
	}

	void TransientResourceManager::AddExternalTexture(TransientResourceID id, Texture* pTexture, TransientState::Value state)
	{
		RDGExternalResourceInstance inst;
		inst.bIsTexture = true;
		inst.pTexture = pTexture;
		inst.state = state;
		externalResources_[id] = inst;
	}
	
	void TransientResourceManager::AddExternalBuffer(TransientResourceID id, Buffer* pBuffer, TransientState::Value state)
	{
		RDGExternalResourceInstance inst;
		inst.bIsTexture = false;
		inst.pBuffer = pBuffer;
		inst.state = state;
		externalResources_[id] = inst;
	}

	RenderGraphResource* TransientResourceManager::GetRenderGraphResource(TransientResourceID id)
	{
		auto it = graphResources_.find(id);
		if (it == graphResources_.end())
		{
			return nullptr;
		}
		return &it->second;
	}

	RenderGraphResource* TransientResourceManager::CreatePassOnlyResource(const TransientResourceDesc& desc)
	{
		std::lock_guard<std::mutex> lock(passOnlyMutex_);
		
		// search in unused list.
		auto find_it = unusedResources_.find(desc);
		if (find_it != unusedResources_.end())
		{
			// use cached resource instance.
			RDGPassOnlyResource passOnly;
			passOnly.desc = desc;
			passOnly.instance = std::move(find_it->second);
			passOnly.graphResource = std::make_unique<RenderGraphResource>();
			passOnly.graphResource->bIsTexture = desc.bIsTexture;
			if (desc.bIsTexture)
			{
				passOnly.graphResource->pTexture = &passOnly.instance->texture;
			}
			else
			{
				passOnly.graphResource->pBuffer = &passOnly.instance->buffer;
			}
			unusedResources_.erase(find_it);
			passOnlyResources_.push_back(std::move(passOnly));
			return passOnlyResources_[passOnlyResources_.size() - 1].graphResource.get();
		}

		// create new resource instance.
		std::unique_ptr<RDGTransientResourceInstance> res = std::make_unique<RDGTransientResourceInstance>();
		res->desc = desc;
		res->state = TransientState::Common;
		if (desc.bIsTexture)
		{
			// create new texture.
			res->texture = MakeUnique<Texture>(pDevice_);
			if (!res->texture->Initialize(pDevice_, desc.textureDesc))
			{
				ConsolePrint("Error : Can NOT create transient texture.");
				assert(false);
			}
		}
		else
		{
			// create new buffer.
			res->buffer = MakeUnique<Buffer>(pDevice_);
			if (!res->buffer->Initialize(pDevice_, desc.bufferDesc))
			{
				ConsolePrint("Error : Can NOT create transient buffer.");
				assert(false);
			}
		}

		// store pass only resource.
		RDGPassOnlyResource passOnly;
		passOnly.desc = desc;
		passOnly.instance = std::move(res);
		passOnly.graphResource = std::make_unique<RenderGraphResource>();
		passOnly.graphResource->bIsTexture = desc.bIsTexture;
		if (desc.bIsTexture)
		{
			passOnly.graphResource->pTexture = &passOnly.instance->texture;
		}
		else
		{
			passOnly.graphResource->pBuffer = &passOnly.instance->buffer;
		}
		passOnlyResources_.push_back(std::move(passOnly));
		return passOnlyResources_[passOnlyResources_.size() - 1].graphResource.get();
	}
	
	TextureView* TransientResourceManager::CreateOrGetTextureView(RenderGraphResource* pResource, u32 firstMip, u32 mipCount, u32 firstArray, u32 arraySize)
	{
		if (!pResource->bIsTexture)
		{
			return nullptr;
		}

		std::lock_guard<std::mutex> lock(viewMutex_);
		
		// find cache.
		RDGTextureViewDesc desc{ firstMip, mipCount, firstArray, arraySize };
		auto ret = viewInstances_.equal_range(pResource->pTexture);
		for (auto it = ret.first; it != ret.second; ++it)
		{
			if (it->second->type == RDGResourceViewType::Texture)
			{
				if (it->second->textureDesc == desc)
				{
					it->second->unusedFrame_ = 0;
					return &(it->second->texture);
				}
			}
		}

		// create new instance.
		std::unique_ptr<RDGResourceViewInstance> inst = std::make_unique<RDGResourceViewInstance>();
		inst->type = RDGResourceViewType::Texture;
		inst->textureDesc = desc;
		inst->texture = MakeUnique<TextureView>(pDevice_);
		bool bTextureViewInitSuccess = inst->texture->Initialize(pDevice_, pResource->pTexture, firstMip, mipCount, firstArray, arraySize);
		assert(bTextureViewInitSuccess);

		auto view = &inst->texture;
		viewInstances_.insert(std::make_pair(pResource->pTexture, std::move(inst)));
		return view;
	}
	
	BufferView* TransientResourceManager::CreateOrGetBufferView(RenderGraphResource* pResource, u32 firstElement, u32 numElement, u32 stride)
	{
		if (pResource->bIsTexture)
		{
			return nullptr;
		}

		std::lock_guard<std::mutex> lock(viewMutex_);

		// find cache.
		RDGBufferViewDesc desc{ firstElement, numElement, stride, 0 };
		auto ret = viewInstances_.equal_range(pResource->pTexture);
		for (auto it = ret.first; it != ret.second; ++it)
		{
			if (it->second->type == RDGResourceViewType::Buffer)
			{
				if (it->second->bufferDesc == desc)
				{
					it->second->unusedFrame_ = 0;
					return &(it->second->buffer);
				}
			}
		}

		// create new instance.
		std::unique_ptr<RDGResourceViewInstance> inst = std::make_unique<RDGResourceViewInstance>();
		inst->type = RDGResourceViewType::Buffer;
		inst->bufferDesc = desc;
		inst->buffer = MakeUnique<BufferView>(pDevice_);
		bool bTextureViewInitSuccess = inst->buffer->Initialize(pDevice_, pResource->pBuffer, firstElement, numElement, stride);
		assert(bTextureViewInitSuccess);

		auto view = &inst->buffer;
		viewInstances_.insert(std::make_pair(pResource->pBuffer, std::move(inst)));
		return view;
	}
	
	RenderTargetView* TransientResourceManager::CreateOrGetRenderTargetView(RenderGraphResource* pResource, u32 mipSlice, u32 firstArray, u32 arraySize)
	{
		if (!pResource->bIsTexture)
		{
			return nullptr;
		}

		std::lock_guard<std::mutex> lock(viewMutex_);

		// find cache.
		RDGTextureViewDesc desc{ mipSlice, 0, firstArray, arraySize };
		auto ret = viewInstances_.equal_range(pResource->pTexture);
		for (auto it = ret.first; it != ret.second; ++it)
		{
			if (it->second->type == RDGResourceViewType::RenderTarget)
			{
				if (it->second->textureDesc == desc)
				{
					it->second->unusedFrame_ = 0;
					return &(it->second->rtv);
				}
			}
		}

		// create new instance.
		std::unique_ptr<RDGResourceViewInstance> inst = std::make_unique<RDGResourceViewInstance>();
		inst->type = RDGResourceViewType::RenderTarget;
		inst->textureDesc = desc;
		inst->rtv = MakeUnique<RenderTargetView>(pDevice_);
		bool bTextureViewInitSuccess = inst->rtv->Initialize(pDevice_, pResource->pTexture, mipSlice, firstArray, arraySize);
		assert(bTextureViewInitSuccess);

		auto view = &inst->rtv;
		viewInstances_.insert(std::make_pair(pResource->pTexture, std::move(inst)));
		return view;
	}
	
	DepthStencilView* TransientResourceManager::CreateOrGetDepthStencilView(RenderGraphResource* pResource, u32 mipSlice, u32 firstArray, u32 arraySize)
	{
		if (!pResource->bIsTexture)
		{
			return nullptr;
		}

		std::lock_guard<std::mutex> lock(viewMutex_);

		// find cache.
		RDGTextureViewDesc desc{ mipSlice, 0, firstArray, arraySize };
		auto ret = viewInstances_.equal_range(pResource->pTexture);
		for (auto it = ret.first; it != ret.second; ++it)
		{
			if (it->second->type == RDGResourceViewType::DepthStencil)
			{
				if (it->second->textureDesc == desc)
				{
					it->second->unusedFrame_ = 0;
					return &(it->second->dsv);
				}
			}
		}

		// create new instance.
		std::unique_ptr<RDGResourceViewInstance> inst = std::make_unique<RDGResourceViewInstance>();
		inst->type = RDGResourceViewType::DepthStencil;
		inst->textureDesc = desc;
		inst->dsv = MakeUnique<DepthStencilView>(pDevice_);
		bool bTextureViewInitSuccess = inst->dsv->Initialize(pDevice_, pResource->pTexture, mipSlice, firstArray, arraySize);
		assert(bTextureViewInitSuccess);

		auto view = &inst->dsv;
		viewInstances_.insert(std::make_pair(pResource->pTexture, std::move(inst)));
		return view;
	}
	
	UnorderedAccessView* TransientResourceManager::CreateOrGetUnorderedAccessTextureView(RenderGraphResource* pResource, u32 mipSlice, u32 firstArray, u32 arraySize)
	{
		if (!pResource->bIsTexture)
		{
			return nullptr;
		}

		std::lock_guard<std::mutex> lock(viewMutex_);

		// find cache.
		RDGTextureViewDesc desc{ mipSlice, 0, firstArray, arraySize };
		auto ret = viewInstances_.equal_range(pResource->pTexture);
		for (auto it = ret.first; it != ret.second; ++it)
		{
			if (it->second->type == RDGResourceViewType::UnorderedAccessTexture)
			{
				if (it->second->textureDesc == desc)
				{
					it->second->unusedFrame_ = 0;
					return &(it->second->uav);
				}
			}
		}

		// create new instance.
		std::unique_ptr<RDGResourceViewInstance> inst = std::make_unique<RDGResourceViewInstance>();
		inst->type = RDGResourceViewType::UnorderedAccessTexture;
		inst->textureDesc = desc;
		inst->uav = MakeUnique<UnorderedAccessView>(pDevice_);
		bool bTextureViewInitSuccess = inst->uav->Initialize(pDevice_, pResource->pTexture, mipSlice, firstArray, arraySize);
		assert(bTextureViewInitSuccess);

		auto view = &inst->uav;
		viewInstances_.insert(std::make_pair(pResource->pTexture, std::move(inst)));
		return view;
	}

	UnorderedAccessView* TransientResourceManager::CreateOrGetUnorderedAccessBufferView(RenderGraphResource* pResource, u32 firstElement, u32 numElement, u32 stride, u32 offset)
	{
		if (pResource->bIsTexture)
		{
			return nullptr;
		}

		std::lock_guard<std::mutex> lock(viewMutex_);

		// find cache.
		RDGBufferViewDesc desc{ firstElement, numElement, stride, offset };
		auto ret = viewInstances_.equal_range(pResource->pTexture);
		for (auto it = ret.first; it != ret.second; ++it)
		{
			if (it->second->type == RDGResourceViewType::UnorderedAccessBuffer)
			{
				if (it->second->bufferDesc == desc)
				{
					it->second->unusedFrame_ = 0;
					return &(it->second->uav);
				}
			}
		}

		// create new instance.
		std::unique_ptr<RDGResourceViewInstance> inst = std::make_unique<RDGResourceViewInstance>();
		inst->type = RDGResourceViewType::UnorderedAccessBuffer;
		inst->bufferDesc = desc;
		inst->uav = MakeUnique<UnorderedAccessView>(pDevice_);
		bool bTextureViewInitSuccess = inst->uav->Initialize(pDevice_, pResource->pBuffer, firstElement, numElement, stride, offset);
		assert(bTextureViewInitSuccess);

		auto view = &inst->uav;
		viewInstances_.insert(std::make_pair(pResource->pBuffer, std::move(inst)));
		return view;
	}

	TransientResourceManager::RDGResourceType TransientResourceManager::GetResourceInstance(TransientResourceID id, RDGTransientResourceInstance*& OutTransient, RDGExternalResourceInstance*& OutExternal)
	{
		OutTransient = nullptr;
		OutExternal = nullptr;
		if (resourceIDMap_.find(id) != resourceIDMap_.end())
		{
			OutTransient = committedResources_[resourceIDMap_[id]].get();
			return RDGResourceType::Transient;
		}
		if (externalResources_.find(id) != externalResources_.end())
		{
			OutExternal = &externalResources_[id];
			return RDGResourceType::External;
		}
		if (historyResources_.find(id) != historyResources_.end())
		{
			OutTransient = historyResources_[id].get();
			return RDGResourceType::History;
		}
		return RDGResourceType::None;
	}

	TransientResourceManager::RDGTransientResourceInstance* TransientResourceManager::GetTransientResourceInstance(TransientResourceID id)
	{
		auto it = resourceIDMap_.find(id);
		if (it == resourceIDMap_.end())
		{
			return nullptr;
		}
		return committedResources_[it->second].get();
	}

	TransientResourceManager::RDGExternalResourceInstance* TransientResourceManager::GetExternalResourceInstance(TransientResourceID id)
	{
		auto it = externalResources_.find(id);
		if (it == externalResources_.end())
		{
			return nullptr;
		}
		return &it->second;
	}

	void TransientResourceManager::ResetResource()
	{
		static const u8 kMaxStorageFrame = 3;
		
		// delete unused resources.
		auto it = unusedResources_.begin();
		while (it != unusedResources_.end())
		{
			it->second->unusedFrame++;
			if (it->second->unusedFrame > kMaxStorageFrame)
			{
				void* viewKey = it->second->desc.bIsTexture
					? reinterpret_cast<void*>(&(it->second->texture))
					: reinterpret_cast<void*>(&(it->second->buffer));
				auto views = viewInstances_.equal_range(viewKey);
				if (views.first != views.second)
				{
					viewInstances_.erase(views.first, views.second);
				}
				it = unusedResources_.erase(it);
				continue;
			}
			++it;
		}

		// delete unused views.
		auto viewIt = viewInstances_.begin();
		while (viewIt != viewInstances_.end())
		{
			viewIt->second->unusedFrame_++;
			if (viewIt->second->unusedFrame_ > kMaxStorageFrame)
			{
				viewIt = viewInstances_.erase(viewIt);
			}
			else
			{
				++viewIt;
			}
		}
		
		// manage history buffers.
		std::map<TransientResourceID, std::unique_ptr<RDGTransientResourceInstance>> tmpHistories;
		for (auto&& res : historyResources_)
		{
			tmpHistories.emplace(std::make_pair(res.first, std::move(res.second)));
		}
		historyResources_.clear();
		for (auto&& res : tmpHistories)
		{
			TransientResourceID id = res.first;
			id.history++;
			if (res.second->desc.historyFrame <= id.history)
			{
				// move to unused resource.
				res.second->unusedFrame = 0;
				unusedResources_.insert(std::make_pair(res.second->desc, std::move(res.second)));
			}
			else
			{
				// history to next frame.
				historyResources_.emplace(id, std::move(res.second));
			}
		}

		// keep history buffers.
		for (auto id : keepHistoryIDs_)
		{
			u16 itemIndex = resourceIDMap_[id];
			assert(itemIndex < committedResources_.size() && committedResources_[itemIndex] != nullptr);
			id.history++;
			historyResources_.emplace(id, std::move(committedResources_[itemIndex]));
		}
		keepHistoryIDs_.clear();

		// transition committed resource to unused.
		for (auto&& res : committedResources_)
		{
			if (res.get())
			{
				res->unusedFrame = 0;
				unusedResources_.insert(std::make_pair(res->desc, std::move(res)));
			}
		}
		committedResources_.clear();

		// transition pass only resources to unused.
		for (auto&& res : passOnlyResources_)
		{
			if (res.instance.get())
			{
				res.instance->unusedFrame = 0;
				unusedResources_.insert(std::make_pair(res.desc, std::move(res.instance)));
			}
		}
		passOnlyResources_.clear();
	}

	bool TransientResourceManager::CommitResources(const std::vector<TransientResourceDesc>& descs, const std::map<TransientResourceID, u16>& idMap, const std::set<TransientResourceID>& keepHistoryTransientIDs)
	{
		// create or cache transient resources.
		for (auto desc : descs)
		{
			auto find_it = unusedResources_.find(desc);
			if (find_it != unusedResources_.end())
			{
				// use cached resource instance.
				committedResources_.push_back(std::move(find_it->second));
				unusedResources_.erase(find_it);
			}
			else
			{
				// create new resource instance.
				std::unique_ptr<RDGTransientResourceInstance> res = std::make_unique<RDGTransientResourceInstance>();
				res->desc = desc;
				res->state = TransientState::Common;
				if (desc.bIsTexture)
				{
					// create new texture.
					res->texture = MakeUnique<Texture>(pDevice_);
					if (!res->texture->Initialize(pDevice_, desc.textureDesc))
					{
						ConsolePrint("Error : Can NOT create transient texture.");
						assert(false);
					}
				}
				else
				{
					// create new buffer.
					res->buffer = MakeUnique<Buffer>(pDevice_);
					if (!res->buffer->Initialize(pDevice_, desc.bufferDesc))
					{
						ConsolePrint("Error : Can NOT create transient buffer.");
						assert(false);
					}
				}
				committedResources_.push_back(std::move(res));
			}
		}

		// external resource to graph resource.
		for (auto res : externalResources_)
		{
			RenderGraphResource rdgRes;
			rdgRes.bIsTexture = res.second.bIsTexture;
			if (rdgRes.bIsTexture)
			{
				rdgRes.pTexture = res.second.pTexture;
			}
			else
			{
				rdgRes.pBuffer = res.second.pBuffer;
			}
			graphResources_[res.first] = rdgRes;
		}

		// history resource to graph resource.
		for (auto&& res : historyResources_)
		{
			RenderGraphResource rdgRes;
			rdgRes.bIsTexture = res.second->desc.bIsTexture;
			if (rdgRes.bIsTexture)
			{
				rdgRes.pTexture = &res.second->texture;
			}
			else
			{
				rdgRes.pBuffer = &res.second->buffer;
			}
			graphResources_[res.first] = rdgRes;
		}

		// mapping id to resource.
		resourceIDMap_.clear();
		for (auto id : idMap)
		{
			u16 itemIndex = id.second;
			resourceIDMap_[id.first] = itemIndex;

			RenderGraphResource rdgRes;
			rdgRes.bIsTexture = committedResources_[itemIndex]->desc.bIsTexture;
			if (rdgRes.bIsTexture)
			{
				rdgRes.pTexture = &committedResources_[itemIndex]->texture;
			}
			else
			{
				rdgRes.pBuffer = &committedResources_[itemIndex]->buffer;
			}
			graphResources_[id.first] = rdgRes;
		}

		// store history buffer IDs.
		keepHistoryIDs_ = keepHistoryTransientIDs;
		
		return true;
	}

	
	RenderGraph::RenderGraph()
		: pDevice_(nullptr)
	{}

	RenderGraph::~RenderGraph()
	{
		resManager_.Reset();
		fenceStorage_.clear();
		commandListStorages_[HardwareQueue::Graphics].clear();
		commandListStorages_[HardwareQueue::Compute].clear();
		commandListStorages_[HardwareQueue::Copy].clear();
	}

	bool RenderGraph::Initialize(Device* pDev)
	{
		pDevice_ = pDev;

		resManager_ = MakeUnique<TransientResourceManager>(nullptr, pDevice_);

		commandListFrame_ = 0;

		return true;
	}

	void RenderGraph::ClearAllPasses()
	{
		renderPasses_.clear();
		graphEdges_.clear();
	}

	void RenderGraph::ClearAllGraphEdges()
	{
		graphEdges_.clear();
	}

	RenderGraph::Node RenderGraph::AddPass(RenderPassID ID, IRenderPass* pPass)
	{
		renderPasses_[ID] = pPass;
		return Node(ID, this);
	}
	
	bool RenderGraph::AddGraphEdge(RenderPassID ParentID, RenderPassID ChildID)
	{
		GraphEdge edge(ParentID, ChildID);
		GraphEdge redge(ChildID, ParentID);
		if (graphEdges_.find(redge) != graphEdges_.end())
		{
			ConsolePrint("Error! Reverse edge founded! (Parent:%s, Child%s)\n", ParentID.name.c_str(), ChildID.name.c_str());
			return false;
		}
		graphEdges_.emplace(edge);
		return true;
	}
	
	int RenderGraph::AddGraphEdges(const std::vector<RenderPassID>& ParentIDs, const std::vector<RenderPassID>& ChildIDs)
	{
		int count = 0;
		for (auto&& pid : ParentIDs)
		{
			for (auto&& cid : ChildIDs)
			{
				bool bSuccess = AddGraphEdge(pid, cid);
				if (bSuccess) ++count;
			}
		}
		return count;
	}

	void RenderGraph::AddExternalTexture(TransientResourceID id, Texture* pTexture, TransientState::Value state)
	{
		resManager_->AddExternalTexture(id, pTexture, state);
	}
	
	void RenderGraph::AddExternalBuffer(TransientResourceID id, Buffer* pBuffer, TransientState::Value state)
	{
		resManager_->AddExternalBuffer(id, pBuffer, state);
	}

	void RenderGraph::PreCompile()
	{
		transientResources_.clear();
		
		sortedCommands_.clear();
		execCommands_.clear();
		commandLoaders_.clear();

		fences_.clear();
		commandLists_.clear();
		commandListFrame_ = (commandListFrame_ + 1) % 3;
	}

	bool RenderGraph::Compile()
	{
		PreCompile();

		// ready graph sorting.
		std::set<GraphEdge> edges = graphEdges_;
		std::map<RenderPassID, std::vector<RenderPassID>> inputEdgeIDs, outputEdgeIDs;
		std::vector<RenderPassID> nodeS, nodeR;
		for (auto edge : edges)
		{
			inputEdgeIDs[edge.second].push_back(edge.first);
			outputEdgeIDs[edge.first].push_back(edge.second);

			if (inputEdgeIDs.find(edge.first) == inputEdgeIDs.end())
			{
				inputEdgeIDs[edge.first] = std::vector<RenderPassID>();
			}
		}
		for (auto inputEdgeID : inputEdgeIDs)
		{
			if (inputEdgeID.second.empty())
			{
				nodeS.push_back(inputEdgeID.first);
			}
			else
			{
				nodeR.push_back(inputEdgeID.first);
			}
		}

		// sort graph.
		std::vector<RenderPassID> sortedNodeIDs;
		while (!nodeS.empty())
		{
			RenderPassID node = nodeS[0];
			nodeS.erase(nodeS.begin());
			sortedNodeIDs.push_back(node);

			auto&& oEdgeIDs = outputEdgeIDs[node];
			for (auto oEdgeID : oEdgeIDs)
			{
				auto parentNode = node;
				auto childNode = oEdgeID;

				auto&& iEdgeIDs = inputEdgeIDs[childNode];
				auto it = std::find(iEdgeIDs.begin(), iEdgeIDs.end(), parentNode);
				iEdgeIDs.erase(it);
				if (iEdgeIDs.empty())
				{
					nodeS.push_back(childNode);
				}
			}
		}

		// create cross queue deps.
		// NodeID : RenderPassID
		// PassNo : Sorted pass index
		CrossQueueDepsType CrossQueueDeps;
		CrossQueueDeps.resize(sortedNodeIDs.size() + 1);
		for (size_t p = 0; p < sortedNodeIDs.size() + 1; p++)
		{
			for (size_t q = 0; q < HardwareQueue::Max; q++)
			{
				CrossQueueDeps[p][q] = 0;
			}
		}
		auto GetParentNodeIDs = [&edges](RenderPassID nodeID)
		{
			std::vector<RenderPassID> IDs;
			for (auto&& edge : edges)
			{
				if (edge.second == nodeID)
				{
					IDs.push_back(edge.first);
				}
			}
			return IDs;
		};
		for (size_t p = 0; p < sortedNodeIDs.size(); p++)
		{
			u16 childPassNo = (u16)(p + 1);
			auto childNodeID = PassNo2NodeID(sortedNodeIDs, childPassNo);
			auto parentNodeIDs = GetParentNodeIDs(childNodeID);
			if (parentNodeIDs.empty()) continue;

			for (RenderPassID parentNodeID : parentNodeIDs)
			{
				auto parentNode = renderPasses_[parentNodeID];
				auto parentPassNo = NodeID2PassNo(sortedNodeIDs, parentNodeID);
				CrossQueueDeps[childPassNo][parentNode->GetExecuteQueue()] = parentPassNo;
			}
			auto childNode = renderPasses_[childNodeID];
			if (CrossQueueDeps[childPassNo][childNode->GetExecuteQueue()] != 0)
			{
				auto parentPassNo = CrossQueueDeps[childPassNo][childNode->GetExecuteQueue()];
				for (size_t q = 0; q < HardwareQueue::Max; q++)
				{
					if (CrossQueueDeps[childPassNo][q] == 0)
					{
						CrossQueueDeps[childPassNo][q] = CrossQueueDeps[parentPassNo][q];
					}
				}
			}
		}

		// gather transient resources and set lifespan.
		std::map<TransientResource, TransientResource> transients;
		std::set<TransientResourceID> keepHistoryTransientIDs;
		for (size_t n = 0; n < sortedNodeIDs.size(); n++)
		{
			RenderPassID nodeID = sortedNodeIDs[n];
			IRenderPass* pass = renderPasses_[nodeID];
			auto resources = pass->GetInputResources(nodeID);
			auto outres = pass->GetOutputResources(nodeID);
			resources.insert(resources.end(), outres.begin(), outres.end());

			for (auto&& res : resources)
			{
				if (resManager_->GetExternalResourceInstance(res.id))
				{
					continue;
				}
				if (res.id.history > 0)
				{
					continue;
				}
				
				auto it = transients.find(res);
				if (it == transients.end())
				{
					// add new transient resource.
					auto r = res;
					r.lifespan = TransientResourceLifespan();
					it = transients.emplace(res, res).first;
				}

				// and resource usage.
				if (it->second.desc.bIsTexture)
				{
					it->second.desc.textureDesc.usage |= StateToUsage(res.state);
				}
				else
				{
					it->second.desc.bufferDesc.usage |= StateToUsage(res.state);
				}

				// extend lifespan.
				it->second.lifespan.Extend((u16)(n + 1), pass->GetExecuteQueue());
				if (res.desc.historyFrame > 0 && keepHistoryTransientIDs.find(res.id) == keepHistoryTransientIDs.end())
				{
					it->second.lifespan.Extend(0xffff, pass->GetExecuteQueue());
					keepHistoryTransientIDs.emplace(res.id);
				}
			}
		}
		for (auto&& r : transients)
		{
			transientResources_.push_back(r.second);
		}
		
		sortedNodeIDs_ = sortedNodeIDs;

		// compile reuse resources.
		std::vector<TransientResourceDesc> commitResourceDescs;
		std::map<TransientResourceID, u16> commitResIDs;
		CompileReuseResources(CrossQueueDeps, commitResourceDescs, commitResIDs);

		// commit resources.
		resManager_->ResetResource();
		if (!resManager_->CommitResources(commitResourceDescs, commitResIDs, keepHistoryTransientIDs))
		{
			ConsolePrint("Error : Failed to commit transient resources.");
			return false;
		}

		// create commands.
		CreateCommands(CrossQueueDeps);

		return true;
	}

	void RenderGraph::CompileReuseResources(const CrossQueueDepsType& CrossQueueDeps, std::vector<TransientResourceDesc>& OutDescs, std::map<TransientResourceID, u16>& OutIDMap)
	{
		// This structure contains a cached resource desc and a set of IDs to use this resource.
		struct CachedResource
		{
			TransientResourceDesc desc;
			std::set<TransientResourceID> ids;
			std::vector<TransientResourceLifespan> lifespans;
		};
		std::multimap<TransientResourceDesc, CachedResource> cache;
		for (const TransientResource& res : transientResources_)
		{
			TransientResourceDesc key = res.desc;
			if (key.bIsTexture) key.textureDesc.usage = 0;
			else key.bufferDesc.usage = 0;

			auto [it, end] = cache.equal_range(key);
			for (; it != end; ++it)
			{
				CachedResource& cached = it->second;
				bool bOverlapped = false;
				for (auto life : cached.lifespans)
				{
					if (TestOverlap(CrossQueueDeps, life, res.lifespan) == EOverlapResult::Overlapped)
					{
						bOverlapped = true;
						break;
					}
				}
				if (!bOverlapped)
				{
					// reuse cache because no overlap.
					if (cached.desc.bIsTexture)
					{
						cached.desc.textureDesc.usage |= res.desc.textureDesc.usage;
					}
					else
					{
						cached.desc.bufferDesc.usage |= res.desc.bufferDesc.usage;
					}
					cached.ids.emplace(res.id);
					cached.lifespans.emplace_back(res.lifespan);
					break;
				}
			}
			if (it == end)
			{
				// add desc because no hit cache.
				cache.emplace(key, CachedResource{res.desc, {res.id}, {res.lifespan}});
			}
		}

		// OutDescs : The array of descs of non-overlapping resources to generated.
		// OutIDMap : The dictionary of TransientResourceID to OutDescs index.
		for (auto it = cache.begin(); it != cache.end(); ++it)
		{
			u16 no = (u16)OutDescs.size();
			OutDescs.emplace_back(it->second.desc);
			for (auto&& id : it->second.ids)
			{
				OutIDMap[id] = no;
			}
		}
	}

	void RenderGraph::CreateCommands(const CrossQueueDepsType& CrossQueueDeps)
	{
		struct TransitionBarrier
		{
			u16 commandIndex;
			u16 beforeNodeID;
			std::vector<RenderPassID> relativeNodeIDs;
		};
		std::vector<TransitionBarrier> graphicsTransitions;
		
		u16 fenceCount = 0;
		std::map<u16, u16> fenceCmds[HardwareQueue::Max]; // passNo to command index.

		std::vector<Command> tempCommands[HardwareQueue::Max];
		
		// If there is no parent GraphicsQueue in ComputeQueue or CopyQueue,
		// barrier command is loaded first.
		std::vector<RenderPassID> nodeIDsWithoutParentGraphics;
		for (auto id : sortedNodeIDs_)
		{
			if (renderPasses_[id]->GetExecuteQueue() == HardwareQueue::Graphics)
			{
				continue;
			}
			auto passNo = NodeID2PassNo(sortedNodeIDs_, id);
			if (CrossQueueDeps[passNo][HardwareQueue::Graphics] == 0)
			{
				nodeIDsWithoutParentGraphics.push_back(id);
			}
		}
		if (!nodeIDsWithoutParentGraphics.empty())
		{
			Command barrierCmd;
			barrierCmd.type = CommandType::Barrier;
			tempCommands[HardwareQueue::Graphics].push_back(barrierCmd);

			TransitionBarrier transition;
			transition.commandIndex = 0;
			transition.beforeNodeID = 0xff;
			transition.relativeNodeIDs = nodeIDsWithoutParentGraphics;
			graphicsTransitions.push_back(transition);

			Command fenceCmd;
			fenceCmd.type = CommandType::Fence;
			fenceCmd.fenceIndex = fenceCount++;
			tempCommands[HardwareQueue::Graphics].push_back(fenceCmd);

			fenceCmds[HardwareQueue::Graphics][0] = 1;
		}

		auto GetRelativeAnotherQueuePass = [&CrossQueueDeps, this](RenderPassID nodeID, HardwareQueue::Value queue) -> std::vector<RenderPassID>
		{
			std::vector<RenderPassID> ret;
			u16 passNo = NodeID2PassNo(sortedNodeIDs_, nodeID);
			u16 passCnt = (u16)CrossQueueDeps.size();
			for (u16 no = passNo + 1; no < passCnt; no++)
			{
				RenderPassID id = PassNo2NodeID(sortedNodeIDs_, no);
				if (renderPasses_[id]->GetExecuteQueue() != queue && CrossQueueDeps[no][queue] == passNo)
				{
					ret.push_back(PassNo2NodeID(sortedNodeIDs_, no));
				}
			}
			return ret;
		};

		std::set<u16> fenceWaitNodeIDs[HardwareQueue::Max];
		auto IsAlreadyFenceWait = [&fenceWaitNodeIDs](HardwareQueue::Value queue, u16 passNo)
		{
			return fenceWaitNodeIDs[queue].find(passNo) != fenceWaitNodeIDs[queue].end();
		};
		auto SetFenceWait = [&fenceWaitNodeIDs](HardwareQueue::Value queue, u16 passNo)
		{
			fenceWaitNodeIDs[queue].insert(passNo);
		};
		for (auto nodeID : sortedNodeIDs_)
		{
			HardwareQueue::Value queue = renderPasses_[nodeID]->GetExecuteQueue();
			u16 passNo = NodeID2PassNo(sortedNodeIDs_, nodeID);

			if (queue == HardwareQueue::Graphics)
			{
				// graphics queue.
				// add fence wait command.
				u16 computePrevPassNo = CrossQueueDeps[passNo][HardwareQueue::Compute];
				u16 copyPrevPassNo = CrossQueueDeps[passNo][HardwareQueue::Copy];
				if (computePrevPassNo != 0 && !IsAlreadyFenceWait(HardwareQueue::Graphics, computePrevPassNo))
				{
					u16 cmdIndex = fenceCmds[HardwareQueue::Compute][computePrevPassNo];
					assert(tempCommands[HardwareQueue::Compute][cmdIndex].type == CommandType::Fence);

					Command waitCmd;
					waitCmd.type = CommandType::Wait;
					waitCmd.fenceIndex = tempCommands[HardwareQueue::Compute][cmdIndex].fenceIndex;
					tempCommands[HardwareQueue::Graphics].push_back(waitCmd);

					SetFenceWait(HardwareQueue::Graphics, computePrevPassNo);
				}
				if (copyPrevPassNo != 0 && !IsAlreadyFenceWait(HardwareQueue::Graphics, copyPrevPassNo))
				{
					u16 cmdIndex = fenceCmds[HardwareQueue::Copy][copyPrevPassNo];
					assert(tempCommands[HardwareQueue::Copy][cmdIndex].type == CommandType::Fence);
						
					Command waitCmd;
					waitCmd.type = CommandType::Wait;
					waitCmd.fenceIndex = tempCommands[HardwareQueue::Copy][cmdIndex].fenceIndex;
					tempCommands[HardwareQueue::Graphics].push_back(waitCmd);

					SetFenceWait(HardwareQueue::Graphics, copyPrevPassNo);
				}

				// add transition barrier command.
				Command barrierCmd;
				barrierCmd.type = CommandType::Barrier;
				tempCommands[HardwareQueue::Graphics].push_back(barrierCmd);

				TransitionBarrier transition;
				transition.commandIndex = (u16)(tempCommands[HardwareQueue::Graphics].size() - 1);
				transition.beforeNodeID = 0xff;
				transition.relativeNodeIDs.push_back(nodeID);
				graphicsTransitions.push_back(transition);

				// add pass command.
				Command passCmd;
				passCmd.type = CommandType::Pass;
				passCmd.passNodeID = nodeID;
				tempCommands[HardwareQueue::Graphics].push_back(passCmd);

				auto relativeNodeIDs = GetRelativeAnotherQueuePass(nodeID, HardwareQueue::Graphics);
				if (!relativeNodeIDs.empty())
				{
					// add transition barrier and fence.
					barrierCmd.type = CommandType::Barrier;
					tempCommands[HardwareQueue::Graphics].push_back(barrierCmd);

					transition.commandIndex = (u16)(tempCommands[HardwareQueue::Graphics].size() - 1);
					transition.beforeNodeID = 0xff;
					transition.relativeNodeIDs = relativeNodeIDs;
					graphicsTransitions.push_back(transition);

					Command fenceCmd;
					fenceCmd.type = CommandType::Fence;
					fenceCmd.fenceIndex = fenceCount++;
					tempCommands[HardwareQueue::Graphics].push_back(fenceCmd);

					fenceCmds[HardwareQueue::Graphics][passNo] = (u16)(tempCommands[HardwareQueue::Graphics].size() - 1);
				}
			}
			else if (queue == HardwareQueue::Compute)
			{
				// compute queue.
				// if no parent graphics pass, add first fence wait.
				if (std::find(nodeIDsWithoutParentGraphics.begin(), nodeIDsWithoutParentGraphics.end(), nodeID) != nodeIDsWithoutParentGraphics.end())
				{
					u16 cmdIndex = fenceCmds[HardwareQueue::Graphics][0];
					assert(tempCommands[HardwareQueue::Graphics][cmdIndex].type == CommandType::Fence);

					Command waitCmd;
					waitCmd.type = CommandType::Wait;
					waitCmd.fenceIndex = tempCommands[HardwareQueue::Graphics][cmdIndex].fenceIndex;
					tempCommands[HardwareQueue::Compute].push_back(waitCmd);

					SetFenceWait(HardwareQueue::Compute, 0);
				}
				
				// add fence wait command.
				u16 graphicsPrevPassNo = CrossQueueDeps[passNo][HardwareQueue::Graphics];
				u16 copyPrevPassNo = CrossQueueDeps[passNo][HardwareQueue::Copy];
				if (graphicsPrevPassNo != 0 && !IsAlreadyFenceWait(HardwareQueue::Compute, graphicsPrevPassNo))
				{
					u16 cmdIndex = fenceCmds[HardwareQueue::Graphics][graphicsPrevPassNo];
					assert(tempCommands[HardwareQueue::Graphics][cmdIndex].type == CommandType::Fence);
						
					Command waitCmd;
					waitCmd.type = CommandType::Wait;
					waitCmd.fenceIndex = tempCommands[HardwareQueue::Graphics][cmdIndex].fenceIndex;
					tempCommands[HardwareQueue::Compute].push_back(waitCmd);

					SetFenceWait(HardwareQueue::Compute, graphicsPrevPassNo);
				}
				if (copyPrevPassNo != 0 && !IsAlreadyFenceWait(HardwareQueue::Compute, copyPrevPassNo))
				{
					u16 cmdIndex = fenceCmds[HardwareQueue::Copy][copyPrevPassNo];
					assert(tempCommands[HardwareQueue::Copy][cmdIndex].type == CommandType::Fence);
						
					Command waitCmd;
					waitCmd.type = CommandType::Wait;
					waitCmd.fenceIndex = tempCommands[HardwareQueue::Copy][cmdIndex].fenceIndex;
					tempCommands[HardwareQueue::Compute].push_back(waitCmd);

					SetFenceWait(HardwareQueue::Compute, copyPrevPassNo);
				}

				u16 prevPassNo = CrossQueueDeps[passNo][HardwareQueue::Compute];
				if (prevPassNo != 0)
				{
					// UAV barrier.
					RenderPassID prevNodeID = PassNo2NodeID(sortedNodeIDs_, prevPassNo);
					auto outputRess = renderPasses_[prevNodeID]->GetOutputResources(prevNodeID);
					auto inputRess = renderPasses_[nodeID]->GetInputResources(prevNodeID);

					Command barrierCmd;
					barrierCmd.type = CommandType::Barrier;
					for (auto inRes : inputRess)
					{
						auto find_it = std::find_if(outputRess.begin(), outputRess.end(),
							[&inRes](const TransientResource& rhs)
							{
								return inRes.id == rhs.id;
							});
						if (find_it != outputRess.end())
						{
							barrierCmd.barriers.push_back(Barrier(inRes.id, TransientState::UnorderedAccess, TransientState::UnorderedAccess));
						}
					}
					if (!barrierCmd.barriers.empty())
					{
						tempCommands[HardwareQueue::Compute].push_back(barrierCmd);
					}
				}

				// add pass command.
				Command passCmd;
				passCmd.type = CommandType::Pass;
				passCmd.passNodeID = nodeID;
				tempCommands[HardwareQueue::Compute].push_back(passCmd);

				auto relativeNodeIDs = GetRelativeAnotherQueuePass(nodeID, HardwareQueue::Compute);
				if (!relativeNodeIDs.empty())
				{
					// add fence command.
					Command fenceCmd;
					fenceCmd.type = CommandType::Fence;
					fenceCmd.fenceIndex = fenceCount++;
					tempCommands[HardwareQueue::Compute].push_back(fenceCmd);

					fenceCmds[HardwareQueue::Compute][passNo] = (u16)(tempCommands[HardwareQueue::Compute].size() - 1);
				}
			}
			else
			{
				// copy queue.
				// if no parent graphics pass, add first fence wait.
				if (std::find(nodeIDsWithoutParentGraphics.begin(), nodeIDsWithoutParentGraphics.end(), nodeID) != nodeIDsWithoutParentGraphics.end())
				{
					u16 cmdIndex = fenceCmds[HardwareQueue::Graphics][0];
					assert(tempCommands[HardwareQueue::Graphics][cmdIndex].type == CommandType::Fence);

					Command waitCmd;
					waitCmd.type = CommandType::Wait;
					waitCmd.fenceIndex = tempCommands[HardwareQueue::Graphics][cmdIndex].fenceIndex;
					tempCommands[HardwareQueue::Copy].push_back(waitCmd);

					SetFenceWait(HardwareQueue::Copy, 0);
				}

				// add fence wait command.
				u16 graphicsPrevPassNo = CrossQueueDeps[passNo][HardwareQueue::Graphics];
				u16 computePrevPassNo = CrossQueueDeps[passNo][HardwareQueue::Compute];
				if (graphicsPrevPassNo != 0 && !IsAlreadyFenceWait(HardwareQueue::Copy, graphicsPrevPassNo))
				{
					u16 cmdIndex = fenceCmds[HardwareQueue::Graphics][graphicsPrevPassNo];
					assert(tempCommands[HardwareQueue::Graphics][cmdIndex].type == CommandType::Fence);
						
					Command waitCmd;
					waitCmd.type = CommandType::Wait;
					waitCmd.fenceIndex = tempCommands[HardwareQueue::Graphics][cmdIndex].fenceIndex;
					tempCommands[HardwareQueue::Copy].push_back(waitCmd);

					SetFenceWait(HardwareQueue::Copy, graphicsPrevPassNo);
				}
				if (computePrevPassNo != 0 && !IsAlreadyFenceWait(HardwareQueue::Copy, computePrevPassNo))
				{
					u16 cmdIndex = fenceCmds[HardwareQueue::Compute][computePrevPassNo];
					assert(tempCommands[HardwareQueue::Compute][cmdIndex].type == CommandType::Fence);
						
					Command waitCmd;
					waitCmd.type = CommandType::Wait;
					waitCmd.fenceIndex = tempCommands[HardwareQueue::Compute][cmdIndex].fenceIndex;
					tempCommands[HardwareQueue::Copy].push_back(waitCmd);

					SetFenceWait(HardwareQueue::Copy, computePrevPassNo);
				}

				u16 prevPassNo = CrossQueueDeps[passNo][HardwareQueue::Copy];
				if (prevPassNo != 0)
				{
					// no barrier.
				}

				// add pass command.
				Command passCmd;
				passCmd.type = CommandType::Pass;
				passCmd.passNodeID = nodeID;
				tempCommands[HardwareQueue::Copy].push_back(passCmd);

				auto relativeNodeIDs = GetRelativeAnotherQueuePass(nodeID, HardwareQueue::Copy);
				if (!relativeNodeIDs.empty())
				{
					// add fence command.
					Command fenceCmd;
					fenceCmd.type = CommandType::Fence;
					fenceCmd.fenceIndex = fenceCount++;
					tempCommands[HardwareQueue::Copy].push_back(fenceCmd);

					fenceCmds[HardwareQueue::Copy][passNo] = (u16)(tempCommands[HardwareQueue::Copy].size() - 1);
				}
			}
		}

		// set resource barrier.
		for (auto&& transition : graphicsTransitions)
		{
			auto&& cmd = tempCommands[HardwareQueue::Graphics][transition.commandIndex];
			assert(cmd.type == CommandType::Barrier);

			std::map<TransientResourceID, TransientResource> transientRess;
			for (auto nodeID : transition.relativeNodeIDs)
			{
				auto inputRess = renderPasses_[nodeID]->GetInputResources(nodeID);
				auto outputRess = renderPasses_[nodeID]->GetOutputResources(nodeID);
				inputRess.insert(inputRess.end(), outputRess.begin(), outputRess.end());
				for (auto res : inputRess)
				{
					if (transientRess.find(res.id) == transientRess.end())
					{
						transientRess[res.id] = res;
					}
				}
			}

			for (auto res : transientRess)
			{
				TransientResourceManager::RDGTransientResourceInstance* pTRes;
				TransientResourceManager::RDGExternalResourceInstance* pERes;
				auto result = resManager_->GetResourceInstance(res.first, pTRes, pERes);
				switch (result)
				{
				case TransientResourceManager::RDGResourceType::Transient:
				case TransientResourceManager::RDGResourceType::History:
					if (pTRes->state != res.second.state)
					{
						cmd.barriers.push_back(Barrier(res.first, pTRes->state, res.second.state));
						pTRes->state = res.second.state;
					}
					break;
				case TransientResourceManager::RDGResourceType::External:
					if (pERes->state != res.second.state)
					{
						cmd.barriers.push_back(Barrier(res.first, pERes->state, res.second.state));
						pERes->state = res.second.state;
					}
					break;
				default:
					if (res.first.history == 0)
					{
						assert(result != TransientResourceManager::RDGResourceType::None);
					}
					break;
				}
			}
		}

		// create fences.
		for (u16 idx = 0; idx < fenceCount; idx++)
		{
			if (fenceStorage_.size() <= idx)
			{
				UniqueHandle<Fence> fence = MakeUnique<Fence>(pDevice_);
				bool bFenceInit = fence->Initialize(pDevice_);
				assert(bFenceInit);
				fenceStorage_.push_back(std::move(fence));
			}
			fences_.push_back(&fenceStorage_[idx]);
		}

		// create command lists.
		u16 allClCount = 0;
		auto CreateCommandLists = [&allClCount, this](std::vector<Command>& Commands, CommandQueue* Queue, HardwareQueue::Value QueueType)
		{
			u16 clIndex = 0xffff;
			u16 clCount = 0;
			for (auto&& cmd : Commands)
			{
				if (cmd.type == CommandType::Fence || cmd.type == CommandType::Wait)
				{
					clIndex = 0xffff;
				}
				else
				{
					if (clIndex == 0xffff)
					{
						if (commandListStorages_[QueueType].size() <= clCount)
						{
							// new command lists.
							for (int i = 0; i < 3; i++)
							{
								UniqueHandle<CommandList> cmdList = MakeUnique<CommandList>(pDevice_);
								cmdList->Initialize(pDevice_, Queue);
								commandListStorages_[QueueType].push_back(std::move(cmdList));
							}
						}
						commandLists_.push_back(&commandListStorages_[QueueType][clCount + commandListFrame_]);
						clIndex = allClCount++;
						clCount += 3;
					}
					cmd.cmdListIndex = clIndex;
				}
			}
		};
		CreateCommandLists(tempCommands[HardwareQueue::Graphics], &pDevice_->GetGraphicsQueue(), HardwareQueue::Graphics);
		CreateCommandLists(tempCommands[HardwareQueue::Compute], &pDevice_->GetComputeQueue(), HardwareQueue::Compute);
		CreateCommandLists(tempCommands[HardwareQueue::Copy], &pDevice_->GetCopyQueue(), HardwareQueue::Copy);

		// sort commands.
		{
			HardwareQueue::Value crrQueue = HardwareQueue::Graphics;
			size_t cmdIndices[HardwareQueue::Max] = {0};
			std::vector<bool> fenceExec;
			fenceExec.resize(fenceCount);
			for (u16 i = 0; i < fenceCount; i++)
			{
				fenceExec[i] = false;
			}

			Loader loader;
			while (cmdIndices[HardwareQueue::Graphics] < tempCommands[HardwareQueue::Graphics].size()
				|| cmdIndices[HardwareQueue::Compute] < tempCommands[HardwareQueue::Compute].size()
				|| cmdIndices[HardwareQueue::Copy] < tempCommands[HardwareQueue::Copy].size())
			{
				size_t cmdIndex = cmdIndices[crrQueue];
				if (cmdIndex >= tempCommands[crrQueue].size())
				{
					crrQueue = (HardwareQueue::Value)((crrQueue + 1) % HardwareQueue::Max);
					continue;
				}
				
				Command cmd = tempCommands[crrQueue][cmdIndex];
				bool bChangeList = false;
				size_t loaderCmdIndex = 0;
				auto ChangeLoader = [&bChangeList, &loaderCmdIndex, &loader, this]()
				{
					if (!loader.commandIndices.empty())
					{
						loaderCmdIndex = execCommands_.size();
						bChangeList = true;
						execCommands_.push_back(Command());
					}
				};
				if (cmd.type == CommandType::Fence)
				{
					ChangeLoader();

					fenceExec[cmd.fenceIndex] = true;
					cmd.queue = crrQueue;
					sortedCommands_.push_back(cmd);
					execCommands_.push_back(cmd);
					cmdIndices[crrQueue]++;
				}
				else if (cmd.type == CommandType::Wait)
				{
					ChangeLoader();
					
					if (fenceExec[cmd.fenceIndex])
					{
						cmd.queue = crrQueue;
						sortedCommands_.push_back(cmd);
						execCommands_.push_back(cmd);
						cmdIndices[crrQueue]++;
					}
					else
					{
						crrQueue = (HardwareQueue::Value)((crrQueue + 1) % HardwareQueue::Max);
					}
				}
				else
				{
					loader.queue = crrQueue;
					loader.pCmdList = commandLists_[cmd.cmdListIndex];
					loader.commandIndices.push_back((u16)sortedCommands_.size());

					cmd.queue = crrQueue;
					sortedCommands_.push_back(cmd);
					cmdIndices[crrQueue]++;
				}

				if (bChangeList)
				{
					Command loadCmd;
					loadCmd.type = CommandType::Loader;
					loadCmd.queue = loader.queue;
					loadCmd.loaderIndex = (u16)commandLoaders_.size();
					execCommands_[loaderCmdIndex] = loadCmd;
					
					loader.bLastCommand = false;
					commandLoaders_.push_back(loader);
					loader.commandIndices.clear();
				}
			}
			if (!loader.commandIndices.empty())
			{
				Command loadCmd;
				loadCmd.type = CommandType::Loader;
				loadCmd.queue = loader.queue;
				loadCmd.loaderIndex = (u16)commandLoaders_.size();
				execCommands_.push_back(loadCmd);
				
				loader.bLastCommand = true;
				commandLoaders_.push_back(loader);
			}
		}
	}

	void RenderGraph::LoadCommand()
	{
		// ready performance counter.
		PerformanceCounter* pCounter = counters_ + countIndex_;
		size_t countSize = renderPasses_.size() * 2;
		if (!pCounter->timestamp.IsValid() || pCounter->timestamp->GetMaxCount() < countSize)
		{
			pCounter->timestamp.Reset();
			pCounter->timestamp = MakeUnique<Timestamp>(pDevice_);
			pCounter->timestamp->Initialize(pDevice_, countSize);
		}
		pCounter->passIndices.clear();
		pCounter->timestamp->Reset();

		auto QueryConter = [pCounter](CommandList* pCmdList, HardwareQueue::Value queue)
		{
			if (queue != HardwareQueue::Copy)
				pCounter->timestamp->Query(pCmdList);
		};
		auto AddCounterIndex = [pCounter](const std::string name, HardwareQueue::Value queue)
		{
			if (queue != HardwareQueue::Copy)
				pCounter->passIndices.push_back({name, queue});
		};
		auto ResolveCounter = [pCounter](CommandList* pCmdList)
		{
			pCounter->timestamp->Resolve(pCmdList);
		};
		
		// TODO: multi thread implement.
		for (auto&& loader : commandLoaders_)
		{
			loader.pCmdList->Reset();
			for (auto cmdIndex : loader.commandIndices)
			{
				auto&& cmd = sortedCommands_[cmdIndex];
				if (cmd.type == CommandType::Pass)
				{
					// render pass.
					auto pass = renderPasses_[cmd.passNodeID];
					QueryConter(loader.pCmdList, loader.queue);
					pass->Execute(loader.pCmdList, &resManager_, cmd.passNodeID);
					QueryConter(loader.pCmdList, loader.queue);
					AddCounterIndex(cmd.passNodeID.name, loader.queue);
				}
				else
				{
					// barrier.
					for (auto&& barrier : cmd.barriers)
					{
						RenderGraphResource* res = resManager_->GetRenderGraphResource(barrier.id);
						bool IsUAVBarrier = barrier.before == TransientState::UnorderedAccess && barrier.after == TransientState::UnorderedAccess;
						D3D12_RESOURCE_STATES before = StateToD3D12State(barrier.before);
						D3D12_RESOURCE_STATES after = StateToD3D12State(barrier.after);
						if (res)
						{
							if (res->bIsTexture)
							{
								if (IsUAVBarrier)
								{
									loader.pCmdList->AddUAVBarrier(res->pTexture);
								}
								else
								{
									loader.pCmdList->AddTransitionBarrier(res->pTexture, before, after);
								}
							}
							else
							{
								if (IsUAVBarrier)
								{
									loader.pCmdList->AddUAVBarrier(res->pBuffer);
								}
								else
								{
									loader.pCmdList->AddTransitionBarrier(res->pBuffer, before, after);
								}
							}
						}
						else
						{
							bool ResourceNotFound = false;
							assert(ResourceNotFound);
						}
					}
					loader.pCmdList->FlushBarriers();
				}
			}
			if (loader.bLastCommand)
			{
				ResolveCounter(loader.pCmdList);
			}
			loader.pCmdList->Close();
		}
	}
	
	void RenderGraph::Execute()
	{
		auto GetQueue = [this](HardwareQueue::Value type)
		{
			switch (type)
			{
			case HardwareQueue::Graphics:
				return &pDevice_->GetGraphicsQueue();
			case HardwareQueue::Compute:
				return &pDevice_->GetComputeQueue();
			default:
				return &pDevice_->GetCopyQueue();
			}
		};
		for (auto&& cmd : execCommands_)
		{
			if (cmd.type == CommandType::Loader)
			{
				commandLoaders_[cmd.loaderIndex].pCmdList->Execute();
			}
			else if (cmd.type == CommandType::Fence)
			{
				CommandQueue* queue = GetQueue(cmd.queue);
				fences_[cmd.fenceIndex]->Signal(queue);
			}
			else if (cmd.type == CommandType::Wait)
			{
				CommandQueue* queue = GetQueue(cmd.queue);
				fences_[cmd.fenceIndex]->WaitSignal(queue);
			}
		}

		// get performance counter from 2 frames ago.
		PerformanceCounter* pCounter = counters_ + ((countIndex_ + 1) % 3);
		if (pCounter->timestamp.IsValid())
		{
			for (auto&& r : pCounter->passResults)
			{
				r.passNames.clear();
				r.passMicroSecTimes.clear();
			}
			
			size_t count = pCounter->timestamp->GetMaxCount();
			std::vector<uint64_t> results(count);
			pCounter->timestamp->GetTimestamp(0, count, results.data());

			uint64_t freq = pDevice_->GetGraphicsQueue().GetTimestampFrequency();
			auto GetMicroSec = [freq](uint64_t t)
			{
				return (float)t / ((float)freq / 1000000.0f);
			};
			size_t i = 0;
			for (auto&& index : pCounter->passIndices)
			{
				uint64_t dt = results[i + 1] - results[i];
				pCounter->passResults[index.second].passNames.push_back(index.first);
				pCounter->passResults[index.second].passMicroSecTimes.push_back(GetMicroSec(dt));
				i += 2;
			}
		}
		countIndex_ = (countIndex_ + 1) % 3;
	}

}	// namespace sl12

//	EOF
