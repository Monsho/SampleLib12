﻿#include "sample_render_pass.h"

#include "sl12/texture_view.h"
#include "sl12/descriptor_set.h"

#define USE_IN_CPP
#include "../shaders/cbuffer.hlsli"

namespace
{
	static const sl12::TransientResourceID kDepthBufferID("DepthBuffer");
	static const sl12::TransientResourceID kGBufferAID("GBufferA");
	static const sl12::TransientResourceID kGBufferBID("GBufferB");
	static const sl12::TransientResourceID kGBufferCID("GBufferC");
	static const sl12::TransientResourceID kLightResultID("LightResult");
	static const sl12::TransientResourceID kSwapchainID("Swapchain");
	static const sl12::TransientResourceID kLightBufferID("LightBuffer");

	static const DXGI_FORMAT	kDepthFormat = DXGI_FORMAT_D32_FLOAT;
	static const DXGI_FORMAT	kGBufferAFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	static const DXGI_FORMAT	kGBufferBFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	static const DXGI_FORMAT	kGBufferCFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
	static const DXGI_FORMAT	kLightResultFormat = DXGI_FORMAT_R11G11B10_FLOAT;
}

std::unique_ptr<SceneRenderState> SceneRenderState::instance_;

//----
void SceneRenderState::InitInstance(sl12::Device* pDev)
{
	if (!instance_)
	{
		instance_ = std::make_unique<SceneRenderState>(pDev);
	}
}

//----
SceneRenderState* SceneRenderState::GetInstance()
{
	return instance_.get();
}

void SceneRenderState::DestroyInstance()
{
	instance_.reset();
}


