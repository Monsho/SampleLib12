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

bool SampleApplication::Initialize()
{
	// initialize resource loader.
	resLoader_ = sl12::MakeUnique<sl12::ResourceLoader>(nullptr);
	if (!resLoader_->Initialize(&device_, sl12::JoinPath(homeDir_, kResourceDir)))
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
	hMeshVV_ = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "mesh.vv.hlsl"),
		"main", sl12::ShaderType::Vertex, 6, 5, nullptr, nullptr);
	hMeshP_ = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "mesh.p.hlsl"),
		"main", sl12::ShaderType::Pixel, 6, 5, nullptr, nullptr);
	
	// load request.
	hSuzanneMesh_ = resLoader_->LoadRequest<sl12::ResourceItemMesh>("mesh/suzanne/suzanne.rmesh");

	// init command list.
	mainCmdList_ = sl12::MakeUnique<CommandLists>(nullptr);
	if (!mainCmdList_->Initialize(&device_, &device_.GetGraphicsQueue()))
	{
		sl12::ConsolePrint("Error: failed to init main command list.");
		return false;
	}

	// init cbv manager.
	cbvMan_ = sl12::MakeUnique<sl12::CbvManager>(nullptr, &device_);
	
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
	
	// create depth stencil buffer.
	{
		depthTex_ = sl12::MakeUnique<sl12::Texture>(&device_);
		sl12::TextureDesc desc{};
		desc.width = displayWidth_;
		desc.height = displayHeight_;
		desc.depth = 1;
		desc.format = DXGI_FORMAT_D32_FLOAT;
		desc.initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		desc.isDepthBuffer = true;

		depthTex_->Initialize(&device_, desc);

		depthDSV_ = sl12::MakeUnique<sl12::DepthStencilView>(&device_);
		depthDSV_->Initialize(&device_, &depthTex_);
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
	
	// init root signature and pipeline state.
	rsVsPs_ = sl12::MakeUnique<sl12::RootSignature>(&device_);
	psoMesh_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(&device_);
	rsVsPs_->Initialize(&device_, hMeshVV_.GetShader(), hMeshP_.GetShader(), nullptr, nullptr, nullptr);
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rsVsPs_;
		desc.pVS = hMeshVV_.GetShader();
		desc.pPS = hMeshP_.GetShader();

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_BACK;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = true;
		desc.rasterizer.isFrontCCW = true;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = true;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

		D3D12_INPUT_ELEMENT_DESC input_elems[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       3, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
		desc.inputLayout.numElements = ARRAYSIZE(input_elems);
		desc.inputLayout.pElements = input_elems;

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.rtvFormats[desc.numRTVs++] = device_.GetSwapchain().GetCurrentTexture()->GetResourceDesc().Format;
		desc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
		desc.multisampleCount = 1;

		if (!psoMesh_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init mesh pso.");
			return false;
		}
	}

	return true;
}

