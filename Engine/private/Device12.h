#pragma once

#include "IDevice.h"

class Device12 : public IDevice
{
public:
	virtual ~Device12();
public:
	bool Initialize(HWND hWnd) override;
	void RenderBegin()         override;
	void RenderEnd()           override;
	void Shutdown()            override;
};

