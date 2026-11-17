#include "pch.h"
#include "Device11.h"

Device11::~Device11() = default;

bool Device11::Initialize(HWND hWnd)
{
	RECT clientRect{};
	::GetClientRect(hWnd, &clientRect);

	_width  = static_cast<u32>(clientRect.right  - clientRect.left);
	_height = static_cast<u32>(clientRect.bottom - clientRect.top);

	// Create Device And SwapChain
	{
		DXGI_SWAP_CHAIN_DESC swapChainDesc{};
		{
			// 플립 모델(FLIP_DISCARD)은 최소 2개의 버퍼가 필요하고 MSAA 스왑체인을 지원하지 않는다.
			swapChainDesc.BufferCount       = kBackBufferCount;
			swapChainDesc.BufferDesc.Width  = _width;
			swapChainDesc.BufferDesc.Height = _height;
			swapChainDesc.BufferDesc.Format = kBackBufferFormat;
			swapChainDesc.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swapChainDesc.OutputWindow      = hWnd;
			swapChainDesc.SampleDesc.Count  = 1;
			swapChainDesc.Windowed          = TRUE;
			swapChainDesc.SwapEffect        = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		}

		UINT createFlags = 0;
#if defined(_DEBUG)
		createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

		auto createDevice = [&](UINT flags)
		{
			return ::D3D11CreateDeviceAndSwapChain(
				nullptr,
				D3D_DRIVER_TYPE_HARDWARE,
				nullptr,
				flags,
				nullptr,
				0,
				D3D11_SDK_VERSION,
				&swapChainDesc,
				_swapChain.GetAddressOf(),
				_device.GetAddressOf(),
				nullptr,
				_context.GetAddressOf());
		};

		HRESULT hr = createDevice(createFlags);

		// 디버그 레이어(Graphics Tools)가 설치되어 있지 않으면 디버그 플래그 없이 다시 시도
		if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING && (createFlags & D3D11_CREATE_DEVICE_DEBUG))
		{
			::OutputDebugStringW(L"[Engine] D3D11 debug layer not available. Continuing without it.\n");
			hr = createDevice(createFlags & ~D3D11_CREATE_DEVICE_DEBUG);
		}

		if (FAILED(hr))
		{
			return false;
		}
	}

	if (!CreateRenderTarget() || !CreateDepthBuffer())
	{
		Shutdown();
		return false;
	}

	SetViewport();
	return true;
}

void Device11::Resize(u32 width, u32 height)
{
	// 최소화(0x0) 또는 초기화 전 호출은 무시
	if (_swapChain == nullptr || width == 0 || height == 0)
	{
		return;
	}

	if (width == _width && height == _height)
	{
		return;
	}

	// 백버퍼를 참조하는 뷰를 모두 해제해야 ResizeBuffers가 성공한다.
	_context->OMSetRenderTargets(0, nullptr, nullptr);
	_renderTargetView.Reset();
	_depthStencilView.Reset();
	_depthBuffer.Reset();
	_context->Flush();

	const HRESULT hr = _swapChain->ResizeBuffers(kBackBufferCount, width, height, kBackBufferFormat, 0);
	if (FAILED(hr))
	{
		::OutputDebugStringW(L"[Engine] ResizeBuffers failed.\n");
		return;
	}

	_width  = width;
	_height = height;

	if (!CreateRenderTarget() || !CreateDepthBuffer())
	{
		::OutputDebugStringW(L"[Engine] Failed to recreate render target after resize.\n");
		return;
	}

	SetViewport();
}

void Device11::RenderBegin()
{
	if (_context == nullptr || _swapChain == nullptr || _renderTargetView == nullptr)
	{
		return;
	}

	// 플립 모델은 Present 후 렌더 타깃 바인딩이 풀리므로 매 프레임 다시 바인딩한다.
	// 리사이즈/ClearState 이후에도 항상 유효하도록 뷰포트도 매 프레임 설정한다.
	BindBackBuffer();

	constexpr f32 clearColor[4] = { 0.1f, 0.3f, 0.9f, 1.0f };
	_context->ClearRenderTargetView(_renderTargetView.Get(), clearColor);
	if (_depthStencilView)
	{
		_context->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	}
}

void Device11::BindBackBuffer()
{
	if (_context == nullptr || _renderTargetView == nullptr)
	{
		return;
	}

	_context->OMSetRenderTargets(1, _renderTargetView.GetAddressOf(), _depthStencilView.Get());
	SetViewport();
}

void Device11::RenderEnd()
{
	if (_swapChain == nullptr)
	{
		return;
	}

	_swapChain->Present(_vsync ? 1 : 0, 0);
}

void Device11::Shutdown()
{
	if (_context)
	{
		_context->OMSetRenderTargets(0, nullptr, nullptr);
		_context->ClearState();
		_context->Flush();
	}

	_renderTargetView.Reset();
	_depthStencilView.Reset();
	_depthBuffer.Reset();
	_swapChain.Reset();
	_context.Reset();
	_device.Reset();
}

bool Device11::CreateRenderTarget()
{
	ComPtr<ID3D11Texture2D> backBuffer;
	if (FAILED(_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()))))
	{
		return false;
	}

	return SUCCEEDED(_device->CreateRenderTargetView(backBuffer.Get(), nullptr, _renderTargetView.GetAddressOf()));
}

bool Device11::CreateDepthBuffer()
{
	D3D11_TEXTURE2D_DESC desc{};
	{
		desc.Width            = _width;
		desc.Height           = _height;
		desc.MipLevels        = 1;
		desc.ArraySize        = 1;
		desc.Format           = kDepthFormat;
		desc.SampleDesc.Count = 1;
		desc.Usage            = D3D11_USAGE_DEFAULT;
		desc.BindFlags        = D3D11_BIND_DEPTH_STENCIL;
	}

	if (FAILED(_device->CreateTexture2D(&desc, nullptr, _depthBuffer.ReleaseAndGetAddressOf())))
	{
		return false;
	}

	return SUCCEEDED(_device->CreateDepthStencilView(_depthBuffer.Get(), nullptr, _depthStencilView.ReleaseAndGetAddressOf()));
}

void Device11::SetViewport()
{
	D3D11_VIEWPORT viewport{};
	{
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width    = static_cast<f32>(_width);
		viewport.Height   = static_cast<f32>(_height);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
	}
	_context->RSSetViewports(1, &viewport);
}