//----------------
// render passes.
class DepthPrePass : public sl12::IRenderPass
{
public:
	DepthPrePass()
	{
		auto state = SceneRenderState::GetInstance();
		pDevice_ = state->GetDevice();
		rootSig_ = sl12::MakeUnique<sl12::RootSignature>(pDevice_);
		pso_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDevice_);
		{
			rootSig_->InitializeWithDynamicResource(state->GetDevice(), 2, 0, 0, 0, 0);

			sl12::GraphicsPipelineStateDesc desc{};
			desc.pRootSignature = &rootSig_;
			desc.pVS = state->GetShaderHandle(ShaderID::Mesh_VV).GetShader();

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
				{"POSITION", 0, sl12::ResourceItemMesh::GetPositionFormat(), 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{"NORMAL",   0, sl12::ResourceItemMesh::GetNormalFormat(),   1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{"TANGENT",  0, sl12::ResourceItemMesh::GetTangentFormat(),  2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{"TEXCOORD", 0, sl12::ResourceItemMesh::GetTexcoordFormat(), 3, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			};
			desc.inputLayout.numElements = ARRAYSIZE(input_elems);
			desc.inputLayout.pElements = input_elems;

			desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			desc.numRTVs = 0;
			desc.dsvFormat = kDepthFormat;
			desc.multisampleCount = 1;

			if (!pso_->Initialize(pDevice_, desc))
			{
				sl12::ConsolePrint("Error: failed to init depth pre pass pso.");
				assert(!"create pso error.");
			}
		}
	}
	virtual ~DepthPrePass()
	{}
	
	virtual std::vector<sl12::TransientResource> GetInputResources() const
	{
		std::vector<sl12::TransientResource> ret;
		return ret;
	}
	virtual std::vector<sl12::TransientResource> GetOutputResources() const
	{
		auto state = SceneRenderState::GetInstance();

		std::vector<sl12::TransientResource> ret;
		sl12::TransientResource depth(kDepthBufferID, sl12::TransientState::DepthStencil);

		depth.desc.bIsTexture = true;
		depth.desc.textureDesc.Initialize2D(kDepthFormat, state->GetScreenWidth(), state->GetScreenHeight(), 1, 1, 0);

		ret.push_back(depth);
		return ret;
	}
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager)
	{
		auto state = SceneRenderState::GetInstance();
		auto width = state->GetScreenWidth();
		auto height = state->GetScreenHeight();
		auto hResMesh = state->GetResMesh();

		auto depthBuffer = pResManager->GetRenderGraphResource(kDepthBufferID)->pTexture;
		sl12::UniqueHandle<sl12::DepthStencilView> dsv = sl12::MakeUnique<sl12::DepthStencilView>(pDevice_);
		dsv->Initialize(pDevice_, depthBuffer);

		// clear depth.
		D3D12_CPU_DESCRIPTOR_HANDLE hDSV = dsv->GetDescInfo().cpuHandle;
		pCmdList->GetLatestCommandList()->ClearDepthStencilView(hDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

		// set render targets.
		pCmdList->GetLatestCommandList()->OMSetRenderTargets(0, nullptr, false, &hDSV);

		// set viewport.
		D3D12_VIEWPORT vp;
		vp.TopLeftX = vp.TopLeftY = 0.0f;
		vp.Width = (float)width;
		vp.Height = (float)height;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		pCmdList->GetLatestCommandList()->RSSetViewports(1, &vp);

		// set scissor rect.
		D3D12_RECT rect;
		rect.left = rect.top = 0;
		rect.right = width;
		rect.bottom = height;
		pCmdList->GetLatestCommandList()->RSSetScissorRects(1, &rect);

		// create constant buffer.
		MeshCB cbMesh;
		{
			auto meshRes = hResMesh.GetItem<sl12::ResourceItemMesh>();
			cbMesh.mtxLocalToWorld = meshRes->GetMtxBoxToLocal();
		}
		auto hMeshCB = state->GetCsvMan()->GetTemporal(&cbMesh, sizeof(cbMesh));

		{
			std::vector<std::vector<sl12::u32>> resIndices;
			resIndices.resize(1);
			resIndices[0].resize(2);

			resIndices[0][0] = state->GetSceneCBV()->GetDynamicDescInfo().index;
			resIndices[0][1] = hMeshCB.GetCBV()->GetDynamicDescInfo().index;

			// set pipeline.
			pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
			pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			auto meshRes = hResMesh.GetItem<sl12::ResourceItemMesh>();
			auto&& submeshes = meshRes->GetSubmeshes();
			auto submesh_count = submeshes.size();
			for (int i = 0; i < submesh_count; i++)
			{
				auto&& submesh = submeshes[i];
				auto&& material = meshRes->GetMaterials()[submesh.materialIndex];

				pCmdList->SetGraphicsRootSignatureAndDynamicResource(&rootSig_, resIndices);

				const D3D12_VERTEX_BUFFER_VIEW vbvs[] = {
					sl12::MeshManager::CreateVertexView(meshRes->GetPositionHandle(), submesh.positionOffsetBytes, submesh.positionSizeBytes, sl12::ResourceItemMesh::GetPositionStride()),
					sl12::MeshManager::CreateVertexView(meshRes->GetNormalHandle(), submesh.normalOffsetBytes, submesh.normalSizeBytes, sl12::ResourceItemMesh::GetNormalStride()),
					sl12::MeshManager::CreateVertexView(meshRes->GetTangentHandle(), submesh.tangentOffsetBytes, submesh.tangentSizeBytes, sl12::ResourceItemMesh::GetTangentStride()),
					sl12::MeshManager::CreateVertexView(meshRes->GetTexcoordHandle(), submesh.texcoordOffsetBytes, submesh.texcoordSizeBytes, sl12::ResourceItemMesh::GetTexcoordStride()),
				};
				pCmdList->GetLatestCommandList()->IASetVertexBuffers(0, ARRAYSIZE(vbvs), vbvs);

				auto ibv = sl12::MeshManager::CreateIndexView(meshRes->GetIndexHandle(), submesh.indexOffsetBytes, submesh.indexSizeBytes, sl12::ResourceItemMesh::GetIndexStride());
				pCmdList->GetLatestCommandList()->IASetIndexBuffer(&ibv);

				pCmdList->GetLatestCommandList()->DrawIndexedInstanced(submesh.indexCount, 1, 0, 0, 0);
			}
		}
	}

private:
	sl12::Device*									pDevice_;
	sl12::UniqueHandle<sl12::RootSignature>			rootSig_;
	sl12::UniqueHandle<sl12::GraphicsPipelineState>	pso_;
};

class GBufferPass : public sl12::IRenderPass
{
public:
	GBufferPass()
	{
		auto state = SceneRenderState::GetInstance();
		pDevice_ = state->GetDevice();
		rootSig_ = sl12::MakeUnique<sl12::RootSignature>(pDevice_);
		pso_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDevice_);
		{
			rootSig_->InitializeWithDynamicResource(state->GetDevice(), 2, 5, 0, 0, 0);

			sl12::GraphicsPipelineStateDesc desc{};
			desc.pRootSignature = &rootSig_;
			desc.pVS = state->GetShaderHandle(ShaderID::Mesh_VV).GetShader();
			desc.pPS = state->GetShaderHandle(ShaderID::Mesh_P).GetShader();

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
				{"POSITION", 0, sl12::ResourceItemMesh::GetPositionFormat(), 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{"NORMAL",   0, sl12::ResourceItemMesh::GetNormalFormat(),   1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{"TANGENT",  0, sl12::ResourceItemMesh::GetTangentFormat(),  2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{"TEXCOORD", 0, sl12::ResourceItemMesh::GetTexcoordFormat(), 3, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			};
			desc.inputLayout.numElements = ARRAYSIZE(input_elems);
			desc.inputLayout.pElements = input_elems;

			desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			desc.numRTVs = 0;
			desc.rtvFormats[desc.numRTVs++] = kGBufferAFormat;
			desc.rtvFormats[desc.numRTVs++] = kGBufferBFormat;
			desc.rtvFormats[desc.numRTVs++] = kGBufferCFormat;
			desc.dsvFormat = kDepthFormat;
			desc.multisampleCount = 1;

			if (!pso_->Initialize(pDevice_, desc))
			{
				sl12::ConsolePrint("Error: failed to init gbuffer pass pso.");
				assert(!"create pso error.");
			}
		}
	}
	virtual ~GBufferPass()
	{}

	virtual std::vector<sl12::TransientResource> GetInputResources() const
	{
		std::vector<sl12::TransientResource> ret;
		return ret;
	}
	virtual std::vector<sl12::TransientResource> GetOutputResources() const
	{
		auto state = SceneRenderState::GetInstance();

		std::vector<sl12::TransientResource> ret;
		sl12::TransientResource gbufferA(kGBufferAID, sl12::TransientState::RenderTarget);
		sl12::TransientResource gbufferB(kGBufferBID, sl12::TransientState::RenderTarget);
		sl12::TransientResource gbufferC(kGBufferCID, sl12::TransientState::RenderTarget);
		sl12::TransientResource depth(kDepthBufferID, sl12::TransientState::DepthStencil);

		gbufferA.desc.bIsTexture = true;
		gbufferA.desc.textureDesc.Initialize2D(kGBufferAFormat, state->GetScreenWidth(), state->GetScreenHeight(), 1, 1, 0);
		gbufferB.desc.bIsTexture = true;
		gbufferB.desc.textureDesc.Initialize2D(kGBufferBFormat, state->GetScreenWidth(), state->GetScreenHeight(), 1, 1, 0);
		gbufferC.desc.bIsTexture = true;
		gbufferC.desc.textureDesc.Initialize2D(kGBufferCFormat, state->GetScreenWidth(), state->GetScreenHeight(), 1, 1, 0);
		depth.desc.bIsTexture = true;
		depth.desc.textureDesc.Initialize2D(kDepthFormat, state->GetScreenWidth(), state->GetScreenHeight(), 1, 1, 0);

		ret.push_back(gbufferA);
		ret.push_back(gbufferB);
		ret.push_back(gbufferC);
		ret.push_back(depth);
		return ret;
	}
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager)
	{
		auto state = SceneRenderState::GetInstance();
		auto width = state->GetScreenWidth();
		auto height = state->GetScreenHeight();
		auto hResMesh = state->GetResMesh();

		auto gbufferA = pResManager->GetRenderGraphResource(kGBufferAID)->pTexture;
		auto gbufferB = pResManager->GetRenderGraphResource(kGBufferBID)->pTexture;
		auto gbufferC = pResManager->GetRenderGraphResource(kGBufferCID)->pTexture;
		auto depthBuffer = pResManager->GetRenderGraphResource(kDepthBufferID)->pTexture;
		sl12::UniqueHandle<sl12::RenderTargetView> rtvA = sl12::MakeUnique<sl12::RenderTargetView>(pDevice_);
		sl12::UniqueHandle<sl12::RenderTargetView> rtvB = sl12::MakeUnique<sl12::RenderTargetView>(pDevice_);
		sl12::UniqueHandle<sl12::RenderTargetView> rtvC = sl12::MakeUnique<sl12::RenderTargetView>(pDevice_);
		sl12::UniqueHandle<sl12::DepthStencilView> dsv = sl12::MakeUnique<sl12::DepthStencilView>(pDevice_);
		rtvA->Initialize(pDevice_, gbufferA);
		rtvB->Initialize(pDevice_, gbufferB);
		rtvC->Initialize(pDevice_, gbufferC);
		dsv->Initialize(pDevice_, depthBuffer);

		// set render targets.
		D3D12_CPU_DESCRIPTOR_HANDLE hRTVs[] = {
			rtvA->GetDescInfo().cpuHandle,
			rtvB->GetDescInfo().cpuHandle,
			rtvC->GetDescInfo().cpuHandle,
		};
		D3D12_CPU_DESCRIPTOR_HANDLE hDSV = dsv->GetDescInfo().cpuHandle;
		pCmdList->GetLatestCommandList()->OMSetRenderTargets(ARRAYSIZE(hRTVs), hRTVs, false, &hDSV);

		// set viewport.
		D3D12_VIEWPORT vp;
		vp.TopLeftX = vp.TopLeftY = 0.0f;
		vp.Width = (float)width;
		vp.Height = (float)height;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		pCmdList->GetLatestCommandList()->RSSetViewports(1, &vp);

		// set scissor rect.
		D3D12_RECT rect;
		rect.left = rect.top = 0;
		rect.right = width;
		rect.bottom = height;
		pCmdList->GetLatestCommandList()->RSSetScissorRects(1, &rect);

		// create constant buffer.
		MeshCB cbMesh;
		{
			auto meshRes = hResMesh.GetItem<sl12::ResourceItemMesh>();
			cbMesh.mtxLocalToWorld = meshRes->GetMtxBoxToLocal();
		}
		auto hMeshCB = state->GetCsvMan()->GetTemporal(&cbMesh, sizeof(cbMesh));

		{
			std::vector<std::vector<sl12::u32>> resIndices;
			resIndices.resize(2);
			resIndices[0].resize(2);
			resIndices[1].resize(5);

			resIndices[0][0] = resIndices[1][0] = state->GetSceneCBV()->GetDynamicDescInfo().index;
			resIndices[0][1] = hMeshCB.GetCBV()->GetDynamicDescInfo().index;
			resIndices[1][4] = state->GetLinearSampler()->GetDynamicDescInfo().index;

			// set pipeline.
			pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
			pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			auto meshRes = hResMesh.GetItem<sl12::ResourceItemMesh>();
			auto&& submeshes = meshRes->GetSubmeshes();
			auto submesh_count = submeshes.size();
			for (int i = 0; i < submesh_count; i++)
			{
				auto&& submesh = submeshes[i];
				auto&& material = meshRes->GetMaterials()[submesh.materialIndex];
				auto bc_tex_res = const_cast<sl12::ResourceItemTextureBase*>(material.baseColorTex.GetItem<sl12::ResourceItemTextureBase>());
				auto nm_tex_res = const_cast<sl12::ResourceItemTextureBase*>(material.normalTex.GetItem<sl12::ResourceItemTextureBase>());
				auto orm_tex_res = const_cast<sl12::ResourceItemTextureBase*>(material.ormTex.GetItem<sl12::ResourceItemTextureBase>());

				resIndices[1][1] = bc_tex_res->GetTextureView().GetDynamicDescInfo().index;
				resIndices[1][2] = nm_tex_res->GetTextureView().GetDynamicDescInfo().index;
				resIndices[1][3] = orm_tex_res->GetTextureView().GetDynamicDescInfo().index;

				pCmdList->SetGraphicsRootSignatureAndDynamicResource(&rootSig_, resIndices);

				const D3D12_VERTEX_BUFFER_VIEW vbvs[] = {
					sl12::MeshManager::CreateVertexView(meshRes->GetPositionHandle(), submesh.positionOffsetBytes, submesh.positionSizeBytes, sl12::ResourceItemMesh::GetPositionStride()),
					sl12::MeshManager::CreateVertexView(meshRes->GetNormalHandle(), submesh.normalOffsetBytes, submesh.normalSizeBytes, sl12::ResourceItemMesh::GetNormalStride()),
					sl12::MeshManager::CreateVertexView(meshRes->GetTangentHandle(), submesh.tangentOffsetBytes, submesh.tangentSizeBytes, sl12::ResourceItemMesh::GetTangentStride()),
					sl12::MeshManager::CreateVertexView(meshRes->GetTexcoordHandle(), submesh.texcoordOffsetBytes, submesh.texcoordSizeBytes, sl12::ResourceItemMesh::GetTexcoordStride()),
				};
				pCmdList->GetLatestCommandList()->IASetVertexBuffers(0, ARRAYSIZE(vbvs), vbvs);

				auto ibv = sl12::MeshManager::CreateIndexView(meshRes->GetIndexHandle(), submesh.indexOffsetBytes, submesh.indexSizeBytes, sl12::ResourceItemMesh::GetIndexStride());
				pCmdList->GetLatestCommandList()->IASetIndexBuffer(&ibv);

				pCmdList->GetLatestCommandList()->DrawIndexedInstanced(submesh.indexCount, 1, 0, 0, 0);
			}
		}
	}

private:
	sl12::Device*									pDevice_;
	sl12::UniqueHandle<sl12::RootSignature>			rootSig_;
	sl12::UniqueHandle<sl12::GraphicsPipelineState>	pso_;
};

class LightingPass : public sl12::IRenderPass
{
public:
	LightingPass()
	{
		auto state = SceneRenderState::GetInstance();
		pDevice_ = state->GetDevice();
		rootSig_ = sl12::MakeUnique<sl12::RootSignature>(pDevice_);
		pso_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDevice_);
		{
			rootSig_->InitializeWithDynamicResource(pDevice_, 7);

			sl12::ComputePipelineStateDesc desc{};
			desc.pCS = state->GetShaderHandle(ShaderID::Lighting_C).GetShader();

			desc.pRootSignature = &rootSig_;

			if (!pso_->Initialize(pDevice_, desc))
			{
				sl12::ConsolePrint("Error: failed to init lighting pso.");
				assert(!"create pso error.");
			}
		}
	}
	virtual ~LightingPass()
	{}

	virtual std::vector<sl12::TransientResource> GetInputResources() const
	{
		std::vector<sl12::TransientResource> ret;
		sl12::TransientResource gbufferA(kGBufferAID, sl12::TransientState::ShaderResource);
		sl12::TransientResource gbufferB(kGBufferBID, sl12::TransientState::ShaderResource);
		sl12::TransientResource gbufferC(kGBufferCID, sl12::TransientState::ShaderResource);
		sl12::TransientResource depth(kDepthBufferID, sl12::TransientState::ShaderResource);
		sl12::TransientResource light(kLightBufferID, sl12::TransientState::ShaderResource);
		ret.push_back(gbufferA);
		ret.push_back(gbufferB);
		ret.push_back(gbufferC);
		ret.push_back(depth);
		ret.push_back(light);
		return ret;
	}
	virtual std::vector<sl12::TransientResource> GetOutputResources() const
	{
		auto state = SceneRenderState::GetInstance();

		std::vector<sl12::TransientResource> ret;
		sl12::TransientResource result(kLightResultID, sl12::TransientState::UnorderedAccess);

		result.desc.bIsTexture = true;
		result.desc.textureDesc.Initialize2D(kLightResultFormat, state->GetScreenWidth(), state->GetScreenHeight(), 1, 1, 0);
		
		ret.push_back(result);
		return ret;
	}
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager)
	{
		auto state = SceneRenderState::GetInstance();
		auto width = state->GetScreenWidth();
		auto height = state->GetScreenHeight();

		auto gbufferA = pResManager->GetRenderGraphResource(kGBufferAID)->pTexture;
		auto gbufferB = pResManager->GetRenderGraphResource(kGBufferBID)->pTexture;
		auto gbufferC = pResManager->GetRenderGraphResource(kGBufferCID)->pTexture;
		auto depthBuffer = pResManager->GetRenderGraphResource(kDepthBufferID)->pTexture;
		auto lightBuffer = pResManager->GetRenderGraphResource(kLightBufferID)->pBuffer;
		auto lightResult = pResManager->GetRenderGraphResource(kLightResultID)->pTexture;

		sl12::UniqueHandle<sl12::TextureView> srvGA = sl12::MakeUnique<sl12::TextureView>(pDevice_);
		sl12::UniqueHandle<sl12::TextureView> srvGB = sl12::MakeUnique<sl12::TextureView>(pDevice_);
		sl12::UniqueHandle<sl12::TextureView> srvGC = sl12::MakeUnique<sl12::TextureView>(pDevice_);
		sl12::UniqueHandle<sl12::TextureView> srvDepth = sl12::MakeUnique<sl12::TextureView>(pDevice_);
		sl12::UniqueHandle<sl12::BufferView> srvLight = sl12::MakeUnique<sl12::BufferView>(pDevice_);
		sl12::UniqueHandle<sl12::UnorderedAccessView> uavResult = sl12::MakeUnique<sl12::UnorderedAccessView>(pDevice_);
		srvGA->Initialize(pDevice_, gbufferA);
		srvGB->Initialize(pDevice_, gbufferB);
		srvGC->Initialize(pDevice_, gbufferC);
		srvDepth->Initialize(pDevice_, depthBuffer);
		size_t size = lightBuffer->GetBufferDesc().size;
		size_t stride = lightBuffer->GetBufferDesc().stride;
		srvLight->Initialize(pDevice_, lightBuffer, 0, (sl12::u32)(size / stride), (sl12::u32)stride);
		uavResult->Initialize(pDevice_, lightResult);

		// set pipeline.
		pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());

		std::vector<sl12::u32> resIndices;
		resIndices.resize(7);
		resIndices[0] = state->GetSceneCBV()->GetDynamicDescInfo().index;
		resIndices[1] = srvGA->GetDynamicDescInfo().index;
		resIndices[2] = srvGB->GetDynamicDescInfo().index;
		resIndices[3] = srvGC->GetDynamicDescInfo().index;
		resIndices[4] = srvDepth->GetDynamicDescInfo().index;
		resIndices[5] = srvLight->GetDynamicDescInfo().index;
		resIndices[6] = uavResult->GetDynamicDescInfo().index;

		pCmdList->SetComputeRootSignatureAndDynamicResource(&rootSig_, resIndices);

		// dispatch.
		UINT x = (width + 7) / 8;
		UINT y = (height + 7) / 8;
		pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);
	}

