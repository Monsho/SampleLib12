#pragma once

#include <vector>
#include <list>
#include <map>
#include <set>
#include <sl12/util.h>
#include <sl12/unique_handle.h>
#include <sl12/buffer.h>
#include <sl12/texture.h>
#include <sl12/fence.h>
#include <sl12/command_list.h>


namespace sl12
{
	class BufferView;
	class TextureView;
	class UnorderedAccessView;
	class RenderTargetView;
	class DepthStencilView;

	struct HardwareQueue
	{
		enum Value { Graphics, Compute, Copy, Max };
	};

	using CrossQueueDepsType = std::vector<std::array<u16, HardwareQueue::Max>>;

	//----
	struct RenderPassID
	{
		std::string	name = "";
		u64			hash = 0;

		RenderPassID()
			: name("")
			, hash(0)
		{}
		RenderPassID(const char* n)
		{
			name = n;
			hash = CalcFnv1a64(name.c_str(), name.length());
		}
		RenderPassID(const std::string& n)
		{
			name = n;
			hash = CalcFnv1a64(n.c_str(), n.length());
		}
		RenderPassID(const RenderPassID& id)
			: name(id.name)
			, hash(id.hash)
		{}

		bool operator==(const RenderPassID& rhs) const
		{
#if _DEBUG
			if (hash == rhs.hash)
			{
				assert(name == rhs.name);
				return true;
			}
			return false;
#else
			return (hash == rhs.hash);
#endif
		}

		bool operator<(const RenderPassID& rhs) const
		{
#if _DEBUG
			if (hash == rhs.hash)
			{
				return name < rhs.name;
			}
#endif
			return hash < rhs.hash;
		}
	};
	
	//----
	struct TransientState
	{
		enum Value
		{
			Common,
			RenderTarget,
			DepthStencil,
			ShaderResource,
			UnorderedAccess,
			IndirectArgument,
			CopySrc,
			CopyDst,
			Present,
		};
	};

	//----
	struct TransientResourceLifespan
	{
		u16 first = 0xffff;
		u16 last[HardwareQueue::Max] = { 0 };

		void Extend(u16 pass, HardwareQueue::Value queue)
		{
			first = std::min(first, pass);
			last[queue] = std::max(last[queue], pass);
		}
	};

	//----
	struct TransientResourceID
	{
		std::string	name = "";
		u64			hash = 0;
		u32			history = 0;

		TransientResourceID(const std::string& n, u32 h = 0)
		{
			name = n;
			hash = CalcFnv1a64(n.c_str(), n.length());
			history = h;
		}
		TransientResourceID(const TransientResourceID& id, u32 h)
		{
			name = id.name;
			hash = id.hash;
			history = h;
		}

		bool operator==(const TransientResourceID& rhs) const
		{
#if _DEBUG
			if (hash == rhs.hash)
			{
				assert(name == rhs.name);
				return (history == rhs.history);
			}
			return false;
#else
			return (hash == rhs.hash) && (history == rhs.history);
#endif
		}

		bool operator<(const TransientResourceID& rhs) const
		{
			if (hash == rhs.hash)
			{
#if _DEBUG
				if (name == rhs.name)
				{
					return history < rhs.history;
				}
				return name < rhs.name;
#endif
			}
			return hash < rhs.hash;
		}
	};

	//----
	struct TransientResourceDesc
	{
		bool		bIsTexture = true;
		u32			historyFrame = 0;
		union
		{
			BufferDesc	bufferDesc;
			TextureDesc	textureDesc;
		};

		TransientResourceDesc()
			: bIsTexture(true)
			, historyFrame(0)
		{
			textureDesc = TextureDesc();
		}

