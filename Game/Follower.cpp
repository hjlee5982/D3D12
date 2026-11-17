#include "pch.h"
#include "Follower.h"

#include "GameObject.h"
#include "Log.h"
#include "Transform.h"

#include <cmath>

Follower::Follower(ObjectRef target, const vec3& offset)
	: target(target)
	, offset(offset)
{
}

void Follower::LateUpdate(f32 deltaTime)
{
	Transform* transform = GetOwner() != nullptr ? GetOwner()->transform : nullptr;
	if (transform == nullptr)
	{
		return;
	}

	const GameObject* targetObject = target.Get();
	if (targetObject == nullptr)
	{
		if (target.IsSet() && !_reportedLost)
		{
			EngineLog("[Game] Follower on '%s': target #%llu is gone. Stopped following.",
				GetOwner()->GetName().c_str(), static_cast<unsigned long long>(target.id));
			_reportedLost = true;
		}
		return;
	}

	// 프레임레이트와 무관한 지수 감쇠 보간
	const vec3 desired = targetObject->transform->GetWorldPosition() + offset;
	const f32  t       = 1.0f - std::exp(-sharpness * deltaTime);
	transform->SetPosition(vec3::Lerp(transform->GetPosition(), desired, t));
}