bool SampleApplication::Execute()
{
	const int kSwapchainBufferOffset = 1;
	auto frameIndex = (device_.GetSwapchain().GetFrameIndex() + sl12::Swapchain::kMaxBuffer - 1) % sl12::Swapchain::kMaxBuffer;
	auto prevFrameIndex = (device_.GetSwapchain().GetFrameIndex() + sl12::Swapchain::kMaxBuffer - 2) % sl12::Swapchain::kMaxBuffer;
	auto pCmdList = &mainCmdList_->Reset();

	gui_->BeginNewFrame(pCmdList, displayWidth_, displayHeight_, inputData_);
	inputData_.Reset();
	{
		ImGui::Text("Deer imgui.");
		ImGui::Button("Push Me!");
		static float sV = 0.0f;
		ImGui::DragFloat("My Float", &sV, 1.0f, 0.0f, 100.0f);
		static char sT[256]{};
		ImGui::InputText("My Text", sT, 256);
	}
	ImGui::Render();

	device_.WaitPresent();
	device_.SyncKillObjects();

	device_.LoadRenderCommands(pCmdList);
	cbvMan_->BeginNewFrame();

	// clear swapchain.
	auto&& swapchain = device_.GetSwapchain();
	pCmdList->TransitionBarrier(swapchain.GetCurrentTexture(kSwapchainBufferOffset), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	{
		float color[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
		pCmdList->GetLatestCommandList()->ClearRenderTargetView(swapchain.GetCurrentRenderTargetView(kSwapchainBufferOffset)->GetDescInfo().cpuHandle, color, 0, nullptr);
		pCmdList->GetLatestCommandList()->ClearDepthStencilView(depthDSV_->GetDescInfo().cpuHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	}

	// set render target.
	{
		auto&& rtv = swapchain.GetCurrentRenderTargetView(kSwapchainBufferOffset)->GetDescInfo().cpuHandle;
		auto dsv = depthDSV_->GetDescInfo().cpuHandle;
		pCmdList->GetLatestCommandList()->OMSetRenderTargets(1, &rtv, false, &dsv);

		D3D12_VIEWPORT vp;
		vp.TopLeftX = vp.TopLeftY = 0.0f;
		vp.Width = (float)displayWidth_;
		vp.Height = (float)displayHeight_;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		pCmdList->GetLatestCommandList()->RSSetViewports(1, &vp);

		D3D12_RECT rect;
		rect.left = rect.top = 0;
		rect.right = displayWidth_;
		rect.bottom = displayHeight_;
		pCmdList->GetLatestCommandList()->RSSetScissorRects(1, &rect);
	}

	// draw mesh.
	{
		// create constant buffer.
		DirectX::XMFLOAT3 camPos(0.0f, 0.0f, 300.0f);
		DirectX::XMFLOAT3 tgtPos(0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT3 upVec(0.0f, 1.0f, 0.0f);
		auto cp = DirectX::XMLoadFloat3(&camPos);
		auto tp = DirectX::XMLoadFloat3(&tgtPos);
		auto up = DirectX::XMLoadFloat3(&upVec);
		auto mtxWorldToView = DirectX::XMMatrixLookAtRH(cp, tp, up);
		auto mtxViewToClip = sl12::MatrixPerspectiveInfiniteFovRH(DirectX::XMConvertToRadians(60.0f), (float)displayWidth_ / (float)displayHeight_, 0.1f);
		auto mtxWorldToClip = mtxWorldToView * mtxViewToClip;

		SceneCB cbScene;
		DirectX::XMStoreFloat4x4(&cbScene.mtxWorldToProj, mtxWorldToClip);
		DirectX::XMStoreFloat4x4(&cbScene.mtxWorldToView, mtxWorldToView);

		auto hSceneCB = cbvMan_->GetTemporal(&cbScene, sizeof(cbScene));

		MeshCB cbMesh;
		DirectX::XMStoreFloat4x4(&cbMesh.mtxLocalToWorld, DirectX::XMMatrixIdentity());

		auto hMeshCB = cbvMan_->GetTemporal(&cbMesh, sizeof(cbMesh));

		// set descriptors.
		sl12::DescriptorSet descSet;
		descSet.Reset();
		descSet.SetVsCbv(0, hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetVsCbv(1, hMeshCB.GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetPsCbv(0, hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetPsSampler(0, linearSampler_->GetDescInfo().cpuHandle);

		pCmdList->GetLatestCommandList()->SetPipelineState(psoMesh_->GetPSO());
		pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		auto meshRes = hSuzanneMesh_.GetItem<sl12::ResourceItemMesh>();
		auto&& submeshes = meshRes->GetSubmeshes();
		auto submesh_count = submeshes.size();
		for (int i = 0; i < submesh_count; i++)
		{
			auto&& submesh = submeshes[i];
			auto&& material = meshRes->GetMaterials()[submesh.materialIndex];
			auto bc_tex_res = const_cast<sl12::ResourceItemTexture*>(material.baseColorTex.GetItem<sl12::ResourceItemTexture>());
			auto nm_tex_res = const_cast<sl12::ResourceItemTexture*>(material.normalTex.GetItem<sl12::ResourceItemTexture>());
			auto orm_tex_res = const_cast<sl12::ResourceItemTexture*>(material.ormTex.GetItem<sl12::ResourceItemTexture>());
			auto&& base_color_srv = bc_tex_res->GetTextureView();

			descSet.SetPsSrv(0, bc_tex_res->GetTextureView().GetDescInfo().cpuHandle);
			descSet.SetPsSrv(1, nm_tex_res->GetTextureView().GetDescInfo().cpuHandle);
			descSet.SetPsSrv(2, orm_tex_res->GetTextureView().GetDescInfo().cpuHandle);

			pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rsVsPs_, &descSet);

			const D3D12_VERTEX_BUFFER_VIEW vbvs[] = {
				submesh.positionVBV.GetView(),
				submesh.normalVBV.GetView(),
				submesh.tangentVBV.GetView(),
				submesh.texcoordVBV.GetView(),
			};
			pCmdList->GetLatestCommandList()->IASetVertexBuffers(0, ARRAYSIZE(vbvs), vbvs);

			auto&& ibv = submesh.indexBV.GetView();
			pCmdList->GetLatestCommandList()->IASetIndexBuffer(&ibv);

			pCmdList->GetLatestCommandList()->DrawIndexedInstanced(submesh.indexCount, 1, 0, 0, 0);
		}
	}

	// set render target.
	{
		auto&& rtv = swapchain.GetCurrentRenderTargetView(kSwapchainBufferOffset)->GetDescInfo().cpuHandle;
		pCmdList->GetLatestCommandList()->OMSetRenderTargets(1, &rtv, false, nullptr);

		D3D12_VIEWPORT vp;
		vp.TopLeftX = vp.TopLeftY = 0.0f;
		vp.Width = (float)displayWidth_;
		vp.Height = (float)displayHeight_;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		pCmdList->GetLatestCommandList()->RSSetViewports(1, &vp);

		D3D12_RECT rect;
		rect.left = rect.top = 0;
		rect.right = displayWidth_;
		rect.bottom = displayHeight_;
		pCmdList->GetLatestCommandList()->RSSetScissorRects(1, &rect);
	}

	// draw GUI.
	gui_->LoadDrawCommands(pCmdList);
	
	// barrier swapchain.
	pCmdList->TransitionBarrier(swapchain.GetCurrentTexture(kSwapchainBufferOffset), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	// wait prev frame render.
	mainCmdList_->Close();
	device_.WaitDrawDone();

	// present swapchain.
	device_.Present(0);

	// execute current frame render.
	mainCmdList_->Execute();

	return true;
}

void SampleApplication::Finalize()
{
	// wait render.
	device_.WaitDrawDone();
	device_.Present(1);

	// destroy render objects.
	gui_.Reset();
	psoMesh_.Reset();
	rsVsPs_.Reset();
	depthTex_.Reset();
	depthDSV_.Reset();
	cbvMan_.Reset();
	mainCmdList_.Reset();
	shaderMan_.Reset();
	resLoader_.Reset();
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
