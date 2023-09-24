#include "texture_convert.h"

#define NOMINMAX
#include <windows.h>
#include <imagehlp.h>

#include <DirectXTex.h>

#include "utils.h"
#include "mesh_work.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "sl12/streaming_texture_format.h"

std::unique_ptr<DirectX::ScratchImage> ConvertToScratchImage(TextureWork* pTex, bool& hasAlpha)
{
	// read png image.
	int width, height, bpp;
	auto pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(pTex->GetBinary().data()), static_cast<int>(pTex->GetBinary().size()), &width, &height, &bpp, 0);
	if (!pixels || (bpp != 3 && bpp != 4))
	{
		return nullptr;
	}

	// convert to DirectX image.
	std::unique_ptr<DirectX::ScratchImage> image(new DirectX::ScratchImage());
	auto hr = image->Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1);
	bool has_alpha = false;
	if (FAILED(hr))
	{
		return nullptr;
	}
	if (bpp == 3)
	{
		auto src = pixels;
		auto dst = image->GetPixels();
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < height; x++)
			{
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
				dst[3] = 0xff;
				src += 3;
				dst += 4;
			}
		}
	}
	else
	{
		auto src = pixels;
		auto dst = image->GetPixels();
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < height; x++)
			{
				has_alpha = has_alpha || (src[3] < 0xff);
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
				dst[3] = src[3];
				src += 4;
				dst += 4;
			}
		}
	}
	stbi_image_free(pixels);

	// generate full mips.
	std::unique_ptr<DirectX::ScratchImage> mipped_image(new DirectX::ScratchImage());
	hr = DirectX::GenerateMipMaps(
		*image->GetImage(0, 0, 0),
		DirectX::TEX_FILTER_CUBIC | DirectX::TEX_FILTER_FORCE_NON_WIC,
		0,
		*mipped_image);

	hasAlpha = has_alpha;
	return mipped_image;
}

std::unique_ptr<DirectX::ScratchImage> ConvertToCompressedImage(TextureWork* pTex, bool isSrgb, bool isNormal, bool isBC7)
{
	bool has_alpha;
	std::unique_ptr<DirectX::ScratchImage> image = ConvertToScratchImage(pTex, has_alpha);
	if (image == nullptr)
	{
		return nullptr;
	}

	// compress.
	DXGI_FORMAT compress_format = (isSrgb) ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
	if (has_alpha || isNormal)
	{
		compress_format = (isBC7)
			? ((isSrgb) ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM)
			: ((isSrgb) ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM);
	}
	DirectX::TEX_COMPRESS_FLAGS comp_flag = DirectX::TEX_COMPRESS_PARALLEL;
	if (isSrgb)
	{
		comp_flag |= DirectX::TEX_COMPRESS_SRGB_OUT;
	}
	std::unique_ptr<DirectX::ScratchImage> comp_image(new DirectX::ScratchImage());
	HRESULT hr = DirectX::Compress(
		image->GetImages(),
		image->GetImageCount(),
		image->GetMetadata(),
		compress_format,
		comp_flag,
		DirectX::TEX_THRESHOLD_DEFAULT,
		*comp_image);
	if (FAILED(hr))
	{
		return nullptr;
	}

	return comp_image;
}

std::unique_ptr<DirectX::ScratchImage> ConvertToUncompressedImage(TextureWork* pTex, bool isSrgb)
{
	bool has_alpha;
	std::unique_ptr<DirectX::ScratchImage> image = ConvertToScratchImage(pTex, has_alpha);
	if (image == nullptr)
	{
		return nullptr;
	}

	if (!isSrgb)
	{
		return image;
	}
	return image;

	// compress.
	DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	DirectX::TEX_COMPRESS_FLAGS comp_flag = DirectX::TEX_COMPRESS_PARALLEL | DirectX::TEX_COMPRESS_SRGB_OUT;
	std::unique_ptr<DirectX::ScratchImage> comp_image(new DirectX::ScratchImage());
	HRESULT hr = DirectX::Compress(
		image->GetImages(),
		image->GetImageCount(),
		image->GetMetadata(),
		format,
		comp_flag,
		DirectX::TEX_THRESHOLD_DEFAULT,
		*comp_image);
	if (FAILED(hr))
	{
		return nullptr;
	}

	return comp_image;
}

bool ConvertToDDS(TextureWork* pTex, const std::string& outputFilePath, bool isSrgb, bool isNormal, bool isBC7)
{
	// convert to BCn image.
	auto image = ConvertToCompressedImage(pTex, isSrgb, isNormal, isBC7);
	if (image == nullptr)
	{
		return false;
	}

	// save dds file.
	size_t len;
	mbstowcs_s(&len, nullptr, 0, outputFilePath.c_str(), 0);
	std::wstring of;
	of.resize(len + 1);
	mbstowcs_s(&len, (wchar_t*)of.data(), of.length(), outputFilePath.c_str(), of.length());
	HRESULT hr = DirectX::SaveToDDSFile(
		image->GetImages(),
		image->GetImageCount(),
		image->GetMetadata(),
		DirectX::DDS_FLAGS_NONE,
		of.c_str());
	if (FAILED(hr))
	{
		return false;
	}

	return true;
}

