﻿#include <sl12/render_graph_deprecated.h>

#include "sl12/buffer.h"
#include "sl12/buffer_view.h"
#include "sl12/command_list.h"


namespace sl12
{
	namespace
	{
		static const u32 kInvalidPassIndex = 0xffffffff;
		
		//----
		bool CreateTarget(Device* pDev, RenderGraphTarget* pTarget)
		{
			if (pTarget->desc.type == RenderGraphTargetType::Buffer)
			{
				// create buffer and views.
				BufferDesc buff_desc{};
				buff_desc.heap = BufferHeap::Default;
				buff_desc.size = pTarget->desc.width;
				buff_desc.usage = pTarget->desc.usage;
				buff_desc.initialState = pTarget->currentState = D3D12_RESOURCE_STATE_COMMON;
				
				pTarget->buffer = MakeUnique<Buffer>(pDev);
				if (!pTarget->buffer->Initialize(pDev, buff_desc))
				{
					return false;
				}

				for (auto&& srv : pTarget->desc.srvDescs)
				{
					auto view = MakeUnique<BufferView>(pDev);
					if (!view->Initialize(pDev, &pTarget->buffer, srv.firstElement, srv.numElement, srv.stride))
					{
						return false;
					}
					pTarget->bufferSrvs.push_back(std::move(view));
				}
				for (auto&& uav : pTarget->desc.uavDescs)
				{
					auto view = MakeUnique<UnorderedAccessView>(pDev);
					if (!view->Initialize(pDev, &pTarget->buffer, uav.firstElement, uav.numElement, uav.stride, uav.offset))
					{
						return false;
					}
					pTarget->uavs.push_back(std::move(view));
				}
			}
			else
			{
				// create texture and views.
				static const TextureDimension::Type kDimensions[] = {
					TextureDimension::Max,
					TextureDimension::Texture1D,
					TextureDimension::Texture2D,
					TextureDimension::Texture3D,
					TextureDimension::Texture2D,
				};
				
				TextureDesc tex_desc{};
				tex_desc.width = (u32)pTarget->desc.width;
				tex_desc.height = pTarget->desc.height;
				tex_desc.depth = pTarget->desc.depth;
				tex_desc.mipLevels = pTarget->desc.mipLevels;
				tex_desc.sampleCount = pTarget->desc.sampleCount;
				tex_desc.format = pTarget->desc.format;
				tex_desc.usage = pTarget->desc.usage;
				tex_desc.dimension = kDimensions[pTarget->desc.type];
				tex_desc.initialState = pTarget->currentState = D3D12_RESOURCE_STATE_GENERIC_READ;
				memcpy(tex_desc.clearColor, pTarget->desc.clearColor, sizeof(tex_desc.clearColor));
				tex_desc.clearDepth = pTarget->desc.clearDepth;
				if (pTarget->desc.type == RenderGraphTargetType::TextureCube)
				{
					tex_desc.depth *= 6;
				}

				pTarget->texture = MakeUnique<Texture>(pDev);
				if (!pTarget->texture->Initialize(pDev, tex_desc))
				{
					return false;
				}

				for (auto&& srv : pTarget->desc.srvDescs)
				{
					auto view = MakeUnique<TextureView>(pDev);
					if (!view->Initialize(pDev, &pTarget->texture, srv.firstMip, srv.mipCount, srv.firstArray, srv.arraySize))
					{
						return false;
					}
					pTarget->textureSrvs.push_back(std::move(view));
				}
				for (auto&& rtv : pTarget->desc.rtvDescs)
				{
					auto view = MakeUnique<RenderTargetView>(pDev);
					if (!view->Initialize(pDev, &pTarget->texture, rtv.mipSlice, rtv.firstArray, rtv.arraySize))
					{
						return false;
					}
					pTarget->rtvs.push_back(std::move(view));
				}
				for (auto&& dsv : pTarget->desc.dsvDescs)
				{
					auto view = MakeUnique<DepthStencilView>(pDev);
					if (!view->Initialize(pDev, &pTarget->texture, dsv.mipSlice, dsv.firstArray, dsv.arraySize))
					{
						return false;
					}
					pTarget->dsvs.push_back(std::move(view));
				}
				for (auto&& uav : pTarget->desc.uavDescs)
				{
					auto view = MakeUnique<UnorderedAccessView>(pDev);
					if (!view->Initialize(pDev, &pTarget->texture, uav.mipSlice, uav.firstArray, uav.arraySize))
					{
						return false;
					}
					pTarget->uavs.push_back(std::move(view));
				}
			}

			return true;
		}
	}
	
