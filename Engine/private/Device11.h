#pragma once

#include "IDevice.h"

class Device11 : public IDevice
{
public:
	virtual ~Device11();
public:
	virtual bool Initialize(HWND hWnd) override;
	virtual void RenderBegin()         override;
	virtual void RenderEnd()           override;
	virtual void Shutdown()            override;

private:
	ComPtr<ID3D11Device>           _device;
	ComPtr<ID3D11DeviceContext>    _context;
	ComPtr<IDXGISwapChain>         _swapChain;
	ComPtr<ID3D11RenderTargetView> _renderTargetView;
};
