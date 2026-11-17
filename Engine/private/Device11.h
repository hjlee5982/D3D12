#pragma once

#include "IDevice.h"

class Device11 : public IDevice
{
public:
	virtual ~Device11();
public:
	virtual bool Initialize(HWND hWnd)          override;
	virtual void Resize(u32 width, u32 height) override;
	virtual void RenderBegin()                 override;
	virtual void RenderEnd()                   override;
	virtual void Shutdown()                    override;

	ID3D11Device*        GetDevice()  const { return _device.Get(); }
	ID3D11DeviceContext* GetContext() const { return _context.Get(); }
	u32                  GetWidth()   const { return _width; }
	u32                  GetHeight()  const { return _height; }

	// 백 버퍼(+깊이)와 창 크기 뷰포트를 바인딩한다. 화면 밖 텍스처에 그린 뒤 창으로 돌아올 때 사용.
	void BindBackBuffer();

private:
	bool CreateRenderTarget();
	bool CreateDepthBuffer();
	void SetViewport();

private:
	static constexpr u32         kBackBufferCount  = 2;
	static constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	static constexpr DXGI_FORMAT kDepthFormat      = DXGI_FORMAT_D24_UNORM_S8_UINT;

	ComPtr<ID3D11Device>           _device;
	ComPtr<ID3D11DeviceContext>    _context;
	ComPtr<IDXGISwapChain>         _swapChain;
	ComPtr<ID3D11RenderTargetView> _renderTargetView;
	ComPtr<ID3D11Texture2D>        _depthBuffer;
	ComPtr<ID3D11DepthStencilView> _depthStencilView;

	u32 _width  = 0;
	u32 _height = 0;
};
