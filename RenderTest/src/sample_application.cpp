#include "sample_application.h"

#include "sl12/resource_mesh.h"
#include "sl12/string_util.h"

#define NOMINMAX
#include <windowsx.h>

#include "sl12/root_signature.h"

#define USE_IN_CPP
#include "../shaders/cbuffer.hlsli"
#include "sl12/descriptor_set.h"
#include "sl12/resource_texture.h"

namespace
{
	static const char* kResourceDir = "resources";
	static const char* kShaderDir = "RenderTest/shaders";

	static std::vector<sl12::RenderGraphTargetDesc> gGBufferDescs;
	static sl12::RenderGraphTargetDesc gAccumDesc;
	void SetGBufferDesc(sl12::u32 width, sl12::u32 height)
	{
		gGBufferDescs.clear();
		
		sl12::RenderGraphTargetDesc desc{};
		desc.name = "GBufferA";
		desc.width = width;
		desc.height = height;
		desc.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		desc.srvDescs.push_back(sl12::RenderGraphSRVDesc(0, 0, 0, 0));
		desc.rtvDescs.push_back(sl12::RenderGraphRTVDesc(0, 0, 0));
		gGBufferDescs.push_back(desc);

		desc.name = "GBufferB";
		desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		gGBufferDescs.push_back(desc);

		desc.name = "GBufferC";
		desc.format = DXGI_FORMAT_R10G10B10A2_UNORM;
		gGBufferDescs.push_back(desc);

		desc.name = "Depth";
		desc.format = DXGI_FORMAT_D32_FLOAT;
		desc.clearDepth = 1.0f;
		desc.rtvDescs.clear();
		desc.dsvDescs.push_back(sl12::RenderGraphDSVDesc(0, 0, 0));
		desc.usage = sl12::ResourceUsage::ShaderResource | sl12::ResourceUsage::DepthStencil;
		gGBufferDescs.push_back(desc);

		gAccumDesc.name = "Accum";
		gAccumDesc.width = width;
		gAccumDesc.height = height;
		gAccumDesc.format = DXGI_FORMAT_R11G11B10_FLOAT;
		gAccumDesc.usage = sl12::ResourceUsage::ShaderResource | sl12::ResourceUsage::UnorderedAccess;
		gAccumDesc.srvDescs.push_back(sl12::RenderGraphSRVDesc(0, 0, 0, 0));
		gAccumDesc.uavDescs.push_back(sl12::RenderGraphUAVDesc(0, 0, 0));
	}
}

SampleApplication::SampleApplication(HINSTANCE hInstance, int nCmdShow, int screenWidth, int screenHeight, sl12::ColorSpaceType csType, const std::string& homeDir)
	: Application(hInstance, nCmdShow, screenWidth, screenHeight, csType)
	, displayWidth_(screenWidth), displayHeight_(screenHeight)
{
	std::filesystem::path p(homeDir);
	p = std::filesystem::absolute(p);
	homeDir_ = p.string();
}

SampleApplication::~SampleApplication()
{}

extern void TestRenderGraph2(sl12::Device* pDevice);

