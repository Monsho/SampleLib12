﻿#include "sl12/resource_loader.h"

#include <filesystem>

#include "sl12/device.h"


namespace sl12
{

	//--------
	bool ResourceHandle::IsValid() const
	{
		if (pParentLoader_ == nullptr)
			return false;
		return pParentLoader_->GetItemBaseFromID(id_) != nullptr;
	}

	//--------
	const ResourceItemBase* ResourceHandle::GetItemBase() const
	{
		if (pParentLoader_ == nullptr)
			return nullptr;
		return pParentLoader_->GetItemBaseFromID(id_);
	}


	//--------
	ResourceLoader::~ResourceLoader()
	{
		Destroy();
	}

	//--------
	bool ResourceLoader::Initialize(Device* pDevice, MeshManager* pMeshMan, const std::string& basePath)
	{
		assert(pDevice != nullptr);
		assert(pMeshMan != nullptr);

		pDevice_ = pDevice;
		pMeshManager_ = pMeshMan;
		handleID_ = 0;
		resourceMap_.clear();
		resourceBasePath_ = basePath;

		// create thread.
		std::thread th([&]
		{
			isAlive_ = true;
			while (isAlive_)
			{
				{
					std::unique_lock<std::mutex> lock(requestMutex_);
					requestCV_.wait(lock, [&] { return !requestList_.empty() || !isAlive_; });
				}

				if (!isAlive_)
				{
					break;
				}

				if (!ThreadBody())
				{
					break;
				}
			}
		});
		loadingThread_ = std::move(th);

		return true;
	}

	//--------
	bool ResourceLoader::ThreadBody()
	{
		isLoading_ = true;

		std::list<RequestItem> items;
		{
			std::lock_guard<std::mutex> lock(listMutex_);
			items.swap(requestList_);
		}

		for (auto&& item : items)
		{
			ResourceItemBase* base = item.funcLoad(this, item.handle, item.filePath);
			if (base)
			{
				base->pParentLoader_ = this;
				base->filePath_ = item.filePath;
				base->fullPath_ = MakeFullPath(item.filePath);
				resourceMap_[item.id].reset(base);
			}

			if (!isAlive_)
			{
				return false;
			}
		}

		isLoading_ = false;

		return true;
	}

	//--------
	void ResourceLoader::Destroy()
	{
		isAlive_ = false;
		requestCV_.notify_one();

		if (loadingThread_.joinable())
			loadingThread_.join();

		requestList_.clear();
		resourceMap_.clear();
	}

	//--------
	std::string ResourceLoader::MakeFullPath(const std::string& filePath)
	{
		std::filesystem::path p(resourceBasePath_);
		p.append(filePath);
		return p.string();
	}

	//--------
	ResourceHandle ResourceLoader::LoadRequest(const std::string& filepath, LoadFunc func)
	{
		RequestItem item;
		item.filePath = filepath;
		item.funcLoad = func;

		{
			std::lock_guard<std::mutex> lock(listMutex_);
			auto it = resourceMap_.begin();
			do
			{
				item.id = handleID_.fetch_add(1);
				it = resourceMap_.find(item.id);
			} while (it != resourceMap_.end());
			item.handle = ResourceHandle(this, item.id);
			resourceMap_[item.id].reset();
			requestList_.push_back(item);
		}

		std::lock_guard<std::mutex> lock(requestMutex_);
		requestCV_.notify_one();

		return item.handle;
	}

	//--------
	const ResourceItemBase* ResourceLoader::GetItemBaseFromID(u64 id) const
	{
		auto it = resourceMap_.find(id);
		if (it == resourceMap_.end())
		{
			return nullptr;
		}
		return it->second.get();
	}

}	// namespace sl12


//	EOF
