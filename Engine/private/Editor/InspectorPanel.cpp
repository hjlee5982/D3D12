#include "pch.h"
#include "InspectorPanel.h"
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

void InspectorPanel::Draw()
{
	if (!_ctx.PrepareWindow(EditorWindow::Inspector))
	{
		return;
	}
	const bool visible = ImGui::Begin("Inspector");
	_ctx.HandleWindowTitleClick(EditorWindow::Inspector);
	if (!visible)
	{
		ImGui::End();
		return;
	}

	if (_ctx.engine.IsPlaying())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Play mode: changes are reverted on Stop");
		ImGui::Separator();
	}

	GameObject* object = _ctx.selected.Get();
	if (object == nullptr)
	{
		_ctx.selected.Reset();
		ImGui::TextDisabled("Select an object in the Hierarchy or click it in the Scene view");
		ImGui::End();
		return;
	}

	bool active = object->IsActive();
	if (ImGui::Checkbox("##active", &active))
	{
		object->SetActive(active);
	}
	ImGui::SameLine();
	std::string name = object->GetName();
	ImGui::SetNextItemWidth(-60.0f);
	if (ImGui::InputText("##name", &name))
	{
		object->SetName(name);
	}
	ImGui::SameLine();
	ImGui::TextDisabled("#%llu", static_cast<unsigned long long>(object->GetId()));
	ImGui::Separator();

	DrawObjectDetails(object);

	Component* removeRequest = nullptr;
	for (u32 i = 0; i < object->GetComponentCount(); ++i)
	{
		if (Component* component = object->GetComponentAt(i))
		{
			DrawComponent(object, component, removeRequest);
		}
	}

	if (removeRequest != nullptr)
	{
		EngineLog("[Editor] Removed component '%s' from '%s'", removeRequest->GetTypeName(), object->GetName().c_str());
		object->RemoveComponent(removeRequest);
	}

	ImGui::Separator();
	if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
	{
		ImGui::OpenPopup("AddComponentPopup");
	}
	if (ImGui::BeginPopup("AddComponentPopup"))
	{
		for (const ComponentTypeInfo* type : _ctx.engine.GetComponentRegistry().GetTypes())
		{
			if (type->id == Transform::StaticTypeId())
			{
				continue;
			}

			const char* origin = type->module == nullptr ? "Engine" : "Game";
			if (ImGui::MenuItem(type->name.c_str(), origin))
			{
				if (Component* added = object->AddComponentByType(*type))
				{
					EngineLog("[Editor] Added component '%s' to '%s'", added->GetTypeName(), object->GetName().c_str());
				}
			}
		}
		ImGui::EndPopup();
	}

	ImGui::End();
}

void InspectorPanel::DrawObjectDetails(GameObject* object)
{
	if (!ImGui::CollapsingHeader("Details", ImGuiTreeNodeFlags_DefaultOpen))
	{
		return;
	}

	ImGui::Indent(8.0f);

	if (ImGui::BeginTable("ObjectDetails", 2, ImGuiTableFlags_SizingStretchProp))
	{
		auto row = [](const char* label)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", label);
			ImGui::TableNextColumn();
		};

		row("ID");
		ImGui::Text("#%llu", static_cast<unsigned long long>(object->GetId()));

		row("Parent");
		Transform* parent = object->transform->GetParent();
		if (parent != nullptr && parent->GetOwner() != nullptr)
		{
			// 클릭하면 부모 선택
			if (ImGui::SmallButton(parent->GetOwner()->GetName().c_str()))
			{
				_ctx.selected = ObjectRef(parent->GetOwner());
			}
		}
		else
		{
			ImGui::TextDisabled("(root)");
		}

		row("Children");
		ImGui::Text("%u", object->transform->GetChildCount());

		row("Components");
		ImGui::Text("%u", object->GetComponentCount());

		// 월드 변환 (부모까지 반영된 최종 값, 읽기 전용)
		vec3 scale;
		quat rotation;
		vec3 position;
		matx world = object->transform->GetWorldMatrix();
		if (world.Decompose(scale, rotation, position))
		{
			const vec3 euler = ToEuler(rotation);
			row("World position");
			ImGui::Text("%.3f, %.3f, %.3f", position.x, position.y, position.z);
			row("World rotation");
			ImGui::Text("%.1f, %.1f, %.1f", CleanZero(DirectX::XMConvertToDegrees(euler.x)), CleanZero(DirectX::XMConvertToDegrees(euler.y)), CleanZero(DirectX::XMConvertToDegrees(euler.z)));
			row("World scale");
			ImGui::Text("%.3f, %.3f, %.3f", scale.x, scale.y, scale.z);
		}

		row("State");
		ImGui::Text("%s%s", object->IsActive() ? "active" : "inactive", object->IsPendingDestroy() ? ", pending destroy" : "");

		ImGui::EndTable();
	}

	ImGui::Unindent(8.0f);
	ImGui::Spacing();
}

