#pragma once

#include "sl12/util.h"
#include "sl12/types.h"
#include "sl12/resource_loader.h"


namespace sl12
{
	//--------
	class StreamTextureSet
	{
		friend class TextureStreamer;
		friend class StreamTextureSetHandle;
		
	private:
		std::vector<ResourceHandle>	handles;
	};	// class StreamTextureSet
	
	//--------
	class StreamTextureSetHandle
	{
		friend class TextureStreamer;

	public:
		StreamTextureSetHandle()
		{}
		StreamTextureSetHandle(const StreamTextureSetHandle& rhs)
			: pParentStreamer_(rhs.pParentStreamer_), id_(rhs.id_)
		{}
		
		bool IsValid() const;

	private:
		const StreamTextureSet* GetTextureSet() const;

	private:
		StreamTextureSetHandle(TextureStreamer* streamer, u64 id)
			: pParentStreamer_(streamer), id_(id)
		{}

		TextureStreamer*	pParentStreamer_ = nullptr;
		u64					id_ = 0;
	};	// class StreamTextureSetHandle
	
	//--------
	class TextureStreamer
	{
		friend class ResourceItemStreamingTexture;
		friend class StreamTextureSetHandle;
		
	public:
		TextureStreamer()
		{}
		~TextureStreamer();

		bool Initialize(Device* pDevice);
		void Destroy();

		StreamTextureSetHandle RegisterTextureSet(const std::vector<ResourceHandle>& textures);
		void RequestStreaming(StreamTextureSetHandle handle, u32 targetWidth);

	private:
		bool ThreadBody();
		const StreamTextureSet* GetTextureSetFromID(u64 id);
		
	private:
		struct RequestItem
		{
			StreamTextureSetHandle	handle;
			u32						targetWidth;
		};	// struct RequestItem

		Device*				pDevice_ = nullptr;
		std::atomic<u64>	handleID_ = 0;

		std::map<u64, std::unique_ptr<StreamTextureSet>>	texSetMap_;

		std::mutex				requestMutex_;
		std::mutex				listMutex_;
		std::condition_variable	requestCV_;
		std::thread				loadingThread_;
		std::list<RequestItem>	requestList_;
		bool					isAlive_ = false;
		bool					isLoading_ = false;
	};	// class TextureStreamer
	
} // namespace sl12

//	EOF
