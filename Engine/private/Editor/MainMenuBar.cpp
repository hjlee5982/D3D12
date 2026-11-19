#include "pch.h"
#include "MainMenuBar.h"
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

void MainMenuBar::Draw(Scene* scene)
{
	// Ctrl+S: 창 포커스와 무관하게 저장 (재생 중에는 SaveCurrentScene이 거부)
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
	{
		SaveCurrentScene();
	}

	// Ctrl+P: 재생/정지, Ctrl+Shift+P: 일시정지
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_P, ImGuiInputFlags_RouteGlobal))
	{
		if (_ctx.engine.IsPlaying()) { _ctx.engine.Stop(); }
		else                     { _ctx.engine.Play(); }
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, ImGuiInputFlags_RouteGlobal))
	{
		_ctx.engine.SetPaused(_ctx.engine.GetPlayState() == PlayState::Playing);
	}

	if (!ImGui::BeginMainMenuBar())
	{
		return;
	}

	DrawFileMenu();

	if (ImGui::BeginMenu("Create"))
	{
		GameObject* parent = _ctx.selected.Get();
		if (ImGui::MenuItem("Empty"))  { _ctx.pendingCreateKind = "Empty"; }
		if (ImGui::MenuItem("Quad"))   { _ctx.pendingCreateKind = "Quad"; }
		if (ImGui::MenuItem("Camera")) { _ctx.pendingCreateKind = "Camera"; }
		ImGui::Separator();
		if (ImGui::MenuItem("Empty Child", nullptr, false, parent != nullptr))
		{
			_ctx.pendingCreateKind   = "Empty";
			_ctx.pendingCreateParent = ObjectRef(parent);
		}
		ImGui::EndMenu();
	}


	if (ImGui::BeginMenu("View"))
	{
		if (ImGui::MenuItem("Reset Layout"))
		{
			_ctx.resetLayout = true;
		}
		ImGui::MenuItem("ImGui demo", nullptr, &_ctx.showDemo);
		ImGui::EndMenu();
	}

	DrawPlayControls();

	const f32 dt = _ctx.engine.GetDeltaTime();
	ImGui::Separator();
	ImGui::Text("%.1f ms (%.0f FPS)", dt * 1000.0f, dt > 0.0f ? 1.0f / dt : 0.0f);
	const std::string& scenePath = _ctx.engine.GetScenePath();
	ImGui::TextDisabled("| %s (%s) | F1: hide editor",
		scene != nullptr ? scene->GetName().c_str() : "(no scene)",
		scenePath.empty() ? "unsaved" : scenePath.c_str());

	ImGui::EndMainMenuBar();

	DrawSaveAsPopup();
}

void MainMenuBar::DrawFileMenu()
{
	if (!ImGui::BeginMenu("File"))
	{
		return;
	}

	if (ImGui::MenuItem("New Scene"))
	{
		_ctx.pendingNew = true;
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
			if (ImGui::MenuItem(file.c_str(), nullptr, file == _ctx.engine.GetScenePath()))
			{
				_ctx.pendingLoad = file;
			}
		}
		ImGui::EndMenu();
	}

	ImGui::Separator();

	const bool playing = _ctx.engine.IsPlaying();
	if (playing)
	{
		ImGui::TextDisabled("Stop play mode to save");
	}
	if (ImGui::MenuItem("Save", "Ctrl+S", false, !playing))
	{
		SaveCurrentScene();
	}
	if (ImGui::MenuItem("Save As...", nullptr, false, !playing))
	{
		_openSaveAs = true;
	}

	ImGui::Separator();
	if (ImGui::MenuItem("Reload From Disk", nullptr, false, !_ctx.engine.GetScenePath().empty()))
	{
		_ctx.pendingLoad = _ctx.engine.GetScenePath();
	}

	ImGui::EndMenu();
}

void MainMenuBar::DrawSaveAsPopup()
{
	if (_openSaveAs)
	{
		if (!_ctx.engine.GetScenePath().empty())
		{
			_saveAsPath = _ctx.engine.GetScenePath();
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

	if (_ctx.engine.SceneFileExists(path) && path != _ctx.engine.GetScenePath())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s exists and will be overwritten.", path.c_str());
	}

	const bool valid = !_saveAsPath.empty() && path.find("..") == std::string::npos && path.find(':') == std::string::npos;
	ImGui::BeginDisabled(!valid);
	if (ImGui::Button("Save", ImVec2(120.0f, 0.0f)) || (submitted && valid))
	{
		_ctx.engine.SaveScene(path);
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

void MainMenuBar::SaveCurrentScene()
{
	if (_ctx.engine.IsPlaying())
	{
		EngineLog("[Editor] Cannot save during play mode. Stop first.");
		return;
	}

	const std::string& path = _ctx.engine.GetScenePath();
	if (path.empty())
	{
		_openSaveAs = true; // 아직 파일이 없으면 다른 이름으로 저장
		return;
	}
	_ctx.engine.SaveScene(path);
}

std::vector<std::string> MainMenuBar::FindSceneFiles() const
{
	std::vector<std::string> files;

	ResourceManager* resources = _ctx.engine.GetResourceManager();
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

void MainMenuBar::DrawPlayControls()
{
	const PlayState state = _ctx.engine.GetPlayState();
	const bool playing = state != PlayState::Edit;

	ImGui::Separator();

	// 재생 / 정지
	if (playing)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
	}
	if (ImGui::Button(playing ? "Stop" : "Play"))
	{
		if (playing) { _ctx.engine.Stop(); }
		else         { _ctx.engine.Play(); }
	}
	if (playing)
	{
		ImGui::PopStyleColor();
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip(playing ? "Stop and restore the scene to its state before Play (Ctrl+P)"
		                          : "Run the game. The scene is restored when you stop (Ctrl+P)");
	}

	// 일시정지 (재생 중에만)
	ImGui::BeginDisabled(!playing);
	const bool paused = state == PlayState::Paused;
	if (paused)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.15f, 1.0f));
	}
	if (ImGui::Button("Pause"))
	{
		_ctx.engine.SetPaused(!paused);
	}
	if (paused)
	{
		ImGui::PopStyleColor();
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::SetTooltip("Pause / resume (Ctrl+Shift+P)");
	}

	// 한 프레임 진행
	if (ImGui::Button("Step"))
	{
		_ctx.engine.Step();
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::SetTooltip("Advance one frame (pauses if playing)");
	}
	ImGui::EndDisabled();

	switch (state)
	{
	case PlayState::Edit:    ImGui::TextDisabled("EDIT"); break;
	case PlayState::Playing: ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "PLAYING"); break;
	case PlayState::Paused:  ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "PAUSED"); break;
	}
}
