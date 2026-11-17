#pragma once

#include "Component.h"
#include "Transform.h"
#include "EngineAPI.h"
#include "Handle.h"
#include "Types.h"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

class Scene;
struct ComponentTypeInfo;

class ENGINE_API GameObject
{
public:
	explicit GameObject(std::string name = "GameObject");
	~GameObject();

	GameObject(const GameObject&) = delete;
	GameObject& operator=(const GameObject&) = delete;

	// 생성 시 부여되는 고유 ID (재사용되지 않음). 다른 오브젝트를 저장할 때는 포인터 대신 이것(ObjectRef)을 쓴다.
	ObjectId GetId() const;

	// ID로 살아 있는 오브젝트를 찾는다 (삭제 예약된 오브젝트 포함). 없으면 nullptr.
	static GameObject* FindById(ObjectId id);

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
		static_assert(HasOwnComponentType<T>, "T must declare COMPONENT_TYPE(T, Base) in its class body");

		if constexpr (std::is_same_v<T, Transform>)
		{
			if (transform != nullptr)
			{
				return transform;
			}
		}

		std::unique_ptr<T> component;
		if constexpr (IsEngineComponent<T>::value)
		{
			// Engine.dll 안에서 생성 → vtable 포인터가 Engine.dll을 가리킨다 (Game.dll 리로드와 무관)
			component.reset(T::CreateInstance(std::forward<Args>(args)...));
		}
		else
		{
			component = std::make_unique<T>(std::forward<Args>(args)...);
		}
		T* raw = component.get();

		// 소유 모듈 기록: 이 템플릿은 호출한 모듈 안에서 인스턴스화되므로 GetCurrentModuleHandle()은
		// 호출한 모듈(Game.dll 등)을 가리킨다. Engine 컴포넌트는 nullptr(영구) 유지.
		if constexpr (!IsEngineComponent<T>::value)
		{
			component->_definingModule = GetCurrentModuleHandle();
		}

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
		static_assert(HasOwnComponentType<T>, "T must declare COMPONENT_TYPE(T, Base) in its class body");

		if constexpr (std::is_same_v<T, Transform>)
		{
			return transform;
		}
		else
		{
			// 타입 ID 비교 (자식 타입도 IsA로 매칭). dynamic_cast/RTTI를 쓰지 않는다.
			for (const auto& component : _components)
			{
				if (component && component->IsA(T::StaticTypeId()))
				{
					return static_cast<T*>(component.get());
				}
			}

			return nullptr;
		}
	}

	void Update(f32 deltaTime);
	void LateUpdate(f32 deltaTime);

	// Scene::DestroyGameObject로 삭제 예약된 상태인지 (예약되면 더 이상 업데이트되지 않음)
	bool IsPendingDestroy() const;

	u32 GetComponentCount() const;
	Component* GetComponentAt(u32 index) const;

	// 레지스트리에 등록된 타입으로 컴포넌트를 만든다 (에디터의 Add Component). Transform이면 기존 것을 반환.
	// callOnCreate = false면 OnCreate를 호출하지 않는다 (씬 로드: 필드를 채운 뒤 직접 호출)
	Component* AddComponentByType(const ComponentTypeInfo& type, bool callOnCreate = true);

	// Transform은 제거할 수 없다. 이 오브젝트의 Update/LateUpdate 도중이면 즉시 비활성화하고
	// 순회가 끝난 뒤 제거한다.
	bool RemoveComponent(Component* component);

private:
	friend class Scene;
	void MarkPendingDestroy();
	void FlushPendingRemovals();

	friend class ComponentRegistry;

public:
	Transform* transform = nullptr;

private:
	ObjectId _id = kInvalidObjectId;
	std::string _name;
	bool _active = true;
	bool _pendingDestroy = false;
	bool _iterating = false;
	std::vector<Component*> _pendingRemovals;
	Scene* _scene = nullptr;
	std::vector<std::unique_ptr<Component>> _components;
};

// 다른 오브젝트의 컴포넌트를 가리키는 약한 참조. 오브젝트 ID만 저장하고 접근할 때마다 찾는다.
// (컴포넌트 포인터는 Game.dll 리로드 때 새 객체로 바뀌므로 저장하면 안 된다)
template <typename T>
struct ComponentRef
{
	ObjectRef object;

	ComponentRef() = default;
	ComponentRef(const GameObject* owner) : object(owner) {}

	T* Get() const
	{
		GameObject* owner = object.Get();
		return owner != nullptr ? owner->GetComponent<T>() : nullptr;
	}

	bool operator==(const ComponentRef&) const = default;
};
