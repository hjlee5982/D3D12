#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <memory>

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
	void EngineRenderBegin();
	void EngineRenderEnd();
	void EngineShutdown();

private:
	std::unique_ptr<IDevice> _device;
};
