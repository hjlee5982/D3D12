#pragma once

#include "Component.h"
#include "EngineAPI.h"
#include "Handle.h"
#include "Types.h"

#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

class GameObject;
class Scene;

template <typename T> struct ComponentRef; // GameObject.h

// ---------------------------------------------------------------------------------------------
// 필드 리플렉션
// ---------------------------------------------------------------------------------------------
enum class FieldType : u8
{
	Bool,
	Int,     // i32
	UInt,    // u32
	Float,   // f32
	Vec2,
	Vec3,
	Vec4,
	Quat,
	String,  // std::string

	// 참조 (값 그대로 저장 — ID/핸들은 Engine이 관리하므로 Game.dll 리로드 후에도 같은 대상을 가리킨다)
	ObjectRef,      // ObjectRef, ComponentRef<T>
	MeshHandle,
	TextureHandle,
	ShaderHandle,
	MaterialHandle,
};

template <typename M> struct FieldTypeOf;
template <> struct FieldTypeOf<bool>        { static constexpr FieldType value = FieldType::Bool;   };
template <> struct FieldTypeOf<i32>         { static constexpr FieldType value = FieldType::Int;    };
template <> struct FieldTypeOf<u32>         { static constexpr FieldType value = FieldType::UInt;   };
template <> struct FieldTypeOf<f32>         { static constexpr FieldType value = FieldType::Float;  };
template <> struct FieldTypeOf<vec2>        { static constexpr FieldType value = FieldType::Vec2;   };
template <> struct FieldTypeOf<vec3>        { static constexpr FieldType value = FieldType::Vec3;   };
template <> struct FieldTypeOf<vec4>        { static constexpr FieldType value = FieldType::Vec4;   };
template <> struct FieldTypeOf<quat>        { static constexpr FieldType value = FieldType::Quat;   };
template <> struct FieldTypeOf<std::string> { static constexpr FieldType value = FieldType::String; };
template <> struct FieldTypeOf<ObjectRef>      { static constexpr FieldType value = FieldType::ObjectRef;      };
template <> struct FieldTypeOf<MeshHandle>     { static constexpr FieldType value = FieldType::MeshHandle;     };
template <> struct FieldTypeOf<TextureHandle>  { static constexpr FieldType value = FieldType::TextureHandle;  };
template <> struct FieldTypeOf<ShaderHandle>   { static constexpr FieldType value = FieldType::ShaderHandle;   };
template <> struct FieldTypeOf<MaterialHandle> { static constexpr FieldType value = FieldType::MaterialHandle; };
template <typename T> struct FieldTypeOf<ComponentRef<T>> { static constexpr FieldType value = FieldType::ObjectRef; }; // ObjectRef 하나만 가진다

struct FieldInfo
{
	std::string name;
	FieldType   type;
	u32         offset; // 컴포넌트 객체 시작 주소 기준
};

using ComponentFactory = Component* (*)();

struct ComponentTypeInfo
{
	std::string            name;
	ComponentTypeId        id      = 0;
	void*                  module  = nullptr; // 이 타입의 코드가 있는 모듈 (Game.dll 사본)
	ComponentFactory       factory = nullptr;
	std::vector<FieldInfo> fields;
};

// T::Reflect(TypeBuilder<T>&) 안에서 저장할 필드를 등록한다.
//   static void Reflect(TypeBuilder<Spinner>& t) { t.Field("speed", &Spinner::speed); }
template <typename T>
class TypeBuilder
{
public:
	explicit TypeBuilder(ComponentTypeInfo& info) : _info(info) {}

	template <typename M>
	TypeBuilder& Field(const char* name, M T::* member)
	{
		static_assert(sizeof(FieldTypeOf<M>) > 0, "Unsupported field type");

		// 멤버 포인터 → 오프셋. 객체를 만들지 않고 계산한다(가상 상속이 없는 클래스에서만 유효).
		alignas(T) unsigned char storage[sizeof(T)];
		const T* object = reinterpret_cast<const T*>(storage);
		const auto offset = reinterpret_cast<const unsigned char*>(&(object->*member)) - storage;

		_info.fields.push_back(FieldInfo{ name, FieldTypeOf<M>::value, static_cast<u32>(offset) });
		return *this;
	}

private:
	ComponentTypeInfo& _info;
};

