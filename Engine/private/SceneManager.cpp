#include "pch.h"
#include "SceneManager.h"
#include "Scene.h"

SceneManager::SceneManager() = default;

SceneManager::~SceneManager() = default;

Scene* SceneManager::CreateScene(std::string name)
{
	_activeScene = std::make_unique<Scene>(std::move(name));
	return _activeScene.get();
}

Scene* SceneManager::GetActiveScene() const
{
	return _activeScene.get();
}

bool SceneManager::HasActiveScene() const
{
	return _activeScene != nullptr;
}

void SceneManager::ClearScene()
{
	_activeScene.reset();
}

void SceneManager::Update(f32 deltaTime, bool editMode)
{
	if (_activeScene)
	{
		_activeScene->Update(deltaTime, editMode);
	}
}

void SceneManager::LateUpdate(f32 deltaTime, bool editMode)
{
	if (_activeScene)
	{
		_activeScene->LateUpdate(deltaTime, editMode);
	}
}