		bool operator==(const TransientResourceDesc& rhs) const
		{
			if (bIsTexture != rhs.bIsTexture)
			{
				return false;
			}

			if (bIsTexture)
			{
				return (textureDesc.format == rhs.textureDesc.format
					&& textureDesc.dimension == rhs.textureDesc.dimension
					&& textureDesc.usage == rhs.textureDesc.usage
					&& textureDesc.width == rhs.textureDesc.width
					&& textureDesc.height == rhs.textureDesc.height
					&& textureDesc.depth == rhs.textureDesc.depth
					&& textureDesc.mipLevels == rhs.textureDesc.mipLevels
					&& textureDesc.sampleCount == rhs.textureDesc.sampleCount
					&& textureDesc.forceSysRam == rhs.textureDesc.forceSysRam
					&& textureDesc.deviceShared == rhs.textureDesc.deviceShared
					&& textureDesc.clearColor[0] == rhs.textureDesc.clearColor[0]
					&& textureDesc.clearColor[1] == rhs.textureDesc.clearColor[1]
					&& textureDesc.clearColor[2] == rhs.textureDesc.clearColor[2]
					&& textureDesc.clearColor[3] == rhs.textureDesc.clearColor[3]
					&& textureDesc.clearDepth == rhs.textureDesc.clearDepth
					&& textureDesc.clearStencil == rhs.textureDesc.clearStencil);
			}
			return (bufferDesc.heap == rhs.bufferDesc.heap
				&& bufferDesc.size == rhs.bufferDesc.size
				&& bufferDesc.stride == rhs.bufferDesc.stride
				&& bufferDesc.usage == rhs.bufferDesc.usage
				&& bufferDesc.forceSysRam == rhs.bufferDesc.forceSysRam
				&& bufferDesc.deviceShared == rhs.bufferDesc.deviceShared);
		}

		bool operator<(const TransientResourceDesc& rhs) const
		{
			if (operator==(rhs))
			{
				return false;
			}
			return memcmp(this, &rhs, sizeof(*this)) < 0;
		}
	};

	//----
	struct TransientResource
	{
		TransientResourceID			id;
		TransientResourceDesc		desc;
		TransientResourceLifespan	lifespan;
		TransientState::Value		state;

		TransientResource()
			: id("")
			, state(TransientState::Common)
		{}
		TransientResource(const std::string& _name, TransientState::Value _state)
			: id(_name)
			, state(_state)
		{}
		TransientResource(const TransientResourceID& _id, TransientState::Value _state)
			: id(_id)
			, state(_state)
		{}

		bool operator==(const TransientResource& rhs) const
		{
			return id == rhs.id;
		}

		bool operator<(const TransientResource& rhs) const
		{
			return id < rhs.id;
		}
	};

	//----
	struct RenderGraphResource
	{
		bool					bIsTexture;
		union
		{
			Texture*			pTexture;
			Buffer*				pBuffer;
		};
	};
	
	//----
	class TransientResourceManager
	{
		friend class RenderGraph;

		enum class RDGResourceType
		{
			None,
			Transient,
			External,
			History,
		};

		struct RDGTransientResourceInstance
		{
			TransientResourceDesc	desc;
			TransientState::Value	state;
			UniqueHandle<Texture>	texture;
			UniqueHandle<Buffer>	buffer;
			u8						unusedFrame = 0;

			RDGTransientResourceInstance()
			{}
			RDGTransientResourceInstance(RDGTransientResourceInstance&& rhs) noexcept
			{
				desc = rhs.desc;
				state = rhs.state;
				texture = std::move(rhs.texture);
				buffer = std::move(rhs.buffer);
				unusedFrame = rhs.unusedFrame;
			}
			RDGTransientResourceInstance& operator=(RDGTransientResourceInstance&& rhs) noexcept
			{
				desc = rhs.desc;
				state = rhs.state;
				texture = std::move(rhs.texture);
				buffer = std::move(rhs.buffer);
				unusedFrame = rhs.unusedFrame;
				return *this;
			}
			RDGTransientResourceInstance(RDGTransientResourceInstance& rhs) = delete;
			RDGTransientResourceInstance& operator=(RDGTransientResourceInstance& rhs) = delete;
		};

		struct RDGExternalResourceInstance
		{
			bool					bIsTexture;
			TransientState::Value	state;
			union
			{
				Texture*			pTexture;
				Buffer*				pBuffer;
			};
		};

		enum class RDGResourceViewType
		{
			Texture,
			Buffer,
			RenderTarget,
			DepthStencil,
			UnorderedAccessTexture,
			UnorderedAccessBuffer,
		};

		struct RDGTextureViewDesc
		{
			u32		firstMip = 0;
			u32		mipCount = 0;
			u32		firstArray = 0;
			u32		arraySize = 0;

			bool operator==(const RDGTextureViewDesc& rhs) const
			{
				return (firstMip == rhs.firstMip)
					&& (mipCount == rhs.mipCount)
					&& (firstArray == rhs.firstArray)
					&& (arraySize == rhs.arraySize);
			}
		};

		struct RDGBufferViewDesc
		{
			u32		firstElement = 0;
			u32		numElement = 0;
			u32		stride = 0;
			u32		offset = 0;

			bool operator==(const RDGBufferViewDesc& rhs) const
			{
				return (firstElement == rhs.firstElement)
					&& (numElement == rhs.numElement)
					&& (stride == rhs.stride);
			}
		};

