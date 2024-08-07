﻿#include "sl12/scene_mesh.h"

#include "sl12/device.h"
#include "sl12/command_list.h"


namespace sl12
{
	namespace
	{
		struct MeshletBound
		{
			DirectX::XMFLOAT3	aabbMin;
			DirectX::XMFLOAT3	aabbMax;
			DirectX::XMFLOAT3	coneApex;
			DirectX::XMFLOAT3	coneAxis;
			float				coneCutoff;
			sl12::u32			pad[3];
		};	// struct MeshletBound

		struct MeshletDrawInfo
		{
			sl12::u32	indexOffset;
			sl12::u32	indexCount;
			sl12::u32	pad[2];
		};	// struct MeshletDrawInfo
	}


	//------------
	//----
	SceneSubmesh::SceneSubmesh(Device* pDevice, const ResourceItemMesh::Submesh& submesh)
		: pParentDevice_(pDevice)
	{
		// create buffers.
		pMeshletBoundsB_ = new Buffer();
		pMeshletDrawInfoB_ = new Buffer();
		pMeshletBoundsBV_ = new BufferView();
		pMeshletDrawInfoBV_ = new BufferView();
		pBoundsStaging_ = new Buffer();
		pDrawInfoStaging_ = new Buffer();
		pMeshletCB_ = new Buffer();
		pMeshletCBV_ = new ConstantBufferView();

		BufferDesc creationDesc{};
		creationDesc.size = sizeof(MeshletBound) * submesh.meshlets.size();
		creationDesc.stride = sizeof(MeshletBound);
		creationDesc.usage = ResourceUsage::ShaderResource;
		creationDesc.heap = BufferHeap::Default;
		creationDesc.initialState = D3D12_RESOURCE_STATE_COMMON;
		pMeshletBoundsB_->Initialize(pDevice, creationDesc);

		creationDesc.heap = BufferHeap::Dynamic;
		creationDesc.initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
		pBoundsStaging_->Initialize(pDevice, creationDesc);

		creationDesc.size = sizeof(MeshletDrawInfo) * submesh.meshlets.size();
		creationDesc.stride = sizeof(MeshletDrawInfo);
		creationDesc.heap = BufferHeap::Default;
		creationDesc.initialState = D3D12_RESOURCE_STATE_COMMON;
		pMeshletDrawInfoB_->Initialize(pDevice, creationDesc);

		creationDesc.heap = BufferHeap::Dynamic;
		creationDesc.initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
		pDrawInfoStaging_->Initialize(pDevice, creationDesc);

		pMeshletBoundsBV_->Initialize(pDevice, pMeshletBoundsB_, 0, 0, sizeof(MeshletBound));
		pMeshletDrawInfoBV_->Initialize(pDevice, pMeshletDrawInfoB_, 0, 0, sizeof(MeshletDrawInfo));

		creationDesc.size = sizeof(MeshletCB);
		creationDesc.stride = 0;
		creationDesc.usage = ResourceUsage::ConstantBuffer;
		creationDesc.heap = BufferHeap::Dynamic;
		creationDesc.initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
		pMeshletCB_->Initialize(pDevice, creationDesc);
		pMeshletCBV_->Initialize(pDevice, pMeshletCB_);

		// upload meshlet data.
		auto bound = (MeshletBound*)pBoundsStaging_->Map();
		auto draw_info = (MeshletDrawInfo*)pDrawInfoStaging_->Map();
		for (auto&& meshlet : submesh.meshlets)
		{
			bound->aabbMin = meshlet.boundingInfo.box.aabbMin;
			bound->aabbMax = meshlet.boundingInfo.box.aabbMax;
			bound->coneApex = meshlet.boundingInfo.cone.apex;
			bound->coneAxis = meshlet.boundingInfo.cone.axis;
			bound->coneCutoff = meshlet.boundingInfo.cone.cutoff;

			draw_info->indexOffset = meshlet.indexOffset;
			draw_info->indexCount = meshlet.indexCount;

			bound++;
			draw_info++;
		}
		pBoundsStaging_->Unmap();
		pDrawInfoStaging_->Unmap();
	}

	//----
	SceneSubmesh::~SceneSubmesh()
	{
		if (pParentDevice_)
		{
			pParentDevice_->KillObject(pMeshletCBV_);
			pParentDevice_->KillObject(pMeshletCB_);
			pParentDevice_->KillObject(pMeshletBoundsBV_);
			pParentDevice_->KillObject(pMeshletDrawInfoBV_);
			pParentDevice_->KillObject(pMeshletBoundsB_);
			pParentDevice_->KillObject(pMeshletDrawInfoB_);

			if (pBoundsStaging_)
			{
				pParentDevice_->KillObject(pBoundsStaging_);
			}
			if (pDrawInfoStaging_)
			{
				pParentDevice_->KillObject(pDrawInfoStaging_);
			}
		}
	}

