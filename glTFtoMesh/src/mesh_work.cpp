#include "mesh_work.h"

#include <fstream>
#include <sstream>
#include <map>


using namespace Microsoft::glTF;

namespace
{
	static const sl12::ResourceMeshMaterialBlendType kBlendTypes[] =
	{
		sl12::ResourceMeshMaterialBlendType::Opaque,
		sl12::ResourceMeshMaterialBlendType::Opaque,
		sl12::ResourceMeshMaterialBlendType::Translucent,
		sl12::ResourceMeshMaterialBlendType::Masked,
	};
	
	std::string ConvYenToSlash(const std::string& path)
	{
		std::string ret;
		ret.reserve(path.length() + 1);
		for (auto&& it : path)
		{
			ret += (it == '\\') ? '/' : it;
		}
		return ret;
	}

	std::string GetExtent(const std::string& filename)
	{
		std::string ret;
		auto pos = filename.rfind('.');
		if (pos != std::string::npos)
		{
			ret = filename.data() + pos;
		}
		return ret;
	}

	class StreamReader : public IStreamReader
	{
	public:
		StreamReader(const std::string& p)
			: path_(p)
		{}

		std::shared_ptr<std::istream> GetInputStream(const std::string& filename) const override
		{
			auto stream = std::make_shared<std::ifstream>(path_ + filename, std::ios_base::binary);
			return stream;
		}

	private:
		std::string		path_;
	};

	struct MikkTSpaceMesh
	{
		std::vector<Vertex>&	vertices;
		std::vector<uint32_t>&	indices;

		MikkTSpaceMesh(std::vector<Vertex>& v, std::vector<uint32_t>& i)
			: vertices(v), indices(i)
		{}

		SMikkTSpaceContext GetContext()
		{
			static SMikkTSpaceInterface inter = {
				GetNumFaces,
				GetNumVerticesOfFace,
				GetPosition,
				GetNormal,
				GetTexCoord,
				SetTSpaceBasic,
				SetTSpace
			};

			SMikkTSpaceContext ret;
			ret.m_pInterface = &inter;
			ret.m_pUserData = this;
			ret.m_bIgnoreDegenerates = false;
			return ret;
		}

		static int GetNumFaces(const SMikkTSpaceContext * pContext)
		{
			auto mesh = (const MikkTSpaceMesh*)pContext->m_pUserData;
			return (int)(mesh->indices.size() / 3);
		}

		static int GetNumVerticesOfFace(const SMikkTSpaceContext * pContext, const int iFace)
		{
			return 3;
		}

		static void GetPosition(const SMikkTSpaceContext * pContext, float fvPosOut[], const int iFace, const int iVert)
		{
			auto mesh = (const MikkTSpaceMesh*)pContext->m_pUserData;
			auto index = mesh->indices[iFace * 3 + iVert];
			fvPosOut[0] = mesh->vertices[index].pos.x;
			fvPosOut[1] = mesh->vertices[index].pos.y;
			fvPosOut[2] = mesh->vertices[index].pos.z;
		}
		static void GetNormal(const SMikkTSpaceContext * pContext, float fvNormOut[], const int iFace, const int iVert)
		{
			auto mesh = (const MikkTSpaceMesh*)pContext->m_pUserData;
			auto index = mesh->indices[iFace * 3 + iVert];
			fvNormOut[0] = mesh->vertices[index].normal.x;
			fvNormOut[1] = mesh->vertices[index].normal.y;
			fvNormOut[2] = mesh->vertices[index].normal.z;
		}
		static void GetTexCoord(const SMikkTSpaceContext * pContext, float fvTexcOut[], const int iFace, const int iVert)
		{
			auto mesh = (const MikkTSpaceMesh*)pContext->m_pUserData;
			auto index = mesh->indices[iFace * 3 + iVert];
			fvTexcOut[0] = mesh->vertices[index].uv.x;
			fvTexcOut[1] = mesh->vertices[index].uv.y;
		}

