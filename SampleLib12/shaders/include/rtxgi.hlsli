// hlsl
#define HLSL
// unmanaged mode.
#define RTXGI_DDGI_RESOURCE_MANAGEMENT	0
// right hand y-up.
#define RTXGI_COORDINATE_SYSTEM			2
// unuse shader reflection.
#define RTXGI_DDGI_SHADER_REFLECTION	0
// no bindless.
#define RTXGI_DDGI_BINDLESS_RESOURCES	0

#include "RTXGI-DDGI/rtxgi-sdk/shaders/ddgi/Irradiance.hlsl"
#define HLSL
#include "RTXGI-DDGI/rtxgi-sdk/include/rtxgi/ddgi/DDGIRootConstants.h"
#undef HLSL
#include "RTXGI-DDGI/rtxgi-sdk/include/rtxgi/ddgi/DDGIVolumeDescGPU.h"
