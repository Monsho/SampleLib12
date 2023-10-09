#pragma once

#include <sl12/util.h>


namespace sl12
{
	class RootSignature;
}

namespace sl12
{
	class Device;

	struct IndirectType
	{
		enum Type
		{
			Draw,
			DrawIndexed,
			Dispatch,
			DispatchMesh,
			DispatchRays,

			Max
		};
	};	// struct IndirectType

	class IndirectExecuter
	{
	public:
		IndirectExecuter()
		{}
		~IndirectExecuter()
		{
			Destroy();
		}

		bool Initialize(Device* pDev, IndirectType::Type type, u32 stride);
		bool InitializeWithConstants(Device* pDev, IndirectType::Type type, u32 stride, RootSignature* pRootSig);
		void Destroy();

		// getter
		ID3D12CommandSignature* GetCommandSignature() { return pCommandSig_; }
		IndirectType::Type GetType() const { return type_; }
		u32 GetStride() const { return stride_; }

	private:
		ID3D12CommandSignature*	pCommandSig_ = nullptr;
		IndirectType::Type		type_ = IndirectType::Max;
		u32						stride_ = 0;
	};	// class IndirectExecuter

}	// namespace sl12

//	EOF
