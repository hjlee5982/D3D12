#pragma once

class IDevice abstract
{
public:
	virtual ~IDevice();
public:
	virtual bool Initialize(HWND hWnd) = 0;
	virtual void RenderBegin()         = 0;
	virtual void RenderEnd()           = 0;
	virtual void Shutdown()            = 0;
};