bool SampleApplication::Initialize()
{
	//TestRenderGraph2(&device_);
	
	// initialize mesh manager.
	const size_t kVertexBufferSize = 512 * 1024 * 1024;		// 512MB
	const size_t kIndexBufferSize = 64 * 1024 * 1024;		// 64MB
	meshMan_ = sl12::MakeUnique<sl12::MeshManager>(&device_, &device_, kVertexBufferSize, kIndexBufferSize);
	
	// initialize resource loader.
	resLoader_ = sl12::MakeUnique<sl12::ResourceLoader>(nullptr);
	if (!resLoader_->Initialize(&device_, &meshMan_, sl12::JoinPath(homeDir_, kResourceDir)))
	{
		sl12::ConsolePrint("Error: failed to init resource loader.");
		return false;
	}

	// initialize shader manager.
	std::vector<std::string> shaderIncludeDirs;
	shaderIncludeDirs.push_back(sl12::JoinPath(homeDir_, "SampleLib12/shaders/include"));
	shaderMan_ = sl12::MakeUnique<sl12::ShaderManager>(nullptr);
	if (!shaderMan_->Initialize(&device_, &shaderIncludeDirs))
	{
		sl12::ConsolePrint("Error: failed to init shader manager.");
		return false;
	}

	// compile shaders.
	const std::string shaderBaseDir = sl12::JoinPath(homeDir_, kShaderDir);
	std::vector<sl12::ShaderDefine> shaderDefines;
	//shaderDefines.push_back(sl12::ShaderDefine("ENABLE_DYNAMIC_RESOURCE", ENABLE_DYNAMIC_RESOURCE ? "1" : "0"));
	hShaders_[ShaderID::Mesh_VV] = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "mesh.vv.hlsl"),
		"main", sl12::ShaderType::Vertex, 6, 6, nullptr, &shaderDefines);
	hShaders_[ShaderID::Mesh_P] = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "mesh.p.hlsl"),
		"main", sl12::ShaderType::Pixel, 6, 6, nullptr, &shaderDefines);
	hShaders_[ShaderID::Lighting_C] = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "lighting.c.hlsl"),
		"main", sl12::ShaderType::Compute, 6, 6, nullptr, &shaderDefines);
	hShaders_[ShaderID::Fullscreen_VV] = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "fullscreen.vv.hlsl"),
		"main", sl12::ShaderType::Vertex, 6, 6, nullptr, &shaderDefines);
	hShaders_[ShaderID::Tonemap_P] = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "tonemap.p.hlsl"),
		"main", sl12::ShaderType::Pixel, 6, 6, nullptr, &shaderDefines);
	
	// load request.
	hResMesh_ = resLoader_->LoadRequest<sl12::ResourceItemMesh>("mesh/chinese_dragon/chinese_dragon.rmesh");

	// init command list.
	frameStartCmdList_ = sl12::MakeUnique<CommandLists>(nullptr);
	if (!frameStartCmdList_->Initialize(&device_, &device_.GetGraphicsQueue()))
	{
		sl12::ConsolePrint("Error: failed to init frame start command list.");
		return false;
	}
	frameEndCmdList_ = sl12::MakeUnique<CommandLists>(nullptr);
	if (!frameEndCmdList_->Initialize(&device_, &device_.GetGraphicsQueue()))
	{
		sl12::ConsolePrint("Error: failed to init frame end command list.");
		return false;
	}

	// init cbv manager.
	cbvMan_ = sl12::MakeUnique<sl12::CbvManager>(nullptr, &device_);

	// get GBuffer target descs.
	SetGBufferDesc(displayWidth_, displayHeight_);
	
	// create sampler.
	{
		linearSampler_ = sl12::MakeUnique<sl12::Sampler>(&device_);

		D3D12_SAMPLER_DESC desc{};
		desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		desc.MaxLOD = FLT_MAX;
		desc.MinLOD = 0.0f;
		desc.MipLODBias = 0.0f;
		linearSampler_->Initialize(&device_, desc);
	}
	
	// init utility command list.
	auto utilCmdList = sl12::MakeUnique<sl12::CommandList>(&device_);
	utilCmdList->Initialize(&device_, &device_.GetGraphicsQueue());
	utilCmdList->Reset();

	// init GUI.
	gui_ = sl12::MakeUnique<sl12::Gui>(nullptr);
	if (!gui_->Initialize(&device_, device_.GetSwapchain().GetTexture(0)->GetResourceDesc().Format))
	{
		sl12::ConsolePrint("Error: failed to init GUI.");
		return false;
	}
	if (!gui_->CreateFontImage(&device_, &utilCmdList))
	{
		sl12::ConsolePrint("Error: failed to create GUI font.");
		return false;
	}

	// create dummy texture.
	if (!device_.CreateDummyTextures(&utilCmdList))
	{
		return false;
	}

	// execute utility commands.
	utilCmdList->Close();
	utilCmdList->Execute();
	device_.WaitDrawDone();

	// wait compile and load.
	while (shaderMan_->IsCompiling() || resLoader_->IsLoading())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	// setup texture streamer.
	texStreamer_ = sl12::MakeUnique<sl12::TextureStreamer>(&device_);
	if (!texStreamer_->Initialize(&device_))
	{
		return false;
	}
	{
		auto resMesh = const_cast<sl12::ResourceItemMesh*>(hResMesh_.GetItem<sl12::ResourceItemMesh>());
		workMaterials_.reserve(resMesh->GetMaterials().size());
		for (auto&& mat : resMesh->GetMaterials())
		{
			WorkMaterial work;
			work.pResMaterial = &mat;
			work.texHandles.push_back(mat.baseColorTex);
			work.texHandles.push_back(mat.normalTex);
			work.texHandles.push_back(mat.ormTex);
			workMaterials_.push_back(work);
		}
	}
	
	// init render graph.
	SceneRenderState::InitInstance(&device_);
	auto state = SceneRenderState::GetInstance();
	state->SetRenderObjects(&cbvMan_);
	state->SetScreenSize(screenWidth_, screenHeight_);
	state->SetShaderHandles(hShaders_);
	state->SetResMesh(hResMesh_);
	renderGraph_ = sl12::MakeUnique<sl12::RenderGraph>(&device_);
	renderGraph_->Initialize(&device_);
	SetupRenderGraph(&device_, &renderGraph_);
	
	return true;
}

