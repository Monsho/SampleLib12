#pragma once

#include <sl12/util.h>
#include <sl12/string_util.h>

namespace sl12
{
	struct DebugName
	{
#ifdef _DEBUG
		std::string name;

		void SetDebugName(const std::string& n, ID3D12Object* pObj = nullptr)
		{
			name = n;
			if (pObj) pObj->SetName(StringToWString(name).c_str());
		}
		const std::string& GetDebugName() const
		{
			return name;
		}
#else
		void SetDebugName(const std::string& n, ID3D12Object* pObj = nullptr)
		{
		}
		const std::string& GetDebugName() const
		{
			return std::string();
		}
#endif
	};

}	// namespace sl12

//	EOF
