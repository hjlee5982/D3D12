#pragma once

#include "EngineAPI.h"
#include "Types.h"

#include <type_traits>

class GameObject;
template <typename T> class TypeBuilder;

// 링커가 모든 모듈(EXE/DLL)에 만들어 주는 심볼. 주소 = 그 모듈의 HMODULE.
// inline 함수 안에서 쓰면 "이 코드가 컴파일된 모듈"을 얻을 수 있다.
extern "C" struct _IMAGE_DOS_HEADER __ImageBase;

inline void* GetCurrentModuleHandle() noexcept
{
	return &__ImageBase;
}

// ---------------------------------------------------------------------------------------------
// 컴포넌트 타입 ID
//
// dynamic_cast 대신 "타입 이름의 해시"로 타입을 식별한다.
//  - 이름 기반이라 Game.dll을 다시 빌드/리로드해도 같은 타입은 같은 ID를 가진다
//    (RTTI type_info는 DLL마다 따로 존재)
//  - 이름으로 타입을 찾을 수 있어 직렬화/레지스트리/에디터에서 쓸 수 있다
//  - 문자열 비교가 일어나는 DLL 간 dynamic_cast보다 빠르다
// ---------------------------------------------------------------------------------------------
using ComponentTypeId = u64;

constexpr ComponentTypeId HashComponentTypeName(const char* name) noexcept
{
	u64 hash = 14695981039346656037ull; // FNV-1a 64
	while (*name != '\0')
	{
		hash ^= static_cast<u8>(*name++);
		hash *= 1099511628211ull;
	}
	return hash;
}

// 모든 컴포넌트 클래스 본문 맨 위에 선언한다.  예) COMPONENT_TYPE(Spinner, Component)
// GetComponent<부모타입>()으로 자식 타입도 찾을 수 있도록 IsA가 부모 체인을 따라간다.
#define COMPONENT_TYPE(Type, BaseType)                                                              \
public:                                                                                             \
	using Super    = BaseType;                                                                     \
	using ThisType = Type;                                                                         \
	static constexpr const char*     StaticTypeName() noexcept { return #Type; }                   \
	static constexpr ComponentTypeId StaticTypeId() noexcept { return HashComponentTypeName(#Type); } \
	const char*     GetTypeName() const override { return #Type; }                                 \
	ComponentTypeId GetTypeId() const override { return StaticTypeId(); }                          \
	bool IsA(ComponentTypeId id) const override { return id == StaticTypeId() || Super::IsA(id); } \
private:

// T가 COMPONENT_TYPE을 직접 선언했는지 (선언하지 않으면 부모의 ID를 물려받아 엉뚱한 타입과 매칭된다)
template <typename T>
inline constexpr bool HasOwnComponentType = std::is_same_v<typename T::ThisType, T>;

// Engine.dll에 정의된 컴포넌트 표시. 이 표시가 없는 타입은 AddComponent를 호출한 모듈(Game.dll 등)의
// 소유로 기록되어, 그 DLL을 내릴 때 직렬화 후 제거되고 새 DLL에서 복원된다.
// 상속되지 않는다: Game에서 MeshRenderer를 상속한 클래스는 Game 소유로 올바르게 분류된다.
//
// 표시한 타입은 Engine.dll 안에서 객체를 만드는 static CreateInstance(...)를 생성자마다 제공해야 한다.
// MSVC는 inline 가상 함수가 있는 dllimport 클래스의 vtable 사본을 사용하는 모듈마다 만들기 때문에,
// Game.dll 코드에서 new 하면 객체의 vtable 포인터가 Game.dll 메모리를 가리키게 되고
// Game.dll을 내리는 순간 그 객체를 쓰면 크래시가 난다.
template <typename T>
struct IsEngineComponent : std::false_type {};

#define ENGINE_COMPONENT(Type) \
	template <> struct IsEngineComponent<Type> : std::true_type {}

// 편집 모드(재생 전)에서도 Update/LateUpdate를 받으려면 클래스 본문에 EXECUTE_IN_EDIT_MODE()를 둔다.
// (기본: 재생 중에만 동작. 유니티의 [ExecuteInEditMode]와 같은 역할)
#define EXECUTE_IN_EDIT_MODE() public: static constexpr bool kExecuteInEditMode = true; private:

template <typename T>
constexpr bool ExecutesInEditMode()
{
	if constexpr (requires { T::kExecuteInEditMode; })
	{
		return T::kExecuteInEditMode;
	}
	else
	{
		return false;
	}
}

class ENGINE_API Component
{
public:
	using ThisType = Component;
	static constexpr const char*     StaticTypeName() noexcept { return "Component"; }
	static constexpr ComponentTypeId StaticTypeId() noexcept { return HashComponentTypeName("Component"); }

	Component() = default;
	virtual ~Component() = default;

	Component(const Component&) = delete;
	Component& operator=(const Component&) = delete;

	void SetOwner(GameObject* owner);
	GameObject* GetOwner() const;

	bool IsEnabled() const;
	void SetEnabled(bool enabled);

	// 이 컴포넌트의 코드가 있는 모듈. nullptr이면 Engine 소유(DLL 리로드와 무관하게 유지).
	void* GetDefiningModule() const;

	// 반환된 문자열은 컴포넌트를 정의한 모듈 안에 있으므로 보관하려면 복사해야 한다.
	virtual const char*     GetTypeName() const { return StaticTypeName(); }
	virtual ComponentTypeId GetTypeId() const { return StaticTypeId(); }
	virtual bool            IsA(ComponentTypeId id) const { return id == StaticTypeId(); }

	virtual void OnCreate() {}

	// 첫 Update 직전에 한 번 (재생을 시작하거나, 재생 중에 추가된 뒤 첫 프레임).
	// 편집 모드에서는 EXECUTE_IN_EDIT_MODE 컴포넌트만 호출된다.
	virtual void Start() {}
	bool HasStarted() const { return _started; }

	virtual void Update(f32 deltaTime) {}
	virtual void LateUpdate(f32 deltaTime) {}
	virtual void OnDestroy() {}

	// 핫 리로드: 이전 DLL에서 직렬화 직전 / 새 DLL에서 필드 복원 직후에 호출된다.
	// (리로드는 객체의 삭제·생성이 아니므로 OnDestroy/OnCreate는 호출되지 않는다)
	// 레지스트리에 등록되지 않은 값(포인터, 캐시 등)은 OnAfterReload에서 다시 구성한다.
	virtual void OnBeforeReload() {}
	virtual void OnAfterReload() {}

protected:
	GameObject* _owner = nullptr;
	bool _enabled = true;

private:
	friend class GameObject;
	friend class ComponentRegistry;
	void* _definingModule = nullptr;
	bool  _started        = false; // Start 호출 여부
	bool  _runInEditMode  = false; // EXECUTE_IN_EDIT_MODE (생성 시 타입 정보로 설정)
};
