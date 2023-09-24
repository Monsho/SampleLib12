#pragma once
#include <string>

class TextureWork;

bool ConvertToDDS(TextureWork* pTex, const std::string& outputFilePath, bool isSrgb, bool isNormal, bool isBC7);
bool ConvertToSTEX(TextureWork* pTex, const std::string& outputFilePath, bool isSrgb, bool isCompress, bool isNormal, bool isBC7, size_t tailMipRes);
