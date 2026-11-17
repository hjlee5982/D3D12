#include "pch.h"
#include "GameAPI.h"

#include "Camera.h"
#include "ComponentRegistry.h"
#include "Engine.h"
#include "Follower.h"
#include "GameObject.h"
#include "Log.h"
#include "MeshRenderer.h"
#include "ResourceManager.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Spinner.h"
#include "Transform.h"

#include <cmath>
#include <exception>

namespace
{
	// Game.dll 전역: 리로드 때 초기화된다. Engine 포인터처럼 다시 얻을 수 있는 것만 둔다.
	Engine* g_engine = nullptr;

	// 에디터에서 File > Save As로 이 경로에 저장하면 다음 실행부터 이 씬으로 시작한다
	constexpr const char* kMainScenePath = "Scene/Main.scene";

	// extern "C" 경계 밖으로 예외가 나가지 않도록 감싼다.
	template <typename Fn>
	bool Guard(const char* where, Fn&& fn)
	{
		try
		{
			fn();
			return true;
		}
		catch (const std::exception& e)
		{
			EngineLog("[Game] Exception in %s: %s", where, e.what());
		}
		catch (...)
		{
			EngineLog("[Game] Exception in %s: unknown exception", where);
		}
		return false;
	}

	f32 GetYawDegrees(const Transform* transform)
	{
		const quat q = transform->GetRotation();
		const f32 yaw = std::atan2(2.0f * (q.w * q.y + q.x * q.z), 1.0f - 2.0f * (q.y * q.y + q.x * q.x));
		return DirectX::XMConvertToDegrees(yaw);
	}

	// 최초 1회: 씬과 오브젝트 생성 (Engine 소유이므로 리로드 후에도 남는다)
	bool BuildInitialScene(SceneManager& sceneManager, ResourceManager* resources)
	{
		Scene* scene = sceneManager.CreateScene("Main");
		if (scene == nullptr)
		{
			return false;
		}

		GameObject* parent = scene->CreateGameObject("Parent");
		GameObject* child  = scene->CreateGameObject("Child");
		if (parent == nullptr || child == nullptr)
		{
			return false;
		}

		child->transform->SetParent(parent->transform);
		parent->AddComponent<Spinner>(90.0f);
		child->transform->SetPosition(1.5f, 0.0f, 0.0f);

		// 카메라: (0, 0, -4)에서 +Z 방향을 본다
		GameObject* camera = scene->CreateGameObject("MainCamera");
		camera->transform->SetPosition(0.0f, 0.0f, -4.0f);
		camera->AddComponent<Camera>();

		// 텍스처 사각형: Child를 따라다닌다 (포인터가 아니라 ObjectRef로 참조)
		if (resources != nullptr)
		{
			const ShaderHandle   shader   = resources->LoadShader("Shader/Default.hlsl");
			const TextureHandle  texture  = resources->LoadTexture("Texture/Proto.png");
			const MaterialHandle material = resources->CreateMaterial("Proto", shader, texture);

			GameObject* quad = scene->CreateGameObject("Quad");
			quad->AddComponent<MeshRenderer>(resources->GetQuadMesh(), material);
			quad->AddComponent<Follower>(ObjectRef(child), vec3(0.0f, 0.0f, 0.0f));

			if (!shader || !texture)
			{
				EngineLog("[Game] Quad resources missing (shader=%d texture=%d)", shader ? 1 : 0, texture ? 1 : 0);
			}
		}
		return true;
	}

	void LogSpinnerState(const char* when, Scene& scene)
	{
		const GameObject* parent   = scene.FindGameObject("Parent");
		const Spinner*    spinner  = parent != nullptr ? parent->GetComponent<Spinner>() : nullptr;
		const GameObject* quad     = scene.FindGameObject("Quad");
		const Follower*   follower = quad != nullptr ? quad->GetComponent<Follower>() : nullptr;
		const GameObject* target   = follower != nullptr ? follower->target.Get() : nullptr;

		EngineLog("[Game] %s: objects=%u parentYaw=%.1f spinner={ degreesPerSecond=%.1f totalDegrees=%.1f } "
			"follower={ target=#%llu '%s' }",
			when,
			scene.GetGameObjectCount(),
			parent != nullptr ? GetYawDegrees(parent->transform) : 0.0f,
			spinner != nullptr ? spinner->degreesPerSecond : -1.0f,
			spinner != nullptr ? spinner->totalDegrees : -1.0f,
			follower != nullptr ? static_cast<unsigned long long>(follower->target.id) : 0ull,
			target != nullptr ? target->GetName().c_str() : "(none)");
	}
}

// 매 로드마다(리로드 포함) 가장 먼저 호출된다. Game 컴포넌트 타입을 등록한다.
GAME_API void GameRegisterTypes(ComponentRegistry* registry)
{
	if (registry == nullptr)
	{
		return;
	}

	Guard("GameRegisterTypes", [&]
	{
		registry->Register<Spinner>();
		registry->Register<Follower>();
	});
}

GAME_API bool GameLoad(Engine* engine, bool reloaded)
{
	if (engine == nullptr)
	{
		return false;
	}

	bool result = false;
	Guard("GameLoad", [&]
	{
		g_engine = engine;
		SceneManager& sceneManager = engine->GetSceneManager();

		// 최초 실행: 저장된 씬 파일이 있으면 불러오고(에디터에서 편집한 결과), 없으면 코드로 만든다.
		// (리로드 때는 씬이 Engine에 그대로 있으므로 둘 다 하지 않는다)
		if (!sceneManager.HasActiveScene())
		{
			const bool loaded = engine->SceneFileExists(kMainScenePath) && engine->LoadScene(kMainScenePath);
			if (!loaded && !BuildInitialScene(sceneManager, engine->GetResourceManager()))
			{
				return;
			}
		}

		// 리로드일 때 Game 컴포넌트는 GameLoad 전에 호스트가 스냅샷으로 이미 복원해 두었다.
		Scene* scene = sceneManager.GetActiveScene();
		LogSpinnerState(reloaded ? "GameLoad(reloaded=1)" : "GameLoad(reloaded=0)", *scene);

		result = true;
	});

	if (!result)
	{
		g_engine = nullptr;
	}
	return result;
}

GAME_API void GameUpdate(f32 deltaTime)
{
	// 게임 전역 로직 (씬 오브젝트 업데이트는 Engine이 수행)
}

GAME_API void GameLateUpdate(f32 deltaTime)
{
}

GAME_API void GameUnload(bool shutdown)
{
	Guard("GameUnload", [&]
	{
		if (g_engine != nullptr)
		{
			if (Scene* scene = g_engine->GetSceneManager().GetActiveScene())
			{
				LogSpinnerState(shutdown ? "GameUnload(shutdown=1)" : "GameUnload(shutdown=0)", *scene);
			}
		}
	});

	g_engine = nullptr;
}

GAME_API void GameGetBuildInfo(BuildInfo* out)
{
	if (out != nullptr)
	{
		*out = MakeBuildInfo();
	}
}
