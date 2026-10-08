#pragma once

#include "GameAPI.h"

class GameHost
{
public:
	GameHost() = default;
	~GameHost();

	GameHost(const GameHost&) = delete;
	GameHost& operator=(const GameHost&) = delete;

	bool Load(const wchar_t* dllPath = L"Game.dll");
	void Unload();

	bool Initialize();
	void Update(f32 deltaTime);
	void LateUpdate(f32 deltaTime);
	void Shutdown();

	bool IsLoaded() const;

private:
	HMODULE            _module     = nullptr;
	PFN_GameInitialize _initialize = nullptr;
	PFN_GameUpdate     _update     = nullptr;
	PFN_GameLateUpdate _lateUpdate = nullptr;
	PFN_GameShutdown   _shutdown   = nullptr;
};
