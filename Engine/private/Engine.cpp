#include "pch.h"
#include "Engine.h"
#include "Device11.h"
#include "Device12.h"
#include "Log.h"
#include "SceneManager.h"
#include "ComponentRegistry.h"
#include "ResourceManager.h"
#include "Renderer11.h"
#include "Editor.h"
#include "Scene.h"
#include "SceneSerializer.h"
#include "Camera.h"
#include "MeshRenderer.h"
#include "Transform.h"

Engine::Engine(i32 version)
	: _sceneManager(std::make_unique<SceneManager>())
	, _componentRegistry(std::make_unique<ComponentRegistry>())
{
	// Engine 컴포넌트도 등록해 두면 에디터가 필드를 보여주고 Add Component로 만들 수 있다.
	// (module = nullptr → Game.dll 리로드 때 등록 해제되지 않는다)
	_componentRegistry->Register<Transform>();
	_componentRegistry->Register<Camera>();
	_componentRegistry->Register<MeshRenderer>();

	if (version == 11)
	{
		_device = std::make_unique<Device11>();
	}
	else
	{
		_device = std::make_unique<Device12>();
	}
}

Engine::~Engine() = default;

bool Engine::EngineInitialize(HWND hWnd)
{
	_timer.Reset();

	// WIC(텍스처 디코딩)가 COM을 사용한다. 이미 다른 모드로 초기화된 경우(RPC_E_CHANGED_MODE)도 사용 가능.
	const HRESULT comResult = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	_comInitialized = SUCCEEDED(comResult);

	if (!_device->Initialize(hWnd))
	{
		EngineLog("[Engine] Device initialization failed");
		return false;
	}

	if (auto* device11 = dynamic_cast<Device11*>(_device.get()))
	{
		_device11  = device11;
		_resources = std::make_unique<ResourceManager>(device11->GetDevice());
		_renderer  = std::make_unique<Renderer11>(*device11);
		if (!_renderer->Initialize())
		{
			return false;
		}

		// 에디터는 실패해도 게임은 계속 실행한다
		_editor = std::make_unique<Editor>(*this);
		if (!_editor->Initialize(hWnd, device11->GetDevice(), device11->GetContext()))
		{
			EngineLog("[Engine] Editor initialization failed. Continuing without editor.");
			_editor.reset();
		}

		// 에디터가 있으면 편집 모드로 시작 (재생 버튼으로 실행), 없으면 바로 게임 실행
		_playState = _editor ? PlayState::Edit : PlayState::Playing;
	}

	return true;
}

void Engine::EngineUpdate()
{
	// 씬 업데이트 도중에 요청된 Play/Stop은 프레임 사이인 여기서 처리한다
	if (_hasPendingCommand)
	{
		_hasPendingCommand = false;
		ApplyPlayCommand(_pendingCommand);
	}

	if (_resources)
	{
		_resources->PollFileChanges();
	}

	_timer.Update();
}

void Engine::EngineRenderBegin()
{
	_device->RenderBegin();
}

void Engine::EngineRender()
{
	const Scene* scene = _sceneManager->GetActiveScene();

	if (_editor && _editor->IsVisible())
	{
		// 1) 씬 → Scene 창 크기의 화면 밖 텍스처 (창이 닫혀 있거나 접혀 있으면 그리지 않음)
		constexpr f32 kSceneClearColor[4] = { 0.1f, 0.3f, 0.9f, 1.0f };
		// Scene 창: 에디터 카메라
		if (_renderer && _editor->BeginSceneView(kSceneClearColor))
		{
			Renderer11::CameraMatrices camera;
			const bool useEditorCamera = _editor->GetSceneViewCamera(camera.view, camera.projection);
			_renderer->Render(scene, *_resources, _editor->GetSceneViewWidth(), _editor->GetSceneViewHeight(),
				useEditorCamera ? &camera : nullptr);
		}

		// Game 창: 씬의 Camera 컴포넌트 (탭이 가려져 있으면 그리지 않음)
		if (_renderer && _editor->BeginGameView(kSceneClearColor))
		{
			_renderer->Render(scene, *_resources, _editor->GetGameViewWidth(), _editor->GetGameViewHeight());
		}

		// 2) 창(백 버퍼)으로 돌아와 에디터 UI를 그린다. Scene 창이 위 텍스처를 표시한다.
		if (_device11)
		{
			_device11->BindBackBuffer();
		}
		_editor->Render();
		return;
	}

	// 에디터를 숨기면 창 전체가 게임 화면
	if (_renderer && _device11)
	{
		_renderer->Render(scene, *_resources, _device11->GetWidth(), _device11->GetHeight());
	}
}

void Engine::EngineRenderEnd()
{
	_device->RenderEnd();
}

void Engine::EngineEndFrame()
{
	_timer.WaitForTargetFrameTime();
}

