#include "pch.h"
#include "Component.h"

void Component::SetOwner(GameObject* owner)
{
	_owner = owner;
}

GameObject* Component::GetOwner() const
{
	return _owner;
}

bool Component::IsEnabled() const
{
	return _enabled;
}

void Component::SetEnabled(bool enabled)
{
	_enabled = enabled;
}
