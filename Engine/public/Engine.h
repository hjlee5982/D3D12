#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <memory>

#include "Timer.h"
#include "Types.h"

class IDevice;

class Engine
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
	void EngineRenderEnd();
	void EngineEndFrame();
	void EngineShutdown();

	void SetTargetFPS(u32 fps);
	u32 GetTargetFPS() const;

	f32 GetDeltaTime() const;
	f32 GetTotalTime() const;

private:
	std::unique_ptr<IDevice> _device;
	Timer _timer;
};
