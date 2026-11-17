#pragma once

#include "Component.h"
#include "ComponentRegistry.h"

// 예제 게임 컴포넌트: 소유 오브젝트를 Y축으로 회전시킨다.
// Game.dll에 정의되어 있으므로 리로드 때 직렬화 → 제거 → 새 DLL에서 복원된다.
// Reflect에 등록한 필드만 리로드 후에도 값이 유지된다.
class Spinner : public Component
{
	COMPONENT_TYPE(Spinner, Component)

public:
	Spinner() = default;
	explicit Spinner(f32 degreesPerSecond);

	void Update(f32 deltaTime) override;

	static void Reflect(TypeBuilder<Spinner>& type)
	{
		type.Field("degreesPerSecond", &Spinner::degreesPerSecond)
			.Field("totalDegrees", &Spinner::totalDegrees);
	}

public:
	f32 degreesPerSecond = 90.0f;
	f32 totalDegrees     = 0.0f; // 지금까지 회전한 누적 각도 (런타임 상태 예시)
};