	//----
	void SceneSubmesh::BeginNewFrame(CommandList* pCmdList)
	{
		if (pBoundsStaging_)
		{
			pCmdList->GetLatestCommandList()->CopyBufferRegion(pMeshletBoundsB_->GetResourceDep(), 0, pBoundsStaging_->GetResourceDep(), 0, pBoundsStaging_->GetBufferDesc().size);
			pParentDevice_->KillObject(pBoundsStaging_);
			pBoundsStaging_ = nullptr;
		}
		if (pDrawInfoStaging_)
		{
			pCmdList->GetLatestCommandList()->CopyBufferRegion(pMeshletDrawInfoB_->GetResourceDep(), 0, pDrawInfoStaging_->GetResourceDep(), 0, pDrawInfoStaging_->GetBufferDesc().size);
			pParentDevice_->KillObject(pDrawInfoStaging_);
			pDrawInfoStaging_ = nullptr;
		}
	}


	//------------
	//----
	SceneMesh::SceneMesh(sl12::Device* pDevice, const sl12::ResourceItemMesh* pSrcMesh)
		: pParentDevice_(pDevice)
		, pParentResource_(pSrcMesh)
		, mtxLocalToWorld_(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f)
	{
		// count total meshlets.
		auto&& submeshes = pSrcMesh->GetSubmeshes();
		u32 submesh_count = (u32)submeshes.size();
		u32 total_meshlets_count = 0;
		for (auto&& submesh : submeshes)
		{
			total_meshlets_count += (sl12::u32)submesh.meshlets.size();
		}

		// create buffers.
		pIndirectArgBuffer_ = new sl12::Buffer();
		pIndirectCountBuffer_ = new sl12::Buffer();
		pFalseNegativeBuffer_ = new sl12::Buffer();
		pFalseNegativeCountBuffer_ = new sl12::Buffer();
		pIndirectArgUAV_ = new sl12::UnorderedAccessView();
		pIndirectCountUAV_ = new sl12::UnorderedAccessView();
		pFalseNegativeUAV_ = new sl12::UnorderedAccessView();
		pFalseNegativeCountUAV_ = new sl12::UnorderedAccessView();

		u32 argSize = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);

		BufferDesc creationDesc{};
		creationDesc.size = argSize * total_meshlets_count * 2;
		creationDesc.stride = argSize;
		creationDesc.usage = ResourceUsage::ShaderResource | ResourceUsage::UnorderedAccess;
		creationDesc.heap = BufferHeap::Default;
		creationDesc.initialState = D3D12_RESOURCE_STATE_COMMON;
		pIndirectArgBuffer_->Initialize(pDevice, creationDesc);

		creationDesc.size = sizeof(u32) * submesh_count * 2;
		creationDesc.stride = sizeof(u32);
		pIndirectCountBuffer_->Initialize(pDevice, creationDesc);

		creationDesc.size = sizeof(u32) * total_meshlets_count;
		creationDesc.stride = sizeof(u32);
		pFalseNegativeBuffer_->Initialize(pDevice, creationDesc);

		creationDesc.size = sizeof(u32) * submesh_count;
		creationDesc.stride = sizeof(u32);
		pFalseNegativeCountBuffer_->Initialize(pDevice, creationDesc);

		pIndirectArgUAV_->Initialize(
			pDevice,
			pIndirectArgBuffer_,
			0, 0, 0, 0);
		pIndirectCountUAV_->Initialize(
			pDevice,
			pIndirectCountBuffer_,
			0, 0, 0, 0);
		pFalseNegativeUAV_->Initialize(
			pDevice,
			pFalseNegativeBuffer_,
			0, 0, 0, 0);
		pFalseNegativeCountUAV_->Initialize(
			pDevice,
			pFalseNegativeCountBuffer_,
			0, 0, 0, 0);

		// create submeshes.
		sl12::u32 submesh_offset = 0;
		sl12::u32 meshlets_offset = 0;
		for (auto&& submesh : submeshes)
		{
			auto meshlets_count = (sl12::u32)submesh.meshlets.size();
			auto ss = std::make_unique<SceneSubmesh>(pDevice, submesh);

			auto cb = &ss->cbData_;
			cb->meshletCount = meshlets_count;
			cb->indirectArg1stIndexOffset = (meshlets_offset * 2);
			cb->indirectArg2ndIndexOffset = (meshlets_offset * 2 + meshlets_count);
			cb->indirectCount1stByteOffset = sizeof(sl12::u32) * (submesh_offset * 2);
			cb->indirectCount2ndByteOffset = sizeof(sl12::u32) * (submesh_offset * 2 + 1);
			cb->falseNegativeIndexOffset = meshlets_offset;
			cb->falseNegativeCountByteOffset = sizeof(sl12::u32) * submesh_offset;
			memcpy(ss->pMeshletCB_->Map(), cb, sizeof(*cb));
			ss->pMeshletCB_->Unmap();

			sceneSubmeshes_.push_back(std::move(ss));
			submesh_offset++;
			meshlets_offset += meshlets_count;
		}

