#include "pch.h"
#include "Editor.h"

#include "Camera.h"
#include "ComponentRegistry.h"
#include "Engine.h"
#include "RenderResources.h"
#include "Renderer11.h"
#include "GameObject.h"
#include "Log.h"
#include "MeshRenderer.h"
#include "ResourceManager.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Transform.h"

#include "imgui.h"
#include "imgui_internal.h" // DockBuilder (기본 도킹 배치)
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "misc/cpp/imgui_stdlib.h"

#include <cfloat>
#include <cmath>
#include <cstring>
#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
	constexpr const char* kObjectPayload = "D3D12_OBJECT";

	std::string ToUtf8(const std::wstring& wide)
	{
		if (wide.empty())
		{
			return {};
		}
		const int size = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
		std::string result(static_cast<size_t>(size), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), size, nullptr, nullptr);
		return result;
	}

	std::wstring ToWide(const std::string& utf8)
	{
		if (utf8.empty())
		{
			return {};
		}
		const int size = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
		std::wstring result(static_cast<size_t>(size), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), result.data(), size);
		return result;
	}

	std::string GetExeDirectory()
	{
		WCHAR path[MAX_PATH]{};
		::GetModuleFileNameW(nullptr, path, MAX_PATH);
		return ToUtf8(std::filesystem::path(path).parent_path().wstring()) + "\\";
	}

	// 쿼터니언 → (pitch, yaw, roll) 라디안. quat::CreateFromYawPitchRoll의 역변환.
	vec3 ToEuler(const quat& q)
	{
		const f32 xx = q.x * q.x;
		const f32 yy = q.y * q.y;
		const f32 zz = q.z * q.z;

		const f32 m31 = 2.0f * q.x * q.z + 2.0f * q.y * q.w;
		const f32 m32 = 2.0f * q.y * q.z - 2.0f * q.x * q.w;
		const f32 m33 = 1.0f - 2.0f * xx - 2.0f * yy;

		const f32 cy = std::sqrt(m33 * m33 + m31 * m31);
		const f32 cx = std::atan2(-m32, cy);
		if (cy > 16.0f * FLT_EPSILON)
		{
			const f32 m12 = 2.0f * q.x * q.y + 2.0f * q.z * q.w;
			const f32 m22 = 1.0f - 2.0f * xx - 2.0f * zz;
			return vec3(cx, std::atan2(m31, m33), std::atan2(m12, m22));
		}

		const f32 m11 = 1.0f - 2.0f * yy - 2.0f * zz;
		const f32 m21 = 2.0f * q.x * q.y - 2.0f * q.z * q.w;
		return vec3(cx, 0.0f, std::atan2(-m21, m11));
	}

	// 표시용: 아주 작은 값과 -0을 0으로 (회전 변환 오차로 "-0.000"이 보이지 않도록)
	f32 CleanZero(f32 value)
	{
		return std::fabs(value) < 5e-4f ? 0.0f : value;
	}

	// 쿼터니언으로 방향 벡터 회전
	vec3 RotateVector(const vec3& v, const quat& q)
	{
		vec3 result;
		DirectX::XMStoreFloat3(&result, DirectX::XMVector3Rotate(DirectX::XMLoadFloat3(&v), DirectX::XMLoadFloat4(&q)));
		return result;
	}

	// 점 변환 (w로 나눔: 투영 역행렬에도 사용)
	vec3 TransformPoint(const vec3& v, const matx& m)
	{
		vec3 result;
		DirectX::XMStoreFloat3(&result, DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&v), m));
		return result;
	}

	bool IsTextureFile(const std::string& path)
	{
		const std::string ext = std::filesystem::path(ToWide(path)).extension().string();
		std::string lower;
		for (char c : ext)
		{
			lower.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(c))));
		}
		return lower == ".png" || lower == ".jpg" || lower == ".jpeg" || lower == ".bmp" || lower == ".gif" || lower == ".tif" || lower == ".tiff";
	}

	bool IsShaderFile(const std::string& path)
	{
		const std::string ext = std::filesystem::path(ToWide(path)).extension().string();
		return ext == ".hlsl" || ext == ".HLSL";
	}

	// 리소스 핸들 콤보: (none) + 목록. 핸들이 가리키는 리소스가 삭제됐으면 (missing)으로 표시.
	template <typename H>
	bool ResourceCombo(const char* label, H& handle, const std::vector<ResourceEntry<H>>& entries, bool alive)
	{
		std::string preview = "(none)";
		if (handle.IsValid())
		{
			preview = "(missing)";
			if (alive)
			{
				for (const auto& entry : entries)
				{
					if (entry.handle == handle)
					{
						preview = entry.name;
						break;
					}
				}
			}
		}

		bool changed = false;
		if (ImGui::BeginCombo(label, preview.c_str()))
		{
			if (ImGui::Selectable("(none)", !handle.IsValid()))
			{
				handle  = H{};
				changed = true;
			}
			for (const auto& entry : entries)
			{
				ImGui::PushID(static_cast<int>(entry.handle.index));
				if (ImGui::Selectable(entry.name.c_str(), entry.handle == handle))
				{
					handle  = entry.handle;
					changed = true;
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		return changed;
	}

	// 부모를 바꿔도 월드 위치/회전/크기가 유지되도록 로컬 값을 다시 계산한다
	void SetParentKeepWorld(Transform* child, Transform* parent)
	{
		const matx world = child->GetWorldMatrix();
		child->SetParent(parent);
		if (child->GetParent() != parent)
		{
			return; // 순환 등으로 거부됨
		}

		const matx local = parent != nullptr ? world * parent->GetWorldMatrix().Invert() : world;

		vec3 scale;
		quat rotation;
		vec3 position;
		if (matx(local).Decompose(scale, rotation, position))
		{
			child->SetPosition(position);
			child->SetRotation(rotation);
			child->SetScale(scale);
		}
	}
}

Editor::Editor(Engine& engine)
	: _engine(engine)
{
}

Editor::~Editor()
{
	Shutdown();
}

bool Editor::Initialize(HWND hWnd, ID3D11Device* device, ID3D11DeviceContext* context)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

	// 창 배치는 실행 파일 옆에 저장 (작업 디렉터리와 무관하게)
	_iniPath = GetExeDirectory() + "imgui.ini";
	io.IniFilename = _iniPath.c_str();

	ImGui::StyleColorsDark();

	// 한글 이름 표시용 폰트 (없으면 ImGui 기본 폰트)
	WCHAR windowsDir[MAX_PATH]{};
	::GetWindowsDirectoryW(windowsDir, MAX_PATH);
	const std::wstring fontPath = std::wstring(windowsDir) + L"\\Fonts\\malgun.ttf";
	if (::GetFileAttributesW(fontPath.c_str()) != INVALID_FILE_ATTRIBUTES)
	{
		io.Fonts->AddFontFromFileTTF(ToUtf8(fontPath).c_str(), 16.0f);
	}

	if (!ImGui_ImplWin32_Init(hWnd))
	{
		ImGui::DestroyContext();
		return false;
	}

	if (!ImGui_ImplDX11_Init(device, context))
	{
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		return false;
	}

	_device      = device;
	_context     = context;
	_initialized = true;
	EngineLog("[Editor] Initialized (ImGui %s). F1 toggles the editor.", IMGUI_VERSION);
	return true;
}

void Editor::Shutdown()
{
	if (!_initialized)
	{
		return;
	}

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	_initialized = false;
}

bool Editor::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (!_initialized)
	{
		return false;
	}

	if (message == WM_KEYDOWN && wParam == VK_F1 && (lParam & (1 << 30)) == 0)
	{
		_visible = !_visible;
		return true;
	}

	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
	{
		return true;
	}

	if (!_visible)
	{
		return false;
	}

	const ImGuiIO& io = ImGui::GetIO();
	switch (message)
	{
	case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
	case WM_MOUSEWHEEL:  case WM_MOUSEHWHEEL: case WM_MOUSEMOVE:
		return io.WantCaptureMouse;

	// 키보드는 텍스트 입력 중일 때만 가로챈다. (WantCaptureKeyboard는 도킹된 에디터 창에 포커스만 있어도
	//  true라서, 그걸 쓰면 F5 리로드/Esc 종료 같은 호스트 단축키가 항상 막힌다)
	case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_CHAR:
		return io.WantTextInput;

	default:
		return false;
	}
}

