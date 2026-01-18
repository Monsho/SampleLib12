#include <sl12/rtxgi_component.h>

#include <rtxgi/ddgi/DDGIVolume.h>
#include <rtxgi/ddgi/gfx/DDGIVolume_D3D12.h>

#include <sl12/shader_manager.h>
#include <sl12/command_list.h>
#include <sl12/device.h>
#include <sl12/shader.h>


namespace
{
	static const char*	kProbeNumTexelDefine = "RTXGI_DDGI_PROBE_NUM_TEXELS";
	static const char*	kRaysPerProbeDefine = "RTXGI_DDGI_BLEND_RAYS_PER_PROBE";
	static const char*	kRadianceDefine = "RTXGI_DDGI_BLEND_RADIANCE";
	static const char*	kSharedMemoryDefine = "RTXGI_DDGI_BLEND_SHARED_MEMORY";
	static const char*	kOutputRegDefine = "OUTPUT_REGISTER";

	static const char*	kBlendingShaderFile = "ProbeBlendingCS.hlsl";
	static const char*	kBorderUpdateShaderFile = "ProbeBorderUpdateCS.hlsl";
	static const char*	kClassificationShaderFile = "ProbeClassificationCS.hlsl";
	static const char*	kRelocationShaderFile = "ProbeRelocationCS.hlsl";

	static const char*	kShaderEntryPoints[] = {
		"DDGIProbeBlendingCS",
		"DDGIProbeBlendingCS",
		"DDGIProbeRelocationCS",
		"DDGIProbeRelocationResetCS",
		"DDGIProbeClassificationCS",
		"DDGIProbeClassificationResetCS",
		"DDGIReductionCS",
		"DDGIExtraReductionCS",
	};

	static const UINT kSRVStartInDescriptorHeap = 2;
}

namespace sl12
{
	//----
	RtxgiComponent::RtxgiComponent(Device* p, const std::string& shaderDir)
		: pParentDevice_(p)
		, shaderDirectory_(shaderDir)
	{}

	//----
	RtxgiComponent::~RtxgiComponent()
	{
		Destroy();
	}

	//----
	bool RtxgiComponent::Initialize(ShaderManager* pManager, const RtxgiVolumeDesc* descs, int numVolumes)
	{
		if (numVolumes != 1)
		{
			sl12::ConsolePrint("[RTXGI Error] current RTXGI component support only one volume.");
			return false;
		}

		if (!descs)
			return false;
		if (!pParentDevice_ || !pManager)
			return false;

		// set volume descriptor.
		rtxgi::DDGIVolumeDesc ddgiDesc;
		name_ = descs->name;
		ddgiDesc.name = const_cast<char*>(name_.c_str());
		ddgiDesc.index = 0; // ボリュームのインデックス番号
		ddgiDesc.rngSeed = 0; // ランダムシード
		ddgiDesc.origin = { descs->origin.x, descs->origin.y, descs->origin.z }; // 中心座標
		ddgiDesc.eulerAngles = { descs->angle.x, descs->angle.y, descs->angle.z }; // オイラー角
		ddgiDesc.probeSpacing = { descs->probeSpacing.x, descs->probeSpacing.y, descs->probeSpacing.z }; // プローブの間隔
		ddgiDesc.probeCounts = { descs->probeCount.x, descs->probeCount.y, descs->probeCount.z }; // プローブの数
		ddgiDesc.probeNumRays = descs->numRays; // プローブのレイの数
		ddgiDesc.probeNumIrradianceTexels = descs->numIrradianceTexels; // Irradianceテクスチャのテクセル数
		ddgiDesc.probeNumIrradianceInteriorTexels = (ddgiDesc.probeNumIrradianceTexels - 2);
		ddgiDesc.probeNumDistanceTexels = descs->numDistanceTexels; // Distanceテクスチャのテクセル数
		ddgiDesc.probeNumDistanceInteriorTexels = (ddgiDesc.probeNumDistanceTexels - 2);
		ddgiDesc.probeHysteresis = 0.97f; // ヒステリシスのデフォルト値
		ddgiDesc.probeNormalBias = 0.1f; // ノーマルバイアスのデフォルト値
		ddgiDesc.probeViewBias = 0.1f; // ビューバイアスのデフォルト値
		ddgiDesc.probeMaxRayDistance = descs->maxRayDistance; // 最大レイ距離
		ddgiDesc.probeDistanceExponent = descs->distanceExponent; // 深度テストのExponent
		ddgiDesc.probeIrradianceThreshold = descs->irradianceThreshold; // Irradianceしきい値
		ddgiDesc.probeBrightnessThreshold = descs->brightnessThreshold; // Brightnessしきい値

		ddgiDesc.showProbes = true; // プローブ表示フラグ
		ddgiDesc.probeVisType = rtxgi::EDDGIVolumeProbeVisType::Default;

		if (!descs->enableHighPrecisionFormat)
		{
			// 標準精度フォーマット
			ddgiDesc.probeRayDataFormat = rtxgi::EDDGIVolumeTextureFormat::F32x2;
			ddgiDesc.probeIrradianceFormat = rtxgi::EDDGIVolumeTextureFormat::U32;
			ddgiDesc.probeDistanceFormat = rtxgi::EDDGIVolumeTextureFormat::F16x2;
			ddgiDesc.probeDataFormat = rtxgi::EDDGIVolumeTextureFormat::F16x4;
			ddgiDesc.probeVariabilityFormat = rtxgi::EDDGIVolumeTextureFormat::F16;
		}
		else
		{
			// 高精度フォーマット
			ddgiDesc.probeRayDataFormat = rtxgi::EDDGIVolumeTextureFormat::F32x4;
			ddgiDesc.probeIrradianceFormat = rtxgi::EDDGIVolumeTextureFormat::F32x4;
			ddgiDesc.probeDistanceFormat = rtxgi::EDDGIVolumeTextureFormat::F16x2;
			ddgiDesc.probeDataFormat = rtxgi::EDDGIVolumeTextureFormat::F16x4;
			ddgiDesc.probeVariabilityFormat = rtxgi::EDDGIVolumeTextureFormat::F16;
		}

		ddgiDesc.probeRelocationEnabled = descs->enableRelocation;
		ddgiDesc.probeMinFrontfaceDistance = 0.1f;
		ddgiDesc.probeClassificationEnabled = descs->enableClassification;
		ddgiDesc.probeVariabilityEnabled = descs->enableVariability;

		ddgiDesc.movementType = rtxgi::EDDGIVolumeMovementType::Default;

		// initialize shaders.
		if (!InitializeShaders(pManager, ddgiDesc))
		{
			sl12::ConsolePrint("[RTXGI Error] shader compile error.");
			return false;
		}

		// new volume instance.
		ddgiVolume_.reset(new rtxgi::d3d12::DDGIVolume());

		// descriptor heap.
		auto p_device_dep = pParentDevice_->GetDeviceDep();
		{
			D3D12_DESCRIPTOR_HEAP_DESC desc;
			desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			desc.NumDescriptors =
				1			// constants structured buffer count
				+ 1			// resource indices index count
				+ rtxgi::GetDDGIVolumeNumTex2DArrayDescriptors() * numVolumes * 2; // SRV + UAV
			desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			desc.NodeMask = 0x01;
			auto hr = p_device_dep->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pSrvDescriptorHeap_));
			if (FAILED(hr))
			{
				sl12::ConsolePrint("[RTXGI Error] failed to create CBV/SRV descriptor heap.");
				return false;
			}

