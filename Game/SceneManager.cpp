#include "pch.h"
#include "SceneManager.h"
#include "Scene.h"
#include "GameObject.h"
#include "Transform.h"

SceneManager::SceneManager()
{
}

SceneManager::~SceneManager()
{
}

void SceneManager::CreateScene(std::string name)
{
	_scene = std::make_unique<Scene>(name);

	LoadResources();

	AddGameObject();
}

const std::string& SceneManager::GetName() const
{
	return _scene->GetName();
}

void SceneManager::SetName(std::string name)
{
	_scene->SetName(name);
}

GameObject* SceneManager::CreateGameObject(std::string name)
{
	return _scene->CreateGameObject(name);
}

void SceneManager::DestroyGameObject(GameObject* gameObject)
{
	_scene->DestroyGameObject(gameObject);
}

void SceneManager::Update(f32 deltaTime)
{
	_scene->Update(deltaTime);
}

void SceneManager::LateUpdate(f32 deltaTime)
{
	_scene->LateUpdate(deltaTime);
}

bool SceneManager::LoadResources()
{
	return true;
}

bool SceneManager::AddGameObject()
{
	auto parent = CreateGameObject();
	if (!parent)
		return false;

	auto child = CreateGameObject();
	if (!child)
		return false;
	
	child->transform->SetParent(parent->transform);


	return true;
}
