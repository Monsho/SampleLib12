#include <sl12/indirect_executer.h>

#include <sl12/device.h>

namespace sl12
{
	//----
	bool IndirectExecuter::Initialize(Device* pDev, IndirectType::Type type, u32 stride)
	{
		static const D3D12_INDIRECT_ARGUMENT_TYPE kTypes[] = {
			D3D12_INDIRECT_ARGUMENT_TYPE_DRAW,
			D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED,
			D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH,
			D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH,
			D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS,
		};
		static const u32 kDefaultStrides[] = {
			sizeof(D3D12_DRAW_ARGUMENTS),
			sizeof(D3D12_DRAW_INDEXED_ARGUMENTS),
			sizeof(D3D12_DISPATCH_ARGUMENTS),
			sizeof(D3D12_DISPATCH_MESH_ARGUMENTS),
			sizeof(D3D12_DISPATCH_RAYS_DESC),
		};

		assert(type < IndirectType::Max);

		if (stride == 0)
		{
			stride = kDefaultStrides[type];
		}
		assert(stride >= kDefaultStrides[type]);
		
		D3D12_INDIRECT_ARGUMENT_DESC args[1]{};
		args[0].Type = kTypes[type];
		D3D12_COMMAND_SIGNATURE_DESC desc{};
		desc.ByteStride = stride;
		desc.NumArgumentDescs = 1;
		desc.pArgumentDescs = args;
		desc.NodeMask = 1;

		auto hr = pDev->GetDeviceDep()->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&pCommandSig_));
		if (FAILED(hr))
		{
			return false;
		}
		type_ = type;
		stride_ = stride;

		return true;
	}

	//----
	void IndirectExecuter::Destroy()
	{
		SafeRelease(pCommandSig_);
	}

}	// namespace sl12

//	EOF
