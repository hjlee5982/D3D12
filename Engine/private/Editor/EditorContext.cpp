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

#include "imgui.h"
#include "imgui_internal.h" // 창의 탭/제목 줄 영역, 설정 저장 표시

using namespace EditorUtil;

// ---------------------------------------------------------------------------------------------
// 창 열기/닫기
// ---------------------------------------------------------------------------------------------

const char* EditorContext::GetWindowName(EditorWindow window)
{
	switch (window)
	{
	case EditorWindow::Scene:     return "Scene";
	case EditorWindow::Game:      return "Game";
	case EditorWindow::Hierarchy: return "Hierarchy";
	case EditorWindow::Inspector: return "Inspector";
	case EditorWindow::Project:   return "Project";
	case EditorWindow::Assets:    return "Assets";
	default:                      return "";
	}
}

void EditorContext::OpenWindow(EditorWindow window)
{
	const int index = static_cast<int>(window);
	if (!windowOpen[index])
	{
		windowOpen[index] = true;
		ImGui::MarkIniSettingsDirty();
	}
	windowFocusRequest[index] = true;
}

void EditorContext::CloseWindow(EditorWindow window)
{
	const int index = static_cast<int>(window);
	if (windowOpen[index])
	{
		windowOpen[index] = false;
		ImGui::MarkIniSettingsDirty();
	}
	windowFocusRequest[index] = false;
}

void EditorContext::OpenAllWindows()
{
	for (int i = 0; i < kWindowCount; ++i)
	{
		windowOpen[i] = true;
	}
	ImGui::MarkIniSettingsDirty();
}

bool EditorContext::PrepareWindow(EditorWindow window)
{
	const int index = static_cast<int>(window);
	if (!windowOpen[index])
	{
		return false;
	}
	if (windowFocusRequest[index])
	{
		// 같은 탭 묶음에 있으면 그 탭을 고르고, 떠 있는 창이면 맨 앞으로
		ImGui::SetNextWindowFocus();
		windowFocusRequest[index] = false;
	}
	return true;
}

void EditorContext::HandleWindowTitleClick(EditorWindow window)
{
	if (!ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
	{
		return;
	}

	// 도킹된 창은 탭, 떠 있는 창은 제목 줄. 탭은 도킹 공간(호스트 창)에 그려지므로 그 창이 마우스 아래에 있어야 한다.
	const ImGuiContext& g = *ImGui::GetCurrentContext();
	ImGuiWindow* current = ImGui::GetCurrentWindow();
	const bool docked = current->DockIsActive && current->DockNode != nullptr;
	const ImRect rect = docked ? current->DC.DockTabItemRect : current->TitleBarRect();
	ImGuiWindow* owner = docked ? current->DockNode->HostWindow : current;

	if (g.HoveredWindow != nullptr && owner != nullptr && g.HoveredWindow->RootWindowDockTree == owner->RootWindowDockTree
		&& rect.GetWidth() > 0.0f && rect.Contains(ImGui::GetMousePos()))
	{
		CloseWindow(window);
	}
}

std::string EditorContext::GetClosedWindowList() const
{
	std::string list;
	for (int i = 0; i < kWindowCount; ++i)
	{
		if (!windowOpen[i])
		{
			list += list.empty() ? "" : ",";
			list += GetWindowName(static_cast<EditorWindow>(i));
		}
	}
	return list;
}

void EditorContext::SetClosedWindowList(const std::string& list)
{
	for (int i = 0; i < kWindowCount; ++i)
	{
		windowOpen[i] = true;
	}

	size_t start = 0;
	while (start <= list.size())
	{
		size_t end = list.find(',', start);
		end = end == std::string::npos ? list.size() : end;
		const std::string name = list.substr(start, end - start);
		for (int i = 0; i < kWindowCount; ++i)
		{
			if (name == GetWindowName(static_cast<EditorWindow>(i)))
			{
				windowOpen[i] = false;
			}
		}
		start = end + 1;
	}
}

// ---------------------------------------------------------------------------------------------
// 미뤄 둔 씬 변경
// ---------------------------------------------------------------------------------------------

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