		static void SetTSpaceBasic(const SMikkTSpaceContext * pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert)
		{
			auto mesh = (const MikkTSpaceMesh*)pContext->m_pUserData;
			auto index = mesh->indices[iFace * 3 + iVert];
			auto&& vertex = mesh->vertices[index];
			vertex.tangent.x = fvTangent[0];
			vertex.tangent.y = fvTangent[1];
			vertex.tangent.z = fvTangent[2];
			vertex.tangent.w = fSign;
		}

		static void SetTSpace(const SMikkTSpaceContext * pContext, const float fvTangent[], const float fvBiTangent[], const float fMagS, const float fMagT,
			const tbool bIsOrientationPreserving, const int iFace, const int iVert)
		{
			// not implemented.
		}
	};

	// from meshoptimizer
	static void ComputeBoundingSphere(const float* points, size_t count, DirectX::XMFLOAT3& resultCenter, float& resultRadius)
	{
		assert(count > 0);

		// find extremum points along all 3 axes; for each axis we get a pair of points with min/max coordinates
		size_t pmin[3] = { 0, 0, 0 };
		size_t pmax[3] = { 0, 0, 0 };

		for (size_t i = 0; i < count; ++i)
		{
			const float* p = points + i * 3;

			for (int axis = 0; axis < 3; ++axis)
			{
				pmin[axis] = (p[axis] < points[pmin[axis] * 3 + axis]) ? i : pmin[axis];
				pmax[axis] = (p[axis] > points[pmax[axis] * 3 + axis]) ? i : pmax[axis];
			}
		}

		// find the pair of points with largest distance
		float paxisd2 = 0;
		int paxis = 0;

		for (int axis = 0; axis < 3; ++axis)
		{
			const float* p1 = points + pmin[axis] * 3;
			const float* p2 = points + pmax[axis] * 3;

			float d2 = (p2[0] - p1[0]) * (p2[0] - p1[0]) + (p2[1] - p1[1]) * (p2[1] - p1[1]) + (p2[2] - p1[2]) * (p2[2] - p1[2]);

			if (d2 > paxisd2)
			{
				paxisd2 = d2;
				paxis = axis;
			}
		}

		// use the longest segment as the initial sphere diameter
		const float* p1 = points + pmin[paxis] * 3;
		const float* p2 = points + pmax[paxis] * 3;

		float center[3] = { (p1[0] + p2[0]) / 2, (p1[1] + p2[1]) / 2, (p1[2] + p2[2]) / 2 };
		float radius = sqrtf(paxisd2) / 2;

		// iteratively adjust the sphere up until all points fit
		for (size_t i = 0; i < count; ++i)
		{
			const float* p = points + i * 3;
			float d2 = (p[0] - center[0]) * (p[0] - center[0]) + (p[1] - center[1]) * (p[1] - center[1]) + (p[2] - center[2]) * (p[2] - center[2]);

			if (d2 > radius * radius)
			{
				float d = sqrtf(d2);
				assert(d > 0);

				float k = 0.5f + (radius / d) / 2;

				center[0] = center[0] * k + p[0] * (1 - k);
				center[1] = center[1] * k + p[1] * (1 - k);
				center[2] = center[2] * k + p[2] * (1 - k);
				radius = (radius + d) / 2;
			}
		}

		resultCenter.x = center[0];
		resultCenter.y = center[1];
		resultCenter.z = center[2];
		resultRadius = radius;
	}

}

