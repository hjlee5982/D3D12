#pragma once

#include "EngineAPI.h"
#include "Types.h"

#include <memory>
#include <string>
#include <vector>

class GameObject;

class ENGINE_API Scene
{
public:
	explicit Scene(std::string name = "Scene");
	~Scene();

	Scene(const Scene&) = delete;
	Scene& operator=(const Scene&) = delete;

	const std::string& GetName() const;
	void SetName(std::string name);

	// 업데이트 중에 생성된 오브젝트는 다음 프레임부터 업데이트된다.
	GameObject* CreateGameObject(std::string name = "GameObject");

	// 업데이트 중에 호출되면 즉시 비활성 처리하고, 실제 삭제는 현재 업데이트 단계가 끝난 뒤 수행한다.
	void DestroyGameObject(GameObject* gameObject);

	// 이름이 같은 첫 번째 오브젝트 (삭제 예약된 오브젝트 제외). 없으면 nullptr.
	GameObject* FindGameObject(const std::string& name) const;
	u32 GetGameObjectCount() const;
	const std::vector<std::unique_ptr<GameObject>>& GetGameObjects() const;

	void Update(f32 deltaTime);
	void LateUpdate(f32 deltaTime);

	// Update/LateUpdate 실행 중인지 (이때 씬을 교체하면 안 된다)
	bool IsUpdating() const;

private:
	void FlushPendingDestroy();
	void DestroyImmediate(GameObject* gameObject);

private:
	std::string _name;
	std::vector<std::unique_ptr<GameObject>> _gameObjects;
	std::vector<GameObject*> _pendingDestroy;
	bool _isUpdating = false;
};
