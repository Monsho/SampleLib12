﻿#pragma once

#define NOMINMAX

#include <stdio.h>
#include "types.h"
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <string>


namespace sl12
{
	// color space
	enum class ColorSpaceType
	{
		Rec709,
		Rec2020,
	};	// enum class ColorSpaceType

	// resource heap allocation.
	enum class ResourceHeapAllocation
	{
		Committed,
		Placed,
		Reserved,
	};	// enum class ResourceHeapAllocation

	// safe release memory funcs.
	template <typename T>
	void SafeRelease(T& p)
	{
		if (p != nullptr)
		{
			p->Release();
			p = nullptr;
		}
	}

	template <typename T>
	void SafeDelete(T& p)
	{
		if (p != nullptr)
		{
			delete p;
			p = nullptr;
		}
	}

	template <typename T>
	void SafeDeleteArray(T& p)
	{
		if (p != nullptr)
		{
			delete[] p;
			p = nullptr;
		}
	}

	// print to debug console.
	inline void ConsolePrint(const char* format, ...)
	{
		va_list arg;

		char tsv[4096];
		va_start(arg, format);
		vsprintf_s(tsv, format, arg);
		va_end(arg);

		OutputDebugStringA(tsv);
	}

	// FNV1a hash funcs.
	static const u32 kFnv1aPrime32 = 16777619;
	static const u32 kFnv1aSeed32 = 0x811c9dc5;
	static const u64 kFnv1aPrime64 = 1099511628211L;
	static const u64 kFnv1aSeed64 = 0xcbf29ce484222325;

	inline u32 CalcFnv1a32(u8 oneByte, u32 hash = kFnv1aSeed32)
	{
		return (oneByte ^ hash) * kFnv1aPrime32;
	}
	inline u32 CalcFnv1a32(const void* data, size_t numBytes, u32 hash = kFnv1aSeed32)
	{
		assert(data);
		const u8* ptr = reinterpret_cast<const u8*>(data);
		while (numBytes--)
			hash = CalcFnv1a32(*ptr++, hash);
		return hash;
	}
	inline u64 CalcFnv1a64(u8 oneByte, u64 hash = kFnv1aSeed64)
	{
		return (oneByte ^ hash) * kFnv1aPrime32;
	}
	inline u64 CalcFnv1a64(const void* data, size_t numBytes, u64 hash = kFnv1aSeed64)
	{
		assert(data);
		const u8* ptr = reinterpret_cast<const u8*>(data);
		while (numBytes--)
			hash = CalcFnv1a64(*ptr++, hash);
		return hash;
	}

	// compute aligned size.
	constexpr u32 GetAlignedSize(const u32 size, const u32 align)
	{
		return ((size + align - 1) / align) * align;
	}
	constexpr size_t GetAlignedSize(const size_t size, const size_t align)
	{
		return ((size + align - 1) / align) * align;
	}

	// hashed string.
	class HashString
	{
	public:
		HashString()
		{}
		HashString(const char* str)
			: str_(str)
		{
			hash_ = CalcFnv1a32(str_.c_str(), str_.size());
		}

		bool operator==(const HashString& rhs) const
		{
			if (hash_ != rhs.hash_) return false;
			return str_ == rhs.str_;
		}
		bool operator<(const HashString& rhs) const
		{
			if (hash_ != rhs.hash_) return hash_ < rhs.hash_;
			return str_ < rhs.str_;
		}
		bool operator>(const HashString& rhs) const
		{
			if (hash_ != rhs.hash_) return hash_ > rhs.hash_;
			return str_ > rhs.str_;
		}

		const std::string& GetString() const
		{
			return str_;
		}
		u32 GetHash() const
		{
			return hash_;
		}

	private:
		std::string	str_;
		u32			hash_ = 0;
	};	// class HashString

	// hashed wstring.
	class HashWString
	{
	public:
		HashWString()
		{}
		HashWString(const wchar_t* str)
			: str_(str)
		{
			hash_ = CalcFnv1a32(str_.c_str(), str_.size());
		}

		bool operator==(const HashWString& rhs) const
		{
			if (hash_ != rhs.hash_) return false;
			return str_ == rhs.str_;
		}
		bool operator<(const HashWString& rhs) const
		{
			if (hash_ != rhs.hash_) return hash_ < rhs.hash_;
			return str_ < rhs.str_;
		}
		bool operator>(const HashWString& rhs) const
		{
			if (hash_ != rhs.hash_) return hash_ > rhs.hash_;
			return str_ > rhs.str_;
		}

