#include "pch.h"
#include "GameAPI.h"
#include "SceneManager.h"

namespace
{
	std::unique_ptr<SceneManager> g_sceneManager;
}

GAME_API bool GameInitialize()
{
	g_sceneManager = std::make_unique<SceneManager>();
	if (!g_sceneManager)
	{
		return false;
	}
	g_sceneManager->CreateScene("Main");


	return true;
}

GAME_API void GameUpdate(f32 deltaTime)
{
	if (g_sceneManager)
	{
		g_sceneManager->Update(deltaTime);
	}
}

GAME_API void GameLateUpdate(f32 deltaTime)
{
	if (g_sceneManager)
	{
		g_sceneManager->LateUpdate(deltaTime);
	}
}

GAME_API void GameShutdown()
{
	g_sceneManager.reset();
}
