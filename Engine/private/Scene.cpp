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

GameObject* Scene::CreateGameObject(std::string name, ObjectId requestedId)
{
	auto gameObject = std::make_unique<GameObject>(std::move(name), requestedId);
	GameObject* raw = gameObject.get();
	raw->SetScene(this);

	// 업데이트 루프는 인덱스로 순회하고 시작 시점의 개수만 처리하므로,
	// 여기서 push_back 해도 순회 중인 원소가 무효화되지 않는다.
	_gameObjects.push_back(std::move(gameObject));
	return raw;
}

void Scene::DestroyGameObject(GameObject* gameObject)
{
	if (gameObject == nullptr || gameObject->GetScene() != this || gameObject->IsPendingDestroy())
	{
		return;
	}

	if (_isUpdating)
	{
		gameObject->MarkPendingDestroy();
		_pendingDestroy.push_back(gameObject);
		return;
	}

	DestroyImmediate(gameObject);
}

void Scene::Update(f32 deltaTime, bool editMode)
{
	_isUpdating = true;

	const size_t count = _gameObjects.size();
	for (size_t i = 0; i < count; ++i)
	{
		GameObject* gameObject = _gameObjects[i].get();
		if (gameObject && !gameObject->IsPendingDestroy())
		{
			gameObject->Update(deltaTime, editMode);
		}
	}

	_isUpdating = false;
	FlushPendingDestroy();
}

void Scene::LateUpdate(f32 deltaTime, bool editMode)
{
	_isUpdating = true;

	const size_t count = _gameObjects.size();
	for (size_t i = 0; i < count; ++i)
	{
		GameObject* gameObject = _gameObjects[i].get();
		if (gameObject && !gameObject->IsPendingDestroy())
		{
			gameObject->LateUpdate(deltaTime, editMode);
		}
	}

	_isUpdating = false;
	FlushPendingDestroy();
}

void Scene::FlushPendingDestroy()
{
	// DestroyImmediate 중 소멸자가 다시 Destroy를 호출할 수 있으므로 복사본으로 처리
	while (!_pendingDestroy.empty())
	{
		std::vector<GameObject*> pending;
		pending.swap(_pendingDestroy);

		for (GameObject* gameObject : pending)
		{
			DestroyImmediate(gameObject);
		}
	}
}

void Scene::DestroyImmediate(GameObject* gameObject)
{
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

GameObject* Scene::FindGameObject(const std::string& name) const
{
	for (const auto& gameObject : _gameObjects)
	{
		if (gameObject && !gameObject->IsPendingDestroy() && gameObject->GetName() == name)
		{
			return gameObject.get();
		}
	}
	return nullptr;
}

u32 Scene::GetGameObjectCount() const
{
	return static_cast<u32>(_gameObjects.size());
}

const std::vector<std::unique_ptr<GameObject>>& Scene::GetGameObjects() const
{
	return _gameObjects;
}

bool Scene::IsUpdating() const
{
	return _isUpdating;
}
