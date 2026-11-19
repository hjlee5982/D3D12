#include "pch.h"
#include "HierarchyPanel.h"
#include "EditorUtils.h"

#include "Camera.h"
#include "ComponentRegistry.h"
#include "Engine.h"
#include "GameObject.h"
#include "Log.h"
#include "MeshRenderer.h"
#include "RenderResources.h"
#include "ResourceManager.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Transform.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace EditorUtil;

void HierarchyPanel::Draw(Scene* scene)
{
	if (!ImGui::Begin("Hierarchy"))
	{
		ImGui::End();
		return;
	}

	if (scene == nullptr)
	{
		ImGui::TextDisabled("No active scene");
		ImGui::End();
		return;
	}

	if (ImGui::Button("+ Empty")) { _ctx.pendingCreateKind = "Empty"; }
	ImGui::SameLine();
	if (ImGui::Button("+ Quad"))  { _ctx.pendingCreateKind = "Quad"; }
	ImGui::SameLine();
	ImGui::BeginDisabled(_ctx.selected.Get() == nullptr);
	if (ImGui::Button("Delete"))  { _ctx.pendingDelete = _ctx.selected; }
	ImGui::EndDisabled();
	ImGui::Separator();

	for (const auto& object : scene->GetGameObjects())
	{
		if (object != nullptr && !object->IsPendingDestroy() && object->transform != nullptr && object->transform->GetParent() == nullptr)
		{
			DrawNode(object.get());
		}
	}

	// 빈 영역에 놓으면 루트로 (부모 해제)
	ImVec2 remaining = ImGui::GetContentRegionAvail();
	remaining.y = remaining.y < 20.0f ? 20.0f : remaining.y;
	ImGui::InvisibleButton("##HierarchyBackground", remaining);
	if (ImGui::IsItemClicked())
	{
		_ctx.selected.Reset();
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kObjectPayload))
		{
			_ctx.pendingReparentChild  = ObjectRef(*static_cast<const ObjectId*>(payload->Data));
			_ctx.pendingReparentParent.Reset();
			_ctx.hasPendingReparent    = true;
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete) && _ctx.selected.Get() != nullptr)
	{
		_ctx.pendingDelete = _ctx.selected;
	}

	ImGui::End();
}

void HierarchyPanel::DrawNode(GameObject* object)
{
	Transform* transform = object->transform;
	const u32 childCount = transform->GetChildCount();
	const ObjectId id = object->GetId();

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
	if (childCount == 0)
	{
		flags |= ImGuiTreeNodeFlags_Leaf;
	}
	if (_ctx.selected.id == id)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}

	if (!object->IsActive())
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	}
	const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(id)), flags, "%s", object->GetName().c_str());
	if (!object->IsActive())
	{
		ImGui::PopStyleColor();
	}

	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
	{
		_ctx.selected = ObjectRef(object);
	}

	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload(kObjectPayload, &id, sizeof(id));
		ImGui::Text("%s", object->GetName().c_str());
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kObjectPayload))
		{
			_ctx.pendingReparentChild  = ObjectRef(*static_cast<const ObjectId*>(payload->Data));
			_ctx.pendingReparentParent = ObjectRef(object);
			_ctx.hasPendingReparent    = true;
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::BeginPopupContextItem())
	{
		_ctx.selected = ObjectRef(object);
		if (ImGui::MenuItem("Create Empty Child"))
		{
			_ctx.pendingCreateKind   = "Empty";
			_ctx.pendingCreateParent = ObjectRef(object);
		}
		if (ImGui::MenuItem("Unparent", nullptr, false, transform->GetParent() != nullptr))
		{
			_ctx.pendingReparentChild = ObjectRef(object);
			_ctx.pendingReparentParent.Reset();
			_ctx.hasPendingReparent   = true;
		}
		if (ImGui::MenuItem("Delete"))
		{
			_ctx.pendingDelete = ObjectRef(object);
		}
		ImGui::EndPopup();
	}

	if (open)
	{
		for (u32 i = 0; i < childCount; ++i)
		{
			Transform* child = transform->GetChild(i);
			if (child != nullptr && child->GetOwner() != nullptr && !child->GetOwner()->IsPendingDestroy())
			{
				DrawNode(child->GetOwner());
			}
		}
		ImGui::TreePop();
	}
}
