﻿#pragma once

#include <sl12/util.h>
#include <DirectXTex.h>
#include <memory>


namespace sl12
{
	class Device;
	class CommandList;
	class Swapchain;

	// テクスチャの次元
	struct TextureDimension
	{
		enum Type
		{
			Texture1D = 0,
			Texture2D,
			Texture3D,

			Max
		};
	};	// struct TextureDimension

	// テクスチャ生成時の記述情報
	struct TextureDesc
	{
		ResourceHeapAllocation	allocation				= ResourceHeapAllocation::Committed;
		TextureDimension::Type	dimension				= TextureDimension::Texture2D;
		u32						width = 1, height = 1, depth = 1;
		u32						mipLevels				= 1;
		DXGI_FORMAT				format					= DXGI_FORMAT_UNKNOWN;
		D3D12_RESOURCE_STATES	initialState			= D3D12_RESOURCE_STATE_COMMON;
		u32						sampleCount				= 1;
		u32						usage					= ResourceUsage::Unknown;
		float					clearColor[4]			= { 0.0f };
		float					clearDepth				= 1.0f;
		u8						clearStencil			= 0;
		bool					forceSysRam				= false;
		bool					deviceShared			= false;

		void Initialize1D(DXGI_FORMAT _format, u32 _width, u32 _mip, u32 _array, u32 _usage)
		{
			dimension = TextureDimension::Texture1D;
			format = _format;
			width = _width;
			height = 1;
			depth = _array > 0 ? _array : 1;
			mipLevels = _mip;
			usage = _usage;
		}
		void Initialize2D(DXGI_FORMAT _format, u32 _width, u32 _height, u32 _mip, u32 _array, u32 _usage)
		{
			dimension = TextureDimension::Texture2D;
			format = _format;
			width = _width;
			height = _height;
			depth = _array > 0 ? _array : 1;
			mipLevels = _mip;
			usage = _usage;
		}
		void Initialize3D(DXGI_FORMAT _format, u32 _width, u32 _height, u32 _depth, u32 _mip, u32 _usage)
		{
			dimension = TextureDimension::Texture3D;
			format = _format;
			width = _width;
			height = _height;
			depth = _depth;
			mipLevels = _mip;
			usage = _usage;
		}
		void InitializeCube(DXGI_FORMAT _format, u32 _width, u32 _height, u32 _depth, u32 _mip, u32 _array, u32 _usage)
		{
			dimension = TextureDimension::Texture2D;
			format = _format;
			width = height = _width;
			depth = _array > 0 ? _array * 6 : 6;
			mipLevels = _mip;
			usage = _usage;
		}
	};	// struct TextureDesc

	class Texture
	{
		friend class CommandList;

	public:
		Texture()
		{}
		~Texture()
		{
			Destroy();
		}

		bool Initialize(Device* pDev, const TextureDesc& desc);
		bool InitializeFromDXImage(Device* pDev, const DirectX::ScratchImage& image, bool isForceSRGB, bool forceSysRam = false);
		bool InitializeFromDDS(Device* pDev, CommandList* pCmdList, const void* pDdsBin, size_t size, sl12::u32 mipLevels, bool isForceSRGB, bool forceSysRam = false);
		bool InitializeFromTGA(Device* pDev, CommandList* pCmdList, const void* pTgaBin, size_t size, sl12::u32 mipLevels, bool isForceSRGB, bool forceSysRam = false);
		bool InitializeFromPNG(Device* pDev, CommandList* pCmdList, const void* pPngBin, size_t size, sl12::u32 mipLevels, bool isForceSRGB, bool forceSysRam = false);
		bool InitializeFromEXR(Device* pDev, CommandList* pCmdList, const void* pPngBin, size_t size, sl12::u32 mipLevels, bool forceSysRam = false);
		bool InitializeFromHDR(Device* pDev, CommandList* pCmdList, const void* pTgaBin, size_t size, sl12::u32 mipLevels, bool forceSysRam = false);
		bool InitializeFromImageBin(Device* pDev, CommandList* pCmdList, const TextureDesc& desc, const void* pImageBin);
		bool InitializeFromSwapchain(Device* pDev, Swapchain* pSwapchain, int bufferIndex);

		std::unique_ptr<DirectX::ScratchImage> InitializeFromDDSwoLoad(Device* pDev, const void* pTgaBin, size_t size, sl12::u32 mipLevels);
		std::unique_ptr<DirectX::ScratchImage> InitializeFromTGAwoLoad(Device* pDev, const void* pTgaBin, size_t size, sl12::u32 mipLevels);
		std::unique_ptr<DirectX::ScratchImage> InitializeFromPNGwoLoad(Device* pDev, const void* pPngBin, size_t size, sl12::u32 mipLevels);
		std::unique_ptr<DirectX::ScratchImage> InitializeFromEXRwoLoad(Device* pDev, const void* pPngBin, size_t size, sl12::u32 mipLevels);
		std::unique_ptr<DirectX::ScratchImage> InitializeFromHDRwoLoad(Device* pDev, const void* pTgaBin, size_t size, sl12::u32 mipLevels);

		bool UpdateImage(Device* pDev, CommandList* pCmdList, const DirectX::ScratchImage& image, ID3D12Resource** ppSrcImage);
		bool UpdateImage(Device* pDev, CommandList* pCmdList, const void* pImageBin, ID3D12Resource** ppSrcImage);

		void Destroy();

		// getter
		ID3D12Resource* GetResourceDep() { return pResource_; }
		const TextureDesc& GetTextureDesc() const { return textureDesc_; }
		const D3D12_RESOURCE_DESC& GetResourceDesc() const { return resourceDesc_; }

	private:
		ID3D12Resource*			pResource_{ nullptr };
		TextureDesc				textureDesc_{};
		D3D12_RESOURCE_DESC		resourceDesc_{};
		D3D12_CLEAR_VALUE		clearValue_{};
	};	// class Texture

}	// namespace sl12

//	EOF
