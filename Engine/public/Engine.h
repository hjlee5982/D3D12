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

// 재생 상태 (유니티의 Edit / Play / Pause)
//   Edit    : 배치·편집만. 컴포넌트 Update와 Game.dll의 GameUpdate가 실행되지 않는다
//             (EXECUTE_IN_EDIT_MODE 컴포넌트만 실행)
//   Playing : 모든 컴포넌트와 게임 로직 실행
//   Paused  : 재생 중 멈춤. Step()으로 한 프레임씩 진행
// Play()는 씬을 메모리에 저장해 두고, Stop()은 그 상태로 되돌린다 (재생 중 변경은 버려진다).
// 재생 중 LoadScene(게임 코드의 레벨 전환 등)은 재생을 유지하며, Stop하면 재생 전 씬으로 돌아간다.
enum class PlayState : u8
{
	Edit,
	Playing,
	Paused,
};

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

	// --- 재생 모드 ---
	// 에디터가 있으면 Edit 상태로 시작하고, 없으면(에디터 초기화 실패 등) 바로 Playing으로 시작한다.
	// Play/Stop은 다음 프레임 시작에 적용된다 (씬 교체 중 포인터 무효화 방지). 호출 직후 GetPlayState는 아직 이전 값.
	void Play();                 // Edit → Playing (씬 스냅샷 저장)
	void Stop();                 // Playing/Paused → Edit (스냅샷으로 복원)
	void SetPaused(bool paused); // Playing ↔ Paused
	void Step();                 // Paused 상태에서 다음 한 프레임만 진행 (Playing이면 멈춘 뒤 진행)

	// 재생 세션을 복원 없이 즉시 끝낸다 (씬을 건드리지 않음). 에디터가 재생 중에 다른 씬을 열 때 사용:
	// 곧 씬을 통째로 바꿀 것이므로 재생 전 상태로 되돌릴 필요가 없다.
	void DiscardPlaySession();

	PlayState GetPlayState() const;
	bool IsPlaying() const;      // Playing 또는 Paused (= 재생 세션 중)

	// 이번 프레임에 게임 로직(Game.dll Update, 컴포넌트 Update)을 실행하는지. 호스트가 GameUpdate 호출 여부를 결정할 때 사용.
	bool IsSimulating() const;

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
	PlayState   _playState     = PlayState::Playing;
	bool        _stepRequested = false;
	std::string _playSnapshot;         // Play 시작 시점의 씬 (Stop에서 복원)
	PlayState   _pendingCommand = PlayState::Playing; // 요청된 Play/Stop (다음 프레임 시작에 처리)
	bool        _hasPendingCommand = false;
	void ApplyPlayCommand(PlayState target);
	std::string _scenePath;
	bool _comInitialized = false;
	Timer _timer;
};