		const std::wstring& GetString() const
		{
			return str_;
		}
		u32 GetHash() const
		{
			return hash_;
		}

	private:
		std::wstring	str_;
		u32				hash_ = 0;
	};	// class HashWString

	// CPU time count class.
	class CpuTimer
	{
	public:
		static void Initialize()
		{
			QueryPerformanceFrequency(&frequency_);
		}

		static CpuTimer CurrentTime()
		{
			CpuTimer r;
			QueryPerformanceCounter(&r.time_);
			return r;
		}

		CpuTimer& operator=(const CpuTimer& v)
		{
			time_ = v.time_;
			return *this;
		}

		CpuTimer& operator+=(const CpuTimer& v)
		{
			time_.QuadPart += v.time_.QuadPart;
			return *this;
		}

		CpuTimer& operator-=(const CpuTimer& v)
		{
			time_.QuadPart -= v.time_.QuadPart;
			return *this;
		}

		CpuTimer operator+(const CpuTimer& v) const
		{
			CpuTimer r;
			r.time_.QuadPart = time_.QuadPart + v.time_.QuadPart;
			return r;
		}

		CpuTimer operator-(const CpuTimer& v) const
		{
			CpuTimer r;
			r.time_.QuadPart = time_.QuadPart - v.time_.QuadPart;
			return r;
		}

		float ToSecond() const
		{
			return (float)time_.QuadPart / (float)frequency_.QuadPart;
		}
		float ToMilliSecond() const
		{
			return (float)(time_.QuadPart * 1000) / (float)frequency_.QuadPart;
		}
		float ToMicroSecond() const
		{
			return (float)(time_.QuadPart * 1000 * 1000) / (float)frequency_.QuadPart;
		}
		float ToNanoSecond() const
		{
			return (float)(time_.QuadPart * 1000 * 1000 * 1000) / (float)frequency_.QuadPart;
		}

	private:
		static LARGE_INTEGER	frequency_;
		LARGE_INTEGER			time_ = {0};
	};	// class CpuTimer

	// random.
	class Random
	{
	public:
		Random()
		{}
		Random(u32 seed)
		{
			x_ = seed = 1812433253 * (seed ^ (seed >> 30));
			y_ = seed = 1812433253 * (seed ^ (seed >> 30)) + 1;
			z_ = seed = 1812433253 * (seed ^ (seed >> 30)) + 2;
			w_ = seed = 1812433253 * (seed ^ (seed >> 30)) + 3;
		}

		u32 GetValue()
		{
			u32 t = x_ ^ (x_ << 11);
			x_ = y_;
			y_ = z_;
			z_ = w_;
			return w_ = (w_ ^ (w_ >> 19)) ^ (t ^ (t >> 8));
		}

		float GetFValue()
		{
			return (float)GetValue() / (float)UINT_MAX;
		}

		float GetFValueRange(float minV, float maxV)
		{
			return minV + (maxV - minV) * GetFValue();
		}

	private:
		u32		x_ = 123456789;
		u32		y_ = 362436069;
		u32		z_ = 521288629;
		u32		w_ = 88675123;
	};	// class Random

	struct BoundingSphere
	{
		DirectX::XMFLOAT3	center;
		float				radius;

		BoundingSphere()
			: center(0.0f, 0.0f, 0.0f), radius(0.0f)
		{}
		BoundingSphere(float x, float y, float z, float r)
			: center(x, y, z), radius(r)
		{}
		BoundingSphere(const DirectX::XMFLOAT3& c, float r)
			: center(c), radius(r)
		{}
	};	// struct BoundingSphere

	// bounding box.
	struct BoundingBox
	{
		DirectX::XMFLOAT3	boxMin;
		DirectX::XMFLOAT3	boxMax;

		BoundingBox()
			: boxMin(0.0f, 0.0f, 0.0f), boxMax(0.0f, 0.0f, 0.0f)
		{}
		BoundingBox(float x0, float y0, float z0, float x1, float y1, float z1)
			: boxMin(x0, y0, z0), boxMax(x1, y1, z1)
		{}
		BoundingBox(const DirectX::XMFLOAT3& pmin, const DirectX::XMFLOAT3& pmax)
			: boxMin(pmin), boxMax(pmax)
		{}
	};	// struct BoundingBox

