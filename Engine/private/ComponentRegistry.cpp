#include "pch.h"
#include "ComponentRegistry.h"
#include "GameObject.h"
#include "Log.h"
#include "Scene.h"

#include <cstring>

namespace
{
	u32 GetFieldSize(FieldType type)
	{
		switch (type)
		{
		case FieldType::Bool:  return sizeof(bool);
		case FieldType::Int:   return sizeof(i32);
		case FieldType::UInt:  return sizeof(u32);
		case FieldType::Float: return sizeof(f32);
		case FieldType::Vec2:  return sizeof(vec2);
		case FieldType::Vec3:  return sizeof(vec3);
		case FieldType::Vec4:  return sizeof(vec4);
		case FieldType::Quat:  return sizeof(quat);
		case FieldType::ObjectRef:      return sizeof(ObjectRef);
		case FieldType::MeshHandle:     return sizeof(MeshHandle);
		case FieldType::TextureHandle:  return sizeof(TextureHandle);
		case FieldType::ShaderHandle:   return sizeof(ShaderHandle);
		case FieldType::MaterialHandle: return sizeof(MaterialHandle);
		default:               return 0;
		}
	}

	bool IsScalar(FieldType type)
	{
		return type == FieldType::Bool || type == FieldType::Int || type == FieldType::UInt || type == FieldType::Float;
	}

	f64 ReadScalar(FieldType type, const u8* data)
	{
		switch (type)
		{
		case FieldType::Bool:  { bool v; std::memcpy(&v, data, sizeof(v)); return v ? 1.0 : 0.0; }
		case FieldType::Int:   { i32  v; std::memcpy(&v, data, sizeof(v)); return v; }
		case FieldType::UInt:  { u32  v; std::memcpy(&v, data, sizeof(v)); return v; }
		case FieldType::Float: { f32  v; std::memcpy(&v, data, sizeof(v)); return v; }
		default:               return 0.0;
		}
	}

	void WriteScalar(FieldType type, u8* data, f64 value)
	{
		switch (type)
		{
		case FieldType::Bool:  { bool v = value != 0.0;                std::memcpy(data, &v, sizeof(v)); break; }
		case FieldType::Int:   { i32  v = static_cast<i32>(value);     std::memcpy(data, &v, sizeof(v)); break; }
		case FieldType::UInt:  { u32  v = value < 0.0 ? 0u : static_cast<u32>(value); std::memcpy(data, &v, sizeof(v)); break; }
		case FieldType::Float: { f32  v = static_cast<f32>(value);     std::memcpy(data, &v, sizeof(v)); break; }
		default: break;
		}
	}

	FieldSnapshot CaptureField(const Component* component, const FieldInfo& field)
	{
		FieldSnapshot snapshot{ field.name, field.type, {} };
		const u8* address = reinterpret_cast<const u8*>(component) + field.offset;

		if (field.type == FieldType::String)
		{
			const auto& text = *reinterpret_cast<const std::string*>(address);
			snapshot.bytes.assign(text.begin(), text.end());
		}
		else
		{
			const u32 size = GetFieldSize(field.type);
			snapshot.bytes.assign(address, address + size);
		}

		return snapshot;
	}

	// 저장한 값을 새 타입 정의의 같은 이름 필드에 쓴다. 쓰지 못하면 false (기본값 유지).
	bool ApplyField(Component* component, const FieldInfo& field, const FieldSnapshot& saved)
	{
		u8* address = reinterpret_cast<u8*>(component) + field.offset;

		if (field.type == saved.type)
		{
			if (field.type == FieldType::String)
			{
				reinterpret_cast<std::string*>(address)->assign(saved.bytes.begin(), saved.bytes.end());
				return true;
			}

			if (saved.bytes.size() == GetFieldSize(field.type))
			{
				std::memcpy(address, saved.bytes.data(), saved.bytes.size());
				return true;
			}
			return false;
		}

		// 숫자 타입끼리는 변환 (예: int → float로 바꾼 필드)
		if (IsScalar(field.type) && IsScalar(saved.type) && saved.bytes.size() == GetFieldSize(saved.type))
		{
			WriteScalar(field.type, address, ReadScalar(saved.type, saved.bytes.data()));
			return true;
		}

		return false;
	}

	bool IsVTableInModule(const Component* component, void* module)
	{
		const void* vtable = *reinterpret_cast<const void* const*>(component);

		HMODULE owner = nullptr;
		return ::GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				static_cast<LPCWSTR>(vtable), &owner)
			&& owner == static_cast<HMODULE>(module);
	}
}

ComponentRegistry::ComponentRegistry() = default;

ComponentRegistry::~ComponentRegistry() = default;

void ComponentRegistry::RegisterType(ComponentTypeInfo info)
{
	if (const auto it = _types.find(info.id); it != _types.end() && it->second.name != info.name)
	{
		EngineLog("[Registry] WARNING: type id collision between '%s' and '%s'", it->second.name.c_str(), info.name.c_str());
	}

	_types[info.id] = std::move(info);
}

void ComponentRegistry::UnregisterModule(void* module)
{
	// 팩토리 함수가 그 모듈 안에 있으므로 반드시 모듈을 내리기 전에 제거한다
	for (auto it = _types.begin(); it != _types.end();)
	{
		it = it->second.module == module ? _types.erase(it) : std::next(it);
	}
}

