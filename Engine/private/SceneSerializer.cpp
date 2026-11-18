#include "pch.h"
#include "SceneSerializer.h"

#include "ComponentRegistry.h"
#include "GameObject.h"
#include "Json.h"
#include "Log.h"
#include "RenderResources.h"
#include "ResourceManager.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Transform.h"

#include <cstring>
#include <filesystem>
#include <unordered_map>

namespace
{
	constexpr int kFormatVersion = 1;

	// ---------------------------------------------------------------------------------------------
	// 파일 입출력
	// ---------------------------------------------------------------------------------------------
	bool ReadTextFile(const std::wstring& path, std::string& out)
	{
		const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		LARGE_INTEGER size{};
		bool ok = ::GetFileSizeEx(file, &size) && size.QuadPart < (256ll << 20);
		if (ok)
		{
			out.resize(static_cast<size_t>(size.QuadPart));
			DWORD read = 0;
			ok = out.empty() || (::ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) && read == out.size());
		}
		::CloseHandle(file);
		return ok;
	}

	// 임시 파일에 다 쓴 뒤 교체한다: 저장 도중 실패해도 기존 파일이 깨지지 않는다
	bool WriteTextFileAtomic(const std::wstring& path, const std::string& text)
	{
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

		const std::wstring temp = path + L".tmp";
		const HANDLE file = ::CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		DWORD written = 0;
		const bool ok = ::WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size()
			&& ::FlushFileBuffers(file);
		::CloseHandle(file);

		if (!ok || !::MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			::DeleteFileW(temp.c_str());
			return false;
		}
		return true;
	}

	// ---------------------------------------------------------------------------------------------
	// 필드 → JSON
	// ---------------------------------------------------------------------------------------------
	template <typename T>
	T Read(const u8* address)
	{
		T value;
		std::memcpy(&value, address, sizeof(T));
		return value;
	}

	template <typename T>
	void Write(u8* address, const T& value)
	{
		std::memcpy(address, &value, sizeof(T));
	}

	Json FloatArray(const f32* values, int count)
	{
		Json array = Json::MakeArray();
		for (int i = 0; i < count; ++i)
		{
			array.Push(Json(values[i]));
		}
		return array;
	}

	Json WriteField(const Component* component, const FieldInfo& field, const ResourceManager* resources)
	{
		const u8* address = reinterpret_cast<const u8*>(component) + field.offset;

		switch (field.type)
		{
		case FieldType::Bool:   return Json(Read<bool>(address));
		case FieldType::Int:    return Json(static_cast<double>(Read<i32>(address)));
		case FieldType::UInt:   return Json(static_cast<double>(Read<u32>(address)));
		case FieldType::Float:  return Json(Read<f32>(address));
		case FieldType::Vec2:   return FloatArray(reinterpret_cast<const f32*>(address), 2);
		case FieldType::Vec3:   return FloatArray(reinterpret_cast<const f32*>(address), 3);
		case FieldType::Vec4:   return FloatArray(reinterpret_cast<const f32*>(address), 4);
		case FieldType::Quat:   return FloatArray(reinterpret_cast<const f32*>(address), 4);
		case FieldType::String: return Json(*reinterpret_cast<const std::string*>(address));

		case FieldType::ObjectRef:
		{
			// 살아 있는 대상만 저장 (삭제된 대상을 가리키던 참조는 null)
			const ObjectRef ref = Read<ObjectRef>(address);
			return ref.Get() != nullptr ? Json(static_cast<unsigned long long>(ref.id)) : Json(nullptr);
		}

		case FieldType::MeshHandle:
		{
			const Mesh* mesh = resources != nullptr ? resources->Get(Read<MeshHandle>(address)) : nullptr;
			return mesh != nullptr ? Json(mesh->name) : Json(nullptr);
		}
		case FieldType::TextureHandle:
		{
			const Texture* texture = resources != nullptr ? resources->Get(Read<TextureHandle>(address)) : nullptr;
			return texture != nullptr ? Json(texture->path) : Json(nullptr);
		}
		case FieldType::ShaderHandle:
		{
			const Shader* shader = resources != nullptr ? resources->Get(Read<ShaderHandle>(address)) : nullptr;
			return shader != nullptr ? Json(shader->path) : Json(nullptr);
		}
		case FieldType::MaterialHandle:
		{
			const Material* material = resources != nullptr ? resources->Get(Read<MaterialHandle>(address)) : nullptr;
			return material != nullptr ? Json(material->name) : Json(nullptr);
		}
		}
		return Json(nullptr);
	}

	// ---------------------------------------------------------------------------------------------
	// JSON → 필드
	// ---------------------------------------------------------------------------------------------
	struct PendingRef
	{
		ObjectRef* ref;
		u64        fileId;
	};

	bool ReadFloats(const Json& value, f32* out, size_t count)
	{
		if (!value.IsArray() || value.Size() < count)
		{
			return false;
		}
		for (size_t i = 0; i < count; ++i)
		{
			if (!value[i].IsNumber())
			{
				return false;
			}
			out[i] = static_cast<f32>(value[i].AsNumber());
		}
		return true;
	}

	bool ReadScalar(const Json& value, double& out)
	{
		if (value.IsNumber()) { out = value.AsNumber(); return true; }
		if (value.IsBool())   { out = value.AsBool() ? 1.0 : 0.0; return true; }
		return false;
	}

	// 실패하면 false (필드는 기본값 유지)
	bool ApplyField(Component* component, const FieldInfo& field, const Json& value, ResourceManager* resources, std::vector<PendingRef>& pendingRefs)
	{
		u8* address = reinterpret_cast<u8*>(component) + field.offset;
		double scalar = 0.0;

		switch (field.type)
		{
		case FieldType::Bool:
			if (!ReadScalar(value, scalar)) return false;
			Write<bool>(address, scalar != 0.0);
			return true;

		case FieldType::Int:
			if (!ReadScalar(value, scalar)) return false;
			Write<i32>(address, static_cast<i32>(scalar));
			return true;

		case FieldType::UInt:
			if (!ReadScalar(value, scalar)) return false;
			Write<u32>(address, scalar < 0.0 ? 0u : static_cast<u32>(scalar));
			return true;

		case FieldType::Float:
			if (!ReadScalar(value, scalar)) return false;
			Write<f32>(address, static_cast<f32>(scalar));
			return true;

		case FieldType::Vec2: return ReadFloats(value, reinterpret_cast<f32*>(address), 2);
		case FieldType::Vec3: return ReadFloats(value, reinterpret_cast<f32*>(address), 3);
		case FieldType::Vec4: return ReadFloats(value, reinterpret_cast<f32*>(address), 4);

		case FieldType::Quat:
		{
			f32 q[4];
			if (!ReadFloats(value, q, 4))
			{
				return false;
			}
			quat rotation(q[0], q[1], q[2], q[3]);

			// 손으로 고친 값처럼 길이가 확실히 틀린 경우만 정규화한다
			// (항상 정규화하면 저장 → 불러오기 → 저장 결과가 마지막 자릿수에서 달라진다)
			const f32 lengthSquared = rotation.LengthSquared();
			if (lengthSquared < 1e-12f)
			{
				return false;
			}
			if (std::fabs(lengthSquared - 1.0f) > 1e-3f)
			{
				rotation.Normalize();
			}
			Write<quat>(address, rotation);
			return true;
		}

		case FieldType::String:
			if (!value.IsString()) return false;
			*reinterpret_cast<std::string*>(address) = value.AsString();
			return true;

		case FieldType::ObjectRef:
		{
			ObjectRef* ref = reinterpret_cast<ObjectRef*>(address);
			ref->Reset();
			if (value.IsNumber())
			{
				// 모든 오브젝트를 만든 뒤 새 ID로 바꿔 연결한다
				pendingRefs.push_back(PendingRef{ ref, static_cast<u64>(value.AsNumber()) });
				return true;
			}
			return value.IsNull();
		}

		case FieldType::MeshHandle:
			if (value.IsNull()) { Write(address, MeshHandle{}); return true; }
			if (!value.IsString() || resources == nullptr) return false;
			Write(address, resources->FindMesh(value.AsString()));
			return true;

		case FieldType::TextureHandle:
			if (value.IsNull()) { Write(address, TextureHandle{}); return true; }
			if (!value.IsString() || resources == nullptr) return false;
			Write(address, resources->LoadTexture(value.AsString()));
			return true;

		case FieldType::ShaderHandle:
			if (value.IsNull()) { Write(address, ShaderHandle{}); return true; }
			if (!value.IsString() || resources == nullptr) return false;
			Write(address, resources->LoadShader(value.AsString()));
			return true;

		case FieldType::MaterialHandle:
			if (value.IsNull()) { Write(address, MaterialHandle{}); return true; }
			if (!value.IsString() || resources == nullptr) return false;
			Write(address, resources->FindMaterial(value.AsString()));
			return true;
		}
		return false;
	}
}

