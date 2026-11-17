#include "pch.h"
#include "GameObject.h"
#include "Component.h"
#include "ComponentRegistry.h"

#include <unordered_map>

namespace
{
	// 살아 있는 모든 GameObject의 ID 테이블. Engine.dll 안에 있으므로 Game.dll 리로드와 무관하다.
	// (게임 루프는 단일 스레드이므로 잠금 없음)
	std::unordered_map<ObjectId, GameObject*>& GetObjectTable()
	{
		static std::unordered_map<ObjectId, GameObject*> table;
		return table;
	}

	ObjectId g_nextObjectId = 1;
}

ObjectRef::ObjectRef(const GameObject* object)
	: id(object != nullptr ? object->GetId() : kInvalidObjectId)
{
}

GameObject* ObjectRef::Get() const
{
	GameObject* object = GameObject::FindById(id);
	return object != nullptr && !object->IsPendingDestroy() ? object : nullptr;
}

GameObject::GameObject(std::string name)
	: _id(g_nextObjectId++)
	, _name(std::move(name))
{
	GetObjectTable().emplace(_id, this);
	AddComponent<Transform>();
}

GameObject::~GameObject()
{
	for (auto& component : _components)
	{
		if (component)
		{
			component->OnDestroy();
		}
	}

	// OnDestroy에서 다른 오브젝트가 이 오브젝트를 조회할 수 있으므로 마지막에 테이블에서 뺀다
	GetObjectTable().erase(_id);
}

ObjectId GameObject::GetId() const
{
	return _id;
}

GameObject* GameObject::FindById(ObjectId id)
{
	if (id == kInvalidObjectId)
	{
		return nullptr;
	}

	const auto& table = GetObjectTable();
	const auto it = table.find(id);
	return it != table.end() ? it->second : nullptr;
}

const std::string& GameObject::GetName() const
{
	return _name;
}

void GameObject::SetName(std::string name)
{
	_name = std::move(name);
}

bool GameObject::IsActive() const
{
	return _active;
}

void GameObject::SetActive(bool active)
{
	_active = active;
}

Scene* GameObject::GetScene() const
{
	return _scene;
}

void GameObject::SetScene(Scene* scene)
{
	_scene = scene;
}

void GameObject::Update(f32 deltaTime)
{
	if (!_active)
	{
		return;
	}

	// 컴포넌트 Update 안에서 AddComponent를 호출해도 안전하도록 인덱스로 순회한다.
	// (push_back으로 재할당되어도 인덱스 접근은 유효, 새 컴포넌트는 다음 프레임부터 처리)
	_iterating = true;
	const size_t count = _components.size();
	for (size_t i = 0; i < count; ++i)
	{
		Component* component = _components[i].get();
		if (component && component->IsEnabled())
		{
			component->Update(deltaTime);
		}
	}
	_iterating = false;
	FlushPendingRemovals();
}

void GameObject::LateUpdate(f32 deltaTime)
{
	if (!_active)
	{
		return;
	}

	// 컴포넌트 Update 안에서 AddComponent를 호출해도 안전하도록 인덱스로 순회한다.
	// (push_back으로 재할당되어도 인덱스 접근은 유효, 새 컴포넌트는 다음 프레임부터 처리)
	_iterating = true;
	const size_t count = _components.size();
	for (size_t i = 0; i < count; ++i)
	{
		Component* component = _components[i].get();
		if (component && component->IsEnabled())
		{
			component->LateUpdate(deltaTime);
		}
	}
	_iterating = false;
	FlushPendingRemovals();
}

bool GameObject::IsPendingDestroy() const
{
	return _pendingDestroy;
}

void GameObject::MarkPendingDestroy()
{
	_pendingDestroy = true;
}

u32 GameObject::GetComponentCount() const
{
	return static_cast<u32>(_components.size());
}

Component* GameObject::GetComponentAt(u32 index) const
{
	return index < _components.size() ? _components[index].get() : nullptr;
}

Component* GameObject::AddComponentByType(const ComponentTypeInfo& type, bool callOnCreate)
{
	if (type.id == Transform::StaticTypeId())
	{
		return transform;
	}

	if (type.factory == nullptr)
	{
		return nullptr;
	}

	Component* component = type.factory();
	if (component == nullptr)
	{
		return nullptr;
	}

	// 팩토리는 타입을 정의한 모듈 안에 있으므로 vtable도 그 모듈을 가리킨다
	component->_definingModule = type.module;
	component->SetOwner(this);
	if (callOnCreate)
	{
		component->OnCreate();
	}
	_components.push_back(std::unique_ptr<Component>(component));
	return component;
}

bool GameObject::RemoveComponent(Component* component)
{
	if (component == nullptr || component == transform)
	{
		return false;
	}

	const auto it = std::find_if(_components.begin(), _components.end(),
		[component](const std::unique_ptr<Component>& owned) { return owned.get() == component; });
	if (it == _components.end())
	{
		return false;
	}

	if (_iterating)
	{
		// 순회 중 erase하면 인덱스 순회가 어긋나므로 미룬다
		component->SetEnabled(false);
		if (std::find(_pendingRemovals.begin(), _pendingRemovals.end(), component) == _pendingRemovals.end())
		{
			_pendingRemovals.push_back(component);
		}
		return true;
	}

	component->OnDestroy();
	_components.erase(it);
	return true;
}

void GameObject::FlushPendingRemovals()
{
	while (!_pendingRemovals.empty())
	{
		std::vector<Component*> pending;
		pending.swap(_pendingRemovals);
		for (Component* component : pending)
		{
			RemoveComponent(component);
		}
	}
}