	//----------------
	//----
	void RenderGraphTargetDesc::CalcHash()
	{
		hash = CalcFnv1a64(&type, sizeof(type));
		hash = CalcFnv1a64(&width, sizeof(width), hash);
		hash = CalcFnv1a64(&height, sizeof(height), hash);
		hash = CalcFnv1a64(&depth, sizeof(depth), hash);
		hash = CalcFnv1a64(&format, sizeof(format), hash);
		hash = CalcFnv1a64(&mipLevels, sizeof(mipLevels), hash);
		hash = CalcFnv1a64(&sampleCount, sizeof(sampleCount), hash);
		hash = CalcFnv1a64(&usage, sizeof(usage), hash);
		hash = CalcFnv1a64(clearColor, sizeof(clearColor), hash);
		hash = CalcFnv1a64(&clearDepth, sizeof(clearDepth), hash);

		if (!srvDescs.empty())
		{
			hash = CalcFnv1a64(srvDescs.data(), sizeof(srvDescs[0]) * srvDescs.size(), hash);
		}
		if (!rtvDescs.empty())
		{
			hash = CalcFnv1a64(rtvDescs.data(), sizeof(rtvDescs[0]) * rtvDescs.size(), hash);
		}
		if (!dsvDescs.empty())
		{
			hash = CalcFnv1a64(dsvDescs.data(), sizeof(dsvDescs[0]) * dsvDescs.size(), hash);
		}
		if (!uavDescs.empty())
		{
			hash = CalcFnv1a64(uavDescs.data(), sizeof(uavDescs[0]) * uavDescs.size(), hash);
		}
	}

	//----------------
	//----
	RenderGraph_Deprecated::RenderGraph_Deprecated()
		: currId_(0)
	{}

	//----
	RenderGraph_Deprecated::~RenderGraph_Deprecated()
	{}

	//----
	void RenderGraph_Deprecated::BeginNewFrame()
	{
		currDescs_.clear();
		targetMap_.clear();
		inputBarriers_.clear();
		outputBarriers_.clear();
		currentPassIndex_ = kInvalidPassIndex;

		for (auto&& t : usedTargets_)
		{
			targetMap_[t.first] = &t.second;
		}
	}

	//----
	RenderGraphTargetID RenderGraph_Deprecated::AddTarget(const RenderGraphTargetDesc& Desc)
	{
		RenderGraphTargetID ret = currId_++;

		currDescs_[ret] = Desc;
		currDescs_[ret].CalcHash();

		return ret;
	}

	//----
	RenderGraphTarget* RenderGraph_Deprecated::CreateOrFindTarget(Device* pDev, const RenderGraphTargetDesc& Desc)
	{
		auto it = std::find_if(unusedTargets_.begin(), unusedTargets_.end(),
			[&Desc](const UniqueHandle<RenderGraphTarget>& Left)
			{
				return Desc.hash == Left->desc.hash;
			});
		
		if (it != unusedTargets_.end())
		{
			// find unused target.
			auto ret = it->Release();
			unusedTargets_.erase(it);
			return ret;
		}

		// create target.
		RenderGraphTarget* Target = new RenderGraphTarget();
		Target->desc = Desc;
		if (!CreateTarget(pDev, Target))
		{
			return nullptr;
		}
		return Target;
	}

