#pragma once

#include "AssetsPanel.h"
#include "EditorContext.h"
#include "GameViewPanel.h"
#include "HierarchyPanel.h"
#include "InspectorPanel.h"
#include "MainMenuBar.h"
#include "ProjectPanel.h"
#include "SceneViewPanel.h"
#include "Types.h"

#include <string>

class Engine;

// ImGui 기반 인게임 에디터 (Engine 내부 전용).
//
// Engine.dll 안에 있으므로 Game.dll을 리로드해도 에디터 상태(선택, 창 배치)가 유지된다.
// 이 클래스는 ImGui 초기화/입력/프레임 순서만 맡고, 화면은 패널들이 그린다.
//
//   MainMenuBar    : File / Create / View 메뉴, 그 아래 도구 줄(가운데 Play / Pause / Step), 단축키
//   HierarchyPanel : 오브젝트 트리, 생성/삭제, 드래그로 부모 변경
//   InspectorPanel : 상세 정보, 리플렉션 필드 편집, 컴포넌트 추가/제거
//   AssetsPanel    : 에셋 파일 로드/리로드/언로드, 머티리얼
//   ProjectPanel   : Assets 폴더 트리 + 폴더 내용 (유니티 Project 창), 파일 끌어다 놓기
//   SceneViewPanel : 에디터 카메라 화면, 클릭 선택, 선택 외곽선, TransformGizmo
//   GameViewPanel  : 씬 카메라 화면
//
// 패널 사이에서 공유하는 상태(선택, 미뤄 둔 씬 변경)는 EditorContext 하나에 둔다.
// F1로 표시/숨김. ImGui가 텍스트 입력 중이면 키 입력은 게임/호스트로 가지 않는다.
class Editor
{
public:
	explicit Editor(Engine& engine);
	~Editor();

	Editor(const Editor&) = delete;
	Editor& operator=(const Editor&) = delete;

	bool Initialize(HWND hWnd, ID3D11Device* device, ID3D11DeviceContext* context);
	void Shutdown();

	// true면 에디터가 이 메시지를 사용했다 (호출자는 무시해야 함)
	bool HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	// 씬을 그린 뒤, Present 전에 호출
	void Render();

	// Scene 창 (에디터 카메라). 창이 닫혀 있거나 가려져 있으면 false.
	bool BeginSceneView(const f32 clearColor[4]);
	u32  GetSceneViewWidth() const  { return _sceneView.GetWidth(); }
	u32  GetSceneViewHeight() const { return _sceneView.GetHeight(); }
	bool GetSceneViewCamera(matx& view, matx& projection) const;

	// Game 창 (씬의 Camera 컴포넌트). 창이 가려져 있으면 false.
	bool BeginGameView(const f32 clearColor[4]);
	u32  GetGameViewWidth() const  { return _gameView.GetWidth(); }
	u32  GetGameViewHeight() const { return _gameView.GetHeight(); }

	void SetVisible(bool visible) { _visible = visible; }
	bool IsVisible() const { return _visible; }

private:
	void BuildDefaultLayout(unsigned int dockspaceId);
	void PushPlayModeTint();
	void PopPlayModeTint();

private:
	bool _initialized = false;
	bool _visible     = true;
	std::string _iniPath;

	int _lastPlayState  = 0; // 재생 상태 전환 감지 (Play → Game 창 앞으로, Stop → Scene 창 앞으로)
	int _playTintPushed = 0;

	// 선언 순서 = 생성 순서: 컨텍스트가 패널보다 먼저 만들어져야 한다
	EditorContext  _ctx;
	MainMenuBar    _mainMenu;
	HierarchyPanel _hierarchy;
	InspectorPanel _inspector;
	AssetsPanel    _assets;
	ProjectPanel   _project;
	SceneViewPanel _sceneView;
	GameViewPanel  _gameView;
};
