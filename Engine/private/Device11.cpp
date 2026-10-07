#include "pch.h"
#include "Device11.h"

Device11::~Device11() = default;

bool Device11::Initialize(HWND hWnd)
{
    RECT clientRect{};
    ::GetClientRect(hWnd, &clientRect);

    const u32 width  = static_cast<u32>(clientRect.right  - clientRect.left);
    const u32 height = static_cast<u32>(clientRect.bottom - clientRect.top);

    // Create Device And SwapChain
	{
		DXGI_SWAP_CHAIN_DESC swapChainDesc{};
		{
			swapChainDesc.BufferCount                        = 1;
			swapChainDesc.BufferDesc.Width                   = width;
			swapChainDesc.BufferDesc.Height                  = height;
			swapChainDesc.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
			swapChainDesc.BufferDesc.RefreshRate.Numerator   = 60;
			swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
			swapChainDesc.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swapChainDesc.OutputWindow                       = hWnd;
			swapChainDesc.SampleDesc.Count                   = 1;
			swapChainDesc.SampleDesc.Quality                 = 0;
			swapChainDesc.Windowed                           = TRUE;
		}

		HRESULT hr = ::D3D11CreateDeviceAndSwapChain
		(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			0,
			nullptr,
			0,
			D3D11_SDK_VERSION,
			&swapChainDesc,
			_swapChain.GetAddressOf(),
			_device.GetAddressOf(),
			nullptr,
			_context.GetAddressOf()
		);

		if (FAILED(hr))
		{
			return false;
		}
	}

	// Create BackBuffer And RenderTargetView
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
		if (FAILED(_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()))))
		{
			Shutdown();
			return false;
		}

		const HRESULT rtvResult = _device->CreateRenderTargetView(backBuffer.Get(), nullptr, _renderTargetView.GetAddressOf());
		if (FAILED(rtvResult))
		{
			Shutdown();
			return false;
		}
	}

	// Set Viewport
	{
		D3D11_VIEWPORT viewport{};
		{
			viewport.TopLeftX = 0.0f;
			viewport.TopLeftY = 0.0f;
			viewport.Width    = static_cast<f32>(width);
			viewport.Height   = static_cast<f32>(height);
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;
		}
		_context->RSSetViewports(1, &viewport);
	}

    return true;
}

void Device11::RenderBegin()
{
	if (_context == nullptr || _swapChain == nullptr || _renderTargetView == nullptr)
	{
		return;
	}

	_context->OMSetRenderTargets(1, _renderTargetView.GetAddressOf(), nullptr);

	constexpr f32 clearColor[4] = { 0.1f, 0.3f, 0.9f, 1.0f };
	_context->ClearRenderTargetView(_renderTargetView.Get(), clearColor);
}

void Device11::RenderEnd()
{
	_swapChain->Present(1, 0);
}

void Device11::Shutdown()
{
	if (_context)
	{
		_context->OMSetRenderTargets(0, nullptr, nullptr);
	}

	_renderTargetView.Reset();
	_swapChain.Reset();
	_context.Reset();
	_device.Reset();
}
