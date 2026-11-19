#pragma once

#include "EditorContext.h"
#include "RenderTexture11.h"
#include "SceneViewport.h"
#include "TransformGizmo.h"

#include <string>
#include <vector>


// Scene 창: 씬을 화면 밖 텍스처로 그려 표시. 에디터 카메라 조작, 클릭 선택, 선택 외곽선, 기즈모
class SceneViewPanel
{
public:
	explicit SceneViewPanel(EditorContext& context) : _ctx(context) {}

	// 지난 프레임에 잰 창 크기로 텍스처를 준비해 바인딩하고 지운다. 창이 안 보이면 false.
	bool Begin(const f32 clearColor[4]);
	u32  GetWidth() const  { return _sceneTexture.GetWidth(); }
	u32  GetHeight() const { return _sceneTexture.GetHeight(); }

	// 에디터 카메라 행렬. 창이 안 보이면 false.
	bool GetCamera(matx& view, matx& projection) const;

	void Draw();

	// Scene 창이 도킹된 노드 (Game 창을 처음 열 때 같은 곳에 탭으로 붙이는 데 사용)
	unsigned int GetDockId() const { return _sceneDockId; }

private:
	EditorContext& _ctx;

	void DrawToolbar();
	void UpdateCamera(bool hovered);
	void PickObject(f32 mouseX, f32 mouseY);
	void DrawSelectionOutline();
	void GetDisplayedCamera(matx& view, matx& projection) const; // 텍스처가 아직 없으면 씬 카메라
	vec3 GetCameraForward() const;
	SceneViewport MakeViewport() const;

	RenderTexture11 _sceneTexture;
	u32  _sceneViewWidth   = 0; // 이번 프레임에 잰 내용 영역 크기 (다음 프레임 렌더에 사용)
	u32  _sceneViewHeight  = 0;
	bool _sceneViewHovered = false;
	unsigned int _sceneDockId = 0;
	f32  _sceneRect[4]{};       // 이미지의 화면 좌표 (minX, minY, maxX, maxY)

	// 에디터 카메라 (씬과 별개. 씬 파일에 저장되지 않는다)
	vec3 _cameraPosition{ 0.0f, 1.0f, -5.0f };
	f32  _cameraYaw   = 0.0f;  // 라디안. +는 오른쪽
	f32  _cameraPitch = 0.2f;  // 라디안. +는 아래
	f32  _cameraSpeed = 4.0f;  // 단위/초
	f32  _cameraFovY  = 60.0f; // 도
	bool _navigating  = false; // 오른쪽/가운데 버튼 드래그 중

	TransformGizmo _gizmo;
};