bool MeshWork::ReadGLTFMesh(const std::string& inputPath, const std::string& inputFile)
{
	bool is_glb = false;
	if (GetExtent(inputFile) == ".glb")
	{
		is_glb = true;
	}

	// glTF stream initialize.
	auto stream_reader = std::make_unique<StreamReader>(inputPath);
	auto gltf_stream = stream_reader->GetInputStream(inputFile);
	std::unique_ptr<GLTFResourceReader> resource_reader;
	std::string manifest;
	if (!is_glb)
	{
		auto gltf_res_reader = std::make_unique<GLTFResourceReader>(std::move(stream_reader));

		std::stringstream manifestStream;
		manifestStream << gltf_stream->rdbuf();
		manifest = manifestStream.str();

		resource_reader = std::move(gltf_res_reader);
	}
	else
	{
		auto glb_res_reader = std::make_unique<GLBResourceReader>(std::move(stream_reader), std::move(gltf_stream));

		manifest = glb_res_reader->GetJson();

		resource_reader = std::move(glb_res_reader);
	}

	if (manifest.empty())
	{
		return false;
	}

	auto document = Deserialize(manifest);

	// read texture images.
	{
		textures_.reserve(document.images.Size());
		for (auto&& image : document.images.Elements())
		{
			auto work = std::make_unique<TextureWork>();

			auto data = resource_reader->ReadBinaryData(document, image);
			work->binary_.swap(data);

			// get image format from mimeType.
			static const std::string kImage("image/");
			size_t p = image.mimeType.find_first_of(kImage);
			if (p != std::string::npos)
			{
				std::string format = image.mimeType.substr(p + kImage.length());
				work->format_ = format;
			}
			
			textures_.push_back(std::move(work));
		}
	}

	// read materials.
	materials_.reserve(document.materials.Size());
	for (auto&& mat : document.materials.Elements())
	{
		std::unique_ptr<MaterialWork> work(new MaterialWork());

		work->name_ = mat.name;

		for (auto&& tex : mat.GetTextures())
		{
			if (tex.first.empty())
			{
				continue;
			}

			auto tex_index = std::stoi(tex.first);
			auto image_index = std::stoi(document.textures.Get(tex_index).imageId);
			auto tex_name = document.images.Get(image_index).uri;
			{
				tex_name = textures_[image_index]->name_;
				if (tex_name.empty())
				{
					tex_name = mat.name;
					switch (tex.second)
					{
					case TextureType::BaseColor:			tex_name += ".bc.png"; break;
					case TextureType::Normal:				tex_name += ".n.png"; break;
					case TextureType::MetallicRoughness:	tex_name += ".orm.png"; break;
					case TextureType::Emissive:				tex_name += ".em.png"; break;
					}
					textures_[image_index]->name_ = tex_name;
				}
			}
			switch (tex.second)
			{
			case TextureType::BaseColor:			work->textures_[MaterialWork::TextureKind::BaseColor] = tex_name; break;
			case TextureType::Normal:				work->textures_[MaterialWork::TextureKind::Normal] = tex_name; break;
			case TextureType::MetallicRoughness:	work->textures_[MaterialWork::TextureKind::ORM] = tex_name; break;
			case TextureType::Emissive:				work->textures_[MaterialWork::TextureKind::Emissive] = tex_name; break;
			}
		}

		auto&& PBR = mat.metallicRoughness;
		work->baseColor_ = DirectX::XMFLOAT4(PBR.baseColorFactor.r, PBR.baseColorFactor.g, PBR.baseColorFactor.b, PBR.baseColorFactor.a);
		work->emissiveColor_ = DirectX::XMFLOAT3(mat.emissiveFactor.r, mat.emissiveFactor.g, mat.emissiveFactor.b);
		work->roughness_ = PBR.roughnessFactor;
		work->metallic_ = PBR.metallicFactor;
		work->blendType_ = kBlendTypes[mat.alphaMode];
		work->cullMode_ = mat.doubleSided ? sl12::ResourceMeshMaterialCullMode::None : sl12::ResourceMeshMaterialCullMode::Back;

		materials_.push_back(std::move(work));
	}

	// read nodes.
	for (auto&& node : document.nodes.Elements())
	{
		NodeWork node_work{};
		DirectX::XMMATRIX matrix = DirectX::XMMatrixIdentity();

		DirectX::XMStoreFloat4x4(&node_work.transformLocal, matrix);
		node_work.meshIndex = -1;
		node_work.children.clear();

		if (!node.meshId.empty())
		{
			node_work.meshIndex = std::stoi(node.meshId);
		}
		for (auto&& child : node.children)
		{
			node_work.children.push_back(std::stoi(child));
		}

		if (node.GetTransformationType() == Microsoft::glTF::TransformationType::TRANSFORMATION_MATRIX)
		{
			node_work.transformLocal = DirectX::XMFLOAT4X4(node.matrix.values.data());
		}
		else if (node.GetTransformationType() == Microsoft::glTF::TransformationType::TRANSFORMATION_TRS)
		{
			auto t = DirectX::XMFLOAT3(node.translation.x, node.translation.y, node.translation.z);
			auto r = DirectX::XMFLOAT4(node.rotation.x, node.rotation.y, node.rotation.z, node.rotation.w);
			auto s = DirectX::XMFLOAT3(node.scale.x, node.scale.y, node.scale.z);

			auto T = DirectX::XMMatrixTranslation(t.x, t.y, t.z);
			auto R = DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&r));
			auto S = DirectX::XMMatrixScaling(s.x, s.y, s.z);

			matrix = DirectX::XMMatrixMultiply(S, R);
			matrix = DirectX::XMMatrixMultiply(matrix, T);

			DirectX::XMStoreFloat4x4(&node_work.transformLocal, matrix);
		}

		node_work.transformGlobal = node_work.transformLocal;

		nodes_.push_back(node_work);
	}
	// resolve children.
	for (auto&& node : nodes_)
	{
		DirectX::XMMATRIX mp = DirectX::XMLoadFloat4x4(&node.transformGlobal);
		for (auto&& child : node.children)
		{
			auto&& child_node = nodes_[child];
			DirectX::XMMATRIX mc = DirectX::XMLoadFloat4x4(&child_node.transformGlobal);
			mc = DirectX::XMMatrixMultiply(mc, mp);
			DirectX::XMStoreFloat4x4(&child_node.transformGlobal, mc);
		}
	}

	// read submeshes.
	std::vector<DirectX::XMFLOAT3> all_points;
	for (auto&& node : nodes_)
	{
		if (node.meshIndex < 0)
			continue;

		auto&& mesh = document.meshes[node.meshIndex];
		DirectX::XMMATRIX transform = DirectX::XMLoadFloat4x4(&node.transformGlobal);
		for (auto&& prim : mesh.primitives)
		{
			std::unique_ptr<SubmeshWork> work(new SubmeshWork());

			work->materialIndex_ = std::stoi(prim.materialId);

			// create base index buffer.
			auto&& index_accessor = document.accessors.Get(prim.indicesAccessorId);
			if (index_accessor.componentType == Microsoft::glTF::ComponentType::COMPONENT_UNSIGNED_BYTE)
			{
				auto indexBuffer = resource_reader->ReadBinaryData<uint8_t>(document, index_accessor);
				work->indexBuffer_.clear();
				work->indexBuffer_.reserve(indexBuffer.size());
				for (auto&& index : indexBuffer)
				{
					work->indexBuffer_.push_back((uint32_t)index);
				}
			}
			if (index_accessor.componentType == Microsoft::glTF::ComponentType::COMPONENT_UNSIGNED_SHORT)
			{
				auto indexBuffer = resource_reader->ReadBinaryData<uint16_t>(document, index_accessor);
				work->indexBuffer_.clear();
				work->indexBuffer_.reserve(indexBuffer.size());
				for (auto&& index : indexBuffer)
				{
					work->indexBuffer_.push_back((uint32_t)index);
				}
			}
			if (index_accessor.componentType == Microsoft::glTF::ComponentType::COMPONENT_UNSIGNED_INT)
			{
				work->indexBuffer_ = resource_reader->ReadBinaryData<uint32_t>(document, index_accessor);
			}

			// create base vertex buffer.
			std::string accessorId;
			if (prim.TryGetAttributeAccessorId("POSITION", accessorId))
			{
				auto&& posAccessor = document.accessors.Get(accessorId);
				auto posData = resource_reader->ReadBinaryData<float>(document, posAccessor);
				size_t vertex_count = posData.size() / 3;
				work->vertexBuffer_.resize(vertex_count);
				for (size_t i = 0; i < vertex_count; ++i)
				{
					DirectX::XMFLOAT3 v(posData[i * 3 + 0], posData[i * 3 + 1], posData[i * 3 + 2]);
					DirectX::XMVECTOR V = DirectX::XMLoadFloat3(&v);
					V = DirectX::XMVector3TransformCoord(V, transform);
					DirectX::XMStoreFloat3(&work->vertexBuffer_[i].pos, V);
				}

				if (prim.TryGetAttributeAccessorId("NORMAL", accessorId))
				{
					auto&& normalAccessor = document.accessors.Get(accessorId);
					auto normalData = resource_reader->ReadBinaryData<float>(document, normalAccessor);
					for (size_t i = 0; i < vertex_count; ++i)
					{
						DirectX::XMFLOAT3 n(normalData[i * 3 + 0], normalData[i * 3 + 1], normalData[i * 3 + 2]);
						DirectX::XMVECTOR N = DirectX::XMLoadFloat3(&n);
						N = DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(N, transform));
						DirectX::XMStoreFloat3(&work->vertexBuffer_[i].normal, N);
					}
				}
				if (prim.TryGetAttributeAccessorId("TEXCOORD_0", accessorId))
				{
					auto&& uvAccessor = document.accessors.Get(accessorId);
					auto uvData = resource_reader->ReadBinaryData<float>(document, uvAccessor);
					for (size_t i = 0; i < vertex_count; ++i)
					{
						work->vertexBuffer_[i].uv.x = uvData[i * 2 + 0];
						work->vertexBuffer_[i].uv.y = uvData[i * 2 + 1];
					}
				}
			}

			// generate mikk t space.
			MikkTSpaceMesh mikk_mesh(work->vertexBuffer_, work->indexBuffer_);
			auto mikk_context = mikk_mesh.GetContext();
			genTangSpaceDefault(&mikk_context);

			// compute bounds.
			std::vector<DirectX::XMFLOAT3> points;
			points.reserve(work->vertexBuffer_.size());
			DirectX::XMVECTOR aabbMin = DirectX::XMLoadFloat3(&work->vertexBuffer_[0].pos);
			DirectX::XMVECTOR aabbMax = DirectX::XMLoadFloat3(&work->vertexBuffer_[0].pos);
			for (auto&& v : work->vertexBuffer_)
			{
				points.push_back(v.pos);
				all_points.push_back(v.pos);

				DirectX::XMVECTOR p = DirectX::XMLoadFloat3(&v.pos);
				aabbMin = DirectX::XMVectorMin(aabbMin, p);
				aabbMax = DirectX::XMVectorMax(aabbMax, p);
			}
			ComputeBoundingSphere(&points[0].x, points.size(), work->boundingSphere_.center, work->boundingSphere_.radius);
			DirectX::XMStoreFloat3(&work->boundingBox_.aabbMin, aabbMin);
			DirectX::XMStoreFloat3(&work->boundingBox_.aabbMax, aabbMax);

			submeshes_.push_back(std::move(work));
		}
	}

	// compute mesh bounds.
	ComputeBoundingSphere(&all_points[0].x, all_points.size(), boundingSphere_.center, boundingSphere_.radius);
	{
		DirectX::XMVECTOR aabbMin = DirectX::XMLoadFloat3(&all_points[0]);
		DirectX::XMVECTOR aabbMax = DirectX::XMLoadFloat3(&all_points[0]);
		for (auto&& v : all_points)
		{
			DirectX::XMVECTOR p = DirectX::XMLoadFloat3(&v);
			aabbMin = DirectX::XMVectorMin(aabbMin, p);
			aabbMax = DirectX::XMVectorMax(aabbMax, p);
		}
		DirectX::XMStoreFloat3(&boundingBox_.aabbMin, aabbMin);
		DirectX::XMStoreFloat3(&boundingBox_.aabbMax, aabbMax);
	}

	return true;
}

