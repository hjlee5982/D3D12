#include "pch.h"
#include "EditorContext.h"
#include "EditorUtils.h"

#include "Camera.h"
#include "Engine.h"
#include "GameObject.h"
#include "Log.h"
#include "MeshRenderer.h"
#include "ResourceManager.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Transform.h"

using namespace EditorUtil;

void EditorContext::ApplyPendingEdits()
{
	Scene* scene = engine.GetSceneManager().GetActiveScene();

	if (scene != nullptr)
	{
		if (hasPendingReparent)
		{
			GameObject* child  = pendingReparentChild.Get();
			GameObject* parent = pendingReparentParent.Get();
			if (child != nullptr)
			{
				SetParentKeepWorld(child->transform, parent != nullptr ? parent->transform : nullptr);
			}
			hasPendingReparent = false;
		}

		if (!pendingCreateKind.empty())
		{
			if (GameObject* created = CreateObject(scene, pendingCreateKind.c_str(), pendingCreateParent.Get()))
			{
				selected = ObjectRef(created);
			}
			pendingCreateKind.clear();
			pendingCreateParent.Reset();
		}

		if (GameObject* doomed = pendingDelete.Get())
		{
			EngineLog("[Editor] Deleted object '%s' (#%llu)", doomed->GetName().c_str(), static_cast<unsigned long long>(doomed->GetId()));
			scene->DestroyGameObject(doomed);
		}
		pendingDelete.Reset();
	}

	// 씬 교체는 다른 변경을 다 처리한 뒤 마지막에 (이 뒤로는 scene 포인터가 무효)
	// 재생 중에 다른 씬을 열면 재생 세션을 끝낸다 (복원 없이: 곧 씬을 통째로 바꾸므로).
	// Stop()은 다음 프레임에 재생 전 씬을 복원하므로 여기서 쓰면 방금 연 씬을 덮어쓴다.
	if ((pendingNew || !pendingLoad.empty()) && engine.IsPlaying())
	{
		engine.DiscardPlaySession();
	}
	if (pendingNew)
	{
		engine.NewScene("Untitled");
		selected.Reset();
		pendingNew = false;
	}
	if (!pendingLoad.empty())
	{
		if (engine.LoadScene(pendingLoad))
		{
			selected.Reset();
		}
		pendingLoad.clear();
	}
}

GameObject* EditorContext::CreateObject(Scene* scene, const char* kind, GameObject* parent)
{
	const std::string type = kind;
	GameObject* object = scene->CreateGameObject(type == "Empty" ? "GameObject" : type);
	if (object == nullptr)
	{
		return nullptr;
	}

	if (parent != nullptr)
	{
		object->transform->SetParent(parent->transform);
	}

	if (type == "Quad")
	{
		ResourceManager* resources = engine.GetResourceManager();
		if (resources != nullptr)
		{
			// 선택된 머티리얼 → 없으면 첫 번째 머티리얼
			MaterialHandle material = resources->IsAlive(selectedMaterial) ? selectedMaterial : MaterialHandle{};
			if (!material)
			{
				const auto materials = resources->ListMaterials();
				material = materials.empty() ? MaterialHandle{} : materials.front().handle;
			}
			object->AddComponent<MeshRenderer>(resources->GetQuadMesh(), material);
		}
	}
	else if (type == "Camera")
	{
		object->AddComponent<Camera>();
	}

	EngineLog("[Editor] Created %s object '%s' (#%llu)", type.c_str(), object->GetName().c_str(), static_cast<unsigned long long>(object->GetId()));
	return object;
}
