﻿#include "sl12/resource_mesh.h"
#include "sl12/string_util.h"
#include "sl12/resource_texture.h"
#include "sl12/resource_streaming_texture.h"
#include "sl12/device.h"
#include "sl12/command_list.h"

#include <fstream>
#include <set>


namespace sl12
{
	namespace
	{
		struct BufferInitRenderCommand
			: public IRenderCommand
		{
			Device* pDevice;
			Buffer* pSrcBuffer;
			Buffer* pDstBuffer;
			D3D12_RESOURCE_STATES initState;

			~BufferInitRenderCommand()
			{
			}

			void LoadCommand(CommandList* pCmdlist) override
			{
				auto d3dCmdList = pCmdlist->GetLatestCommandList();
				d3dCmdList->CopyResource(pDstBuffer->GetResourceDep(), pSrcBuffer->GetResourceDep());
				pDevice->KillObject(pSrcBuffer);
				pCmdlist->TransitionBarrier(pDstBuffer, D3D12_RESOURCE_STATE_COPY_DEST, initState);
			}
		};	// struct BufferInitRenderCommand

		bool IsStreamingTexture(const std::string& path)
		{
			auto n = path.rfind(".stex");
			return n == (path.size() - 5);
		}
	}

	//---------------
	ResourceItemMesh::~ResourceItemMesh()
	{
	}