		mtxPrevLocalToWorld_ = mtxLocalToWorld_;
	}

	//----
	SceneMesh::~SceneMesh()
	{
		sceneSubmeshes_.clear();

		if (pParentDevice_)
		{
			pParentDevice_->KillObject(pIndirectArgUAV_);
			pParentDevice_->KillObject(pIndirectCountUAV_);
			pParentDevice_->KillObject(pFalseNegativeUAV_);
			pParentDevice_->KillObject(pFalseNegativeCountUAV_);

			pParentDevice_->KillObject(pIndirectArgBuffer_);
			pParentDevice_->KillObject(pIndirectCountBuffer_);
			pParentDevice_->KillObject(pFalseNegativeBuffer_);
			pParentDevice_->KillObject(pFalseNegativeCountBuffer_);

			for (auto&& v : materialCBVs_)
				pParentDevice_->KillObject(v);
			materialCBVs_.clear();
			pParentDevice_->KillObject(pMaterialCB_);
		}
	}

	//----
	void SceneMesh::BeginNewFrame(CommandList* pCmdList)
	{
		for (auto&& v : sceneSubmeshes_)
		{
			v->BeginNewFrame(pCmdList);
		}

		if (!pMaterialCB_)
		{
			auto&& materials = pParentResource_->GetMaterials();
			u32 matDataSize = (u32)GetAlignedSize(sizeof(MeshMaterialData), (size_t)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
			pMaterialCB_ = new Buffer();

			BufferDesc creationDesc{};
			creationDesc.size = matDataSize * materials.size();
			creationDesc.usage = ResourceUsage::ConstantBuffer;
			pMaterialCB_->Initialize(pParentDevice_, creationDesc);

			Buffer* pCopySrc = new Buffer();

			creationDesc.size = matDataSize * materials.size();
			creationDesc.usage = ResourceUsage::Unknown;
			creationDesc.heap = BufferHeap::Dynamic;
			pCopySrc->Initialize(pParentDevice_, creationDesc);

			u8* pData = (u8*)pCopySrc->Map();
			materialCBVs_.resize(materials.size());
			for (size_t i = 0; i < materials.size(); i++)
			{
				auto cbv = new ConstantBufferView();
				cbv->Initialize(pParentDevice_, pMaterialCB_, i * matDataSize, matDataSize);
				materialCBVs_[i] = cbv;

				MeshMaterialData* md = (MeshMaterialData*)(pData + matDataSize * i);
				md->baseColor = materials[i].baseColor;
				md->emissiveColor = materials[i].emissiveColor;
				md->roughness = materials[i].roughness;
				md->metallic = materials[i].metallic;
			}
			pCopySrc->Unmap();

			pCmdList->GetLatestCommandList()->CopyBufferRegion(
				pMaterialCB_->GetResourceDep(),
				0,
				pCopySrc->GetResourceDep(),
				0,
				matDataSize * materials.size());
			pCmdList->TransitionBarrier(pMaterialCB_, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);

			pParentDevice_->KillObject(pCopySrc);
		}

		LoadUpdateMaterialCommand(pCmdList);
	}

	//----
	void SceneMesh::CreateRenderCommand(CbvManager* pCbvMan, RenderCommandsList& outRenderCmds)
	{
		auto ret = std::make_unique<MeshRenderCommand>(this, pCbvMan);
		mtxPrevLocalToWorld_ = mtxLocalToWorld_;
		outRenderCmds.push_back(std::move(ret));
	}

	//----
	void SceneMesh::UpdateMaterial(u32 index, const MeshMaterialData& data)
	{
		if (index < materialCBVs_.size())
		{
			updateMaterials_[index] = data;
		}
	}

	//----
	void SceneMesh::LoadUpdateMaterialCommand(CommandList* pCmdList)
	{
		if (!updateMaterials_.empty())
		{
			pCmdList->TransitionBarrier(pMaterialCB_, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
			for (auto&& data : updateMaterials_)
			{
				u32 matDataSize = (sizeof(MeshMaterialData) + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1) / D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT * D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
				pParentDevice_->CopyToBuffer(pCmdList, pMaterialCB_, data.first * matDataSize, &data.second, sizeof(data.second));
			}
			updateMaterials_.clear();
			pCmdList->TransitionBarrier(pMaterialCB_, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
		}
	}

}	// namespace sl12
//	EOF
