#pragma once

#include "IDevice.h"

class Device12 : public IDevice
{
public:
	virtual ~Device12();
public:
	bool Initialize(HWND hWnd)          override;
	void Resize(u32 width, u32 height) override;
	void RenderBegin()                 override;
	void RenderEnd()                   override;
	void Shutdown()                    override;
};