	// inverse z perspective matrix.
	inline DirectX::XMMATRIX MatrixPerspectiveInverseFovRH(float FovYRad, float Aspect, float Zn, float Zf)
	{
		float YScale = 1.0f / tanf(FovYRad * 0.5f);
		float XScale = YScale / Aspect;
		float DivZ = 1.0f / (Zf - Zn);
		DirectX::XMFLOAT4X4 m(
			XScale, 0.0f, 0.0f, 0.0f,
			0.0f, YScale, 0.0f, 0.0f,
			0.0f, 0.0f, Zn * DivZ, -1.0f,
			0.0f, 0.0f, Zn * Zf * DivZ, 0.0f);
		return DirectX::XMLoadFloat4x4(&m);
	}
	inline DirectX::XMMATRIX MatrixPerspectiveInverseFovLH(float FovYRad, float Aspect, float Zn, float Zf)
	{
		float YScale = 1.0f / tanf(FovYRad * 0.5f);
		float XScale = YScale / Aspect;
		float DivZ = 1.0f / (Zf - Zn);
		DirectX::XMFLOAT4X4 m(
			XScale, 0.0f, 0.0f, 0.0f,
			0.0f, YScale, 0.0f, 0.0f,
			0.0f, 0.0f, -Zn * DivZ, 1.0f,
			0.0f, 0.0f, Zn * Zf * DivZ, 0.0f);
		return DirectX::XMLoadFloat4x4(&m);
	}

	// infinite perspective matrix.
	inline DirectX::XMMATRIX MatrixPerspectiveInfiniteFovRH(float FovYRad, float Aspect, float Zn)
	{
		float YScale = 1.0f / tanf(FovYRad * 0.5f);
		float XScale = YScale / Aspect;
		DirectX::XMFLOAT4X4 m(
			XScale, 0.0f, 0.0f, 0.0f,
			0.0f, YScale, 0.0f, 0.0f,
			0.0f, 0.0f, -1.0f, -1.0f,
			0.0f, 0.0f, -Zn, 0.0f);
		return DirectX::XMLoadFloat4x4(&m);
	}
	inline DirectX::XMMATRIX MatrixPerspectiveInfiniteFovLH(float FovYRad, float Aspect, float Zn)
	{
		float YScale = 1.0f / tanf(FovYRad * 0.5f);
		float XScale = YScale / Aspect;
		DirectX::XMFLOAT4X4 m(
			XScale, 0.0f, 0.0f, 0.0f,
			0.0f, YScale, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			0.0f, 0.0f, -Zn, 0.0f);
		return DirectX::XMLoadFloat4x4(&m);
	}

	// infinite inverse z perspective matrix.
	inline DirectX::XMMATRIX MatrixPerspectiveInfiniteInverseFovRH(float FovYRad, float Aspect, float Zn)
	{
		float YScale = 1.0f / tanf(FovYRad * 0.5f);
		float XScale = YScale / Aspect;
		DirectX::XMFLOAT4X4 m(
			XScale, 0.0f, 0.0f, 0.0f,
			0.0f, YScale, 0.0f, 0.0f,
			0.0f, 0.0f, 0.0f, -1.0f,
			0.0f, 0.0f, Zn, 0.0f);
		return DirectX::XMLoadFloat4x4(&m);
	}
	inline DirectX::XMMATRIX MatrixPerspectiveInfiniteInverseFovLH(float FovYRad, float Aspect, float Zn)
	{
		float YScale = 1.0f / tanf(FovYRad * 0.5f);
		float XScale = YScale / Aspect;
		DirectX::XMFLOAT4X4 m(
			XScale, 0.0f, 0.0f, 0.0f,
			0.0f, YScale, 0.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f,
			0.0f, 0.0f, Zn, 0.0f);
		return DirectX::XMLoadFloat4x4(&m);
	}

