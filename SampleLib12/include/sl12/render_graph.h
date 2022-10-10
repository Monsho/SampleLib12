#pragma once

#include <vector>
#include <list>
#include <map>
#include <sl12/util.h>
#include <sl12/unique_handle.h>


namespace sl12
{
	class Texture;
	class Buffer;
	class BufferView;
	class TextureView;
	class UnorderedAccessView;
	class RenderTargetView;
	class DepthStencilView;
	
	typedef u64 RenderGraphTargetID;

	//----------------
	struct RenderGraphTargetType
	{
		enum Type
		{
			Buffer,
			Texture1D,
			Texture2D,
			Texture3D,
			TextureCube,

			Max
		};
	};	// struct RenderGraphTargetType

	//----------------
	struct RenderGraphSRVDesc
	{
		u32		firstMip = 0;
		u32		mipCount = 0;
		u32		firstArray = 0;
		u32		arraySize = 0;

		u32		firstElement = 0;
		u32		numElement = 0;
		u32		stride = 0;

		RenderGraphSRVDesc(u32 FirstMip, u32 MipCount, u32 FirstArray, u32 ArraySize)
			: firstMip(FirstMip), mipCount(MipCount), firstArray(FirstArray), arraySize(ArraySize)
		{}
		RenderGraphSRVDesc(u32 FirstElement, u32 NumElement, u32 Stride)
			: firstElement(FirstElement), numElement(NumElement), stride(Stride)
		{}
	};	// struct RenderGraphRTVDesc
	
	//----------------
	struct RenderGraphRTVDesc
	{
		u32		mipSlice = 0;
		u32		firstArray = 0;
		u32		arraySize = 0;

		RenderGraphRTVDesc(u32 MipSlice, u32 FirstArray, u32 ArraySize)
			: mipSlice(MipSlice), firstArray(FirstArray), arraySize(ArraySize)
		{}
	};	// struct RenderGraphRTVDesc
	
	//----------------
	struct RenderGraphDSVDesc
	{
		u32		mipSlice = 0;
		u32		firstArray = 0;
		u32		arraySize = 0;

		RenderGraphDSVDesc(u32 MipSlice, u32 FirstArray, u32 ArraySize)
			: mipSlice(MipSlice), firstArray(FirstArray), arraySize(ArraySize)
		{}
	};	// struct RenderGraphDSVDesc

	//----------------
	struct RenderGraphUAVDesc
	{
		u32		mipSlice = 0;
		u32		firstArray = 0;
		u32		arraySize = 0;
		
		u32		firstElement = 0;
		u32		numElement = 0;
		u32		stride = 0;
		u32		offset = 0;

		RenderGraphUAVDesc(u32 MipSlice, u32 FirstArray, u32 ArraySize)
			: mipSlice(MipSlice), firstArray(FirstArray), arraySize(ArraySize)
		{}
		RenderGraphUAVDesc(u32 FirstElement, u32 NumElement, u32 Stride, u32 Offset)
			: firstElement(FirstElement), numElement(NumElement), stride(Stride), offset(Offset)
		{}
	};	// struct RenderGraphUAVDesc
	
	//----------------
	struct RenderGraphTargetDesc
	{
		std::string						name = "";
		RenderGraphTargetType::Type		type = RenderGraphTargetType::Texture2D;
		u64								width = 1;
		u32								height = 1, depth = 1;
		DXGI_FORMAT						format = DXGI_FORMAT_UNKNOWN;
		u32								mipLevels = 1;
		u32								sampleCount = 1;
		u32								usage = ResourceUsage::ShaderResource | ResourceUsage::RenderTarget;

		std::vector<RenderGraphSRVDesc>	srvDescs;
		std::vector<RenderGraphRTVDesc>	rtvDescs;
		std::vector<RenderGraphDSVDesc>	dsvDescs;
		std::vector<RenderGraphUAVDesc>	uavDescs;

		u64								hash = 0;

		void CalcHash();
	};	// struct RenderGraphTargetDesc

	//----------------
	struct RenderGraphTarget
	{
		RenderGraphTargetDesc							desc;
		UniqueHandle<Buffer>							buffer;
		UniqueHandle<Texture>							texture;
		std::vector<UniqueHandle<BufferView>>			bufferSrvs;
		std::vector<UniqueHandle<TextureView>>			textureSrvs;
		std::vector<UniqueHandle<RenderTargetView>>		rtvs;
		std::vector<UniqueHandle<DepthStencilView>>		dsvs;
		std::vector<UniqueHandle<UnorderedAccessView>>	uavs;

		D3D12_RESOURCE_STATES							currentState;

		bool IsValid() const
		{
			return buffer.IsValid() || texture.IsValid();
		}
	};	// struct RenderGraphTarget

	//----------------
	struct RenderPass
	{
		std::vector<RenderGraphTargetID>	input;
		std::vector<RenderGraphTargetID>	output;
		std::vector<D3D12_RESOURCE_STATES>	inputStates;
		std::vector<D3D12_RESOURCE_STATES>	outputStates;
	};	// struct RenderPass
	
	//----------------
	class RenderGraph
	{
		struct Barrier
		{
			D3D12_RESOURCE_STATES	before;
			D3D12_RESOURCE_STATES	after;
		};	// struct Barrier
		typedef std::map<RenderGraphTargetID, Barrier> BarrierMap;
		
	public:
		RenderGraph();
		~RenderGraph();

		void BeginNewFrame();

		RenderGraphTargetID AddTarget(const RenderGraphTargetDesc& Desc);

		bool CreateRenderPasses(Device* pDev, const std::vector<RenderPass>& Passes, const std::vector<RenderGraphTargetID>& CurrHistories);

		RenderGraphTarget* GetTarget(RenderGraphTargetID TargetID);

		bool BeginPass(CommandList* pCmdList, u32 PassIndex, bool UseInputBarrier = true);
		void EndPass();
		void BarrierInput(CommandList* pCmdList, RenderGraphTargetID TargetID);
		void BarrierInputsAll(CommandList* pCmdList);
		void BarrierOutput(CommandList* pCmdList, RenderGraphTargetID TargetID);
		void BarrierOutputsAll(CommandList* pCmdList);

	private:
		RenderGraphTarget* CreateOrFindTarget(Device* pDev, const RenderGraphTargetDesc& Desc);
		
	private:
		RenderGraphTargetID		currId_;

		std::map<RenderGraphTargetID, RenderGraphTargetDesc>			currDescs_;
		std::map<RenderGraphTargetID, RenderGraphTarget*>				targetMap_;
		std::map<RenderGraphTargetID, UniqueHandle<RenderGraphTarget>>	usedTargets_;
		std::list<UniqueHandle<RenderGraphTarget>>						unusedTargets_;
		std::vector<BarrierMap>											inputBarriers_;
		std::vector<BarrierMap>											outputBarriers_;

		u32		currentPassIndex_;
	};	// class RenderGraph
	
}	// namespace sl12

//	EOF