		struct RDGResourceViewInstance
		{
			RDGResourceViewType					type = RDGResourceViewType::Texture;
			u8									unusedFrame_ = 0;
			union
			{
				RDGTextureViewDesc				textureDesc;
				RDGBufferViewDesc				bufferDesc;
			};
			UniqueHandle<TextureView>			texture;
			UniqueHandle<BufferView>			buffer;
			UniqueHandle<RenderTargetView>		rtv;
			UniqueHandle<DepthStencilView>		dsv;
			UniqueHandle<UnorderedAccessView>	uav;

			RDGResourceViewInstance()
			{}
			RDGResourceViewInstance(RDGResourceViewInstance&& rhs) noexcept
			{
				type = rhs.type;
				unusedFrame_ = rhs.unusedFrame_;
				switch (type)
				{
				case RDGResourceViewType::Texture:
				case RDGResourceViewType::RenderTarget:
				case RDGResourceViewType::DepthStencil:
				case RDGResourceViewType::UnorderedAccessTexture:
					textureDesc = rhs.textureDesc; break;
				default:
					bufferDesc = rhs.bufferDesc; break;
				}
				texture = std::move(rhs.texture);
				buffer = std::move(rhs.buffer);
				rtv = std::move(rhs.rtv);
				dsv = std::move(rhs.dsv);
				uav = std::move(rhs.uav);
			}
			RDGResourceViewInstance& operator=(RDGResourceViewInstance&& rhs) noexcept
			{
				type = rhs.type;
				unusedFrame_ = rhs.unusedFrame_;
				switch (type)
				{
				case RDGResourceViewType::Texture:
				case RDGResourceViewType::RenderTarget:
				case RDGResourceViewType::DepthStencil:
				case RDGResourceViewType::UnorderedAccessTexture:
					textureDesc = rhs.textureDesc; break;
				default:
					bufferDesc = rhs.bufferDesc; break;
				}
				texture = std::move(rhs.texture);
				buffer = std::move(rhs.buffer);
				rtv = std::move(rhs.rtv);
				dsv = std::move(rhs.dsv);
				uav = std::move(rhs.uav);
				return *this;
			}
			RDGResourceViewInstance(RDGResourceViewInstance& rhs) = delete;
			RDGResourceViewInstance& operator=(RDGResourceViewInstance& rhs) = delete;
		};
		
	public:
		TransientResourceManager(Device* pDev)
			: pDevice_(pDev)
		{}
		~TransientResourceManager();

		RenderGraphResource* GetRenderGraphResource(TransientResourceID id);

		TextureView* CreateOrGetTextureView(RenderGraphResource* pResource, u32 firstMip = 0, u32 mipCount = 0, u32 firstArray = 0, u32 arraySize = 0);
		BufferView* CreateOrGetBufferView(RenderGraphResource* pResource, u32 firstElement, u32 numElement, u32 stride);
		RenderTargetView* CreateOrGetRenderTargetView(RenderGraphResource* pResource, u32 mipSlice = 0, u32 firstArray = 0, u32 arraySize = 1);
		DepthStencilView* CreateOrGetDepthStencilView(RenderGraphResource* pResource, u32 mipSlice = 0, u32 firstArray = 0, u32 arraySize = 1);
		UnorderedAccessView* CreateOrGetUnorderedAccessTextureView(RenderGraphResource* pResource, u32 mipSlice = 0, u32 firstArray = 0, u32 arraySize = 1);
		UnorderedAccessView* CreateOrGetUnorderedAccessBufferView(RenderGraphResource* pResource, u32 firstElement, u32 numElement, u32 stride, u32 offset);
		
	private:
		void AddExternalTexture(TransientResourceID id, Texture* pTexture, TransientState::Value state);
		void AddExternalBuffer(TransientResourceID id, Buffer* pBuffer, TransientState::Value state);

		void ResetResource();
		bool CommitResources(const std::vector<TransientResourceDesc>& descs, const std::map<TransientResourceID, u16>& idMap, const std::set<TransientResourceID>& keepHistoryTransientIDs);

		RDGResourceType GetResourceInstance(TransientResourceID id, RDGTransientResourceInstance*& OutTransient, RDGExternalResourceInstance*& OutExternal);
		RDGTransientResourceInstance* GetTransientResourceInstance(TransientResourceID id);
		RDGExternalResourceInstance* GetExternalResourceInstance(TransientResourceID id);

	private:
		Device*		pDevice_ = nullptr;

