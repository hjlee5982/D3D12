#include "pch.h"
#include "Scene.h"
#include "GameObject.h"

Scene::Scene(std::string name)
	: _name(std::move(name))
{
}

Scene::~Scene() = default;

const std::string& Scene::GetName() const
{
	return _name;
}

void Scene::SetName(std::string name)
{
	_name = std::move(name);
}

GameObject* Scene::CreateGameObject(std::string name)
{
	auto gameObject = std::make_unique<GameObject>(std::move(name));
	GameObject* raw = gameObject.get();
	raw->SetScene(this);
	_gameObjects.push_back(std::move(gameObject));
	return raw;
}

void Scene::DestroyGameObject(GameObject* gameObject)
{
	if (gameObject == nullptr)
	{
		return;
	}

	const auto it = std::find_if(
		_gameObjects.begin(),
		_gameObjects.end(),
		[gameObject](const std::unique_ptr<GameObject>& owned)
		{
			return owned.get() == gameObject;
		});

	if (it != _gameObjects.end())
	{
		_gameObjects.erase(it);
	}
}

void Scene::Update(f32 deltaTime)
{
	for (auto& gameObject : _gameObjects)
	{
		if (gameObject)
		{
			gameObject->Update(deltaTime);
		}
	}
}

void Scene::LateUpdate(f32 deltaTime)
{
	for (auto& gameObject : _gameObjects)
	{
		if (gameObject)
		{
			gameObject->LateUpdate(deltaTime);
		}
	}
}
