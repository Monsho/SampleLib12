#include "sl12/resource_streaming_texture.h"
#include "sl12/device.h"
#include "sl12/file.h"
#include "sl12/string_util.h"
#include "sl12/command_list.h"
#include <cctype>


namespace sl12
{

	namespace
	{
		struct TailMipInitRenderCommand
			: public IRenderCommand
		{
			std::unique_ptr<File>	texBin;
			Device*					pDevice;
			Texture*				pTexture;

			~TailMipInitRenderCommand()
			{
			}

			void LoadCommand(CommandList* pCmdlist) override
			{
				// get texture footprint.
				auto resDesc = pTexture->GetResourceDesc();
				u32 numSubresources = resDesc.DepthOrArraySize * resDesc.MipLevels;
				std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprint(numSubresources);
				std::vector<u32> numRows(numSubresources);
				std::vector<u64> rowSize(numSubresources);
				u64 totalSize;
				pDevice->GetDeviceDep()->GetCopyableFootprints(&resDesc, 0, numSubresources, 0, footprint.data(), numRows.data(), rowSize.data(), &totalSize);

				// create upload buffer.
				D3D12_HEAP_PROPERTIES heapProp = {};
				heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;
				heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
				heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
				heapProp.CreationNodeMask = 1;
				heapProp.VisibleNodeMask = 1;

				D3D12_RESOURCE_DESC desc = {};
				desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
				desc.Alignment = 0;
				desc.Width = totalSize;
				desc.Height = 1;
				desc.DepthOrArraySize = 1;
				desc.MipLevels = 1;
				desc.Format = DXGI_FORMAT_UNKNOWN;
				desc.SampleDesc.Count = 1;
				desc.SampleDesc.Quality = 0;
				desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
				desc.Flags = D3D12_RESOURCE_FLAG_NONE;

				ID3D12Resource* pSrcImage{ nullptr };
				auto hr = pDevice->GetDeviceDep()->CreateCommittedResource(
					&heapProp,
					D3D12_HEAP_FLAG_NONE,
					&desc,
					D3D12_RESOURCE_STATE_GENERIC_READ,
					nullptr,
					IID_PPV_ARGS(&pSrcImage));
				assert(SUCCEEDED(hr));

				// get header.
				const u8* texPtr = reinterpret_cast<u8*>(texBin->GetData());
				const StreamingTextureHeader* pHeader = reinterpret_cast<const StreamingTextureHeader*>(texPtr);
				const StreamingSubresourceHeader* pSubHeader = reinterpret_cast<const StreamingSubresourceHeader*>(pHeader + 1);
				
				// ready copy source.
				u8* pData{ nullptr };
				hr = pSrcImage->Map(0, nullptr, reinterpret_cast<void**>(&pData));
				assert(SUCCEEDED(hr));

				for (u32 d = 0; d < resDesc.DepthOrArraySize; d++)
				{
					for (u32 m = 0; m < resDesc.MipLevels; m++)
					{
						size_t i = d * resDesc.MipLevels + m;
						assert(rowSize[i] < (SIZE_T)-1);
						D3D12_MEMCPY_DEST dstData = { pData + footprint[i].Offset, footprint[i].Footprint.RowPitch, footprint[i].Footprint.RowPitch * numRows[i] };
						const u8* pImage = texPtr + pSubHeader[i].offsetFromFileHead;
						if (rowSize[i] == footprint[i].Footprint.RowPitch)
						{
							memcpy(dstData.pData, pImage, pSubHeader[i].rowSize * numRows[i]);
						}
						else if (rowSize[i] < footprint[i].Footprint.RowPitch)
						{
							const u8* p_src = pImage;
							u8* p_dst = reinterpret_cast<u8*>(dstData.pData);
							for (u32 r = 0; r < numRows[i]; r++, p_src += pSubHeader[i].rowSize, p_dst += footprint[i].Footprint.RowPitch)
							{
								memcpy(p_dst, p_src, rowSize[i]);
							}
						}
					}
				}

				pSrcImage->Unmap(0, nullptr);

				// copy resource.
				for (u32 i = 0; i < numSubresources; i++)
				{
					D3D12_TEXTURE_COPY_LOCATION src, dst;
					src.pResource = pSrcImage;
					src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
					src.PlacedFootprint = footprint[i];
					dst.pResource = pTexture->GetResourceDep();
					dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
					dst.SubresourceIndex = i;
					pCmdlist->GetLatestCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
				}
				pDevice->PendingKill(new ReleaseObjectItem<ID3D12Resource>(pSrcImage));

				pCmdlist->TransitionBarrier(pTexture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
			}
		};	// struct TailMipInitRenderCommand

		struct MiplevelUpRenderCommand
			: public IRenderCommand
		{
			Texture*	pPrevTexture;
			Texture*	pNextTexture;
			u32			prevMiplevel;
			u32			nextMiplevel;

			~MiplevelUpRenderCommand()
			{
			}

			void LoadCommand(CommandList* pCmdlist) override
			{
				// get texture desc.
				auto prevDesc = pPrevTexture->GetResourceDesc();
				auto nextDesc = pNextTexture->GetResourceDesc();

				// copy mips.
				pCmdlist->TransitionBarrier(pPrevTexture, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_SOURCE);

				u32 srcSubresourceIndex = prevDesc.DepthOrArraySize * (nextMiplevel - prevMiplevel);
				u32 numSubresources = nextDesc.DepthOrArraySize * nextDesc.MipLevels;
				for (u32 i = 0; i < numSubresources; i++)
				{
					D3D12_TEXTURE_COPY_LOCATION src, dst;
					src.pResource = pPrevTexture->GetResourceDep();
					src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
					src.SubresourceIndex = srcSubresourceIndex++;
					dst.pResource = pNextTexture->GetResourceDep();
					dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
					dst.SubresourceIndex = i;
					pCmdlist->GetLatestCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
				}

				pCmdlist->TransitionBarrier(pNextTexture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
			}
		};	// struct MiplevelUpRenderCommand

		struct MiplevelDownRenderCommand
			: public IRenderCommand
		{
			std::vector<std::unique_ptr<File>>	texBins;
			Device*		pDevice;
			Texture*	pPrevTexture;
			Texture*	pNextTexture;
			u32			prevMiplevel;
			u32			nextMiplevel;

			~MiplevelDownRenderCommand()
			{
			}

			void LoadCommand(CommandList* pCmdlist) override
			{
				// get texture footprint.
				auto resDesc = pNextTexture->GetResourceDesc();
				u32 numSubresources = resDesc.DepthOrArraySize * (prevMiplevel - nextMiplevel);
				std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprint(numSubresources);
				std::vector<u32> numRows(numSubresources);
				std::vector<u64> rowSize(numSubresources);
				u64 totalSize;
				pDevice->GetDeviceDep()->GetCopyableFootprints(&resDesc, 0, numSubresources, 0, footprint.data(), numRows.data(), rowSize.data(), &totalSize);

				// copy high level mips.
				pCmdlist->TransitionBarrier(pPrevTexture, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_SOURCE);

				u32 dstSubresourceIndex = numSubresources;
				u32 numCopySubresource = resDesc.DepthOrArraySize * pPrevTexture->GetResourceDesc().MipLevels;
				for (u32 i = 0; i < numCopySubresource; i++)
				{
					D3D12_TEXTURE_COPY_LOCATION src, dst;
					src.pResource = pPrevTexture->GetResourceDep();
					src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
					src.SubresourceIndex = i;
					dst.pResource = pNextTexture->GetResourceDep();
					dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
					dst.SubresourceIndex = dstSubresourceIndex++;
					pCmdlist->GetLatestCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
				}

				// create upload buffer.
				D3D12_HEAP_PROPERTIES heapProp = {};
				heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;
				heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
				heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
				heapProp.CreationNodeMask = 1;
				heapProp.VisibleNodeMask = 1;

				D3D12_RESOURCE_DESC desc = {};
				desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
				desc.Alignment = 0;
				desc.Width = totalSize;
				desc.Height = 1;
				desc.DepthOrArraySize = 1;
				desc.MipLevels = 1;
				desc.Format = DXGI_FORMAT_UNKNOWN;
				desc.SampleDesc.Count = 1;
				desc.SampleDesc.Quality = 0;
				desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
				desc.Flags = D3D12_RESOURCE_FLAG_NONE;

				ID3D12Resource* pSrcImage{ nullptr };
				auto hr = pDevice->GetDeviceDep()->CreateCommittedResource(
					&heapProp,
					D3D12_HEAP_FLAG_NONE,
					&desc,
					D3D12_RESOURCE_STATE_GENERIC_READ,
					nullptr,
					IID_PPV_ARGS(&pSrcImage));
				assert(SUCCEEDED(hr));
				
				// ready copy source.
				u8* pData{ nullptr };
				hr = pSrcImage->Map(0, nullptr, reinterpret_cast<void**>(&pData));
				assert(SUCCEEDED(hr));
				u32 footprintIndex = 0;
				for (auto&& texBin : texBins)
				{
					const u8* texPtr = reinterpret_cast<u8*>(texBin->GetData());
					const StreamingSubresourceHeader* pSubHeader = reinterpret_cast<const StreamingSubresourceHeader*>(texPtr);

					u32 i = footprintIndex++;
					assert(rowSize[i] < (SIZE_T)-1);
					D3D12_MEMCPY_DEST dstData = { pData + footprint[i].Offset, footprint[i].Footprint.RowPitch, footprint[i].Footprint.RowPitch * numRows[i] };
					const u8* pImage = texPtr + pSubHeader->offsetFromFileHead;
					if (rowSize[i] == footprint[i].Footprint.RowPitch)
					{
						memcpy(dstData.pData, pImage, pSubHeader->rowSize * numRows[i]);
					}
					else if (rowSize[i] < footprint[i].Footprint.RowPitch)
					{
						const u8* p_src = pImage;
						u8* p_dst = reinterpret_cast<u8*>(dstData.pData);
						for (u32 r = 0; r < numRows[i]; r++, p_src += pSubHeader->rowSize, p_dst += footprint[i].Footprint.RowPitch)
						{
							memcpy(p_dst, p_src, rowSize[i]);
						}
					}
				}
				pSrcImage->Unmap(0, nullptr);

				// copy resource.
				for (u32 i = 0; i < numSubresources; i++)
				{
					D3D12_TEXTURE_COPY_LOCATION src, dst;
					src.pResource = pSrcImage;
					src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
					src.PlacedFootprint = footprint[i];
					dst.pResource = pNextTexture->GetResourceDep();
					dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
					dst.SubresourceIndex = i;
					pCmdlist->GetLatestCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
				}
				pDevice->PendingKill(new ReleaseObjectItem<ID3D12Resource>(pSrcImage));

				pCmdlist->TransitionBarrier(pNextTexture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
			}
		};	// struct MiplevelDownRenderCommand
	}

	//--------
	ResourceItemStreamingTexture::~ResourceItemStreamingTexture()
	{
		currTextureView_.Reset();
		currTexture_.Reset();
	}

	//--------
	ResourceItemBase* ResourceItemStreamingTexture::LoadFunction(ResourceLoader* pLoader, ResourceHandle handle, const std::string& filepath)
	{
		auto device = pLoader->GetDevice();

		std::unique_ptr<ResourceItemStreamingTexture> ret(new ResourceItemStreamingTexture());

		// load file.
		std::unique_ptr<File> texBin = std::make_unique<File>();
		if (!texBin->ReadFile(pLoader->MakeFullPath(filepath).c_str()))
		{
			return nullptr;
		}

		// store header.
		const u8* filePtr = reinterpret_cast<u8*>(texBin->GetData());
		const StreamingTextureHeader* pHeader = reinterpret_cast<const StreamingTextureHeader*>(filePtr);
		const StreamingSubresourceHeader* pSubHeader = reinterpret_cast<const StreamingSubresourceHeader*>(pHeader + 1);
		ret->streamingHeader_ = *pHeader;
		ret->currMiplevel_ = pHeader->topMipCount;

		// create resources.
		ret->currTexture_ = sl12::MakeUnique<Texture>(device);
		ret->currTextureView_ = sl12::MakeUnique<TextureView>(device);

		const TextureDimension::Type kDimension[] = {
			TextureDimension::Texture1D,
			TextureDimension::Texture2D,
			TextureDimension::Texture3D,
			TextureDimension::Texture2D,
		};

		// init texture.
		TextureDesc desc{};
		desc.dimension = kDimension[static_cast<int>(pHeader->dimension)];
		desc.format = pHeader->format;
		desc.width = pSubHeader->width;
		desc.height = pSubHeader->height;
		desc.depth = pHeader->depth;
		desc.mipLevels = pHeader->tailMipCount;
		desc.usage = ResourceUsage::ShaderResource;
		desc.initialState = D3D12_RESOURCE_STATE_COPY_DEST;
		if (!ret->currTexture_->Initialize(device, desc))
		{
			return false;
		}

		// init texture view.
		ret->currTextureView_->Initialize(device, &ret->currTexture_);
		
		TailMipInitRenderCommand* command = new TailMipInitRenderCommand();
		command->texBin = std::move(texBin);
		command->pDevice = device;
		command->pTexture = &ret->currTexture_;
		
		device->AddRenderCommand(std::unique_ptr<IRenderCommand>(command));
		return ret.release();
	}

	//--------
	u32 ResourceItemStreamingTexture::CalcMipLevel(u32 nextWidth)
	{
		u32 width = streamingHeader_.width;
		for (u32 mip = 0; mip < streamingHeader_.topMipCount; mip++)
		{
			if (nextWidth >= width)
			{
				return mip;
			}
			width >>= 1;
		}
		return streamingHeader_.topMipCount;
	}

	//--------
	bool ResourceItemStreamingTexture::ChangeMiplevel(Device* pDevice, ResourceItemStreamingTexture* pSTex, u32 nextWidth)
	{
		u32 nextMiplevel = pSTex->CalcMipLevel(nextWidth);
		if (pSTex->currMiplevel_ == nextMiplevel)
		{
			// no change mips.
			return true;
		}

		// create next texture desc.
		TextureDesc desc = pSTex->currTexture_->GetTextureDesc();
		desc.width = pSTex->streamingHeader_.width;
		desc.height = pSTex->streamingHeader_.height;
		for (u32 mip = 0; mip < nextMiplevel; mip++)
		{
			desc.width >>= 1;
			desc.height >>= 1;
		}
		desc.mipLevels = pSTex->streamingHeader_.mipLevels - nextMiplevel;
		desc.initialState = D3D12_RESOURCE_STATE_COPY_DEST;

		// init next texture.
		UniqueHandle<Texture> nextTex = sl12::MakeUnique<Texture>(pDevice);
		UniqueHandle<TextureView> nextTexView = sl12::MakeUnique<TextureView>(pDevice);
		if (!nextTex->Initialize(pDevice, desc))
		{
			return false;
		}
		nextTexView->Initialize(pDevice, &nextTex);

		// miplevel up.
		if (pSTex->currMiplevel_ < nextMiplevel)
		{
			MiplevelUpRenderCommand* command = new MiplevelUpRenderCommand();
			command->pPrevTexture = &pSTex->currTexture_;
			command->pNextTexture = &nextTex;
			command->prevMiplevel = pSTex->currMiplevel_;
			command->nextMiplevel = nextMiplevel;
		
			pDevice->AddRenderCommand(std::unique_ptr<IRenderCommand>(command));
		}
		// miplevel down.
		else
		{
			MiplevelDownRenderCommand* command = new MiplevelDownRenderCommand();
			command->pDevice = pDevice;
			command->texBins.resize(pSTex->currMiplevel_ - nextMiplevel);
			command->pPrevTexture = &pSTex->currTexture_;
			command->pNextTexture = &nextTex;
			command->prevMiplevel = pSTex->currMiplevel_;
			command->nextMiplevel = nextMiplevel;
			auto cmd = std::unique_ptr<IRenderCommand>(command);

			// file read.
			for (u32 i = 0; i < command->texBins.size(); i++)
			{
				auto file = std::make_unique<File>();
				u32 index = nextMiplevel + i;
				std::string index_str = std::to_string(index);
				index_str = std::string(std::max(0, 2 - (int)index_str.size()), '0') + index_str;
				std::string filepath = pSTex->fullPath_ + index_str;
				if (!file->ReadFile(filepath.c_str()))
				{
					return false;
				}

				command->texBins[i] = std::move(file);
			}
		
			pDevice->AddRenderCommand(cmd);
		}

		pSTex->currTexture_ = std::move(nextTex);
		pSTex->currTextureView_ = std::move(nextTexView);
		pSTex->currMiplevel_ = nextMiplevel;

		return true;
	}


}	// namespace sl12


//	EOF
