#include "pch.h"
#include "Editor.h"
#include "EditorUtils.h"

#include "Engine.h"
#include "Log.h"
#include "Scene.h"
#include "SceneManager.h"

#include "imgui.h"
#include "imgui_internal.h" // DockBuilder (기본 도킹 배치)
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace EditorUtil;

Editor::Editor(Engine& engine)
	: _ctx(engine)
	, _mainMenu(_ctx)
	, _hierarchy(_ctx)
	, _inspector(_ctx)
	, _assets(_ctx)
	, _sceneView(_ctx)
	, _gameView(_ctx)
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

	_ctx.device        = device;
	_ctx.deviceContext = context;
	_initialized       = true;
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
	if (_ctx.resetLayout || dockNode == nullptr || !dockNode->IsSplitNode())
	{
		BuildDefaultLayout(dockspaceId);
		_ctx.resetLayout = false;
	}
	// 재생 중에는 에디터 색을 바꿔 "여기서 바꾼 값은 Stop하면 사라진다"는 것을 알린다
	PushPlayModeTint();

	ImGui::DockSpaceOverViewport(dockspaceId, ImGui::GetMainViewport());

	// 재생 시작 → Game 창을 앞으로, 정지 → Scene 창을 앞으로 (같은 탭 묶음일 때)
	const int playState = static_cast<int>(_ctx.engine.GetPlayState());
	if (playState != _lastPlayState)
	{
		const bool enteredPlay = _lastPlayState == static_cast<int>(PlayState::Edit);
		const bool stopped     = playState == static_cast<int>(PlayState::Edit);
		if (enteredPlay)
		{
			ImGui::SetWindowFocus("Game");
		}
		else if (stopped)
		{
			ImGui::SetWindowFocus("Scene");
		}
		_lastPlayState = playState;
	}

	Scene* scene = _ctx.engine.GetSceneManager().GetActiveScene();

	_mainMenu.Draw(scene);
	_hierarchy.Draw(scene);
	_inspector.Draw();
	_assets.Draw();
	_sceneView.Draw();
	_gameView.Draw(_sceneView.GetDockId());

	if (_ctx.showDemo)
	{
		ImGui::ShowDemoWindow(&_ctx.showDemo);
	}

	// 미뤄 둔 씬 변경(생성/삭제/부모 변경/씬 열기)을 패널을 다 그린 뒤 처리
	_ctx.ApplyPendingEdits();

	PopPlayModeTint();

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
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
	ImGui::DockBuilderDockWindow("Game", center); // Scene과 같은 탭 묶음. 끌어서 나란히 볼 수도 있다
	ImGui::DockBuilderFinish(dockspaceId);
}

void Editor::PushPlayModeTint()
{
	_playTintPushed = 0;
	if (!_ctx.engine.IsPlaying())
	{
		return;
	}

	// 기본 색에 푸른 기운을 섞는다 (유니티의 재생 중 틴트와 같은 목적)
	const ImGuiStyle& style = ImGui::GetStyle();
	auto tint = [&](ImGuiCol index)
	{
		const ImVec4 base = style.Colors[index];
		ImGui::PushStyleColor(index, ImVec4(base.x * 0.80f, base.y * 0.90f, base.z * 1.25f + 0.04f, base.w));
		++_playTintPushed;
	};

	tint(ImGuiCol_WindowBg);
	tint(ImGuiCol_MenuBarBg);
	tint(ImGuiCol_TitleBg);
	tint(ImGuiCol_TitleBgActive);
	tint(ImGuiCol_DockingEmptyBg);
}

void Editor::PopPlayModeTint()
{
	if (_playTintPushed > 0)
	{
		ImGui::PopStyleColor(_playTintPushed);
		_playTintPushed = 0;
	}
}

bool Editor::BeginSceneView(const f32 clearColor[4])
{
	return _initialized && _visible && _sceneView.Begin(clearColor);
}

bool Editor::GetSceneViewCamera(matx& view, matx& projection) const
{
	return _sceneView.GetCamera(view, projection);
}

bool Editor::BeginGameView(const f32 clearColor[4])
{
	return _initialized && _visible && _gameView.Begin(clearColor);
}