	//----
	bool RenderGraph_Deprecated::CreateRenderPasses(Device* pDev, const std::vector<RenderPass>& Passes, const std::vector<RenderGraphTargetID>& CurrHistories, const std::vector<RenderGraphTargetID>& ReturnHistories)
	{
		// get target lifetime.
		std::map<RenderGraphTargetID, u32>	lifetime;
		u32 time = 0;
		for (auto&& pass : Passes)
		{
			for (auto id : pass.input)
			{
				if (id != kInvalidTargetID)
					lifetime[id] = time;
			}
			for (auto id : pass.output)
			{
				if (id != kInvalidTargetID)
					lifetime[id] = time;
			}
			time++;
		}
		for (auto&& id : CurrHistories)
		{
			if (id != kInvalidTargetID)
				lifetime[id] = kInvalidPassIndex;
		}

		// iterate passes.
		u32 index = 0;
		inputBarriers_.resize(Passes.size());
		outputBarriers_.resize(Passes.size());
		for (auto&& pass : Passes)
		{
			// pass validate.
			if ((pass.input.size() != pass.inputStates.size())
				|| (pass.output.size() != pass.outputStates.size()))
			{
				ConsolePrint("Error! Pass input/output count is NOT equal state count.\n");
				return false;
			}

			// add input barriers.
			for (u32 i = 0; i < pass.input.size(); i++)
			{
				auto id = pass.input[i];
				auto state = pass.inputStates[i];

				auto it = targetMap_.find(id);
				if (it == targetMap_.end())
				{
					ConsolePrint("Error! input target is NOT after output. (%s)", currDescs_[id].name.c_str());
					return false;
				}

				Barrier barrier{};
				barrier.before = it->second->currentState;
				barrier.after = state;
				if (barrier.before != barrier.after)
				{
					inputBarriers_[index][id] = barrier;
					it->second->currentState = state;
				}
			}

			// create outputs.
			for (u32 i = 0; i < pass.output.size(); i++)
			{
				auto id = pass.output[i];
				auto state = pass.outputStates[i];

				// create or find target.
				RenderGraphTarget* target = nullptr;
				auto it = targetMap_.find(id);
				if (it != targetMap_.end())
				{
					target = it->second;
				}
				else
				{
					target = CreateOrFindTarget(pDev, currDescs_[id]);
					usedTargets_[id] = UniqueHandle<RenderGraphTarget>(target);
					targetMap_[id] = target;
				}

				// add barrier.
				Barrier barrier{};
				barrier.before = target->currentState;
				barrier.after = state;
				if (barrier.before != barrier.after)
				{
					outputBarriers_[index][id] = barrier;
					target->currentState = state;
				}
			}

			// check lifetime.
			for (auto id : pass.input)
			{
				if (lifetime[id] == index && usedTargets_.find(id) != usedTargets_.end())
				{
					unusedTargets_.push_back(std::move(usedTargets_[id]));
					usedTargets_.erase(id);
				}
			}
			for (auto id : pass.output)
			{
				if (lifetime[id] == index && usedTargets_.find(id) != usedTargets_.end())
				{
					unusedTargets_.push_back(std::move(usedTargets_[id]));
					usedTargets_.erase(id);
				}
			}
			
			index++;
		}

		// return histories.
		for (auto id : ReturnHistories)
		{
			if (id != kInvalidTargetID && usedTargets_.find(id) != usedTargets_.end())
			{
				unusedTargets_.push_back(std::move(usedTargets_[id]));
				usedTargets_.erase(id);
			}
		}

		currentPassIndex_ = prevPassIndex_ = kInvalidPassIndex;

		return true;
	}

	//----
	RenderGraphTarget* RenderGraph_Deprecated::GetTarget(RenderGraphTargetID TargetID)
	{
		auto it = targetMap_.find(TargetID);
		return (it != targetMap_.end()) ? it->second : nullptr;
	}

