#pragma once

#include "EditorContext.h"
#include "RenderTexture11.h"

#include <string>
#include <vector>


// Game 창: 씬의 Camera 컴포넌트로 그린 화면 (재생 결과 확인용)
class GameViewPanel
{
public:
	explicit GameViewPanel(EditorContext& context) : _ctx(context) {}

	// 지난 프레임에 잰 창 크기로 텍스처를 준비해 바인딩하고 지운다. 창이 안 보이면(탭이 가려짐) false.
	bool Begin(const f32 clearColor[4]);
	u32  GetWidth() const  { return _gameTexture.GetWidth(); }
	u32  GetHeight() const { return _gameTexture.GetHeight(); }

	// sceneDockId: 처음 열릴 때 Scene 창과 같은 곳에 탭으로 붙인다
	void Draw(unsigned int sceneDockId);

private:
	EditorContext& _ctx;

	RenderTexture11 _gameTexture;
	u32 _gameViewWidth  = 0;
	u32 _gameViewHeight = 0;
};