private:
	sl12::Device*									pDevice_;
	sl12::UniqueHandle<sl12::RootSignature>			rootSig_;
	sl12::UniqueHandle<sl12::ComputePipelineState>	pso_;
};

class TonemapPass : public sl12::IRenderPass
{
public:
	TonemapPass()
	{
		auto state = SceneRenderState::GetInstance();
		pDevice_ = state->GetDevice();
		rootSig_ = sl12::MakeUnique<sl12::RootSignature>(pDevice_);
		pso_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDevice_);
		{
			rootSig_->InitializeWithDynamicResource(pDevice_, 0, 1, 0, 0, 0);

			sl12::GraphicsPipelineStateDesc desc{};
			desc.pRootSignature = &rootSig_;
			desc.pVS = state->GetShaderHandle(ShaderID::Fullscreen_VV).GetShader();
			desc.pPS = state->GetShaderHandle(ShaderID::Tonemap_P).GetShader();

			desc.blend.sampleMask = UINT_MAX;
			desc.blend.rtDesc[0].isBlendEnable = false;
			desc.blend.rtDesc[0].writeMask = 0xf;

			desc.rasterizer.cullMode = D3D12_CULL_MODE_NONE;
			desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
			desc.rasterizer.isDepthClipEnable = true;
			desc.rasterizer.isFrontCCW = true;

			desc.depthStencil.isDepthEnable = false;
			desc.depthStencil.isDepthWriteEnable = false;

			desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			desc.numRTVs = 0;
			desc.rtvFormats[desc.numRTVs++] = pDevice_->GetSwapchain().GetTexture(0)->GetResourceDesc().Format;
			desc.dsvFormat = DXGI_FORMAT_UNKNOWN;
			desc.multisampleCount = 1;

			if (!pso_->Initialize(pDevice_, desc))
			{
				sl12::ConsolePrint("Error: failed to init tonemap pso.");
				assert(!"create pso error.");
			}
		}
	}
	virtual ~TonemapPass()
	{}

	virtual std::vector<sl12::TransientResource> GetInputResources() const
	{
		std::vector<sl12::TransientResource> ret;
		sl12::TransientResource result(kLightResultID, sl12::TransientState::ShaderResource);
		ret.push_back(result);
		return ret;
	}
	virtual std::vector<sl12::TransientResource> GetOutputResources() const
	{
		auto state = SceneRenderState::GetInstance();

		std::vector<sl12::TransientResource> ret;
		sl12::TransientResource swapchain(kSwapchainID, sl12::TransientState::RenderTarget);

		swapchain.desc.bIsTexture = true;
		
		ret.push_back(swapchain);
		return ret;
	}
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager)
	{
		auto state = SceneRenderState::GetInstance();
		auto width = state->GetScreenWidth();
		auto height = state->GetScreenHeight();

		auto lightResult = pResManager->GetRenderGraphResource(kLightResultID)->pTexture;
		auto swapchain = pResManager->GetRenderGraphResource(kSwapchainID)->pTexture;

		sl12::UniqueHandle<sl12::TextureView> srvResult = sl12::MakeUnique<sl12::TextureView>(pDevice_);
		sl12::UniqueHandle<sl12::RenderTargetView> rtvSwap = sl12::MakeUnique<sl12::RenderTargetView>(pDevice_);
		srvResult->Initialize(pDevice_, lightResult);
		rtvSwap->Initialize(pDevice_, swapchain);
		
		// set render targets.
		auto&& rtv = rtvSwap->GetDescInfo().cpuHandle;
		pCmdList->GetLatestCommandList()->OMSetRenderTargets(1, &rtv, false, nullptr);

		// set viewport.
		D3D12_VIEWPORT vp;
		vp.TopLeftX = vp.TopLeftY = 0.0f;
		vp.Width = (float)width;
		vp.Height = (float)height;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		pCmdList->GetLatestCommandList()->RSSetViewports(1, &vp);

		// set scissor rect.
		D3D12_RECT rect;
		rect.left = rect.top = 0;
		rect.right = width;
		rect.bottom = height;
		pCmdList->GetLatestCommandList()->RSSetScissorRects(1, &rect);

		// set pipeline.
		pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
		pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		std::vector<std::vector<sl12::u32>> resIndices;
		resIndices.resize(1);
		resIndices[0].resize(1);
		resIndices[0][0] = srvResult->GetDynamicDescInfo().index;

		pCmdList->SetGraphicsRootSignatureAndDynamicResource(&rootSig_, resIndices);

		// draw fullscreen.
		pCmdList->GetLatestCommandList()->DrawInstanced(3, 1, 0, 0);
	}