void InspectorPanel::DrawComponent(GameObject* object, Component* component, Component*& removeRequest)
{
	ImGui::PushID(component);

	const bool isTransform = component == object->transform;
	const ComponentTypeInfo* info = _ctx.engine.GetComponentRegistry().Find(component->GetTypeId());

	const bool open = ImGui::CollapsingHeader(component->GetTypeName(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

	if (!isTransform)
	{
		// 헤더 오른쪽: 활성화 체크박스 + 제거 버튼
		const f32 right = ImGui::GetContentRegionMax().x;
		ImGui::SameLine(right - 90.0f);
		bool enabled = component->IsEnabled();
		if (ImGui::Checkbox("##enabled", &enabled))
		{
			component->SetEnabled(enabled);
		}
		ImGui::SameLine(right - 58.0f);
		if (ImGui::SmallButton("Remove"))
		{
			removeRequest = component;
		}
	}

	if (open)
	{
		ImGui::Indent(8.0f);
		if (info == nullptr)
		{
			ImGui::TextDisabled("Not registered (call Register<%s>() in GameRegisterTypes)", component->GetTypeName());
		}
		else if (info->fields.empty())
		{
			ImGui::TextDisabled("No reflected fields (add a static Reflect)");
		}
		else
		{
			for (const FieldInfo& field : info->fields)
			{
				DrawField(component, field);
			}
		}

		DrawComponentDetails(component);

		if (info != nullptr && info->module != nullptr)
		{
			ImGui::TextDisabled("Defined in Game.dll (state kept across reload)");
		}
		ImGui::Unindent(8.0f);
	}

	ImGui::PopID();
}

void InspectorPanel::DrawComponentDetails(Component* component)
{
	ResourceManager* resources = _ctx.engine.GetResourceManager();

	// MeshRenderer: 핸들이 실제로 가리키는 리소스 정보
	if (component->IsA(MeshRenderer::StaticTypeId()) && resources != nullptr)
	{
		const MeshRenderer* renderer = static_cast<const MeshRenderer*>(component);
		const Mesh*     mesh     = resources->Get(renderer->GetMesh());
		const Material* material = resources->Get(renderer->GetMaterial());
		const Shader*   shader   = material != nullptr ? resources->Get(material->shader) : nullptr;
		const Texture*  texture  = material != nullptr ? resources->Get(material->texture) : nullptr;

		ImGui::TextDisabled("Mesh: %s", mesh != nullptr ? mesh->name.c_str() : "(none)");
		if (mesh != nullptr)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(%zu vertices, %u indices)", mesh->positions.size(), mesh->indexCount);
		}
		ImGui::TextDisabled("Shader: %s", shader != nullptr ? shader->path.c_str() : "(none)");
		if (texture != nullptr)
		{
			ImGui::TextDisabled("Texture: %s (%ux%u)", texture->path.c_str(), texture->width, texture->height);
			// 미리보기
			const f32 size = 64.0f;
			ImGui::Image(reinterpret_cast<ImTextureID>(texture->srv.Get()), ImVec2(size, size));
		}
		else
		{
			ImGui::TextDisabled("Texture: (none)");
		}

		if (mesh == nullptr || shader == nullptr)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Not rendered: missing %s", mesh == nullptr ? "mesh" : "material/shader");
		}
	}

	// Camera: Scene 창의 게임 카메라 모드에서 실제로 쓰이는 카메라인지
	if (component->IsA(Camera::StaticTypeId()))
	{
		const Scene* scene = _ctx.engine.GetSceneManager().GetActiveScene();
		bool isActiveCamera = false;
		if (scene != nullptr)
		{
			for (const auto& object : scene->GetGameObjects())
			{
				if (object == nullptr || !object->IsActive() || object->IsPendingDestroy())
				{
					continue;
				}
				const Camera* camera = object->GetComponent<Camera>();
				if (camera != nullptr && camera->IsEnabled())
				{
					isActiveCamera = camera == component;
					break;
				}
			}
		}
		ImGui::TextDisabled(isActiveCamera ? "Active game camera (used by the Game window / F1)" : "Not the active game camera");
	}
}

