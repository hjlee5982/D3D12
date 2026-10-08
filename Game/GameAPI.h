#pragma once

#include "Types.h"

extern "C"
{
	using PFN_GameInitialize = bool (*)();
	using PFN_GameUpdate     = void (*)(f32 deltaTime);
	using PFN_GameLateUpdate = void (*)(f32 deltaTime);
	using PFN_GameShutdown   = void (*)();
}

#if defined(GAME_EXPORTS)
#define GAME_API extern "C" __declspec(dllexport)

GAME_API bool GameInitialize();
GAME_API void GameUpdate(f32 deltaTime);
GAME_API void GameLateUpdate(f32 deltaTime);
GAME_API void GameShutdown();
#endif