	//----
	bool RenderGraph_Deprecated::BeginPass(CommandList* pCmdList, u32 PassIndex, bool UseInputBarrier)
	{
		if (currentPassIndex_ != kInvalidPassIndex)
		{
			return false;
		}
		currentPassIndex_ = PassIndex;
		
		if (UseInputBarrier)
		{
			for (auto&& barrier : inputBarriers_[PassIndex])
			{
				auto target = GetTarget(barrier.first);
				if (target)
				{
					if (target->texture.IsValid())
					{
						pCmdList->AddTransitionBarrier(&target->texture, barrier.second.before, barrier.second.after);
					}
					else
					{
						pCmdList->AddTransitionBarrier(&target->buffer, barrier.second.before, barrier.second.after);
					}
				}
			}
			pCmdList->FlushBarriers();
		}

		return true;
	}

	//----
	bool RenderGraph_Deprecated::NextPass(CommandList* pCmdList, bool UseInputBarrier)
	{
		return BeginPass(pCmdList, prevPassIndex_ == kInvalidPassIndex ? 0 : prevPassIndex_ + 1, UseInputBarrier);
	}
	
	//----
	void RenderGraph_Deprecated::EndPass()
	{
		assert(currentPassIndex_ != kInvalidPassIndex);
		prevPassIndex_ = currentPassIndex_;
		currentPassIndex_ = kInvalidPassIndex;
	}

	//----
	void RenderGraph_Deprecated::BarrierInput(CommandList* pCmdList, RenderGraphTargetID TargetID)
	{
		if (currentPassIndex_ == kInvalidPassIndex)
		{
			return;
		}
		
		auto it = inputBarriers_[currentPassIndex_].find(TargetID);
		if (it == inputBarriers_[currentPassIndex_].end())
		{
			return;
		}
		auto target = GetTarget(TargetID);
		if (target)
		{
			if (target->texture.IsValid())
			{
				pCmdList->TransitionBarrier(&target->texture, it->second.before, it->second.after);
			}
			else
			{
				pCmdList->TransitionBarrier(&target->buffer, it->second.before, it->second.after);
			}
		}
	}
	void RenderGraph_Deprecated::BarrierInputsAll(CommandList* pCmdList)
	{
		if (currentPassIndex_ == kInvalidPassIndex)
		{
			return;
		}

		for (auto&& barrier : inputBarriers_[currentPassIndex_])
		{
			auto target = GetTarget(barrier.first);
			if (target)
			{
				if (target->texture.IsValid())
				{
					pCmdList->AddTransitionBarrier(&target->texture, barrier.second.before, barrier.second.after);
				}
				else
				{
					pCmdList->AddTransitionBarrier(&target->buffer, barrier.second.before, barrier.second.after);
				}
			}
		}
		pCmdList->FlushBarriers();
	}

	//----
	void RenderGraph_Deprecated::BarrierOutput(CommandList* pCmdList, RenderGraphTargetID TargetID)
	{
		if (currentPassIndex_ == kInvalidPassIndex)
		{
			return;
		}
		
		auto it = outputBarriers_[currentPassIndex_].find(TargetID);
		if (it == outputBarriers_[currentPassIndex_].end())
		{
			return;
		}
		auto target = GetTarget(TargetID);
		if (target)
		{
			if (target->texture.IsValid())
			{
				pCmdList->TransitionBarrier(&target->texture, it->second.before, it->second.after);
			}
			else
			{
				pCmdList->TransitionBarrier(&target->buffer, it->second.before, it->second.after);
			}
		}
	}
	void RenderGraph_Deprecated::BarrierOutputsAll(CommandList* pCmdList)
	{
		if (currentPassIndex_ == kInvalidPassIndex)
		{
			return;
		}

		for (auto&& barrier : outputBarriers_[currentPassIndex_])
		{
			auto target = GetTarget(barrier.first);
			if (target)
			{
				if (target->texture.IsValid())
				{
					pCmdList->AddTransitionBarrier(&target->texture, barrier.second.before, barrier.second.after);
				}
				else
				{
					pCmdList->AddTransitionBarrier(&target->buffer, barrier.second.before, barrier.second.after);
				}
			}
		}
		pCmdList->FlushBarriers();
	}

}	// namespace sl12

//	EOF