size_t MeshWork::MergeSubmesh()
{
	std::map<int, int> submesh_map;

	int submesh_count = (int)submeshes_.size();
	for (int i = 0; i < submesh_count; i++)
	{
		auto it = submesh_map.find(submeshes_[i]->materialIndex_);
		if (it == submesh_map.end())
		{
			// first submesh.
			submesh_map[submeshes_[i]->materialIndex_] = i;
			continue;
		}

		auto&& their = submeshes_[it->second];
		auto&& mine = submeshes_[i];

		// append vertex buffer.
		uint32_t vertex_start = (uint32_t)their->vertexBuffer_.size();
		their->vertexBuffer_.resize(vertex_start + mine->vertexBuffer_.size());
		memcpy(their->vertexBuffer_.data() + vertex_start, mine->vertexBuffer_.data(), sizeof(Vertex) * mine->vertexBuffer_.size());

		// append index buffer.
		for (auto index : mine->indexBuffer_)
		{
			their->indexBuffer_.push_back(index + vertex_start);
		}

		// delete mine.
		submeshes_[i].reset(nullptr);
	}

	// delete null mesh.
	auto it = submeshes_.begin();
	while (it != submeshes_.end())
	{
		if (*it == nullptr)
		{
			it = submeshes_.erase(it);
		}
		else
		{
			++it;
		}
	}

	return submeshes_.size();
}

