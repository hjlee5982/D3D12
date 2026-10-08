#pragma once

#include "Types.h"

class GameObject;

class Component
{
public:
	Component() = default;
	virtual ~Component() = default;

	Component(const Component&) = delete;
	Component& operator=(const Component&) = delete;

	void SetOwner(GameObject* owner);
	GameObject* GetOwner() const;

	bool IsEnabled() const;
	void SetEnabled(bool enabled);

	virtual void OnCreate() {}
	virtual void Update(f32 deltaTime) {}
	virtual void LateUpdate(f32 deltaTime) {}
	virtual void OnDestroy() {}

protected:
	GameObject* _owner = nullptr;
	bool _enabled = true;
};