private:
	sl12::Device*									pDevice_;
	sl12::UniqueHandle<sl12::RootSignature>			rootSig_;
	sl12::UniqueHandle<sl12::GraphicsPipelineState>	pso_;
};

class CopyLightDataPass : public sl12::IRenderPass
{
public:
	virtual std::vector<sl12::TransientResource> GetInputResources() const
	{
		std::vector<sl12::TransientResource> ret;
		return ret;
	}
	virtual std::vector<sl12::TransientResource> GetOutputResources() const
	{
		std::vector<sl12::TransientResource> ret;
		sl12::TransientResource light(kLightBufferID, sl12::TransientState::CopyDst);

		light.desc.bIsTexture = false;
		light.desc.bufferDesc.heap = sl12::BufferHeap::Default;
		light.desc.bufferDesc.stride = sizeof(LightData);
		light.desc.bufferDesc.size = light.desc.bufferDesc.stride * 1;
		light.desc.bufferDesc.usage = sl12::ResourceUsage::ShaderResource;
		
		ret.push_back(light);
		return ret;
	}
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Copy;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager)
	{
		auto state = SceneRenderState::GetInstance();
		auto pDev = state->GetDevice();
		
		sl12::UniqueHandle<sl12::Buffer> CopySrcBuffer = sl12::MakeUnique<sl12::Buffer>(pDev);
		sl12::BufferDesc desc;
		desc.heap = sl12::BufferHeap::Dynamic;
		desc.stride = sizeof(LightData);
		desc.size = desc.stride;
		desc.usage = sl12::ResourceUsage::Unknown;
		bool bInitSuccess = CopySrcBuffer->Initialize(pDev, desc);
		assert(bInitSuccess);

		float c = sinf(time_ / 360.0f * DirectX::XM_PI) * 0.5f + 0.5f;
		time_ += 1.0f;
		
		LightData* pData = reinterpret_cast<LightData*>(CopySrcBuffer->Map());
		pData->color = DirectX::XMFLOAT3(c, 0.0f, 0.0f);
		pData->dir = DirectX::XMFLOAT3(0.0f, -1.0f, 0.0f);
		CopySrcBuffer->Unmap();

		sl12::Buffer* pDstBuffer = pResManager->GetRenderGraphResource(kLightBufferID)->pBuffer;
		pCmdList->GetLatestCommandList()->CopyResource(pDstBuffer->GetResourceDep(), CopySrcBuffer->GetResourceDep());
	}