SceneSerializer::Result SceneSerializer::Save(const Scene& scene, const ComponentRegistry& registry, const ResourceManager* resources, const std::wstring& filePath)
{
	std::string text;
	Result result = SaveToString(scene, registry, resources, text);
	if (result.ok && !WriteTextFileAtomic(filePath, text))
	{
		result.ok    = false;
		result.error = "cannot write file";
	}
	return result;
}

SceneSerializer::Result SceneSerializer::SaveToString(const Scene& scene, const ComponentRegistry& registry, const ResourceManager* resources, std::string& out)
{
	Result result;

	Json root = Json::MakeObject();
	root.Set("format", kFormatVersion);
	root.Set("name", scene.GetName());

	// 머티리얼: 셰이더/텍스처를 경로로 기록 (씬이 참조하지 않는 것도 에디터에서 만든 것이면 남긴다)
	Json& materials = root.Set("materials", Json::MakeArray());
	if (resources != nullptr)
	{
		for (const auto& entry : resources->ListMaterials())
		{
			const Material* material = resources->Get(entry.handle);
			const Shader*   shader   = resources->Get(material->shader);
			const Texture*  texture  = resources->Get(material->texture);

			Json item = Json::MakeObject();
			item.Set("name", material->name);
			item.Set("shader", shader != nullptr ? Json(shader->path) : Json(nullptr));
			item.Set("texture", texture != nullptr ? Json(texture->path) : Json(nullptr));
			materials.Push(std::move(item));
		}
	}

	Json& objects = root.Set("objects", Json::MakeArray());
	for (const auto& object : scene.GetGameObjects())
	{
		if (object == nullptr || object->IsPendingDestroy())
		{
			continue;
		}

		const Transform* parent = object->transform != nullptr ? object->transform->GetParent() : nullptr;

		Json item = Json::MakeObject();
		item.Set("id", static_cast<unsigned long long>(object->GetId()));
		item.Set("name", object->GetName());
		item.Set("active", object->IsActive());
		item.Set("parent", parent != nullptr && parent->GetOwner() != nullptr
			? Json(static_cast<unsigned long long>(parent->GetOwner()->GetId())) : Json(nullptr));

		Json& components = item.Set("components", Json::MakeArray());
		for (u32 i = 0; i < object->GetComponentCount(); ++i)
		{
			const Component* component = object->GetComponentAt(i);
			if (component == nullptr)
			{
				continue;
			}

			const ComponentTypeInfo* info = registry.Find(component->GetTypeId());
			if (info == nullptr)
			{
				EngineLog("[Scene] WARNING: '%s' on '%s' is not registered and was not saved.", component->GetTypeName(), object->GetName().c_str());
				++result.skipped;
				continue;
			}

			Json entry = Json::MakeObject();
			entry.Set("type", info->name);
			entry.Set("enabled", component->IsEnabled());

			Json& fields = entry.Set("fields", Json::MakeObject());
			for (const FieldInfo& field : info->fields)
			{
				fields.Set(field.name, WriteField(component, field, resources));
			}

			components.Push(std::move(entry));
			++result.components;
		}

		objects.Push(std::move(item));
		++result.objects;
	}

	out = root.Dump();
	result.ok = true;
	return result;
}

