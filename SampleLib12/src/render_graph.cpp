#include <sl12/render_graph.h>

#include <set>

#include "sl12/buffer.h"
#include "sl12/buffer_view.h"
#include "sl12/command_list.h"


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
			D3D12_RESOURCE_STATE_COPY_SOURCE,			// CopySrc
			D3D12_RESOURCE_STATE_COPY_DEST,				// CopyDst
			D3D12_RESOURCE_STATE_PRESENT,				// Present
		};
		return kD3D12States[state];
	}

	sl12::u16 NodeID2PassNo(const std::vector<sl12::u16>& sortedNodeIDs, sl12::u16 nodeID)
	{
		auto dist = std::distance(sortedNodeIDs.begin(), std::find(sortedNodeIDs.begin(), sortedNodeIDs.end(), nodeID));
		assert(dist >= 0);
		return (sl12::u16)(dist + 1);
	};

	sl12::u16 PassNo2NodeID(const std::vector<sl12::u16>& sortedNodeIDs, sl12::u16 passNo)
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
		ExternalResourceInstance inst;
		inst.bIsTexture = true;
		inst.pTexture = pTexture;
		inst.state = state;
		externalResources_[id] = inst;
	}
	
	void TransientResourceManager::AddExternalBuffer(TransientResourceID id, Buffer* pBuffer, TransientState::Value state)
	{
		ExternalResourceInstance inst;
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

	TransientResourceManager::ResourceType TransientResourceManager::GetResourceInstance(TransientResourceID id, TransientResourceInstance*& OutTransient, ExternalResourceInstance*& OutExternal)
	{
		OutTransient = nullptr;
		OutExternal = nullptr;
		if (resourceIDMap_.find(id) != resourceIDMap_.end())
		{
			OutTransient = committedResources_[resourceIDMap_[id]].get();
			return ResourceType::Transient;
		}
		if (externalResources_.find(id) != externalResources_.end())
		{
			OutExternal = &externalResources_[id];
			return ResourceType::External;
		}
		return ResourceType::None;
	}

	TransientResourceInstance* TransientResourceManager::GetTransientResourceInstance(TransientResourceID id)
	{
		auto it = resourceIDMap_.find(id);
		if (it == resourceIDMap_.end())
		{
			return nullptr;
		}
		return committedResources_[it->second].get();
	}

	ExternalResourceInstance* TransientResourceManager::GetExternalResourceInstance(TransientResourceID id)
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
		for (auto&& res : committedResources_)
		{
			unusedResources_.insert(std::make_pair(res->desc, std::move(res)));
		}
		committedResources_.clear();
	}

	bool TransientResourceManager::CommitResources(const std::vector<TransientResourceDesc>& descs, const std::map<TransientResourceID, u16>& idMap)
	{
		// create or cache resources.
		std::vector<u16> DescIndexToItemIndex;
		for (auto desc : descs)
		{
			auto find_it = unusedResources_.find(desc);
			if (find_it != unusedResources_.end())
			{
				// use cached texture.
				committedResources_.push_back(std::move(find_it->second));
				unusedResources_.erase(find_it);
			}
			else
			{
				std::unique_ptr<TransientResourceInstance> res = std::make_unique<TransientResourceInstance>();
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
			DescIndexToItemIndex.push_back((u16)(committedResources_.size() - 1));
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

		// mapping id to resource.
		resourceIDMap_.clear();
		for (auto id : idMap)
		{
			u16 itemIndex = DescIndexToItemIndex[id.second];
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

	void RenderGraph::ClearPasses()
	{
		renderPasses_.clear();
		graphEdges_.clear();
	}

	void RenderGraph::AddPass(IRenderPass* pPass, IRenderPass* pParent)
	{
		std::vector<IRenderPass*> parents;
		if (pParent) parents.push_back(pParent);
		AddPass(pPass, parents);
	}

	void RenderGraph::AddPass(IRenderPass* pPass, const std::vector<IRenderPass*>& parents)
	{
#if _DEBUG
		auto it = std::find(renderPasses_.begin(), renderPasses_.end(), pPass);
		assert(it == renderPasses_.end());
#endif
		u16 ownID = (u16)renderPasses_.size();
		renderPasses_.push_back(pPass);
		if (!parents.empty())
		{
			for (auto pParent : parents)
			{
				auto pit = std::find(renderPasses_.begin(), renderPasses_.end(), pParent);
				assert(pit != renderPasses_.end());
				u16 parentID = (u16)std::distance(renderPasses_.begin(), pit);
				graphEdges_.push_back(GraphEdge(parentID, ownID));
			}
		}
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
		std::vector<GraphEdge> edges = graphEdges_;
		std::map<u16, std::vector<u16>> inputEdgeIDs, outputEdgeIDs;
		std::vector<u16> nodeS, nodeR;
		for (size_t i = 0; i < renderPasses_.size(); i++)
		{
			inputEdgeIDs[(u16)i] = std::vector<u16>();
			outputEdgeIDs[(u16)i] = std::vector<u16>();
		}
		for (auto edge : edges)
		{
			inputEdgeIDs[edge.second].push_back(edge.first);
			outputEdgeIDs[edge.first].push_back(edge.second);
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
		std::vector<u16> sortedNodeIDs;
		while (!nodeS.empty())
		{
			u16 node = nodeS[0];
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
		CrossQueueDepsType CrossQueueDeps;
		CrossQueueDeps.resize(sortedNodeIDs.size() + 1);
		for (size_t p = 0; p < sortedNodeIDs.size() + 1; p++)
		{
			for (size_t q = 0; q < HardwareQueue::Max; q++)
			{
				CrossQueueDeps[p][q] = 0;
			}
		}
		auto GetParentNodeIDs = [&edges](u16 nodeID)
		{
			std::vector<u16> IDs;
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

			for (u16 parentNodeID : parentNodeIDs)
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
		for (size_t n = 0; n < sortedNodeIDs.size(); n++)
		{
			u16 nodeID = sortedNodeIDs[n];
			IRenderPass* pass = renderPasses_[nodeID];
			auto resources = pass->GetInputResources();
			auto outres = pass->GetOutputResources();
			resources.insert(resources.end(), outres.begin(), outres.end());

			for (auto&& res : resources)
			{
				if (resManager_->GetExternalResourceInstance(res.id))
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
		if (!resManager_->CommitResources(commitResourceDescs, commitResIDs))
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
			std::vector<u16> relativeNodeIDs;
		};
		std::vector<TransitionBarrier> graphicsTransitions;
		
		u16 fenceCount = 0;
		std::map<u16, u16> fenceCmds[HardwareQueue::Max]; // passNo to command index.

		std::vector<Command> tempCommands[HardwareQueue::Max];
		
		// If there is no parent GraphicsQueue in ComputeQueue or CopyQueue,
		// barrier command is loaded first.
		std::vector<u16> nodeIDsWithoutParentGraphics;
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

		auto GetRelativeAnotherQueuePass = [&CrossQueueDeps, this](u16 nodeID, HardwareQueue::Value queue) -> std::vector<u16>
		{
			std::vector<u16> ret;
			u16 passNo = NodeID2PassNo(sortedNodeIDs_, nodeID);
			u16 passCnt = (u16)CrossQueueDeps.size();
			for (u16 no = passNo + 1; no < passCnt; no++)
			{
				u16 id = PassNo2NodeID(sortedNodeIDs_, no);
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
					u16 prevNodeID = PassNo2NodeID(sortedNodeIDs_, prevPassNo);
					auto outputRess = renderPasses_[prevNodeID]->GetOutputResources();
					auto inputRess = renderPasses_[nodeID]->GetInputResources();

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
				auto inputRess = renderPasses_[nodeID]->GetInputResources();
				auto outputRess = renderPasses_[nodeID]->GetOutputResources();
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
				TransientResourceInstance* pTRes;
				ExternalResourceInstance* pERes;
				auto result = resManager_->GetResourceInstance(res.first, pTRes, pERes);
				switch (result)
				{
				case TransientResourceManager::Transient:
					if (pTRes->state != res.second.state)
					{
						cmd.barriers.push_back(Barrier(res.first, pTRes->state, res.second.state));
						pTRes->state = res.second.state;
					}
					break;
				case TransientResourceManager::External:
					if (pERes->state != res.second.state)
					{
						cmd.barriers.push_back(Barrier(res.first, pERes->state, res.second.state));
						pERes->state = res.second.state;
					}
					break;
				default:
					assert(result != TransientResourceManager::None);
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
				commandLoaders_.push_back(loader);
			}
		}
	}

	void RenderGraph::LoadCommand()
	{
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
					pass->Execute(loader.pCmdList, &resManager_);
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
	}

}	// namespace sl12

//	EOF
