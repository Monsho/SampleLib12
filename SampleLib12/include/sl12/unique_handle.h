#pragma once

#include <sl12/device.h>


namespace sl12
{
	template <typename T>
	class UniqueHandle
	{
	public:
		UniqueHandle()
			: pParentDevice_(nullptr)
			, pObject_(nullptr)
		{}
		UniqueHandle(Device* pDev)
			: pParentDevice_(pDev)
			, pObject_(nullptr)
		{}
		UniqueHandle(T* p, Device* pDev = nullptr)
			: pParentDevice_(pDev)
			, pObject_(p)
		{}
		UniqueHandle(UniqueHandle<T>&& t) noexcept
		{
			pParentDevice_ = t.pParentDevice_;
			pObject_ = t.Release();
		}
		~UniqueHandle()
		{
			Reset(nullptr);
		}

		UniqueHandle& operator=(UniqueHandle<T>&& t) noexcept
		{
			if (this != std::addressof(t))
			{
				Reset(t.Release());
				pParentDevice_ = t.pParentDevice_;
			}
			return *this;
		}
		UniqueHandle& operator=(nullptr_t) noexcept
		{
			Reset();
			return *this;
		}

		T* operator->() const noexcept
		{
			return pObject_;
		}

		T* operator&() const noexcept
		{
			return pObject_;
		}

		void Reset(T* p = nullptr) noexcept
		{
			if (pObject_ == p)
			{
				return;
			}
			
			if (pObject_)
			{
				if (pParentDevice_)
				{
					pParentDevice_->KillObject(pObject_);
				}
				else
				{
					delete pObject_;
				}
				pObject_ = nullptr;
			}

			pObject_ = p;
		}

		T* Release() noexcept
		{
			T* p = pObject_;
			pObject_ = nullptr;
			return p;
		}

		void Swap(UniqueHandle& Right) noexcept
		{
			if (pObject_ == Right.pObject_)
			{
				return;
			}
			
			std::swap(pParentDevice_, Right.pParentDevice_);
			std::swap(pObject_, Right.pObject_);
		}

		bool IsValid() const noexcept
		{
			return pObject_ != nullptr;
		}

	private:
		Device*	pParentDevice_ = nullptr;
		T*		pObject_ = nullptr;
	};	// class UniqueHandle

	template <class _T, class... _Types>
	UniqueHandle<_T> MakeUnique(Device* pDev, _Types&&... _Args)
	{
		return UniqueHandle<_T>(new _T(std::forward<_Types>(_Args)...), pDev);
	}

}	// namespace sl12

//	EOF