SceneSerializer::Result SceneSerializer::Load(SceneManager& sceneManager, const ComponentRegistry& registry, ResourceManager* resources, const std::wstring& filePath)
{
	std::string text;
	if (!ReadTextFile(filePath, text))
	{
		Result result;
		result.error = "cannot read file";
		return result;
	}
	return LoadFromString(sceneManager, registry, resources, text, LoadOptions{});
}

SceneSerializer::Result SceneSerializer::LoadFromString(SceneManager& sceneManager, const ComponentRegistry& registry, ResourceManager* resources,
	const std::string& text, const LoadOptions& options)
{
	Result result;

	// 1) 파싱 + 형식 확인. 여기서 실패하면 기존 씬을 건드리지 않는다.
	Json root;
	if (!Json::Parse(text, root, result.error))
	{
		result.error = "parse error at " + result.error;
		return result;
	}

	const int format = static_cast<int>(root["format"].AsNumber(-1.0));
	if (format != kFormatVersion || !root["objects"].IsArray())
	{
		result.error = "unsupported scene format " + std::to_string(format);
		return result;
	}

	// 2) 머티리얼 (같은 이름이 있으면 저장된 셰이더/텍스처로 맞춘다)
	if (resources != nullptr && options.applyMaterials)
	{
		const Json& materials = root["materials"];
		for (size_t i = 0; i < materials.Size(); ++i)
		{
			const Json& item = materials[i];
			const std::string& name = item["name"].AsString();
			if (name.empty())
			{
				continue;
			}

			const ShaderHandle  shader  = item["shader"].IsString()  ? resources->LoadShader(item["shader"].AsString())   : ShaderHandle{};
			const TextureHandle texture = item["texture"].IsString() ? resources->LoadTexture(item["texture"].AsString()) : TextureHandle{};

			if (const MaterialHandle existing = resources->FindMaterial(name))
			{
				resources->SetMaterialShader(existing, shader);
				resources->SetMaterialTexture(existing, texture);
			}
			else
			{
				resources->CreateMaterial(name, shader, texture);
			}
		}
	}

	// 3) 씬 교체 + 오브젝트/컴포넌트 생성 (OnCreate는 필드와 참조를 다 채운 뒤 호출)
	const std::string sceneName = root["name"].IsString() ? root["name"].AsString() : "Scene";
	Scene* scene = sceneManager.CreateScene(sceneName);

	std::unordered_map<u64, GameObject*> byFileId;
	std::vector<std::pair<GameObject*, u64>> parentLinks;
	std::vector<PendingRef> pendingRefs;
	std::vector<Component*> created;

	const Json& objects = root["objects"];
	for (size_t i = 0; i < objects.Size(); ++i)
	{
		const Json& item = objects[i];

		// preserveIds: 기존 씬이 이미 파괴되었으므로 파일의 ID가 비어 있다 → 그대로 사용 (선택/ObjectRef 유지)
		const ObjectId requestedId = options.preserveIds && item["id"].IsNumber() ? static_cast<ObjectId>(item["id"].AsNumber()) : kInvalidObjectId;
		GameObject* object = scene->CreateGameObject(item["name"].IsString() ? item["name"].AsString() : "GameObject", requestedId);
		object->SetActive(item["active"].AsBool(true));
		++result.objects;

		if (item["id"].IsNumber())
		{
			byFileId[static_cast<u64>(item["id"].AsNumber())] = object;
		}
		if (item["parent"].IsNumber())
		{
			parentLinks.emplace_back(object, static_cast<u64>(item["parent"].AsNumber()));
		}

		const Json& components = item["components"];
		for (size_t c = 0; c < components.Size(); ++c)
		{
			const Json& entry = components[c];
			const std::string& typeName = entry["type"].AsString();

			const ComponentTypeInfo* info = registry.Find(typeName);
			if (info == nullptr)
			{
				EngineLog("[Scene] WARNING: component type '%s' on '%s' is not registered. Skipped.", typeName.c_str(), object->GetName().c_str());
				++result.skipped;
				continue;
			}

			Component* component = object->AddComponentByType(*info, false);
			if (component == nullptr)
			{
				++result.skipped;
				continue;
			}

			component->SetEnabled(entry["enabled"].AsBool(true));

			const Json& fields = entry["fields"];
			for (const auto& [fieldName, value] : fields.Members())
			{
				const auto field = std::find_if(info->fields.begin(), info->fields.end(),
					[&fieldName](const FieldInfo& f) { return f.name == fieldName; });

				if (field == info->fields.end())
				{
					EngineLog("[Scene] '%s.%s' no longer exists. Ignored.", typeName.c_str(), fieldName.c_str());
					continue;
				}

				if (!ApplyField(component, *field, value, resources, pendingRefs))
				{
					EngineLog("[Scene] '%s.%s' has an incompatible value. Keeping default.", typeName.c_str(), fieldName.c_str());
				}
			}

			if (component != object->transform)
			{
				created.push_back(component);
			}
			++result.components;
		}
	}

	// 4) 부모 연결 (로컬 값을 저장했으므로 그대로 붙이면 된다)
	for (const auto& [child, parentFileId] : parentLinks)
	{
		const auto it = byFileId.find(parentFileId);
		if (it != byFileId.end())
		{
			child->transform->SetParent(it->second->transform);
		}
	}

	// 5) 오브젝트 참조를 새 ID로 연결 (파일에 없는 대상이면 비움)
	for (const PendingRef& pending : pendingRefs)
	{
		const auto it = byFileId.find(pending.fileId);
		*pending.ref = it != byFileId.end() ? ObjectRef(it->second) : ObjectRef();
	}

	// 6) 모든 값이 채워진 뒤 OnCreate
	for (Component* component : created)
	{
		component->OnCreate();
	}

	result.ok = true;
	return result;
}
