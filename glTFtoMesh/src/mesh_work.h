#pragma once

#include <algorithm>
#include "GLTFSDK/GLTF.h"
#include "GLTFSDK/GLBResourceReader.h"
#include "GLTFSDK/Deserialize.h"
#include "meshoptimizer.h"
#include "mikktspace.h"
#define private public
#include "sl12/resource_mesh.h"
#undef private
#include <DirectXMath.h>


struct Vertex
{
	DirectX::XMFLOAT3	pos;
	DirectX::XMFLOAT3	normal;
	DirectX::XMFLOAT4	tangent;
	DirectX::XMFLOAT2	uv;
};	// struct Vertex

struct BoundSphere
{
	DirectX::XMFLOAT3	center;
	float				radius;
};

struct BoundBox
{
	DirectX::XMFLOAT3	aabbMin;
	DirectX::XMFLOAT3	aabbMax;
};

struct Cone
{
	DirectX::XMFLOAT3	apex;
	DirectX::XMFLOAT3	axis;
	float				cutoff;
};

struct Meshlet
{
	uint32_t				indexOffset;
	uint32_t				indexCount;
	uint32_t				primitiveOffset;
	uint32_t				primitiveCount;
	uint32_t				vertexIndexOffset;
	uint32_t				vertexIndexCount;
	BoundSphere				boundingSphere;
	BoundBox				boundingBox;
	Cone					cone;
};	// struct Meshlet

struct NodeWork
{
	DirectX::XMFLOAT4X4		transformLocal;
	DirectX::XMFLOAT4X4		transformGlobal;
	int						meshIndex;
	std::vector<uint32_t>	children;
};

class SubmeshWork
{
	friend class MeshWork;

public:
	SubmeshWork()
	{}
	~SubmeshWork()
	{}

	int GetMaterialIndex() const
	{
		return materialIndex_;
	}
	const std::vector<Vertex>& GetVertexBuffer() const
	{
		return vertexBuffer_;
	}
	const std::vector<uint32_t>& GetIndexBuffer() const
	{
		return indexBuffer_;
	}
	const std::vector<uint32_t>& GetPackedPrimitive() const
	{
		return meshletPackedPrimitive_;
	}
	const std::vector<uint32_t>& GetVertexIndexBuffer() const
	{
		return meshletVertexIndexBuffer_;
	}
	const BoundSphere& GetBoundingSphere() const
	{
		return boundingSphere_;
	}
	const BoundBox& GetBoundingBox() const
	{
		return boundingBox_;
	}
	const std::vector<Meshlet>& GetMeshlets() const
	{
		return meshlets_;
	}

private:
	int						materialIndex_;
	std::vector<Vertex>		vertexBuffer_;
	std::vector<uint32_t>	indexBuffer_;
	BoundSphere				boundingSphere_;
	BoundBox				boundingBox_;

	std::vector<Meshlet>	meshlets_;
	std::vector<uint32_t>	meshletIndexBuffer_;
	std::vector<uint32_t>	meshletPackedPrimitive_;
	std::vector<uint32_t>	meshletVertexIndexBuffer_;
};	// class SubmeshWork

class MaterialWork
{
	friend class MeshWork;

public:
	struct TextureKind
	{
		enum {
			BaseColor,
			Normal,
			ORM,
			Emissive,

			Max
		};
	};	// struct TextureType

public:
	MaterialWork()
	{}
	~MaterialWork()
	{}

	const std::string& GetName() const
	{
		return name_;
	}
	const std::string* GetTextrues() const
	{
		return textures_;
	}
	const DirectX::XMFLOAT4& GetBaseColor() const
	{
		return baseColor_;
	}
	const DirectX::XMFLOAT3& GetEmissiveColor() const
	{
		return emissiveColor_;
	}
	float GetRoughness() const
	{
		return roughness_;
	}
	float GetMetallic() const
	{
		return metallic_;
	}
	sl12::ResourceMeshMaterialBlendType GetBlendType() const
	{
		return blendType_;
	}
	sl12::ResourceMeshMaterialCullMode GetCullMode() const
	{
		return cullMode_;
	}

private:
	std::string			name_;
	std::string			textures_[TextureKind::Max];
	DirectX::XMFLOAT4	baseColor_ = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	DirectX::XMFLOAT3	emissiveColor_ = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	float				roughness_ = 1.0f;
	float				metallic_ = 1.0f;
	sl12::ResourceMeshMaterialBlendType	blendType_ = sl12::ResourceMeshMaterialBlendType::Opaque;
	sl12::ResourceMeshMaterialCullMode	cullMode_ = sl12::ResourceMeshMaterialCullMode::Back;
};	// class MaterialWork

class TextureWork
{
	friend class MeshWork;

public:
	TextureWork()
	{}
	~TextureWork()
	{}

	const std::string& GetName() const
	{
		return name_;
	}
	const std::string& GetFormat() const
	{
		return format_;
	}
	const std::vector<uint8_t>& GetBinary() const
	{
		return binary_;
	}

private:
	std::string				name_;
	std::string				format_;
	std::vector<uint8_t>	binary_;
};	// class TextureWork

class MeshWork
{
public:
	MeshWork()
	{}
	~MeshWork()
	{}

	bool ReadGLTFMesh(const std::string& inputPath, const std::string& inputFile);

	size_t MergeSubmesh();

	void OptimizeSubmesh();

	void BuildMeshlets(int maxVertices, int maxTriangles);

	const std::vector<std::unique_ptr<MaterialWork>>& GetMaterials() const
	{
		return materials_;
	}
	const std::vector<std::unique_ptr<SubmeshWork>>& GetSubmeshes() const
	{
		return submeshes_;
	}
	const std::vector<std::unique_ptr<TextureWork>>& GetTextures() const
	{
		return textures_;
	}
	const BoundSphere& GetBoundingSphere() const
	{
		return boundingSphere_;
	}
	const BoundBox& GetBoundingBox() const
	{
		return boundingBox_;
	}

private:
	std::string									sourceFilePath_;
	std::vector<NodeWork>						nodes_;
	std::vector<std::unique_ptr<MaterialWork>>	materials_;
	std::vector<std::unique_ptr<SubmeshWork>>	submeshes_;
	std::vector<std::unique_ptr<TextureWork>>	textures_;

	BoundSphere				boundingSphere_;
	BoundBox				boundingBox_;
};	// class MeshWork

//	EOF
