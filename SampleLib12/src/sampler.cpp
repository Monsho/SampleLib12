#include <sl12/sampler.h>

#include <sl12/device.h>
#include <sl12/descriptor.h>
#include <sl12/descriptor_heap.h>

namespace sl12
{
	//----
	bool Sampler::Initialize(Device* pDev, const D3D12_SAMPLER_DESC& desc)
	{
		descInfo_ = pDev->GetSamplerDescriptorHeap().Allocate();
		if (!descInfo_.IsValid())
		{
			return false;
		}

		pDev->GetDeviceDep()->CreateSampler(&desc, descInfo_.cpuHandle);

		samplerDesc_ = desc;

		auto pDynamicHeap = pDev->GetDynamicSamplerDescriptorHeap();
		if (pDynamicHeap)
		{
			dynamicDescInfo_ = pDynamicHeap->Allocate();
			if (dynamicDescInfo_.IsValid())
			{
				pDev->GetDeviceDep()->CreateSampler(&desc, dynamicDescInfo_.cpuHandle);
			}
		}

		return true;
	}

	//----
	void Sampler::Destroy()
	{
		descInfo_.Free();
		dynamicDescInfo_.Free();
	}

}	// namespace sl12

//	EOF
