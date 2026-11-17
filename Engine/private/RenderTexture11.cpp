#include "pch.h"
#include "RenderTexture11.h"
#include "Log.h"

bool RenderTexture11::Resize(ID3D11Device* device, u32 width, u32 height)
{
	if (width == _width && height == _height && IsValid())
	{
		return true;
	}

	Release();
	if (device == nullptr || width == 0 || height == 0)
	{
		return false;
	}

	// 색: 백 버퍼와 같은 형식 (감마 처리가 같도록), 렌더 타깃 + 셰이더 리소스
	D3D11_TEXTURE2D_DESC colorDesc{};
	colorDesc.Width            = width;
	colorDesc.Height           = height;
	colorDesc.MipLevels        = 1;
	colorDesc.ArraySize        = 1;
	colorDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
	colorDesc.SampleDesc.Count = 1;
	colorDesc.Usage            = D3D11_USAGE_DEFAULT;
	colorDesc.BindFlags        = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	D3D11_TEXTURE2D_DESC depthDesc = colorDesc;
	depthDesc.Format    = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

	if (FAILED(device->CreateTexture2D(&colorDesc, nullptr, _color.GetAddressOf())) ||
		FAILED(device->CreateRenderTargetView(_color.Get(), nullptr, _renderTargetView.GetAddressOf())) ||
		FAILED(device->CreateShaderResourceView(_color.Get(), nullptr, _shaderResourceView.GetAddressOf())) ||
		FAILED(device->CreateTexture2D(&depthDesc, nullptr, _depth.GetAddressOf())) ||
		FAILED(device->CreateDepthStencilView(_depth.Get(), nullptr, _depthStencilView.GetAddressOf())))
	{
		EngineLog("[Render] Failed to create render texture %ux%u", width, height);
		Release();
		return false;
	}

	_width  = width;
	_height = height;
	return true;
}

void RenderTexture11::Release()
{
	_depthStencilView.Reset();
	_depth.Reset();
	_shaderResourceView.Reset();
	_renderTargetView.Reset();
	_color.Reset();
	_width  = 0;
	_height = 0;
}

void RenderTexture11::Begin(ID3D11DeviceContext* context, const f32 clearColor[4])
{
	if (!IsValid())
	{
		return;
	}

	// 지난 프레임에 이 텍스처를 셰이더 입력(ImGui 이미지)으로 썼다면 먼저 풀어 준다.
	// (입력으로 바인딩된 채 렌더 타깃으로 쓰면 런타임이 강제로 풀고 디버그 경고를 낸다)
	ID3D11ShaderResourceView* nullViews[1] = { nullptr };
	context->PSSetShaderResources(0, 1, nullViews);

	context->OMSetRenderTargets(1, _renderTargetView.GetAddressOf(), _depthStencilView.Get());
	context->ClearRenderTargetView(_renderTargetView.Get(), clearColor);
	context->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	D3D11_VIEWPORT viewport{};
	viewport.Width    = static_cast<f32>(_width);
	viewport.Height   = static_cast<f32>(_height);
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);
}
