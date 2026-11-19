#pragma once

#include "EditorUtils.h"
#include "Types.h"

// Scene 창의 화면 영역과 그때의 카메라 행렬. 피킹/기즈모가 화면 좌표 ↔ 월드 광선을 계산할 때 쓴다.
struct SceneViewport
{
	f32  rect[4]{}; // 이미지의 화면 좌표 (minX, minY, maxX, maxY)
	matx view;
	matx projection;

	f32 Width() const  { return rect[2] - rect[0]; }
	f32 Height() const { return rect[3] - rect[1]; }

	// 마우스 위치 → 월드 공간의 가까운 평면 위 점(origin) → 먼 평면 위 점까지의 선분(direction)
	bool MouseRay(f32 mouseX, f32 mouseY, vec3& origin, vec3& direction) const
	{
		if (Width() <= 0.0f || Height() <= 0.0f)
		{
			return false;
		}

		const matx inverseViewProjection = (view * projection).Invert();
		const f32 ndcX = (mouseX - rect[0]) / Width() * 2.0f - 1.0f;
		const f32 ndcY = 1.0f - (mouseY - rect[1]) / Height() * 2.0f;
		origin    = EditorUtil::TransformPoint(vec3(ndcX, ndcY, 0.0f), inverseViewProjection);
		direction = EditorUtil::TransformPoint(vec3(ndcX, ndcY, 1.0f), inverseViewProjection) - origin;
		return true;
	}
};
