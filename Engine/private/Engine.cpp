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
	}

	return true;
}

void Engine::EngineUpdate()
{
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
		if (_renderer && _editor->BeginSceneView(kSceneClearColor))
		{
			// 에디터 카메라 모드면 그 행렬로, 게임 카메라 모드면 씬의 카메라로 그린다
			Renderer11::CameraMatrices camera;
			const bool useEditorCamera = _editor->GetSceneViewCamera(camera.view, camera.projection);
			_renderer->Render(scene, *_resources, _editor->GetSceneViewWidth(), _editor->GetSceneViewHeight(),
				useEditorCamera ? &camera : nullptr);
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
	if (!_scenePaused)
	{
		_sceneManager->Update(_timer.GetDeltaTime());
	}
}

void Engine::EngineSceneLateUpdate()
{
	if (!_scenePaused)
	{
		_sceneManager->LateUpdate(_timer.GetDeltaTime());
	}
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

void Engine::SetScenePaused(bool paused)
{
	_scenePaused = paused;
}

bool Engine::IsScenePaused() const
{
	return _scenePaused;
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
