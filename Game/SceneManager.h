#pragma once

class Scene;
class GameObject;

class SceneManager
{
public:
	SceneManager();
	~SceneManager();

	SceneManager(const SceneManager&) = delete;
	SceneManager& operator=(const SceneManager&) = delete;

	void CreateScene(std::string name);

	const std::string& GetName() const;
	void SetName(std::string name);

	GameObject* CreateGameObject(std::string name = "GameObject");
	void DestroyGameObject(GameObject* gameObject);

	void Update(f32 deltaTime);
	void LateUpdate(f32 deltaTime);

private:
	bool LoadResources();
	bool AddGameObject();

private:
	std::unique_ptr<Scene> _scene;
};

