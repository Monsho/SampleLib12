#include "sl12/resource_streaming_texture.h"
#include "sl12/device.h"
#include "sl12/file.h"
#include "sl12/string_util.h"
#include "sl12/command_list.h"
#include "sl12/command_queue.h"
#include <cctype>


namespace sl12
{

	namespace detail
	{
		struct UpdateTileQueueCommand
			: public IQueueCommand
		{
			Texture*										pTexture = nullptr;
			ID3D12Heap*										pHeap = nullptr;
			UINT											updatedRegions = 0;
			std::vector<D3D12_TILED_RESOURCE_COORDINATE>	startCoordinates;
			std::vector<D3D12_TILE_REGION_SIZE>				regionSizes;
			std::vector<D3D12_TILE_RANGE_FLAGS>				rangeFlags;
			std::vector<UINT>								heapRangeStartOffsets;
			std::vector<UINT>								rangeTileCounts;

			IRenderCommand*									pRenderCommand = nullptr;

			void ExecuteCommand(CommandQueue* pGraphicsQueue) override
			{
				ID3D12CommandQueue* pQueueDep = pGraphicsQueue->GetQueueDep();
				pQueueDep->UpdateTileMappings(
					pTexture->GetResourceDep(),
					updatedRegions,
					&startCoordinates[0],
					&regionSizes[0],
					pHeap,
					updatedRegions,
					&rangeFlags[0],
					&heapRangeStartOffsets[0],
					&rangeTileCounts[0],
					D3D12_TILE_MAPPING_FLAG_NONE
					);

				if (pRenderCommand)
				{
					std::unique_ptr<IRenderCommand> ptr(pRenderCommand);
					pGraphicsQueue->GetParentDevice()->AddRenderCommand(ptr);
				}
			}
		};

		struct TailMipInitRenderCommand
			: public IRenderCommand
		{
			std::unique_ptr<File>	texBin;
			Device*					pDevice;
			Texture*				pTexture;
			u32						firstMipLevel;
			u32						numMiplevels;

			void LoadCommand(CommandList* pCmdlist) override
			{
				// get texture footprint.
				auto resDesc = pTexture->GetResourceDesc();
				u32 firstSubresource = resDesc.DepthOrArraySize * firstMipLevel;
				u32 numSubresources = resDesc.DepthOrArraySize * numMiplevels;
				std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprint(numSubresources);
				std::vector<u32> numRows(numSubresources);
				std::vector<u64> rowSize(numSubresources);
				u64 totalSize;
				pDevice->GetDeviceDep()->GetCopyableFootprints(&resDesc, firstSubresource, numSubresources, 0, footprint.data(), numRows.data(), rowSize.data(), &totalSize);

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
					for (u32 m = 0; m < numMiplevels; m++)
					{
						size_t i = d * numMiplevels + m;
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
					dst.SubresourceIndex = firstSubresource + i;
					pCmdlist->GetLatestCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
				}
				pDevice->PendingKill(new ReleaseObjectItem<ID3D12Resource>(pSrcImage));

				pCmdlist->TransitionBarrier(pTexture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
			}
		};	// struct TailMipInitRenderCommand

		struct MiplevelUpRenderCommand
			: public IRenderCommand
		{
			ResourceItemStreamingTexture*	pResource;
			u32			prevMiplevel;
			u32			nextMiplevel;

			IQueueCommand*		pQueueCommand = nullptr;

			void LoadCommand(CommandList* pCmdlist) override
			{
				// swap texture view.
				pResource->currTextureView_ = std::move(pResource->nextTextureView_);

				if (pQueueCommand)
				{
					std::unique_ptr<IQueueCommand> ptr(pQueueCommand);
					pCmdlist->GetParentDevice()->AddQueueCommand(ptr);
				}
			}
		};	// struct MiplevelUpRenderCommand

		struct MiplevelDownRenderCommand
			: public IRenderCommand
		{
			std::vector<std::unique_ptr<File>>	texBins;
			ResourceItemStreamingTexture*	pResource;
			Device*		pDevice;
			// Texture*	pPrevTexture;
			// Texture*	pNextTexture;
			u32			prevMiplevel;
			u32			nextMiplevel;

			void LoadCommand(CommandList* pCmdlist) override
			{
				Texture* pTexture = &pResource->currTexture_;

				// get texture footprint.
				auto resDesc = pTexture->GetResourceDesc();
				u32 firstSubresource = resDesc.DepthOrArraySize * nextMiplevel;
				u32 numSubresources = resDesc.DepthOrArraySize * (prevMiplevel - nextMiplevel);
				std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprint(numSubresources);
				std::vector<u32> numRows(numSubresources);
				std::vector<u64> rowSize(numSubresources);
				u64 totalSize;
				pDevice->GetDeviceDep()->GetCopyableFootprints(&resDesc, firstSubresource, numSubresources, 0, footprint.data(), numRows.data(), rowSize.data(), &totalSize);

				pCmdlist->TransitionBarrier(pTexture, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);

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
					dst.pResource = pTexture->GetResourceDep();
					dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
					dst.SubresourceIndex = firstSubresource + i;
					pCmdlist->GetLatestCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
				}
				pDevice->PendingKill(new ReleaseObjectItem<ID3D12Resource>(pSrcImage));

				pCmdlist->TransitionBarrier(pTexture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);

				// swap texture view.
				pResource->currTextureView_ = std::move(pResource->nextTextureView_);
			}
		};	// struct MiplevelDownRenderCommand
	}

