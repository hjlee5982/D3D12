#include "pch.h"
#include "GameObject.h"
#include "Component.h"

GameObject::GameObject(std::string name)
	: _name(std::move(name))
{
	AddComponent<Transform>();
}

GameObject::~GameObject()
{
	for (auto& component : _components)
	{
		if (component)
		{
			component->OnDestroy();
		}
	}
}

const std::string& GameObject::GetName() const
{
	return _name;
}

void GameObject::SetName(std::string name)
{
	_name = std::move(name);
}

bool GameObject::IsActive() const
{
	return _active;
}

void GameObject::SetActive(bool active)
{
	_active = active;
}

Scene* GameObject::GetScene() const
{
	return _scene;
}

void GameObject::SetScene(Scene* scene)
{
	_scene = scene;
}

void GameObject::Update(f32 deltaTime)
{
	if (!_active)
	{
		return;
	}

	for (auto& component : _components)
	{
		if (component && component->IsEnabled())
		{
			component->Update(deltaTime);
		}
	}
}

void GameObject::LateUpdate(f32 deltaTime)
{
	if (!_active)
	{
		return;
	}

	for (auto& component : _components)
	{
		if (component && component->IsEnabled())
		{
			component->LateUpdate(deltaTime);
		}
	}
}
