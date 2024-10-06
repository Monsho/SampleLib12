#pragma once

#include <sl12/util.h>
#include <vector>


#define LatestCommandList	ID3D12GraphicsCommandList10

namespace sl12
{
	class Device;
	class CommandQueue;
	class Texture;
	class Buffer;
	class DescriptorStackList;
	class SamplerDescriptorCache;
	class RootSignature;
	class DescriptorSet;
	class RaytracingDescriptorManager;

	class CommandList
	{
	public:
		class GpuMarker
		{
		public:
			GpuMarker(CommandList* pCmdList, u8 colorIndex, char const* format, ...)
				: pCmdList_(pCmdList)
			{
				assert(pCmdList_ != nullptr);
				va_list args;
				va_start(args, format);
				pCmdList_->PushMarker(colorIndex, format, args);
				va_end(args);
			}
			GpuMarker(const GpuMarker&) = delete;
			~GpuMarker()
			{
				Terminate();
			}

			void Terminate()
			{
				if (pCmdList_)
				{
					pCmdList_->PopMarker();
					pCmdList_ = nullptr;
				}
			}

		private:
			CommandList*	pCmdList_ = nullptr;
		};	// class GpuMarker

	public:
		CommandList()
		{}
		~CommandList()
		{
			Destroy();
		}

		bool Initialize(Device* pDev, CommandQueue* pQueue, bool forDxr = false);
		void Destroy();

		void Reset();

		void Close();

		void Execute();

		// リソースの状態遷移バリア
		void TransitionBarrier(Texture* p, D3D12_RESOURCE_STATES prevState, D3D12_RESOURCE_STATES nextState);
		void TransitionBarrier(Texture* p, UINT subresource, D3D12_RESOURCE_STATES prevState, D3D12_RESOURCE_STATES nextState);
		void TransitionBarrier(Buffer* p, D3D12_RESOURCE_STATES prevState, D3D12_RESOURCE_STATES nextState);

		// UAVの処理完了バリア
		void UAVBarrier(Texture* p);
		void UAVBarrier(Buffer* p);

		// Add barrier request.
		void AddTransitionBarrier(Texture* p, D3D12_RESOURCE_STATES prevState, D3D12_RESOURCE_STATES nextState);
		void AddTransitionBarrier(Texture* p, UINT subresource, D3D12_RESOURCE_STATES prevState, D3D12_RESOURCE_STATES nextState);
		void AddTransitionBarrier(Buffer* p, D3D12_RESOURCE_STATES prevState, D3D12_RESOURCE_STATES nextState);
		void AddUAVBarrier(Texture* p);
		void AddUAVBarrier(Buffer* p);

		// Execute requested barriers.
		void FlushBarriers();

		void SetDescriptorHeapDirty()
		{
			changeHeap_ = true;
		}
		// Set root signature and descriptor set (NOT dynamic resource).
		void SetGraphicsRootSignatureAndDescriptorSet(RootSignature* pRS, DescriptorSet* pDSet, const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>** ppBindlessArrays = nullptr);
		void SetMeshRootSignatureAndDescriptorSet(RootSignature* pRS, DescriptorSet* pDSet, const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>** ppBindlessArrays = nullptr);
		void SetComputeRootSignatureAndDescriptorSet(RootSignature* pRS, DescriptorSet* pDSet, const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>** ppBindlessArrays = nullptr);

		// Set root signature and dynamic resource index.
		void SetGraphicsRootSignatureAndDynamicResource(RootSignature* pRS, const std::vector<std::vector<u32>>& rootIndices);
		void SetMeshRootSignatureAndDynamicResource(RootSignature* pRS, const std::vector<std::vector<u32>>& rootIndices);
		void SetComputeRootSignatureAndDynamicResource(RootSignature* pRS, const std::vector<u32>& rootIndices);

		// Set raytracing global root signature and descriptor set (NOT dynamic resource).
		void SetRaytracingGlobalRootSignatureAndDescriptorSet(
			RootSignature* pRS,
			DescriptorSet* pDSet,
			RaytracingDescriptorManager* pRtDescMan,
			D3D12_GPU_VIRTUAL_ADDRESS* asAddress,
			u32 asAddressCount);

		// Set raytracing global root signature and dynamic resource index.
		void SetRaytracingGlobalRootSignatureAndDynamicResource(
			RootSignature* pRS,
			D3D12_GPU_VIRTUAL_ADDRESS* asAddress,
			u32 asAddressCount,
			const std::vector<u32>& rootIndices);

		// GPU events
		void PushMarker(u8 colorIndex, char const* format, ...);
		void PopMarker();

		// getter
		CommandQueue* GetParentQueue() { return pParentQueue_; }
		DescriptorStackList* GetViewDescriptorStack() { return pViewDescStack_; }
		SamplerDescriptorCache* GetSamplerDescriptorCache() { return pSamplerDescCache_; }
		ID3D12CommandAllocator* GetCommandAllocator() { return pCmdAllocator_; }
		LatestCommandList* GetLatestCommandList() { return pLatestCmdList_; }
		ID3D12GraphicsCommandList* GetCommandList() { return pCmdList_; }
		LatestCommandList* GetDxrCommandList() { return pLatestCmdList_; }
		//ID3D12GraphicsCommandList4* GetDxrCommandList() { return pDxrCmdList_; }

	private:
		Device*						pParentDevice_{ nullptr };
		CommandQueue*				pParentQueue_{ nullptr };
		DescriptorStackList*		pViewDescStack_{ nullptr };
		SamplerDescriptorCache*		pSamplerDescCache_{ nullptr };
		ID3D12CommandAllocator*		pCmdAllocator_{ nullptr };
		ID3D12GraphicsCommandList*	pCmdList_{ nullptr };
		LatestCommandList*			pLatestCmdList_{ nullptr };

		ID3D12DescriptorHeap*		pCurrentSamplerHeap_{ nullptr };
		ID3D12DescriptorHeap*		pPrevSamplerHeap_{ nullptr };
		bool						changeHeap_{ true };

		std::vector<D3D12_RESOURCE_BARRIER>	requestBarriers_;
	};	// class CommandList

}	// namespace sl12

#define GPU_MARKER(pCmdList, colorIndex, format, ...)	sl12::CommandList::GpuMarker gm(pCmdList, colorIndex, format, __VA_ARGS__)

//	EOF
