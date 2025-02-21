#pragma once

#include "sl12/application.h"
#include "sl12/resource_loader.h"
#include "sl12/shader_manager.h"
#include "sl12/command_list.h"
#include "sl12/gui.h"
#include "sl12/root_signature.h"
#include "sl12/pipeline_state.h"
#include "sl12/unique_handle.h"
#include "sl12/cbv_manager.h"
#include "sl12/render_graph.h"
#include "sl12/texture_streamer.h"
#include "sl12/resource_mesh.h"
#include "sl12/resource_streaming_texture.h"
#include "sl12/render_graph2.h"

struct ShaderID
{
	enum Value
	{
		Mesh_VV,
		Mesh_P,
		Lighting_C,
		Fullscreen_VV,
		Tonemap_P,

		Max
	};
};

class SceneRenderState
{
public:
	SceneRenderState(sl12::Device* pDev)
		: pDevice_(pDev)
	{
		// create linear sampler.
		{
			linearSampler_ = sl12::MakeUnique<sl12::Sampler>(pDev);

			D3D12_SAMPLER_DESC desc{};
			desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.MaxLOD = FLT_MAX;
			desc.MinLOD = 0.0f;
			desc.MipLODBias = 0.0f;
			linearSampler_->Initialize(pDev, desc);
		}
	}

	void SetRenderObjects(sl12::CbvManager* csvMan)
	{
		pCsvMan_ = csvMan;
	}
	void SetScreenSize(sl12::u32 w, sl12::u32 h)
	{
		screenWidth_ = w;
		screenHeight_ = h;
	}
	void SetShaderHandles(const sl12::ShaderHandle Shaders[ShaderID::Max])
	{
		for (int i = 0; i < ShaderID::Max; i++)
		{
			hShaders_[i] = Shaders[i];
		}
	}
	void SetResMesh(sl12::ResourceHandle h)
	{
		hResMesh_ = h;
	}
	void SetFrameResource(sl12::ConstantBufferView* sceneCBV)
	{
		pSceneCBV_ = sceneCBV;
	}
	void AddPass(std::unique_ptr<sl12::IRenderPass>& pass)
	{
		renderPasses_.push_back(std::move(pass));
	}

	sl12::Device* GetDevice()
	{
		return pDevice_;
	}
	sl12::CbvManager* GetCsvMan()
	{
		return pCsvMan_;
	}
	sl12::u32 GetScreenWidth() const
	{
		return screenWidth_;
	}
	sl12::u32 GetScreenHeight() const
	{
		return screenHeight_;
	}
	sl12::ShaderHandle GetShaderHandle(int id)
	{
		return hShaders_[id];
	}
	sl12::ResourceHandle GetResMesh()
	{
		return hResMesh_;
	}
	sl12::Sampler* GetLinearSampler()
	{
		return &linearSampler_;
	}
	sl12::ConstantBufferView* GetSceneCBV()
	{
		return pSceneCBV_;
	}

private:
	sl12::Device*		pDevice_;
	sl12::CbvManager*	pCsvMan_;
	sl12::u32			screenWidth_, screenHeight_;
	sl12::ShaderHandle	hShaders_[ShaderID::Max];
	sl12::ResourceHandle	hResMesh_;

	sl12::UniqueHandle<sl12::Sampler>	linearSampler_;

	sl12::ConstantBufferView*	pSceneCBV_;
	
	std::vector<std::unique_ptr<sl12::IRenderPass>>	renderPasses_;

public:
	static void InitInstance(sl12::Device* pDev);
	static SceneRenderState* GetInstance();
	static void DestroyInstance();

private:
	static std::unique_ptr<SceneRenderState> instance_;
};

void SetupRenderGraph(sl12::Device* pDev, sl12::RenderGraph2* pRenderGraph);
void CompileRenderGraph(sl12::Device* pDev, sl12::RenderGraph2* pRenderGraph, sl12::Texture* pSwapchain);

//	EOF
