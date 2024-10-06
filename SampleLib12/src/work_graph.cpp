#include "sl12/work_graph.h"


namespace sl12
{
	//----
	WorkGraphContext::WorkGraphContext()
	{}

	//----
	WorkGraphContext::~WorkGraphContext()
	{
		Destroy();
	}

	//----
	bool WorkGraphContext::Initialize(Device* pDev, WorkGraphState* pState, LPCWSTR programName)
	{
		assert(pDev != nullptr);
		assert(pState != nullptr);

		pParentDevice_ = pDev;

		ID3D12StateObjectProperties1* SOProps;
		pState->GetPSO()->QueryInterface(IID_PPV_ARGS(&SOProps));
		programHandle_ = SOProps->GetProgramIdentifier(programName);

		ID3D12WorkGraphProperties* WGProps;
		pState->GetPSO()->QueryInterface(IID_PPV_ARGS(&WGProps));

		UINT WorkGraphIndex = WGProps->GetWorkGraphIndex(programName);
		D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS MemReqs;
		WGProps->GetWorkGraphMemoryRequirements(WorkGraphIndex, &MemReqs);

		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = MemReqs.MaxSizeInBytes;
		desc.Height = desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heap.CreationNodeMask = 1;
		heap.VisibleNodeMask = 1;

		SafeRelease(SOProps);
		SafeRelease(WGProps);

		HRESULT hr = pDev->GetDeviceDep()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&pBackingMemory_));
		if (FAILED(hr))
		{
			return false;
		}

		backingMemRange_.SizeInBytes = desc.Width;
		backingMemRange_.StartAddress = pBackingMemory_->GetGPUVirtualAddress();

		return true;
	}

	//----
	void WorkGraphContext::Destroy()
	{
		SafeRelease(pBackingMemory_);
	}

	//----
	void WorkGraphContext::SetProgram(CommandList* pCmdList, D3D12_SET_WORK_GRAPH_FLAGS flags)
	{
		D3D12_SET_PROGRAM_DESC SetProgram = {};
		SetProgram.Type = D3D12_PROGRAM_TYPE_WORK_GRAPH;
		SetProgram.WorkGraph.ProgramIdentifier = programHandle_;
		SetProgram.WorkGraph.Flags = flags;
		SetProgram.WorkGraph.BackingMemory = backingMemRange_;
		pCmdList->GetLatestCommandList()->SetProgram(&SetProgram);
	}

	//----
	void WorkGraphContext::DispatchGraphCPU(CommandList* pCmdList, u32 entryPointIndex, u32 numRecords, u64 recordStride, void* pRecords)
	{
		D3D12_DISPATCH_GRAPH_DESC DSDesc = {};
		DSDesc.Mode = D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
		DSDesc.NodeCPUInput.EntrypointIndex = entryPointIndex;
		DSDesc.NodeCPUInput.NumRecords = numRecords;
		DSDesc.NodeCPUInput.RecordStrideInBytes = recordStride;
		DSDesc.NodeCPUInput.pRecords = pRecords;
		pCmdList->GetLatestCommandList()->DispatchGraph(&DSDesc);
	}

}	// namespace sl12

//	EOF
