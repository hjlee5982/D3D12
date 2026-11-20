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
#include "imgui_internal.h" // BeginViewportSideBar (메뉴 바 아래 도구 줄)
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

	// 창 목록: 누르면 닫힌 창은 다시 열고, 열린 창은 앞으로 꺼낸다 (체크 = 열려 있음)
	if (ImGui::BeginMenu("Window"))
	{
		for (int i = 0; i < static_cast<int>(EditorWindow::Count); ++i)
		{
			const EditorWindow window = static_cast<EditorWindow>(i);
			if (ImGui::MenuItem(EditorContext::GetWindowName(window), nullptr, _ctx.IsWindowOpen(window)))
			{
				_ctx.OpenWindow(window);
			}
		}
		ImGui::Separator();
		ImGui::TextDisabled("Middle-click a tab to close it");
		ImGui::EndMenu();
	}

	const f32 dt = _ctx.engine.GetDeltaTime();
	ImGui::Separator();
	ImGui::Text("%.1f ms (%.0f FPS)", dt * 1000.0f, dt > 0.0f ? 1.0f / dt : 0.0f);
	const std::string& scenePath = _ctx.engine.GetScenePath();
	ImGui::TextDisabled("| %s (%s) | F1: hide editor",
		scene != nullptr ? scene->GetName().c_str() : "(no scene)",
		scenePath.empty() ? "unsaved" : scenePath.c_str());

	ImGui::EndMainMenuBar();

	DrawToolbar();
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

void MainMenuBar::DrawToolbar()
{
	// 메인 메뉴 바 바로 아래에 붙는 한 줄. 메뉴 바처럼 뷰포트 작업 영역을 줄이므로 도킹 공간은 그 아래부터 시작한다.
	// (메뉴 바를 그린 다음에 불러야 메뉴 바 아래에 놓인다)
	const ImGuiStyle& style = ImGui::GetStyle();
	const ImVec2 padding(style.WindowPadding.x, 4.0f);
	const f32 height = ImGui::GetFrameHeight() + padding.y * 2.0f;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
	const bool open = ImGui::BeginViewportSideBar("##PlayToolbar", ImGui::GetMainViewport(), ImGuiDir_Up, height,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
	ImGui::PopStyleVar();

	if (open)
	{
		DrawPlayControls();
	}
	ImGui::End();
}

void MainMenuBar::DrawPlayControls()
{
	const PlayState state = _ctx.engine.GetPlayState();
	const bool playing = state != PlayState::Edit;
	const bool paused  = state == PlayState::Paused;

	// 세 아이콘 버튼을 가운데 정렬
	const ImVec2 buttonSize(40.0f, ImGui::GetFrameHeight());
	const f32 spacing    = ImGui::GetStyle().ItemSpacing.x;
	const f32 groupWidth = buttonSize.x * 3.0f + spacing * 2.0f;
	ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - groupWidth) * 0.5f));

	// 버튼 배경 (80, 80, 80). 켜져 있는 상태(재생 중 / 일시정지 중)는 색으로 구분한다.
	const ImVec4 normal (80.0f / 255.0f,  80.0f / 255.0f,  80.0f / 255.0f, 1.0f);
	const ImVec4 hovered(100.0f / 255.0f, 100.0f / 255.0f, 100.0f / 255.0f, 1.0f);
	const ImVec4 pressed(65.0f / 255.0f,  65.0f / 255.0f,  65.0f / 255.0f, 1.0f);
	const ImVec4 playOn (0.20f, 0.45f, 0.85f, 1.0f);
	const ImVec4 pauseOn(0.85f, 0.55f, 0.15f, 1.0f);

	ImGui::PushStyleColor(ImGuiCol_Button,        normal);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive,  pressed);

	// 아이콘은 버튼을 그린 뒤 그 위에 도형으로 그린다. 글자색(GetColorU32)을 쓰므로 비활성일 때 자동으로 흐려진다.
	enum class Icon { Play, Stop, Pause, Step };
	auto iconButton = [&](const char* id, Icon icon, bool on, const ImVec4& onColor) -> bool
	{
		if (on)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, onColor);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(onColor.x * 1.15f, onColor.y * 1.15f, onColor.z * 1.15f, 1.0f));
		}
		const bool clicked = ImGui::Button(id, buttonSize);
		if (on)
		{
			ImGui::PopStyleColor(2);
		}

		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
		const f32 h = (max.y - min.y) * 0.26f; // 아이콘 반 높이
		const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		switch (icon)
		{
		case Icon::Play:  // ▶
			drawList->AddTriangleFilled(ImVec2(c.x - h * 0.8f, c.y - h), ImVec2(c.x - h * 0.8f, c.y + h), ImVec2(c.x + h, c.y), color);
			break;
		case Icon::Stop:  // ■
			drawList->AddRectFilled(ImVec2(c.x - h * 0.85f, c.y - h * 0.85f), ImVec2(c.x + h * 0.85f, c.y + h * 0.85f), color);
			break;
		case Icon::Pause: // ❚❚
		{
			const f32 w = h * 0.45f;
			drawList->AddRectFilled(ImVec2(c.x - h * 0.75f, c.y - h), ImVec2(c.x - h * 0.75f + w, c.y + h), color);
			drawList->AddRectFilled(ImVec2(c.x + h * 0.75f - w, c.y - h), ImVec2(c.x + h * 0.75f, c.y + h), color);
			break;
		}
		case Icon::Step:  // ▶❚
		{
			const f32 left = c.x - h * 1.0f;
			drawList->AddTriangleFilled(ImVec2(left, c.y - h), ImVec2(left, c.y + h), ImVec2(left + h * 1.4f, c.y), color);
			drawList->AddRectFilled(ImVec2(left + h * 1.55f, c.y - h), ImVec2(left + h * 1.55f + h * 0.4f, c.y + h), color);
			break;
		}
		}
		return clicked;
	};

	// 재생 / 정지
	if (iconButton("##PlayStop", playing ? Icon::Stop : Icon::Play, playing, playOn))
	{
		if (playing) { _ctx.engine.Stop(); }
		else         { _ctx.engine.Play(); }
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip(playing ? "Stop and restore the scene to its state before Play (Ctrl+P)"
		                          : "Play: run the game. The scene is restored when you stop (Ctrl+P)");
	}

	// 일시정지 (재생 중에만)
	ImGui::SameLine();
	ImGui::BeginDisabled(!playing);
	if (iconButton("##Pause", Icon::Pause, paused, pauseOn))
	{
		_ctx.engine.SetPaused(!paused);
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::SetTooltip("Pause / resume (Ctrl+Shift+P)");
	}

	// 한 프레임 진행
	ImGui::SameLine();
	if (iconButton("##Step", Icon::Step, false, normal))
	{
		_ctx.engine.Step();
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::SetTooltip("Step: advance one frame (pauses if playing)");
	}
	ImGui::EndDisabled();

	ImGui::PopStyleColor(3);

	// 상태 표시는 버튼 묶음 오른쪽에 (가운데 정렬 계산에는 넣지 않는다)
	ImGui::SameLine(0.0f, spacing * 3.0f);
	ImGui::AlignTextToFramePadding();
	switch (state)
	{
	case PlayState::Edit:    ImGui::TextDisabled("EDIT"); break;
	case PlayState::Playing: ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "PLAYING"); break;
	case PlayState::Paused:  ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "PAUSED"); break;
	}
}
