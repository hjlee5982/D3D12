#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <memory>
#include <string>

#include "EngineAPI.h"
#include "Timer.h"
#include "Types.h"

class IDevice;
class SceneManager;
class ResourceManager;
class ComponentRegistry;
class Renderer11;
class Editor;
class Device11;

class ENGINE_API Engine
{
public:
	Engine(i32 version);
	~Engine();

	Engine(const Engine&) = delete;
	Engine& operator=(const Engine&) = delete;

public:
	bool EngineInitialize(HWND hWnd);
	void EngineUpdate();
	void EngineRenderBegin();
	// 활성 씬을 그린다 (RenderBegin과 RenderEnd 사이).
	// 에디터가 보이면 씬은 에디터의 Scene 창(화면 밖 텍스처)에, 숨기면(F1) 창 전체에 그린다.
	void EngineRender();
	void EngineRenderEnd();
	void EngineEndFrame();
	void EngineShutdown();

	// 활성 씬의 Update / LateUpdate (Game의 Update/LateUpdate 다음에 각각 호출)
	void EngineSceneUpdate();
	void EngineSceneLateUpdate();

	// 씬은 Engine이 소유하므로 Game.dll을 리로드해도 유지된다.
	SceneManager& GetSceneManager();

	// 텍스처/셰이더/메시/머티리얼. Engine 소유이므로 Game.dll 리로드 후에도 유지된다.
	// D3D11 백엔드에서만 유효하며, 초기화 전이거나 다른 백엔드면 nullptr.
	ResourceManager* GetResourceManager();

	// Game 컴포넌트 타입 레지스트리 (핫 리로드 직렬화에 사용)
	ComponentRegistry& GetComponentRegistry();

	// module(HMODULE)에 코드가 있는 컴포넌트를 모두 해제한다. DLL을 내리기 직전에 호출.
	// keepSnapshot이면 해제 전에 등록된 필드를 직렬화해 두고, RestoreModuleObjects에서 복원한다.
	u32 ReleaseModuleObjects(void* module, bool keepSnapshot);

	// 새 모듈이 타입을 등록한 뒤 호출: 보관한 스냅샷으로 컴포넌트를 다시 만든다.
	u32 RestoreModuleObjects();

	// --- 씬 파일 (경로는 Assets 기준 상대 경로, 예: "Scene/Main.scene") ---
	// 활성 씬을 저장한다. 성공하면 현재 씬 경로가 assetPath가 된다.
	bool SaveScene(const std::string& assetPath);

	// 파일에서 씬을 불러와 활성 씬을 교체한다. 파일을 읽거나 파싱하지 못하면 기존 씬을 유지하고 false.
	// 씬 Update/LateUpdate 도중(컴포넌트 안)에는 호출할 수 없다 (false).
	bool LoadScene(const std::string& assetPath);

	// 빈 씬으로 교체한다 (현재 씬 경로는 비워진다)
	void NewScene(const std::string& name);

	bool SceneFileExists(const std::string& assetPath) const;
	const std::string& GetScenePath() const;

	// 창 메시지를 에디터(ImGui)에 먼저 전달한다. true면 에디터가 사용한 입력이므로 호출자는 무시해야 한다.
	// (예: 텍스트 입력 중 Esc/F5가 앱 종료/리로드로 처리되지 않도록)
	bool EngineHandleWindowMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	// 에디터 표시 (F1로도 전환)
	void SetEditorVisible(bool visible);
	bool IsEditorVisible() const;

	// true면 EngineSceneUpdate/LateUpdate를 건너뛴다 (에디터에서 멈춘 채 편집)
	void SetScenePaused(bool paused);
	bool IsScenePaused() const;

	// 창 클라이언트 영역 크기가 바뀌었을 때 호출 (WM_SIZE).
	void OnResize(u32 width, u32 height);

	void SetTargetFPS(u32 fps);
	u32 GetTargetFPS() const;

	// true면 Present가 수직동기를 기다린다. 목표 FPS 제한과 함께 쓰면 대기가 겹치므로 기본값은 false.
	void SetVSync(bool enabled);
	bool IsVSyncEnabled() const;

	f32 GetDeltaTime() const;
	f64 GetTotalTime() const;

private:
	std::unique_ptr<IDevice>      _device;
	std::unique_ptr<SceneManager> _sceneManager;
	std::unique_ptr<ComponentRegistry> _componentRegistry;
	std::unique_ptr<ResourceManager> _resources;
	std::unique_ptr<Renderer11>      _renderer;
	std::unique_ptr<Editor>          _editor;
	Device11*                        _device11 = nullptr; // _device가 D3D11일 때 같은 객체
	bool _scenePaused = false;
	std::string _scenePath;
	bool _comInitialized = false;
	Timer _timer;
};
