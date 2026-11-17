#pragma once

#include "EngineAPI.h"
#include "Types.h"

// ---------------------------------------------------------------------------------------------
// 오브젝트 ID / 리소스 핸들
//
// 원시 포인터 대신 이 값들을 저장하면
//  - 대상이 삭제된 뒤에 접근해도 크래시 대신 nullptr을 얻는다 (dangling 포인터 방지)
//  - 메모리 주소가 아니라 값이므로 핫 리로드 스냅샷(그리고 나중에 씬 저장)에 그대로 기록할 수 있다
//  - 리소스를 다시 로드해도(5단계 에셋 핫 리로드) 핸들은 그대로 두고 내용만 바꿀 수 있다
// ---------------------------------------------------------------------------------------------

// GameObject 고유 ID. 프로세스 동안 재사용하지 않는다. 0은 "없음".
using ObjectId = u64;
inline constexpr ObjectId kInvalidObjectId = 0;

class GameObject;

// 다른 GameObject를 가리키는 약한 참조.
// 대상이 삭제됐거나 삭제 예약 상태면 Get()이 nullptr을 반환한다.
struct ENGINE_API ObjectRef
{
	ObjectId id = kInvalidObjectId;

	ObjectRef() = default;
	explicit ObjectRef(ObjectId objectId) : id(objectId) {}
	ObjectRef(const GameObject* object); // nullptr 허용

	GameObject* Get() const;
	bool IsAlive() const { return Get() != nullptr; }
	bool IsSet() const { return id != kInvalidObjectId; }
	void Reset() { id = kInvalidObjectId; }

	bool operator==(const ObjectRef&) const = default;
};

// 세대(generation) 기반 리소스 핸들. 슬롯이 해제되면 세대가 올라가므로 이전 핸들은 무효가 된다.
// generation 0은 "없음".
template <typename T>
struct Handle
{
	u32 index      = 0;
	u32 generation = 0;

	constexpr bool IsValid() const noexcept { return generation != 0; }
	constexpr explicit operator bool() const noexcept { return IsValid(); }
	constexpr bool operator==(const Handle&) const = default;
};

class Mesh;
class Texture;
class Shader;
class Material;

using MeshHandle     = Handle<Mesh>;
using TextureHandle  = Handle<Texture>;
using ShaderHandle   = Handle<Shader>;
using MaterialHandle = Handle<Material>;
