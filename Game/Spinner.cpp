#include "pch.h"
#include "Spinner.h"
#include "GameObject.h"
#include "Transform.h"

Spinner::Spinner(f32 degreesPerSecond)
	: degreesPerSecond(degreesPerSecond)
{
}

void Spinner::Update(f32 deltaTime)
{
	Transform* transform = _owner != nullptr ? _owner->transform : nullptr;
	if (transform == nullptr)
	{
		return;
	}

	const f32 step = degreesPerSecond * deltaTime;
	totalDegrees += step;

	const quat delta = quat::CreateFromAxisAngle(vec3::Up, DirectX::XMConvertToRadians(step));
	transform->SetRotation(transform->GetRotation() * delta);
}
