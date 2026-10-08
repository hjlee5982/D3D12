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
	_timer.Reset();

	if (!_device->Initialize(hWnd))
	{
		return false;
	}

	return true;
}

void Engine::EngineUpdate()
{
	_timer.Update();
}

void Engine::EngineRenderBegin()
{
	_device->RenderBegin();
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
	_device->Shutdown();
}

void Engine::SetTargetFPS(u32 fps)
{
	_timer.SetTargetFPS(fps);
}

u32 Engine::GetTargetFPS() const
{
	return _timer.GetTargetFPS();
}

f32 Engine::GetDeltaTime() const
{
	return _timer.GetDeltaTime();
}

f32 Engine::GetTotalTime() const
{
	return _timer.GetTotalTime();
}