void InspectorPanel::DrawField(Component* component, const FieldInfo& field)
{
	u8* address = reinterpret_cast<u8*>(component) + field.offset;
	const char* label = field.name.c_str();
	ResourceManager* resources = _ctx.engine.GetResourceManager();

	switch (field.type)
	{
	case FieldType::Bool:  ImGui::Checkbox(label, reinterpret_cast<bool*>(address)); break;
	case FieldType::Int:   ImGui::DragInt(label, reinterpret_cast<i32*>(address)); break;
	case FieldType::UInt:  ImGui::DragScalar(label, ImGuiDataType_U32, address); break;
	case FieldType::Float: ImGui::DragFloat(label, reinterpret_cast<f32*>(address), 0.05f); break;
	case FieldType::Vec2:  ImGui::DragFloat2(label, reinterpret_cast<f32*>(address), 0.05f); break;
	case FieldType::Vec3:  ImGui::DragFloat3(label, reinterpret_cast<f32*>(address), 0.05f); break;
	case FieldType::Vec4:  ImGui::DragFloat4(label, reinterpret_cast<f32*>(address), 0.05f); break;

	case FieldType::Quat:
	{
		// 편집은 오일러 각(도)으로: x = pitch, y = yaw, z = roll
		quat rotation;
		std::memcpy(&rotation, address, sizeof(rotation));
		const vec3 radians = ToEuler(rotation);
		f32 degrees[3] =
		{
			CleanZero(DirectX::XMConvertToDegrees(radians.x)),
			CleanZero(DirectX::XMConvertToDegrees(radians.y)),
			CleanZero(DirectX::XMConvertToDegrees(radians.z)),
		};
		if (ImGui::DragFloat3(label, degrees, 0.5f))
		{
			rotation = quat::CreateFromYawPitchRoll(
				DirectX::XMConvertToRadians(degrees[1]),
				DirectX::XMConvertToRadians(degrees[0]),
				DirectX::XMConvertToRadians(degrees[2]));
			std::memcpy(address, &rotation, sizeof(rotation));
		}
		break;
	}

	case FieldType::String:
		ImGui::InputText(label, reinterpret_cast<std::string*>(address));
		break;

	case FieldType::ObjectRef:
	{
		ObjectRef& ref = *reinterpret_cast<ObjectRef*>(address);
		const GameObject* target = ref.Get();
		const std::string preview = target != nullptr ? target->GetName() : (ref.IsSet() ? "(missing)" : "(none)");

		if (ImGui::BeginCombo(label, preview.c_str()))
		{
			if (ImGui::Selectable("(none)", !ref.IsSet()))
			{
				ref.Reset();
			}
			if (Scene* scene = _ctx.engine.GetSceneManager().GetActiveScene())
			{
				for (const auto& candidate : scene->GetGameObjects())
				{
					if (candidate == nullptr || candidate->IsPendingDestroy())
					{
						continue;
					}
					ImGui::PushID(candidate.get());
					if (ImGui::Selectable(candidate->GetName().c_str(), ref.id == candidate->GetId()))
					{
						ref = ObjectRef(candidate.get());
					}
					ImGui::PopID();
				}
			}
			ImGui::EndCombo();
		}

		// Hierarchy에서 끌어다 놓기로도 지정
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kObjectPayload))
			{
				ref = ObjectRef(*static_cast<const ObjectId*>(payload->Data));
			}
			ImGui::EndDragDropTarget();
		}
		break;
	}

	case FieldType::MeshHandle:
		if (resources != nullptr)
		{
			auto& handle = *reinterpret_cast<MeshHandle*>(address);
			ResourceCombo(label, handle, resources->ListMeshes(), resources->IsAlive(handle));
		}
		break;

	case FieldType::TextureHandle:
		if (resources != nullptr)
		{
			auto& handle = *reinterpret_cast<TextureHandle*>(address);
			ResourceCombo(label, handle, resources->ListTextures(), resources->IsAlive(handle));
			AcceptAssetDrop(resources, handle); // Project 창에서 끌어다 놓기
		}
		break;

	case FieldType::ShaderHandle:
		if (resources != nullptr)
		{
			auto& handle = *reinterpret_cast<ShaderHandle*>(address);
			ResourceCombo(label, handle, resources->ListShaders(), resources->IsAlive(handle));
			AcceptAssetDrop(resources, handle);
		}
		break;

	case FieldType::MaterialHandle:
		if (resources != nullptr)
		{
			auto& handle = *reinterpret_cast<MaterialHandle*>(address);
			ResourceCombo(label, handle, resources->ListMaterials(), resources->IsAlive(handle));
		}
		break;
	}
}
