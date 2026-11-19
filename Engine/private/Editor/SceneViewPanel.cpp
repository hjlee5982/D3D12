#include "pch.h"
#include "SceneViewPanel.h"
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
#include "Renderer11.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace EditorUtil;

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

bool SceneViewPanel::Begin(const f32 clearColor[4])
{
	if (_sceneViewWidth == 0 || _sceneViewHeight == 0)
	{
		return false;
	}

	// 크기가 바뀐 경우에만 다시 만든다. 이전 텍스처는 지난 프레임 ImGui 드로우가 이미 끝났으므로 바로 해제해도 된다.
	if (!_sceneTexture.Resize(_ctx.device, _sceneViewWidth, _sceneViewHeight))
	{
		return false;
	}

	_sceneTexture.Begin(_ctx.deviceContext, clearColor);
	return true;
}

void SceneViewPanel::Draw()
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

	_sceneDockId = ImGui::GetWindowDockID();
	DrawToolbar();

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
	UpdateCamera(_sceneViewHovered);

	// 외곽선을 먼저 그리고 기즈모를 그 위에 그린다
	DrawSelectionOutline();
	_gizmo.Update(_ctx, MakeViewport(), _sceneViewHovered, _navigating);

	// 왼쪽 클릭(드래그 아님)으로 오브젝트 선택. 기즈모를 누른 클릭은 제외.
	const ImGuiIO& io = ImGui::GetIO();
	if (_sceneViewHovered && !_navigating && !_gizmo.ConsumedClick() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
		if (drag.x * drag.x + drag.y * drag.y < 16.0f)
		{
			PickObject(io.MousePos.x, io.MousePos.y);
		}
	}
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		_gizmo.ClearConsumedClick();
	}

	ImGui::End();
}

void SceneViewPanel::DrawToolbar()
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

	// 에디터 카메라 설정 (게임 카메라 화면은 Game 창)
	ImGui::SetNextItemWidth(90.0f);
	ImGui::DragFloat("Speed", &_cameraSpeed, 0.1f, 0.1f, 200.0f, "%.1f");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(70.0f);
	ImGui::DragFloat("FOV", &_cameraFovY, 0.5f, 10.0f, 120.0f, "%.0f");

	// 기즈모
	placeGroup(radioWidth("Move (W)") + style.ItemSpacing.x + radioWidth("Rotate (E)") + style.ItemSpacing.x + buttonWidth("World"));
	int gizmo = _gizmo.GetMode() == TransformGizmo::Mode::Translate ? 0 : 1;
	if (ImGui::RadioButton("Move (W)", &gizmo, 0))   { _gizmo.SetMode(TransformGizmo::Mode::Translate); }
	ImGui::SameLine();
	if (ImGui::RadioButton("Rotate (E)", &gizmo, 1)) { _gizmo.SetMode(TransformGizmo::Mode::Rotate); }
	ImGui::SameLine();
	if (ImGui::Button(_gizmo.IsLocal() ? "Local" : "World"))
	{
		_gizmo.SetLocal(!_gizmo.IsLocal());
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

vec3 SceneViewPanel::GetCameraForward() const
{
	const quat rotation = quat::CreateFromYawPitchRoll(_cameraYaw, _cameraPitch, 0.0f);
	return RotateVector(vec3(0.0f, 0.0f, 1.0f), rotation);
}

bool SceneViewPanel::GetCamera(matx& view, matx& projection) const
{
	if (_sceneTexture.GetWidth() == 0 || _sceneTexture.GetHeight() == 0)
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

void SceneViewPanel::GetDisplayedCamera(matx& view, matx& projection) const
{
	if (GetCamera(view, projection))
	{
		return;
	}

	const f32 width  = static_cast<f32>(_sceneTexture.GetWidth() > 0 ? _sceneTexture.GetWidth() : 1);
	const f32 height = static_cast<f32>(_sceneTexture.GetHeight() > 0 ? _sceneTexture.GetHeight() : 1);
	const Renderer11::CameraMatrices camera = Renderer11::ResolveSceneCamera(_ctx.engine.GetSceneManager().GetActiveScene(), width / height);
	view       = camera.view;
	projection = camera.projection;
}

void SceneViewPanel::UpdateCamera(bool hovered)
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
	if (!_navigating && (hovered || ImGui::IsWindowFocused()) && !io.WantTextInput && !_gizmo.IsDragging())
	{
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) { _gizmo.SetMode(TransformGizmo::Mode::Translate); }
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) { _gizmo.SetMode(TransformGizmo::Mode::Rotate); }
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
		if (const GameObject* selected = _ctx.selected.Get())
		{
			constexpr f32 kFocusDistance = 3.0f;
			_cameraPosition = selected->transform->GetWorldPosition() - forward * kFocusDistance;
		}
	}
}

void SceneViewPanel::PickObject(f32 mouseX, f32 mouseY)
{
	Scene* scene = _ctx.engine.GetSceneManager().GetActiveScene();
	ResourceManager* resources = _ctx.engine.GetResourceManager();
	const f32 width  = _sceneRect[2] - _sceneRect[0];
	const f32 height = _sceneRect[3] - _sceneRect[1];
	if (scene == nullptr || resources == nullptr || width <= 0.0f || height <= 0.0f)
	{
		return;
	}

	// 마우스 → 월드 공간의 가까운 평면 위 점 → 먼 평면 위 점 (선분)
	vec3 nearWorld;
	vec3 segment;
	if (!MakeViewport().MouseRay(mouseX, mouseY, nearWorld, segment))
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
	_ctx.selected = best != nullptr ? ObjectRef(best) : ObjectRef();
}

void SceneViewPanel::DrawSelectionOutline()
{
	GameObject* selected = _ctx.selected.Get();
	ResourceManager* resources = _ctx.engine.GetResourceManager();
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

SceneViewport SceneViewPanel::MakeViewport() const
{
	SceneViewport viewport;
	for (int i = 0; i < 4; ++i)
	{
		viewport.rect[i] = _sceneRect[i];
	}
	GetDisplayedCamera(viewport.view, viewport.projection);
	return viewport;
}