void SampleApplication::Finalize()
{
	// wait render.
	device_.WaitDrawDone();
	device_.Present(1);

	// destroy render objects.
	SceneRenderState::DestroyInstance();
	gui_.Reset();
	texStreamer_.Reset();
	renderGraph_.Reset();
	cbvMan_.Reset();
	frameEndCmdList_.Reset();
	frameStartCmdList_.Reset();
	shaderMan_.Reset();
	resLoader_.Reset();
}

bool SampleApplication::Execute()
{
	const int kSwapchainBufferOffset = 1;
	auto frameIndex = (device_.GetSwapchain().GetFrameIndex() + sl12::Swapchain::kMaxBuffer - 1) % sl12::Swapchain::kMaxBuffer;
	auto prevFrameIndex = (device_.GetSwapchain().GetFrameIndex() + sl12::Swapchain::kMaxBuffer - 2) % sl12::Swapchain::kMaxBuffer;

	// compile render graph.
	sl12::Texture* pSwapchainTex = device_.GetSwapchain().GetCurrentTexture(kSwapchainBufferOffset);
	CompileRenderGraph(&device_, &renderGraph_, pSwapchainTex);

	// device sync.
	device_.WaitPresent();
	device_.SyncKillObjects();

	// frame start commands.
	auto pCmdList = &frameStartCmdList_->Reset();

	// imgui setup.
	bool bStream = false;
	static sl12::u32 texTargetWidth = 256;
	
	gui_->BeginNewFrame(pCmdList, displayWidth_, displayHeight_, inputData_);
	inputData_.Reset();
	{
		ImGui::Text("Deer imgui.");
		if (ImGui::Button("Miplevel Down"))
		{
			bStream = true;
			texTargetWidth <<= 1;
			texTargetWidth = std::min(texTargetWidth, 4096u);
		}
		if (ImGui::Button("Miplevel Up"))
		{
			bStream = true;
			texTargetWidth >>= 1;
			texTargetWidth = std::max(texTargetWidth, 32u);
		}
		static float sV = 0.0f;
		ImGui::DragFloat("My Float", &sV, 1.0f, 0.0f, 100.0f);
		static char sT[256]{};
		ImGui::InputText("My Text", sT, 256);
	}
	ImGui::Render();

	// frame begin commands.
	device_.LoadRenderCommands(pCmdList);
	meshMan_->BeginNewFrame(pCmdList);
	cbvMan_->BeginNewFrame();

	// texture streaming request.
	if (bStream)
	{
		for (auto&& work : workMaterials_)
		{
			for (auto&& handle : work.texHandles)
			{
				texStreamer_->RequestStreaming(handle, texTargetWidth);
			}
		}
	}
	
	// create scene constant buffer.
	sl12::CbvHandle hSceneCB;
	{
		DirectX::XMFLOAT3 camPos(300.0f, 100.0f, 0.0f);
		DirectX::XMFLOAT3 tgtPos(0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT3 upVec(0.0f, 1.0f, 0.0f);
		auto cp = DirectX::XMLoadFloat3(&camPos);
		auto tp = DirectX::XMLoadFloat3(&tgtPos);
		auto up = DirectX::XMLoadFloat3(&upVec);
		auto mtxWorldToView = DirectX::XMMatrixLookAtRH(cp, tp, up);
		auto mtxViewToClip = sl12::MatrixPerspectiveInfiniteFovRH(DirectX::XMConvertToRadians(60.0f), (float)displayWidth_ / (float)displayHeight_, 0.1f);
		auto mtxWorldToClip = mtxWorldToView * mtxViewToClip;
		auto mtxClipToWorld = DirectX::XMMatrixInverse(nullptr, mtxWorldToClip);
		auto mtxViewToWorld = DirectX::XMMatrixInverse(nullptr, mtxWorldToView);

		SceneCB cbScene;
		DirectX::XMStoreFloat4x4(&cbScene.mtxWorldToProj, mtxWorldToClip);
		DirectX::XMStoreFloat4x4(&cbScene.mtxWorldToView, mtxWorldToView);
		DirectX::XMStoreFloat4x4(&cbScene.mtxProjToWorld, mtxClipToWorld);
		DirectX::XMStoreFloat4x4(&cbScene.mtxViewToWorld, mtxViewToWorld);
		cbScene.screenSize.x = (float)displayWidth_;
		cbScene.screenSize.y = (float)displayHeight_;

		hSceneCB = cbvMan_->GetTemporal(&cbScene, sizeof(cbScene));
	}
	SceneRenderState::GetInstance()->SetFrameResource(hSceneCB.GetCBV());
	frameStartCmdList_->Close();

	
	// render graph commands.
	renderGraph_->LoadCommand();


	// frame end commands.
	pCmdList = &frameEndCmdList_->Reset();
	
	// draw GUI.
	{
		auto&& rtv = device_.GetSwapchain().GetCurrentRenderTargetView(kSwapchainBufferOffset)->GetDescInfo().cpuHandle;
		pCmdList->GetLatestCommandList()->OMSetRenderTargets(1, &rtv, false, nullptr);

		// set viewport.
		D3D12_VIEWPORT vp;
		vp.TopLeftX = vp.TopLeftY = 0.0f;
		vp.Width = (float)displayWidth_;
		vp.Height = (float)displayHeight_;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		pCmdList->GetLatestCommandList()->RSSetViewports(1, &vp);

		// set scissor rect.
		D3D12_RECT rect;
		rect.left = rect.top = 0;
		rect.right = displayWidth_;
		rect.bottom = displayHeight_;
		pCmdList->GetLatestCommandList()->RSSetScissorRects(1, &rect);

		gui_->LoadDrawCommands(pCmdList);
	}
	
	// barrier swapchain.
	pCmdList->TransitionBarrier(pSwapchainTex, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	frameEndCmdList_->Close();

	// wait prev frame render.
	device_.WaitDrawDone();

	// present swapchain.
	device_.Present(0);

	// execute current frame render.
	frameStartCmdList_->Execute();
	renderGraph_->Execute();
	frameEndCmdList_->Execute();

	return true;
}

int SampleApplication::Input(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_LBUTTONDOWN:
		inputData_.mouseButton |= sl12::MouseButton::Left;
		return 0;
	case WM_RBUTTONDOWN:
		inputData_.mouseButton |= sl12::MouseButton::Right;
		return 0;
	case WM_MBUTTONDOWN:
		inputData_.mouseButton |= sl12::MouseButton::Middle;
		return 0;
	case WM_LBUTTONUP:
		inputData_.mouseButton &= ~sl12::MouseButton::Left;
		return 0;
	case WM_RBUTTONUP:
		inputData_.mouseButton &= ~sl12::MouseButton::Right;
		return 0;
	case WM_MBUTTONUP:
		inputData_.mouseButton &= ~sl12::MouseButton::Middle;
		return 0;
	case WM_MOUSEMOVE:
		inputData_.mouseX = GET_X_LPARAM(lParam);
		inputData_.mouseY = GET_Y_LPARAM(lParam);
		return 0;
	case WM_KEYUP:
	case WM_SYSKEYUP:
		inputData_.key = wParam;
		inputData_.scancode = (int)LOBYTE(HIWORD(lParam));;
		inputData_.keyDown = false;
		return 0;
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		inputData_.key = wParam;
		inputData_.scancode = (int)LOBYTE(HIWORD(lParam));;
		inputData_.keyDown = true;
		return 0;
	case WM_CHAR:
		inputData_.chara = (sl12::u16)wParam;
		return 0;
	}

	return 0;
}

//	EOF
