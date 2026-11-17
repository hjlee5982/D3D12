#pragma once

#include "Types.h"

class IDevice abstract
{
public:
	virtual ~IDevice();
public:
	virtual bool Initialize(HWND hWnd)          = 0;
	virtual void Resize(u32 width, u32 height) = 0;
	virtual void RenderBegin()                 = 0;
	virtual void RenderEnd()                   = 0;
	virtual void Shutdown()                    = 0;

	void SetVSync(bool enabled) { _vsync = enabled; }
	bool IsVSyncEnabled() const { return _vsync; }

protected:
	bool _vsync = false;
};