// ---------------------------------------------------------------------------------------------
// 핫 리로드 스냅샷 (모듈 독립적인 값만 담는다)
// ---------------------------------------------------------------------------------------------
struct FieldSnapshot
{
	std::string     name;
	FieldType       type;
	std::vector<u8> bytes; // String이면 문자 데이터, 그 외에는 값의 메모리 그대로
};

struct ComponentSnapshot
{
	ObjectId                   owner = kInvalidObjectId; // 포인터 대신 ID (리로드 중 오브젝트가 사라져도 안전)
	u32                        index = 0;       // 오브젝트 안에서의 순서 (업데이트 순서 유지)
	bool                       enabled = true;
	std::string                typeName;
	std::vector<FieldSnapshot> fields;
};

// Game 컴포넌트 타입 레지스트리 + 핫 리로드 직렬화.
//
// 리로드 흐름 (GameHost가 호출)
//   1) SnapshotAndRelease(module): 그 모듈의 컴포넌트를 등록된 필드로 직렬화한 뒤 제거하고,
//      그 모듈의 타입 등록을 해제한다.
//   2) 새 DLL의 GameRegisterTypes가 타입을 다시 등록한다.
//   3) RestoreSnapshots(): 타입 이름으로 새 팩토리를 찾아 다시 만들고, 필드를 이름으로 복원한다.
//      - 새로 추가된 필드: 생성자 기본값 유지
//      - 삭제된 필드: 무시
//      - 타입이 바뀐 필드: 숫자끼리는 변환(int↔float 등), 그 외는 기본값 유지
//      - 등록되지 않은 타입: 상태를 보존할 수 없으므로 경고 후 제거
//      - ObjectRef/핸들 필드: ID 값 그대로 복원 → 같은 오브젝트/리소스를 계속 가리킨다
class ENGINE_API ComponentRegistry
{
public:
	ComponentRegistry();
	~ComponentRegistry();

	ComponentRegistry(const ComponentRegistry&) = delete;
	ComponentRegistry& operator=(const ComponentRegistry&) = delete;

	// Game.dll의 GameRegisterTypes 안에서 호출. module 기본값은 호출한 모듈.
	template <typename T>
	void Register(void* module = GetCurrentModuleHandle())
	{
		static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
		static_assert(HasOwnComponentType<T>, "T must declare COMPONENT_TYPE(T, Base)");
		static_assert(std::is_default_constructible_v<T>, "Registered components need a default constructor");

		ComponentTypeInfo info;
		info.name = T::StaticTypeName();
		info.id   = T::StaticTypeId();

		if constexpr (IsEngineComponent<T>::value)
		{
			// Engine 컴포넌트는 Engine.dll 안에서 생성해야 vtable이 Engine을 가리킨다
			info.module  = nullptr;
			info.factory = []() -> Component* { return T::CreateInstance(); };
		}
		else
		{
			info.module  = module;
			info.factory = []() -> Component* { return new T(); };
		}

		if constexpr (requires(TypeBuilder<T>& builder) { T::Reflect(builder); })
		{
			TypeBuilder<T> builder(info);
			T::Reflect(builder);
		}

		RegisterType(std::move(info));
	}

	void RegisterType(ComponentTypeInfo info);
	void UnregisterModule(void* module);

	const ComponentTypeInfo* Find(const std::string& name) const;
	const ComponentTypeInfo* Find(ComponentTypeId id) const;
	u32 GetTypeCount() const;

	// 등록된 모든 타입 (이름순). 포인터는 다음 등록/해제(리로드) 전까지만 유효.
	std::vector<const ComponentTypeInfo*> GetTypes() const;

	// module이 정의한 컴포넌트를 scene에서 제거한다. keepSnapshot이면 제거 전에 직렬화해 둔다.
	// 제거한 컴포넌트 수를 반환.
	u32 SnapshotAndRelease(Scene* scene, void* module, bool keepSnapshot);

	// 보관한 스냅샷을 현재 등록된 타입으로 다시 만든다. 복원한 컴포넌트 수를 반환.
	u32 RestoreSnapshots();

	u32 GetPendingSnapshotCount() const;

private:
	std::unordered_map<ComponentTypeId, ComponentTypeInfo> _types;
	std::vector<ComponentSnapshot> _snapshots;
};
