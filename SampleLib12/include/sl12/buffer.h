﻿#pragma once

#include <sl12/util.h>


namespace sl12
{
	class Device;
	class CommandList;

	struct BufferHeap
	{
		enum Type
		{
			Default,
			Dynamic,
			ReadBack,

			Max
		};
	};	// struct BufferHeap

	struct BufferDesc
	{
		size_t					size = 0;
		size_t					stride = 0;
		u32						usage = ResourceUsage::ConstantBuffer;
		BufferHeap::Type		heap = BufferHeap::Default;
		D3D12_RESOURCE_STATES	initialState = D3D12_RESOURCE_STATE_COMMON;
		bool					forceSysRam = false;
	};	// struct BufferDesc

	class Buffer
	{
		friend class CommandList;

	public:
		Buffer()
		{}
		~Buffer()
		{
			Destroy();
		}

		bool Initialize(Device* pDev, const BufferDesc& desc);
		void Destroy();

		void UpdateBuffer(Device* pDev, CommandList* pCmdList, const void* pData, size_t size, size_t offset = 0);

		void* Map();
		void* Map(const D3D12_RANGE& range);
		void Unmap();
		void Unmap(const D3D12_RANGE& range);

		// getter
		const BufferDesc& GetBufferDesc() const { return bufferDesc_; }
		ID3D12Resource* GetResourceDep() { return pResource_; }
		const D3D12_RESOURCE_DESC& GetResourceDesc() const { return resourceDesc_; }

	private:
		ID3D12Resource*			pResource_ = nullptr;
		BufferDesc				bufferDesc_ = {};
		D3D12_HEAP_PROPERTIES	heapProp_ = {};
		D3D12_RESOURCE_DESC		resourceDesc_ = {};
	};	// class Buffer

}	// namespace sl12

//	EOF