void MeshWork::OptimizeSubmesh()
{
	static const float kOverdrawThreshold = 3.0f;

	for (auto&& submesh : submeshes_)
	{
		// generate vertex remap table.
		std::vector<uint32_t> remap;
		remap.resize(submesh->vertexBuffer_.size());
		auto new_vertex_count = meshopt_generateVertexRemap(remap.data(), submesh->indexBuffer_.data(), submesh->indexBuffer_.size(), submesh->vertexBuffer_.data(), submesh->vertexBuffer_.size(), sizeof(Vertex));

		// remap vertex/index buffer.
		std::vector<Vertex> new_vertex_buffer;
		std::vector<uint32_t> new_index_buffer;
		new_vertex_buffer.resize(new_vertex_count);
		new_index_buffer.resize(submesh->indexBuffer_.size());
		meshopt_remapVertexBuffer(new_vertex_buffer.data(), submesh->vertexBuffer_.data(), submesh->vertexBuffer_.size(), sizeof(Vertex), remap.data());
		meshopt_remapIndexBuffer(new_index_buffer.data(), submesh->indexBuffer_.data(), submesh->indexBuffer_.size(), remap.data());

		// optimization.
		meshopt_optimizeVertexCache(new_index_buffer.data(), new_index_buffer.data(), new_index_buffer.size(), new_vertex_count);
		//meshopt_optimizeOverdraw(new_index_buffer.data(), new_index_buffer.data(), new_index_buffer.size(), &new_vertex_buffer[0].pos.x, new_vertex_buffer.size(), sizeof(Vertex), kOverdrawThreshold);
		meshopt_optimizeVertexFetch(new_vertex_buffer.data(), new_index_buffer.data(), new_index_buffer.size(), new_vertex_buffer.data(), new_vertex_buffer.size(), sizeof(Vertex));

		// swap.
		submesh->vertexBuffer_.swap(new_vertex_buffer);
		submesh->indexBuffer_.swap(new_index_buffer);
	}
}