void Editor::Render()
{
	if (!_initialized || !_visible)
	{
		return;
	}

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// 가운데는 비워 두어 씬이 보이도록 (패널은 가장자리에 도킹)
	// 창 전체를 도킹 공간으로 쓴다. 씬은 가운데 Scene 창에 그려진다.
	// imgui.ini에 저장된 배치가 없거나(첫 실행) 분할되지 않은 상태면 기본 배치를 만든다.
	const ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");
	const ImGuiDockNode* dockNode = ImGui::DockBuilderGetNode(dockspaceId);
	if (_resetLayout || dockNode == nullptr || !dockNode->IsSplitNode())
	{
		BuildDefaultLayout(dockspaceId);
		_resetLayout = false;
	}
	ImGui::DockSpaceOverViewport(dockspaceId, ImGui::GetMainViewport());

	Scene* scene = _engine.GetSceneManager().GetActiveScene();

	DrawMainMenu(scene);
	DrawHierarchy(scene);
	DrawInspector();
	DrawAssets();
	DrawSceneView();

	if (_showDemo)
	{
		ImGui::ShowDemoWindow(&_showDemo);
	}

	// --- 미뤄 둔 씬 변경 처리 (오브젝트 목록을 순회하는 중에는 바꾸지 않는다) ---
	if (scene != nullptr)
	{
		if (_hasPendingReparent)
		{
			GameObject* child  = _pendingReparentChild.Get();
			GameObject* parent = _pendingReparentParent.Get();
			if (child != nullptr)
			{
				SetParentKeepWorld(child->transform, parent != nullptr ? parent->transform : nullptr);
			}
			_hasPendingReparent = false;
		}

		if (!_pendingCreateKind.empty())
		{
			if (GameObject* created = CreateObject(scene, _pendingCreateKind.c_str(), _pendingCreateParent.Get()))
			{
				_selected = ObjectRef(created);
			}
			_pendingCreateKind.clear();
			_pendingCreateParent.Reset();
		}

		if (GameObject* doomed = _pendingDelete.Get())
		{
			EngineLog("[Editor] Deleted object '%s' (#%llu)", doomed->GetName().c_str(), static_cast<unsigned long long>(doomed->GetId()));
			scene->DestroyGameObject(doomed);
		}
		_pendingDelete.Reset();
	}

	// 씬 교체는 다른 변경을 다 처리한 뒤 마지막에 (이 뒤로는 scene 포인터가 무효)
	if (_pendingNew)
	{
		_engine.NewScene("Untitled");
		_selected.Reset();
		_pendingNew = false;
	}
	if (!_pendingLoad.empty())
	{
		if (_engine.LoadScene(_pendingLoad))
		{
			_selected.Reset();
		}
		_pendingLoad.clear();
	}

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void Editor::DrawMainMenu(Scene* scene)
{
	// Ctrl+S: 창 포커스와 무관하게 저장
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
	{
		SaveCurrentScene();
	}

	if (!ImGui::BeginMainMenuBar())
	{
		return;
	}

	DrawFileMenu();

	if (ImGui::BeginMenu("Create"))
	{
		GameObject* parent = _selected.Get();
		if (ImGui::MenuItem("Empty"))  { _pendingCreateKind = "Empty"; }
		if (ImGui::MenuItem("Quad"))   { _pendingCreateKind = "Quad"; }
		if (ImGui::MenuItem("Camera")) { _pendingCreateKind = "Camera"; }
		ImGui::Separator();
		if (ImGui::MenuItem("Empty Child", nullptr, false, parent != nullptr))
		{
			_pendingCreateKind   = "Empty";
			_pendingCreateParent = ObjectRef(parent);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Scene"))
	{
		bool paused = _engine.IsScenePaused();
		if (ImGui::MenuItem("Pause scene update", "", &paused))
		{
			_engine.SetScenePaused(paused);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("View"))
	{
		if (ImGui::MenuItem("Reset Layout"))
		{
			_resetLayout = true;
		}
		ImGui::MenuItem("ImGui demo", nullptr, &_showDemo);
		ImGui::EndMenu();
	}

	const f32 dt = _engine.GetDeltaTime();
	ImGui::Separator();
	ImGui::Text("%.1f ms (%.0f FPS)", dt * 1000.0f, dt > 0.0f ? 1.0f / dt : 0.0f);
	if (_engine.IsScenePaused())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "PAUSED");
	}
	const std::string& scenePath = _engine.GetScenePath();
	ImGui::TextDisabled("| %s (%s) | F1: hide editor",
		scene != nullptr ? scene->GetName().c_str() : "(no scene)",
		scenePath.empty() ? "unsaved" : scenePath.c_str());

	ImGui::EndMainMenuBar();

	DrawSaveAsPopup();
}

void Editor::DrawHierarchy(Scene* scene)
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

	if (ImGui::Button("+ Empty")) { _pendingCreateKind = "Empty"; }
	ImGui::SameLine();
	if (ImGui::Button("+ Quad"))  { _pendingCreateKind = "Quad"; }
	ImGui::SameLine();
	ImGui::BeginDisabled(_selected.Get() == nullptr);
	if (ImGui::Button("Delete"))  { _pendingDelete = _selected; }
	ImGui::EndDisabled();
	ImGui::Separator();

	for (const auto& object : scene->GetGameObjects())
	{
		if (object != nullptr && !object->IsPendingDestroy() && object->transform != nullptr && object->transform->GetParent() == nullptr)
		{
			DrawHierarchyNode(object.get());
		}
	}

	// 빈 영역에 놓으면 루트로 (부모 해제)
	ImVec2 remaining = ImGui::GetContentRegionAvail();
	remaining.y = remaining.y < 20.0f ? 20.0f : remaining.y;
	ImGui::InvisibleButton("##HierarchyBackground", remaining);
	if (ImGui::IsItemClicked())
	{
		_selected.Reset();
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kObjectPayload))
		{
			_pendingReparentChild  = ObjectRef(*static_cast<const ObjectId*>(payload->Data));
			_pendingReparentParent.Reset();
			_hasPendingReparent    = true;
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete) && _selected.Get() != nullptr)
	{
		_pendingDelete = _selected;
	}

	ImGui::End();
}

void Editor::DrawHierarchyNode(GameObject* object)
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
	if (_selected.id == id)
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
		_selected = ObjectRef(object);
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
			_pendingReparentChild  = ObjectRef(*static_cast<const ObjectId*>(payload->Data));
			_pendingReparentParent = ObjectRef(object);
			_hasPendingReparent    = true;
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::BeginPopupContextItem())
	{
		_selected = ObjectRef(object);
		if (ImGui::MenuItem("Create Empty Child"))
		{
			_pendingCreateKind   = "Empty";
			_pendingCreateParent = ObjectRef(object);
		}
		if (ImGui::MenuItem("Unparent", nullptr, false, transform->GetParent() != nullptr))
		{
			_pendingReparentChild = ObjectRef(object);
			_pendingReparentParent.Reset();
			_hasPendingReparent   = true;
		}
		if (ImGui::MenuItem("Delete"))
		{
			_pendingDelete = ObjectRef(object);
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
				DrawHierarchyNode(child->GetOwner());
			}
		}
		ImGui::TreePop();
	}
}

