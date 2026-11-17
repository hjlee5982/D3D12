#pragma once

#include "Types.h"

// 화면 밖 렌더 타깃 (색 + 깊이). 에디터 Scene 창처럼 씬을 텍스처로 그린 뒤 다른 곳에 표시할 때 쓴다.
// 색 버퍼는 셰이더 리소스로도 쓸 수 있다 (ImGui::Image 등).
class RenderTexture11
{
public:
	// 크기가 바뀌었을 때만 다시 만든다. 0이면 해제.
	bool Resize(ID3D11Device* device, u32 width, u32 height);
	void Release();

	// 렌더 타깃/깊이/뷰포트를 바인딩하고 지운다
	void Begin(ID3D11DeviceContext* context, const f32 clearColor[4]);

	bool IsValid() const { return _renderTargetView != nullptr; }
	u32  GetWidth() const { return _width; }
	u32  GetHeight() const { return _height; }
	ID3D11ShaderResourceView* GetShaderResourceView() const { return _shaderResourceView.Get(); }

private:
	ComPtr<ID3D11Texture2D>          _color;
	ComPtr<ID3D11RenderTargetView>   _renderTargetView;
	ComPtr<ID3D11ShaderResourceView> _shaderResourceView;
	ComPtr<ID3D11Texture2D>          _depth;
	ComPtr<ID3D11DepthStencilView>   _depthStencilView;

	u32 _width  = 0;
	u32 _height = 0;
};