namespace
{
	// this functions are ported from meshoptimizer and modified.
	struct modify_Meshlet
	{
		std::vector<unsigned int> vertices;
		std::vector<unsigned char> indices;
		unsigned int triangle_count;
		unsigned int vertex_count;

		void Alloc(int maxVertices, int maxTriangles)
		{
			vertices.resize(maxVertices);
			indices.resize(maxTriangles * 3);
			triangle_count = vertex_count = 0;
		}
	};

	size_t modify_buildMeshlets(modify_Meshlet* destination, const unsigned int* indices, size_t index_count, size_t vertex_count, size_t max_vertices, size_t max_triangles)
	{
		assert(index_count % 3 == 0);
		assert(max_vertices >= 3);
		assert(max_vertices < 256);
		assert(max_triangles >= 1);

		meshopt_Allocator allocator;

		modify_Meshlet meshlet;
		meshlet.Alloc((int)max_vertices, (int)max_triangles);

		// index of the vertex in the meshlet, 0xff if the vertex isn't used
		unsigned short* used = allocator.allocate<unsigned short>(vertex_count);
		memset(used, -1, vertex_count * sizeof(unsigned short));

		size_t offset = 0;

		for (size_t i = 0; i < index_count; i += 3)
		{
			unsigned int a = indices[i + 0], b = indices[i + 1], c = indices[i + 2];
			assert(a < vertex_count && b < vertex_count && c < vertex_count);

			unsigned short& av = used[a];
			unsigned short& bv = used[b];
			unsigned short& cv = used[c];

			unsigned int used_extra = (av == 0xffff) + (bv == 0xffff) + (cv == 0xffff);

			if (meshlet.vertex_count + used_extra > max_vertices || meshlet.triangle_count >= max_triangles)
			{
				destination[offset++] = meshlet;

				for (size_t j = 0; j < meshlet.vertex_count; ++j)
					used[meshlet.vertices[j]] = 0xffff;

				meshlet.Alloc((int)max_vertices, (int)max_triangles);
			}

			if (av == 0xffff)
			{
				av = meshlet.vertex_count;
				meshlet.vertices[meshlet.vertex_count++] = a;
			}

			if (bv == 0xffff)
			{
				bv = meshlet.vertex_count;
				meshlet.vertices[meshlet.vertex_count++] = b;
			}

			if (cv == 0xffff)
			{
				cv = meshlet.vertex_count;
				meshlet.vertices[meshlet.vertex_count++] = c;
			}

			meshlet.indices[meshlet.triangle_count * 3 + 0] = (unsigned char)av;
			meshlet.indices[meshlet.triangle_count * 3 + 1] = (unsigned char)bv;
			meshlet.indices[meshlet.triangle_count * 3 + 2] = (unsigned char)cv;
			meshlet.triangle_count++;
		}

		if (meshlet.triangle_count)
			destination[offset++] = meshlet;

		assert(offset <= meshopt_buildMeshletsBound(index_count, max_vertices, max_triangles));

		return offset;
	}

