#pragma once

#include "Component.h"
#include "ComponentRegistry.h"
#include "Handle.h"

// 예제 게임 컴포넌트: target 오브젝트의 월드 위치(+offset)를 부드럽게 따라간다.
//
// 다른 오브젝트를 GameObject* 대신 ObjectRef로 저장하는 예시.
//  - target이 삭제되면 target.Get()이 nullptr → 따라가기를 멈출 뿐 크래시하지 않는다.
//  - ObjectRef는 ID 값이라 Reflect에 등록하면 Game.dll 리로드 후에도 같은 대상을 가리킨다.
class Follower : public Component
{
	COMPONENT_TYPE(Follower, Component)

public:
	Follower() = default;
	Follower(ObjectRef target, const vec3& offset);

	void LateUpdate(f32 deltaTime) override;

	static void Reflect(TypeBuilder<Follower>& type)
	{
		type.Field("target", &Follower::target)
			.Field("offset", &Follower::offset)
			.Field("sharpness", &Follower::sharpness);
	}

public:
	ObjectRef target;
	vec3      offset{ 0.0f, 0.0f, 0.0f };
	f32       sharpness = 6.0f; // 클수록 빠르게 따라붙는다

private:
	bool _reportedLost = false; // 리플렉션 안 함 → 리로드 후 기본값
};
