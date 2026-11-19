#pragma once

#include "EditorContext.h"
#include "SceneViewport.h"
#include "Types.h"

// 선택한 오브젝트를 이동/회전시키는 Scene 창 기즈모.
//  - 이동: X/Y/Z 축 화살표 + 평면 손잡이 3개, 회전: X/Y/Z 링
//  - Ctrl: 스냅 (0.25 단위 / 15도), Local/World 축
//  - 화면에서 항상 같은 크기, 부모가 있으면 월드 값 → 로컬 값으로 바꿔 적용
class TransformGizmo
{
public:
	enum class Mode : u8 { Translate, Rotate };

	// 매 프레임 Scene 이미지를 그린 뒤 호출 (손잡이 판정, 드래그 적용, 그리기)
	bool Update(EditorContext& ctx, const SceneViewport& viewport, bool hovered, bool navigating);

	Mode GetMode() const    { return _mode; }
	void SetMode(Mode mode) { _mode = mode; }
	bool IsLocal() const    { return _local; }
	void SetLocal(bool local) { _local = local; }

	bool IsDragging() const    { return _active >= 0; }
	bool ConsumedClick() const { return _consumedClick; } // 이번 클릭을 기즈모가 썼으면 선택 클릭으로 처리하지 않는다
	void ClearConsumedClick()  { _consumedClick = false; }

private:
	Mode _mode  = Mode::Translate;
	bool _local = false; // true: 오브젝트 축, false: 월드 축
	int  _hover  = -1;   // 이동: 0~2 축, 3~5 평면(법선 축 번호 + 3). 회전: 0~2 링
	int  _active = -1;   // 드래그 중인 핸들
	bool _consumedClick = false;
	vec3 _axes[3];        // 드래그 시작 시점의 축 (드래그 중 고정)
	vec3 _startPosition;  // 드래그 시작 시 월드 위치
	quat _startRotation;  // 드래그 시작 시 월드 회전
	vec3 _startHit;       // 시작 시 마우스 광선과 축/평면의 교점
	f32  _startParam = 0.0f;
	f32  _dragValue  = 0.0f; // 표시용 (이동 거리 또는 회전 각도)
};
