#include <sl12/buffer.h>

#include <sl12/device.h>
#include <sl12/command_list.h>
#include <sl12/fence.h>


namespace sl12
{
	//----
	bool Buffer::Initialize(Device* pDev, const BufferDesc& desc)
	{
		if (desc.usage & (ResourceUsage::RenderTarget | ResourceUsage::DepthStencil))
		{
			return false;
		}
		
		static const D3D12_HEAP_TYPE kHeapTypes[] = {
			D3D12_HEAP_TYPE_DEFAULT,
			D3D12_HEAP_TYPE_UPLOAD,
			D3D12_HEAP_TYPE_READBACK,
		};
		D3D12_HEAP_TYPE heapType = kHeapTypes[desc.heap];

		size_t allocSize = desc.size;
		if (desc.usage & ResourceUsage::ConstantBuffer)
		{
			allocSize = GetAlignedSize(allocSize, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
		}

		// ByteAddressBufferの場合はR32_TYPELESSに設定する
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

		D3D12_HEAP_PROPERTIES heapProp{};
		heapProp.Type = heapType;
		heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProp.CreationNodeMask = 1;
		heapProp.VisibleNodeMask = 1;

		if (desc.forceSysRam)
		{
			const D3D12_CPU_PAGE_PROPERTY  kCpuPages[] = {
				D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE,
				D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE,
				D3D12_CPU_PAGE_PROPERTY_WRITE_BACK,
			};
			heapProp.Type = D3D12_HEAP_TYPE_CUSTOM;
			heapProp.CPUPageProperty = kCpuPages[desc.heap];
			heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
		}

		D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE;
		if (desc.deviceShared)
		{
			flags = D3D12_HEAP_FLAG_SHARED;
		}
		
		D3D12_RESOURCE_DESC resDesc{};
		resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resDesc.Alignment = 0;
		resDesc.Width = allocSize;
		resDesc.Height = 1;
		resDesc.DepthOrArraySize = 1;
		resDesc.MipLevels = 1;
		resDesc.Format = format;
		resDesc.SampleDesc.Count = 1;
		resDesc.SampleDesc.Quality = 0;
		resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resDesc.Flags = (desc.usage & ResourceUsage::UnorderedAccess) ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;

		auto hr = pDev->GetDeviceDep()->CreateCommittedResource(&heapProp, flags, &resDesc, desc.initialState, nullptr, IID_PPV_ARGS(&pResource_));
		if (FAILED(hr))
		{
			return false;
		}

		bufferDesc_ = desc;
		resourceDesc_ = resDesc;
		heapProp_ = heapProp;
		return true;
	}

	//----
	void Buffer::Destroy()
	{
		SafeRelease(pResource_);
	}

	//----
	void Buffer::UpdateBuffer(Device* pDev, CommandList* pCmdList, const void* pData, size_t size, size_t offset)
	{
		if (!pDev || !pCmdList)
		{
			return;
		}
		if (!pData || !size)
		{
			return;
		}
		if (offset + size > bufferDesc_.size)
		{
			return;
		}

		if (bufferDesc_.heap == BufferHeap::Dynamic)
		{
			D3D12_RANGE range{};
			range.Begin = offset;
			range.End = offset + size;
			void* p = Map(range);
			memcpy(p, pData, size);
			Unmap(range);
		}
		else
		{
			BufferDesc tmpDesc{};
			tmpDesc.size = size;
			tmpDesc.usage = ResourceUsage::Unknown;
			tmpDesc.heap = BufferHeap::Dynamic;
			Buffer* src = new Buffer();
			if (!src->Initialize(pDev, tmpDesc))
			{
				return;
			}
			src->UpdateBuffer(pDev, pCmdList, pData, size, 0);

			pCmdList->GetCommandList()->CopyBufferRegion(pResource_, offset, src->pResource_, 0, size);
			pDev->KillObject(src);
		}

	}

	//----
	void* Buffer::Map()
	{
		if (!pResource_)
		{
			return nullptr;
		}

		void* pData{ nullptr };
		auto hr = pResource_->Map(0, nullptr, &pData);
		if (FAILED(hr))
		{
			return nullptr;
		}
		return pData;
	}
	void* Buffer::Map(const D3D12_RANGE& range)
	{
		if (!pResource_)
		{
			return nullptr;
		}

		void* pData{ nullptr };
		auto hr = pResource_->Map(0, &range, &pData);
		if (FAILED(hr))
		{
			return nullptr;
		}
		return (u8*)pData + range.Begin;
	}

	//----
	void Buffer::Unmap()
	{
		if (!pResource_)
		{
			return;
		}

		pResource_->Unmap(0, nullptr);
	}
	void Buffer::Unmap(const D3D12_RANGE& range)
	{
		if (!pResource_)
		{
			return;
		}

		pResource_->Unmap(0, &range);
	}

}	// namespace sl12

//	EOF
