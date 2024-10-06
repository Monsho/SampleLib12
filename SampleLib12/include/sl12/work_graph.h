#pragma once

#include "sl12/util.h"
#include "sl12/device.h"
#include "sl12/command_list.h"
#include "sl12/pipeline_state.h"


namespace sl12
{
	class WorkGraphContext
	{
	public:
		WorkGraphContext();
		~WorkGraphContext();

		bool Initialize(Device* pDev, WorkGraphState* pState, LPCWSTR programName);
		void Destroy();

		void SetProgram(CommandList* pCmdList, D3D12_SET_WORK_GRAPH_FLAGS flags);

		void DispatchGraphCPU(CommandList* pCmdList, u32 entryPointIndex, u32 numRecords, u64 recordStride, void* pRecords);

	private:
		Device*							pParentDevice_ = nullptr;
		ID3D12Resource*					pBackingMemory_ = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS_RANGE	backingMemRange_;
		D3D12_PROGRAM_IDENTIFIER		programHandle_;
	};	// class WorkGraphContext
}	// namespace sl12

//	EOF
