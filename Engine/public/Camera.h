#pragma once

#include "Component.h"
#include "EngineAPI.h"
#include "Types.h"

// 원근 카메라. 소유 오브젝트의 Transform(월드 행렬)의 역행렬이 뷰 행렬이 된다.
// 좌표계는 DirectX 기본인 왼손 좌표계(+Z가 화면 안쪽).
// 씬에서 처음 찾은 활성 카메라로 렌더링하며, 없으면 (0, 0, -5)에서 +Z를 보는 기본 카메라를 쓴다.
class ENGINE_API Camera : public Component
{
	COMPONENT_TYPE(Camera, Component)

public:
	Camera();
	~Camera() override;

	// AddComponent<Camera>()가 사용 (Engine.dll 안에서 생성)
	static Camera* CreateInstance();

	matx GetViewMatrix() const;
	matx GetProjectionMatrix(f32 aspectRatio) const;

	static void Reflect(TypeBuilder<Camera>& type);

public:
	f32 fovYDegrees = 60.0f;
	f32 nearZ       = 0.1f;
	f32 farZ        = 1000.0f;
};

ENGINE_COMPONENT(Camera);
