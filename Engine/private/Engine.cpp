#include "pch.h"
#include "Engine.h"
#include "Device11.h"
#include "Device12.h"

Engine::Engine(i32 version)
{
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
	if (!_device->Initialize(hWnd))
	{
		return false;
	}

	return true;
}

void Engine::EngineRenderBegin()
{
	_device->RenderBegin();
}

void Engine::EngineRenderEnd()
{
	_device->RenderEnd();
}

void Engine::EngineShutdown()
{
	_device->Shutdown();
}