	meshopt_Bounds modify_computeMeshletBounds(const modify_Meshlet* meshlet, const float* vertex_positions, size_t vertex_count, size_t vertex_positions_stride)
	{
		assert(vertex_positions_stride > 0 && vertex_positions_stride <= 256);
		assert(vertex_positions_stride % sizeof(float) == 0);

		std::vector<unsigned int> indices;
		indices.resize(meshlet->indices.size());

		for (size_t i = 0; i < meshlet->triangle_count; ++i)
		{
			unsigned int a = meshlet->vertices[meshlet->indices[i * 3 + 0]];
			unsigned int b = meshlet->vertices[meshlet->indices[i * 3 + 1]];
			unsigned int c = meshlet->vertices[meshlet->indices[i * 3 + 2]];

			assert(a < vertex_count && b < vertex_count && c < vertex_count);

			indices[i * 3 + 0] = a;
			indices[i * 3 + 1] = b;
			indices[i * 3 + 2] = c;
		}

		return meshopt_computeClusterBounds(indices.data(), meshlet->triangle_count * 3, vertex_positions, vertex_count, vertex_positions_stride);
	}

}

void MeshWork::BuildMeshlets(int maxVertices, int maxTriangles)
{
	for (auto&& submesh : submeshes_)
	{
		// build meshlets.
		const size_t kMaxMeshletVertex = (size_t)maxVertices;
		const size_t kMaxMeshletTriangle = (size_t)maxTriangles;
		//std::vector<meshopt_Meshlet> meshlets;
		std::vector<modify_Meshlet> meshlets;
		meshlets.resize(meshopt_buildMeshletsBound(submesh->indexBuffer_.size(), kMaxMeshletVertex, kMaxMeshletTriangle));
		//meshopt_buildMeshlets(meshlets.data(), submesh->indexBuffer_.data(), submesh->indexBuffer_.size(), submesh->vertexBuffer_.size(), kMaxMeshletVertex, kMaxMeshletTriangle);
		modify_buildMeshlets(meshlets.data(), submesh->indexBuffer_.data(), submesh->indexBuffer_.size(), submesh->vertexBuffer_.size(), kMaxMeshletVertex, kMaxMeshletTriangle);

		// create work meshlets.
		for (auto&& meshlet : meshlets)
		{
			if (meshlet.triangle_count == 0)
			{
				continue;
			}

			std::vector<Vertex> this_vtx;

			// copy indices.
			Meshlet work;
			work.indexOffset = (uint32_t)submesh->meshletIndexBuffer_.size();
			work.indexCount = meshlet.triangle_count * 3;
			work.primitiveOffset = (uint32_t)submesh->meshletPackedPrimitive_.size();
			work.primitiveCount = meshlet.triangle_count;
			work.vertexIndexOffset = (uint32_t)submesh->meshletVertexIndexBuffer_.size();
			work.vertexIndexCount = meshlet.vertex_count;
			for (uint32_t i = 0; i < meshlet.triangle_count; i++)
			{
				//uint32_t i0 = meshlet.indices[i][0];
				//uint32_t i1 = meshlet.indices[i][1];
				//uint32_t i2 = meshlet.indices[i][2];
				uint32_t i0 = meshlet.indices[i * 3 + 0];
				uint32_t i1 = meshlet.indices[i * 3 + 1];
				uint32_t i2 = meshlet.indices[i * 3 + 2];

				submesh->meshletIndexBuffer_.push_back(meshlet.vertices[i0]);
				submesh->meshletIndexBuffer_.push_back(meshlet.vertices[i1]);
				submesh->meshletIndexBuffer_.push_back(meshlet.vertices[i2]);

				submesh->meshletPackedPrimitive_.push_back((i2 << 20) | (i1 << 10) | i0);

				//this_vtx.push_back(submesh->vertexBuffer_[meshlet.vertices[meshlet.indices[i][0]]]);
				//this_vtx.push_back(submesh->vertexBuffer_[meshlet.vertices[meshlet.indices[i][1]]]);
				//this_vtx.push_back(submesh->vertexBuffer_[meshlet.vertices[meshlet.indices[i][2]]]);
				this_vtx.push_back(submesh->vertexBuffer_[meshlet.vertices[meshlet.indices[i * 3 + 0]]]);
				this_vtx.push_back(submesh->vertexBuffer_[meshlet.vertices[meshlet.indices[i * 3 + 1]]]);
				this_vtx.push_back(submesh->vertexBuffer_[meshlet.vertices[meshlet.indices[i * 3 + 2]]]);
			}
			for (uint32_t i = 0; i < meshlet.vertex_count; i++)
			{
				submesh->meshletVertexIndexBuffer_.push_back(meshlet.vertices[i]);
			}

			// compute bounds.
			//auto bounds = meshopt_computeMeshletBounds(&meshlet, &submesh->vertexBuffer_[0].pos.x, submesh->vertexBuffer_.size(), sizeof(Vertex));
			auto bounds = modify_computeMeshletBounds(&meshlet, &submesh->vertexBuffer_[0].pos.x, submesh->vertexBuffer_.size(), sizeof(Vertex));
			work.boundingSphere.center.x = bounds.center[0];
			work.boundingSphere.center.y = bounds.center[1];
			work.boundingSphere.center.z = bounds.center[2];
			work.boundingSphere.radius = bounds.radius;
			work.cone.apex.x = bounds.cone_apex[0];
			work.cone.apex.y = bounds.cone_apex[1];
			work.cone.apex.z = bounds.cone_apex[2];
			work.cone.axis.x = bounds.cone_axis[0];
			work.cone.axis.y = bounds.cone_axis[1];
			work.cone.axis.z = bounds.cone_axis[2];
			work.cone.cutoff = bounds.cone_cutoff;

			DirectX::XMVECTOR aabbMin = DirectX::XMLoadFloat3(&this_vtx[0].pos);
			DirectX::XMVECTOR aabbMax = DirectX::XMLoadFloat3(&this_vtx[0].pos);
			for (auto&& v : this_vtx)
			{
				DirectX::XMVECTOR p = DirectX::XMLoadFloat3(&v.pos);
				aabbMin = DirectX::XMVectorMin(aabbMin, p);
				aabbMax = DirectX::XMVectorMax(aabbMax, p);
			}
			DirectX::XMStoreFloat3(&work.boundingBox.aabbMin, aabbMin);
			DirectX::XMStoreFloat3(&work.boundingBox.aabbMax, aabbMax);

			submesh->meshlets_.push_back(work);
		}

		// check.
		size_t count = submesh->indexBuffer_.size();
		for (size_t i = 0; i < count; i++)
		{
			if (submesh->indexBuffer_[i] != submesh->meshletIndexBuffer_[i])
			{
				fprintf(stderr, "There is a difference between index buffer and meshlet index buffer.\n");
			}
		}
	}
}


//	EOF
