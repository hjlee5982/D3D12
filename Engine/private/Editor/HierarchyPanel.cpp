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

namespace
{
	// 유니티의 GameObject 아이콘처럼 위에서 비스듬히 본 정육면체 테두리 (윗면은 옅게 채움)
	void DrawCubeIcon(ImDrawList* drawList, const ImVec2& min, f32 size, ImU32 color)
	{
		const f32 cx = min.x + size * 0.5f;
		const f32 cy = min.y + size * 0.5f;
		const f32 r  = size * 0.46f;
		const f32 dx = r * 0.866f; // cos 30°

		const ImVec2 center(cx, cy);
		const ImVec2 top(cx, cy - r);
		const ImVec2 upperRight(cx + dx, cy - r * 0.5f);
		const ImVec2 lowerRight(cx + dx, cy + r * 0.5f);
		const ImVec2 bottom(cx, cy + r);
		const ImVec2 lowerLeft(cx - dx, cy + r * 0.5f);
		const ImVec2 upperLeft(cx - dx, cy - r * 0.5f);

		const ImVec2 topFace[4] = { upperLeft, top, upperRight, center };
		drawList->AddConvexPolyFilled(topFace, 4, (color & ~IM_COL32_A_MASK) | (((color >> IM_COL32_A_SHIFT) & 0xFF) / 4 << IM_COL32_A_SHIFT));

		const f32 thickness = size >= 18.0f ? 1.5f : 1.2f;
		const ImVec2 outline[6] = { top, upperRight, lowerRight, bottom, lowerLeft, upperLeft };
		drawList->AddPolyline(outline, 6, color, ImDrawFlags_Closed, thickness);
		drawList->AddLine(center, upperLeft, color, thickness);
		drawList->AddLine(center, upperRight, color, thickness);
		drawList->AddLine(center, bottom, color, thickness);
	}
}

void HierarchyPanel::Draw(Scene* scene)
{
	if (!_ctx.PrepareWindow(EditorWindow::Hierarchy))
	{
		return;
	}
	const bool visible = ImGui::Begin("Hierarchy");
	_ctx.HandleWindowTitleClick(EditorWindow::Hierarchy);
	if (!visible)
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

	// 이름 앞에 아이콘 자리를 공백으로 비워 두고, 노드를 그린 뒤 그 자리에 정육면체 아이콘을 그린다
	const f32 iconSize = ImGui::GetTextLineHeight();
	const int padding  = static_cast<int>(std::ceil((iconSize + 6.0f) / ImGui::CalcTextSize(" ").x));
	const std::string label = std::string(static_cast<size_t>(padding), ' ') + object->GetName();

	// 비활성 오브젝트는 이름과 아이콘을 흐리게
	const ImU32 iconColor = ImGui::GetColorU32(object->IsActive() ? ImGuiCol_Text : ImGuiCol_TextDisabled, 0.85f);
	if (!object->IsActive())
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	}
	const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(id)), flags, "%s", label.c_str());
	if (!object->IsActive())
	{
		ImGui::PopStyleColor();
	}

	const ImVec2 rowMin = ImGui::GetItemRectMin();
	const f32 rowHeight = ImGui::GetItemRectSize().y;
	DrawCubeIcon(ImGui::GetWindowDrawList(), ImVec2(rowMin.x + ImGui::GetTreeNodeToLabelSpacing(), rowMin.y + (rowHeight - iconSize) * 0.5f), iconSize, iconColor);

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
