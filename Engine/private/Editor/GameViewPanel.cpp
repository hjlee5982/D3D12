#include "pch.h"
#include "GameViewPanel.h"
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

bool GameViewPanel::Begin(const f32 clearColor[4])
{
	if (_gameViewWidth == 0 || _gameViewHeight == 0)
	{
		return false;
	}

	if (!_gameTexture.Resize(_ctx.device, _gameViewWidth, _gameViewHeight))
	{
		return false;
	}

	_gameTexture.Begin(_ctx.deviceContext, clearColor);
	return true;
}

void GameViewPanel::Draw(unsigned int sceneDockId)
{
	// 처음 열리는 Game 창(이전 배치 파일에 없음)은 Scene 창과 같은 곳에 탭으로 붙인다
	if (sceneDockId != 0)
	{
		ImGui::SetNextWindowDockID(sceneDockId, ImGuiCond_FirstUseEver);
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool open = ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();

	if (!open)
	{
		// 다른 탭에 가려져 있으면 그리지 않는다
		_gameViewWidth  = 0;
		_gameViewHeight = 0;
		ImGui::End();
		return;
	}

	const ImVec2 available = ImGui::GetContentRegionAvail();
	_gameViewWidth  = available.x >= 1.0f ? static_cast<u32>(available.x) : 0;
	_gameViewHeight = available.y >= 1.0f ? static_cast<u32>(available.y) : 0;

	if (ID3D11ShaderResourceView* view = _gameTexture.GetShaderResourceView())
	{
		ImGui::Image(reinterpret_cast<ImTextureID>(view), available);
	}
	else
	{
		ImGui::Dummy(available);
	}

	// 안내 문구: 씬에 활성 카메라가 없으면 기본 카메라로 그리고 있음을 알린다
	bool hasCamera = false;
	if (const Scene* scene = _ctx.engine.GetSceneManager().GetActiveScene())
	{
		for (const auto& object : scene->GetGameObjects())
		{
			if (object != nullptr && object->IsActive() && !object->IsPendingDestroy())
			{
				const Camera* camera = object->GetComponent<Camera>();
				if (camera != nullptr && camera->IsEnabled())
				{
					hasCamera = true;
					break;
				}
			}
		}
	}

	const ImVec2 origin = ImGui::GetItemRectMin();
	char label[96];
	std::snprintf(label, sizeof(label), "%u x %u%s%s", _gameTexture.GetWidth(), _gameTexture.GetHeight(),
		hasCamera ? "" : "  |  No Camera in scene (default view)",
		_ctx.engine.IsPlaying() ? "" : "  |  Edit mode: press Play to run");
	ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x + 8.0f, origin.y + 6.0f), IM_COL32(255, 255, 255, 190), label);

	ImGui::End();
}