void Editor::DrawInspector()
{
	if (!ImGui::Begin("Inspector"))
	{
		ImGui::End();
		return;
	}

	GameObject* object = _selected.Get();
	if (object == nullptr)
	{
		_selected.Reset();
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
		for (const ComponentTypeInfo* type : _engine.GetComponentRegistry().GetTypes())
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

void Editor::DrawComponent(GameObject* object, Component* component, Component*& removeRequest)
{
	ImGui::PushID(component);

	const bool isTransform = component == object->transform;
	const ComponentTypeInfo* info = _engine.GetComponentRegistry().Find(component->GetTypeId());

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

void Editor::DrawField(Component* component, const FieldInfo& field)
{
	u8* address = reinterpret_cast<u8*>(component) + field.offset;
	const char* label = field.name.c_str();
	ResourceManager* resources = _engine.GetResourceManager();

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
			if (Scene* scene = _engine.GetSceneManager().GetActiveScene())
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
		}
		break;

	case FieldType::ShaderHandle:
		if (resources != nullptr)
		{
			auto& handle = *reinterpret_cast<ShaderHandle*>(address);
			ResourceCombo(label, handle, resources->ListShaders(), resources->IsAlive(handle));
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

GameObject* Editor::CreateObject(Scene* scene, const char* kind, GameObject* parent)
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
		ResourceManager* resources = _engine.GetResourceManager();
		if (resources != nullptr)
		{
			// 선택된 머티리얼 → 없으면 첫 번째 머티리얼
			MaterialHandle material = resources->IsAlive(_selectedMaterial) ? _selectedMaterial : MaterialHandle{};
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

void Editor::DrawAssets()
{
	if (!ImGui::Begin("Assets"))
	{
		ImGui::End();
		return;
	}

	ResourceManager* resources = _engine.GetResourceManager();
	if (resources == nullptr)
	{
		ImGui::TextDisabled("No resource manager (D3D11 only)");
		ImGui::End();
		return;
	}

	bool watch = resources->IsFileWatchEnabled();
	if (ImGui::Checkbox("Hot reload on file change", &watch))
	{
		resources->SetFileWatchEnabled(watch);
	}

	const ResourceManager::Stats stats = resources->GetStats();
	ImGui::TextDisabled("meshes %u  textures %u  shaders %u  materials %u", stats.meshes, stats.textures, stats.shaders, stats.materials);

	if (ImGui::BeginTabBar("AssetTabs"))
	{
		if (ImGui::BeginTabItem("Files"))
		{
			DrawAssetFiles();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Materials"))
		{
			DrawMaterials();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Meshes"))
		{
			for (const auto& mesh : resources->ListMeshes())
			{
				ImGui::BulletText("%s", mesh.name.c_str());
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
}

void Editor::ScanAssetFiles()
{
	_assetFiles.clear();
	_lastAssetScan = ::GetTickCount64();

	ResourceManager* resources = _engine.GetResourceManager();
	if (resources == nullptr || resources->GetAssetRoot().empty())
	{
		return;
	}

	const std::filesystem::path root = ToWide(resources->GetAssetRoot());
	std::error_code ec;
	for (auto it = std::filesystem::recursive_directory_iterator(root, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
	{
		if (!it->is_regular_file(ec))
		{
			continue;
		}

		std::wstring relative = it->path().lexically_relative(root).wstring();
		for (wchar_t& c : relative)
		{
			c = c == L'\\' ? L'/' : c;
		}

		const std::string path = ToUtf8(relative);
		if (IsTextureFile(path) || IsShaderFile(path))
		{
			_assetFiles.push_back(path);
		}
	}
	std::sort(_assetFiles.begin(), _assetFiles.end());
}

void Editor::DrawAssetFiles()
{
	ResourceManager* resources = _engine.GetResourceManager();

	// 새 파일을 반영하도록 2초마다 다시 스캔
	if (_lastAssetScan == 0 || ::GetTickCount64() - _lastAssetScan > 2000)
	{
		ScanAssetFiles();
	}

	if (!ImGui::BeginTable("AssetFiles", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
	{
		return;
	}

	ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 70.0f);
	ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 130.0f);
	ImGui::TableHeadersRow();

	for (const std::string& path : _assetFiles)
	{
		ImGui::PushID(path.c_str());
		ImGui::TableNextRow();

		const bool isTexture = IsTextureFile(path);
		const TextureHandle texture = isTexture ? resources->FindTexture(path) : TextureHandle{};
		const ShaderHandle  shader  = isTexture ? ShaderHandle{} : resources->FindShader(path);
		const bool loaded = isTexture ? resources->IsAlive(texture) : resources->IsAlive(shader);

		ImGui::TableNextColumn();
		ImGui::TextUnformatted(path.c_str());

		ImGui::TableNextColumn();
		if (loaded)
		{
			ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "loaded");
		}
		else
		{
			ImGui::TextDisabled("-");
		}

		ImGui::TableNextColumn();
		if (!loaded)
		{
			if (ImGui::SmallButton("Load"))
			{
				if (isTexture) { resources->LoadTexture(path); }
				else           { resources->LoadShader(path); }
			}
		}
		else
		{
			if (ImGui::SmallButton("Reload"))
			{
				if (isTexture) { resources->ReloadTexture(texture); }
				else           { resources->ReloadShader(shader); }
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Unload"))
			{
				// 이 리소스를 쓰는 머티리얼/렌더러의 핸들은 무효가 된다 (크래시 없이 표시만 사라짐)
				if (isTexture) { resources->Destroy(texture); }
				else           { resources->Destroy(shader); }
			}
		}

		ImGui::PopID();
	}

	ImGui::EndTable();
}

void Editor::DrawMaterials()
{
	ResourceManager* resources = _engine.GetResourceManager();

	ImGui::SetNextItemWidth(-70.0f);
	ImGui::InputText("##newMaterial", &_newMaterialName);
	ImGui::SameLine();
	ImGui::BeginDisabled(_newMaterialName.empty() || resources->FindMaterial(_newMaterialName).IsValid());
	if (ImGui::Button("Create"))
	{
		const auto shaders = resources->ListShaders();
		_selectedMaterial = resources->CreateMaterial(_newMaterialName, shaders.empty() ? ShaderHandle{} : shaders.front().handle, TextureHandle{});
		EngineLog("[Editor] Created material '%s'", _newMaterialName.c_str());
	}
	ImGui::EndDisabled();
	ImGui::Separator();

	const auto materials = resources->ListMaterials();

	ImGui::BeginChild("MaterialList", ImVec2(140.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
	for (const auto& material : materials)
	{
		ImGui::PushID(static_cast<int>(material.handle.index));
		if (ImGui::Selectable(material.name.c_str(), material.handle == _selectedMaterial))
		{
			_selectedMaterial = material.handle;
		}
		ImGui::PopID();
	}
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("MaterialDetails");
	if (!resources->IsAlive(_selectedMaterial))
	{
		_selectedMaterial = {};
		ImGui::TextDisabled("Select a material");
	}
	else
	{
		ShaderHandle shader = resources->GetMaterialShader(_selectedMaterial);
		if (ResourceCombo("Shader", shader, resources->ListShaders(), resources->IsAlive(shader)))
		{
			resources->SetMaterialShader(_selectedMaterial, shader);
		}

		TextureHandle texture = resources->GetMaterialTexture(_selectedMaterial);
		if (ResourceCombo("Texture", texture, resources->ListTextures(), resources->IsAlive(texture)))
		{
			resources->SetMaterialTexture(_selectedMaterial, texture);
		}

		ImGui::Spacing();
		if (ImGui::Button("Delete material"))
		{
			resources->Destroy(_selectedMaterial);
			_selectedMaterial = {};
		}
	}
	ImGui::EndChild();
}

std::vector<std::string> Editor::FindSceneFiles() const
{
	std::vector<std::string> files;

	ResourceManager* resources = _engine.GetResourceManager();
	if (resources == nullptr || resources->GetAssetRoot().empty())
	{
		return files;
	}

	const std::filesystem::path root = ToWide(resources->GetAssetRoot());
	std::error_code ec;
	for (auto it = std::filesystem::recursive_directory_iterator(root, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
	{
		if (it->is_regular_file(ec) && it->path().extension() == L".scene")
		{
			std::wstring relative = it->path().lexically_relative(root).wstring();
			for (wchar_t& c : relative)
			{
				c = c == L'\\' ? L'/' : c;
			}
			files.push_back(ToUtf8(relative));
		}
	}
	std::sort(files.begin(), files.end());
	return files;
}

void Editor::SaveCurrentScene()
{
	const std::string& path = _engine.GetScenePath();
	if (path.empty())
	{
		_openSaveAs = true; // 아직 파일이 없으면 다른 이름으로 저장
		return;
	}
	_engine.SaveScene(path);
}

void Editor::DrawFileMenu()
{
	if (!ImGui::BeginMenu("File"))
	{
		return;
	}

	if (ImGui::MenuItem("New Scene"))
	{
		_pendingNew = true;
	}

	if (ImGui::BeginMenu("Open Scene"))
	{
		const std::vector<std::string> files = FindSceneFiles();
		if (files.empty())
		{
			ImGui::TextDisabled("No .scene files in Assets");
		}
		for (const std::string& file : files)
		{
			if (ImGui::MenuItem(file.c_str(), nullptr, file == _engine.GetScenePath()))
			{
				_pendingLoad = file;
			}
		}
		ImGui::EndMenu();
	}

	ImGui::Separator();

	if (ImGui::MenuItem("Save", "Ctrl+S"))
	{
		SaveCurrentScene();
	}
	if (ImGui::MenuItem("Save As..."))
	{
		_openSaveAs = true;
	}

	ImGui::Separator();
	if (ImGui::MenuItem("Reload From Disk", nullptr, false, !_engine.GetScenePath().empty()))
	{
		_pendingLoad = _engine.GetScenePath();
	}

	ImGui::EndMenu();
}

void Editor::DrawSaveAsPopup()
{
	if (_openSaveAs)
	{
		if (!_engine.GetScenePath().empty())
		{
			_saveAsPath = _engine.GetScenePath();
		}
		ImGui::OpenPopup("Save Scene As");
		_openSaveAs = false;
	}

	if (!ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		return;
	}

	ImGui::TextUnformatted("Path (relative to Assets):");
	ImGui::SetNextItemWidth(320.0f);
	if (ImGui::IsWindowAppearing())
	{
		ImGui::SetKeyboardFocusHere();
	}
	const bool submitted = ImGui::InputText("##scenePath", &_saveAsPath, ImGuiInputTextFlags_EnterReturnsTrue);

	std::string path = _saveAsPath;
	if (path.size() < 6 || path.compare(path.size() - 6, 6, ".scene") != 0)
	{
		path += ".scene";
	}

	if (_engine.SceneFileExists(path) && path != _engine.GetScenePath())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s exists and will be overwritten.", path.c_str());
	}

	const bool valid = !_saveAsPath.empty() && path.find("..") == std::string::npos && path.find(':') == std::string::npos;
	ImGui::BeginDisabled(!valid);
	if (ImGui::Button("Save", ImVec2(120.0f, 0.0f)) || (submitted && valid))
	{
		_engine.SaveScene(path);
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
	{
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void Editor::BuildDefaultLayout(unsigned int dockspaceId)
{
	// ┌──────────┬────────────────┬───────────┐
	// │Hierarchy │     Scene      │ Inspector │
	// │          ├────────────────┤           │
	// │          │     Assets     │           │
	// └──────────┴────────────────┴───────────┘
	const ImGuiViewport* viewport = ImGui::GetMainViewport();

	ImGui::DockBuilderRemoveNode(dockspaceId);
	ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

	ImGuiID center = dockspaceId;
	const ImGuiID left   = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
	const ImGuiID right  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
	const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);

	ImGui::DockBuilderDockWindow("Hierarchy", left);
	ImGui::DockBuilderDockWindow("Inspector", right);
	ImGui::DockBuilderDockWindow("Assets", bottom);
	ImGui::DockBuilderDockWindow("Scene", center);
	ImGui::DockBuilderFinish(dockspaceId);
}

bool Editor::BeginSceneView(const f32 clearColor[4])
{
	if (!_initialized || !_visible || _sceneViewWidth == 0 || _sceneViewHeight == 0)
	{
		return false;
	}

	// 크기가 바뀐 경우에만 다시 만든다. 이전 텍스처는 지난 프레임 ImGui 드로우가 이미 끝났으므로 바로 해제해도 된다.
	if (!_sceneTexture.Resize(_device, _sceneViewWidth, _sceneViewHeight))
	{
		return false;
	}

	_sceneTexture.Begin(_context, clearColor);
	return true;
}

void Editor::DrawSceneView()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool open = ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();

	if (!open)
	{
		// 접혀 있거나 다른 탭에 가려져 있으면 다음 프레임에 씬을 그리지 않는다
		_sceneViewWidth   = 0;
		_sceneViewHeight  = 0;
		_sceneViewHovered = false;
		_navigating       = false;
		ImGui::End();
		return;
	}

	DrawSceneToolbar();

	const ImVec2 available = ImGui::GetContentRegionAvail();
	_sceneViewWidth  = available.x >= 1.0f ? static_cast<u32>(available.x) : 0;
	_sceneViewHeight = available.y >= 1.0f ? static_cast<u32>(available.y) : 0;

	// 이번 프레임에 그린 텍스처를 표시 (크기가 바뀐 직후 한 프레임은 이전 크기라 늘려서 보여준다)
	if (ID3D11ShaderResourceView* view = _sceneTexture.GetShaderResourceView())
	{
		ImGui::Image(reinterpret_cast<ImTextureID>(view), available);
	}
	else
	{
		ImGui::Dummy(available);
	}

	const ImVec2 imageMin = ImGui::GetItemRectMin();
	const ImVec2 imageMax = ImGui::GetItemRectMax();
	_sceneRect[0] = imageMin.x;
	_sceneRect[1] = imageMin.y;
	_sceneRect[2] = imageMax.x;
	_sceneRect[3] = imageMax.y;

	_sceneViewHovered = ImGui::IsItemHovered();
	UpdateSceneCamera(_sceneViewHovered);

	// 외곽선을 먼저 그리고 기즈모를 그 위에 그린다
	DrawSelectionOutline();
	UpdateGizmo(_sceneViewHovered);

	// 왼쪽 클릭(드래그 아님)으로 오브젝트 선택. 기즈모를 누른 클릭은 제외.
	const ImGuiIO& io = ImGui::GetIO();
	if (_sceneViewHovered && !_navigating && !_gizmoConsumedClick && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
		if (drag.x * drag.x + drag.y * drag.y < 16.0f)
		{
			PickObject(io.MousePos.x, io.MousePos.y);
		}
	}
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		_gizmoConsumedClick = false;
	}

	ImGui::End();
}

void Editor::DrawSceneToolbar()
{
	ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 6.0f, ImGui::GetCursorPosY() + 3.0f));

	// 항목 묶음 단위로 배치: 한 줄에 다 들어가지 않으면 다음 줄로 (Scene 창이 좁을 때 잘리지 않도록)
	const ImGuiStyle& style = ImGui::GetStyle();
	auto radioWidth  = [&](const char* text) { return ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(text).x; };
	auto buttonWidth = [&](const char* text) { return ImGui::CalcTextSize(text).x + style.FramePadding.x * 2.0f; };
	auto labelWidth  = [&](const char* text) { return style.ItemInnerSpacing.x + ImGui::CalcTextSize(text).x; };
	auto placeGroup  = [&](f32 width)
	{
		const f32 right = ImGui::GetItemRectMax().x + style.ItemSpacing.x + width;
		if (right <= ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 6.0f)
		{
			ImGui::SameLine();
		}
		else
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6.0f);
		}
	};

	// 카메라 모드
	int mode = _useGameCamera ? 1 : 0;
	if (ImGui::RadioButton("Editor camera", &mode, 0))
	{
		_useGameCamera = false;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Game camera", &mode, 1))
	{
		_useGameCamera = true;
	}

	// 에디터 카메라 설정
	placeGroup(90.0f + labelWidth("Speed") + style.ItemSpacing.x + 70.0f + labelWidth("FOV"));
	ImGui::BeginDisabled(_useGameCamera);
	ImGui::SetNextItemWidth(90.0f);
	ImGui::DragFloat("Speed", &_cameraSpeed, 0.1f, 0.1f, 200.0f, "%.1f");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(70.0f);
	ImGui::DragFloat("FOV", &_cameraFovY, 0.5f, 10.0f, 120.0f, "%.0f");
	ImGui::EndDisabled();

	// 기즈모
	placeGroup(radioWidth("Move (W)") + style.ItemSpacing.x + radioWidth("Rotate (E)") + style.ItemSpacing.x + buttonWidth("World"));
	int gizmo = _gizmoMode == GizmoMode::Translate ? 0 : 1;
	if (ImGui::RadioButton("Move (W)", &gizmo, 0))   { _gizmoMode = GizmoMode::Translate; }
	ImGui::SameLine();
	if (ImGui::RadioButton("Rotate (E)", &gizmo, 1)) { _gizmoMode = GizmoMode::Rotate; }
	ImGui::SameLine();
	if (ImGui::Button(_gizmoLocal ? "Local" : "World"))
	{
		_gizmoLocal = !_gizmoLocal;
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Gizmo axes: object (Local) or world (World)");
	}

	// 해상도 + 도움말
	char resolution[48];
	std::snprintf(resolution, sizeof(resolution), "%u x %u  (?)", _sceneTexture.GetWidth(), _sceneTexture.GetHeight());
	placeGroup(ImGui::CalcTextSize(resolution).x);
	ImGui::TextDisabled("%s", resolution);
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip(
			"Left click     : select object\n"
			"Right drag     : look around (+ W/A/S/D move, Q/E down/up, Shift faster)\n"
			"Middle drag    : pan\n"
			"Wheel          : zoom (while right dragging: change speed)\n"
			"F              : focus selected object\n"
			"W / E          : move / rotate gizmo\n"
			"Gizmo drag     : hold Ctrl to snap (0.25 units / 15 degrees)");
	}

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
}

vec3 Editor::GetEditorCameraForward() const
{
	const quat rotation = quat::CreateFromYawPitchRoll(_cameraYaw, _cameraPitch, 0.0f);
	return RotateVector(vec3(0.0f, 0.0f, 1.0f), rotation);
}

bool Editor::GetSceneViewCamera(matx& view, matx& projection) const
{
	if (_useGameCamera || _sceneTexture.GetWidth() == 0 || _sceneTexture.GetHeight() == 0)
	{
		return false;
	}

	// 카메라 월드 행렬(회전 → 이동)의 역행렬이 뷰 행렬. 왼손 좌표계(+Z 앞).
	const quat rotation = quat::CreateFromYawPitchRoll(_cameraYaw, _cameraPitch, 0.0f);
	const matx world = matx::CreateFromQuaternion(rotation) * matx::CreateTranslation(_cameraPosition);
	view = world.Invert();

	const f32 aspect = static_cast<f32>(_sceneTexture.GetWidth()) / static_cast<f32>(_sceneTexture.GetHeight());
	projection = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(_cameraFovY), aspect, 0.05f, 2000.0f);
	return true;
}

void Editor::GetDisplayedCamera(matx& view, matx& projection) const
{
	if (GetSceneViewCamera(view, projection))
	{
		return;
	}

	const f32 width  = static_cast<f32>(_sceneTexture.GetWidth() > 0 ? _sceneTexture.GetWidth() : 1);
	const f32 height = static_cast<f32>(_sceneTexture.GetHeight() > 0 ? _sceneTexture.GetHeight() : 1);
	const Renderer11::CameraMatrices camera = Renderer11::ResolveSceneCamera(_engine.GetSceneManager().GetActiveScene(), width / height);
	view       = camera.view;
	projection = camera.projection;
}

void Editor::UpdateSceneCamera(bool hovered)
{
	const ImGuiIO& io = ImGui::GetIO();

	// 드래그는 Scene 창 위에서 시작했을 때만. 시작한 뒤에는 창 밖으로 나가도 계속.
	if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
	{
		_navigating = true;
		ImGui::SetWindowFocus(); // 키 입력(WASD, F)이 Scene 창으로 오도록
	}
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
	{
		_navigating = false;
	}

	// 기즈모 모드 전환 (오른쪽 드래그 중의 W는 카메라 이동이므로 제외)
	if (!_navigating && (hovered || ImGui::IsWindowFocused()) && !io.WantTextInput && _gizmoActive < 0)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) { _gizmoMode = GizmoMode::Translate; }
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) { _gizmoMode = GizmoMode::Rotate; }
	}

	if (_useGameCamera)
	{
		return; // 게임 카메라는 씬의 Camera 컴포넌트가 결정한다 (Inspector에서 편집)
	}

	const quat rotation = quat::CreateFromYawPitchRoll(_cameraYaw, _cameraPitch, 0.0f);
	const vec3 forward  = RotateVector(vec3(0.0f, 0.0f, 1.0f), rotation);
	const vec3 right    = RotateVector(vec3(1.0f, 0.0f, 0.0f), rotation);
	const vec3 up       = RotateVector(vec3(0.0f, 1.0f, 0.0f), rotation);

	const f32 dt = io.DeltaTime;

	if (_navigating && ImGui::IsMouseDown(ImGuiMouseButton_Right))
	{
		// 둘러보기: 마우스 이동량 → 회전 (화면 픽셀당 0.25도)
		constexpr f32 kLookSensitivity = DirectX::XMConvertToRadians(0.25f);
		_cameraYaw   += io.MouseDelta.x * kLookSensitivity;
		_cameraPitch += io.MouseDelta.y * kLookSensitivity;

		constexpr f32 kPitchLimit = DirectX::XMConvertToRadians(89.0f);
		_cameraPitch = _cameraPitch < -kPitchLimit ? -kPitchLimit : (_cameraPitch > kPitchLimit ? kPitchLimit : _cameraPitch);

		// 날기: WASD + QE
		vec3 move(0.0f, 0.0f, 0.0f);
		if (ImGui::IsKeyDown(ImGuiKey_W)) move += forward;
		if (ImGui::IsKeyDown(ImGuiKey_S)) move -= forward;
		if (ImGui::IsKeyDown(ImGuiKey_D)) move += right;
		if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right;
		if (ImGui::IsKeyDown(ImGuiKey_E)) move += vec3(0.0f, 1.0f, 0.0f);
		if (ImGui::IsKeyDown(ImGuiKey_Q)) move -= vec3(0.0f, 1.0f, 0.0f);

		const f32 boost = io.KeyShift ? 3.0f : 1.0f;
		if (move.LengthSquared() > 0.0f)
		{
			move.Normalize();
			_cameraPosition += move * (_cameraSpeed * boost * dt);
		}

		// 오른쪽 드래그 중 휠: 이동 속도 조절
		if (io.MouseWheel != 0.0f)
		{
			_cameraSpeed *= io.MouseWheel > 0.0f ? 1.2f : 1.0f / 1.2f;
			_cameraSpeed = _cameraSpeed < 0.1f ? 0.1f : (_cameraSpeed > 200.0f ? 200.0f : _cameraSpeed);
		}
	}
	else if (_navigating && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
	{
		// 이동(팬): 화면 픽셀 → 월드 거리. 속도 설정에 비례.
		const f32 panScale = _cameraSpeed * 0.0025f;
		_cameraPosition -= right * (io.MouseDelta.x * panScale);
		_cameraPosition += up * (io.MouseDelta.y * panScale);
	}
	else if (hovered && io.MouseWheel != 0.0f)
	{
		// 줌: 바라보는 방향으로 이동
		_cameraPosition += forward * (io.MouseWheel * _cameraSpeed * 0.15f);
	}

	// F: 선택한 오브젝트로 이동 (그 오브젝트를 화면 가운데, 일정 거리에서 보도록)
	if ((hovered || ImGui::IsWindowFocused()) && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false))
	{
		if (const GameObject* selected = _selected.Get())
		{
			constexpr f32 kFocusDistance = 3.0f;
			_cameraPosition = selected->transform->GetWorldPosition() - forward * kFocusDistance;
		}
	}
}

namespace
{
	// 선분(origin → origin + direction, t ∈ [0, 1])과 삼각형 교차 (Möller–Trumbore, 양면).
	bool IntersectTriangle(const vec3& origin, const vec3& direction, const vec3& a, const vec3& b, const vec3& c, f32& t)
	{
		const vec3 edge1 = b - a;
		const vec3 edge2 = c - a;
		const vec3 p = direction.Cross(edge2);
		const f32 det = edge1.Dot(p);
		if (std::fabs(det) < 1e-12f)
		{
			return false; // 평행
		}

		const f32 inverse = 1.0f / det;
		const vec3 s = origin - a;
		const f32 u = s.Dot(p) * inverse;
		if (u < 0.0f || u > 1.0f)
		{
			return false;
		}

		const vec3 q = s.Cross(edge1);
		const f32 v = direction.Dot(q) * inverse;
		if (v < 0.0f || u + v > 1.0f)
		{
			return false;
		}

		t = edge2.Dot(q) * inverse;
		return t >= 0.0f && t <= 1.0f;
	}
}

void Editor::PickObject(f32 mouseX, f32 mouseY)
{
	Scene* scene = _engine.GetSceneManager().GetActiveScene();
	ResourceManager* resources = _engine.GetResourceManager();
	const f32 width  = _sceneRect[2] - _sceneRect[0];
	const f32 height = _sceneRect[3] - _sceneRect[1];
	if (scene == nullptr || resources == nullptr || width <= 0.0f || height <= 0.0f)
	{
		return;
	}

	// 마우스 → 월드 공간의 가까운 평면 위 점 → 먼 평면 위 점 (선분)
	vec3 nearWorld;
	vec3 segment;
	if (!GetMouseRay(mouseX, mouseY, nearWorld, segment))
	{
		return;
	}
	const vec3 farWorld = nearWorld + segment;

	GameObject* best = nullptr;
	f32 bestT = 2.0f;

	for (const auto& object : scene->GetGameObjects())
	{
		if (object == nullptr || !object->IsActive() || object->IsPendingDestroy() || object->transform == nullptr)
		{
			continue;
		}

		const MeshRenderer* renderer = object->GetComponent<MeshRenderer>();
		const Mesh* mesh = renderer != nullptr && renderer->IsEnabled() ? resources->Get(renderer->GetMesh()) : nullptr;
		if (mesh == nullptr || mesh->positions.empty())
		{
			continue;
		}

		// 선분을 오브젝트 로컬 공간으로 옮겨 검사한다. 아핀 변환이라 선분 매개변수 t는 월드와 같다.
		const matx inverseWorld = object->transform->GetWorldMatrix().Invert();
		const vec3 origin    = TransformPoint(nearWorld, inverseWorld);
		const vec3 direction = TransformPoint(farWorld, inverseWorld) - origin;

		for (size_t i = 0; i + 2 < mesh->indices.size(); i += 3)
		{
			f32 t = 0.0f;
			if (IntersectTriangle(origin, direction,
					mesh->positions[mesh->indices[i]], mesh->positions[mesh->indices[i + 1]], mesh->positions[mesh->indices[i + 2]], t)
				&& t < bestT)
			{
				bestT = t;
				best  = object.get();
			}
		}
	}

	// 빈 곳을 클릭하면 선택 해제
	_selected = best != nullptr ? ObjectRef(best) : ObjectRef();
}

void Editor::DrawSelectionOutline()
{
	GameObject* selected = _selected.Get();
	ResourceManager* resources = _engine.GetResourceManager();
	if (selected == nullptr || resources == nullptr || selected->transform == nullptr)
	{
		return;
	}

	matx view;
	matx projection;
	GetDisplayedCamera(view, projection);
	const matx worldViewProjection = selected->transform->GetWorldMatrix() * view * projection;

	const f32 width  = _sceneRect[2] - _sceneRect[0];
	const f32 height = _sceneRect[3] - _sceneRect[1];
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRect(ImVec2(_sceneRect[0], _sceneRect[1]), ImVec2(_sceneRect[2], _sceneRect[3]), true);

	// 로컬 점 → 화면 좌표. 카메라 뒤면 false.
	auto project = [&](const vec3& local, ImVec2& out) -> bool
	{
		const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(DirectX::XMVectorSet(local.x, local.y, local.z, 1.0f), worldViewProjection);
		const f32 w = DirectX::XMVectorGetW(clip);
		if (w <= 1e-4f)
		{
			return false;
		}
		const f32 x = DirectX::XMVectorGetX(clip) / w;
		const f32 y = DirectX::XMVectorGetY(clip) / w;
		out = ImVec2(_sceneRect[0] + (x * 0.5f + 0.5f) * width, _sceneRect[1] + (0.5f - y * 0.5f) * height);
		return true;
	};

	const ImU32 color = IM_COL32(255, 160, 40, 255);

	const MeshRenderer* renderer = selected->GetComponent<MeshRenderer>();
	const Mesh* mesh = renderer != nullptr ? resources->Get(renderer->GetMesh()) : nullptr;
	if (mesh != nullptr)
	{
		// 로컬 경계 상자의 12개 모서리 (평면 메시는 사각형이 된다)
		const vec3& lo = mesh->boundsMin;
		const vec3& hi = mesh->boundsMax;
		const vec3 corners[8] =
		{
			{ lo.x, lo.y, lo.z }, { hi.x, lo.y, lo.z }, { hi.x, hi.y, lo.z }, { lo.x, hi.y, lo.z },
			{ lo.x, lo.y, hi.z }, { hi.x, lo.y, hi.z }, { hi.x, hi.y, hi.z }, { lo.x, hi.y, hi.z },
		};
		constexpr int edges[12][2] = { {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7} };

		ImVec2 screen[8];
		bool visible[8];
		for (int i = 0; i < 8; ++i)
		{
			visible[i] = project(corners[i], screen[i]);
		}
		for (const auto& edge : edges)
		{
			if (visible[edge[0]] && visible[edge[1]])
			{
				drawList->AddLine(screen[edge[0]], screen[edge[1]], color, 2.0f);
			}
		}
	}
	else
	{
		// 메시가 없는 오브젝트(빈 오브젝트, 카메라 등)는 위치에 십자 표시
		ImVec2 center;
		if (project(vec3(0.0f, 0.0f, 0.0f), center))
		{
			drawList->AddLine(ImVec2(center.x - 8.0f, center.y), ImVec2(center.x + 8.0f, center.y), color, 2.0f);
			drawList->AddLine(ImVec2(center.x, center.y - 8.0f), ImVec2(center.x, center.y + 8.0f), color, 2.0f);
			drawList->AddCircle(center, 6.0f, color, 0, 1.5f);
		}
	}

	drawList->PopClipRect();
}

void Editor::DrawObjectDetails(GameObject* object)
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
				_selected = ObjectRef(parent->GetOwner());
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

void Editor::DrawComponentDetails(Component* component)
{
	ResourceManager* resources = _engine.GetResourceManager();

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
		const Scene* scene = _engine.GetSceneManager().GetActiveScene();
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
		ImGui::TextDisabled(isActiveCamera ? "Active game camera (used by Game camera view / F1)" : "Not the active game camera");
	}
}

// =================================================================================================
// 기즈모
// =================================================================================================
namespace
{
	// 직선 L(t) = point + t * axis 위에서 광선 R(s) = origin + s * direction 과 가장 가까운 점의 t.
	// 두 직선이 거의 평행이면 false.
	bool ClosestParamOnLine(const vec3& point, const vec3& axis, const vec3& origin, const vec3& direction, f32& t)
	{
		const vec3 w0 = point - origin;
		const f32 a = axis.Dot(axis);
		const f32 b = axis.Dot(direction);
		const f32 c = direction.Dot(direction);
		const f32 d = axis.Dot(w0);
		const f32 e = direction.Dot(w0);
		const f32 denominator = a * c - b * b;
		if (std::fabs(denominator) < 1e-9f * a * c)
		{
			return false;
		}
		t = (b * e - c * d) / denominator;
		return true;
	}

	// 광선과 평면(점 point, 법선 normal)의 교점. 평행하거나 카메라 뒤면 false.
	bool IntersectRayPlane(const vec3& origin, const vec3& direction, const vec3& point, const vec3& normal, vec3& hit)
	{
		const f32 denominator = direction.Dot(normal);
		if (std::fabs(denominator) < 1e-6f * direction.Length())
		{
			return false;
		}
		const f32 s = (point - origin).Dot(normal) / denominator;
		if (s < 0.0f)
		{
			return false;
		}
		hit = origin + direction * s;
		return true;
	}

	f32 DistanceToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b)
	{
		const f32 abx = b.x - a.x, aby = b.y - a.y;
		const f32 lengthSquared = abx * abx + aby * aby;
		f32 t = lengthSquared > 0.0f ? ((p.x - a.x) * abx + (p.y - a.y) * aby) / lengthSquared : 0.0f;
		t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
		const f32 dx = p.x - (a.x + abx * t), dy = p.y - (a.y + aby * t);
		return std::sqrt(dx * dx + dy * dy);
	}

	bool PointInConvexQuad(const ImVec2& p, const ImVec2 quad[4])
	{
		int sign = 0;
		for (int i = 0; i < 4; ++i)
		{
			const ImVec2& a = quad[i];
			const ImVec2& b = quad[(i + 1) % 4];
			const f32 cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
			const int s = cross > 0.0f ? 1 : (cross < 0.0f ? -1 : 0);
			if (s != 0)
			{
				if (sign != 0 && s != sign)
				{
					return false;
				}
				sign = s;
			}
		}
		return true;
	}

	f32 Snap(f32 value, f32 step)
	{
		return std::round(value / step) * step;
	}

	// 월드 값 → 부모 기준 로컬 값으로 바꿔 적용 (자식 오브젝트도 화면에서 본 대로 움직이도록)
	void SetWorldPosition(Transform* transform, const vec3& worldPosition)
	{
		const Transform* parent = transform->GetParent();
		transform->SetPosition(parent != nullptr ? TransformPoint(worldPosition, parent->GetWorldMatrix().Invert()) : worldPosition);
	}

	void SetWorldRotation(Transform* transform, const quat& worldRotation)
	{
		const Transform* parent = transform->GetParent();
		if (parent == nullptr)
		{
			transform->SetRotation(worldRotation);
			return;
		}

		vec3 parentScale;
		quat parentRotation;
		vec3 parentPosition;
		matx parentWorld = parent->GetWorldMatrix();
		if (!parentWorld.Decompose(parentScale, parentRotation, parentPosition))
		{
			return;
		}

		// world = local * parent  →  local = world * parent⁻¹
		quat inverseParent;
		parentRotation.Inverse(inverseParent);
		quat local = worldRotation * inverseParent;
		local.Normalize();
		transform->SetRotation(local);
	}
}

bool Editor::GetMouseRay(f32 mouseX, f32 mouseY, vec3& origin, vec3& direction) const
{
	const f32 width  = _sceneRect[2] - _sceneRect[0];
	const f32 height = _sceneRect[3] - _sceneRect[1];
	if (width <= 0.0f || height <= 0.0f)
	{
		return false;
	}

	matx view;
	matx projection;
	GetDisplayedCamera(view, projection);
	const matx inverseViewProjection = (view * projection).Invert();

	const f32 ndcX = (mouseX - _sceneRect[0]) / width * 2.0f - 1.0f;
	const f32 ndcY = 1.0f - (mouseY - _sceneRect[1]) / height * 2.0f;
	origin    = TransformPoint(vec3(ndcX, ndcY, 0.0f), inverseViewProjection);
	direction = TransformPoint(vec3(ndcX, ndcY, 1.0f), inverseViewProjection) - origin; // 가까운 평면 → 먼 평면
	return true;
}

bool Editor::UpdateGizmo(bool hovered)
{
	GameObject* selected = _selected.Get();
	const f32 width  = _sceneRect[2] - _sceneRect[0];
	const f32 height = _sceneRect[3] - _sceneRect[1];
	if (selected == nullptr || selected->transform == nullptr || width <= 0.0f || height <= 0.0f)
	{
		_gizmoActive = -1;
		_gizmoHover  = -1;
		return false;
	}

	Transform* transform = selected->transform;
	const ImGuiIO& io = ImGui::GetIO();

	matx view;
	matx projection;
	GetDisplayedCamera(view, projection);
	const matx viewProjection = view * projection;

	// 현재 월드 위치/회전
	vec3 worldScale;
	quat worldRotation = quat::Identity;
	vec3 worldPosition = transform->GetWorldPosition();
	{
		matx world = transform->GetWorldMatrix();
		vec3 position;
		if (world.Decompose(worldScale, worldRotation, position))
		{
			worldPosition = position;
		}
	}

	// 축: 드래그 중에는 시작 시점 값으로 고정 (로컬 축이 회전하면서 흔들리지 않도록)
	vec3 axes[3];
	const vec3 unit[3] = { vec3(1.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f) };
	for (int i = 0; i < 3; ++i)
	{
		axes[i] = _gizmoActive >= 0 ? _gizmoAxes[i] : (_gizmoLocal ? RotateVector(unit[i], worldRotation) : unit[i]);
	}

	// 화면에서 항상 같은 크기: 기즈모 중심 깊이에서 화면 1픽셀의 월드 길이
	const vec3 centerInView = TransformPoint(worldPosition, view);
	if (centerInView.z <= 0.01f)
	{
		return false; // 카메라 뒤
	}
	const f32 worldPerPixel = 2.0f * centerInView.z / (projection._22 * height);
	const f32 size = 90.0f * worldPerPixel;

	auto toScreen = [&](const vec3& p, ImVec2& out) -> bool
	{
		const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(DirectX::XMVectorSet(p.x, p.y, p.z, 1.0f), viewProjection);
		const f32 w = DirectX::XMVectorGetW(clip);
		if (w <= 1e-4f)
		{
			return false;
		}
		out = ImVec2(_sceneRect[0] + (DirectX::XMVectorGetX(clip) / w * 0.5f + 0.5f) * width,
		             _sceneRect[1] + (0.5f - DirectX::XMVectorGetY(clip) / w * 0.5f) * height);
		return true;
	};

	ImVec2 center;
	if (!toScreen(worldPosition, center))
	{
		return false;
	}

	const ImVec2 mouse = io.MousePos;

	// --- 핸들의 화면 도형 ---
	ImVec2 tips[3];
	bool   axisVisible[3]{};
	ImVec2 planes[3][4];
	bool   planeVisible[3]{};
	constexpr int kRingSegments = 64;
	ImVec2 rings[3][kRingSegments];
	bool   ringVisible[3]{};

	if (_gizmoMode == GizmoMode::Translate)
	{
		for (int i = 0; i < 3; ++i)
		{
			// 축이 시선과 거의 평행하면(화면에서 너무 짧으면) 숨긴다: 잡아도 제대로 움직일 수 없음
			axisVisible[i] = toScreen(worldPosition + axes[i] * size, tips[i])
				&& (tips[i].x - center.x) * (tips[i].x - center.x) + (tips[i].y - center.y) * (tips[i].y - center.y) > 12.0f * 12.0f;

			const vec3& a = axes[(i + 1) % 3];
			const vec3& b = axes[(i + 2) % 3];
			const vec3 corners[4] =
			{
				worldPosition + (a * 0.20f + b * 0.20f) * size,
				worldPosition + (a * 0.45f + b * 0.20f) * size,
				worldPosition + (a * 0.45f + b * 0.45f) * size,
				worldPosition + (a * 0.20f + b * 0.45f) * size,
			};
			planeVisible[i] = true;
			for (int k = 0; k < 4; ++k)
			{
				planeVisible[i] = planeVisible[i] && toScreen(corners[k], planes[i][k]);
			}
		}
	}
	else
	{
		for (int i = 0; i < 3; ++i)
		{
			const vec3& u = axes[(i + 1) % 3];
			const vec3& v = axes[(i + 2) % 3];
			ringVisible[i] = true;
			for (int k = 0; k < kRingSegments; ++k)
			{
				const f32 angle = DirectX::XM_2PI * static_cast<f32>(k) / kRingSegments;
				ringVisible[i] = ringVisible[i] && toScreen(worldPosition + (u * std::cos(angle) + v * std::sin(angle)) * (size * 0.85f), rings[i][k]);
			}
		}
	}

	// --- 마우스가 올라간 핸들 ---
	int hover = -1;
	if (hovered && !_navigating && _gizmoActive < 0)
	{
		constexpr f32 kPickDistance = 7.0f;
		f32 best = kPickDistance;

		if (_gizmoMode == GizmoMode::Translate)
		{
			for (int i = 0; i < 3; ++i)
			{
				if (axisVisible[i])
				{
					const f32 distance = DistanceToSegment(mouse, center, tips[i]);
					if (distance < best)
					{
						best  = distance;
						hover = i;
					}
				}
			}
			for (int i = 0; i < 3 && hover < 0; ++i)
			{
				if (planeVisible[i] && PointInConvexQuad(mouse, planes[i]))
				{
					hover = 3 + i;
				}
			}
		}
		else
		{
			for (int i = 0; i < 3; ++i)
			{
				if (!ringVisible[i])
				{
					continue;
				}
				for (int k = 0; k < kRingSegments; ++k)
				{
					const f32 distance = DistanceToSegment(mouse, rings[i][k], rings[i][(k + 1) % kRingSegments]);
					if (distance < best)
					{
						best  = distance;
						hover = i;
					}
				}
			}
		}
	}
	_gizmoHover = hover;

	// --- 드래그 시작 ---
	vec3 rayOrigin;
	vec3 rayDirection;
	const bool hasRay = GetMouseRay(mouse.x, mouse.y, rayOrigin, rayDirection);

	if (_gizmoHover >= 0 && hasRay && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		bool started = false;
		const int handle = _gizmoHover;

		if (_gizmoMode == GizmoMode::Translate && handle < 3)
		{
			started = ClosestParamOnLine(worldPosition, axes[handle], rayOrigin, rayDirection, _gizmoStartParam);
		}
		else
		{
			// 이동 평면(법선 = 그 축) 또는 회전 링의 평면(법선 = 회전축)
			const vec3& normal = axes[handle % 3];
			started = IntersectRayPlane(rayOrigin, rayDirection, worldPosition, normal, _gizmoStartHit);
		}

		if (started)
		{
			_gizmoActive        = handle;
			_gizmoStartPosition = worldPosition;
			_gizmoStartRotation = worldRotation;
			_gizmoDragValue     = 0.0f;
			for (int i = 0; i < 3; ++i)
			{
				_gizmoAxes[i] = axes[i];
			}
		}
		_gizmoConsumedClick = true; // 시작에 실패해도(평행 등) 선택 클릭으로 처리하지 않는다
	}

	// --- 드래그 중 ---
	if (_gizmoActive >= 0)
	{
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			EngineLog("[Editor] %s '%s' (%s %.3f)", _gizmoMode == GizmoMode::Translate ? "Moved" : "Rotated",
				selected->GetName().c_str(), _gizmoMode == GizmoMode::Translate ? "distance" : "degrees", _gizmoDragValue);
			_gizmoActive = -1;
		}
		else if (hasRay)
		{
			const bool snap = io.KeyCtrl;
			constexpr f32 kMoveSnap   = 0.25f;
			constexpr f32 kRotateSnap = DirectX::XMConvertToRadians(15.0f);

			if (_gizmoMode == GizmoMode::Translate && _gizmoActive < 3)
			{
				// 축 이동: 마우스 광선과 축의 가장 가까운 점까지
				const vec3& axis = _gizmoAxes[_gizmoActive];
				f32 t = 0.0f;
				if (ClosestParamOnLine(_gizmoStartPosition, axis, rayOrigin, rayDirection, t))
				{
					f32 distance = t - _gizmoStartParam;
					distance = snap ? Snap(distance, kMoveSnap) : distance;
					SetWorldPosition(transform, _gizmoStartPosition + axis * distance);
					_gizmoDragValue = distance;
				}
			}
			else if (_gizmoMode == GizmoMode::Translate)
			{
				// 평면 이동: 마우스 광선과 평면의 교점 이동량 (평면의 두 축 성분만)
				const int normalIndex = _gizmoActive - 3;
				const vec3& a = _gizmoAxes[(normalIndex + 1) % 3];
				const vec3& b = _gizmoAxes[(normalIndex + 2) % 3];
				vec3 hit;
				if (IntersectRayPlane(rayOrigin, rayDirection, _gizmoStartPosition, _gizmoAxes[normalIndex], hit))
				{
					const vec3 delta = hit - _gizmoStartHit;
					f32 da = delta.Dot(a);
					f32 db = delta.Dot(b);
					if (snap)
					{
						da = Snap(da, kMoveSnap);
						db = Snap(db, kMoveSnap);
					}
					SetWorldPosition(transform, _gizmoStartPosition + a * da + b * db);
					_gizmoDragValue = std::sqrt(da * da + db * db);
				}
			}
			else
			{
				// 회전: 시작 교점 → 현재 교점이 회전축 둘레로 이루는 부호 있는 각도
				const vec3& axis = _gizmoAxes[_gizmoActive];
				vec3 hit;
				if (IntersectRayPlane(rayOrigin, rayDirection, _gizmoStartPosition, axis, hit))
				{
					vec3 from = _gizmoStartHit - _gizmoStartPosition;
					vec3 to   = hit - _gizmoStartPosition;
					if (from.LengthSquared() > 1e-10f && to.LengthSquared() > 1e-10f)
					{
						from.Normalize();
						to.Normalize();
						f32 angle = std::atan2(from.Cross(to).Dot(axis), from.Dot(to));
						angle = snap ? Snap(angle, kRotateSnap) : angle;

						// 월드 축 기준 회전을 시작 회전 뒤에 적용
						quat rotation = _gizmoStartRotation * quat::CreateFromAxisAngle(axis, angle);
						rotation.Normalize();
						SetWorldRotation(transform, rotation);
						_gizmoDragValue = DirectX::XMConvertToDegrees(angle);
					}
				}
			}
		}
	}

	// --- 그리기 ---
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRect(ImVec2(_sceneRect[0], _sceneRect[1]), ImVec2(_sceneRect[2], _sceneRect[3]), true);

	const ImU32 axisColors[3] = { IM_COL32(230, 60, 60, 255), IM_COL32(80, 210, 80, 255), IM_COL32(70, 120, 255, 255) };
	const ImU32 highlight = IM_COL32(255, 220, 60, 255);
	auto colorOf = [&](int handle, int axis) -> ImU32
	{
		return (_gizmoActive == handle || (_gizmoActive < 0 && _gizmoHover == handle)) ? highlight : axisColors[axis];
	};

	if (_gizmoMode == GizmoMode::Translate)
	{
		for (int i = 0; i < 3; ++i)
		{
			if (!planeVisible[i])
			{
				continue;
			}
			const ImU32 color = colorOf(3 + i, i);
			drawList->AddConvexPolyFilled(planes[i], 4, (color & 0x00FFFFFF) | 0x50000000);
			drawList->AddPolyline(planes[i], 4, color, ImDrawFlags_Closed, 1.5f);
		}

		for (int i = 0; i < 3; ++i)
		{
			if (!axisVisible[i])
			{
				continue;
			}
			const ImU32 color = colorOf(i, i);
			drawList->AddLine(center, tips[i], color, 3.0f);

			// 화살촉
			f32 dx = tips[i].x - center.x;
			f32 dy = tips[i].y - center.y;
			const f32 length = std::sqrt(dx * dx + dy * dy);
			dx /= length;
			dy /= length;
			drawList->AddTriangleFilled(
				ImVec2(tips[i].x + dx * 12.0f, tips[i].y + dy * 12.0f),
				ImVec2(tips[i].x - dy * 6.0f, tips[i].y + dx * 6.0f),
				ImVec2(tips[i].x + dy * 6.0f, tips[i].y - dx * 6.0f),
				color);
		}
	}
	else
	{
		for (int i = 0; i < 3; ++i)
		{
			if (ringVisible[i])
			{
				const bool emphasized = _gizmoActive == i || (_gizmoActive < 0 && _gizmoHover == i);
				drawList->AddPolyline(rings[i], kRingSegments, colorOf(i, i), ImDrawFlags_Closed, emphasized ? 3.5f : 2.5f);
			}
		}

		// 회전 중: 중심 → 시작점, 중심 → 현재 마우스
		if (_gizmoActive >= 0)
		{
			ImVec2 start;
			if (toScreen(_gizmoStartHit, start))
			{
				drawList->AddLine(center, start, IM_COL32(255, 255, 255, 120), 1.0f);
			}
			drawList->AddLine(center, mouse, IM_COL32(255, 255, 255, 200), 1.0f);
		}
	}

	drawList->AddCircleFilled(center, 4.0f, IM_COL32(255, 255, 255, 230));

	if (_gizmoActive >= 0)
	{
		char label[64];
		if (_gizmoMode == GizmoMode::Translate)
		{
			std::snprintf(label, sizeof(label), "%+.3f%s", _gizmoDragValue, io.KeyCtrl ? "  (snap)" : "");
		}
		else
		{
			std::snprintf(label, sizeof(label), "%+.1f deg%s", _gizmoDragValue, io.KeyCtrl ? "  (snap)" : "");
		}
		drawList->AddText(ImVec2(center.x + 14.0f, center.y + 10.0f), IM_COL32(255, 255, 255, 255), label);
	}

	drawList->PopClipRect();
	return _gizmoConsumedClick;
}
