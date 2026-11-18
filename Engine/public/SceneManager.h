#pragma once

#include "EngineAPI.h"
#include "Types.h"

#include <memory>
#include <string>

class Scene;

// 씬은 Engine이 소유한다. Game.dll을 리로드해도 씬과 오브젝트, 엔진 컴포넌트(Transform 등)는 그대로 남는다.
class ENGINE_API SceneManager
{
public:
	SceneManager();
	~SceneManager();

	SceneManager(const SceneManager&) = delete;
	SceneManager& operator=(const SceneManager&) = delete;

	// 새 씬을 만들어 활성 씬으로 교체한다. 기존 씬은 파괴된다.
	Scene* CreateScene(std::string name);
	Scene* GetActiveScene() const;
	bool   HasActiveScene() const;
	void   ClearScene();

	void Update(f32 deltaTime, bool editMode = false);
	void LateUpdate(f32 deltaTime, bool editMode = false);

private:
	std::unique_ptr<Scene> _activeScene;
};
