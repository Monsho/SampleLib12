#pragma once

#include <vector>
#include <list>
#include <map>
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

	struct TransientState
	{
		enum Value
		{
			Common,
			RenderTarget,
			DepthStencil,
			ShaderResource,
			UnorderedAccess,
			CopySrc,
			CopyDst,
			Present,
		};
	};

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

	struct TransientResourceID
	{
		std::string	name = "";
		u64			hash = 0;

		TransientResourceID(const std::string& n)
		{
			name = n;
			hash = CalcFnv1a64(n.c_str(), n.length());
		}

		bool operator==(const TransientResourceID& rhs) const
		{
#if _DEBUG
			if (hash == rhs.hash)
			{
				assert(name == rhs.name);
				return true;
			}
			return false;
#else
			return hash == rhs.hash;
#endif
		}

		bool operator<(const TransientResourceID& rhs) const
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

	struct TransientResourceDesc
	{
		bool		bIsTexture = true;
		union
		{
			BufferDesc	bufferDesc;
			TextureDesc	textureDesc;
		};

		TransientResourceDesc()
			: bIsTexture(true)
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
					&& textureDesc.deviceShared == rhs.textureDesc.deviceShared);
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

	struct TransientResourceInstance
	{
		TransientResourceDesc	desc;
		TransientState::Value	state;
		UniqueHandle<Texture>	texture;
		UniqueHandle<Buffer>	buffer;

		TransientResourceInstance()
		{}
		TransientResourceInstance(TransientResourceInstance&& rhs)
		{
			desc = rhs.desc;
			state = rhs.state;
			texture = std::move(rhs.texture);
			buffer = std::move(rhs.buffer);
		}
		TransientResourceInstance& operator=(TransientResourceInstance&& rhs) noexcept
		{
			desc = rhs.desc;
			state = rhs.state;
			texture = std::move(rhs.texture);
			buffer = std::move(rhs.buffer);
			return *this;
		}
	};

	struct ExternalResourceInstance
	{
		bool					bIsTexture;
		TransientState::Value	state;
		union
		{
			Texture*			pTexture;
			Buffer*				pBuffer;
		};
	};

	struct RenderGraphResource
	{
		bool					bIsTexture;
		union
		{
			Texture*			pTexture;
			Buffer*				pBuffer;
		};
	};
	
	class TransientResourceManager
	{
		friend class RenderGraph2;

		enum ResourceType
		{
			None,
			Transient,
			External,
		};
		
	public:
		TransientResourceManager(Device* pDev)
			: pDevice_(pDev)
		{}
		~TransientResourceManager();

		RenderGraphResource* GetRenderGraphResource(TransientResourceID id);
		
	private:
		void AddExternalTexture(TransientResourceID id, Texture* pTexture, TransientState::Value state);
		void AddExternalBuffer(TransientResourceID id, Buffer* pBuffer, TransientState::Value state);

		bool CommitResources(const std::vector<TransientResourceDesc>& descs, const std::map<TransientResourceID, u16>& idMap);

		ResourceType GetResourceInstance(TransientResourceID id, TransientResourceInstance*& OutTransient, ExternalResourceInstance*& OutExternal);
		TransientResourceInstance* GetTransientResourceInstance(TransientResourceID id);
		ExternalResourceInstance* GetExternalResourceInstance(TransientResourceID id);

	private:
		Device*		pDevice_ = nullptr;

		std::vector<std::unique_ptr<TransientResourceInstance>>								committedResources_;
		std::map<TransientResourceID, RenderGraphResource>									graphResources_;
		std::map<TransientResourceID, u16>													resourceIDMap_;
		std::multimap<TransientResourceDesc, std::unique_ptr<TransientResourceInstance>>	unusedResources_;
		std::map<TransientResourceID, ExternalResourceInstance>								externalResources_;
	};

	class IRenderPass
	{
	public:
		virtual ~IRenderPass() {}
		virtual std::vector<TransientResource> GetInputResources() const = 0;
		virtual std::vector<TransientResource> GetOutputResources() const = 0;
		virtual HardwareQueue::Value GetExecuteQueue() const = 0;
		virtual void Execute(CommandList* pCmdList, TransientResourceManager* pResManager) = 0;
	};

	class RenderGraph2
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
			u16						cmdListIndex;
			u16						passNodeID;
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
		RenderGraph2();
		~RenderGraph2();

		bool Initialize(Device* pDev);

		void ClearPasses();
		void AddPass(IRenderPass* pPass, IRenderPass* pParent);
		void AddPass(IRenderPass* pPass, const std::vector<IRenderPass*>& parents);

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
		typedef std::pair<u16, u16> GraphEdge;
		
	private:
		Device*							pDevice_ = nullptr;
		UniqueHandle<TransientResourceManager>	resManager_;
		std::vector<IRenderPass*>		renderPasses_;
		std::vector<GraphEdge>			graphEdges_;
		std::vector<u16>				sortedNodeIDs_;
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