bool ConvertToSTEX(TextureWork* pTex, const std::string& outputFilePath, bool isSrgb, bool isCompress, bool isNormal, bool isBC7, size_t tailMipRes)
{
	if (tailMipRes <= 0)
	{
		return false;
	}
	
	std::unique_ptr<DirectX::ScratchImage> image;
	if (isCompress)
	{
		// convert to BCn image.
		image = ConvertToCompressedImage(pTex, isSrgb, isNormal, isBC7);
	}
	else
	{
		// convert to RGBA8 image.
		image = ConvertToUncompressedImage(pTex, isSrgb);
	}
	if (image == nullptr)
	{
		return false;
	}

	// create STEX file header.
	sl12::StreamingTextureHeader fileHeader{};
	fileHeader.dimension = sl12::StreamingTextureDimension::Texture2D;
	fileHeader.format = image->GetMetadata().format;
	fileHeader.width = image->GetMetadata().width;
	fileHeader.height = image->GetMetadata().height;
	fileHeader.depth = 1;
	fileHeader.mipLevels = image->GetMetadata().mipLevels;
	fileHeader.topMipCount = 0;
	fileHeader.tailMipCount = fileHeader.mipLevels;
	for (sl12::u32 i = 0; i < fileHeader.mipLevels; i++)
	{
		auto sub_image = image->GetImage(i, 0, 0);
		auto max_res = std::max(sub_image->width, sub_image->height);
		if (max_res <= tailMipRes)
		{
			fileHeader.topMipCount = i;
			fileHeader.tailMipCount = fileHeader.mipLevels - i;
			break;
		}
	}

	// create tail mip subresource headers.
	std::vector<sl12::StreamingSubresourceHeader> subHeaders;
	subHeaders.resize(fileHeader.tailMipCount);
	sl12::u64 imageOffset = sizeof(fileHeader) + sizeof(sl12::StreamingSubresourceHeader) * fileHeader.tailMipCount;
	for (sl12::u32 i = 0; i < fileHeader.tailMipCount; i++)
	{
		auto sub_image = image->GetImage(fileHeader.topMipCount + i, 0, 0);
		subHeaders[i].width = (sl12::u32)sub_image->width;
		subHeaders[i].height = (sl12::u32)sub_image->height;
		subHeaders[i].rowSize = (sl12::u32)sub_image->rowPitch;
		subHeaders[i].rowCount = (sl12::u32)(sub_image->slicePitch / sub_image->rowPitch);
		subHeaders[i].offsetFromFileHead = imageOffset;

		imageOffset += sub_image->slicePitch;
	}

	// save STEX.
	{
		FILE* fp;
		if (fopen_s(&fp, outputFilePath.c_str(), "wb") != 0)
		{
			fprintf(stderr, "can NOT open STEX file. (%s)\n", outputFilePath.c_str());
			return false;
		}

		fwrite(&fileHeader, sizeof(fileHeader), 1, fp);
		fwrite(subHeaders.data(), sizeof(subHeaders[0]), subHeaders.size(), fp);

		for (sl12::u32 i = 0; i < fileHeader.tailMipCount; i++)
		{
			auto sub_image = image->GetImage(fileHeader.topMipCount + i, 0, 0);
			fwrite(sub_image->pixels, 1, sub_image->slicePitch, fp);
		}

		fclose(fp);
	}

	// save top mips.
	for (sl12::u32 i = 0; i < fileHeader.topMipCount; i++)
	{
		auto sub_image = image->GetImage(i, 0, 0);

		sl12::StreamingSubresourceHeader subHeader{};
		subHeader.width = (sl12::u32)sub_image->width;
		subHeader.height = (sl12::u32)sub_image->height;
		subHeader.rowSize = (sl12::u32)sub_image->rowPitch;
		subHeader.rowCount = (sl12::u32)(sub_image->slicePitch / sub_image->rowPitch);
		subHeader.offsetFromFileHead = sizeof(subHeader);

		std::string index_str = std::to_string(i);
		index_str = std::string(std::max(0, 2 - (int)index_str.size()), '0') + index_str;
		std::string outfile = outputFilePath + index_str;

		FILE* fp;
		if (fopen_s(&fp, outfile.c_str(), "wb") != 0)
		{
			fprintf(stderr, "can NOT open top mips file. (%s)\n", outfile.c_str());
			return false;
		}

		fwrite(&subHeader, sizeof(subHeader), 1, fp);
		fwrite(sub_image->pixels, 1, sub_image->slicePitch, fp);

		fclose(fp);
	}

	return true;
}