void Engine::EngineShutdown()
{
	// 에디터 → 씬 → 리소스 → 렌더러 → 디바이스 순으로 해제
	_editor.reset();
	_device11 = nullptr;
	_sceneManager->ClearScene();
	if (_resources)
	{
		_resources->Clear();
	}
	_renderer.reset();
	_resources.reset();
	_device->Shutdown();

	if (_comInitialized)
	{
		::CoUninitialize();
		_comInitialized = false;
	}
}

void Engine::EngineSceneUpdate()
{
	if (IsSimulating())
	{
		_sceneManager->Update(_timer.GetDeltaTime());
	}
	else if (_playState == PlayState::Edit)
	{
		_sceneManager->Update(_timer.GetDeltaTime(), true); // EXECUTE_IN_EDIT_MODE 컴포넌트만
	}
}

void Engine::EngineSceneLateUpdate()
{
	if (IsSimulating())
	{
		_sceneManager->LateUpdate(_timer.GetDeltaTime());
	}
	else if (_playState == PlayState::Edit)
	{
		_sceneManager->LateUpdate(_timer.GetDeltaTime(), true);
	}

	_stepRequested = false; // Step은 한 프레임만
}

SceneManager& Engine::GetSceneManager()
{
	return *_sceneManager;
}

ResourceManager* Engine::GetResourceManager()
{
	return _resources.get();
}

ComponentRegistry& Engine::GetComponentRegistry()
{
	return *_componentRegistry;
}

u32 Engine::ReleaseModuleObjects(void* module, bool keepSnapshot)
{
	const u32 released = _componentRegistry->SnapshotAndRelease(_sceneManager->GetActiveScene(), module, keepSnapshot);
	EngineLog("[Engine] Released %u component(s) owned by module %p (snapshots kept: %u)",
		released, module, _componentRegistry->GetPendingSnapshotCount());
	return released;
}

u32 Engine::RestoreModuleObjects()
{
	const u32 pending  = _componentRegistry->GetPendingSnapshotCount();
	const u32 restored = _componentRegistry->RestoreSnapshots();
	if (pending > 0)
	{
		EngineLog("[Engine] Restored %u/%u component(s) from snapshots", restored, pending);
	}
	return restored;
}

void Engine::OnResize(u32 width, u32 height)
{
	_device->Resize(width, height);
}

void Engine::SetTargetFPS(u32 fps)
{
	_timer.SetTargetFPS(fps);
}

u32 Engine::GetTargetFPS() const
{
	return _timer.GetTargetFPS();
}

void Engine::SetVSync(bool enabled)
{
	_device->SetVSync(enabled);
}

bool Engine::IsVSyncEnabled() const
{
	return _device->IsVSyncEnabled();
}

f32 Engine::GetDeltaTime() const
{
	return _timer.GetDeltaTime();
}

f64 Engine::GetTotalTime() const
{
	return _timer.GetTotalTime();
}

bool Engine::EngineHandleWindowMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	return _editor != nullptr && _editor->HandleMessage(hWnd, message, wParam, lParam);
}

void Engine::SetEditorVisible(bool visible)
{
	if (_editor)
	{
		_editor->SetVisible(visible);
	}
}

bool Engine::IsEditorVisible() const
{
	return _editor != nullptr && _editor->IsVisible();
}

// Play/Stop은 요청만 기록하고, 실제 씬 스냅샷/복원은 다음 프레임 시작(EngineUpdate)에서 한다.
// 씬을 통째로 교체하므로 호출하는 쪽(에디터 그리기 도중, 컴포넌트 Update 도중 등)이 들고 있는
// Scene*/GameObject* 포인터가 그 프레임 안에서 무효가 되는 일을 원천적으로 막는다.
// 같은 프레임에 여러 번 호출되면 마지막 요청만 적용된다.
void Engine::Play()
{
	_pendingCommand    = PlayState::Playing;
	_hasPendingCommand = true;
}

void Engine::Stop()
{
	_pendingCommand    = PlayState::Edit;
	_hasPendingCommand = true;
}

void Engine::DiscardPlaySession()
{
	_hasPendingCommand = false;
	_stepRequested     = false;
	_playSnapshot.clear();
	if (_playState != PlayState::Edit)
	{
		_playState = PlayState::Edit;
		EngineLog("[Play] Play session discarded");
	}
}