		std::vector<std::unique_ptr<RDGTransientResourceInstance>>							committedResources_;
		std::map<TransientResourceID, RenderGraphResource>									graphResources_;
		std::map<TransientResourceID, u16>													resourceIDMap_;
		std::multimap<TransientResourceDesc, std::unique_ptr<RDGTransientResourceInstance>>	unusedResources_;
		std::set<TransientResourceID>														keepHistoryIDs_;
		std::map<TransientResourceID, std::unique_ptr<RDGTransientResourceInstance>>		historyResources_;
		std::map<TransientResourceID, RDGExternalResourceInstance>							externalResources_;

		std::mutex																			viewMutex_;
		std::multimap<void*, std::unique_ptr<RDGResourceViewInstance>>						viewInstances_;
	};

	//----
	class IRenderPass
	{
	public:
		virtual ~IRenderPass() {}
		virtual std::vector<TransientResource> GetInputResources(const RenderPassID& ID) const = 0;
		virtual std::vector<TransientResource> GetOutputResources(const RenderPassID& ID) const = 0;
		virtual HardwareQueue::Value GetExecuteQueue() const = 0;
		virtual void Execute(CommandList* pCmdList, TransientResourceManager* pResManager, const RenderPassID& ID) = 0;
	};

	//----
	class RenderGraph
	{
	private:
		struct CommandType
		{
			enum Value
			{
				Pass,
				Barrier,
				Fence,
				Wait,
				Loader,
			};
		};
		struct Barrier
		{
			TransientResourceID		id;
			TransientState::Value	before;
			TransientState::Value	after;

			Barrier(TransientResourceID _id, TransientState::Value _before, TransientState::Value _after)
				: id(_id), before(_before), after(_after)
			{}
		};
		struct Command
		{
			CommandType::Value		type;
			HardwareQueue::Value	queue;
			RenderPassID			passNodeID;
			u16						cmdListIndex;
			//u16						passNodeID;
			u16						fenceIndex;
			u16						loaderIndex;
			std::vector<Barrier>	barriers;
		};
		struct Loader
		{
			HardwareQueue::Value	queue;
			CommandList*			pCmdList;
			std::vector<u16>		commandIndices;
		};
		
	public:
		RenderGraph();
		~RenderGraph();

		bool Initialize(Device* pDev);

		void ClearAllPasses();
		void ClearAllGraphEdges();
		RenderPassID AddPass(RenderPassID ID, IRenderPass* pPass);
		bool AddGraphEdge(RenderPassID ParentID, RenderPassID ChildID);
		int AddGraphEdges(const std::vector<RenderPassID>& ParentIDs, const std::vector<RenderPassID>& ChildID);
		// void AddPass(IRenderPass* pPass, IRenderPass* pParent);
		// void AddPass(IRenderPass* pPass, const std::vector<IRenderPass*>& parents);

		void AddExternalTexture(TransientResourceID id, Texture* pTexture, TransientState::Value state);
		void AddExternalBuffer(TransientResourceID id, Buffer* pBuffer, TransientState::Value state);
		
		bool Compile();
		void LoadCommand();
		void Execute();

	private:
		void PreCompile();
		void CompileReuseResources(const CrossQueueDepsType& CrossQueueDeps, std::vector<TransientResourceDesc>& OutDescs, std::map<TransientResourceID, u16>& OutIDMap);
		void CreateCommands(const CrossQueueDepsType& CrossQueueDeps);

	private:
		typedef std::pair<RenderPassID, RenderPassID> GraphEdge;
		//typedef std::pair<u16, u16> GraphEdge;
		
	private:
		Device*							pDevice_ = nullptr;
		UniqueHandle<TransientResourceManager>	resManager_;
		std::map<RenderPassID, IRenderPass*>	renderPasses_;
		//std::vector<IRenderPass*>		renderPasses_;
		std::set<GraphEdge>				graphEdges_;
		std::vector<RenderPassID>		sortedNodeIDs_;
		//std::vector<u16>				sortedNodeIDs_;
		std::vector<TransientResource>	transientResources_;

		std::vector<Command>			sortedCommands_;
		std::vector<Command>			execCommands_;
		std::vector<Loader>				commandLoaders_;

		std::vector<Fence*>						fences_;				
		std::vector<UniqueHandle<Fence>>		fenceStorage_;
		std::vector<CommandList*>				commandLists_;
		std::vector<UniqueHandle<CommandList>>	commandListStorages_[HardwareQueue::Max];
		u8										commandListFrame_;
	};
	
}	// namespace sl12

//	EOF
