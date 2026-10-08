#pragma once

#include "Component.h"
#include "Transform.h"
#include "Types.h"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

class Scene;

class GameObject
{
public:
	explicit GameObject(std::string name = "GameObject");
	~GameObject();

	GameObject(const GameObject&) = delete;
	GameObject& operator=(const GameObject&) = delete;

	const std::string& GetName() const;
	void SetName(std::string name);

	bool IsActive() const;
	void SetActive(bool active);

	Scene* GetScene() const;
	void SetScene(Scene* scene);

	template <typename T, typename... Args>
	T* AddComponent(Args&&... args)
	{
		static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");

		if constexpr (std::is_same_v<T, Transform>)
		{
			if (transform != nullptr)
			{
				return transform;
			}
		}

		auto component = std::make_unique<T>(std::forward<Args>(args)...);
		T* raw = component.get();
		component->SetOwner(this);
		component->OnCreate();
		_components.push_back(std::move(component));

		if constexpr (std::is_same_v<T, Transform>)
		{
			transform = raw;
		}

		return raw;
	}

	template <typename T>
	T* GetComponent() const
	{
		static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");

		if constexpr (std::is_same_v<T, Transform>)
		{
			return transform;
		}

		for (const auto& component : _components)
		{
			if (T* casted = dynamic_cast<T*>(component.get()))
			{
				return casted;
			}
		}

		return nullptr;
	}

	void Update(f32 deltaTime);
	void LateUpdate(f32 deltaTime);

public:
	Transform* transform = nullptr;

private:
	std::string _name;
	bool _active = true;
	Scene* _scene = nullptr;
	std::vector<std::unique_ptr<Component>> _components;
};