	// inverse z ortho matrix.
	inline DirectX::XMMATRIX MatrixOrthoInverseFovRH(float w, float h, float Zn, float Zf)
	{
		float DivZ = 1.0f / (Zf - Zn);
		DirectX::XMFLOAT4X4 m(
			2.0f / w, 0.0f, 0.0f, 0.0f,
			0.0f, 2.0f / h, 0.0f, 0.0f,
			0.0f, 0.0f, DivZ, 0.0f,
			0.0f, 0.0f, Zf * DivZ, 1.0f);
		return DirectX::XMLoadFloat4x4(&m);
	}
	inline DirectX::XMMATRIX MatrixOrthoInverseFovLH(float w, float h, float Zn, float Zf)
	{
		float DivZ = 1.0f / (Zf - Zn);
		DirectX::XMFLOAT4X4 m(
			2.0f / w, 0.0f, 0.0f, 0.0f,
			0.0f, 2.0f / h, 0.0f, 0.0f,
			0.0f, 0.0f, -DivZ, 1.0f,
			0.0f, 0.0f, Zf * DivZ, 0.0f);
		return DirectX::XMLoadFloat4x4(&m);
	}
	
	// View Z from perspective matrix.
	inline float ViewZFromPerspective(const DirectX::XMMATRIX& Persp, float DeviceZ)
	{
		float a = Persp.r[2].m128_f32[2];
		float b = Persp.r[3].m128_f32[2];
		float c = Persp.r[2].m128_f32[3];
		return -b / (a - c * DeviceZ);
	}

	// frustum from view-projection matrix.
	inline int CalcFrustumPlanes(const DirectX::XMMATRIX& mtxViewProj, bool bInverse, bool bInfinite, DirectX::XMFLOAT4 outFrustumPlanes[6])
	{
		const float kNearDeviceZ = bInverse ? 1.0f : 0.0f;
		const float kFarDeviceZ = 0.5f;

		DirectX::XMMATRIX mtxProjView = DirectX::XMMatrixInverse(nullptr, mtxViewProj);
		DirectX::XMFLOAT4 BasePoints[8] = {
			DirectX::XMFLOAT4(-1.0f,  1.0f, kNearDeviceZ, 1.0f),
			DirectX::XMFLOAT4( 1.0f,  1.0f, kNearDeviceZ, 1.0f),
			DirectX::XMFLOAT4(-1.0f, -1.0f, kNearDeviceZ, 1.0f),
			DirectX::XMFLOAT4( 1.0f, -1.0f, kNearDeviceZ, 1.0f),
			DirectX::XMFLOAT4(-1.0f,  1.0f, kFarDeviceZ, 1.0f),
			DirectX::XMFLOAT4( 1.0f,  1.0f, kFarDeviceZ, 1.0f),
			DirectX::XMFLOAT4(-1.0f, -1.0f, kFarDeviceZ, 1.0f),
			DirectX::XMFLOAT4( 1.0f, -1.0f, kFarDeviceZ, 1.0f),
		};
		DirectX::XMVECTOR FrustumPoints[8];
		for (int i = 0; i < 8; i++)
		{
			FrustumPoints[i] = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat4(&BasePoints[i]), mtxProjView);
		}

		auto CalcPlane = [FrustumPoints](int v0, int v1, int v2)
		{
			auto ab = DirectX::XMVectorSubtract(FrustumPoints[v1], FrustumPoints[v0]);
			auto ac = DirectX::XMVectorSubtract(FrustumPoints[v2], FrustumPoints[v0]);
			auto normal = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(ab, ac));
			auto d = DirectX::XMVector3Dot(normal, FrustumPoints[v0]);
			DirectX::XMFLOAT4 r;
			DirectX::XMStoreFloat4(&r, normal);
			r.w = -d.m128_f32[0];
			return r;
		};

		outFrustumPlanes[0] = CalcPlane(0, 2, 4);
		outFrustumPlanes[1] = CalcPlane(1, 5, 3);
		outFrustumPlanes[2] = CalcPlane(0, 4, 1);
		outFrustumPlanes[3] = CalcPlane(2, 3, 6);
		outFrustumPlanes[4] = CalcPlane(0, 1, 2);
		if (bInfinite)
		{
			outFrustumPlanes[5] = CalcPlane(0, 1, 2);
			return 5;
		}

		outFrustumPlanes[5] = CalcPlane(4, 6, 5);
		return 6;
	}


	extern Random GlobalRandom;
}	// namespace sl12

//	EOF