const ComponentTypeInfo* ComponentRegistry::Find(const std::string& name) const
{
	return Find(HashComponentTypeName(name.c_str()));
}

const ComponentTypeInfo* ComponentRegistry::Find(ComponentTypeId id) const
{
	const auto it = _types.find(id);
	return it != _types.end() ? &it->second : nullptr;
}

u32 ComponentRegistry::GetTypeCount() const
{
	return static_cast<u32>(_types.size());
}

std::vector<const ComponentTypeInfo*> ComponentRegistry::GetTypes() const
{
	std::vector<const ComponentTypeInfo*> types;
	types.reserve(_types.size());
	for (const auto& [id, info] : _types)
	{
		types.push_back(&info);
	}
	std::sort(types.begin(), types.end(), [](const ComponentTypeInfo* a, const ComponentTypeInfo* b) { return a->name < b->name; });
	return types;
}

u32 ComponentRegistry::SnapshotAndRelease(Scene* scene, void* module, bool keepSnapshot)
{
	u32 released = 0;

	if (scene != nullptr && module != nullptr)
	{
		for (const auto& object : scene->GetGameObjects())
		{
			if (object == nullptr)
			{
				continue;
			}

			auto& components = object->_components;
			u32 index = 0;

			for (auto it = components.begin(); it != components.end(); ++index)
			{
				Component* component = it->get();

				bool release = component != nullptr && component != object->transform && component->GetDefiningModule() == module;

				// 안전망: 소유 표시와 상관없이 vtable이 내려갈 모듈 안에 있으면 남겨둘 수 없다 (남기면 크래시)
				if (component != nullptr && !release && component != object->transform && IsVTableInModule(component, module))
				{
					EngineLog("[Registry] WARNING: '%s' on '%s' has its vtable in the unloading module. "
						"Engine components must be created via CreateInstance. Removing it.",
						component->GetTypeName(), object->GetName().c_str());
					release = true;
				}

				if (!release)
				{
					++it;
					continue;
				}

				if (keepSnapshot)
				{
					component->OnBeforeReload();

					// 타입 이름은 지금 내려갈 모듈 안의 문자열이므로 복사해 둔다
					const ComponentTypeInfo* info = Find(component->GetTypeId());
					if (info != nullptr)
					{
						ComponentSnapshot snapshot;
						snapshot.owner    = object->GetId();
						snapshot.index    = index;
						snapshot.enabled  = component->IsEnabled();
						snapshot.typeName = info->name;
						for (const FieldInfo& field : info->fields)
						{
							snapshot.fields.push_back(CaptureField(component, field));
						}
						_snapshots.push_back(std::move(snapshot));
					}
					else
					{
						EngineLog("[Registry] WARNING: '%s' on '%s' is not registered (call Register<%s>() in GameRegisterTypes). Its state is lost on reload.",
							component->GetTypeName(), object->GetName().c_str(), component->GetTypeName());
					}
				}
				else
				{
					component->OnDestroy();
				}

				// 코드가 아직 메모리에 있을 때 소멸자를 호출해야 한다
				it = components.erase(it);
				++released;
			}
		}
	}

	UnregisterModule(module);
	return released;
}

u32 ComponentRegistry::RestoreSnapshots()
{
	u32 restored = 0;

	// 원래 순서대로 끼워 넣기 위해 index 오름차순으로 처리
	std::stable_sort(_snapshots.begin(), _snapshots.end(),
		[](const ComponentSnapshot& a, const ComponentSnapshot& b) { return a.index < b.index; });

	for (ComponentSnapshot& snapshot : _snapshots)
	{
		GameObject* owner = GameObject::FindById(snapshot.owner);
		if (owner == nullptr)
		{
			EngineLog("[Registry] '%s': owner object #%llu no longer exists. Dropped.",
				snapshot.typeName.c_str(), static_cast<unsigned long long>(snapshot.owner));
			continue;
		}

		const ComponentTypeInfo* info = Find(snapshot.typeName);
		if (info == nullptr || info->factory == nullptr)
		{
			EngineLog("[Registry] '%s' on '%s' no longer exists in the new module. Dropped.",
				snapshot.typeName.c_str(), owner->GetName().c_str());
			continue;
		}

		Component* component = info->factory();
		if (component == nullptr)
		{
			continue;
		}

		component->_definingModule = info->module;
		component->SetOwner(owner);
		component->SetEnabled(snapshot.enabled);

		u32 applied = 0;
		for (const FieldSnapshot& saved : snapshot.fields)
		{
			const auto field = std::find_if(info->fields.begin(), info->fields.end(),
				[&saved](const FieldInfo& f) { return f.name == saved.name; });

			if (field == info->fields.end())
			{
				continue; // 삭제된 필드
			}

			if (ApplyField(component, *field, saved))
			{
				++applied;
			}
			else
			{
				EngineLog("[Registry] '%s.%s': type changed, keeping default value.", snapshot.typeName.c_str(), saved.name.c_str());
			}
		}

		auto& components = owner->_components;
		const size_t position = std::min<size_t>(snapshot.index, components.size());
		components.insert(components.begin() + static_cast<std::ptrdiff_t>(position), std::unique_ptr<Component>(component));

		component->OnAfterReload();
		++restored;
	}

	_snapshots.clear();
	return restored;
}

u32 ComponentRegistry::GetPendingSnapshotCount() const
{
	return static_cast<u32>(_snapshots.size());
}