	//--------
	ResourceItemStreamingTexture::~ResourceItemStreamingTexture()
	{
		if (tailHeap_)
		{
			tailHeap_->Release();
		}
		for (auto handle : heapHandles_)
		{
			handle.Invalidate();
		}

		currTextureView_.Reset();
		currTexture_.Reset();
	}

	//--------
	u32 ResourceItemStreamingTexture::GetMipLevelFromMemSize(u32 memSize) const
	{
		u32 tileCount = memSize / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
		u32 mipCount = (u32)standardTiles_.size();
		for (u32 i = 0; i < mipCount; i++)
		{
			u32 t = standardTiles_[i].WidthInTiles * standardTiles_[i].HeightInTiles * standardTiles_[i].DepthInTiles;
			if (tileCount == t)
			{
				return i;
			}
		}
		return 0;
	}

	//--------
	void ResourceItemStreamingTexture::GetCurrentSize(u32& width, u32& height) const
	{
		width = currTexture_->GetTextureDesc().width;
		height = currTexture_->GetTextureDesc().height;
		for (u32 i = 0; i < currMiplevel_; i++)
		{
			width >>= 1;
			height >>= 1;
		}
	}

	//--------
	ResourceItemBase* ResourceItemStreamingTexture::LoadFunction(ResourceLoader* pLoader, ResourceHandle handle, const std::string& filepath)
	{
		auto device = pLoader->GetDevice();

		std::unique_ptr<ResourceItemStreamingTexture> ret(new ResourceItemStreamingTexture(handle));

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
		std::string debugName = GetFileNameWithoutExtent(filepath);
		TextureDesc desc{};
		desc.allocation = ResourceHeapAllocation::Reserved;
		desc.dimension = kDimension[static_cast<int>(pHeader->dimension)];
		desc.format = pHeader->format;
		desc.width = pHeader->width;
		desc.height = pHeader->height;
		desc.depth = pHeader->depth;
		desc.mipLevels = pHeader->mipLevels;
		desc.usage = ResourceUsage::ShaderResource;
		desc.initialState = D3D12_RESOURCE_STATE_COPY_DEST;
		desc.debugName = debugName.c_str();
		if (!ret->currTexture_->Initialize(device, desc))
		{
			return false;
		}
		// get tile info.
		{
			UINT numTiles;
			UINT subresourceCount = desc.mipLevels;
			std::vector<D3D12_SUBRESOURCE_TILING> tilings(subresourceCount);
			device->GetDeviceDep()->GetResourceTiling(ret->currTexture_->GetResourceDep(), &numTiles, &ret->packedMipInfo_, &ret->tileShape_, &subresourceCount, 0, &tilings[0]);
			ret->standardTiles_.resize(ret->packedMipInfo_.NumStandardMips);
			for (UINT8 i = 0; i < ret->packedMipInfo_.NumStandardMips; i++)
			{
				ret->standardTiles_[i] = tilings[i];
			}
		}

		// create first heap.
		{
			size_t numHeaps = pHeader->topMipCount + 1;
			ret->heapHandles_.resize(pHeader->topMipCount);
			u32 firstHeapIndex = pHeader->topMipCount;
			UINT tileCount = ret->packedMipInfo_.NumTilesForPackedMips;
			for (u32 i = pHeader->topMipCount; i < (u32)ret->packedMipInfo_.NumStandardMips; i++)
			{
				tileCount += ret->standardTiles_[i].WidthInTiles * ret->standardTiles_[i].HeightInTiles * ret->standardTiles_[i].DepthInTiles;
			}
			D3D12_HEAP_DESC heapDesc = {};
			heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
			heapDesc.SizeInBytes = tileCount * D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
			heapDesc.Alignment = 0;
			heapDesc.Flags = D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES;
			HRESULT hr = device->GetDeviceDep()->CreateHeap(&heapDesc, IID_PPV_ARGS(&ret->tailHeap_));
			if (FAILED(hr))
			{
				return false;
			}
		}

		// init texture view.
		ret->currTextureView_->Initialize(device, &ret->currTexture_, pHeader->topMipCount, pHeader->tailMipCount);

		// resource copy command.
		IRenderCommand* pRenderCommand = nullptr;
		{
			detail::TailMipInitRenderCommand* command = new detail::TailMipInitRenderCommand();
			command->texBin = std::move(texBin);
			command->pDevice = device;
			command->pTexture = &ret->currTexture_;
			command->firstMipLevel = pHeader->topMipCount;
			command->numMiplevels = pHeader->tailMipCount;
			pRenderCommand = command;
		}

		// execute update tile command.
		{
			detail::UpdateTileQueueCommand* command = new detail::UpdateTileQueueCommand();
			command->pTexture = &ret->currTexture_;
			command->pHeap = ret->tailHeap_;

			u32 updateMip = pHeader->topMipCount;
			UINT tileCount = 0;
			// standard mips tile.
			for (; updateMip < (u32)ret->packedMipInfo_.NumStandardMips; updateMip++)
			{
				D3D12_TILED_RESOURCE_COORDINATE coord = {};
				coord.Subresource = updateMip;
				command->startCoordinates.push_back(coord);

				D3D12_TILE_REGION_SIZE regionSize = {};
				regionSize.Width = ret->standardTiles_[updateMip].WidthInTiles;
				regionSize.Height = ret->standardTiles_[updateMip].HeightInTiles;
				regionSize.Depth = ret->standardTiles_[updateMip].DepthInTiles;
				regionSize.NumTiles = regionSize.Width * regionSize.Height * regionSize.Depth;
				regionSize.UseBox = TRUE;
				command->regionSizes.push_back(regionSize);

				command->rangeFlags.push_back(D3D12_TILE_RANGE_FLAG_NONE);
				command->heapRangeStartOffsets.push_back(tileCount);
				command->rangeTileCounts.push_back(regionSize.NumTiles);
				command->updatedRegions++;

				tileCount += regionSize.NumTiles;
			}
			// packed mips tile.
			if (ret->packedMipInfo_.NumPackedMips > 0)
			{
				D3D12_TILED_RESOURCE_COORDINATE coord = {};
				coord.Subresource = updateMip;
				command->startCoordinates.push_back(coord);

				D3D12_TILE_REGION_SIZE regionSize = {};
				regionSize.NumTiles = ret->packedMipInfo_.NumTilesForPackedMips;
				regionSize.UseBox = FALSE;
				command->regionSizes.push_back(regionSize);

				command->rangeFlags.push_back(D3D12_TILE_RANGE_FLAG_NONE);
				command->heapRangeStartOffsets.push_back(tileCount);
				command->rangeTileCounts.push_back(regionSize.NumTiles);
				command->updatedRegions++;
			}

			command->pRenderCommand = pRenderCommand;
			std::unique_ptr<IQueueCommand> ptr(command);
			device->AddQueueCommand(ptr);
		}

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
		if (pSTex->currMiplevel_ < nextMiplevel)
		{
			// if miplevel is upword, +1 miplevel only.
			nextMiplevel = pSTex->currMiplevel_ + 1;
		}

		u32 prevMiplevel = pSTex->currMiplevel_;

		TextureStreamAllocator* allocator = pDevice->GetTextureStreamAllocator();

		// miplevel up.
		if (prevMiplevel < nextMiplevel)
		{
			// load update tile command.
			IQueueCommand* pQueueCommand = nullptr;
			{
				detail::UpdateTileQueueCommand* command = new detail::UpdateTileQueueCommand();
				command->pTexture = &pSTex->currTexture_;

				for (u32 updateMip = prevMiplevel; updateMip < nextMiplevel; updateMip++)
				{
					if (!pSTex->heapHandles_[updateMip].IsValid())
					{
						continue;
					}

					pSTex->heapHandles_[updateMip].Invalidate();

					D3D12_TILED_RESOURCE_COORDINATE coord = {};
					coord.Subresource = updateMip;
					command->startCoordinates.push_back(coord);

					D3D12_TILE_REGION_SIZE regionSize = {};
					regionSize.Width = pSTex->standardTiles_[updateMip].WidthInTiles;
					regionSize.Height = pSTex->standardTiles_[updateMip].HeightInTiles;
					regionSize.Depth = pSTex->standardTiles_[updateMip].DepthInTiles;
					regionSize.NumTiles = regionSize.Width * regionSize.Height * regionSize.Depth;
					regionSize.UseBox = TRUE;
					command->regionSizes.push_back(regionSize);

					command->rangeFlags.push_back(D3D12_TILE_RANGE_FLAG_NULL);
					command->heapRangeStartOffsets.push_back(0);
					command->rangeTileCounts.push_back(regionSize.NumTiles);
					command->updatedRegions++;
				}
				pQueueCommand = command;
			}

			// load upword command.
			{
				detail::MiplevelUpRenderCommand* command = new detail::MiplevelUpRenderCommand();
				command->pResource = pSTex;
				command->prevMiplevel = prevMiplevel;
				command->nextMiplevel = nextMiplevel;
				command->pQueueCommand = pQueueCommand;

				pDevice->AddRenderCommand(std::unique_ptr<IRenderCommand>(command));
			}
		}
		// miplevel down.
		else
		{
			u32 updateMiplevel = prevMiplevel - 1;
			u32 numUpdateMips = prevMiplevel - nextMiplevel;

			// miplevel down command.
			IRenderCommand* pRenderCommand = nullptr;
			if (numUpdateMips > 0)
			{
				detail::MiplevelDownRenderCommand* command = new detail::MiplevelDownRenderCommand();
				command->pResource = pSTex;
				command->pDevice = pDevice;
				command->texBins.resize(prevMiplevel - nextMiplevel);
				command->prevMiplevel = prevMiplevel;
				command->nextMiplevel = nextMiplevel;

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
				pRenderCommand = command;
			}

			// allocate memory and update tile.
			for (u32 i = 0; i < numUpdateMips; i++, updateMiplevel--)
			{
				detail::UpdateTileQueueCommand* command = new detail::UpdateTileQueueCommand();

				D3D12_TILED_RESOURCE_COORDINATE coord = {};
				coord.Subresource = updateMiplevel;
				command->startCoordinates.push_back(coord);

				D3D12_TILE_REGION_SIZE regionSize = {};
				regionSize.Width = pSTex->standardTiles_[updateMiplevel].WidthInTiles;
				regionSize.Height = pSTex->standardTiles_[updateMiplevel].HeightInTiles;
				regionSize.Depth = pSTex->standardTiles_[updateMiplevel].DepthInTiles;
				regionSize.NumTiles = regionSize.Width * regionSize.Height * regionSize.Depth;
				regionSize.UseBox = TRUE;
				command->regionSizes.push_back(regionSize);

				if (!pSTex->heapHandles_[updateMiplevel].IsValid())
				{
					auto heapHandle = allocator->Allocate(pSTex->GetHandle(), regionSize.NumTiles * D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES);
					if (!heapHandle.IsValid())
					{
						numUpdateMips = i;
						nextMiplevel = prevMiplevel - i;
						break;
					}
					pSTex->heapHandles_[updateMiplevel] = heapHandle;
				}
				command->pTexture = &pSTex->currTexture_;
				command->pHeap = pSTex->heapHandles_[updateMiplevel].GetHeapDep();

				command->rangeFlags.push_back(D3D12_TILE_RANGE_FLAG_NONE);
				command->heapRangeStartOffsets.push_back(pSTex->heapHandles_[updateMiplevel].GetTileOffset());
				command->rangeTileCounts.push_back(regionSize.NumTiles);
				command->updatedRegions++;

				if (i == 0)
				{
					command->pRenderCommand = pRenderCommand;
				}

				std::unique_ptr<IQueueCommand> ptr(command);
				pDevice->AddQueueCommand(ptr);
			}
		}

		// init next texture view.
		UniqueHandle<TextureView> nextTexView = sl12::MakeUnique<TextureView>(pDevice);
		nextTexView->Initialize(pDevice, &pSTex->currTexture_, nextMiplevel, pSTex->currTexture_->GetTextureDesc().mipLevels - nextMiplevel);

		pSTex->nextTextureView_ = std::move(nextTexView);
		pSTex->currMiplevel_ = nextMiplevel;

		return true;
	}


}	// namespace sl12


//	EOF
