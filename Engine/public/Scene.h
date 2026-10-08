#pragma once

#include "Types.h"

#include <memory>
#include <string>
#include <vector>

class GameObject;

class Scene
{
public:
	explicit Scene(std::string name = "Scene");
	~Scene();

	Scene(const Scene&) = delete;
	Scene& operator=(const Scene&) = delete;

	const std::string& GetName() const;
	void SetName(std::string name);

	GameObject* CreateGameObject(std::string name = "GameObject");
	void DestroyGameObject(GameObject* gameObject);

	void Update(f32 deltaTime);
	void LateUpdate(f32 deltaTime);

private:
	std::string _name;
	std::vector<std::unique_ptr<GameObject>> _gameObjects;
};
