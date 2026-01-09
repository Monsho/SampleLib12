#pragma once

#include <vector>
#include <memory>
#include "sl12/util.h"
#include "sl12/cbv_manager.h"


namespace sl12
{
	class SceneMesh;
	class SceneSubmesh;

	//------------
	struct RenderCommandType
	{
		enum Type
		{
			Unknown,
			Mesh,
			Submesh,

			Max
		};
	};	// struct RenderCommandType


	//------------
	class RenderCommand
	{
	public:
		RenderCommand()
		{}
		virtual ~RenderCommand()
		{}

		// getter
		virtual RenderCommandType::Type GetType() const
		{
			return RenderCommandType::Unknown;
		}
		const BoundingSphere& GetBoundingSphere() const
		{
			return boundingSphere_;
		}
		const BoundingBox& GetBoundingBox() const
		{
			return boundingBox_;
		}
		bool IsUnbound() const
		{
			return isUnbound_;
		}

	protected:
		BoundingSphere	boundingSphere_ = BoundingSphere();
		BoundingBox		boundingBox_ = BoundingBox();
		bool			isUnbound_ = true;
	};	// class RenderCommand


	//------------
	class SubmeshRenderCommand
		: public RenderCommand
	{
	public:
		SubmeshRenderCommand(SceneSubmesh* pSubmesh);
		virtual ~SubmeshRenderCommand();

		// getter
		virtual RenderCommandType::Type GetType() const
		{
			return RenderCommandType::Submesh;
		}
		SceneSubmesh* GetParentSubmesh()
		{
			return pParentSubmesh_;
		}

	private:
		SceneSubmesh*	pParentSubmesh_ = nullptr;
	};	// class SubmeshRenderCommand


	//------------
	class MeshRenderCommand
		: public RenderCommand
	{
	public:
		MeshRenderCommand(SceneMesh* pMesh, CbvManager* pCbvMan);
		virtual ~MeshRenderCommand();

		// getter
		virtual RenderCommandType::Type GetType() const
		{
			return RenderCommandType::Mesh;
		}
		SceneMesh* GetParentMesh()
		{
			return pParentMesh_;
		}
		ConstantBufferView* GetCBView()
		{
			return cbHandle_.GetCBV();
		}
		auto&& GetSubmeshCommands()
		{
			return submeshCommands_;
		}

	private:
		SceneMesh*	pParentMesh_ = nullptr;
		CbvHandle	cbHandle_;

		std::vector<std::unique_ptr<SubmeshRenderCommand>>	submeshCommands_;

	private:
		static std::function<CbvHandle(SceneMesh*, CbvManager*)> sCreateCbvFn_;

	public:
		static void SetCreateCbvFn(std::function<CbvHandle(SceneMesh*, CbvManager*)> fn)
		{
			sCreateCbvFn_ = fn;
		}
	};	// class MeshRenderCommand

}	// namespace sl12

//	EOF
