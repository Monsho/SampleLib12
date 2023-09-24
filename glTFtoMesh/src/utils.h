#pragma once
#include <string>

inline std::string ConvYenToSlash(const std::string& path)
{
	std::string ret;
	ret.reserve(path.length() + 1);
	for (auto&& it : path)
	{
		ret += (it == '\\') ? '/' : it;
	}
	return ret;
}

inline std::string ConvSlashToYen(const std::string& path)
{
	std::string ret;
	ret.reserve(path.length() + 1);
	for (auto&& it : path)
	{
		ret += (it == '/') ? '\\' : it;
	}
	return ret;
}

inline std::string GetExtent(const std::string& filename)
{
	std::string ret;
	auto pos = filename.rfind('.');
	if (pos != std::string::npos)
	{
		ret = filename.data() + pos;
	}
	return ret;
}

inline std::string GetFileName(const std::string& filename)
{
	std::string ret = filename;
	auto pos = filename.rfind('.');
	if (pos != std::string::npos)
	{
		ret.erase(pos);
	}
	return ret;
}

inline std::string GetPath(const std::string& filename)
{
	std::string ret = "./";
	auto pos = filename.rfind('/');
	if (pos != std::string::npos)
	{
		ret = filename.substr(0, pos + 1);
	}
	return ret;
}

inline std::string GetTextureKind(const std::string& filename)
{
	std::string name = GetFileName(filename);
	size_t pos = name.rfind(".");
	std::string ret;
	if (pos != std::string::npos)
	{
		ret = name.data() + pos + 1;
	}
	return ret;
}