	//---------------
	ResourceItemBase* ResourceItemMesh::LoadFunction(ResourceLoader* pLoader, ResourceHandle handle, const std::string& filepath)
	{
		ResourceMesh mesh_bin;
		std::fstream ifs(pLoader->MakeFullPath(filepath), std::ios::in | std::ios::binary);
		cereal::BinaryInputArchive ar(ifs);
		ar(cereal::make_nvp("mesh", mesh_bin));

		if (mesh_bin.GetIndexBuffer().empty())
		{
			return nullptr;
		}

		std::unique_ptr<ResourceItemMesh> ret(new ResourceItemMesh(handle));

		// set bounding sphere.
		ret->boundingInfo_.sphere.center.x = mesh_bin.GetBoundingSphere().centerX;
		ret->boundingInfo_.sphere.center.y = mesh_bin.GetBoundingSphere().centerY;
		ret->boundingInfo_.sphere.center.z = mesh_bin.GetBoundingSphere().centerZ;
		ret->boundingInfo_.sphere.radius = mesh_bin.GetBoundingSphere().radius;
		ret->boundingInfo_.box.aabbMin.x = mesh_bin.GetBoundingBox().minX;
		ret->boundingInfo_.box.aabbMin.y = mesh_bin.GetBoundingBox().minY;
		ret->boundingInfo_.box.aabbMin.z = mesh_bin.GetBoundingBox().minZ;
		ret->boundingInfo_.box.aabbMax.x = mesh_bin.GetBoundingBox().maxX;
		ret->boundingInfo_.box.aabbMax.y = mesh_bin.GetBoundingBox().maxY;
		ret->boundingInfo_.box.aabbMax.z = mesh_bin.GetBoundingBox().maxZ;

		// create buffers.
		auto pDev = pLoader->GetDevice();
		auto pMeshMan = pLoader->GetMeshManager();
		assert(pDev != nullptr && pMeshMan != nullptr);
		auto CreateBuffer = [&](MeshManager::Handle* pHandle, const std::vector<u8>& data, size_t stride, u32 usage, bool isEmpyOk = false)
		{
			if (data.empty())
			{
				return isEmpyOk;
			}

			if (!pHandle)
			{
				return false;
			}

			// deploy to mesh manager.
			if (usage & ResourceUsage::VertexBuffer)
			{
				*pHandle = pMeshMan->DeployVertexBuffer(data.data(), data.size());
			}
			else if (usage & ResourceUsage::IndexBuffer)
			{
				*pHandle = pMeshMan->DeployIndexBuffer(data.data(), data.size());
			}

			return true;
		};
		if (!CreateBuffer(&ret->hPosition_, mesh_bin.GetVBPosition(), sizeof(DirectX::XMFLOAT3), ResourceUsage::VertexBuffer))
		{
			return nullptr;
		}
		if (!CreateBuffer(&ret->hNormal_, mesh_bin.GetVBNormal(), sizeof(DirectX::XMFLOAT3), ResourceUsage::VertexBuffer))
		{
			return nullptr;
		}
		if (!CreateBuffer(&ret->hTangent_, mesh_bin.GetVBTangent(), sizeof(DirectX::XMFLOAT4), ResourceUsage::VertexBuffer))
		{
			return nullptr;
		}
		if (!CreateBuffer(&ret->hTexcoord_, mesh_bin.GetVBTexcoord(), sizeof(DirectX::XMFLOAT2), ResourceUsage::VertexBuffer))
		{
			return nullptr;
		}
		if (!CreateBuffer(&ret->hIndex_, mesh_bin.GetIndexBuffer(), sizeof(u32), ResourceUsage::IndexBuffer))
		{
			return nullptr;
		}
		if (!CreateBuffer(&ret->hMeshletPackedPrim_, mesh_bin.GetMeshletPackedPrimitive(), sizeof(u32), ResourceUsage::IndexBuffer, true))
		{
			return nullptr;
		}
		if (!CreateBuffer(&ret->hMeshletVertexIndex_, mesh_bin.GetMeshletVertexIndex(), sizeof(u32), ResourceUsage::IndexBuffer, true))
		{
			return nullptr;
		}

		auto path = sl12::GetFilePath(filepath);

		// create materials.
		auto&& src_materials = mesh_bin.GetMaterials();
		auto mat_count = src_materials.size();
		ret->mateirals_.resize(mat_count);
		for (size_t i = 0; i < mat_count; i++)
		{
			ret->mateirals_[i].name = src_materials[i].GetName();
			if (!src_materials[i].GetTextureNames()[0].empty())
			{
				std::string f = path + src_materials[i].GetTextureNames()[0];
				ret->mateirals_[i].baseColorTex = IsStreamingTexture(f) ? pLoader->LoadRequest<ResourceItemStreamingTexture>(f) : pLoader->LoadRequest<ResourceItemTexture>(f);
			}
			if (!src_materials[i].GetTextureNames()[1].empty())
			{
				std::string f = path + src_materials[i].GetTextureNames()[1];
				ret->mateirals_[i].normalTex = IsStreamingTexture(f) ? pLoader->LoadRequest<ResourceItemStreamingTexture>(f) : pLoader->LoadRequest<ResourceItemTexture>(f);
			}
			if (!src_materials[i].GetTextureNames()[2].empty())
			{
				std::string f = path + src_materials[i].GetTextureNames()[2];
				ret->mateirals_[i].ormTex = IsStreamingTexture(f) ? pLoader->LoadRequest<ResourceItemStreamingTexture>(f) : pLoader->LoadRequest<ResourceItemTexture>(f);
			}
			ret->mateirals_[i].baseColor = src_materials[i].GetBaseColor();
			ret->mateirals_[i].emissiveColor = src_materials[i].GetEmissiveColor();
			ret->mateirals_[i].roughness = src_materials[i].GetRoughness();
			ret->mateirals_[i].metallic = src_materials[i].GetMetallic();
			ret->mateirals_[i].isOpaque = src_materials[i].IsOpaque();
		}

		// create submeshes.
		auto&& src_submeshes = mesh_bin.GetSubmeshes();
		auto sub_count = src_submeshes.size();
		ret->Submeshes_.resize(sub_count);
		for (size_t i = 0; i < sub_count; i++)
		{
			auto&& src = src_submeshes[i];
			auto&& dst = ret->Submeshes_[i];

			dst.materialIndex = src.GetMaterialIndex();
			dst.vertexCount = src.GetVertexCount();
			dst.indexCount = src.GetIndexCount();

			dst.positionSizeBytes = ResourceItemMesh::GetPositionStride() * src.GetVertexCount();
			dst.normalSizeBytes = ResourceItemMesh::GetNormalStride() * src.GetVertexCount();
			dst.tangentSizeBytes = ResourceItemMesh::GetTangentStride() * src.GetVertexCount();
			dst.texcoordSizeBytes = ResourceItemMesh::GetTexcoordStride() * src.GetVertexCount();
			dst.indexSizeBytes = ResourceItemMesh::GetIndexStride() * src.GetIndexCount();
			dst.meshletPackedPrimSizeBytes = sizeof(u32) * src.GetMeshletPrimitiveCount();
			dst.meshletVertexIndexSizeBytes = ResourceItemMesh::GetIndexStride() * src.GetMeshletVertexIndexCount();

			dst.positionOffsetBytes = ResourceItemMesh::GetPositionStride() * src.GetVertexOffset();
			dst.normalOffsetBytes = ResourceItemMesh::GetNormalStride() * src.GetVertexOffset();
			dst.tangentOffsetBytes = ResourceItemMesh::GetTangentStride() * src.GetVertexOffset();
			dst.texcoordOffsetBytes = ResourceItemMesh::GetTexcoordStride() * src.GetVertexOffset();
			dst.indexOffsetBytes = ResourceItemMesh::GetIndexStride() * src.GetIndexOffset();
			dst.meshletPackedPrimOffsetBytes = sizeof(u32) * src.GetMeshletPrimitiveOffset();
			dst.meshletVertexIndexOffsetBytes = ResourceItemMesh::GetIndexStride() * src.GetMeshletVertexIndexOffset();

			dst.boundingInfo.sphere.center.x = src.GetBoundingSphere().centerX;
			dst.boundingInfo.sphere.center.y = src.GetBoundingSphere().centerY;
			dst.boundingInfo.sphere.center.z = src.GetBoundingSphere().centerZ;
			dst.boundingInfo.sphere.radius = src.GetBoundingSphere().radius;
			dst.boundingInfo.box.aabbMin.x = src.GetBoundingBox().minX;
			dst.boundingInfo.box.aabbMin.y = src.GetBoundingBox().minY;
			dst.boundingInfo.box.aabbMin.z = src.GetBoundingBox().minZ;
			dst.boundingInfo.box.aabbMax.x = src.GetBoundingBox().maxX;
			dst.boundingInfo.box.aabbMax.y = src.GetBoundingBox().maxY;
			dst.boundingInfo.box.aabbMax.z = src.GetBoundingBox().maxZ;

			// create meshlets.
			auto&& src_meshlets = src.GetMeshlets();
			if (!src_meshlets.empty())
			{
				auto let_count = src_meshlets.size();
				dst.meshlets.resize(let_count);
				for (size_t j = 0; j < let_count; j++)
				{
					dst.meshlets[j].indexCount = src_meshlets[j].GetIndexCount();
					dst.meshlets[j].indexOffset = src_meshlets[j].GetIndexOffset();
					dst.meshlets[j].primitiveCount = src_meshlets[j].GetPrimitiveCount();
					dst.meshlets[j].primitiveOffset = src_meshlets[j].GetPrimitiveOffset();
					dst.meshlets[j].vertexIndexCount = src_meshlets[j].GetVertexIndexCount();
					dst.meshlets[j].vertexIndexOffset = src_meshlets[j].GetVertexIndexOffset();

					dst.meshlets[j].boundingInfo.sphere.center.x = src_meshlets[j].GetBoundingSphere().centerX;
					dst.meshlets[j].boundingInfo.sphere.center.y = src_meshlets[j].GetBoundingSphere().centerY;
					dst.meshlets[j].boundingInfo.sphere.center.z = src_meshlets[j].GetBoundingSphere().centerZ;
					dst.meshlets[j].boundingInfo.sphere.radius = src_meshlets[j].GetBoundingSphere().radius;
					dst.meshlets[j].boundingInfo.box.aabbMin.x = src_meshlets[j].GetBoundingBox().minX;
					dst.meshlets[j].boundingInfo.box.aabbMin.y = src_meshlets[j].GetBoundingBox().minY;
					dst.meshlets[j].boundingInfo.box.aabbMin.z = src_meshlets[j].GetBoundingBox().minZ;
					dst.meshlets[j].boundingInfo.box.aabbMax.x = src_meshlets[j].GetBoundingBox().maxX;
					dst.meshlets[j].boundingInfo.box.aabbMax.y = src_meshlets[j].GetBoundingBox().maxY;
					dst.meshlets[j].boundingInfo.box.aabbMax.z = src_meshlets[j].GetBoundingBox().maxZ;
					dst.meshlets[j].boundingInfo.cone.apex.x = src_meshlets[j].GetCone().apexX;
					dst.meshlets[j].boundingInfo.cone.apex.y = src_meshlets[j].GetCone().apexY;
					dst.meshlets[j].boundingInfo.cone.apex.z = src_meshlets[j].GetCone().apexZ;
					dst.meshlets[j].boundingInfo.cone.axis.x = src_meshlets[j].GetCone().axisX;
					dst.meshlets[j].boundingInfo.cone.axis.y = src_meshlets[j].GetCone().axisY;
					dst.meshlets[j].boundingInfo.cone.axis.z = src_meshlets[j].GetCone().axisZ;
					dst.meshlets[j].boundingInfo.cone.cutoff = src_meshlets[j].GetCone().cutoff;
				}
			}
		}

		// create box to local transform.
		DirectX::XMVECTOR aabbMin = DirectX::XMLoadFloat3(&ret->boundingInfo_.box.aabbMin);
		DirectX::XMVECTOR aabbMax = DirectX::XMLoadFloat3(&ret->boundingInfo_.box.aabbMax);
		DirectX::XMVECTOR boxSize = DirectX::XMVectorSubtract(aabbMax, aabbMin);
		DirectX::XMVECTOR boxCenter = DirectX::XMVectorScale(DirectX::XMVectorAdd(aabbMax, aabbMin), 0.5f);
		DirectX::XMMATRIX mbox = DirectX::XMMatrixMultiply(
			DirectX::XMMatrixScaling(boxSize.m128_f32[0], boxSize.m128_f32[1], boxSize.m128_f32[2]),
			DirectX::XMMatrixTranslation(boxCenter.m128_f32[0], boxCenter.m128_f32[1], boxCenter.m128_f32[2]));
		DirectX::XMStoreFloat4x4(&ret->mtxBoxToLocal_, mbox);

		return ret.release();
	}

}	// namespace sl12


//	EOF
