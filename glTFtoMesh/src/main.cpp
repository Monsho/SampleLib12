#pragma comment(lib, "imagehlp.lib")

#include <algorithm>
#include "GLTFSDK/GLTF.h"
#include "GLTFSDK/GLBResourceReader.h"
#include "GLTFSDK/Deserialize.h"
#include "meshoptimizer.h"
#include "mikktspace.h"

#include <cereal/cereal.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/types/vector.hpp>

#include <DirectXTex.h>

#include <string>
#include <fstream>
#include <sstream>

#include "utils.h"
#include "mesh_work.h"
#include "texture_convert.h"

#define NOMINMAX
#include <windows.h>
#include <imagehlp.h>


using namespace Microsoft::glTF;

struct ToolOptions
{
	std::string		inputFileName = "";
	std::string		inputPath = "";
	std::string		outputFilePath = "";
	std::string		outputTexPath = "";

	bool			textureDDS = true;
	bool			compressBC7 = false;
	int				streamingTex = 0;
	bool			mergeFlag = true;
	bool			optimizeFlag = true;
	bool			meshletFlag = false;
	int				meshletMaxVertices = 64;
	int				meshletMaxTriangles = 126;
};	// struct ToolOptions

void DisplayHelp()
{
	fprintf(stdout, "glTFtoMesh : Convert glTF format to sl12 mesh format.\n");
	fprintf(stdout, "options:\n");
	fprintf(stdout, "    -i <file_path>  : input glTf(.glb) file path.\n");
	fprintf(stdout, "    -o <file_path>  : output sl12 mesh(.rmesh) file path.\n");
	fprintf(stdout, "    -to <directory> : output texture file directory.\n");
	fprintf(stdout, "    -dds <0/1>      : change texture format png to dds, or not. if stex is true, compress texture image. (default: 1)\n");
	fprintf(stdout, "    -bc7 <0/1>      : if 1, use bc7 compression for a part of dds. if 0, use bc3. (default: 0)\n");
	fprintf(stdout, "    -stex <res>     : if > 0, use streaming texture format and indicate tail mips resolution. if 0, use dds or png. (default: 0)\n");
	fprintf(stdout, "    -merge <0/1>    : merge submeshes have same material. (default: 1)\n");
	fprintf(stdout, "    -opt <0/1>      : optimize mesh. (default: 1)\n");
	fprintf(stdout, "    -let <0/1>      : create meshlets. (default: 0)\n");
	fprintf(stdout, "    -letvert <int>  : meshlet max vertices. (default: 64)\n");
	fprintf(stdout, "    -lettri <int>   : meshlet max triangles. (default: 126)\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "example:\n");
	fprintf(stdout, "    glTFtoMesh.exe -i \"D:/input/sample.glb\" -o \"D:/output/sample.rmesh\" -to \"D:/output/textures/\" -let 1\n");
}

int main(int argv, char* argc[])
{
	if (argv == 1)
	{
		// display help.
		DisplayHelp();
		return 0;
	}

	// get options.
	ToolOptions options;
	for (int i = 1; i < argv; i++)
	{
		std::string op = argc[i];
		if (op[0] == '-' || op[0] == '/')
		{
			if (op == "-i" || op == "/i")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.inputPath = ConvYenToSlash(argc[++i]);
				auto slash = options.inputPath.rfind('/');
				if (slash == std::string::npos)
				{
					options.inputFileName = options.inputPath;
					options.inputPath = "./";
				}
				else
				{
					options.inputFileName = &options.inputPath.data()[slash + 1];
					options.inputPath = options.inputPath.erase(slash + 1, std::string::npos);
				}
			}
			else if (op == "-o" || op == "/o")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.outputFilePath = argc[++i];
			}
			else if (op == "-to" || op == "/to")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.outputTexPath = argc[++i];
			}
			else if (op == "-dds" || op == "/dds")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.textureDDS = std::stoi(argc[++i]);
			}
			else if (op == "-bc7" || op == "/bc7")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.compressBC7 = std::stoi(argc[++i]);
			}
			else if (op == "-stex" || op == "/stex")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.streamingTex = std::stoi(argc[++i]);
			}
			else if (op == "-merge" || op == "/merge")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.mergeFlag = std::stoi(argc[++i]);
			}
			else if (op == "-opt" || op == "/opt")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.optimizeFlag = std::stoi(argc[++i]);
			}
			else if (op == "-let" || op == "/let")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.meshletFlag = std::stoi(argc[++i]);
			}
			else if (op == "-letvert" || op == "/letvert")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.meshletMaxVertices = std::stoi(argc[++i]);
			}
			else if (op == "-lettri" || op == "/lettri")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.meshletMaxTriangles = std::stoi(argc[++i]);
			}
			else
			{
				fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
				return -1;
			}
		}
		else
		{
			fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
			return -1;
		}
	}

	if (options.inputFileName.empty() || options.inputPath.empty())
	{
		fprintf(stderr, "invalid input file name.\n");
		return -1;
	}
	if (options.outputFilePath.empty())
	{
		fprintf(stderr, "invalid output file name.\n");
		return -1;
	}
	if (options.outputTexPath.empty())
	{
		options.outputTexPath = GetPath(ConvYenToSlash(options.outputFilePath));
	}
	else
	{
		options.outputTexPath = ConvYenToSlash(options.outputTexPath);
		if (options.outputTexPath[options.outputTexPath.length() - 1] != '/')
		{
			options.outputTexPath += '/';
		}
	}

	{
		auto outDir = GetPath(ConvYenToSlash(options.outputFilePath));
		MakeSureDirectoryPathExists(ConvSlashToYen(outDir).c_str());
		MakeSureDirectoryPathExists(ConvSlashToYen(options.outputTexPath).c_str());
	}

	fprintf(stdout, "read glTF mesh. (%s)\n", options.inputFileName.c_str());
	auto mesh_work = std::make_unique<MeshWork>();
	if (!mesh_work->ReadGLTFMesh(options.inputPath, options.inputFileName))
	{
		fprintf(stderr, "failed to read glTF mesh. (%s)\n", options.inputFileName.c_str());
		return -1;
	}

	if (options.mergeFlag)
	{
		fprintf(stdout, "merge submeshes.\n");
		if (mesh_work->MergeSubmesh() == 0)
		{
			fprintf(stderr, "failed to merge submeshes.\n");
			return -1;
		}
	}

	if (options.optimizeFlag)
	{
		fprintf(stdout, "optimize mesh.\n");
		mesh_work->OptimizeSubmesh();
	}

	if (options.meshletFlag)
	{
		fprintf(stdout, "build meshlets.\n");
		mesh_work->BuildMeshlets(options.meshletMaxVertices, options.meshletMaxTriangles);
	}

	// output textures.
	if (options.streamingTex > 0)
	{
		if (!mesh_work->GetTextures().empty())
		{
			fprintf(stdout, "output STEX textures.\n");
			HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

			for (auto&& tex : mesh_work->GetTextures())
			{
				std::string name = GetFileName(tex->GetName()) + ".stex";
				std::string kind = GetTextureKind(tex->GetName());
				fprintf(stdout, "writing %s texture... (kind: %s)\n", name.c_str(), kind.c_str());
				if (!ConvertToSTEX(tex.get(), options.outputTexPath + name, kind == "bc", options.textureDDS, kind == "n", options.compressBC7, (size_t)options.streamingTex))
				{
					fprintf(stderr, "failed to write %s texture...\n", name.c_str());
					return -1;
				}
			}

			CoUninitialize();
			fprintf(stdout, "complete to output STEX textures.\n");
		}
	}
	else if (options.textureDDS)
	{
		if (!mesh_work->GetTextures().empty())
		{
			fprintf(stdout, "output DDS textures.\n");
			HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

			for (auto&& tex : mesh_work->GetTextures())
			{
				std::string name = GetFileName(tex->GetName()) + ".dds";
				std::string kind = GetTextureKind(tex->GetName());
				fprintf(stdout, "writing %s texture... (kind: %s)\n", name.c_str(), kind.c_str());
				if (!ConvertToDDS(tex.get(), options.outputTexPath + name, kind == "bc", kind == "n", options.compressBC7))
				{
					fprintf(stderr, "failed to write %s texture...\n", name.c_str());
					return -1;
				}
			}

			CoUninitialize();
			fprintf(stdout, "complete to output DDS textures.\n");
		}
	}
	else
	{
		fprintf(stdout, "output PNG textures.\n");
		for (auto&& tex : mesh_work->GetTextures())
		{
			fprintf(stdout, "writing %s texture...\n", tex->GetName().c_str());
			std::fstream ofs(options.outputTexPath + tex->GetName(), std::ios::out | std::ios::binary);
			ofs.write((const char*)tex->GetBinary().data(), tex->GetBinary().size());
		}
		fprintf(stdout, "complete to output PNG textures.\n");
	}

	auto PNGtoExt = [](const std::string& filename, const std::string& ext)
	{
		std::string ret = filename;
		auto pos = ret.rfind(".png");
		if (pos != std::string::npos)
		{
			ret.erase(pos);
			ret += ext;
		}
		return ret;
	};

	DirectX::XMVECTOR aabbMin = DirectX::XMLoadFloat3(&mesh_work->GetBoundingBox().aabbMin);
	DirectX::XMVECTOR aabbMax = DirectX::XMLoadFloat3(&mesh_work->GetBoundingBox().aabbMax);
	DirectX::XMVECTOR aabbSize = DirectX::XMVectorSubtract(aabbMax, aabbMin);
	DirectX::XMVECTOR boxCenter = DirectX::XMVectorScale(DirectX::XMVectorAdd(aabbMax, aabbMin), 0.5f);
	DirectX::XMMATRIX boxLocalMatrix = DirectX::XMMatrixMultiply(
		DirectX::XMMatrixTranslation(-boxCenter.m128_f32[0], -boxCenter.m128_f32[1], -boxCenter.m128_f32[2]),
		DirectX::XMMatrixScaling(1.0f / aabbSize.m128_f32[0], 1.0f / aabbSize.m128_f32[1], 1.0f / aabbSize.m128_f32[2]));

	// output binary.
	fprintf(stdout, "output rmesh binary.\n");
	auto out_resource = std::make_unique<sl12::ResourceMesh>();
	out_resource->boundingSphere_.centerX = mesh_work->GetBoundingSphere().center.x;
	out_resource->boundingSphere_.centerY = mesh_work->GetBoundingSphere().center.y;
	out_resource->boundingSphere_.centerZ = mesh_work->GetBoundingSphere().center.z;
	out_resource->boundingSphere_.radius = mesh_work->GetBoundingSphere().radius;
	out_resource->boundingBox_.minX = mesh_work->GetBoundingBox().aabbMin.x;
	out_resource->boundingBox_.minY = mesh_work->GetBoundingBox().aabbMin.y;
	out_resource->boundingBox_.minZ = mesh_work->GetBoundingBox().aabbMin.z;
	out_resource->boundingBox_.maxX = mesh_work->GetBoundingBox().aabbMax.x;
	out_resource->boundingBox_.maxY = mesh_work->GetBoundingBox().aabbMax.y;
	out_resource->boundingBox_.maxZ = mesh_work->GetBoundingBox().aabbMax.z;
	for (auto&& mat : mesh_work->GetMaterials())
	{
		auto bcName = mat->GetTextrues()[MaterialWork::TextureKind::BaseColor];
		auto nName = mat->GetTextrues()[MaterialWork::TextureKind::Normal];
		auto ormName = mat->GetTextrues()[MaterialWork::TextureKind::ORM];
		if (options.streamingTex > 0)
		{
			bcName = PNGtoExt(bcName, ".stex");
			nName = PNGtoExt(nName, ".stex");
			ormName = PNGtoExt(ormName, ".stex");
		}
		else if (options.textureDDS)
		{
			bcName = PNGtoExt(bcName, ".dds");
			nName = PNGtoExt(nName, ".dds");
			ormName = PNGtoExt(ormName, ".dds");
		}

		sl12::ResourceMeshMaterial out_mat;
		out_mat.name_ = mat->GetName();
		out_mat.textureNames_.push_back(bcName);
		out_mat.textureNames_.push_back(nName);
		out_mat.textureNames_.push_back(ormName);
		out_mat.baseColorR_ = mat->GetBaseColor().x;
		out_mat.baseColorG_ = mat->GetBaseColor().y;
		out_mat.baseColorB_ = mat->GetBaseColor().z;
		out_mat.baseColorA_ = mat->GetBaseColor().w;
		out_mat.emissiveColorR_ = mat->GetEmissiveColor().x;
		out_mat.emissiveColorG_ = mat->GetEmissiveColor().y;
		out_mat.emissiveColorB_ = mat->GetEmissiveColor().z;
		out_mat.roughness_ = mat->GetRoughness();
		out_mat.metallic_ = mat->GetMetallic();
		out_mat.blendType_ = mat->GetBlendType();
		out_mat.cullMode_ = mat->GetCullMode();
		out_resource->materials_.push_back(out_mat);
	}
	uint32_t vb_offset = 0;
	uint32_t ib_offset = 0;
	uint32_t pb_offset = 0;
	uint32_t vib_offset = 0;
	for (auto&& submesh : mesh_work->GetSubmeshes())
	{
		sl12::ResourceMeshSubmesh out_sub;
		out_sub.materialIndex_ = submesh->GetMaterialIndex();

		auto&& src_vb = submesh->GetVertexBuffer();
		auto&& src_ib = submesh->GetIndexBuffer();
		auto&& src_pb = submesh->GetPackedPrimitive();
		auto&& src_vib = submesh->GetVertexIndexBuffer();
		std::vector<unsigned short> vbpos;
		std::vector<char> vbnorm, vbtan;
		std::vector<unsigned short> vbuv;
		vbpos.resize(4 * src_vb.size());
		vbnorm.resize(4 * src_vb.size());
		vbtan.resize(4 * src_vb.size());
		vbuv.resize(2 * src_vb.size());
		for (size_t i = 0; i < src_vb.size(); i++)
		{
			DirectX::XMVECTOR vPos = DirectX::XMLoadFloat3(&src_vb[i].pos);
			vPos = DirectX::XMVector3Transform(vPos, boxLocalMatrix);
			unsigned short pos[4] = {
				(unsigned short)meshopt_quantizeSnorm(vPos.m128_f32[0], 16),
				(unsigned short)meshopt_quantizeSnorm(vPos.m128_f32[1], 16),
				(unsigned short)meshopt_quantizeSnorm(vPos.m128_f32[2], 16),
				(unsigned short)meshopt_quantizeSnorm(1.0f, 16)
			};
			memcpy(&vbpos[i * 4], pos, sizeof(pos));

			char norm[4] = {
				(char)meshopt_quantizeSnorm(src_vb[i].normal.x, 8),
				(char)meshopt_quantizeSnorm(src_vb[i].normal.y, 8),
				(char)meshopt_quantizeSnorm(src_vb[i].normal.z, 8),
				(char)meshopt_quantizeSnorm(0.0f, 8)
			};
			memcpy(&vbnorm[i * 4], norm, sizeof(norm));

			char tan[4] = {
				(char)meshopt_quantizeSnorm(src_vb[i].tangent.x, 8),
				(char)meshopt_quantizeSnorm(src_vb[i].tangent.y, 8),
				(char)meshopt_quantizeSnorm(src_vb[i].tangent.z, 8),
				(char)meshopt_quantizeSnorm(src_vb[i].tangent.w, 8)
			};
			memcpy(&vbtan[i * 4], tan, sizeof(tan));

			unsigned short uv[2] = {
				meshopt_quantizeHalf(src_vb[i].uv.x),
				meshopt_quantizeHalf(src_vb[i].uv.y)
			};
			memcpy(&vbuv[i * 2], uv, sizeof(uv));
		}

		auto CopyBuffer = [](std::vector<sl12::u8>& dst, const void* pData, size_t dataSize)
		{
			auto cs = dst.size();
			dst.resize(cs + dataSize);
			memcpy(dst.data() + cs, pData, dataSize);
		};
		CopyBuffer(out_resource->vbPosition_,  vbpos.data(), sizeof(vbpos.data()[0]) * vbpos.size());
		CopyBuffer(out_resource->vbNormal_,    vbnorm.data(), sizeof(vbnorm.data()[0]) * vbnorm.size());
		CopyBuffer(out_resource->vbTangent_,   vbtan.data(), sizeof(vbtan.data()[0]) * vbtan.size());
		CopyBuffer(out_resource->vbTexcoord_,  vbuv.data(), sizeof(vbuv.data()[0]) * vbuv.size());
		CopyBuffer(out_resource->indexBuffer_, src_ib.data(), sizeof(uint32_t)* src_ib.size());
		CopyBuffer(out_resource->meshletPackedPrimitive_, src_pb.data(), sizeof(uint32_t)* src_pb.size());
		CopyBuffer(out_resource->meshletVertexIndex_, src_vib.data(), sizeof(float) * src_vib.size());

		out_sub.vertexOffset_ = vb_offset;
		out_sub.vertexCount_ = (uint32_t)src_vb.size();
		out_sub.indexOffset_ = ib_offset;
		out_sub.indexCount_ = (uint32_t)src_ib.size();
		out_sub.meshletPrimitiveOffset_ = pb_offset;
		out_sub.meshletPrimitiveCount_ = (uint32_t)src_pb.size();
		out_sub.meshletVertexIndexOffset_ = vib_offset;
		out_sub.meshletVertexIndexCount_ = (uint32_t)src_vib.size();
		vb_offset += out_sub.vertexCount_;
		ib_offset += out_sub.indexCount_;
		pb_offset += out_sub.meshletPrimitiveCount_;
		vib_offset += out_sub.meshletVertexIndexCount_;

		out_sub.boundingSphere_.centerX = submesh->GetBoundingSphere().center.x;
		out_sub.boundingSphere_.centerY = submesh->GetBoundingSphere().center.y;
		out_sub.boundingSphere_.centerZ = submesh->GetBoundingSphere().center.z;
		out_sub.boundingSphere_.radius = submesh->GetBoundingSphere().radius;
		out_sub.boundingBox_.minX = submesh->GetBoundingBox().aabbMin.x;
		out_sub.boundingBox_.minY = submesh->GetBoundingBox().aabbMin.y;
		out_sub.boundingBox_.minZ = submesh->GetBoundingBox().aabbMin.z;
		out_sub.boundingBox_.maxX = submesh->GetBoundingBox().aabbMax.x;
		out_sub.boundingBox_.maxY = submesh->GetBoundingBox().aabbMax.y;
		out_sub.boundingBox_.maxZ = submesh->GetBoundingBox().aabbMax.z;

		for (auto&& meshlet : submesh->GetMeshlets())
		{
			sl12::ResourceMeshMeshlet m;
			m.indexOffset_ = meshlet.indexOffset;
			m.indexCount_ = meshlet.indexCount;
			m.primitiveOffset_ = meshlet.primitiveOffset;
			m.primitiveCount_ = meshlet.primitiveCount;
			m.vertexIndexOffset_ = meshlet.vertexIndexOffset;
			m.vertexIndexCount_ = meshlet.vertexIndexCount;
			m.boundingSphere_.centerX = meshlet.boundingSphere.center.x;
			m.boundingSphere_.centerY = meshlet.boundingSphere.center.y;
			m.boundingSphere_.centerZ = meshlet.boundingSphere.center.z;
			m.boundingSphere_.radius = meshlet.boundingSphere.radius;
			m.boundingBox_.minX = meshlet.boundingBox.aabbMin.x;
			m.boundingBox_.minY = meshlet.boundingBox.aabbMin.y;
			m.boundingBox_.minZ = meshlet.boundingBox.aabbMin.z;
			m.boundingBox_.maxX = meshlet.boundingBox.aabbMax.x;
			m.boundingBox_.maxY = meshlet.boundingBox.aabbMax.y;
			m.boundingBox_.maxZ = meshlet.boundingBox.aabbMax.z;
			m.cone_.apexX = meshlet.cone.apex.x;
			m.cone_.apexY = meshlet.cone.apex.y;
			m.cone_.apexZ = meshlet.cone.apex.z;
			m.cone_.axisX = meshlet.cone.axis.x;
			m.cone_.axisY = meshlet.cone.axis.y;
			m.cone_.axisZ = meshlet.cone.axis.z;
			m.cone_.cutoff = meshlet.cone.cutoff;
			out_sub.meshlets_.push_back(m);
		}

		out_resource->submeshes_.push_back(out_sub);
	}

	{
		std::fstream ofs(options.outputFilePath, std::ios::out | std::ios::binary);
		cereal::BinaryOutputArchive ar(ofs);
		ar(cereal::make_nvp("mesh", *out_resource));
	}

	fprintf(stdout, "convert succeeded!!.\n");

	return 0;
}


//	EOF