private:
	float	time_ = 0.0f;
};


void SetupRenderGraph(sl12::Device* pDev, sl12::RenderGraph* pRenderGraph)
{
	auto state = SceneRenderState::GetInstance();

	std::unique_ptr<sl12::IRenderPass> depth_pre_pass = std::make_unique<DepthPrePass>();
	std::unique_ptr<sl12::IRenderPass> gbuffer_pass = std::make_unique<GBufferPass>();
	std::unique_ptr<sl12::IRenderPass> lighting_pass = std::make_unique<LightingPass>();
	std::unique_ptr<sl12::IRenderPass> tonemap_pass = std::make_unique<TonemapPass>();
	std::unique_ptr<sl12::IRenderPass> copy_light_pass = std::make_unique<CopyLightDataPass>();
	pRenderGraph->AddPass(depth_pre_pass.get(), nullptr);
	pRenderGraph->AddPass(gbuffer_pass.get(), depth_pre_pass.get());
	pRenderGraph->AddPass(copy_light_pass.get(), gbuffer_pass.get());
	pRenderGraph->AddPass(lighting_pass.get(), copy_light_pass.get());
	// pRenderGraph->AddPass(copy_light_pass.get(), nullptr);
	// pRenderGraph->AddPass(lighting_pass.get(), { gbuffer_pass.get(), copy_light_pass.get() });
	pRenderGraph->AddPass(tonemap_pass.get(), lighting_pass.get());
	state->AddPass(depth_pre_pass);
	state->AddPass(gbuffer_pass);
	state->AddPass(lighting_pass);
	state->AddPass(tonemap_pass);
	state->AddPass(copy_light_pass);
}

void CompileRenderGraph(sl12::Device* pDev, sl12::RenderGraph* pRenderGraph, sl12::Texture* pSwapchain)
{
	pRenderGraph->AddExternalTexture(kSwapchainID, pSwapchain, sl12::TransientState::Present);
	pRenderGraph->Compile();
}

//	EOF
