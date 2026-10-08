#include "pch.h"
#include "GameHost.h"

GameHost::~GameHost()
{
	Unload();
}

bool GameHost::Load(const wchar_t* dllPath)
{
	Unload();

	_module = LoadLibraryW(dllPath);
	if (_module == nullptr)
	{
		return false;
	}

	_initialize = reinterpret_cast<PFN_GameInitialize>(GetProcAddress(_module, "GameInitialize"));
	_update     = reinterpret_cast<PFN_GameUpdate>    (GetProcAddress(_module, "GameUpdate"));
	_lateUpdate = reinterpret_cast<PFN_GameLateUpdate>(GetProcAddress(_module, "GameLateUpdate"));
	_shutdown   = reinterpret_cast<PFN_GameShutdown>  (GetProcAddress(_module, "GameShutdown"));

	if (_initialize == nullptr || _update == nullptr || _lateUpdate == nullptr || _shutdown == nullptr)
	{
		Unload();
		return false;
	}

	return true;
}

void GameHost::Unload()
{
	if (_module != nullptr)
	{
		FreeLibrary(_module);
		_module = nullptr;
	}

	_initialize = nullptr;
	_update     = nullptr;
	_lateUpdate = nullptr;
	_shutdown   = nullptr;
}

bool GameHost::Initialize()
{
	return _initialize != nullptr && _initialize();
}

void GameHost::Update(f32 deltaTime)
{
	if (_update != nullptr)
	{
		_update(deltaTime);
	}
}

void GameHost::LateUpdate(f32 deltaTime)
{
	if (_lateUpdate != nullptr)
	{
		_lateUpdate(deltaTime);
	}
}

void GameHost::Shutdown()
{
	if (_shutdown != nullptr)
	{
		_shutdown();
	}
}

bool GameHost::IsLoaded() const
{
	return _module != nullptr;
}