			desc.NumDescriptors = rtxgi::GetDDGIVolumeNumRTVDescriptors() * numVolumes;
			desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

			// Create the RTV heap
			hr = p_device_dep->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pRtvDescriptorHeap_));
			if (FAILED(hr))
			{
				sl12::ConsolePrint("[RTXGI Error] failed to create RTV descriptor heap.");
				return false;
			}
		}

		// DDGI resources
		ddgiVolumeResource_ = std::make_unique<rtxgi::d3d12::DDGIVolumeResources>();
		auto& descHeap = ddgiVolumeResource_->descriptorHeap;
		descHeap.resources = pSrvDescriptorHeap_;
		descHeap.entrySize = p_device_dep->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		descHeap.constantsIndex = 0;
		descHeap.resourceIndicesIndex = 1;
		static const UINT kSRVStart = kSRVStartInDescriptorHeap;
		static const UINT kUAVStart = kSRVStart + rtxgi::GetDDGIVolumeNumTex2DArrayDescriptors();
		descHeap.resourceIndices.rayDataSRVIndex = kSRVStart + 0;
		descHeap.resourceIndices.rayDataUAVIndex = kUAVStart + 0;
		descHeap.resourceIndices.probeIrradianceSRVIndex = kSRVStart + 1;
		descHeap.resourceIndices.probeIrradianceUAVIndex = kUAVStart + 1;
		descHeap.resourceIndices.probeDistanceSRVIndex = kSRVStart + 2;
		descHeap.resourceIndices.probeDistanceUAVIndex = kUAVStart + 2;
		descHeap.resourceIndices.probeDataSRVIndex = kSRVStart + 3;
		descHeap.resourceIndices.probeDataUAVIndex = kUAVStart + 3;
		descHeap.resourceIndices.probeVariabilitySRVIndex = kSRVStart + 4;
		descHeap.resourceIndices.probeVariabilityUAVIndex = kUAVStart + 4;
		descHeap.resourceIndices.probeVariabilityAverageSRVIndex = kSRVStart + 5;
		descHeap.resourceIndices.probeVariabilityAverageUAVIndex = kUAVStart + 5;

		// unmanaged mode + no bindless.
		ddgiVolumeResource_->managed.enabled = false;
		ddgiVolumeResource_->bindless.enabled = false;
		ddgiVolumeResource_->unmanaged.enabled = true;
		ddgiVolumeResource_->unmanaged.rootParamSlotRootConstants = 0;
		ddgiVolumeResource_->unmanaged.rootParamSlotResourceDescriptorTable = 1;

		// constant buffer.
		{
			auto stride = sizeof(rtxgi::DDGIVolumeDescGPUPacked);
			auto size = stride * numVolumes;
			
			BufferDesc creationDesc{};
			creationDesc.size = size;
			creationDesc.stride = stride;
			creationDesc.usage = ResourceUsage::ShaderResource;
			creationDesc.heap = BufferHeap::Default;
			creationDesc.initialState = D3D12_RESOURCE_STATE_COMMON;
			if (!constantSTB_.Initialize(pParentDevice_, creationDesc))
			{
				return false;
			}

			creationDesc.size = size * 2;
			creationDesc.heap = BufferHeap::Dynamic;
			creationDesc.initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
			if (!constantSTBUpload_.Initialize(pParentDevice_, creationDesc))
			{
				return false;
			}
			ddgiVolumeResource_->constantsBuffer = constantSTB_.GetResourceDep();
			ddgiVolumeResource_->constantsBufferUpload = constantSTBUpload_.GetResourceDep();
			ddgiVolumeResource_->constantsBufferSizeInBytes = size;

			if (!constantSTBView_.Initialize(pParentDevice_, &constantSTB_, 0, (u32)numVolumes, (u32)stride))
			{
				return false;
			}

			// create SRV for RTXGI.
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Buffer.NumElements = numVolumes;
			srvDesc.Buffer.StructureByteStride = (UINT)stride;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

			auto p_device_dep = pParentDevice_->GetDeviceDep();
			UINT srvDescSize = p_device_dep->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			D3D12_CPU_DESCRIPTOR_HANDLE handle;
			handle = ddgiVolumeResource_->descriptorHeap.resources->GetCPUDescriptorHandleForHeapStart();
			handle.ptr += (ddgiVolumeResource_->descriptorHeap.constantsIndex * srvDescSize);
			p_device_dep->CreateShaderResourceView(constantSTB_.GetResourceDep(), &srvDesc, handle);
		}

		// create texture resources.
		if (!CreateTextures(ddgiDesc, ddgiVolumeResource_.get()))
			return false;

		// create root signature and psos.
		if (!CreatePipelines(ddgiVolumeResource_.get()))
			return false;

		// create ddgi volume.
		auto status = ddgiVolume_->Create(ddgiDesc, *ddgiVolumeResource_.get());
		if (status != rtxgi::ERTXGIStatus::OK)
		{
			return false;
		}

		return true;
	}

	//----
	bool RtxgiComponent::InitializeShaders(ShaderManager* pManager, const rtxgi::DDGIVolumeDesc& ddgiDesc)
	{
		ShaderHandle handles[EShaderType::Max];

		// create base defines.
		std::vector<ShaderDefine> baseDefines;
		baseDefines.push_back(ShaderDefine("HLSL", ""));									// HLSL

		baseDefines.push_back(ShaderDefine("RTXGI_DDGI_RESOURCE_MANAGEMENT", "0"));			// unmanaged mode.
		baseDefines.push_back(ShaderDefine("RTXGI_COORDINATE_SYSTEM", "2"));				// right hand y-up.
		baseDefines.push_back(ShaderDefine("RTXGI_DDGI_SHADER_REFLECTION", "0"));			// unuse shader reflection.
		baseDefines.push_back(ShaderDefine("RTXGI_DDGI_BINDLESS_RESOURCES", "0"));			// no bindless resources.
		baseDefines.push_back(ShaderDefine("RTXGI_DDGI_DEBUG_PROBE_INDEXING", "0"));		// no debug.
		baseDefines.push_back(ShaderDefine("RTXGI_DDGI_DEBUG_OCTAHEDRAL_INDEXING", "0"));	// no debug.
		baseDefines.push_back(ShaderDefine("RTXGI_DDGI_DEBUG_BORDER_COPY_INDEXING", "0"));	// no debug.

		// register settings.
		baseDefines.push_back(ShaderDefine("CONSTS_REGISTER", "b0"));
		baseDefines.push_back(ShaderDefine("CONSTS_SPACE", "space1"));
		baseDefines.push_back(ShaderDefine("VOLUME_CONSTS_REGISTER", "t0"));
		baseDefines.push_back(ShaderDefine("VOLUME_CONSTS_SPACE", "space1"));
		baseDefines.push_back(ShaderDefine("RAY_DATA_REGISTER", "u0"));
		baseDefines.push_back(ShaderDefine("RAY_DATA_SPACE", "space1"));
		baseDefines.push_back(ShaderDefine("OUTPUT_SPACE", "space1"));
		baseDefines.push_back(ShaderDefine("PROBE_DATA_REGISTER", "u3"));
		baseDefines.push_back(ShaderDefine("PROBE_DATA_SPACE", "space1"));
		baseDefines.push_back(ShaderDefine("PROBE_VARIABILITY_SPACE", "space1"));
		baseDefines.push_back(ShaderDefine("PROBE_VARIABILITY_REGISTER", "u4"));
		baseDefines.push_back(ShaderDefine("PROBE_VARIABILITY_AVERAGE_REGISTER", "u5"));

#define RTXGI_DDGI_BLEND_SHARED_MEMORY 0

		std::string rays_text = std::to_string(ddgiDesc.probeNumRays);
		std::string irr_text = std::to_string(ddgiDesc.probeNumIrradianceTexels);
		std::string irr_int_text = std::to_string(ddgiDesc.probeNumIrradianceInteriorTexels);
		std::string dist_text = std::to_string(ddgiDesc.probeNumDistanceTexels);
		std::string dist_int_text = std::to_string(ddgiDesc.probeNumDistanceInteriorTexels);
		std::string sh_mem_text = std::to_string(RTXGI_DDGI_BLEND_SHARED_MEMORY);
		std::string scl_sh_mem_text = std::to_string(ddgiDesc.probeBlendingUseScrollSharedMemory);
		std::string wave_lane_text = std::to_string(32);

		std::string file_dir = shaderDirectory_;
		if (file_dir[file_dir.length() - 1] != '\\' || file_dir[file_dir.length() - 1] != '/')
		{
			file_dir += "/";
		}


		// blending irradiance shader.
		{
			auto defines = baseDefines;
			defines.push_back(ShaderDefine("RTXGI_DDGI_BLEND_RADIANCE", "1"));
			defines.push_back(ShaderDefine("RTXGI_DDGI_PROBE_NUM_TEXELS", irr_text.c_str()));
			defines.push_back(ShaderDefine("RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS", irr_int_text.c_str()));
			defines.push_back(ShaderDefine("RTXGI_DDGI_BLEND_SHARED_MEMORY", sh_mem_text.c_str()));
#if RTXGI_DDGI_BLEND_SHARED_MEMORY
			defines.push_back(ShaderDefine("RTXGI_DDGI_BLEND_RAYS_PER_PROBE", rays_text.c_str()));
#endif
			defines.push_back(ShaderDefine("RTXGI_DDGI_BLEND_SCROLL_SHARED_MEMORY", scl_sh_mem_text.c_str()));
			defines.push_back(ShaderDefine("OUTPUT_REGISTER", "u1"));

			handles[EShaderType::IrradianceBlending] = pManager->CompileFromFile(
				file_dir + kBlendingShaderFile,
				kShaderEntryPoints[EShaderType::IrradianceBlending],
				ShaderType::Compute, 6, 6, nullptr, &defines);
		}

		// blending distance shader.
		{
			auto defines = baseDefines;
			defines.push_back(ShaderDefine("RTXGI_DDGI_BLEND_RADIANCE", "0"));
			defines.push_back(ShaderDefine("RTXGI_DDGI_PROBE_NUM_TEXELS", dist_text.c_str()));
			defines.push_back(ShaderDefine("RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS", dist_int_text.c_str()));
			defines.push_back(ShaderDefine("RTXGI_DDGI_BLEND_SHARED_MEMORY", sh_mem_text.c_str()));
#if RTXGI_DDGI_BLEND_SHARED_MEMORY
			defines.push_back(ShaderDefine("RTXGI_DDGI_BLEND_RAYS_PER_PROBE", numRays.c_str()));
#endif
			defines.push_back(ShaderDefine("RTXGI_DDGI_BLEND_SCROLL_SHARED_MEMORY", scl_sh_mem_text.c_str()));
			defines.push_back(ShaderDefine("OUTPUT_REGISTER", "u2"));

			handles[EShaderType::DistanceBlending] = pManager->CompileFromFile(
				file_dir + kBlendingShaderFile,
				kShaderEntryPoints[EShaderType::DistanceBlending],
				ShaderType::Compute, 6, 6, nullptr, &defines);
		}

		// relocation shader.
		{
			handles[EShaderType::ProbeRelocation] = pManager->CompileFromFile(
				file_dir + kRelocationShaderFile,
				kShaderEntryPoints[EShaderType::ProbeRelocation],
				ShaderType::Compute, 6, 6, nullptr, &baseDefines);

			handles[EShaderType::ProbeRelocationReset] = pManager->CompileFromFile(
				file_dir + kRelocationShaderFile,
				kShaderEntryPoints[EShaderType::ProbeRelocationReset],
				ShaderType::Compute, 6, 6, nullptr, &baseDefines);
		}

		// classification shader.
		{
			handles[EShaderType::ProbeClassification] = pManager->CompileFromFile(
				file_dir + kClassificationShaderFile,
				kShaderEntryPoints[EShaderType::ProbeClassification],
				ShaderType::Compute, 6, 6, nullptr, &baseDefines);

			handles[EShaderType::ProbeClassificationReset] = pManager->CompileFromFile(
				file_dir + kClassificationShaderFile,
				kShaderEntryPoints[EShaderType::ProbeClassificationReset],
				ShaderType::Compute, 6, 6, nullptr, &baseDefines);
		}

		// variability reduction shader.
		{
			auto defines = baseDefines;
			defines.push_back(ShaderDefine("RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS", irr_int_text.c_str()));
			defines.push_back(ShaderDefine("RTXGI_DDGI_WAVE_LANE_COUNT", wave_lane_text.c_str()));

			handles[EShaderType::VariabilityReduction] = pManager->CompileFromFile(
				file_dir + kBlendingShaderFile,
				kShaderEntryPoints[EShaderType::VariabilityReduction],
				ShaderType::Compute, 6, 6, nullptr, &defines);
		}

		// extra reduction shader.
		{
			auto defines = baseDefines;
			defines.push_back(ShaderDefine("RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS", irr_int_text.c_str()));
			defines.push_back(ShaderDefine("RTXGI_DDGI_WAVE_LANE_COUNT", wave_lane_text.c_str()));

			handles[EShaderType::ExtraReduction] = pManager->CompileFromFile(
				file_dir + kBlendingShaderFile,
				kShaderEntryPoints[EShaderType::ExtraReduction],
				ShaderType::Compute, 6, 6, nullptr, &defines);
		}

		// wait compile.
		while (pManager->IsCompiling())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		// store shaders.
		for (int i = 0; i < EShaderType::Max; i++)
		{
			assert(handles[i].IsValid());
			shaders_[i] = handles[i].GetShader();
		}

		return true;
	}

	//----
	bool RtxgiComponent::CreateTextures(const rtxgi::DDGIVolumeDesc& ddgiDesc, rtxgi::d3d12::DDGIVolumeResources* ddgiResource)
	{
		sl12::TextureDesc desc{};
		desc.dimension = sl12::TextureDimension::Texture2D;
		desc.depth = 1;
		desc.mipLevels = 1;
		desc.sampleCount = 1;

		// create texture resources.
		{
			// ray data.
			rtxgi::GetDDGIVolumeTextureDimensions(ddgiDesc, rtxgi::EDDGIVolumeTextureType::RayData, desc.width, desc.height, desc.depth);
			desc.format = rtxgi::d3d12::GetDDGIVolumeTextureFormat(rtxgi::EDDGIVolumeTextureType::RayData, ddgiDesc.probeRayDataFormat);
			desc.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			desc.usage = ResourceUsage::UnorderedAccess;
			if (!textures_[ETextureType::RayData].Initialize(pParentDevice_, desc))
			{
				return false;
			}
			ddgiResource->unmanaged.probeRayData = textures_[ETextureType::RayData].GetResourceDep();

			// irradiance.
			rtxgi::GetDDGIVolumeTextureDimensions(ddgiDesc, rtxgi::EDDGIVolumeTextureType::Irradiance, desc.width, desc.height, desc.depth);
			desc.format = rtxgi::d3d12::GetDDGIVolumeTextureFormat(rtxgi::EDDGIVolumeTextureType::Irradiance, ddgiDesc.probeIrradianceFormat);
			desc.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			desc.usage = ResourceUsage::RenderTarget | ResourceUsage::UnorderedAccess;
			if (!textures_[ETextureType::Irradiance].Initialize(pParentDevice_, desc))
			{
				return false;
			}
			ddgiResource->unmanaged.probeIrradiance = textures_[ETextureType::Irradiance].GetResourceDep();

			// distance.
			rtxgi::GetDDGIVolumeTextureDimensions(ddgiDesc, rtxgi::EDDGIVolumeTextureType::Distance, desc.width, desc.height, desc.depth);
			desc.format = rtxgi::d3d12::GetDDGIVolumeTextureFormat(rtxgi::EDDGIVolumeTextureType::Distance, ddgiDesc.probeDistanceFormat);
			desc.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			desc.usage = ResourceUsage::RenderTarget | ResourceUsage::UnorderedAccess;
			if (!textures_[ETextureType::Distance].Initialize(pParentDevice_, desc))
			{
				return false;
			}
			ddgiResource->unmanaged.probeDistance = textures_[ETextureType::Distance].GetResourceDep();

			// data.
			rtxgi::GetDDGIVolumeTextureDimensions(ddgiDesc, rtxgi::EDDGIVolumeTextureType::Data, desc.width, desc.height, desc.depth);
			desc.format = rtxgi::d3d12::GetDDGIVolumeTextureFormat(rtxgi::EDDGIVolumeTextureType::Data, ddgiDesc.probeDataFormat);
			desc.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			desc.usage = ResourceUsage::UnorderedAccess;
			if (!textures_[ETextureType::ProbeData].Initialize(pParentDevice_, desc))
			{
				return false;
			}
			ddgiResource->unmanaged.probeData = textures_[ETextureType::ProbeData].GetResourceDep();

			// variability.
			rtxgi::GetDDGIVolumeTextureDimensions(ddgiDesc, rtxgi::EDDGIVolumeTextureType::Variability, desc.width, desc.height, desc.depth);
			desc.format = rtxgi::d3d12::GetDDGIVolumeTextureFormat(rtxgi::EDDGIVolumeTextureType::Variability, ddgiDesc.probeVariabilityFormat);
			desc.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			desc.usage = ResourceUsage::UnorderedAccess;
			if (!textures_[ETextureType::Variability].Initialize(pParentDevice_, desc))
			{
				return false;
			}
			ddgiResource->unmanaged.probeVariability = textures_[ETextureType::Variability].GetResourceDep();

			// variability average.
			rtxgi::GetDDGIVolumeTextureDimensions(ddgiDesc, rtxgi::EDDGIVolumeTextureType::VariabilityAverage, desc.width, desc.height, desc.depth);
			desc.format = rtxgi::d3d12::GetDDGIVolumeTextureFormat(rtxgi::EDDGIVolumeTextureType::VariabilityAverage, ddgiDesc.probeVariabilityFormat);
			desc.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			desc.usage = ResourceUsage::UnorderedAccess;
			if (!textures_[ETextureType::VariabilityAverage].Initialize(pParentDevice_, desc))
			{
				return false;
			}
			ddgiResource->unmanaged.probeVariabilityAverage = textures_[ETextureType::VariabilityAverage].GetResourceDep();

			// variability readback.
			sl12::BufferDesc bufferDesc;
			bufferDesc.heap = BufferHeap::ReadBack;
			bufferDesc.size = sizeof(float) * 2;
			bufferDesc.stride = 0;
			bufferDesc.usage = ResourceUsage::Unknown;
			bufferDesc.initialState = D3D12_RESOURCE_STATE_COPY_DEST;
			if (!buffers_[EBufferType::VariabilityReadBack].Initialize(pParentDevice_, bufferDesc))
			{
				return false;
			}
			ddgiResource->unmanaged.probeVariabilityReadback = buffers_[EBufferType::VariabilityReadBack].GetResourceDep();
		}

		// store descriptor heap.
		{
			auto p_device_dep = pParentDevice_->GetDeviceDep();

			static const SIZE_T kSRVStart = kSRVStartInDescriptorHeap;
			static const SIZE_T kUAVStart = kSRVStart + rtxgi::GetDDGIVolumeNumTex2DArrayDescriptors();

			D3D12_CPU_DESCRIPTOR_HANDLE resHandle, srvHandle, uavHandle, rtvHandle;
			resHandle = ddgiResource->descriptorHeap.resources->GetCPUDescriptorHandleForHeapStart();
			rtvHandle = pRtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

			UINT srvDescSize = p_device_dep->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			UINT rtvDescSize = p_device_dep->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			srvDesc.Texture2DArray.MipLevels = 1;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

			D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;

			auto& resourceIndices = ddgiResource->descriptorHeap.resourceIndices;
			uint32_t width, height;

			// ray data.
			{
				srvHandle.ptr = resHandle.ptr + static_cast<SIZE_T>(resourceIndices.rayDataSRVIndex * srvDescSize);
				uavHandle.ptr = resHandle.ptr + static_cast<SIZE_T>(resourceIndices.rayDataUAVIndex * srvDescSize);

				rtxgi::GetDDGIVolumeTextureDimensions(ddgiDesc, rtxgi::EDDGIVolumeTextureType::RayData, width, height, srvDesc.Texture2DArray.ArraySize);
				uavDesc.Texture2DArray.ArraySize = srvDesc.Texture2DArray.ArraySize;
				srvDesc.Format = uavDesc.Format = rtxgi::d3d12::GetDDGIVolumeTextureFormat(rtxgi::EDDGIVolumeTextureType::RayData, ddgiDesc.probeRayDataFormat);
				p_device_dep->CreateUnorderedAccessView(ddgiResource->unmanaged.probeRayData, nullptr, &uavDesc, uavHandle);
				p_device_dep->CreateShaderResourceView(ddgiResource->unmanaged.probeRayData, &srvDesc, srvHandle);
			}

			// irradiance.
			{
				srvHandle.ptr = resHandle.ptr + static_cast<SIZE_T>(resourceIndices.probeIrradianceSRVIndex * srvDescSize);
				uavHandle.ptr = resHandle.ptr + static_cast<SIZE_T>(resourceIndices.probeIrradianceUAVIndex * srvDescSize);

				rtxgi::GetDDGIVolumeTextureDimensions(ddgiDesc, rtxgi::EDDGIVolumeTextureType::Irradiance, width, height, srvDesc.Texture2DArray.ArraySize);
				uavDesc.Texture2DArray.ArraySize = rtvDesc.Texture2DArray.ArraySize = srvDesc.Texture2DArray.ArraySize;
				srvDesc.Format = uavDesc.Format = rtvDesc.Format = rtxgi::d3d12::GetDDGIVolumeTextureFormat(rtxgi::EDDGIVolumeTextureType::Irradiance, ddgiDesc.probeIrradianceFormat);
				p_device_dep->CreateUnorderedAccessView(ddgiResource->unmanaged.probeIrradiance, nullptr, &uavDesc, uavHandle);
				p_device_dep->CreateShaderResourceView(ddgiResource->unmanaged.probeIrradiance, &srvDesc, srvHandle);

				ddgiResource->unmanaged.probeIrradianceRTV.ptr = rtvHandle.ptr + static_cast<SIZE_T>(ddgiDesc.index * rtxgi::GetDDGIVolumeNumRTVDescriptors() * rtvDescSize);
				p_device_dep->CreateRenderTargetView(ddgiResource->unmanaged.probeIrradiance, &rtvDesc, ddgiResource->unmanaged.probeIrradianceRTV);
			}

			// distance.
			{
				srvHandle.ptr = resHandle.ptr + static_cast<SIZE_T>(resourceIndices.probeDistanceSRVIndex * srvDescSize);
				uavHandle.ptr = resHandle.ptr + static_cast<SIZE_T>(resourceIndices.probeDistanceUAVIndex * srvDescSize);

				rtxgi::GetDDGIVolumeTextureDimensions(ddgiDesc, rtxgi::EDDGIVolumeTextureType::Distance, width, height, srvDesc.Texture2DArray.ArraySize);
				uavDesc.Texture2DArray.ArraySize = rtvDesc.Texture2DArray.ArraySize = srvDesc.Texture2DArray.ArraySize;
				srvDesc.Format = uavDesc.Format = rtvDesc.Format = rtxgi::d3d12::GetDDGIVolumeTextureFormat(rtxgi::EDDGIVolumeTextureType::Distance, ddgiDesc.probeDistanceFormat);
				p_device_dep->CreateUnorderedAccessView(ddgiResource->unmanaged.probeDistance, nullptr, &uavDesc, uavHandle);
				p_device_dep->CreateShaderResourceView(ddgiResource->unmanaged.probeDistance, &srvDesc, srvHandle);

				ddgiResource->unmanaged.probeDistanceRTV.ptr = ddgiResource->unmanaged.probeIrradianceRTV.ptr + rtvDescSize;
				p_device_dep->CreateRenderTargetView(ddgiResource->unmanaged.probeDistance, &rtvDesc, ddgiResource->unmanaged.probeDistanceRTV);
			}

			// Probe data texture descriptors
			{
				srvHandle.ptr = resHandle.ptr + static_cast<SIZE_T>(resourceIndices.probeDataSRVIndex * srvDescSize);
				uavHandle.ptr = resHandle.ptr + static_cast<SIZE_T>(resourceIndices.probeDataUAVIndex * srvDescSize);

				rtxgi::GetDDGIVolumeTextureDimensions(ddgiDesc, rtxgi::EDDGIVolumeTextureType::Data, width, height, srvDesc.Texture2DArray.ArraySize);
				uavDesc.Texture2DArray.ArraySize = srvDesc.Texture2DArray.ArraySize;
				srvDesc.Format = uavDesc.Format = rtxgi::d3d12::GetDDGIVolumeTextureFormat(rtxgi::EDDGIVolumeTextureType::Data, ddgiDesc.probeDataFormat);
				p_device_dep->CreateUnorderedAccessView(ddgiResource->unmanaged.probeData, nullptr, &uavDesc, uavHandle);
				p_device_dep->CreateShaderResourceView(ddgiResource->unmanaged.probeData, &srvDesc, srvHandle);
			}

			// Probe variability texture descriptors
			{
				srvHandle.ptr = resHandle.ptr + static_cast<SIZE_T>(resourceIndices.probeVariabilitySRVIndex * srvDescSize);
				uavHandle.ptr = resHandle.ptr + static_cast<SIZE_T>(resourceIndices.probeVariabilityUAVIndex * srvDescSize);

				rtxgi::GetDDGIVolumeTextureDimensions(ddgiDesc, rtxgi::EDDGIVolumeTextureType::Variability, width, height, srvDesc.Texture2DArray.ArraySize);
				uavDesc.Texture2DArray.ArraySize = srvDesc.Texture2DArray.ArraySize;
				srvDesc.Format = uavDesc.Format = rtxgi::d3d12::GetDDGIVolumeTextureFormat(rtxgi::EDDGIVolumeTextureType::Variability, ddgiDesc.probeVariabilityFormat);
				p_device_dep->CreateUnorderedAccessView(ddgiResource->unmanaged.probeVariability, nullptr, &uavDesc, uavHandle);
				p_device_dep->CreateShaderResourceView(ddgiResource->unmanaged.probeVariability, &srvDesc, srvHandle);
			}

			// Probe variability average texture descriptors
			{
				srvHandle.ptr = resHandle.ptr + static_cast<SIZE_T>(resourceIndices.probeVariabilityAverageSRVIndex * srvDescSize);
				uavHandle.ptr = resHandle.ptr + static_cast<SIZE_T>(resourceIndices.probeVariabilityAverageUAVIndex * srvDescSize);

				rtxgi::GetDDGIVolumeTextureDimensions(ddgiDesc, rtxgi::EDDGIVolumeTextureType::VariabilityAverage, width, height, srvDesc.Texture2DArray.ArraySize);
				uavDesc.Texture2DArray.ArraySize = srvDesc.Texture2DArray.ArraySize;
				srvDesc.Format = uavDesc.Format = rtxgi::d3d12::GetDDGIVolumeTextureFormat(rtxgi::EDDGIVolumeTextureType::VariabilityAverage, ddgiDesc.probeVariabilityFormat);
				p_device_dep->CreateUnorderedAccessView(ddgiResource->unmanaged.probeVariabilityAverage, nullptr, &uavDesc, uavHandle);
				p_device_dep->CreateShaderResourceView(ddgiResource->unmanaged.probeVariabilityAverage, &srvDesc, srvHandle);
			}
		}

		// create views for sl12
		for (int i = 0; i < ETextureType::Max; i++)
		{
			if (!textureSrvs_[i].Initialize(pParentDevice_, &textures_[i]))
			{
				return false;
			}
			if (!textureUavs_[i].Initialize(pParentDevice_, &textures_[i]))
			{
				return false;
			}
		}

		return true;
	}

	//----
	bool RtxgiComponent::CreatePipelines(rtxgi::d3d12::DDGIVolumeResources* ddgiResource)
	{
		auto p_device_dep = pParentDevice_->GetDeviceDep();

		// create root signature.
		{
			ID3DBlob* signature;
			if (!rtxgi::d3d12::GetDDGIVolumeRootSignatureDesc(ddgiResource->descriptorHeap, signature))
				return false;
			if (signature == nullptr)
				return false;

			HRESULT hr = p_device_dep->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRootSignature_));
			sl12::SafeRelease(signature);
			if (FAILED(hr))
				return false;

			ddgiResource->unmanaged.rootSignature = pRootSignature_;
		}

		// create psos.
		{
			D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
			psoDesc.pRootSignature = ddgiResource->unmanaged.rootSignature;

			{
				psoDesc.CS.BytecodeLength = shaders_[EShaderType::IrradianceBlending]->GetSize();
				psoDesc.CS.pShaderBytecode = shaders_[EShaderType::IrradianceBlending]->GetData();
				auto hr = p_device_dep->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&psos_[EShaderType::IrradianceBlending]));
				if (FAILED(hr))
					return false;
				ddgiResource->unmanaged.probeBlendingIrradiancePSO = psos_[EShaderType::IrradianceBlending];
			}

			{
				psoDesc.CS.BytecodeLength = shaders_[EShaderType::DistanceBlending]->GetSize();
				psoDesc.CS.pShaderBytecode = shaders_[EShaderType::DistanceBlending]->GetData();
				auto hr = p_device_dep->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&psos_[EShaderType::DistanceBlending]));
				if (FAILED(hr))
					return false;
				ddgiResource->unmanaged.probeBlendingDistancePSO = psos_[EShaderType::DistanceBlending];
			}

			{
				psoDesc.CS.BytecodeLength = shaders_[EShaderType::ProbeRelocation]->GetSize();
				psoDesc.CS.pShaderBytecode = shaders_[EShaderType::ProbeRelocation]->GetData();
				auto hr = p_device_dep->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&psos_[EShaderType::ProbeRelocation]));
				if (FAILED(hr))
					return false;
				ddgiResource->unmanaged.probeRelocation.updatePSO = psos_[EShaderType::ProbeRelocation];
			}

			{
				psoDesc.CS.BytecodeLength = shaders_[EShaderType::ProbeRelocationReset]->GetSize();
				psoDesc.CS.pShaderBytecode = shaders_[EShaderType::ProbeRelocationReset]->GetData();
				auto hr = p_device_dep->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&psos_[EShaderType::ProbeRelocationReset]));
				if (FAILED(hr))
					return false;
				ddgiResource->unmanaged.probeRelocation.resetPSO = psos_[EShaderType::ProbeRelocationReset];
			}

			{
				psoDesc.CS.BytecodeLength = shaders_[EShaderType::ProbeClassification]->GetSize();
				psoDesc.CS.pShaderBytecode = shaders_[EShaderType::ProbeClassification]->GetData();
				auto hr = p_device_dep->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&psos_[EShaderType::ProbeClassification]));
				if (FAILED(hr))
					return false;
				ddgiResource->unmanaged.probeClassification.updatePSO = psos_[EShaderType::ProbeClassification];
			}

			{
				psoDesc.CS.BytecodeLength = shaders_[EShaderType::ProbeClassificationReset]->GetSize();
				psoDesc.CS.pShaderBytecode = shaders_[EShaderType::ProbeClassificationReset]->GetData();
				auto hr = p_device_dep->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&psos_[EShaderType::ProbeClassificationReset]));
				if (FAILED(hr))
					return false;
				ddgiResource->unmanaged.probeClassification.resetPSO = psos_[EShaderType::ProbeClassificationReset];
			}

			{
				psoDesc.CS.BytecodeLength = shaders_[EShaderType::VariabilityReduction]->GetSize();
				psoDesc.CS.pShaderBytecode = shaders_[EShaderType::VariabilityReduction]->GetData();
				auto hr = p_device_dep->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&psos_[EShaderType::VariabilityReduction]));
				if (FAILED(hr))
					return false;
				ddgiResource->unmanaged.probeVariabilityPSOs.reductionPSO = psos_[EShaderType::VariabilityReduction];
			}

			{
				psoDesc.CS.BytecodeLength = shaders_[EShaderType::ExtraReduction]->GetSize();
				psoDesc.CS.pShaderBytecode = shaders_[EShaderType::ExtraReduction]->GetData();
				auto hr = p_device_dep->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&psos_[EShaderType::ExtraReduction]));
				if (FAILED(hr))
					return false;
				ddgiResource->unmanaged.probeVariabilityPSOs.extraReductionPSO = psos_[EShaderType::ExtraReduction];
			}
		}

		return true;
	}

	//----
	void RtxgiComponent::Destroy()
	{
		sl12::SafeRelease(pSrvDescriptorHeap_);
		sl12::SafeRelease(pRtvDescriptorHeap_);
		for (auto&& v : volumeCBVs_) v.Destroy();
		for (auto&& v : volumeCBs_) v.Destroy();
		for (auto&& v : textureUavs_) v.Destroy();
		for (auto&& v : textureSrvs_) v.Destroy();
		for (auto&& v : textures_) v.Destroy();
		sl12::SafeRelease(pRootSignature_);
		for (auto&& v : psos_) sl12::SafeRelease(v);

		ddgiVolume_.reset(nullptr);
	}

	//----
	void RtxgiComponent::UpdateVolume(const DirectX::XMFLOAT3* pTranslate)
	{
		if (pTranslate)
		{
			rtxgi::float3 origin;
			origin.x = pTranslate->x;
			origin.y = pTranslate->y;
			origin.z = pTranslate->z;
			ddgiVolume_->SetOrigin(origin);
		}
		ddgiVolume_->Update();
	}

	//----
	void RtxgiComponent::ClearProbes(sl12::CommandList* pCmdList)
	{
		ddgiVolume_->ClearProbes(pCmdList->GetLatestCommandList());
	}

	//----
	void RtxgiComponent::UploadConstants(sl12::CommandList* pCmdList, sl12::u32 frameIndex)
	{
		auto volumes = ddgiVolume_.get();
		rtxgi::d3d12::UploadDDGIVolumeConstants(pCmdList->GetLatestCommandList(), frameIndex & 0x01, 1, &volumes);
	}

	//----
	void RtxgiComponent::UpdateProbes(sl12::CommandList* pCmdList)
	{
		auto volumes = ddgiVolume_.get();
		rtxgi::d3d12::UpdateDDGIVolumeProbes(pCmdList->GetLatestCommandList(), 1, &volumes);
	}

	//----
	void RtxgiComponent::RelocateProbes(sl12::CommandList* pCmdList, float distanceScale)
	{
		auto volumes = ddgiVolume_.get();
		rtxgi::d3d12::RelocateDDGIVolumeProbes(pCmdList->GetLatestCommandList(), 1, &volumes);
	}

	//----
	void RtxgiComponent::ClassifyProbes(sl12::CommandList* pCmdList)
	{
		auto volumes = ddgiVolume_.get();
		rtxgi::d3d12::ClassifyDDGIVolumeProbes(pCmdList->GetLatestCommandList(), 1, &volumes);
	}

	//----
	int RtxgiComponent::GetNumProbes() const
	{
		return ddgiVolume_->GetNumProbes();
	}
	int RtxgiComponent::GetNumRaysPerProbe() const
	{
		return ddgiVolume_->GetNumRaysPerProbe();
	}

	//----
	void RtxgiComponent::SetDescHysteresis(float v)
	{
		ddgiVolume_->SetProbeHysteresis(v);
	}
	void RtxgiComponent::SetDescIrradianceThreshold(float v)
	{
		ddgiVolume_->SetProbeIrradianceThreshold(v);
	}
	void RtxgiComponent::SetDescBrightnessThreshold(float v)
	{
		ddgiVolume_->SetProbeBrightnessThreshold(v);
	}

}	// namespace sl12


//	EOF
