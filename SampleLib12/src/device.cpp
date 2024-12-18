﻿#include <sl12/device.h>

#include <sl12/util.h>
#include <sl12/swapchain.h>
#include <sl12/command_queue.h>
#include <sl12/descriptor_heap.h>
#include <sl12/texture.h>
#include <sl12/command_list.h>
#include <sl12/ring_buffer.h>
#include <string>

#include "sl12/texture_streamer.h"

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 614; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

namespace sl12
{
	LARGE_INTEGER CpuTimer::frequency_;

	//----
	Device::Device()
	{}

	//----
	Device::~Device()
	{
		Destroy();
	}

	//----
	bool Device::Initialize(const DeviceDesc& devDesc)
	{
		uint32_t factoryFlags = 0;
#ifdef _DEBUG
		// enable d3d debug layer.
		if (devDesc.enableDebugLayer)
		{
			ID3D12Debug* debugController;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
			{
				debugController->EnableDebugLayer();
				debugController->Release();
			}
		}
#endif
		// create factory.
		auto hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&pFactory_));
		if (FAILED(hr))
		{
			return false;
		}

		// enumerate adapters.
		bool isWarp = false;
		IDXGIAdapter1* pAdapter = nullptr;
		ID3D12Device* pDevice = nullptr;
		LatestDevice* pLatestDevice = nullptr;
		UINT adapterIndex = 0;
		while (true)
		{
			SafeRelease(pAdapter);
			SafeRelease(pDevice);

			hr = pFactory_->EnumAdapterByGpuPreference(adapterIndex++, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&pAdapter));
			if (FAILED(hr))
				break;

			DXGI_ADAPTER_DESC ad;
			pAdapter->GetDesc(&ad);

			hr = D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&pDevice));
			if (FAILED(hr))
				continue;

			bool isFeatureValid = true;
			if (devDesc.featureFlags & (FeatureFlag::RayTracing_1_0 | FeatureFlag::RayTracing_1_1))
			{
				D3D12_FEATURE_DATA_D3D12_OPTIONS5 Options5{};
				if (SUCCEEDED(pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &Options5, sizeof(Options5))))
				{
					if (devDesc.featureFlags & FeatureFlag::RayTracing_1_1)
					{
						isDxrSupported_ = Options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
					}
					else if (devDesc.featureFlags & FeatureFlag::RayTracing_1_0)
					{
						isDxrSupported_ = Options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0;
					}
				}
				if (!isDxrSupported_)
				{
					isFeatureValid = false;
				}
			}
			if (devDesc.featureFlags & FeatureFlag::MeshShader)
			{
				D3D12_FEATURE_DATA_D3D12_OPTIONS7 Options7{};
				if (SUCCEEDED(pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &Options7, sizeof(Options7))))
				{
					isMeshShaderSupported_ = Options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
				}
				if (!isMeshShaderSupported_)
				{
					isFeatureValid = false;
				}
			}
			if (devDesc.featureFlags & FeatureFlag::WorkGraph)
			{
				D3D12_FEATURE_DATA_D3D12_OPTIONS21 Options21{};
				if (SUCCEEDED(pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS21, &Options21, sizeof(Options21))))
				{
					isWorkGraphSupported_ = Options21.WorkGraphsTier != D3D12_WORK_GRAPHS_TIER_NOT_SUPPORTED;
				}
				if (!isWorkGraphSupported_)
				{
					isFeatureValid = false;
				}
			}
			if (isFeatureValid)
			{
				pDevice->QueryInterface(IID_PPV_ARGS(&pLatestDevice));
				break;
			}
		}
		if (!pDevice)
		{
			hr = pFactory_->EnumAdapters1(0, &pAdapter);
			if (FAILED(hr))
			{
				hr = pFactory_->EnumWarpAdapter(IID_PPV_ARGS(&pAdapter));
				if (FAILED(hr))
				{
					SafeRelease(pAdapter);
					return false;
				}
				isWarp = true;
			}

			hr = D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&pDevice));
			if (FAILED(hr))
				return false;
		}

		hr = pAdapter->QueryInterface(IID_PPV_ARGS(&pAdapter_));
		SafeRelease(pAdapter);
		if (FAILED(hr))
		{
			return false;
		}

		pDevice_ = pDevice;
		pLatestDevice_ = pLatestDevice;

		// enumerate displays.
		// NOTE: if multi GPU exists, only get Output from 0 index adapter.
		IDXGIOutput* pOutput{ nullptr };
		int OutputIndex = 0;
		bool enableHDR = devDesc.colorSpace != ColorSpaceType::Rec709;
		pFactory_->EnumAdapters1(0, &pAdapter);
		while (pAdapter->EnumOutputs(OutputIndex, &pOutput) != DXGI_ERROR_NOT_FOUND)
		{
			hr = pOutput->QueryInterface(IID_PPV_ARGS(&pOutput_));
			SafeRelease(pOutput);
			if (FAILED(hr))
			{
				SafeRelease(pOutput_);
				continue;
			}

			// get desc1.
			DXGI_OUTPUT_DESC1 OutDesc;
			pOutput_->GetDesc1(&OutDesc);

			if (!enableHDR)
			{
				// if HDR mode disabled, choose first output.
				desktopCoordinates_ = OutDesc.DesktopCoordinates;
				minLuminance_ = OutDesc.MinLuminance;
				maxLuminance_ = OutDesc.MaxLuminance;
				maxFullFrameLuminance_ = OutDesc.MaxFullFrameLuminance;
				colorSpaceType_ = ColorSpaceType::Rec709;
				break;
			}

			if (OutDesc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
			{
				desktopCoordinates_ = OutDesc.DesktopCoordinates;
				minLuminance_ = OutDesc.MinLuminance;
				maxLuminance_ = OutDesc.MaxLuminance;
				maxFullFrameLuminance_ = OutDesc.MaxFullFrameLuminance;
				colorSpaceType_ = ColorSpaceType::Rec2020;
				break;
			}

			SafeRelease(pOutput_);
			OutputIndex++;
		}
		// if HDR display not found, choose first output.
		if (!pOutput_)
		{
			pAdapter_->EnumOutputs(0, &pOutput);
			hr = pOutput->QueryInterface(IID_PPV_ARGS(&pOutput_));
			SafeRelease(pOutput);
			if (FAILED(hr))
			{
				SafeRelease(pOutput_);
				return false;
			}

			DXGI_OUTPUT_DESC1 OutDesc;
			pOutput_->GetDesc1(&OutDesc);

			desktopCoordinates_ = OutDesc.DesktopCoordinates;
			minLuminance_ = OutDesc.MinLuminance;
			maxLuminance_ = OutDesc.MaxLuminance;
			maxFullFrameLuminance_ = OutDesc.MaxFullFrameLuminance;
			colorSpaceType_ = ColorSpaceType::Rec709;
		}
		SafeRelease(pAdapter);

#ifdef _DEBUG
		// avoid COPY_DESCRIPTORS_INVALID_RANGES error.
		ID3D12InfoQueue* pD3DInfoQueue;
		if (SUCCEEDED(pDevice_->QueryInterface(__uuidof(ID3D12InfoQueue), reinterpret_cast<void**>(&pD3DInfoQueue))))
		{
#if 1
			// if break from error, enable this #if.
			pD3DInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
			pD3DInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
			pD3DInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
#endif

			D3D12_MESSAGE_ID blockedIds[] = { D3D12_MESSAGE_ID_COPY_DESCRIPTORS_INVALID_RANGES, D3D12_MESSAGE_ID_COMMAND_LIST_DRAW_INDEX_BUFFER_NOT_SET };
			D3D12_INFO_QUEUE_FILTER filter = {};
			filter.DenyList.pIDList = blockedIds;
			filter.DenyList.NumIDs = _countof(blockedIds);
			pD3DInfoQueue->AddRetrievalFilterEntries(&filter);
			pD3DInfoQueue->AddStorageFilterEntries(&filter);
			pD3DInfoQueue->Release();
		}
#endif

		// create queues.
		pGraphicsQueue_ = new CommandQueue();
		pComputeQueue_ = new CommandQueue();
		pCopyQueue_ = new CommandQueue();
		if (!pGraphicsQueue_ || !pComputeQueue_ || !pCopyQueue_)
		{
			return false;
		}
		if (!pGraphicsQueue_->Initialize(this, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_HIGH))
		{
			return false;
		}
		if (!pComputeQueue_->Initialize(this, D3D12_COMMAND_LIST_TYPE_COMPUTE))
		{
			return false;
		}
		if (!pCopyQueue_->Initialize(this, D3D12_COMMAND_LIST_TYPE_COPY, D3D12_COMMAND_QUEUE_PRIORITY_HIGH))
		{
			return false;
		}

		// check dynamic resource support.
		isDynamicResourceSupported_ = false;
		if (devDesc.enableDynamicResource)
		{
			D3D12_FEATURE_DATA_D3D12_OPTIONS featureOptions{};
			D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{};
			shaderModel.HighestShaderModel = D3D_SHADER_MODEL_6_6;
			if (SUCCEEDED(pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &featureOptions, sizeof(featureOptions)))
				&& SUCCEEDED(pDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))))
			{
				bool isTier3 = featureOptions.ResourceBindingTier == D3D12_RESOURCE_BINDING_TIER_3;
				isDynamicResourceSupported_ = isTier3;
			}
		}

		// create DescriptorHeaps
		{
			D3D12_DESCRIPTOR_HEAP_DESC desc{};
			desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			desc.NumDescriptors = 500000;
			desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			desc.NodeMask = 1;
			pGlobalViewDescHeap_ = new GlobalDescriptorHeap();
			if (!pGlobalViewDescHeap_->Initialize(this, desc))
			{
				return false;
			}

			desc.NumDescriptors = devDesc.numDescs[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV];
			desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			pViewDescHeap_ = new DescriptorAllocator();
			if (!pViewDescHeap_->Initialize(this, desc))
			{
				return false;
			}
			if (isDynamicResourceSupported_)
			{
				desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
				pDynamicViewDescHeap_ = new DescriptorAllocator();
				if (!pDynamicViewDescHeap_->Initialize(this, desc))
				{
					return false;
				}
			}

			desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			desc.NumDescriptors = devDesc.numDescs[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER];
			desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			pSamplerDescHeap_ = new DescriptorAllocator();
			if (!pSamplerDescHeap_->Initialize(this, desc))
			{
				return false;
			}
			if (isDynamicResourceSupported_)
			{
				desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
				pDynamicSamplerDescHeap_ = new DescriptorAllocator();
				if (!pDynamicSamplerDescHeap_->Initialize(this, desc))
				{
					return false;
				}
			}

			desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			desc.NumDescriptors = devDesc.numDescs[D3D12_DESCRIPTOR_HEAP_TYPE_RTV];
			desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			pRtvDescHeap_ = new DescriptorAllocator();
			if (!pRtvDescHeap_->Initialize(this, desc))
			{
				return false;
			}

			desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			desc.NumDescriptors = devDesc.numDescs[D3D12_DESCRIPTOR_HEAP_TYPE_DSV];
			desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			pDsvDescHeap_ = new DescriptorAllocator();
			if (!pDsvDescHeap_->Initialize(this, desc))
			{
				return false;
			}

			defaultViewDescInfo_ = pViewDescHeap_->Allocate();
			defaultSamplerDescInfo_ = pSamplerDescHeap_->Allocate();
		}

		// create swapchain.
		pSwapchain_ = new Swapchain();
		if (!pSwapchain_)
		{
			return false;
		}
		if (!pSwapchain_->Initialize(this, pGraphicsQueue_, devDesc.hWnd, devDesc.screenWidth, devDesc.screenHeight))
		{
			return false;
		}

		// create fence.
		hr = pDevice_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence_));
		if (FAILED(hr))
		{
			return false;
		}
		fenceValue_ = 1;

		fenceEvent_ = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);
		if (fenceEvent_ == nullptr)
		{
			return false;
		}

		// create copy ring buffer.
		pRingBuffer_ = std::make_unique<CopyRingBuffer>(this);
		// create texture stream allocator.
		pTextureStreamAllocator_ = std::make_unique<TextureStreamAllocator>(this);

		return true;
	}

	//----
	void Device::Destroy()
	{
		// before clear death list.
		pRingBuffer_.reset();
		pTextureStreamAllocator_.reset();

		// clear death list.
		SyncKillObjects(true);

		// shutdown system.
		dummyTextureViews_.clear();
		dummyTextures_.clear();

		SafeRelease(pFence_);

		SafeDelete(pSwapchain_);

		defaultSamplerDescInfo_.Free();
		defaultViewDescInfo_.Free();

		SafeDelete(pDsvDescHeap_);
		SafeDelete(pRtvDescHeap_);
		SafeDelete(pDynamicSamplerDescHeap_);
		SafeDelete(pSamplerDescHeap_);
		SafeDelete(pDynamicViewDescHeap_);
		SafeDelete(pViewDescHeap_);
		SafeDelete(pGlobalViewDescHeap_);

		SafeDelete(pGraphicsQueue_);
		SafeDelete(pComputeQueue_);
		SafeDelete(pCopyQueue_);

		SafeRelease(pLatestDevice_);

		SafeRelease(pDevice_);
		SafeRelease(pOutput_);
		SafeRelease(pAdapter_);
		SafeRelease(pFactory_);
	}

	//----
	void Device::Present(int syncInterval)
	{
		if (pSwapchain_)
		{
			pSwapchain_->Present(syncInterval);
		}
	}

	//----
	void Device::WaitDrawDone()
	{
		if (pGraphicsQueue_)
		{
			// 現在のFence値がコマンド終了後にFenceに書き込まれるようにする
			UINT64 fvalue = fenceValue_;
			pGraphicsQueue_->GetQueueDep()->Signal(pFence_, fvalue);
			fenceValue_++;

			// まだコマンドキューが終了していないことを確認する
			// ここまででコマンドキューが終了してしまうとイベントが一切発火されなくなるのでチェックしている
			if (pFence_->GetCompletedValue() < fvalue)
			{
				// このFenceにおいて、fvalue の値になったらイベントを発火させる
				pFence_->SetEventOnCompletion(fvalue, fenceEvent_);
				// イベントが発火するまで待つ
				WaitForSingleObject(fenceEvent_, INFINITE);
			}
		}

		// begin new frame.
		pRingBuffer_->BeginNewFrame();
	}

	//----
	void Device::WaitPresent()
	{
		if (pSwapchain_)
		{
			pSwapchain_->WaitPresent();
		}
	}

	//----
	bool Device::CreateDummyTextures(CommandList* pCmdList)
	{
		dummyTextures_.clear();
		dummyTextureViews_.clear();
		dummyTextures_.resize(DummyTex::Max);
		dummyTextureViews_.resize(DummyTex::Max);

		// Black
		{
			TextureDesc desc{};
			desc.initialState = D3D12_RESOURCE_STATE_COPY_DEST;
			desc.width = desc.height = 4;
			desc.depth = 1;
			desc.dimension = TextureDimension::Texture2D;
			desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.mipLevels = 1;

			std::vector<sl12::u32> bin;
			bin.resize(64 * 4);
			for (auto&& pix : bin)
			{
				pix = 0x00000000;
			}

			dummyTextures_[DummyTex::Black] = std::make_unique<Texture>();
			if (!dummyTextures_[DummyTex::Black]->InitializeFromImageBin(this, pCmdList, desc, bin.data()))
			{
				return false;
			}

			dummyTextureViews_[DummyTex::Black] = std::make_unique<TextureView>();
			if (!dummyTextureViews_[DummyTex::Black]->Initialize(this, dummyTextures_[DummyTex::Black].get()))
			{
				return false;
			}

		}

		// White
		{
			TextureDesc desc{};
			desc.initialState = D3D12_RESOURCE_STATE_COPY_DEST;
			desc.width = desc.height = 4;
			desc.depth = 1;
			desc.dimension = TextureDimension::Texture2D;
			desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.mipLevels = 1;

			std::vector<sl12::u32> bin;
			bin.resize(64 * 4);
			for (auto&& pix : bin)
			{
				pix = 0xFFFFFFFF;
			}

			dummyTextures_[DummyTex::White] = std::make_unique<Texture>();
			if (!dummyTextures_[DummyTex::White]->InitializeFromImageBin(this, pCmdList, desc, bin.data()))
			{
				return false;
			}

			dummyTextureViews_[DummyTex::White] = std::make_unique<TextureView>();
			if (!dummyTextureViews_[DummyTex::White]->Initialize(this, dummyTextures_[DummyTex::White].get()))
			{
				return false;
			}
		}

		// FlatNormal
		{
			TextureDesc desc{};
			desc.initialState = D3D12_RESOURCE_STATE_COPY_DEST;
			desc.width = desc.height = 4;
			desc.depth = 1;
			desc.dimension = TextureDimension::Texture2D;
			desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.mipLevels = 1;

			std::vector<sl12::u32> bin;
			bin.resize(64 * 4);
			for (auto&& pix : bin)
			{
				pix = 0xFFFF7F7F;
			}

			dummyTextures_[DummyTex::FlatNormal] = std::make_unique<Texture>();
			if (!dummyTextures_[DummyTex::FlatNormal]->InitializeFromImageBin(this, pCmdList, desc, bin.data()))
			{
				return false;
			}

			dummyTextureViews_[DummyTex::FlatNormal] = std::make_unique<TextureView>();
			if (!dummyTextureViews_[DummyTex::FlatNormal]->Initialize(this, dummyTextures_[DummyTex::FlatNormal].get()))
			{
				return false;
			}
		}

		for (auto&& tex : dummyTextures_)
		{
			pCmdList->AddTransitionBarrier(tex.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
		}
		pCmdList->FlushBarriers();

		return true;
	}

	//----
	void Device::CopyToBuffer(CommandList* pCmdList, Buffer* pDstBuffer, u32 dstOffset, const void* pSrcData, u32 srcSize)
	{
		pRingBuffer_->CopyToBuffer(pCmdList, pDstBuffer, dstOffset, pSrcData, srcSize);
	}

}	// namespace sl12

//	EOF