void Engine::ApplyPlayCommand(PlayState target)
{
	if (target == PlayState::Playing && _playState == PlayState::Edit)
	{
		// 재생 시작: 지금 씬을 메모리에 저장 (Stop에서 이 상태로 되돌린다)
		_playSnapshot.clear();
		if (const Scene* scene = _sceneManager->GetActiveScene())
		{
			const SceneSerializer::Result result = SceneSerializer::SaveToString(*scene, *_componentRegistry, _resources.get(), _playSnapshot);
			if (!result.ok)
			{
				EngineLog("[Play] Could not snapshot the scene (%s). Not entering play mode.", result.error.c_str());
				return;
			}
			if (result.skipped > 0)
			{
				EngineLog("[Play] WARNING: %u unregistered component(s) will not be restored on Stop.", result.skipped);
			}
		}

		_playState     = PlayState::Playing;
		_stepRequested = false;
		EngineLog("[Play] Play (snapshot %zu bytes)", _playSnapshot.size());
		return;
	}

	if (target == PlayState::Edit && _playState != PlayState::Edit)
	{
		_playState     = PlayState::Edit;
		_stepRequested = false;

		if (_playSnapshot.empty())
		{
			EngineLog("[Play] Stop (no snapshot to restore)");
			return;
		}

		// 재생 전 상태로 복원. 오브젝트 ID를 유지해서 에디터 선택과 ObjectRef가 끊기지 않게 한다.
		// 리소스(머티리얼 등)는 에셋이므로 되돌리지 않는다.
		SceneSerializer::LoadOptions options;
		options.preserveIds    = true;
		options.applyMaterials = false;

		const SceneSerializer::Result result = SceneSerializer::LoadFromString(*_sceneManager, *_componentRegistry, _resources.get(), _playSnapshot, options);
		_playSnapshot.clear();

		if (!result.ok)
		{
			EngineLog("[Play] Stop: restore failed (%s). Keeping the play-mode scene.", result.error.c_str());
			return;
		}
		EngineLog("[Play] Stop (restored objects=%u components=%u)", result.objects, result.components);
	}
}

void Engine::SetPaused(bool paused)
{
	if (_playState == PlayState::Playing && paused)
	{
		_playState = PlayState::Paused;
		EngineLog("[Play] Paused");
	}
	else if (_playState == PlayState::Paused && !paused)
	{
		_playState = PlayState::Playing;
		EngineLog("[Play] Resumed");
	}
}

void Engine::Step()
{
	if (_playState == PlayState::Playing)
	{
		_playState = PlayState::Paused;
	}
	if (_playState == PlayState::Paused)
	{
		_stepRequested = true;
	}
}

PlayState Engine::GetPlayState() const
{
	return _playState;
}

bool Engine::IsPlaying() const
{
	return _playState != PlayState::Edit;
}

bool Engine::IsSimulating() const
{
	return _playState == PlayState::Playing || (_playState == PlayState::Paused && _stepRequested);
}

namespace
{
	std::wstring Utf8ToWide(const std::string& utf8)
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
}

bool Engine::SaveScene(const std::string& assetPath)
{
	if (IsPlaying())
	{
		// 재생 중 상태(움직인 위치 등)가 저장되는 것을 막는다
		EngineLog("[Scene] Save refused during play mode. Stop first.");
		return false;
	}

	const Scene* scene = _sceneManager->GetActiveScene();
	if (scene == nullptr || _resources == nullptr || assetPath.empty())
	{
		EngineLog("[Scene] Save failed: no active scene or resource manager");
		return false;
	}

	const SceneSerializer::Result result = SceneSerializer::Save(*scene, *_componentRegistry, _resources.get(),
		Utf8ToWide(_resources->ResolveAssetPath(assetPath)));

	if (!result.ok)
	{
		EngineLog("[Scene] Save failed (%s): %s", assetPath.c_str(), result.error.c_str());
		return false;
	}

	_scenePath = assetPath;
	EngineLog("[Scene] Saved '%s' to %s: objects=%u components=%u skipped=%u",
		scene->GetName().c_str(), assetPath.c_str(), result.objects, result.components, result.skipped);
	return true;
}

bool Engine::LoadScene(const std::string& assetPath)
{
	if (_resources == nullptr || assetPath.empty())
	{
		return false;
	}

	if (const Scene* active = _sceneManager->GetActiveScene(); active != nullptr && active->IsUpdating())
	{
		EngineLog("[Scene] LoadScene(%s) called during scene update. Call it outside component Update.", assetPath.c_str());
		return false;
	}

	const SceneSerializer::Result result = SceneSerializer::Load(*_sceneManager, *_componentRegistry, _resources.get(),
		Utf8ToWide(_resources->ResolveAssetPath(assetPath)));

	if (!result.ok)
	{
		EngineLog("[Scene] Load failed (%s): %s. Keeping current scene.", assetPath.c_str(), result.error.c_str());
		return false;
	}

	_scenePath = assetPath;
	EngineLog("[Scene] Loaded '%s' from %s: objects=%u components=%u skipped=%u",
		_sceneManager->GetActiveScene()->GetName().c_str(), assetPath.c_str(), result.objects, result.components, result.skipped);
	return true;
}

void Engine::NewScene(const std::string& name)
{
	_sceneManager->CreateScene(name);
	_scenePath.clear();
	EngineLog("[Scene] New scene '%s'", name.c_str());
}

bool Engine::SceneFileExists(const std::string& assetPath) const
{
	if (_resources == nullptr || assetPath.empty())
	{
		return false;
	}
	const DWORD attributes = ::GetFileAttributesW(Utf8ToWide(_resources->ResolveAssetPath(assetPath)).c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

const std::string& Engine::GetScenePath() const
{
	return _scenePath;
}
